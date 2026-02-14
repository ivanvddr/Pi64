/*
 * Questo file fa parte del progetto Pi64.
 *
 * Copyright (C) 2025 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 */
#include "SID.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

const float MASTER_VOLUME = 0.3f;

// rate periods (invariati)
const uint16_t SID::RATE_PERIODS[16] = {
    9, 32, 63, 95, 149, 220, 267, 313,
    392, 977, 1954, 3126, 3907, 11720, 19532, 31251
};

// costruttore
SID::SID() { 
    currentQuality = QUALITY_FULL;  // default: massima qualita
    syncInterval = 22;
    reset(); 
}

void SID::reset() {
    for (int i = 0; i < 3; ++i) {
        v[i] = Voice();
        v[i].lfsr = 0x7FFFFF;
        v[i].envState = 3;
    }
    
    filter = FilterState();
    fcLo = fcHi = resFilt = modeVol = 0;
    sampleRate = SID_SAMPLE_RATE;
    PHASE_K = 0;
    filter_w = 0;
    slice_num = 0;
    
    aa_x1 = aa_x2 = aa_y1 = aa_y2 = 0;
    dither_lfsr = 0x12345678;
}

void SID::begin(uint32_t fs) {
    sampleRate = fs ? fs : SID_SAMPLE_RATE;
    PHASE_K = ((uint64_t)C64_CLOCK_HZ << 16) / (uint64_t)sampleRate;
    
    for (int i = 0; i < 16; ++i) {
        uint32_t cycles = RATE_PERIODS[i];
        uint32_t samples = (cycles * sampleRate) / C64_CLOCK_HZ;
        if (samples == 0) samples = 1;
        
        atkStep[i] = 4095 / samples;
        decRelStep[i] = 4095 / samples;
        
        if (atkStep[i] == 0) atkStep[i] = 1;
        if (decRelStep[i] == 0) decRelStep[i] = 1;
    }
    
    for (int i = 0; i < 3; ++i) recalcPhaseInc(i);
    updateFilterCoeff();
    
    Serial.printf("SID: %lu Hz, Quality=%d\n", sampleRate, currentQuality);
}

