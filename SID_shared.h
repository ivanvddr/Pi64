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

#ifndef SID_SHARED_H
#define SID_SHARED_H

#include "configs.h"
#include "hardware/sync.h"
#include <stdint.h>

// Variabili SID condivise
extern volatile uint8_t  sidRegs[0x20];
extern volatile uint32_t sidRegChanged;
extern volatile uint8_t  sidReadRegs[0x20];


// scrittura veloce (Core0) - Lock minimo
inline void sid_write_register(uint8_t reg, uint8_t value) {
    if (reg >= 0x20) return;
    
    // scrivo senza lock (singola scrittura atomica su RP2040)
    sidRegs[reg] = value;
    
    // aggiorno flag con operazione atomica
    __atomic_or_fetch(&sidRegChanged, (1UL << reg), __ATOMIC_RELAXED);
}


// lettura (Core1) - Lock minimo
inline uint8_t sid_read_register_core1(uint8_t reg) {
    if (reg >= 0x20) return 0xFF;
    return sidRegs[reg];  // Lettura diretta (atomica su 8-bit)
}

inline uint32_t sid_get_and_clear_changed() {
    // swap atomico: leggo e azzero in un colpo solo
    return __atomic_exchange_n(&sidRegChanged, 0, __ATOMIC_RELAXED);
}

// =====================
// Lettura registri (Core0 legge da Core1)
// =====================
inline uint8_t sid_read_register(uint8_t reg) {
    if (reg >= 0x20) return 0xFF;
    
    if (reg < 0x19) {
        return sidRegs[reg];
    }
    
    switch (reg) {
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
            return sidReadRegs[reg];
        default:
            return sidRegs[reg];
    }
}

#endif