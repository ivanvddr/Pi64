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

#ifndef CIA1_H
#define CIA1_H

#include "PS2Keyboard.h"
#include "FunduinoJoystick.h"
#include <stdint.h>

class PS2Keyboard;
class cpu;

extern volatile bool joystickInputDisabled;

class CIA1 {
private:
    // Port registers
    uint8_t portA, portB;
    uint8_t ddrA, ddrB;
    
    // Timer registers
    uint16_t timerA_counter;
    uint16_t timerA_latch;
    uint8_t timerA_control;
    bool timerA_running;
    bool timerA_oneshot;
    bool timerA_pbToggle;
    
    uint16_t timerB_counter;
    uint16_t timerB_latch;
    uint8_t timerB_control;
    bool timerB_running;
    bool timerB_oneshot;
    bool timerB_pbToggle;
    uint8_t timerB_inputMode;
    
    // Interrupt control
    uint8_t interruptFlags;
    uint8_t interruptMask;
    
    // TOD (Time Of Day) - BCD format
    uint8_t todTenth, todSec, todMin, todHour;
    uint8_t todAlarmTenth, todAlarmSec, todAlarmMin, todAlarmHour;
    bool todRunning;
    bool todAlarmEnabled;
    bool todLatched;
    uint32_t todCounter;
    
    // Serial Data Register
    uint8_t sdr;
    uint8_t sdrCounter;
    bool sdrShifting;
    
    // FLAG pin
    bool flag_pin_state;
    bool flag_pin_last_state;
    
    // References
    cpu* _cpu;
    PS2Keyboard* keyboard;
    FunduinoJoystick* joystickPort1;
    FunduinoJoystick* joystickPort2;
    
    // Joystick ports
    uint8_t joystick1, joystick2;
    
    // Metodi privati
    uint8_t getEffectivePortA();
    uint8_t getEffectivePortB();
    void checkInterrupts();
    void triggerInterrupt(uint8_t flag);
    void updateTOD();
    uint8_t toBCD(uint8_t value);
    uint8_t fromBCD(uint8_t bcd);

public:
    CIA1();

    inline bool hasIRQ() const {
        return (interruptFlags & interruptMask) != 0;
    }

    void setJoysticks(FunduinoJoystick* joy1, FunduinoJoystick* joy2) {
        joystickPort1 = joy1;
        joystickPort2 = joy2;
        
        if (joy1) {
            Serial.printf("[CIA1] Joystick Port 1 connected\n");
        }
        if (joy2) {
            Serial.printf("[CIA1] Joystick Port 2 connected\n");
        }
    }
    
    void init(cpu* cpuRef, PS2Keyboard* keyboard);
    void reset();
    
    inline uint8_t read(uint8_t address) {
        address &= 0x0F;
        
        static uint32_t lastDebugTime = 0;
        static uint32_t readCount = 0;
        readCount++;
        
        switch (address) {
            case 0x00: // Port A - Keyboard columns (OUTPUT) + Joystick 2
                {
                    uint8_t effective = (portA & ddrA) | ((~ddrA) & 0xFF);
                    
                    // JOYSTICK 2 su Port A
                    if (!joystickInputDisabled && joystickPort2) {
                        uint8_t joy2State = joystickPort2->read();
                        effective &= joy2State;
                    }
                    
                    return effective;
                }
                
            case 0x01: // Port B - Keyboard rows (INPUT) + Joystick 1
                {
                    uint8_t result = 0xFF;
                    
                    // scansione tastiera
                    if (keyboard) {
                        // keyboard->processKeys(); viene chiamato da timer
                        uint8_t columnSelect = getEffectivePortA();
                        result = keyboard->scanKeyboard(~columnSelect);

                    }
                    
                    // Combina con output bits
                    result = (result & ~ddrB) | (portB & ddrB);
                    
                    // JOYSTICK 1 su Port B
                    if (!joystickInputDisabled && joystickPort1) {
                        uint8_t joy1State = joystickPort1->read();
                        result &= joy1State;
                    }
                    
                    return result;
                }
                
            case 0x02: // DDR A                
                return ddrA;
                
            case 0x03: // DDR B               
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
                todRunning = false;
                todLatched = false;
                return todTenth;
                
            case 0x09: // TOD Seconds
                return todSec;
                
            case 0x0A: // TOD Minutes
                return todMin;
                
            case 0x0B: // TOD Hours
                todLatched = true;
                todRunning = true;
                return todHour;
                
            case 0x0C: // Serial Data Register
                return sdr;
                
            case 0x0D: // ICR - Interrupt Control and Status
                {
                    uint8_t result = interruptFlags;
                    if (interruptFlags & interruptMask) {
                        result |= 0x80;
                    }
                    interruptFlags = 0x00;
                    checkInterrupts();
                    return result;
                }
                
            case 0x0E: // Control Register A
                return timerA_control;
                
            case 0x0F: // Control Register B
                return timerB_control;
                
            default:
                return 0xFF;
        }
    }

