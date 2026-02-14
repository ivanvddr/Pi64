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
#include "FunduinoJoystick.h"

FunduinoJoystick::FunduinoJoystick(uint8_t joystickPort) {
    port = joystickPort;
    pinUp = -1;
    pinDown = -1;
    pinLeft = -1;
    pinRight = -1;
    pinFire = -1;
    lastState = 0xFF;  // Tutti inattivi
    debouncedState = 0xFF;
    lastChangeTime = 0;
}

void FunduinoJoystick::begin(int upPin, int downPin, int leftPin, int rightPin, int firePin) {
    // Salva pin assignment
    pinUp = upPin;
    pinDown = downPin;
    pinLeft = leftPin;
    pinRight = rightPin;
    pinFire = firePin;
    
    // Configura pin come INPUT_PULLUP (joystick shield usa logica negativa)
    pinMode(pinUp, INPUT_PULLUP);
    pinMode(pinDown, INPUT_PULLUP);
    pinMode(pinLeft, INPUT_PULLUP);
    pinMode(pinRight, INPUT_PULLUP);
    pinMode(pinFire, INPUT_PULLUP);
    
    Serial.printf("FunduinoJoystick: Port %d initialized\n", port);
    Serial.printf("  UP:    GP%d\n", pinUp);
    Serial.printf("  DOWN:  GP%d\n", pinDown);
    Serial.printf("  LEFT:  GP%d\n", pinLeft);
    Serial.printf("  RIGHT: GP%d\n", pinRight);
    Serial.printf("  FIRE:  GP%d\n", pinFire);
    
    // leggi stato iniziale
    lastState = readHardware();
    debouncedState = lastState;
}

// lettura hardware diretta (SEMPRE aggiornata)
uint8_t FunduinoJoystick::readHardware() {
    uint8_t state = 0xFF;  // Default: tutti inattivi (bit = 1)
    
    // Leggi ogni direzione (logica negativa: LOW = premuto)
    if (readPin(pinUp))    state &= ~0x01;  // Bit 0 = 0
    if (readPin(pinDown))  state &= ~0x02;  // Bit 1 = 0
    if (readPin(pinLeft))  state &= ~0x04;  // Bit 2 = 0
    if (readPin(pinRight)) state &= ~0x08;  // Bit 3 = 0
    if (readPin(pinFire))  state &= ~0x10;  // Bit 4 = 0
    
    return state;
}

// lettura SENZA debounce per CIA1
uint8_t FunduinoJoystick::read() {
    // aggiorna SEMPRE lo stato leggendo direttamente l'hardware
    uint8_t currentState = readHardware();
    
    // aggiorna stato interno (volatile per thread-safety)
    lastState = currentState;
    
    return currentState;
}

// versione con debounce (opzionale, non usata da CIA1)
uint8_t FunduinoJoystick::readDebounced() {
    uint8_t currentState = readHardware();
    
    // Debouncing: cambia stato solo dopo stabilizzazione
    if (currentState != debouncedState) {
        if (millis() - lastChangeTime > DEBOUNCE_MS) {
            debouncedState = currentState;
            lastChangeTime = millis();
        }
    }
    
    return debouncedState;
}

bool FunduinoJoystick::readPin(int pin) {
    if (pin < 0) return false;
    
    // funduino shield: LOW = pressed, HIGH = released
    // ritorna true se premuto
    return digitalRead(pin) == LOW;
}

bool FunduinoJoystick::isUpPressed() {
    return readPin(pinUp);
}

bool FunduinoJoystick::isDownPressed() {
    return readPin(pinDown);
}

bool FunduinoJoystick::isLeftPressed() {
    return readPin(pinLeft);
}

bool FunduinoJoystick::isRightPressed() {
    return readPin(pinRight);
}

bool FunduinoJoystick::isFirePressed() {
    return readPin(pinFire);
}

void FunduinoJoystick::printStatus() {
    uint8_t state = read();
    
    Serial.printf("Joy%d: State=$%02X ", port, state);
    
    if (state == 0xFF) {
        Serial.println("(IDLE)");
    } else {
        Serial.print("(");
        if (!(state & 0x01)) Serial.print("UP ");
        if (!(state & 0x02)) Serial.print("DOWN ");
        if (!(state & 0x04)) Serial.print("LEFT ");
        if (!(state & 0x08)) Serial.print("RIGHT ");
        if (!(state & 0x10)) Serial.print("FIRE ");
        Serial.println(")");
    }
}