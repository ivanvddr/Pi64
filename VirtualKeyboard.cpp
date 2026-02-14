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
#include "VirtualKeyboard.h"

// ============================================
// LAYOUT COMPLETO C64 - 4 RIGHE × 16 COLONNE
// ============================================
// Tutti i 64 tasti essenziali per giochi e BASIC
// 
// Mappatura colori:
// - TFT_DARKGREY: lettere/numeri normali
// - TFT_NAVY: tasti funzione (F1-F8)
// - TFT_MAROON: tasti speciali (RETURN, SPACE, RUN/STOP, cursori)
// - TFT_DARKGREEN: modificatori (SHIFT, C=, CTRL)
// - TFT_OLIVE: punteggiatura

const VirtualKeyboard::KeyDef VirtualKeyboard::layout[ROWS][COLS] = {
    // ========== RIGA 0: Numeri e simboli principali ==========
    {
        {"1",  7, 0, TFT_DARKGREY},   // 1
        {"2",  7, 3, TFT_DARKGREY},   // 2
        {"3",  1, 0, TFT_DARKGREY},   // 3
        {"4",  1, 3, TFT_DARKGREY},   // 4
        {"5",  2, 0, TFT_DARKGREY},   // 5
        {"6",  2, 3, TFT_DARKGREY},   // 6
        {"7",  3, 0, TFT_DARKGREY},   // 7
        {"8",  3, 3, TFT_DARKGREY},   // 8
        {"9",  4, 0, TFT_DARKGREY},   // 9
        {"0",  4, 3, TFT_DARKGREY},   // 0
        {"+",  5, 0, TFT_OLIVE},      // +
        {"-",  5, 3, TFT_OLIVE},      // -
        {"@",  5, 6, TFT_OLIVE},      // @
        {"*",  6, 1, TFT_OLIVE},      // *
        {"=",  6, 5, TFT_OLIVE},      // =
        {"/",  6, 7, TFT_OLIVE}       // /
    },
    
    // ========== RIGA 1: QWERTYUIOP + punteggiatura ==========
    {
        {"Q",  7, 6, TFT_DARKGREY},   // Q
        {"W",  1, 1, TFT_DARKGREY},   // W
        {"E",  1, 6, TFT_DARKGREY},   // E
        {"R",  2, 1, TFT_DARKGREY},   // R
        {"T",  2, 6, TFT_DARKGREY},   // T
        {"Y",  3, 1, TFT_DARKGREY},   // Y
        {"U",  3, 6, TFT_DARKGREY},   // U
        {"I",  4, 1, TFT_DARKGREY},   // I
        {"O",  4, 6, TFT_DARKGREY},   // O
        {"P",  5, 1, TFT_DARKGREY},   // P
        {":",  5, 5, TFT_OLIVE},      // : (SHIFT+;)
        {";",  6, 2, TFT_OLIVE},      // ;
        {",",  5, 7, TFT_OLIVE},      // ,
        {".",  5, 4, TFT_OLIVE},      // .
        {"HM", 6, 3, TFT_MAROON},     // CLR/HOME
        {"DL", 0, 0, TFT_MAROON}      // INST/DEL
    },
    
    // ========== RIGA 2: ASDFGHJKL + ZXCVBNM ==========
    {
        {"A",  1, 2, TFT_DARKGREY},   // A
        {"S",  1, 5, TFT_DARKGREY},   // S
        {"D",  2, 2, TFT_DARKGREY},   // D
        {"F",  2, 5, TFT_DARKGREY},   // F
        {"G",  3, 2, TFT_DARKGREY},   // G
        {"H",  3, 5, TFT_DARKGREY},   // H
        {"J",  4, 2, TFT_DARKGREY},   // J
        {"K",  4, 5, TFT_DARKGREY},   // K
        {"L",  5, 2, TFT_DARKGREY},   // L
        {"Z",  1, 4, TFT_DARKGREY},   // Z
        {"X",  2, 7, TFT_DARKGREY},   // X
        {"C",  2, 4, TFT_DARKGREY},   // C
        {"V",  3, 7, TFT_DARKGREY},   // V
        {"B",  3, 4, TFT_DARKGREY},   // B
        {"N",  4, 7, TFT_DARKGREY},   // N
        {"M",  4, 4, TFT_DARKGREY}    // M
    },
    
    // ========== RIGA 3: Funzioni + Speciali + Cursori ==========
    {
        {"F1", 0, 4, TFT_NAVY},       // F1
        {"F2", 0, 4, TFT_NAVY},       // F2 (SHIFT+F1)
        {"F3", 0, 5, TFT_NAVY},       // F3
        {"F4", 0, 5, TFT_NAVY},       // F4 (SHIFT+F3)
        {"F5", 0, 6, TFT_NAVY},       // F5
        {"F6", 0, 6, TFT_NAVY},       // F6 (SHIFT+F5)
        {"F7", 0, 3, TFT_NAVY},       // F7
        {"F8", 0, 3, TFT_NAVY},       // F8 (SHIFT+F7)
        {"SP", 7, 4, TFT_MAROON},     // SPACE
        {"RT", 0, 1, TFT_MAROON},     // RETURN
        {"SH", 1, 7, TFT_DARKGREEN},  // LEFT SHIFT
        {"C=", 7, 5, TFT_DARKGREEN},  // COMMODORE
        {"RS", 7, 7, TFT_MAROON},     // RUN/STOP
        {"UP", 0, 7, TFT_MAROON},     // CRSR ↑ (SHIFT+↓)
        {"DN", 0, 7, TFT_MAROON},     // CRSR ↓
        {"LR", 0, 2, TFT_MAROON}      // CRSR ← / → (stesso tasto, SHIFT inverte)
    }
};

