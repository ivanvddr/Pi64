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
#ifndef VIC2_NEW_H
#define VIC2_NEW_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "configs.h"

// Forward declaration
class cpu;

// Costanti VIC-II
#define VIC_REGISTERS_COUNT 64
#define SPRITE_MAX_X (SCREEN_WIDTH + 24)

// Buffer 4-bit: 320x200 pixel, ogni pixel usa 4 bit (indice palette)
#define BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 2)  // 32000 bytes

// Palette C64 standard (16 colori)
extern const uint16_t c64_palette[16];

class vic2 {
private:
    // Riferimento alla CPU
    cpu* _cpu;
    
    // Display
    TFT_eSPI* tft;

    bool vcInitializedThisFrame;

    int rasterIRQAckDelay;
    bool autoAckPending;
    uint32_t autoAckTime;

    volatile bool frameBufferReady = false;
    uint32_t lastDisplayUpdate = 0;
    //static const uint32_t DISPLAY_INTERVAL_US = 20000; // 50 FPS = 20ms
    static const uint32_t DISPLAY_INTERVAL_US = 33333; // 30 FPS = 33ms
    //static const uint32_t DISPLAY_INTERVAL_US = 40000; // 25 FPS = 40ms
    //static const uint32_t DISPLAY_INTERVAL_US = 50000; // 20 FPS = 50ms
    // Buffer RGB565 statico per evitare allocazioni continue
    static uint16_t rgbFullFrameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    
    void convertAndDisplaySingleBuffer();

    // Lookup table (anche questa statica)
    static uint32_t pixelPairLookup[256];
    static bool lookupInitialized;
    
    // Registri VIC-II
    uint8_t registers[VIC_REGISTERS_COUNT];
    
    // Buffer video 4-bit (320x200 pixel, 4 bit per pixel)
    uint8_t* videoBuffer;
    
    // Banking memoria VIC
    uint8_t memoryBank;
    uint16_t memoryBankBase;
    
    // Stato raster
    uint16_t rasterLine;
    uint16_t rasterCompare;
    bool rasterIRQEnabled;
    
    // Stato VIC
    uint16_t vcbase;  // Video counter base
    uint16_t vc;      // Video counter
    uint8_t rc;       // Row counter
    bool idle;        // Idle state
    bool badline;     // Bad line condition
    bool denLatch;    // Display enable latch
    bool borderFlag;  // Border flag
    
    // Scroll
    uint8_t scrollX;
    uint8_t scrollY;
    
    // Sprite system integrato
    uint16_t spriteLine[SPRITE_MAX_X]; // Buffer sprite per linea
    bool lineHasSprites;
    
    // Cursore
    bool simpleCursor = true;
    uint8_t lastCursorX = 0xFF;  // Per rilevare movimento
    uint8_t lastCursorY = 0xFF;

    uint8_t videoMatrixBuffer[40];  // Buffer 40 byte del VIC
    uint8_t colorRamBuffer[40];     // Anche color RAM viene latchata
    bool bufferValid;
    
    void fetchVideoMatrix();  // Fetch durante badline
    
    
    // Metodi helper
    inline uint8_t getCharsetByte(uint8_t charCode, uint8_t row) __attribute__((always_inline));
    inline void updateRaster() __attribute__((always_inline));
    inline void checkRasterIRQ() __attribute__((always_inline));
    inline void checkBadLine() __attribute__((always_inline));

    void renderSprites(int rasterY);
    inline void clearSpriteLine() __attribute__((always_inline));
    void applySpritesOptimized(int displayLine);
    
    // Buffer utilities
    inline void setPixel4bit(uint8_t* buffer, int x, int y, uint8_t colorIndex) __attribute__((always_inline));
    inline void setPixel4bitFast(uint8_t* buffer, int pixelIndex, uint8_t colorIndex) __attribute__((always_inline));
    inline uint8_t getPixel4bit(uint8_t* buffer, int x, int y) __attribute__((always_inline));
    
    // Accesso proprietà registri
    inline bool getDEN() { return (registers[0x11] & 0x10) != 0; }
    inline bool getRSEL() { return (registers[0x11] & 0x08) != 0; }
    inline bool getCSEL() { return (registers[0x16] & 0x08) != 0; }
    inline bool getECM() { return (registers[0x11] & 0x40) != 0; }
    inline bool getBMM() { return (registers[0x11] & 0x20) != 0; }
    inline bool getMCM() { return (registers[0x16] & 0x10) != 0; }
    inline uint8_t getXSCROLL() { return registers[0x16] & 0x07; }
    inline uint8_t getYSCROLL() { return registers[0x11] & 0x07; }

    // Cache per accesso diretto memoria (bypass PLA)
    uint8_t* cpuMemory;           // Puntatore diretto a cpu.memory
    const uint8_t* charROM;       // Puntatore diretto a charROM
    uint16_t cachedVideoMatrix;   // Cache indirizzi calcolati
    uint16_t cachedCharsetBase;
    uint16_t cachedBitmapBase;

