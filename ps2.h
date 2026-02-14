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
#ifndef _PS2_H
#define _PS2_H

#include "pico/stdlib.h"

#define PS2_TAB 9
#define PS2_SHIFT_TAB 12
#define PS2_ENTER 13
#define PS2_BACKSPACE 127
#define PS2_ESC 27
#define PS2_INSERT 0
#define PS2_DELETE 127
#define PS2_HOME 0
#define PS2_END 0
#define PS2_PAGEUP 25
#define PS2_PAGEDOWN 26
#define PS2_UPARROW 11
#define PS2_LEFTARROW 8
#define PS2_DOWNARROW 10
#define PS2_RIGHTARROW 21
#define PS2_F1 0
#define PS2_F2 0
#define PS2_F3 0
#define PS2_F4 0
#define PS2_F5 0
#define PS2_F6 0
#define PS2_F7 0
#define PS2_F8 0
#define PS2_F9 0
#define PS2_F10 0
#define PS2_F11 0
#define PS2_F12 0
#define PS2_SCROLL 0
#define PS2_EURO_SIGN 0

#define PS2_KEYMAP_SIZE 136

typedef struct
{
    uint8_t noshift[PS2_KEYMAP_SIZE];
    uint8_t shift[PS2_KEYMAP_SIZE];
    unsigned int uses_altgr;
    uint8_t altgr[PS2_KEYMAP_SIZE];
} PS2Keymap_t;

// Keymaps disponibili
extern const PS2Keymap_t PS2Keymap_US;
extern const PS2Keymap_t PS2Keymap_IT;  

void PS2_init(int d, int c);
void PS2_selectKeyMap(PS2Keymap_t *km);

uint8_t PS2_keyAvailable(void);
int PS2_readKey();

uint8_t get_scan_code(void);

// Hook per diagnostica
void PS2_setISRHook(volatile uint32_t* counter);

#endif