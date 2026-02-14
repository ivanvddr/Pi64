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

#ifndef JOYSTICK_CONFIG_H
#define JOYSTICK_CONFIG_H

#include "FunduinoJoystick.h"

#define JOY1_UP_PIN      7   // GP7
#define JOY1_DOWN_PIN    8   // GP8
#define JOY1_LEFT_PIN    9   // GP9
#define JOY1_RIGHT_PIN   1   // GP1
#define JOY1_FIRE_PIN    0   // GP0

#define JOY2_UP_PIN      2   // GP2
#define JOY2_DOWN_PIN    3   // GP3
#define JOY2_LEFT_PIN    4   // GP4
#define JOY2_RIGHT_PIN   5   // GP5
#define JOY2_FIRE_PIN    6   // GP6

// ===== CONFIGURAZIONE =====
#define ENABLE_JOYSTICK_1    1  // 1 = attivo, 0 = disabilitato
#define ENABLE_JOYSTICK_2    1  // 1 = attivo, 0 = disabilitato

// ===== TIMING =====
#define JOYSTICK_POLL_MS     10  // Frequenza polling (10ms = 100Hz)

// ===== HELPER FUNCTION =====
inline void initJoysticks(FunduinoJoystick** joy1, FunduinoJoystick** joy2) {
    #if ENABLE_JOYSTICK_1
        *joy1 = new FunduinoJoystick(JOYSTICK_PORT_1);
        (*joy1)->begin(JOY1_UP_PIN, JOY1_DOWN_PIN, JOY1_LEFT_PIN, 
                       JOY1_RIGHT_PIN, JOY1_FIRE_PIN);
        Serial.println("✅ Joystick Port 1 initialized");
    #else
        *joy1 = nullptr;
        Serial.println("⚠️  Joystick Port 1 disabled");
    #endif
    
    #if ENABLE_JOYSTICK_2
        *joy2 = new FunduinoJoystick(JOYSTICK_PORT_2);
        (*joy2)->begin(JOY2_UP_PIN, JOY2_DOWN_PIN, JOY2_LEFT_PIN, 
                       JOY2_RIGHT_PIN, JOY2_FIRE_PIN);
        Serial.println("✅ Joystick Port 2 initialized");
    #else
        *joy2 = nullptr;
        Serial.println("⚠️  Joystick Port 2 disabled");
    #endif
}

#endif
