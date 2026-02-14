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
#include "PLA.h"
#include "cpu.h"
#include "vic2.h"
#include "CIA1.h"
#include "CIA2.h"
#include "SID.h"
#include "SID_shared.h"
#include "basic.h"
#include "kernal.h"
#include "char_rom.h"
#include <Arduino.h>

// Memory configuration table based on CHAREN(2)/HIRAM(1)/LORAM(0) bits
const MemoryRegion PLA::configTable[8][16] = {
    // Config 000 (0): All RAM except I/O impossible (invalid state, treat as RAM)
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM },
    
    // Config 001 (1): LORAM=1, others 0 - Character ROM visible
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_CHAR_ROM, MEM_RAM, MEM_RAM },
    
    // Config 010 (2): HIRAM=1, others 0 - Character ROM + KERNAL
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_CHAR_ROM, MEM_KERNAL, MEM_KERNAL },
    
    // Config 011 (3): HIRAM=1, LORAM=1, CHAREN=0 - BASIC + Character ROM + KERNAL
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_BASIC, MEM_BASIC, MEM_RAM, MEM_CHAR_ROM, MEM_KERNAL, MEM_KERNAL },
    
    // Config 100 (4): CHAREN=1, others 0 - All RAM
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM },
    
    // Config 101 (5): CHAREN=1, LORAM=1, HIRAM=0 - I/O visible, no ROM
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_VIC, MEM_RAM, MEM_RAM },
    
    // Config 110 (6): CHAREN=1, HIRAM=1, LORAM=0 - I/O + KERNAL
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_VIC, MEM_KERNAL, MEM_KERNAL },
    
    // Config 111 (7): Standard configuration - BASIC + I/O + KERNAL
    { MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM, MEM_RAM,
      MEM_RAM, MEM_RAM, MEM_BASIC, MEM_BASIC, MEM_RAM, MEM_VIC, MEM_KERNAL, MEM_KERNAL }
};

PLA::PLA() : _cpu(nullptr), _vic2(nullptr), _cia1(nullptr), _cia2(nullptr), _sid(nullptr), currentConfig(7) {
    // Inizializza alla configurazione standard C64
    memConfig.configIndex = 7;
    for (int i = 0; i < 16; i++) {
        memConfig.regions[i] = configTable[7][i];
    }
}

void PLA::init(cpu* cpuRef, vic2* vic2Ref, CIA1* cia1Ref, CIA2* cia2Ref, SID* sidRef) {
    _cpu = cpuRef;
    _vic2 = vic2Ref;
    _cia1 = cia1Ref;
    _cia2 = cia2Ref;
    _sid = sidRef;
    
    reset();
    Serial.println("PLA initialized");
}

void PLA::reset() {
    // Inizializza Port $00/$01 come fa il 6510 al reset hardware
    if (_cpu) {
        _cpu->memory[0] = 0x2F;  // DDR: bit 0-5 output, 6-7 input
        _cpu->memory[1] = 0x37;  // Port: LORAM=1, HIRAM=1, CHAREN=1
    }
    
    currentConfig = 7; // configurazione C64 standard (111)
    updateMemoryMap();
    Serial.println("PLA reset to standard configuration");
}

inline uint8_t PLA::getEffectivePort() {
    if (!_cpu) return 0x37;
    
    // Sul 6510 il valore effettivo è: (port & ddr) | (~ddr)
    // maschera ~ddr a 8 bit per evitare contaminazione
    uint8_t ddr = _cpu->memory[0];
    uint8_t port = _cpu->memory[1];
    uint8_t effective = (port & ddr) | ((~ddr) & 0xFF);
    
    return effective;
}

inline uint8_t PLA::getConfigIndex() {
    uint8_t effective = getEffectivePort();
    
    // calcolo diretto senza shift multipli
    return ((effective & 0x04) << 0) |  // CHAREN (bit 2) -> bit 2
           ((effective & 0x02) << 0) |  // HIRAM (bit 1) -> bit 1
           (effective & 0x01);          // LORAM (bit 0) -> bit 0
}

