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
#ifndef CIA2_H
#define CIA2_H

#include <stdint.h>
#include <Arduino.h>

class cpu;  // Forward declaration
class vic2; // Forward declaration

class CIA2 {
private:
    // Registri principali
    uint8_t portA, portB;               // Port data registers
    uint8_t ddrA, ddrB;                 // Data Direction Registers
    uint8_t interruptMask;              // Interrupt enable mask
    uint8_t interruptFlags;             // Interrupt status flags
    
    // Timer A
    uint16_t timerA_counter;            // Valore corrente
    uint16_t timerA_latch;              // Valore di ricarica
    uint8_t timerA_control;             // Registro di controllo
    bool timerA_running;
    bool timerA_oneshot;
    bool timerA_pbToggle;               // PB6 toggle mode
    
    // Timer B  
    uint16_t timerB_counter;            // Valore corrente
    uint16_t timerB_latch;              // Valore di ricarica
    uint8_t timerB_control;             // Registro di controllo
    bool timerB_running;
    bool timerB_oneshot;
    bool timerB_pbToggle;               // PB7 toggle mode
    uint8_t timerB_inputMode;           // 0=φ2, 1=CNT, 2=Timer A, 3=Timer A+CNT
    
    // TOD Clock
    uint32_t tod_offset;                // Offset dal millis() di Arduino
    uint32_t tod_alarm;                 // Valore di allarme
    bool tod_running;                   // Clock in esecuzione
    bool tod_latched;                   // Lettura delle ore congela il clock
    uint32_t tod_latch_value;           // Valore congelato
    bool tod_alarm_enabled;             // Allarme abilitato
    bool tod_setting_alarm;             // True quando si sta impostando l'allarme
    uint8_t tod_freq;                   // Frequenza (50Hz/60Hz)
    uint32_t tod_last_check;            // Ultimo check alarm (in deciseconds)
    
    // Serial Port
    uint8_t sdr;                        // Serial Data Register
    uint8_t serial_counter;             // Contatore bit seriale
    bool serial_input;                  // Modalità input/output
    
    // FLAG pin
    bool flag_pin_state;                // Stato corrente del pin FLAG
    bool flag_pin_last_state;           // Stato precedente per edge detection
    
    // References
    cpu* _cpu;
    vic2* _vic2;
    
    // Metodi privati
    uint8_t getEffectivePortA();
    uint8_t getEffectivePortB();
    void updateVICBank();
    void updateTimerA(uint32_t cycles);
    void updateTimerB(uint32_t cycles, bool timerA_underflow);
    void updateTOD();
    void checkInterrupts();
    void triggerInterrupt(uint8_t flag);
    uint32_t getTODValue();
    void setTODValue(uint32_t value);
    uint8_t toBCD(uint8_t value);
    uint8_t fromBCD(uint8_t value);
    
public:
    CIA2();
    void init(cpu* cpuRef, vic2* vic2Ref);
    void reset();

    inline bool hasNMI() const {
        return (interruptFlags & interruptMask) != 0;
    }
    
