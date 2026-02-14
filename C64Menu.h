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

#ifndef C64_MENU_H
#define C64_MENU_H

#include <TFT_eSPI.h>
#include "FunduinoJoystick.h"

// ===== VOCI MENU =====
enum MenuItem {
    MENU_BASIC = 0,
    MENU_GAME = 1,
    MENU_SID = 2,
    MENU_ITEMS_COUNT = 3
};

// ===== AZIONI MENU =====
enum MenuAction {
    ACTION_NONE = 0,
    ACTION_START_BASIC = 1,
    ACTION_START_GAME = 2,
    ACTION_START_SID = 3
};

class C64Menu {
public:
    C64Menu(TFT_eSPI* display, FunduinoJoystick* joystick);
    
    // Inizializza menu (disegna tutto)
    void begin();
    
    // Aggiorna menu (chiamare in loop)
    // Ritorna ACTION_NONE finché non viene selezionata una voce
    MenuAction update();
    
    // Disegna menu completo
    void draw();
    
    // Imposta logo (opzionale, chiamare prima di begin())
    void setLogo(const unsigned short* logoData, int width, int height);
    
private:
    TFT_eSPI* tft;
    FunduinoJoystick* joy;
    
    // Logo
    const unsigned short* logo;
    int logoWidth;
    int logoHeight;
    bool hasLogo;
    
    // Stato menu
    MenuItem selectedItem;
    MenuItem lastSelectedItem;
    
    // Debouncing joystick
    uint32_t lastJoyTime;
    static const uint32_t JOY_DEBOUNCE_MS = 200;
    
    // Disegno
    void drawBackground();
    void drawLogo();
    void drawMenuItems();
    void drawCredits();
    void drawMenuItem(MenuItem item, bool selected);
    
    // Calcolo posizioni
    int getMenuItemY(MenuItem item);
    
    // Testi menu
    const char* getMenuItemText(MenuItem item);
};

#endif