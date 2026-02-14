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

#include "CIA1.h"
#include "cpu.h"
#include "PS2Keyboard.h"
#include <Arduino.h>

CIA1::CIA1() : _cpu(nullptr), keyboard(nullptr) {
    portA = 0xFF;
    portB = 0xFF;
    ddrA = 0x00;
    ddrB = 0x00;
    interruptFlags = 0x00;
    interruptMask = 0x00;
    flag_pin_state = true;
    flag_pin_last_state = true;
    joystickPort1 = nullptr;
    joystickPort2 = nullptr;
}

void CIA1::init(cpu* cpuRef, PS2Keyboard* kb) {
    _cpu = cpuRef;
    keyboard = kb;
    reset();
    Serial.println("CIA1 initialized");
}

void CIA1::reset() {
    // Port registers
    portA = 0xFF;
    portB = 0xFF;
    ddrA = 0x00;
    ddrB = 0x00;
    
    // Timer A
    timerA_counter = 0xFFFF;
    timerA_latch = 0xFFFF;
    timerA_control = 0x00;
    timerA_running = false;
    timerA_oneshot = false;
    timerA_pbToggle = false;
    
    // Timer B
    timerB_counter = 0xFFFF;
    timerB_latch = 0xFFFF;
    timerB_control = 0x00;
    timerB_running = false;
    timerB_oneshot = false;
    timerB_pbToggle = false;
    timerB_inputMode = 0;
    
    // Interrupts
    interruptFlags = 0x00;
    interruptMask = 0x00;
    
    // TOD
    todTenth = todSec = todMin = todHour = 0;
    todAlarmTenth = todAlarmSec = todAlarmMin = todAlarmHour = 0;
    todRunning = false;
    todAlarmEnabled = false;
    todLatched = false;
    todCounter = 0;
    
    // Serial
    sdr = 0;
    sdrCounter = 0;
    sdrShifting = false;
    
    // FLAG pin
    flag_pin_state = true;
    flag_pin_last_state = true;
    
    // Joysticks
    joystick1 = 0xFF;
    joystick2 = 0xFF;
    
    Serial.println("CIA1 reset completed");
}

uint8_t CIA1::getEffectivePortA() {
    // Valore effettivo: (data & ddr) | (~ddr & pull-up)
    uint8_t effective = (portA & ddrA) | ((~ddrA) & 0xFF);
    return effective;
}

uint8_t CIA1::getEffectivePortB() {
    uint8_t effective = (portB & ddrB) | ((~ddrB) & 0xFF);
    return effective;
}

void CIA1::updateTOD() {
    uint8_t tenth = fromBCD(todTenth);
    uint8_t sec = fromBCD(todSec);
    uint8_t min = fromBCD(todMin);
    uint8_t hour = fromBCD(todHour);
    
    tenth++;
    if (tenth >= 10) {
        tenth = 0;
        sec++;
        if (sec >= 60) {
            sec = 0;
            min++;
            if (min >= 60) {
                min = 0;
                hour++;
                if (hour >= 24) {
                    hour = 0;
                }
            }
        }
    }
    
    todTenth = toBCD(tenth);
    todSec = toBCD(sec);
    todMin = toBCD(min);
    todHour = toBCD(hour);
    
    // Check TOD alarm
    if (todAlarmEnabled) {
        if (todTenth == todAlarmTenth && todSec == todAlarmSec && 
            todMin == todAlarmMin && todHour == todAlarmHour) {
            triggerInterrupt(INT_TOD_ALARM);
        }
    }
}

void CIA1::checkInterrupts() {
    // CIA1 genera IRQ (maskable interrupt)
    bool interrupt_pending = (interruptFlags & interruptMask) != 0;
    if (interrupt_pending && _cpu) {
        _cpu->irq();  // Chiama il metodo IRQ corretto
    }
}

void CIA1::triggerInterrupt(uint8_t flag) {
    // Set interrupt flag se quella sorgente è abilitata nella mask
    if (interruptMask & flag) {
        interruptFlags |= flag;
        checkInterrupts();
    }
}

void CIA1::setFlagPin(bool state) {
    flag_pin_last_state = flag_pin_state;
    flag_pin_state = state;
    
    // Negative edge detection (1->0)
    if (flag_pin_last_state && !flag_pin_state) {
        triggerInterrupt(INT_FLAG);
    }
}

void CIA1::setTOD(uint8_t hour, uint8_t min, uint8_t sec, uint8_t tenth) {
    todHour = toBCD(hour & 0x1F);
    todMin = toBCD(min & 0x3F);
    todSec = toBCD(sec & 0x3F);
    todTenth = toBCD(tenth & 0x0F);
}

void CIA1::setTODAlarm(uint8_t hour, uint8_t min, uint8_t sec, uint8_t tenth) {
    todAlarmHour = toBCD(hour & 0x1F);
    todAlarmMin = toBCD(min & 0x3F);
    todAlarmSec = toBCD(sec & 0x3F);
    todAlarmTenth = toBCD(tenth & 0x0F);
}

uint8_t CIA1::toBCD(uint8_t value) {
    return ((value / 10) << 4) | (value % 10);
}

uint8_t CIA1::fromBCD(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}