    // Interfaccia principale
    inline uint8_t read(uint8_t address) {
        address &= 0x0F;
        
        switch (address) {
            case 0x00: // Port A Data
                return getEffectivePortA();
                
            case 0x01: // Port B Data
                return getEffectivePortB();
                
            case 0x02: // Data Direction A
                return ddrA;
                
            case 0x03: // Data Direction B
                return ddrB;
                
            case 0x04: // Timer A Low
                return timerA_counter & 0xFF;
                
            case 0x05: // Timer A High
                return (timerA_counter >> 8) & 0xFF;
                
            case 0x06: // Timer B Low
                return timerB_counter & 0xFF;
                
            case 0x07: // Timer B High  
                return (timerB_counter >> 8) & 0xFF;
                
            case 0x08: // TOD 10ths
                {
                    uint32_t tod = getTODValue();
                    uint8_t result = (tod % 10);
                    // Reading 10ths unlocks the latch
                    if (tod_latched) {
                        tod_latched = false;
                    }
                    return result;
                }
                
            case 0x09: // TOD Seconds
                {
                    uint32_t tod = getTODValue();
                    return toBCD((tod / 10) % 60);
                }
                
            case 0x0A: // TOD Minutes
                {
                    uint32_t tod = getTODValue();
                    return toBCD((tod / 600) % 60);
                }
                
            case 0x0B: // TOD Hours
                {
                    // Reading hours latches all TOD registers
                    if (!tod_latched) {
                        tod_latched = true;
                        tod_latch_value = getTODValue();
                    }
                    
                    uint32_t tod = tod_latch_value;
                    uint8_t hours = (tod / 36000) % 24;
                    
                    // Convert to 12-hour format
                    bool pm = (hours >= 12);
                    if (hours == 0) hours = 12;
                    else if (hours > 12) hours -= 12;
                    
                    uint8_t result = toBCD(hours);
                    if (pm) result |= 0x80;
                    
                    return result;
                }
                
            case 0x0C: // Serial Data Register
                return sdr;
                
            case 0x0D: // Interrupt Control and Status
                {
                    uint8_t result = interruptFlags;
                    if (interruptFlags & interruptMask) {
                        result |= 0x80; // Master interrupt flag (IR)
                    }
                    // Clear flags on read
                    interruptFlags = 0x00;
                    checkInterrupts();
                    return result;
                }
                
            case 0x0E: // Timer A Control
                return timerA_control;
                
            case 0x0F: // Timer B Control
                return timerB_control;
                
            default:
                return 0x00;
        }
    }

