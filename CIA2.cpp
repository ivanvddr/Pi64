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
#include "CIA2.h"
#include "cpu.h"
#include "vic2.h"

CIA2::CIA2() : _cpu(nullptr), _vic2(nullptr) {
    portA = 0xFF;
    portB = 0xFF;
    ddrA = 0x00;
    ddrB = 0x00;
    interruptMask = 0x00;
    interruptFlags = 0x00;
    flag_pin_state = true;
    flag_pin_last_state = true;
    tod_last_check = 0;
}

void CIA2::init(cpu* cpuRef, vic2* vic2Ref) {
    _cpu = cpuRef;
    _vic2 = vic2Ref;
    reset();
    Serial.println("CIA2 initialized");
}

void CIA2::reset() {
    // Port A: bit 0-1 controllano VIC bank (output), bit 2-7 per serial/RS232
    portA = 0x03;           // Bit 0-1 alti = VIC bank 0 (invertito: ~0x03 & 0x03 = 0)
    portB = 0xFF;           // Port B input pull-up
    ddrA = 0x3F;            // Bit 0-5 output (VIC bank + serial), 6-7 input
    ddrB = 0x00;            // Port B tutto input di default
    
    // Reset timers
    timerA_counter = 0xFFFF;
    timerA_latch = 0xFFFF;
    timerA_control = 0x00;
    timerA_running = false;
    timerA_oneshot = false;
    timerA_pbToggle = false;
    
    timerB_counter = 0xFFFF;
    timerB_latch = 0xFFFF;
    timerB_control = 0x00;
    timerB_running = false;
    timerB_oneshot = false;
    timerB_pbToggle = false;
    timerB_inputMode = 0;
    
    // Reset interrupts
    interruptMask = 0x00;
    interruptFlags = 0x00;
    
    // Reset TOD
    tod_offset = millis();
    tod_alarm = 0;
    tod_running = true;
    tod_latched = false;
    tod_latch_value = 0;
    tod_alarm_enabled = false;
    tod_setting_alarm = false;
    tod_freq = 60;
    tod_last_check = 0;
    
    // Reset serial
    sdr = 0x00;
    serial_counter = 0;
    serial_input = true;
    
    // Reset FLAG pin
    flag_pin_state = true;
    flag_pin_last_state = true;
    
    // Applica VIC banking iniziale
    updateVICBank();
    
    Serial.printf("CIA2 reset: VIC Bank=%d, Port A=$%02X, DDR A=$%02X\n", 
                  getVICBank(), portA, ddrA);
}

uint8_t CIA2::getEffectivePortA() {
    // Valore effettivo: (data & ddr) | (~ddr & pull-up)
    // CRITICAL: Maschera ~ddr a 8 bit
    uint8_t effective = (portA & ddrA) | ((~ddrA) & 0xFF);
    return effective;
}

uint8_t CIA2::getEffectivePortB() {
    uint8_t effective = (portB & ddrB) | ((~ddrB) & 0xFF);
    return effective;
}

void CIA2::updateVICBank() {
    if (!_vic2) return;
    
    // VIC banking: bit 0-1 di Port A, INVERTITI
    // Solo se i bit sono output (ddrA bit 0-1)
    uint8_t effective = getEffectivePortA();
    uint8_t vicBank = (~effective) & 0x03;
    
    _vic2->setMemoryBank(vicBank);
}
 
 
void CIA2::updateTimerA(uint32_t cycles) {
    if (!timerA_running) return;
    
    bool underflow = false;
    
    if (timerA_counter >= cycles) {
        timerA_counter -= cycles;
    } else {
        timerA_counter = timerA_latch;
        underflow = true;
        
        triggerInterrupt(INT_TIMER_A);
        
        if (timerA_pbToggle) {
            portB ^= 0x40;
        }
        
        if (timerA_oneshot) {
            timerA_running = false;
            timerA_control &= ~0x01;
        }
    }
    
    updateTimerB(cycles, underflow);
}

void CIA2::updateTimerB(uint32_t cycles, bool timerA_underflow) {
    if (!timerB_running) return;
    
    uint32_t decrementValue = 0;
    
    switch (timerB_inputMode) {
        case 0:
            decrementValue = cycles;
            break;
        case 1:
            decrementValue = 0;
            break;
        case 2:
        case 3:
            decrementValue = timerA_underflow ? 1 : 0;
            break;
    }
    
    if (decrementValue > 0) {
        if (timerB_counter >= decrementValue) {
            timerB_counter -= decrementValue;
        } else {
            timerB_counter = timerB_latch;
            
            triggerInterrupt(INT_TIMER_B);
            
            if (timerB_pbToggle) {
                portB ^= 0x80;
            }
            
            if (timerB_oneshot) {
                timerB_running = false;
                timerB_control &= ~0x01;
            }
        }
    }
}