    inline uint8_t vicReadMemoryDirect(uint16_t addr) __attribute__((always_inline)) {
        uint16_t offset = addr & 0x3FFF;
        
        // Character ROM visibile SOLO in bank 0 e 2, offset $1000-$1FFF
        if (offset >= 0x1000 && offset < 0x2000) {
            if (memoryBank == 0 || memoryBank == 2) {
                return charROM[offset - 0x1000];
            }
        }
        
        // Accesso diretto RAM (shift invece di moltiplicazione)
        uint16_t physicalAddr = (memoryBank << 14) + offset;
        
        // boundary check (VIC non può accedere oltre 64KB)
        return cpuMemory[physicalAddr];
    }

public:
    // Costruttore/Distruttore
    vic2();
    ~vic2();

    inline bool hasIRQ() const {
        uint8_t irqReg = registers[0x19];
        return (irqReg & 0x80) != 0;
    }

    void initMemoryPointers(cpu* cpuRef, const uint8_t* charRomPtr);

    inline void updateMemoryCache() {
        cachedVideoMatrix = (memoryBank * 0x4000) + (((registers[0x18] >> 4) & 0x0F) << 10);
        
        uint8_t charsetBits = registers[0x18] & 0x0E;
        cachedCharsetBase = (memoryBank * 0x4000) + (charsetBits << 10);
        
        cachedBitmapBase = (memoryBank * 0x4000) + ((registers[0x18] & 0x08) << 10);
        if (getECM() && getBMM()) {
            cachedBitmapBase &= 0xF9FF;
        }
    }

    volatile bool dmaInProgress;

    inline uint16_t getVideoMatrix() { 
        return cachedVideoMatrix;
    }
    inline uint16_t getCharsetBase() { 
        return cachedCharsetBase;
    }
    inline uint16_t getBitmapBase() { 
        return cachedBitmapBase;
    }
    inline uint8_t getCurrentMode() { return (getECM() ? 4 : 0) | (getBMM() ? 2 : 0) | (getMCM() ? 1 : 0); }

    inline uint8_t getRawMemoryByte(uint16_t addr) __attribute__((always_inline));

    void updateDisplay();

    // Metodi privati per rendering
    void mode0(uint8_t* buffer, int y, uint16_t vc);  // Standard text
    void mode1(uint8_t* buffer, int y, uint16_t vc);  // Multicolor text
    void mode2(uint8_t* buffer, int y, uint16_t vc);  // Standard bitmap
    void mode3(uint8_t* buffer, int y, uint16_t vc);  // Multicolor bitmap
    void mode4(uint8_t* buffer, int y, uint16_t vc);  // Extended color text
    void mode5(uint8_t* buffer, int y, uint16_t vc);  // Invalid text
    void mode6(uint8_t* buffer, int y, uint16_t vc);  // Invalid bitmap 1
    void mode7(uint8_t* buffer, int y, uint16_t vc);  // Invalid bitmap 2

    bool rendering_started;
    bool rasterIRQTriggered;
    
    // Inizializzazione
    void init(TFT_eSPI* display);
    void setCPU(cpu* cpuRef);
    void reset();
    
    // Interfaccia registri VIC-II
    inline uint8_t readRegister(uint8_t address) {
        address &= 0x3F;
        
        switch (address) {
            case 0x11:
                return (registers[address] & 0x7F) | ((rasterLine & 0x100) >> 1);
            case 0x12:
                return rasterLine & 0xFF;
            case 0x16:
                return registers[address] | 0xC0;
            case 0x18:
                return registers[address] | 0x01;
            case 0x19:
                {
                    uint8_t ret = registers[address];
                    if (ret & 0x7F) {
                        ret |= 0x80;
                    }
                    return ret;
                }
            case 0x1A:
                return registers[address] | 0xF0;
            case 0x1E:
            case 0x1F:
                {
                    uint8_t ret = registers[address];
                    registers[address] = 0;
                    return ret;
                }
            case 0x20 ... 0x2E:
                return registers[address] | 0xF0;
            case 0x2F ... 0x3F:
                return 0xFF;
            default:
                return registers[address];
        }
    }

    void writeRegister(uint8_t address, uint8_t value);
    
    // Banking memoria
    void setMemoryBank(uint8_t bank);
    uint8_t getMemoryBank() { return memoryBank; }
    
    // Rendering principale
    void doRasterLine(); 
    
    // Stato VIC
    uint16_t getCurrentRasterLine() { return rasterLine; }
    void setRasterCompare(uint16_t line) { rasterCompare = line; }
    
    // Controllo scroll
    void getScrollOffsets(uint8_t& outScrollX, uint8_t& outScrollY);
    
    
    // Utilities
    inline uint16_t swapBytes(uint16_t color) {
        return ((color & 0xFF) << 8) | ((color >> 8) & 0xFF);
    }
};

// Tabella funzioni modalità rendering
typedef void (vic2::*ModeFunction)(uint8_t* buffer, int y, uint16_t vc);
extern const ModeFunction vicModes[8];

#endif // VIC2_NEW_H