void PLA::updateMemoryMap() {
    uint8_t newConfig = getConfigIndex();
    
    if (newConfig != currentConfig) {
        currentConfig = newConfig;
        memConfig.configIndex = currentConfig;
        
        // copia la configurazione dalla tabella
        memcpy(memConfig.regions, configTable[currentConfig], sizeof(memConfig.regions));
        
        // Serial.printf("PLA: Memory configuration changed to %d (CHAREN=%d, HIRAM=%d, LORAM=%d)\n", currentConfig, (currentConfig>>2)&1, (currentConfig>>1)&1, currentConfig&1);
    }
}

inline MemoryRegion PLA::getRegionForAddress(uint16_t addr) {
    // shift invece di divisione
    uint8_t block = addr >> 12;
    return memConfig.regions[block];
}

inline uint8_t PLA::openBusRead(uint16_t addr)  {
    // Comportamento "open bus": tipicamente ritorna 0xFF
    // In hardware reale potrebbe essere l'ultimo valore sul bus
    // Per sicurezza ed accuratezza emulativa uso 0xFF
    return 0xFF;
}

uint8_t PLA::read(uint16_t addr) {
    // ========================================================================
    // FAST PATH 1: Zero page e stack (80% degli accessi in tipici programmi)
    // ========================================================================
    if (addr < 0x0200) {
        return _cpu->memory[addr];
    }
    
    // ========================================================================
    // FAST PATH 2: RAM bassa (comune per variabili e dati)
    // ========================================================================
    if (addr < 0xA000) {
        return _cpu->memory[addr];
    }
    
    // ========================================================================
    // SLOW PATH: ROM/IO area - usa region lookup
    // ========================================================================
    MemoryRegion region = memConfig.regions[addr >> 12];
    
    // switch con case raggruppati per frequenza
    switch (region) {
        case MEM_RAM:
            return _cpu->memory[addr];
            
        case MEM_KERNAL:
            // Offset diretto senza sottrazione ripetuta
            return kernal[addr - 0xE000];
            
        case MEM_BASIC:
            return basic[addr - 0xA000];
            
        case MEM_VIC:
            // CHAREN=1: I/O visibile - gestione ottimizzata
            
            // VIC registers (D000-D3FF) - più comune
            if (addr < 0xD400) {
                return _vic2 ? _vic2->readRegister(addr & 0x3F) : 0xFF;
            }
            
            // Color RAM (D800-DBFF) - secondo più comune
            if (addr >= 0xD800) {
                if (addr < 0xDC00) {
                    return _cpu->colorram[addr - 0xD800] & 0x0F;
                }
                // CIA1 (DC00-DCFF)
                if (addr < 0xDD00) {
                    return _cia1 ? _cia1->read(addr & 0x0F) : 0xFF;
                }
                // CIA2 (DD00-DDFF)
                if (addr < 0xDE00) {
                    return _cia2 ? _cia2->read(addr & 0x0F) : 0xFF;
                }
                return 0xFF; // Unmapped I/O
            }
            
            // SID registers (D400-D7FF)
            return sid_read_register(addr & 0x1F);
            
        case MEM_CHAR_ROM:
            // CHAREN=0: Character ROM
            return charROM[addr - 0xD000];
            
        case MEM_IO_UNMAPPED:
        default:
            return 0xFF;
    }
}