void SID::initPWM(uint8_t audio_pin) {
    gpio_set_function(audio_pin, GPIO_FUNC_NULL);
    gpio_init(audio_pin);
    gpio_set_dir(audio_pin, GPIO_OUT);
    gpio_put(audio_pin, 0);
    busy_wait_us(100);
    
    gpio_set_function(audio_pin, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(audio_pin);
    
    pwm_set_wrap(slice_num, 511);
    pwm_set_clkdiv(slice_num, 1.0f);
    pwm_set_gpio_level(audio_pin, 256);
    pwm_set_enabled(slice_num, true);
    
    Serial.printf("PWM init: GPIO%d slice %d\n", audio_pin, slice_num);
}

// cambia modalita runtime
void SID::setQualityMode(QualityMode mode) {
    if (currentQuality == mode) return;
    
    currentQuality = mode;
    
    // Adatta intervallo sync
    switch (mode) {
        case QUALITY_FULL:
            syncInterval = 22;
            Serial.println("SID: QUALITY_FULL (max fidelity)");
            break;
        case QUALITY_BALANCED:
            syncInterval = 44;
            Serial.println("SID: QUALITY_BALANCED (optimized)");
            break;
        case QUALITY_FAST:
            syncInterval = 44;
            Serial.println("SID: QUALITY_FAST (minimum overhead)");
            break;
    }
}

// chiama versione appropriata alla modalita corrente
int16_t SID::audioTick() {
    switch (currentQuality) {
        case QUALITY_FULL:
            return audioTick_Full();
        case QUALITY_BALANCED:
            return audioTick_Balanced();
        case QUALITY_FAST:
            return audioTick_Fast();
        default:
            return audioTick_Full();
    }
}


// VERSIONE 1: full
int16_t SID::audioTick_Full() {
    // Sync ogni 22 sample
    static uint8_t sync_counter = 0;
    if (++sync_counter >= syncInterval ) {
        sync_counter = 0;
        syncFromSharedRegs();
    }
    
    // rendering voci
    int16_t tri0 = (int16_t)triWave(v[0].phase) - 2048;
    int16_t tri1 = (int16_t)triWave(v[1].phase) - 2048;
    int16_t tri2 = (int16_t)triWave(v[2].phase) - 2048;
    
    int16_t o0 = renderVoice(0, tri2, false);
    int16_t o1 = renderVoice(1, tri0, false);
    int16_t o2 = renderVoice(2, tri1, false);
    
    o0 = o0 - (o0 >> 2);
    o1 = o1 - (o1 >> 2);
    o2 = o2 - (o2 >> 2);
    
    // mix con filtro completo
    uint8_t filtMask = resFilt & 0x0F;
    int32_t filtered = 0;
    int32_t unfiltered = 0;
    
    if (filtMask & 0x01) filtered += o0; else unfiltered += o0;
    if (filtMask & 0x02) filtered += o1; else unfiltered += o1;
    if (filtMask & 0x04) filtered += o2; else unfiltered += o2;
    
    int16_t filteredOut = applyMultiFilter_Full(filtered);
    int32_t finalMix = (int32_t)filteredOut + unfiltered;
    
    // volume
    uint8_t vol = modeVol & 0x0F;
    finalMix = (finalMix * (int32_t)vol) >> 4;
    
    // Soft limiter
    if (finalMix > 26000) {
        int32_t over = finalMix - 26000;
        finalMix = 26000 + (over >> 2);
    } else if (finalMix < -26000) {
        int32_t over = finalMix + 26000;
        finalMix = -26000 + (over >> 2);
    }
    
    if (finalMix > 30000) finalMix = 30000;
    if (finalMix < -30000) finalMix = -30000;
    
    int16_t sample = (int16_t)finalMix;
    
    // anti-aliasing (solo FULL)
    sample = applyAntiAliasing(sample);
    
    // PWM output
    dither_lfsr ^= (dither_lfsr << 13);
    dither_lfsr ^= (dither_lfsr >> 17);
    dither_lfsr ^= (dither_lfsr << 5);
    int16_t dither = ((dither_lfsr & 0xFF) >> 1) - 64;
    
    int32_t pwm_val = 256 + ((int32_t)(sample * MASTER_VOLUME + dither) >> 7);
    if (pwm_val < 16) pwm_val = 16;
    if (pwm_val > 495) pwm_val = 495;
    
    pwm_set_gpio_level(AUDIO_PIN, (uint16_t)pwm_val);
    
    return sample;
}

// VERSIONE 2: bilanciato (filtro 32-bit, sync ridotto)
int16_t SID::audioTick_Balanced() {
    // Sync ogni 44 sample
    static uint8_t sync_counter = 0;
    if (++sync_counter >= syncInterval ) {
        sync_counter = 0;
        syncFromSharedRegs();
    }
    
    // rendering voci (invariato)
    int16_t tri0 = (int16_t)triWave(v[0].phase) - 2048;
    int16_t tri1 = (int16_t)triWave(v[1].phase) - 2048;
    int16_t tri2 = (int16_t)triWave(v[2].phase) - 2048;
    
    int16_t o0 = renderVoice(0, tri2, false);
    int16_t o1 = renderVoice(1, tri0, false);
    int16_t o2 = renderVoice(2, tri1, false);
    
    o0 = o0 - (o0 >> 2);
    o1 = o1 - (o1 >> 2);
    o2 = o2 - (o2 >> 2);
    
    // filtro semplificato (32-bit)
    uint8_t filtMask = resFilt & 0x0F;
    int32_t filtered = 0;
    int32_t unfiltered = 0;
    
    if (filtMask & 0x01) filtered += o0; else unfiltered += o0;
    if (filtMask & 0x02) filtered += o1; else unfiltered += o1;
    if (filtMask & 0x04) filtered += o2; else unfiltered += o2;
    
    int16_t filteredOut = applyMultiFilter_Balanced(filtered);
    int32_t finalMix = (int32_t)filteredOut + unfiltered;
    
    // Volume
    uint8_t vol = modeVol & 0x0F;
    finalMix = (finalMix * (int32_t)vol) >> 4;
    
    if (finalMix > 30000) finalMix = 30000;
    if (finalMix < -30000) finalMix = -30000;
    
    int16_t sample = (int16_t)finalMix;

    // PWM output (semplificato)
    dither_lfsr ^= (dither_lfsr << 13);
    int16_t dither = ((dither_lfsr >> 16) & 0x7F) - 64;
    
    int32_t pwm_val = 256 + ((int32_t)(sample * MASTER_VOLUME / 2 + dither) >> 7);
    if (pwm_val < 16) pwm_val = 16;
    if (pwm_val > 495) pwm_val = 495;
    
    pwm_set_gpio_level(AUDIO_PIN, (uint16_t)pwm_val);
    
    return sample;
}


// VERSIONE 3: fast (minimo overhead)
int16_t SID::audioTick_Fast() {
    // Sync ogni 44 sample
    static uint8_t sync_counter = 0;
    if (++sync_counter >= syncInterval ) {
        sync_counter = 0;
        syncFromSharedRegs();
    }
    
    // rendering voci
    int16_t tri0 = (int16_t)triWave(v[0].phase) - 2048;
    int16_t tri1 = (int16_t)triWave(v[1].phase) - 2048;
    int16_t tri2 = (int16_t)triWave(v[2].phase) - 2048;
    
    int16_t o0 = renderVoice(0, tri2, false);
    int16_t o1 = renderVoice(1, tri0, false);
    int16_t o2 = renderVoice(2, tri1, false);
    
    o0 -= (o0 >> 2);
    o1 -= (o1 >> 2);
    o2 -= (o2 >> 2);
    
    // mix diretto (filtro semplificato)
    int32_t finalMix = (int32_t)o0 + o1 + o2;
    
    uint8_t filtMask = resFilt & 0x0F;
    if (filtMask != 0) {
        finalMix = applyFilter_Fast(finalMix);
    }
    
    // Volume
    uint8_t vol = modeVol & 0x0F;
    finalMix = (finalMix * vol) >> 4;
    
    if (finalMix > 30000) finalMix = 30000;
    if (finalMix < -30000) finalMix = -30000;
    
    int16_t sample = (int16_t)finalMix;
    
    // PWM output (minimale)
    dither_lfsr ^= (dither_lfsr << 13);
    int16_t dither = ((dither_lfsr >> 16) & 0x7F) - 64;
    
    int32_t pwm_val = 256 + ((int32_t)(sample  * MASTER_VOLUME / 2 + dither) >> 7);
    if (pwm_val < 16) pwm_val = 16;
    if (pwm_val > 495) pwm_val = 495;
    
    pwm_set_gpio_level(AUDIO_PIN, (uint16_t)pwm_val);
    
    return sample;
}

// ============================================
// filtri - 3 VERSIONI
// ============================================

// VERSIONE FULL (con 64-bit math)
int16_t SID::applyMultiFilter_Full(int32_t sum) {
    uint8_t filtMask = resFilt & 0x0F;
    
    bool lp = (modeVol & 0x10) != 0;
    bool bp = (modeVol & 0x20) != 0;
    bool hp = (modeVol & 0x40) != 0;
    bool useFilter = (lp || bp || hp) && (filtMask != 0);
    
    if (!useFilter) {
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        return (int16_t)sum;
    }
    
    int32_t input = sum >> 3;
    int32_t f = filter_w;
    
    const int32_t STATE_LIMIT = 50000;
    if (filter.vbp > STATE_LIMIT) filter.vbp = STATE_LIMIT;
    if (filter.vbp < -STATE_LIMIT) filter.vbp = -STATE_LIMIT;
    if (filter.vlp > STATE_LIMIT) filter.vlp = STATE_LIMIT;
    if (filter.vlp < -STATE_LIMIT) filter.vlp = -STATE_LIMIT;
    
    filter.hp = input - (filter.vlp >> 3) - (filter.vbp >> 3);
    if (filter.hp > 32767) filter.hp = 32767;
    if (filter.hp < -32768) filter.hp = -32768;
    
    int64_t bp_increment = ((int64_t)filter.hp * (int64_t)f) >> 14;
    if (bp_increment > 8192) bp_increment = 8192;
    if (bp_increment < -8192) bp_increment = -8192;
    filter.vbp += (int32_t)bp_increment;
    
    int64_t lp_increment = ((int64_t)filter.vbp * (int64_t)f) >> 14;
    if (lp_increment > 8192) lp_increment = 8192;
    if (lp_increment < -8192) lp_increment = -8192;
    filter.vlp += (int32_t)lp_increment;
    
    int32_t output = 0;
    if (lp) output += filter.vlp >> 3;
    if (bp) output += filter.vbp >> 3;
    if (hp) output += filter.hp;
    
    uint8_t res = (resFilt >> 4) & 0x0F;
    if (res > 0) {
        int32_t fb = (filter.vbp * res) >> 7;
        if (fb > 4096) fb = 4096;
        if (fb < -4096) fb = -4096;
        output += fb;
    }
    
    output = output << 2;
    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    
    return (int16_t)output;
}

// VERSIONE BALANCED (32-bit math)
int16_t SID::applyMultiFilter_Balanced(int32_t sum) {
    uint8_t filtMask = resFilt & 0x0F;
    
    bool lp = (modeVol & 0x10) != 0;
    bool bp = (modeVol & 0x20) != 0;
    bool hp = (modeVol & 0x40) != 0;
    bool useFilter = (lp || bp || hp) && (filtMask != 0);
    
    if (!useFilter) {
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        return (int16_t)sum;
    }
    
    int32_t input = sum >> 3;
    int32_t f = filter_w;
    
    // State limiting
    const int32_t STATE_LIMIT = 40000;
    if (filter.vbp > STATE_LIMIT) filter.vbp = STATE_LIMIT;
    if (filter.vbp < -STATE_LIMIT) filter.vbp = -STATE_LIMIT;
    if (filter.vlp > STATE_LIMIT) filter.vlp = STATE_LIMIT;
    if (filter.vlp < -STATE_LIMIT) filter.vlp = -STATE_LIMIT;
    
    // ⚡ 32-bit math (invece di 64-bit)
    filter.hp = input - (filter.vlp >> 4) - (filter.vbp >> 4);
    
    int32_t bp_inc = (filter.hp * f) >> 14;
    filter.vbp += bp_inc;
    
    int32_t lp_inc = (filter.vbp * f) >> 14;
    filter.vlp += lp_inc;
    
    int32_t output = 0;
    if (lp) output += filter.vlp >> 4;
    if (bp) output += filter.vbp >> 4;
    if (hp) output += filter.hp;
    
    // Resonance semplificata
    uint8_t res = (resFilt >> 4) & 0x0F;
    if (res > 0) {
        int32_t fb = (filter.vbp * res) >> 8;
        if (fb > 4096) fb = 4096;
        if (fb < -4096) fb = -4096;
        output += fb;
    }
    
    output = output << 2;
    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    
    return (int16_t)output;
}

// VERSIONE FAST (single-pass lowpass)
int16_t SID::applyFilter_Fast(int32_t sum) {
    // Filtro semplice single-pole lowpass
    static int32_t lp_state = 0;
    
    int32_t input = sum >> 2;
    int32_t coeff = filter_w >> 2;
    
    // Simple LP: y[n] = y[n-1] + k * (x[n] - y[n-1])
    lp_state += ((input - lp_state) * coeff) >> 12;
    
    int32_t output = lp_state << 2;
    
    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    
    return (int16_t)output;
}

// ============================================
// ADSR - 2 VERSIONI
// ============================================

// VERSIONE NORMALE
void SID::updateADSR(int i) {
    Voice& vv = v[i];
    uint8_t A = (vv.ad >> 4) & 0x0F;
    uint8_t D = (vv.ad >> 0) & 0x0F;
    uint8_t S = (vv.sr >> 4) & 0x0F;
    uint8_t R = (vv.sr >> 0) & 0x0F;
    
    uint16_t sustainLevel = (uint16_t)S * 256;
    
    uint16_t rate_period = 1;
    switch (vv.envState) {
        case 0: rate_period = RATE_PERIODS[A] / 100; break;
        case 1: rate_period = RATE_PERIODS[D] / 50; break;
        case 3: rate_period = RATE_PERIODS[R] / 50; break;
    }
    
    if (++vv.rate_counter < rate_period) return;
    vv.rate_counter = 0;
    
    switch (vv.envState) {
        case 0: // Attack
            if (vv.env < 4095) {
                vv.env += atkStep[A];
                if (vv.env >= 4095) {
                    vv.env = 4095;
                    vv.envState = 1;
                }
            }
            break;
        case 1: // Decay
            if (vv.env > sustainLevel) {
                uint16_t step = decRelStep[D];
                vv.env = (vv.env > step) ? (vv.env - step) : sustainLevel;
                if (vv.env <= sustainLevel) {
                    vv.envState = 2;
                }
            }
            break;
        case 2: // Sustain
            vv.env = sustainLevel;
            break;
        case 3: // Release
            if (vv.env > 0) {
                uint16_t step = decRelStep[R];
                vv.env = (vv.env > step) ? (vv.env - step) : 0;
            }
            break;
    }
}

// VERSIONE FAST (early exit su sustain)
void SID::updateADSR_Fast(int i) {
    Voice& vv = v[i];
    
    // early exit se in sustain stabile
    if (vv.envState == 2) {
        uint16_t sustainLevel = ((uint16_t)(vv.sr >> 4) & 0x0F) * 256;
        vv.env = sustainLevel;
        return;
    }
    
    // Altrimenti chiamare versione normale
    updateADSR(i);
}

// RENDERING VOICE (usa ADSR appropriato)
int16_t SID::renderVoice(int i, int16_t modTri, bool modMSBRise) {
    Voice& vv = v[i];
    
    if ((vv.ctrl & 0xF0) == 0) return 0;
    
    uint8_t oldMSB = (vv.phase >> 23) & 1;
    
    if ((vv.ctrl & 0x02) && modMSBRise) {
        vv.phase = 0;
    }
    
    vv.phase += vv.inc;
    uint8_t newMSB = (vv.phase >> 23) & 1;
    bool triMSBRise = (!oldMSB && newMSB);
    
    uint16_t wave = 0;
    bool useNoise = (vv.ctrl & 0x80) != 0;
    
    if (useNoise && triMSBRise) {
        wave = noiseWave(vv);
    } else {
        wave = combinedWave(vv.phase, vv.pwReg, vv.ctrl);
        
        if ((vv.ctrl & 0x04) && (vv.ctrl & 0x10)) {
            uint16_t modMask = (modTri < 0) ? 0x0000 : 0x0FFF;
            wave ^= modMask;
        }
    }
    
    // usa ADSR appropriato per modalita
    if (currentQuality == QUALITY_FULL) {
        updateADSR(i);
    } else {
        updateADSR_Fast(i);
    }
    
    int16_t dacOut = waveToDac(wave);
    int32_t amplitude = ((int32_t)dacOut * (int32_t)vv.env) >> 12;
    
    if (amplitude > 32767) amplitude = 32767;
    if (amplitude < -32768) amplitude = -32768;
    
    return (int16_t)amplitude;
}

// medoti comuni

void SID::recalcPhaseInc(int i) {
    uint64_t temp = (uint64_t)v[i].freqReg * PHASE_K;
    v[i].inc = (uint32_t)(temp >> 16);
}

void SID::updateFilterCoeff() {
    uint16_t fc = ((uint16_t)fcLo & 0x07) | ((uint16_t)fcHi << 3);
    float f = 30.0f + (12000.0f - 30.0f) * (float)fc / 2047.0f;
    float w = 2.0f * 3.14159265f * f / (float)sampleRate;
    if (w > 1.5f) w = 1.5f;
    if (w < 0.01f) w = 0.01f;
    filter_w = (uint16_t)(w * 16384.0f);
}

void SID::syncFromSharedRegs() {
    uint32_t changed = sid_get_and_clear_changed();
    if (!changed) return;
    
    for (int vi = 0; vi < 3; vi++) {
        Voice& vv = v[vi];
        int base = vi * 7;
        
        bool voiceChanged = false;
        for (int r = 0; r < 7; r++) {
            if (changed & (1UL << (base + r))) {
                voiceChanged = true;
                break;
            }
        }
        
        if (!voiceChanged) continue;
        
        uint16_t newFreq = sid_read_register_core1(base + 0) | 
                          ((uint16_t)sid_read_register_core1(base + 1) << 8);
        if (newFreq != vv.freqReg) {
            vv.freqReg = newFreq;
            recalcPhaseInc(vi);
        }
        
        uint8_t oldGate = vv.ctrl & 0x01;
        uint8_t newCtrl = sid_read_register_core1(base + 4);
        if (newCtrl != vv.ctrl) {
            vv.ctrl = newCtrl;
        }
        
        uint8_t newGate = vv.ctrl & 0x01;
        if (!oldGate && newGate) {
            vv.envState = 0;
            vv.rate_counter = 0;
        } else if (oldGate && !newGate) {
            vv.envState = 3;
            vv.rate_counter = 0;
        }
        
        vv.pwReg = sid_read_register_core1(base + 2) | 
                   ((uint16_t)(sid_read_register_core1(base + 3) & 0x0F) << 8);
        vv.ad = sid_read_register_core1(base + 5);
        vv.sr = sid_read_register_core1(base + 6);
    }
    
    if (changed & (0xFUL << 0x15)) {
        uint8_t newFcLo = sid_read_register_core1(0x15) & 0x07;
        uint8_t newFcHi = sid_read_register_core1(0x16);
        if (newFcLo != fcLo || newFcHi != fcHi) {
            fcLo = newFcLo;
            fcHi = newFcHi;
            updateFilterCoeff();
        }
        
        resFilt = sid_read_register_core1(0x17);
        modeVol = sid_read_register_core1(0x18);
    }
}

inline uint16_t SID::triWave(uint32_t ph) const {
    uint16_t x = (ph >> 12) & 0x0FFF;
    return (ph & 0x800000) ? ((~x) & 0x0FFF) : x;
}

inline uint16_t SID::sawWave(uint32_t ph) const {
    return (ph >> 12) & 0x0FFF;
}

inline uint16_t SID::pulseWave(uint32_t ph, uint16_t pw) const {
    uint16_t x = (ph >> 12) & 0x0FFF;
    return (x < (pw & 0x0FFF)) ? 4095 : 0;
}

uint16_t SID::noiseWave(Voice& vv) const {
    uint32_t b22 = (vv.lfsr >> 22) & 1;
    uint32_t b17 = (vv.lfsr >> 17) & 1;
    uint32_t fb = b22 ^ b17;
    vv.lfsr = ((vv.lfsr << 1) | fb) & 0x7FFFFF;
    
    uint16_t out = 0;
    uint32_t bits = vv.lfsr;
    out |= ((bits >> 22) & 1) << 11;
    out |= ((bits >> 20) & 1) << 10;
    out |= ((bits >> 16) & 1) << 9;
    out |= ((bits >> 13) & 1) << 8;
    out |= ((bits >> 11) & 1) << 7;
    out |= ((bits >> 7) & 1) << 6;
    out |= ((bits >> 4) & 1) << 5;
    out |= ((bits >> 2) & 1) << 4;
    out |= ((bits >> 1) & 1) << 3;
    out |= ((bits >> 0) & 1) << 2;
    
    return out & 0x0FFF;
}

uint16_t SID::combinedWave(uint32_t ph, uint16_t pw, uint8_t ctrl) const {
    bool useTri = (ctrl & 0x10) != 0;
    bool useSaw = (ctrl & 0x20) != 0;
    bool usePul = (ctrl & 0x40) != 0;
    bool useNoi = (ctrl & 0x80) != 0;
    
    uint8_t waveCount = useTri + useSaw + usePul + useNoi;
    if (waveCount == 0) return 0;
    if (waveCount == 1) {
        if (useTri) return triWave(ph);
        if (useSaw) return sawWave(ph);
        if (usePul) return pulseWave(ph, pw);
        return 0;
    }
    
    uint16_t result = 0;
    
    if (useTri && useSaw) {
        result = triWave(ph) & sawWave(ph);
    } else if (useTri && usePul) {
        result = triWave(ph) & pulseWave(ph, pw);
    } else if (useSaw && usePul) {
        uint16_t saw = sawWave(ph);
        uint16_t pul = pulseWave(ph, pw);
        result = saw | (pul & 0xF00);
    } else {
        uint16_t sum = 0;
        if (useTri) sum += triWave(ph);
        if (useSaw) sum += sawWave(ph);
        if (usePul) sum += pulseWave(ph, pw);
        result = sum / waveCount;
    }
    
    return result & 0x0FFF;
}

inline int16_t SID::waveToDac(uint16_t wave) const {
    uint16_t index = wave;
    uint32_t dac_value = DAC_TABLE[index];
    int32_t scaled = (int32_t)(dac_value >> 2);
    scaled -= 16384;
    if (scaled > 32767) scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    return (int16_t)scaled;
}

inline int16_t SID::applyAntiAliasing(int16_t input) {
    const int16_t B0 = 4022;
    const int16_t B1 = 8044;
    const int16_t B2 = 4022;
    const int16_t A1 = 7912;
    const int16_t A2 = -7602;
    
    int32_t scaled_input = (int32_t)input >> 1;
    
    int32_t acc = (int32_t)B0 * scaled_input;
    acc += (int32_t)B1 * (aa_x1 >> 1);
    acc += (int32_t)B2 * (aa_x2 >> 1);
    acc -= (int32_t)A1 * (aa_y1 >> 1);
    acc -= (int32_t)A2 * (aa_y2 >> 1);
    
    acc = acc >> 14;
    if (acc > 32767) acc = 32767;
    if (acc < -32768) acc = -32768;
    
    int16_t y0 = (int16_t)acc;
    
    aa_x2 = aa_x1;
    aa_x1 = input;
    aa_y2 = aa_y1;
    aa_y1 = y0;
    
    return y0;
}