// Mappa tasti che richiedono SHIFT
static const bool requiresShift[VirtualKeyboard::ROWS][VirtualKeyboard::COLS] = {
    // Riga 0: nessuno richiede SHIFT
    {false, false, false, false, false, false, false, false, 
     false, false, false, false, false, false, false, false},
    
    // Riga 1: ":" (pos 10) richiede SHIFT+;
    {false, false, false, false, false, false, false, false,
     false, false, true, false, false, false, false, false},
    
    // Riga 2: nessuno richiede SHIFT
    {false, false, false, false, false, false, false, false,
     false, false, false, false, false, false, false, false},
    
    // Riga 3: F2,F4,F6,F8 (pos 1,3,5,7) e UP (pos 13)
    {false, true, false, true, false, true, false, true,
     false, false, false, false, false, true, false, false}
};


// costruttore
VirtualKeyboard::VirtualKeyboard(TFT_eSPI* display, PS2Keyboard* ps2) {
    tft = display;
    keyboard = ps2;
    visible = false;
    cursorX = 0;
    cursorY = 0;
    lastMove = 0;
    lastFire = 0;
    lastJoyState = 0xFF;
}

void VirtualKeyboard::begin() {
    Serial.println("VirtualKeyboard: Layout 4x16 (64 keys COMPLETE)");
    Serial.println("Includes: A-Z, 0-9, F1-F8, cusors, punctuation, SPACE, RETURN, RUN/STOP");
}


// SHOW/HIDE
void VirtualKeyboard::show() {
    if (visible) return;
    
    visible = true;
    resetCursor();
    drawKeyboard();
    
    Serial.println("Virtual keyboard SHOWN (64 keys)");
}

void VirtualKeyboard::hide() {
    if (!visible) return;
    
    visible = false;
    
    // Pulisci area tastiera
    tft->fillRect(0, KB_Y_START, 320, 40, TFT_BLACK);
    
    Serial.println("HIDDEN virtual keyboard");
}

bool VirtualKeyboard::isVisible() {
    return visible;
}

void VirtualKeyboard::resetCursor() {
    cursorX = 0;
    cursorY = 0;
}


// RENDERING
void VirtualKeyboard::drawKeyboard() {
    // Sfondo scuro
    tft->fillRect(0, KB_Y_START, 320, 40, TFT_BLACK);
    
    // Disegna tutti i tasti
    for (uint8_t y = 0; y < ROWS; y++) {
        for (uint8_t x = 0; x < COLS; x++) {
            drawKey(x, y, (x == cursorX && y == cursorY));
        }
    }
}