void CIA2::updateTOD() {
    if (!tod_running || !tod_alarm_enabled) return;
    
    uint32_t current_tod = getTODValue();
    
    if (current_tod != tod_last_check) {
        tod_last_check = current_tod;
        
        if (current_tod == tod_alarm) {
            triggerInterrupt(INT_TOD_ALARM);
        }
    }
}

void CIA2::checkInterrupts() {
    // CIA2 genera NMI (Non-Maskable Interrupt)
    bool interrupt_pending = (interruptFlags & interruptMask) != 0;
    
    if (interrupt_pending && _cpu) {
        _cpu->nmi();  // Chiama il metodo NMI corretto
    }
}

void CIA2::triggerInterrupt(uint8_t flag) {
    interruptFlags |= flag;
    checkInterrupts();
}

void CIA2::setFlagPin(bool state) {
    flag_pin_last_state = flag_pin_state;
    flag_pin_state = state;
    
    if (flag_pin_last_state && !flag_pin_state) {
        triggerInterrupt(INT_FLAG);
    }
}

uint8_t CIA2::getVICBank() {
    uint8_t effective = getEffectivePortA();
    return (~effective) & 0x03;
}

uint32_t CIA2::getTODValue() {
    if (tod_latched) {
        return tod_latch_value;
    } else if (!tod_running) {
        uint32_t elapsed = millis() - tod_offset;
        return (elapsed / 100) % 864000;
    } else {
        uint32_t elapsed = millis() - tod_offset;
        return (elapsed / 100) % 864000;
    }
}

void CIA2::setTODValue(uint32_t value) {
    tod_offset = millis() - (value * 100);
}

uint8_t CIA2::toBCD(uint8_t value) {
    return ((value / 10) << 4) | (value % 10);
}

uint8_t CIA2::fromBCD(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void CIA2::setSerialClock(bool state) {
    if (ddrA & 0x10) {
        if (state) portA |= 0x10;
        else portA &= ~0x10;
    }
}

void CIA2::setSerialData(bool state) {
    if (ddrA & 0x20) {
        if (state) portA |= 0x20;
        else portA &= ~0x20;
    }
}

bool CIA2::getSerialClock() {
    return (getEffectivePortA() & 0x10) != 0;
}

bool CIA2::getSerialData() {
    return (getEffectivePortA() & 0x20) != 0;
}

void CIA2::debugState() {
    Serial.println("=== CIA2 Debug State ===");
    Serial.printf("Port A: $%02X (DDR: $%02X, Effective: $%02X)\n", 
                  portA, ddrA, getEffectivePortA());
    Serial.printf("Port B: $%02X (DDR: $%02X, Effective: $%02X)\n", 
                  portB, ddrB, getEffectivePortB());
    Serial.printf("VIC Bank: %d (Base: $%04X)\n", getVICBank(), getVICBank() * 0x4000);
    Serial.printf("Interrupts: Flags=$%02X, Mask=$%02X\n", interruptFlags, interruptMask);
    debugTimers();
    debugTOD();
    Serial.println("========================");
}

void CIA2::debugTimers() {
    Serial.printf("Timer A: $%04X/$%04X %s %s\n", 
                  timerA_counter, timerA_latch, 
                  timerA_running ? "RUN" : "STOP",
                  timerA_oneshot ? "1SHOT" : "CONT");
    Serial.printf("Timer B: $%04X/$%04X %s %s Mode=%d\n", 
                  timerB_counter, timerB_latch,
                  timerB_running ? "RUN" : "STOP", 
                  timerB_oneshot ? "1SHOT" : "CONT",
                  timerB_inputMode);
}

void CIA2::debugTOD() {
    uint32_t tod = getTODValue();
    uint8_t hours = (tod / 36000) % 24;
    uint8_t mins = (tod / 600) % 60;
    uint8_t secs = (tod / 10) % 60;
    uint8_t tenths = tod % 10;
    
    Serial.printf("TOD: %02d:%02d:%02d.%d (%s)\n", 
                  hours, mins, secs, tenths,
                  tod_running ? "RUN" : "STOP");
}