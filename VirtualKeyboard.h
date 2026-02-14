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
#ifndef VIRTUAL_KEYBOARD_H
#define VIRTUAL_KEYBOARD_H

#include <TFT_eSPI.h>
#include "FunduinoJoystick.h"
#include "PS2Keyboard.h"

// ============================================
// Mini tastiera virtuale C64 COMPLETA
// Area: 320x40px (4 righe × 16 colonne = 64 tasti)
// Include: tutti i tasti C64 essenziali
// ============================================

class VirtualKeyboard {
public:
    VirtualKeyboard(TFT_eSPI* display, PS2Keyboard* ps2);
    
    void begin();
    void show();      // Mostra tastiera
    void hide();      // Nascondi tastiera
    bool isVisible(); // Stato visibilità
    
    // Update con joystick - ritorna true se tasto premuto
    bool update(uint8_t joyState);
    
    // Reset posizione cursore
    void resetCursor();

    // Dimensioni layout
    static const uint8_t COLS = 16;  // 16 colonne
    static const uint8_t ROWS = 4;   // 4 righe (era 3)
    static const uint16_t KEY_WIDTH = 20;   // 320/16 = 20px per tasto
    static const uint16_t KEY_HEIGHT = 10;  // 40/4 = 10px per riga
    static const uint16_t KB_Y_START = 200; // Parte da y=200
    
private:
    TFT_eSPI* tft;
    PS2Keyboard* keyboard;
    
    bool visible;
    uint8_t cursorX;  // Posizione cursore (0-15 colonne)
    uint8_t cursorY;  // Riga (0-3)
    
    uint32_t lastMove;    // Debounce movimento
    uint32_t lastFire;    // Debounce fire
    uint8_t lastJoyState; // Per edge detection
    
    // Definizione tasto
    struct KeyDef {
        const char* label;  // Etichetta (max 2 caratteri)
        uint8_t col;        // Colonna keyMatrix
        uint8_t row;        // Row bit
        uint16_t color;     // Colore tasto
    };
    
    // Layout 4 righe × 16 colonne = 64 tasti
    static const KeyDef layout[ROWS][COLS];
    
    // Rendering
    void drawKeyboard();
    void drawKey(uint8_t x, uint8_t y, bool selected);
    void pressKey(uint8_t x, uint8_t y);
    
    // Utility
    bool isJoyPressed(uint8_t joyState, uint8_t direction);
};

#endif