void VirtualKeyboard::drawKey(uint8_t x, uint8_t y, bool selected) {
    const KeyDef& key = layout[y][x];
    
    uint16_t px = x * KEY_WIDTH;
    uint16_t py = KB_Y_START + (y * KEY_HEIGHT);
    
    // Colore tasto
    uint16_t bgColor = selected ? TFT_YELLOW : key.color;
    uint16_t textColor = selected ? TFT_BLACK : TFT_WHITE;
    
    // Disegna background
    tft->fillRect(px + 1, py + 1, KEY_WIDTH - 2, KEY_HEIGHT - 2, bgColor);
    
    // Bordo nero per separazione
    tft->drawRect(px, py, KEY_WIDTH, KEY_HEIGHT, TFT_BLACK);
    
    // Etichetta centrata (font piccolo per 20px width)
    tft->setTextColor(textColor, bgColor);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    
    tft->drawString(key.label, px + KEY_WIDTH / 2, py + KEY_HEIGHT / 2);
}


// UPDATE - Gestisce input joystick
bool VirtualKeyboard::update(uint8_t joyState) {
    if (!visible) return false;
    
    uint32_t now = millis();
    bool keyPressed = false;
    
    // Edge detection
    uint8_t joyPressed = joyState & ~lastJoyState;
    

    // MOVIMENTO CURSORE
    if (now - lastMove > 120) {
        uint8_t oldX = cursorX;
        uint8_t oldY = cursorY;
        
        // UP
        if (joyPressed & 0x01) {
            if (cursorY > 0) {
                cursorY--;
            } else {
                cursorY = ROWS - 1;
            }
            lastMove = now;
        }
        // DOWN
        else if (joyPressed & 0x02) {
            if (cursorY < ROWS - 1) {
                cursorY++;
            } else {
                cursorY = 0;
            }
            lastMove = now;
        }
        // LEFT
        else if (joyPressed & 0x04) {
            if (cursorX > 0) {
                cursorX--;
            } else {
                cursorX = COLS - 1;
            }
            lastMove = now;
        }
        // RIGHT
        else if (joyPressed & 0x08) {
            if (cursorX < COLS - 1) {
                cursorX++;
            } else {
                cursorX = 0;
            }
            lastMove = now;
        }
        
        // Ridisegna se mosso
        if (oldX != cursorX || oldY != cursorY) {
            drawKey(oldX, oldY, false);
            drawKey(cursorX, cursorY, true);
        }
    }
    
    // FIRE = PREMI TASTO
    if ((joyPressed & 0x10) && (now - lastFire > 200)) {
        pressKey(cursorX, cursorY);
        lastFire = now;
        keyPressed = true;
    }
    
    lastJoyState = joyState;
    return keyPressed;
}


// PRESSIONE TASTO
void VirtualKeyboard::pressKey(uint8_t x, uint8_t y) {
    const KeyDef& key = layout[y][x];
    bool needsShift = requiresShift[y][x];
    
    Serial.printf("Pressed: '%s' [col=%d, row=%d]", 
                  key.label, key.col, key.row);
    
    if (needsShift) {
        Serial.print(" +SHIFT");
    }
    Serial.println();
    
    // Premi SHIFT se necessario
    if (needsShift) {
        noInterrupts();
        PS2Keyboard::keyMatrix[1] &= ~(1 << 7);  // SHIFT
        interrupts();
        delay(20);
    }
    
    // Premi tasto principale
    noInterrupts();
    PS2Keyboard::keyMatrix[key.col] &= ~(1 << key.row);
    interrupts();
    
    // Feedback visivo
    tft->fillRect(cursorX * KEY_WIDTH + 1, 
                  KB_Y_START + cursorY * KEY_HEIGHT + 1,
                  KEY_WIDTH - 2, KEY_HEIGHT - 2, 
                  TFT_WHITE);
    delay(80);
    
    // Rilascia tasto
    noInterrupts();
    PS2Keyboard::keyMatrix[key.col] |= (1 << key.row);
    interrupts();
    
    // Rilascia SHIFT se usato
    if (needsShift) {
        delay(20);
        noInterrupts();
        PS2Keyboard::keyMatrix[1] |= (1 << 7);
        interrupts();
    }
    
    // Ridisegna
    drawKey(cursorX, cursorY, true);
}

// ============================================
// UTILITY
// ============================================
bool VirtualKeyboard::isJoyPressed(uint8_t joyState, uint8_t direction) {
    return !(joyState & (1 << direction));
}