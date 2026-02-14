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
#include "C64Menu.h"

C64Menu::C64Menu(TFT_eSPI* display, FunduinoJoystick* joystick) {
    tft = display;
    joy = joystick;
    
    selectedItem = MENU_BASIC;
    lastSelectedItem = MENU_BASIC;
    lastJoyTime = 0;
    
    logo = nullptr;
    logoWidth = 0;
    logoHeight = 0;
    hasLogo = false;
}

void C64Menu::setLogo(const unsigned short* logoData, int width, int height) {
    logo = logoData;
    logoWidth = width;
    logoHeight = height;
    hasLogo = (logo != nullptr);
}

void C64Menu::begin() {
    Serial.println("C64Menu: Init...");
    draw();
}

void C64Menu::draw() {
    drawBackground();
    drawLogo();
    drawMenuItems();
    drawCredits();
}

void C64Menu::drawBackground() {
    tft->fillScreen(TFT_BLUE);
}

void C64Menu::drawLogo() {
    if (!hasLogo) {
        // Se non c'è logo, mostra testo "COMMODORE 64"
        tft->setTextColor(TFT_CYAN);
        tft->setTextSize(3);
        tft->setTextDatum(TC_DATUM); // Top Center
        tft->drawString("COMMODORE 64", 160, 20);
        return;
    }
    
    // centra logo orizzontalmente
    int logoX = (320 - logoWidth) / 2;
    int logoY = 20;
    
    // abilita swap bytes per RGB565
    tft->setSwapBytes(true);
    tft->pushImage(logoX, logoY, logoWidth, logoHeight, logo, 0x0000);
    tft->setSwapBytes(false);  // ripristina per altri usi
}

void C64Menu::drawCredits() {
    tft->setTextSize(1);
    tft->setTextColor(TFT_CYAN);
    tft->setTextDatum(BR_DATUM); // basso a destra (BR)
    tft->drawString("@IV COMMODORE 64 emulator", 315, 235);
}

void C64Menu::drawMenuItems() {
    for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
        drawMenuItem((MenuItem)i, i == selectedItem);
    }
}

void C64Menu::drawMenuItem(MenuItem item, bool selected) {
    int y = getMenuItemY(item);
    const char* text = getMenuItemText(item);
    
    // Cancella area voce (per aggiornamento)
    tft->fillRect(0, y - 2, 320, 26, TFT_BLUE);
    
    // Dimensione testo
    tft->setTextSize(2);
    tft->setTextDatum(TC_DATUM); // in alto centro (TC)
    
    if (selected) {
        // voce selezionata: disegna '>' prima del testo
        tft->setTextColor(TFT_WHITE);
        
        // calcola posizione del '>'
        int textWidth = strlen(text) * 12; // approssimazione larghezza
        int cursorX = (320 - textWidth) / 2 - 20; // 20px prima del testo
        
        tft->drawString(">", cursorX, y);
        tft->drawString(text, 160, y);
    } else {
        // voce non selezionata
        tft->setTextColor(TFT_CYAN);
        tft->drawString(text, 160, y);
    }
}

int C64Menu::getMenuItemY(MenuItem item) {
    // Logo termina circa a Y=100 (20 + altezza logo)
    // Inizia menu da Y=110
    int startY = 110;
    int spacing = 35;
    
    return startY + (item * spacing);
}

const char* C64Menu::getMenuItemText(MenuItem item) {
    switch (item) {
        case MENU_BASIC:
            return "START BASIC";
        case MENU_GAME:
            return "GAMES BROWSER";
        case MENU_SID:    
            return "SID PLAYER";
        default:
            return "???";
    }
}

MenuAction C64Menu::update() {
    // Leggi joystick
    uint8_t joyState = joy->read();
    
    // debouncing: ignora input troppo veloci
    uint32_t now = millis();
    if (now - lastJoyTime < JOY_DEBOUNCE_MS) {
        return ACTION_NONE;
    }
    
    // UP: selezione precedente
    if (!(joyState & 0x01)) {  // Bit 0 = UP
        lastJoyTime = now;
        
        if (selectedItem > 0) {
            selectedItem = (MenuItem)(selectedItem - 1);
        } else {
            selectedItem = (MenuItem)(MENU_ITEMS_COUNT - 1); // Wrap around
        }
        
        // ridisegna solo se cambiato
        if (selectedItem != lastSelectedItem) {
            drawMenuItem(lastSelectedItem, false);
            drawMenuItem(selectedItem, true);
            lastSelectedItem = selectedItem;
        }
        
        return ACTION_NONE;
    }
    
    // DOWN: selezione successiva
    if (!(joyState & 0x02)) {  // Bit 1 = DOWN
        lastJoyTime = now;
        
        if (selectedItem < MENU_ITEMS_COUNT - 1) {
            selectedItem = (MenuItem)(selectedItem + 1);
        } else {
            selectedItem = (MenuItem)0; // Wrap around
        }
        
        // ridisegna solo se cambiato
        if (selectedItem != lastSelectedItem) {
            drawMenuItem(lastSelectedItem, false);
            drawMenuItem(selectedItem, true);
            lastSelectedItem = selectedItem;
        }
        
        return ACTION_NONE;
    }
    
    // FIRE: conferma selezione
    if (!(joyState & 0x10)) {  // Bit 4 = FIRE
        lastJoyTime = now;
        
        // Feedback visivo: lampeggio
        drawMenuItem(selectedItem, false);
        delay(100);
        drawMenuItem(selectedItem, true);
        delay(100);
        
        // Ritorna azione corrispondente
        switch (selectedItem) {
            case MENU_BASIC:
                return ACTION_START_BASIC;
            case MENU_GAME:
                return ACTION_START_GAME;
            case MENU_SID:
                return ACTION_START_SID;
            default:
                return ACTION_NONE;
        }
    }
    
    return ACTION_NONE;
}