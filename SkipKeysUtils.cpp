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
#include "SkipKeysUtils.h"

// Sequenza: N, Y, SPACE, RETURN, F1, F3, F5..
VirtualKey skipSequence[] = {
    {4, 7, 120, 200, "N"},       
    {3, 1, 120, 200, "Y"},       
    {7, 0, 120, 200, "1"},         
    {7, 4, 120, 200, "SPACE"},   
    {0, 1, 120, 200, "RETURN"}, 
    {0, 4, 120, 200, "F1"},     
    {0, 5, 120, 200, "F3"},      
    {0, 3, 120, 200, "F7"},      
    {0, 6, 120, 200, "F5"},
    {3, 5, 120, 200, "H"},
    {7, 7, 120, 200, "RUN/STOP"}      
};

const uint8_t SKIP_SEQUENCE_LENGTH = 11;

// State machine
volatile bool skipSequenceActive = false;
volatile uint8_t skipSequenceIndex = 0;
volatile uint32_t skipSequenceTimer = 0;
volatile bool skipKeyPressed = false;

// Debounce
volatile uint32_t lastSkipButtonPress = 0;

// ISR del bottone skip
void skipButtonISR() {
    uint32_t now = millis();
    
    if (now - lastSkipButtonPress < SKIP_DEBOUNCE_MS) {
        return;
    }
    
    if (skipSequenceActive) {
        return;
    }
    
    lastSkipButtonPress = now;
    skipSequenceActive = true;
    skipSequenceIndex = 0;
    skipSequenceTimer = now;
    skipKeyPressed = true;
    
    Serial.println("[SKIP] Sequence started!");
}

// Setup del bottone skip
void setupSkipButton(int pin) {
    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), 
                    skipButtonISR, 
                    FALLING);  // Trigger su pressione (LOW)
    Serial.printf("Skip button active on GPIO %d\n", pin);
}

// Update della sequenza skip
void updateSkipSequence() {
    if (!skipSequenceActive) return;
    
    uint32_t now = millis();
    uint32_t elapsed = now - skipSequenceTimer;
    VirtualKey& key = skipSequence[skipSequenceIndex];
    
    if (skipKeyPressed) {
        if (elapsed < key.pressMs) {
            // accede direttamente alla variabile static
            noInterrupts();
            PS2Keyboard::keyMatrix[key.col] &= ~(1 << key.row);
            interrupts();
        } else {
            noInterrupts();
            PS2Keyboard::keyMatrix[key.col] |= (1 << key.row);
            interrupts();
            
            skipKeyPressed = false;
            skipSequenceTimer = now;
            
            Serial.printf("[SKIP] %s released\n", key.name);
        }
    } else {
        if (elapsed < key.pauseMs) {
            // Attendi pausa
        } else {
            skipSequenceIndex++;
            
            if (skipSequenceIndex >= SKIP_SEQUENCE_LENGTH) {
                skipSequenceActive = false;
                
                noInterrupts();
                for (int i = 0; i < 8; i++) {
                    PS2Keyboard::keyMatrix[i] = 0xFF;  
                }
                interrupts();
                
                Serial.println("[SKIP] Sequence completed!");
            } else {
                skipKeyPressed = true;
                skipSequenceTimer = now;
                
                Serial.printf("[SKIP] Pressed: %s\n", 
                             skipSequence[skipSequenceIndex].name);
            }
        }
    }
}

// Reset della sequenza skip
void resetSkipSequence() {
    skipSequenceActive = false;
    skipSequenceIndex = 0;
    
    noInterrupts();
    for (int i = 0; i < 8; i++) {
        PS2Keyboard::keyMatrix[i] = 0xFF;
    }
    interrupts();
}