void PLA::write(uint16_t addr, uint8_t value) {
    // ========================================================================
    // FAST PATH SPECIALE: Port registers
    // ========================================================================
    if (addr < 0x0002) {
        _cpu->memory[addr] = value;
        if (addr == 0x0001 || addr == 0x0000) {
            onPortChanged();
        }
        return;
    }
    
    // ========================================================================
    // FAST PATH 1: Zero page e stack
    // ========================================================================
    if (addr < 0x0200) {
        _cpu->memory[addr] = value;
        return;
    }
    
    // ========================================================================
    // FAST PATH 2: RAM bassa
    // ========================================================================
    if (addr < 0xA000) {
        _cpu->memory[addr] = value;
        return;
    }
    
    // ========================================================================
    // SLOW PATH: ROM/IO area
    // ========================================================================
    MemoryRegion region = memConfig.regions[addr >> 12];
    
    switch (region) {
        case MEM_RAM:
            _cpu->memory[addr] = value;
            break;
            
        case MEM_BASIC:
        case MEM_KERNAL:
            // Scritture in ROM vanno nella RAM sottostante
            _cpu->memory[addr] = value;
            break;
            
        case MEM_CHAR_ROM:
            // Color RAM ha precedenza
            if (addr >= 0xD800 && addr < 0xDC00) {
                _cpu->colorram[addr - 0xD800] = value & 0x0F;
            } else {
                _cpu->memory[addr] = value;
            }
            break;
            
        case MEM_VIC:
            // I/O area - gestione ottimizzata
            
            // VIC registers (D000-D3FF) - più comune
            if (addr < 0xD400) {
                if (_vic2) _vic2->writeRegister(addr & 0x3F, value);
                return;
            }
            
            // Color RAM (D800-DBFF)
            if (addr >= 0xD800) {
                if (addr < 0xDC00) {
                    _cpu->colorram[addr - 0xD800] = value & 0x0F;
                    return;
                }
                // CIA1 (DC00-DCFF)
                if (addr < 0xDD00) {
                    if (_cia1) _cia1->write(addr & 0x0F, value);
                    return;
                }
                // CIA2 (DD00-DDFF)
                if (addr < 0xDE00) {
                    if (_cia2) _cia2->write(addr & 0x0F, value);
                    return;
                }
                return; // Unmapped I/O
            }
            
            // SID registers (D400-D7FF)
            sid_write_register(addr & 0x1F, value);
            break;
            
        case MEM_IO_UNMAPPED:
        default:
            // Ignora scritture non mappate
            break;
    }
}

// VIC-II memory access (regole di mappatura diverse)
uint8_t PLA::vicRead(uint16_t addr) {
    if (!_vic2 || !_cpu) return 0xFF;
    
    uint8_t vicBank = _vic2->getMemoryBank();
    uint16_t offset = addr & 0x3FFF;
    
    // ========================================================================
    // Fast path per bank più comuni
    // ========================================================================
    
    // Character ROM visibile solo in bank 0 e 2, offset $1000-$1FFF
    if (offset >= 0x1000 && offset < 0x2000) {
        if (vicBank == 0 || vicBank == 2) {
            return charROM[offset - 0x1000];
        }
    }
    
    // Calcolo indirizzo fisico ottimizzato (shift invece di moltiplicazione)
    uint16_t physicalAddr = (vicBank << 14) + offset;
    
    // Boundary check ottimizzato
    if (physicalAddr < 0x10000) {
        return _cpu->memory[physicalAddr];
    }
    
    return 0xFF;
}


void PLA::onPortChanged() {
    // Forza l'aggiornamento della configurazione della memoria quando $00 o $01 cambiano
    uint8_t newConfig = getConfigIndex();
    if (newConfig != currentConfig) {
        // Serial.printf("PLA: Port $01 changed, new config: %d (eff=$%02X)\n", newConfig, getEffectivePort());
        updateMemoryMap();
    }
}

void PLA::debugMemoryMap() {
    Serial.println("=== PLA Memory Map Debug ===");
    Serial.printf("Current configuration: %d\n", currentConfig);
    Serial.printf("Port $00 (DDR): $%02X\n", _cpu ? _cpu->memory[0] : 0);
    Serial.printf("Port $01 (Data): $%02X\n", _cpu ? _cpu->memory[1] : 0);
    Serial.printf("Effective Port: $%02X\n", getEffectivePort());
    Serial.printf("CHAREN=%d, HIRAM=%d, LORAM=%d\n", 
                  (currentConfig>>2)&1, (currentConfig>>1)&1, currentConfig&1);
    
    const char* regionNames[] = {
        "RAM", "BASIC", "KERNAL", "CHAR_ROM", "VIC", "SID", 
        "COLOR_RAM", "CIA1", "CIA2", "IO_UNMAPPED"
    };
    
    for (int i = 0; i < 16; i++) {
        uint16_t startAddr = i * 0x1000;
        uint16_t endAddr = startAddr + 0x0FFF;
        MemoryRegion region = memConfig.regions[i];
        
        Serial.printf("$%04X-$%04X: %s\n", startAddr, endAddr, 
                      region < 10 ? regionNames[region] : "UNKNOWN");
    }
    Serial.println("============================");
}