    inline void write(uint8_t address, uint8_t value) {
        address &= 0x0F;
        
        switch (address) {
            case 0x00: // Port A Data
                {
                    // Scrivi solo i bit configurati come output
                    portA = (portA & ~ddrA) | (value & ddrA);
                    
                    // Se bit VIC banking (0-1) sono output, aggiorna VIC
                    if (ddrA & 0x03) {
                        updateVICBank();
                    }
                }
                break;
                
            case 0x01: // Port B Data
                portB = (portB & ~ddrB) | (value & ddrB);
                break;
                
            case 0x02: // Data Direction A
                {
                    uint8_t oldDDR = ddrA;
                    ddrA = value;
                    
                    // Se bit VIC banking cambiano da input a output, applica subito
                    if (((oldDDR & 0x03) != (ddrA & 0x03)) && (ddrA & 0x03)) {
                        updateVICBank();
                    }
                }
                break;
                
            case 0x03: // Data Direction B
                ddrB = value;
                break;
                
            case 0x04: // Timer A Latch Low
                timerA_latch = (timerA_latch & 0xFF00) | value;
                break;
                
            case 0x05: // Timer A Latch High
                timerA_latch = (timerA_latch & 0x00FF) | (value << 8);
                if (!timerA_running) {
                    timerA_counter = timerA_latch;
                }
                break;
                
            case 0x06: // Timer B Latch Low
                timerB_latch = (timerB_latch & 0xFF00) | value;
                break;
                
            case 0x07: // Timer B Latch High
                timerB_latch = (timerB_latch & 0x00FF) | (value << 8);
                if (!timerB_running) {
                    timerB_counter = timerB_latch;
                }
                break;
                
            case 0x08: // TOD 10ths
                if (tod_setting_alarm) {
                    tod_alarm = (tod_alarm & 0xFFFFFFF0) | (value & 0x0F);
                } else {
                    tod_running = false;
                    uint32_t newTime = (getTODValue() & 0xFFFFFFF0) | (value & 0x0F);
                    setTODValue(newTime);
                    tod_running = true;
                }
                break;
                
            case 0x09: // TOD Seconds
                if (tod_setting_alarm) {
                    uint32_t secs = fromBCD(value & 0x7F);
                    tod_alarm = (tod_alarm & 0xFFFFF00F) | ((secs * 10) & 0xFF0);
                } else {
                    uint32_t secs = fromBCD(value & 0x7F);
                    uint32_t newTime = (getTODValue() & 0xFFFFF00F) | ((secs * 10) & 0xFF0);
                    setTODValue(newTime);
                }
                break;
                
            case 0x0A: // TOD Minutes  
                if (tod_setting_alarm) {
                    uint32_t mins = fromBCD(value & 0x7F);
                    tod_alarm = (tod_alarm & 0xFFFF00FF) | ((mins * 600) & 0xFF00);
                } else {
                    uint32_t mins = fromBCD(value & 0x7F);
                    uint32_t newTime = (getTODValue() & 0xFFFF00FF) | ((mins * 600) & 0xFF00);
                    setTODValue(newTime);
                }
                break;
                
            case 0x0B: // TOD Hours
                {
                    uint8_t hours = fromBCD(value & 0x1F);
                    if (value & 0x80) { // PM
                        if (hours == 12) hours = 12;
                        else hours += 12;
                    } else { // AM
                        if (hours == 12) hours = 0;
                    }
                    
                    if (tod_setting_alarm) {
                        tod_alarm = (tod_alarm & 0xFF00FFFF) | ((hours * 36000UL) & 0xFF0000);
                    } else {
                        uint32_t newTime = (getTODValue() & 0xFF00FFFF) | ((hours * 36000UL) & 0xFF0000);
                        setTODValue(newTime);
                        tod_running = false;
                    }
                }
                break;
                
            case 0x0C: // Serial Data Register
                sdr = value;
                triggerInterrupt(INT_SERIAL);
                break;
                
            case 0x0D: // Interrupt Control
                if (value & 0x80) {
                    interruptMask |= (value & 0x1F);
                } else {
                    interruptMask &= ~(value & 0x1F);
                }
                checkInterrupts();
                break;
                
            case 0x0E: // Timer A Control
                {
                    bool wasRunning = timerA_running;
                    
                    timerA_control = value;
                    timerA_running = (value & 0x01) != 0;
                    timerA_oneshot = (value & 0x08) != 0;
                    timerA_pbToggle = (value & 0x04) != 0;
                    
                    if (value & 0x10) {
                        timerA_counter = timerA_latch;
                    }
                    
                    if (timerA_pbToggle) {
                        ddrB |= 0x40;
                        if (!wasRunning && timerA_running) {
                            portB ^= 0x40;
                        }
                    }
                }
                break;
                
            case 0x0F: // Timer B Control
                {
                    bool wasRunning = timerB_running;
                    
                    timerB_control = value;
                    timerB_running = (value & 0x01) != 0;
                    timerB_oneshot = (value & 0x08) != 0;
                    timerB_pbToggle = (value & 0x04) != 0;
                    timerB_inputMode = (value >> 5) & 0x03;
                    
                    tod_setting_alarm = (value & 0x80) != 0;
                    tod_alarm_enabled = tod_setting_alarm;
                    
                    if (value & 0x10) {
                        timerB_counter = timerB_latch;
                    }
                    
                    if (timerB_pbToggle) {
                        ddrB |= 0x80;
                        if (!wasRunning && timerB_running) {
                            portB ^= 0x80;
                        }
                    }
                }
                break;
        }
    }
    
    // Aggiornamento temporizzato
    inline void update(uint32_t cycles) {
        updateTimerA(cycles);
        updateTOD();
    }

    
    // Controllo VIC-II banking
    uint8_t getVICBank();
    
    // FLAG pin control
    void setFlagPin(bool state);
    
    // Gestione seriale (per future espansioni)
    void setSerialClock(bool state);
    void setSerialData(bool state);
    bool getSerialClock();
    bool getSerialData();
    
    // Debug e diagnostica
    void debugState();
    void debugTimers();
    void debugTOD();
    
    // Interrupt flags
    static const uint8_t INT_TIMER_A = 0x01;
    static const uint8_t INT_TIMER_B = 0x02;
    static const uint8_t INT_TOD_ALARM = 0x04;
    static const uint8_t INT_SERIAL = 0x08;
    static const uint8_t INT_FLAG = 0x10;
};

#endif