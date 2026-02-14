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
#ifndef SID_H
#define SID_H

#include "configs.h"
#include <stdint.h>
#include "SID_shared.h"
#include <Arduino.h>


struct Voice {
    uint32_t phase;
    uint32_t inc;
    uint16_t freqReg;
    uint16_t pwReg;
    uint8_t ctrl;
    uint8_t ad;
    uint8_t sr;
    uint16_t env;
    uint8_t envState;
    uint16_t rate_counter;
    mutable uint32_t lfsr;
};

struct FilterState {
    int32_t vlp;
    int32_t vbp;
    int32_t hp;
};

class SID {
public:
    // gestione qualita
    enum QualityMode {
        QUALITY_FULL,     // massima fedelta (SID Player)
        QUALITY_BALANCED, // filtro semplificato + sync ridotto
        QUALITY_FAST      // no anti-aliasing + filtro minimo
    };
    
    SID();
    
    void reset();
    void begin(uint32_t fs = SID_SAMPLE_RATE);
    void initPWM(uint8_t audio_pin);
    
    // cambia modalita a runtime
    void setQualityMode(QualityMode mode);
    QualityMode getQualityMode() const { return currentQuality; }
    
    // genera campione audio
    int16_t audioTick();
    
    inline uint16_t getVoiceEnvelope(int i) const { return v[i].env; }
    inline uint32_t getVoicePhase(int i) const { return v[i].phase; }
    inline uint32_t getVoiceLFSR(int i) const { return v[i].lfsr; }
    
private:
    // modalita corrente
    QualityMode currentQuality;
    
    // contatori per sync adattivo
    uint8_t syncInterval;  // 22 per FULL, 44 per BALANCED/FAST
    
    // voci
    Voice v[3];
    
    // filtro
    FilterState filter;
    uint8_t fcLo, fcHi;
    uint8_t resFilt;
    uint8_t modeVol;
    uint16_t filter_w;
    
    //sSample rate e timing
    uint32_t sampleRate;
    uint32_t PHASE_K;
    
    // modulazione PWM
    uint slice_num;
    
    // ADSR step tables
    uint16_t atkStep[16];
    uint16_t decRelStep[16];
    
    // Anti-aliasing
    int16_t aa_x1, aa_x2, aa_y1, aa_y2;
    uint32_t dither_lfsr;
    
    // Tabelle statiche
    static const uint16_t RATE_PERIODS[16];
    static const uint32_t DAC_TABLE[4096];
    
    // Metodi core (comuni)
    void recalcPhaseInc(int i);
    void updateFilterCoeff();
    void syncFromSharedRegs();
    
    // audioTick 
    int16_t audioTick_Full();      // Massima qualita
    int16_t audioTick_Balanced();  // Filtro semplificato
    int16_t audioTick_Fast();      // Minimo overhead
    
    // generazioni forme onda (comune a tutte le modalita)
    inline uint16_t triWave(uint32_t ph) const;
    inline uint16_t sawWave(uint32_t ph) const;
    inline uint16_t pulseWave(uint32_t ph, uint16_t pw) const;
    uint16_t noiseWave(Voice& vv) const;
    uint16_t combinedWave(uint32_t ph, uint16_t pw, uint8_t ctrl) const;
    
    // DAC
    inline int16_t waveToDac(uint16_t wave) const;
    
    // Voice rendering (comune)
    int16_t renderVoice(int i, int16_t modTri, bool modMSBRise);
    
    // ADSR - 2 versioni
    void updateADSR(int i);           // Full (chiamato da FULL)
    void updateADSR_Fast(int i);      // Fast-path (chiamato da BALANCED/FAST)
    
    // filtri - 3 versioni
    int16_t applyMultiFilter_Full(int32_t sum);      // 64-bit math
    int16_t applyMultiFilter_Balanced(int32_t sum);  // 32-bit math
    int16_t applyFilter_Fast(int32_t sum);           // Single-pass LP
    
    // anti-aliasing (solo FULL)
    inline int16_t applyAntiAliasing(int16_t input);
};

#endif // SID_H