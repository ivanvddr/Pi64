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
#ifndef SKIPKEYSUTILS_H
#define SKIPKEYSUTILS_H

#include <Arduino.h>
#include "configs.h"
#include "PS2Keyboard.h"

// Struttura sequenza tasti
struct VirtualKey {
    uint8_t col;      // Colonna keyMatrix (0-7)
    uint8_t row;      // Row bit (0-7)
    uint16_t pressMs; // Durata pressione
    uint16_t pauseMs; // Pausa dopo rilascio
    const char* name; // Nome per debug
};

// Configurazione sequenza
extern VirtualKey skipSequence[];
extern const uint8_t SKIP_SEQUENCE_LENGTH;

// Variabili di stato
extern volatile bool skipSequenceActive;
extern volatile uint8_t skipSequenceIndex;
extern volatile uint32_t skipSequenceTimer;
extern volatile bool skipKeyPressed;

// Configurazione debounce
extern volatile uint32_t lastSkipButtonPress;

// Funzioni
void setupSkipButton(int pin);
void skipButtonISR();
void updateSkipSequence();
void resetSkipSequence();


#endif