    inline void write(uint8_t address, uint8_t value) {
        address &= 0x0F;
        
        switch (address) {
            case 0x00: // Port A - Keyboard columns
                portA = (portA & ~ddrA) | (value & ddrA);
                break;
                
            case 0x01: // Port B
                portB = (portB & ~ddrB) | (value & ddrB);
                break;
                
            case 0x02: // DDR A
                ddrA = value;
                break;
                
            case 0x03: // DDR B
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
                todRunning = false;
                todTenth = value & 0x0F;
                break;
                
            case 0x09: // TOD Seconds
                todSec = toBCD(value & 0x7F);
                break;
                
            case 0x0A: // TOD Minutes
                todMin = toBCD(value & 0x7F);
                break;
                
            case 0x0B: // TOD Hours
                todHour = toBCD(value & 0x3F);
                todRunning = true;
                todCounter = 0;
                break;
                
            case 0x0C: // Serial Data Register
                sdr = value;
                if (timerA_control & 0x40) {
                    sdrShifting = true;
                    sdrCounter = 8;
                }
                break;
                
            case 0x0D: // ICR - Interrupt Control
                {
                    if (value & 0x80) {
                        interruptMask |= (value & 0x1F);
                    } else {
                        interruptMask &= ~(value & 0x1F);
                    }
                    checkInterrupts();
                }
                break;
                
            case 0x0E: // Control Register A
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
                
            case 0x0F: // Control Register B
                {
                    bool wasRunning = timerB_running;
                    
                    timerB_control = value;
                    timerB_running = (value & 0x01) != 0;
                    timerB_oneshot = (value & 0x08) != 0;
                    timerB_pbToggle = (value & 0x04) != 0;
                    timerB_inputMode = (value >> 5) & 0x03;
                    
                    todAlarmEnabled = (value & 0x80) != 0;
                    
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

    inline void update(uint32_t cycles) {
        // Update Timer A
        if (timerA_running) {
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
            
            // Update Timer B
            if (timerB_running) {
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
                        decrementValue = underflow ? 1 : 0;
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
        }
        
        // Update TOD
        if (todRunning) {
            todCounter += cycles;
            if (todCounter >= 100000) {
                todCounter -= 100000;
                updateTOD();
            }
        }
        
        // Update Serial
        if (sdrShifting && sdrCounter > 0) {
            sdrCounter--;
            if (sdrCounter == 0) {
                sdrShifting = false;
                triggerInterrupt(INT_SERIAL);
            }
        }
    }
    
    void setKeyboard(PS2Keyboard* kb) { keyboard = kb; }
    void setJoystick1(uint8_t value) { joystick1 = value; }
    void setJoystick2(uint8_t value) { joystick2 = value; }
    void setFlagPin(bool state);
    
    void setTOD(uint8_t hour, uint8_t min, uint8_t sec, uint8_t tenth);
    void setTODAlarm(uint8_t hour, uint8_t min, uint8_t sec, uint8_t tenth);
    
    // Interrupt flags
    static const uint8_t INT_TIMER_A = 0x01;
    static const uint8_t INT_TIMER_B = 0x02;
    static const uint8_t INT_TOD_ALARM = 0x04;
    static const uint8_t INT_SERIAL = 0x08;
    static const uint8_t INT_FLAG = 0x10;
};

#endif