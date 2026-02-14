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
#ifndef PS2KEYBOARD_H
#define PS2KEYBOARD_H

#include <Arduino.h>
#include "ps2.h"

//#define DEBUG_KEYBOARD

// LAYOUT TASTIERA
enum PS2KeyboardLayout {
    PS2_LAYOUT_US,  // Tastiera americana
    PS2_LAYOUT_IT   // Tastiera italiana
};

// Modalità operative
enum PS2KeyboardMode {
    PS2_MODE_DISABLED,
    PS2_MODE_CORE0,
    PS2_MODE_CORE1
};

class PS2Keyboard {
public:
    PS2Keyboard();
    
    // Init con layout selezionabile (default IT)
    void begin(int dataPin, int clkPin, PS2KeyboardLayout layout = PS2_LAYOUT_IT);
    
    // Selezione layout runtime
    void setLayout(PS2KeyboardLayout layout);
    PS2KeyboardLayout getLayout() const { return currentLayout; }
    
    // Gestione modalità
    void setMode(PS2KeyboardMode mode);
    PS2KeyboardMode getMode() const { return currentMode; }
    
    // Processing (chiamato da Core0 timer o Core1 loop)
    void processKeys(uint8_t maxKeys = 2);
    
    // Loop Core1
    static void core1_entry();
    
    // Scanning atomico per CIA1
    uint8_t scanKeyboard(uint8_t columnMask);
    
    // Matrix accessibile (per VirtualKeyboard e test)
    static volatile uint8_t keyMatrix[8];
    static volatile bool processingEnabled;
    
    // DIAGNOSTICO: Contatori per debug
    static volatile uint32_t isrCallCount;
    static volatile uint32_t scancodeCount;
    static volatile uint32_t matrixUpdateCount;
    
private:
    static int dataPin;
    static int clkPin;
    static PS2KeyboardMode currentMode;
    static PS2KeyboardLayout currentLayout;
    static PS2Keyboard* instance;
    
    // Gestione scancodes per multi-key
    void handleScancode(uint8_t scancode);
    void pressScancode(uint8_t scancode);
    void releaseScancode(uint8_t scancode);
    
    // Loop Core1
    void core1_loop();
};

#endif