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
#ifndef PLA_H
#define PLA_H

#include <stdint.h>
#include "SID_shared.h"

// Forward declarations
class cpu;
class vic2;
class CIA1;
class CIA2;
class SID;

// Memory region types
enum MemoryRegion {
    MEM_RAM,
    MEM_BASIC,
    MEM_KERNAL,
    MEM_CHAR_ROM,
    MEM_VIC,
    MEM_SID,
    MEM_COLOR_RAM,
    MEM_CIA1,
    MEM_CIA2,
    MEM_IO_UNMAPPED
};

// Memory configuration basata sul 6510
struct MemoryConfig {
    MemoryRegion regions[16];  // 16 blocchi di 4KB ciascuno
    uint8_t configIndex;       // indice config corrente (0-7)
};

class PLA {
private:
    // Riferimenti ai componenti del sistema
    cpu* _cpu;
    vic2* _vic2;
    CIA1* _cia1;
    CIA2* _cia2;
    SID* _sid;
    
    // Configurazione della memoria corrente
    uint8_t currentConfig;
    
    // Matrice statica delle configurazioni (based on CHAREN/HIRAM/LORAM bits)
    static const MemoryRegion configTable[8][16];
    
    // Helper 
    inline uint8_t getConfigIndex() __attribute__((always_inline));
    inline uint8_t getEffectivePort() __attribute__((always_inline));
    void updateMemoryMap();
    inline MemoryRegion getRegionForAddress(uint16_t addr) __attribute__((always_inline));
    inline uint8_t openBusRead(uint16_t addr) __attribute__((always_inline));
    
public:
    PLA();
    MemoryConfig memConfig;
    // inizilaizzazione
    void init(cpu* cpuRef, vic2* vic2Ref, CIA1* cia1Ref, CIA2* cia2Ref, SID* sidRef);
    void reset();
    // metodi per l'accesso alla memoria
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t value);  
    uint8_t vicRead(uint16_t addr); 

    
    // Port $01 change notification
    void onPortChanged();
    
    // Debug
    void debugMemoryMap();
    uint8_t getCurrentConfig() const { return currentConfig; }
};

#endif // PLA_H