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
#ifndef FUNDUINO_JOYSTICK_H
#define FUNDUINO_JOYSTICK_H

#include <Arduino.h>

// ===== CONFIGURAZIONE =====
#define JOYSTICK_PORT_1  1
#define JOYSTICK_PORT_2  2

class FunduinoJoystick {
public:
    FunduinoJoystick(uint8_t port = JOYSTICK_PORT_1);
    
    // Inizializzazione con pin assignment
    void begin(int upPin, int downPin, int leftPin, int rightPin, int firePin);
    
    // Lettura DIRETTA senza debounce per CIA1
    uint8_t read();
    
    // Lettura con debounce (per altre applicazioni)
    uint8_t readDebounced();
    
    // Query singoli input (per debug)
    bool isUpPressed();
    bool isDownPressed();
    bool isLeftPressed();
    bool isRightPressed();
    bool isFirePressed();
    
    // Ritorna porta assegnata
    uint8_t getPort() const { return port; }
    
    // Debug
    void printStatus();
    
    // getter per ultimo stato (thread-safe)
    volatile uint8_t getLastState() const { return lastState; }
    
private:
    // Pin assignment
    int pinUp;
    int pinDown;
    int pinLeft;
    int pinRight;
    int pinFire;
    
    // Porta (1 o 2)
    uint8_t port;
    
    // Stato volatile per accesso thread-safe
    volatile uint8_t lastState;
    volatile uint8_t debouncedState;
    uint32_t lastChangeTime;
    static const uint32_t DEBOUNCE_MS = 10; 
    
    // Lettura raw con pull-up
    bool readPin(int pin);
    
    // Lettura hardware diretta
    uint8_t readHardware();
};

#endif