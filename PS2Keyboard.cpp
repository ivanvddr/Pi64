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
#include "PS2Keyboard.h"

extern "C" {
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
}

// Variabili statiche
int PS2Keyboard::dataPin = 0;
int PS2Keyboard::clkPin = 0;
volatile uint8_t PS2Keyboard::keyMatrix[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
PS2KeyboardMode PS2Keyboard::currentMode = PS2_MODE_DISABLED;
PS2KeyboardLayout PS2Keyboard::currentLayout = PS2_LAYOUT_US;
PS2Keyboard* PS2Keyboard::instance = nullptr;
volatile bool PS2Keyboard::processingEnabled = true;

// Tracking tasti premuti
static uint8_t pressedKeys[16] = {0};
static uint8_t pressedCount = 0;

volatile uint32_t PS2Keyboard::isrCallCount = 0;
volatile uint32_t PS2Keyboard::scancodeCount = 0;
volatile uint32_t PS2Keyboard::matrixUpdateCount = 0;

PS2Keyboard::PS2Keyboard() {
    instance = this;
}

void PS2Keyboard::begin(int data, int clk, PS2KeyboardLayout layout) {
    dataPin = data;
    clkPin = clk;
    currentLayout = layout;

    for (int i = 0; i < 8; i++) {
        keyMatrix[i] = 0xFF;
    }
    
    // Reset tracking
    pressedCount = 0;
    for (int i = 0; i < 16; i++) {
        pressedKeys[i] = 0;
    }
    
    bool isrWasActive = (isrCallCount > 0);
    
    if (!isrWasActive) {
        isrCallCount = 0;
        scancodeCount = 0;
        matrixUpdateCount = 0;
        
        Serial.printf("PS/2 Keyboard: DATA=%d, CLK=%d (primo init)\n", dataPin, clkPin);
    } else {
        Serial.printf("PS/2 Keyboard: RE-INIT (preservo ISR count=%u)\n", isrCallCount);
    }

    PS2_init(dataPin, clkPin);
    setLayout(layout);
    PS2_setISRHook(&isrCallCount);
}

void PS2Keyboard::setLayout(PS2KeyboardLayout layout) {
    currentLayout = layout;
    
    switch (layout) {
        case PS2_LAYOUT_US:
            PS2_selectKeyMap((PS2Keymap_t*)&PS2Keymap_US);
            Serial.println("Keyboard layout: US");
            break;
            
        case PS2_LAYOUT_IT:
            PS2_selectKeyMap((PS2Keymap_t*)&PS2Keymap_IT);
            Serial.println("Keyboard layout: IT");
            break;
    }
}

void PS2Keyboard::setMode(PS2KeyboardMode mode) {
    currentMode = mode;
    
    switch (mode) {
        case PS2_MODE_DISABLED:
            processingEnabled = false;
            Serial.println("PS2Keyboard: DISABLED");
            break;
            
        case PS2_MODE_CORE0:
            processingEnabled = true;
            Serial.println("PS2Keyboard: CORE0 mode (timer)");
            break;
            
        case PS2_MODE_CORE1:
            processingEnabled = true;
            Serial.println("PS2Keyboard: CORE1 mode (loop)");
            break;
    }
}

void PS2Keyboard::processKeys(uint8_t maxKeys) {
    if (!processingEnabled) {
        return;
    }
    
    uint8_t processed = 0;
    
    while (processed < maxKeys) {
        uint8_t scancode = get_scan_code();
        
        if (scancode == 0) {
            break;
        }
        
        scancodeCount++;
        handleScancode(scancode);
        processed++;
    }
}

void PS2Keyboard::handleScancode(uint8_t scancode) {
    static bool breakNext = false;
    
    if (scancode == 0xF0) {
        breakNext = true;
        return;
    }
    
    if (breakNext) {
        breakNext = false;
        releaseScancode(scancode);
        return;
    }
    
    pressScancode(scancode);
}

// ⚡ HELPER: Trova mapping scancode → matrix
struct ScancodeMapping {
    uint8_t scancode;
    uint8_t col;
    uint8_t row;
};

static const ScancodeMapping scancodeMap[] = {
    // Numeri
    {0x16, 7, 0}, {0x1E, 7, 3}, {0x26, 1, 0}, {0x25, 1, 3}, {0x2E, 2, 0},
    {0x36, 2, 3}, {0x3D, 3, 0}, {0x3E, 3, 3}, {0x46, 4, 0}, {0x45, 4, 3},
    
    // Lettere
    {0x15, 7, 6}, {0x1D, 1, 1}, {0x24, 1, 6}, {0x2D, 2, 1}, {0x2C, 2, 6},
    {0x35, 3, 1}, {0x3C, 3, 6}, {0x43, 4, 1}, {0x44, 4, 6}, {0x4D, 5, 1},
    {0x1C, 1, 2}, {0x1B, 1, 5}, {0x23, 2, 2}, {0x2B, 2, 5}, {0x34, 3, 2},
    {0x33, 3, 5}, {0x3B, 4, 2}, {0x42, 4, 5}, {0x4B, 5, 2}, {0x1A, 1, 4},
    {0x22, 2, 7}, {0x21, 2, 4}, {0x2A, 3, 7}, {0x32, 3, 4}, {0x31, 4, 7},
    {0x3A, 4, 4},
    
    // Simboli
    {0x29, 7, 4}, {0x41, 5, 7}, {0x49, 5, 4}, {0x4C, 6, 2}, {0x4E, 5, 3},
    {0x55, 5, 0}, {0x54, 5, 6}, {0x5B, 6, 1}, {0x5D, 6, 0}, {0x52, 7, 3},
    {0x61, 6, 7},
    
    // Tasti speciali
    {0x5A, 0, 1}, {0x66, 0, 0}, {0x76, 7, 7},
    
    // SHIFT
    {0x12, 1, 7}, {0x59, 6, 4},
    
    // Function keys
    {0x05, 0, 4}, {0x06, 0, 4}, {0x04, 0, 5}, {0x0C, 0, 5},
    {0x03, 0, 6}, {0x0B, 0, 6}, {0x83, 0, 3}, {0x0A, 0, 3},
    
    // Cursori
    {0x75, 0, 7}, {0x72, 0, 7}, {0x6B, 0, 2}, {0x74, 0, 2},
};

// HELPER: Trova col/row da scancode
bool findMapping(uint8_t scancode, uint8_t* col, uint8_t* row) {
    for (size_t i = 0; i < sizeof(scancodeMap) / sizeof(ScancodeMapping); i++) {
        if (scancodeMap[i].scancode == scancode) {
            *col = scancodeMap[i].col;
            *row = scancodeMap[i].row;
            return true;
        }
    }
    return false;
}

void PS2Keyboard::pressScancode(uint8_t scancode) {
    uint8_t col, row;
    
    if (!findMapping(scancode, &col, &row)) {
        #ifdef DEBUG_KEYBOARD
        Serial.printf("Scancode not mapped: 0x%02X\n", scancode);
        #endif
        return;
    }
    
    // Verifica se già premuto (evita duplicati)
    for (uint8_t i = 0; i < pressedCount; i++) {
        if (pressedKeys[i] == scancode) {
            return;  // Già nella lista, skip
        }
    }
    
    // Aggiungi alla lista
    if (pressedCount < 16) {
        pressedKeys[pressedCount++] = scancode;
    }
    
    // Aggiorna matrice
    uint32_t irq_state = save_and_disable_interrupts();
    
    keyMatrix[col] &= ~(1 << row);
    
    __dmb();
    __dsb();
    __isb();
    
    restore_interrupts(irq_state);
    
    matrixUpdateCount++;
    
    #ifdef DEBUG_KEYBOARD
    Serial.printf("Press: 0x%02X → Matrix[%d][%d] (premuti: %d)\n", 
                 scancode, col, row, pressedCount);
    #endif
}

void PS2Keyboard::releaseScancode(uint8_t scancode) {
    // Rimuovi dalla lista
    bool found = false;
    for (uint8_t i = 0; i < pressedCount; i++) {
        if (pressedKeys[i] == scancode) {
            // Shifta array
            for (uint8_t j = i; j < pressedCount - 1; j++) {
                pressedKeys[j] = pressedKeys[j + 1];
            }
            pressedCount--;
            found = true;
            break;
        }
    }
    
    if (!found) {
        #ifdef DEBUG_KEYBOARD
        //warning rilascio di un tasto non premuto
        Serial.printf("Release of unpressed key: 0x%02X\n", scancode);
        #endif
        return;
    }
    
    // RICOSTRUISCI matrice da ZERO senza interruzioni da altri timer
    uint32_t irq_state = save_and_disable_interrupts();
    
    // Reset matrice
    for (int i = 0; i < 8; i++) {
        keyMatrix[i] = 0xFF;
    }
    
    // Ri-applica SOLO i tasti ancora nella lista
    // no pressScancode() per evitare duplicati!
    for (uint8_t i = 0; i < pressedCount; i++) {
        uint8_t col, row;
        if (findMapping(pressedKeys[i], &col, &row)) {
            keyMatrix[col] &= ~(1 << row);
        }
    }
    
    __dmb();
    __dsb();
    
    restore_interrupts(irq_state);
    
    #ifdef DEBUG_KEYBOARD
    Serial.printf("Release: 0x%02X (pressed: %d)\n", scancode, pressedCount);
    #endif
}

uint8_t PS2Keyboard::scanKeyboard(uint8_t columnMask) {
    uint8_t result = 0xFF;
    
    uint32_t irq_state = save_and_disable_interrupts();
    __dmb();
    __dsb();
    
    for (int col = 0; col < 8; col++) {
        if (columnMask & (1 << col)) {
            result &= keyMatrix[col];
        }
    }
    
    __dmb();
    restore_interrupts(irq_state);
    
    return result;
}

void PS2Keyboard::core1_entry() {
    if (instance) {
        instance->core1_loop();
    }
}

void PS2Keyboard::core1_loop() {
    Serial.println("PS2Keyboard Core1 loop started");
    
    const uint32_t POLL_INTERVAL_US = 1000;
    uint32_t next_poll = time_us_32();
    
    while (true) {
        while ((int32_t)(time_us_32() - next_poll) < 0) {
            tight_loop_contents();
        }
        
        if (processingEnabled) {
            processKeys(16);
        }
        
        next_poll += POLL_INTERVAL_US;
        
        uint32_t now = time_us_32();
        if ((int32_t)(now - next_poll) > (int32_t)(POLL_INTERVAL_US * 2)) {
            next_poll = now;
        }
    }
}