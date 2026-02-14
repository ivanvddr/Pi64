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
#include "vic2.h"
#include "cpu.h"
#include "char_rom.h"

// Tabella delle funzioni modalità 
const ModeFunction vicModes[8] = {
    &vic2::mode0,  // Standard text
    &vic2::mode1,  // Multicolor text  
    &vic2::mode2,  // Standard bitmap
    &vic2::mode3,  // Multicolor bitmap
    &vic2::mode4,  // Extended color text
    &vic2::mode5,  // Invalid text
    &vic2::mode6,  // Invalid bitmap 1
    &vic2::mode7   // Invalid bitmap 2
};

uint16_t vic2::rgbFullFrameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
uint32_t vic2::pixelPairLookup[256];
bool vic2::lookupInitialized = false;

vic2::vic2() : _cpu(nullptr), memoryBank(0), memoryBankBase(0x0000) {
    videoBuffer = new uint8_t[BUFFER_SIZE];

    dmaInProgress = false;
    
    rasterLine = 0;
    rasterCompare = 0;
    rasterIRQEnabled = false;
    rasterIRQTriggered = false;
    rendering_started = false;
    
    vcbase = 0;
    vc = 0; 
    rc = 0;
    idle = true;
    badline = false;
    denLatch = false;
    borderFlag = true;
    
    scrollX = 0;
    scrollY = 3;
    
    lineHasSprites = false;
    
    simpleCursor = true;

    vcInitializedThisFrame = false;
    
    rasterIRQAckDelay = 0;
    autoAckPending = false;
    
    memset(registers, 0, VIC_REGISTERS_COUNT);
    memset(videoBuffer, 0, BUFFER_SIZE);
    memset(spriteLine, 0, sizeof(spriteLine));
}

vic2::~vic2() {
    delete[] videoBuffer;
}

void vic2::init(TFT_eSPI* display) {
    tft = display;
    if (!lookupInitialized) {
        Serial.println("Generazione lookup table pixel...");
        for (int i = 0; i < 256; i++) {
            uint8_t highPixel = i >> 4;
            uint8_t lowPixel = i & 0x0F;
            pixelPairLookup[i] = ((uint32_t)c64_palette[highPixel] << 16) | c64_palette[lowPixel];
        }
        lookupInitialized = true;
        Serial.println("Lookup table pronta!");
    }
    
    dmaInProgress = false;

    Serial.println("\n=== TFT CONFIG CHECK ===");
    #ifdef SPI_FREQUENCY
    Serial.printf("SPI_FREQUENCY defined: %d MHz\n", SPI_FREQUENCY/1000000);
    #else
    Serial.println("WARNING: SPI_FREQUENCY not defined!");
    #endif

    tft->fillScreen(TFT_BLACK);
    tft->startWrite();
    
    reset();
}

void vic2::initMemoryPointers(cpu* cpuRef, const uint8_t* charRomPtr) {
    cpuMemory = cpuRef->memory;
    charROM = charRomPtr;
    updateMemoryCache();
}

void vic2::setCPU(cpu* cpuRef) {
    _cpu = cpuRef;
    if (cpuRef) {
        // Inizializza puntatori diretti
        extern const uint8_t charROM[4096];
        initMemoryPointers(cpuRef, charROM);
    }
}

void vic2::reset() {
    memset(registers, 0, VIC_REGISTERS_COUNT);
    registers[0x11] = 0x9B;
    registers[0x16] = 0x08;
    registers[0x18] = 0x14;
    registers[0x19] = 0x0F;
    
    rasterLine = 0;
    rasterIRQTriggered = false;
    vcbase = 0;
    vc = 0;
    rc = 0;
    idle = true;
    badline = false;
    denLatch = false;
    borderFlag = true;
    lineHasSprites = false;

    autoAckPending = false;
    autoAckTime = 0;
    
    // FORZA rendering attivo subito!
    rendering_started = true;
    
    memset(videoBuffer, 0, BUFFER_SIZE);
    
    Serial.println("VIC-II Reset completed - rendering FORCED ON");
}

void vic2::fetchVideoMatrix() {
    if (!badline) {
        bufferValid = false;
        return;
    }
    
    uint16_t videoMatrix = getVideoMatrix();
    
    for (int i = 0; i < 40; i++) {
        videoMatrixBuffer[i] = getRawMemoryByte(videoMatrix + vc + i);
        colorRamBuffer[i] = _cpu->colorram[(vc + i) & 0x3FF] & 0x0F;
    }
    
    bufferValid = true;
}

void vic2::convertAndDisplaySingleBuffer() {
    // NON entrare se DMA ancora attivo!
    if (dmaInProgress) {
        Serial.println("convertAndDisplaySingleBuffer() called while DMA busy!");
        return;
    }
    
    uint64_t startTime = micros();
    const int BYTES_PER_ROW = SCREEN_WIDTH >> 1;
    
    uint16_t* dstPtr = rgbFullFrameBuffer;
    uint8_t* srcPtr = videoBuffer;
    
    // ===== CONVERSIONE OTTIMIZZATA: Loop unroll 8 byte =====
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        // Processa 8 byte (16 pixel) alla volta
        int i;
        for (i = 0; i <= BYTES_PER_ROW - 8; i += 8) {
            // Prefetch 8 lookup (cache-friendly)
            uint32_t p0 = pixelPairLookup[srcPtr[0]];
            uint32_t p1 = pixelPairLookup[srcPtr[1]];
            uint32_t p2 = pixelPairLookup[srcPtr[2]];
            uint32_t p3 = pixelPairLookup[srcPtr[3]];
            uint32_t p4 = pixelPairLookup[srcPtr[4]];
            uint32_t p5 = pixelPairLookup[srcPtr[5]];
            uint32_t p6 = pixelPairLookup[srcPtr[6]];
            uint32_t p7 = pixelPairLookup[srcPtr[7]];
            
            // Write burst (16 write consecutive)
            dstPtr[0]  = p0 >> 16; dstPtr[1]  = p0 & 0xFFFF;
            dstPtr[2]  = p1 >> 16; dstPtr[3]  = p1 & 0xFFFF;
            dstPtr[4]  = p2 >> 16; dstPtr[5]  = p2 & 0xFFFF;
            dstPtr[6]  = p3 >> 16; dstPtr[7]  = p3 & 0xFFFF;
            dstPtr[8]  = p4 >> 16; dstPtr[9]  = p4 & 0xFFFF;
            dstPtr[10] = p5 >> 16; dstPtr[11] = p5 & 0xFFFF;
            dstPtr[12] = p6 >> 16; dstPtr[13] = p6 & 0xFFFF;
            dstPtr[14] = p7 >> 16; dstPtr[15] = p7 & 0xFFFF;
            
            srcPtr += 8;
            dstPtr += 16;
        }
        
        // Byte rimanenti (ultimi 0-7 byte della riga)
        for (; i < BYTES_PER_ROW; i++) {
            uint32_t packed = pixelPairLookup[*srcPtr++];
            *dstPtr++ = packed >> 16;
            *dstPtr++ = packed & 0xFFFF;
        }
    }
    
    uint64_t conversionTime = micros() - startTime;
    
    // Setta flag PRIMA di avviare DMA
    dmaInProgress = true;
    
    // ===== AVVIA DMA ASINCRONO =====
    tft->pushImageDMA(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, rgbFullFrameBuffer);
    
    // NON chiamare dmaWait()! Il DMA gira in background
    
    // Log solo se la conversione è lenta (>8ms) 
    /*
    if (conversionTime > 8000) {
        Serial.printf("Conversion slow: %lu μs (%.1f FPS)\n", 
                      conversionTime, 1000000.0/conversionTime);
    } */
}

//super ottimizzato con bypass del PLA
inline uint8_t vic2::getRawMemoryByte(uint16_t addr) {
    return vicReadMemoryDirect(addr);
}


inline uint8_t vic2::getCharsetByte(uint8_t charCode, uint8_t row) {
    uint16_t charsetBase = getCharsetBase();
    uint16_t charAddr;
    
    if (getECM()) {
        charAddr = charsetBase + (charCode & 0x3F) * 8 + row;
    } else {
        charAddr = charsetBase + charCode * 8 + row;
    }
    
    return getRawMemoryByte(charAddr);
}

// setPixel4bit ultra-veloce (rimosse bounds check)
inline void vic2::setPixel4bit(uint8_t* buffer, int x, int y, uint8_t colorIndex) {
    int pixelIndex = y * SCREEN_WIDTH + x;
    int byteIndex = pixelIndex >> 1;  // Shift invece di divisione
    
    if (pixelIndex & 1) {
        buffer[byteIndex] = (buffer[byteIndex] & 0xF0) | (colorIndex & 0x0F);
    } else {
        buffer[byteIndex] = (buffer[byteIndex] & 0x0F) | ((colorIndex & 0x0F) << 4);
    }
}

inline uint8_t vic2::getPixel4bit(uint8_t* buffer, int x, int y) {
    int pixelIndex = y * SCREEN_WIDTH + x;
    int byteIndex = pixelIndex >> 1;
    
    return (pixelIndex & 1) ? (buffer[byteIndex] & 0x0F) : ((buffer[byteIndex] >> 4) & 0x0F);
}

void vic2::writeRegister(uint8_t address, uint8_t value) {
    address &= 0x3F;
    
    switch (address) {
        case 0x11:
            
            registers[address] = value;
            rasterCompare = (rasterCompare & 0xFF) | (((uint16_t)(value & 0x80)) << 1);
            scrollY = value & 0x07;
            rasterIRQEnabled = (registers[0x1A] & 0x01) != 0;
            
            if (rasterLine == 0x30) denLatch |= getDEN();
            checkBadLine();
            
            updateMemoryCache();
            break;
            
        case 0x12:
            rasterCompare = (rasterCompare & 0x100) | value;
            registers[address] = value;
            break;
            
        case 0x16:
            registers[address] = value;
            scrollX = value & 0x07;
            break;
            
        case 0x18:
            registers[address] = value;
            updateMemoryCache();
            break;
            
        case 0x19:
            registers[0x19] &= ~(value & 0x0F);
            
            if (value & 0x01) {
                rasterIRQTriggered = false;
                // Il gioco ha fatto l'ACK, cancella il timer
                autoAckPending = false;
                rasterIRQAckDelay = 0;
            }
            
            if (registers[0x19] & 0x7F) {
                registers[0x19] |= 0x80;
            } else {
                registers[0x19] &= 0x7F;
            }
            break;
            
        case 0x1A:
            registers[address] = value & 0x0F;
            rasterIRQEnabled = (value & 0x01) != 0;
            break;
            
        case 0x1E:
        case 0x1F:
            registers[address] = 0;
            break;
            
        case 0x20 ... 0x2E:
            registers[address] = value & 0x0F;
            break;
            
        case 0x2F ... 0x3F:
            break;
            
        default:
            registers[address] = value;
            break;
    }
}

void vic2::setMemoryBank(uint8_t bank) {
    uint8_t oldBank = memoryBank;
    memoryBank = bank & 0x03;
    memoryBankBase = memoryBank * 0x4000;
    
    if (oldBank != memoryBank) {
        // Serial.printf("[VIC] Bank: %d->%d ($%04X)\n", oldBank, memoryBank, memoryBankBase);
        
        // Aggiorna i puntatori
        updateMemoryCache();
        
        // Se ECM è attivo, verifica che il charset sia in RAM
        if (getECM()) {
            uint16_t charsetAddr = getCharsetBase();
        }
    }
}

// Raster management inline
inline void vic2::updateRaster() {
    rasterLine++;
    if (rasterLine >= RASTER_LINES) {
        rasterLine = 0;
        vcbase = 0;
        denLatch = false;
    }
    
    checkRasterIRQ();
    checkBadLine();
}


inline void vic2::checkRasterIRQ() {

      if (autoAckPending) {
        uint32_t now = micros();
        if ((int32_t)(now - autoAckTime) >= 0) {  // Timeout scaduto
            registers[0x19] &= ~0x01;
            rasterIRQTriggered = false;
            autoAckPending = false;
            
            // Update bit 7
            if (registers[0x19] & 0x7F) {
                registers[0x19] |= 0x80;
            } else {
                registers[0x19] &= 0x7F;
            }
        }
    }

    if (rasterLine == rasterCompare) {
        if (rasterIRQEnabled && !rasterIRQTriggered) {
            rasterIRQTriggered = true;
            registers[0x19] |= 0x01;
            
            // Avvia il timer per l'auto-ACK (50 cicli CPU)
            rasterIRQAckDelay = 50;
            autoAckTime = micros() + 100;
            autoAckPending = true;
            
            if (registers[0x19] & 0x7F) {
                registers[0x19] |= 0x80;
            }
        }
    }

}

inline void vic2::checkBadLine() {
    if (rasterLine == 0x30) denLatch |= getDEN();
    
    badline = (denLatch && (rasterLine >= 0x30) && (rasterLine <= 0xF7) && 
               ((rasterLine & 0x07) == getYSCROLL()));
    
    if (badline) {
        idle = false;
        rc = 0;
    }
    
    if (borderFlag) {
        int firstLine = getRSEL() ? 0x33 : 0x37;
        if (getDEN() && rasterLine == firstLine) borderFlag = false;
    } else {
        int lastLine = getRSEL() ? 0xFB : 0xF7;
        if (rasterLine == lastLine) borderFlag = true;
    }
}

inline void vic2::clearSpriteLine() {
    memset(spriteLine, 0, sizeof(spriteLine));
    lineHasSprites = false;
}

void vic2::renderSprites(int rasterY) {
    clearSpriteLine();
    
    uint8_t spriteEnable = registers[0x15];
    if (!spriteEnable) return;
    
    uint8_t spriteYExpand = registers[0x17];
    uint8_t spriteMDP = registers[0x1B];
    uint8_t spriteMC = registers[0x1C];
    uint8_t spriteXExpand = registers[0x1D];
    
    uint8_t spritePixelMask[SPRITE_MAX_X] = {0};
    
    for (int i = 0; i < 8; i++) {
        if (!(spriteEnable & (1 << i))) continue;
        
        uint16_t spriteY = registers[i * 2 + 1];
        uint16_t spriteX = registers[i * 2] | (((registers[0x10] >> i) & 1) << 8);
        
        if (spriteX >= SPRITE_MAX_X) continue;
        
        int spriteHeight = (spriteYExpand & (1 << i)) ? 42 : 21;
        if (rasterY < spriteY || rasterY >= spriteY + spriteHeight) continue;
        
        int spLine = rasterY - spriteY;
        if (spriteYExpand & (1 << i)) spLine /= 2;
        
        // Fetch sprite data
        uint16_t videoMatrix = getVideoMatrix();
        uint16_t spriteAddr = videoMatrix + 0x3F8 + i;
        uint8_t spritePointer = getRawMemoryByte(spriteAddr);
        uint16_t dataAddr = (memoryBank * 0x4000) + (spritePointer << 6) + (spLine * 3);
        
        uint32_t spriteData = (getRawMemoryByte(dataAddr) << 16) |
                             (getRawMemoryByte(dataAddr + 1) << 8) |
                             getRawMemoryByte(dataAddr + 2);
        
        if (!spriteData) continue;
        lineHasSprites = true;
        
        uint8_t spriteColor = registers[0x27 + i] & 0x0F;
        uint16_t spriteInfo = 0x8000 | (i << 8) | spriteColor;
        if (spriteMDP & (1 << i)) spriteInfo |= 0x4000;
        
        uint8_t spriteMask = (1 << i);
        bool isXExpanded = (spriteXExpand & (1 << i)) != 0;
        
        if (spriteMC & (1 << i)) {
            // ==================== SPRITE MULTICOLOR ====================
            uint8_t mc0 = registers[0x25] & 0x0F;
            uint8_t mc1 = registers[0x26] & 0x0F;
            
            for (int bit = 0; bit < 12; bit++) {
                int shiftAmount = 22 - (bit * 2);
                uint8_t pixelPair = (spriteData >> shiftAmount) & 0x03;
                
                if (pixelPair == 0) continue;
                
                uint8_t color;
                switch (pixelPair) {
                    case 1: color = mc0; break;
                    case 2: color = spriteColor; break;
                    case 3: color = mc1; break;
                    default: continue;
                }
                
                uint16_t info = (spriteInfo & 0xFFF0) | color;
                
                // *** FIX: Calcolo corretto posizione X ***
                int pixelsPerBit = isXExpanded ? 4 : 2;
                int xPos = spriteX + (bit * pixelsPerBit);
                
                for (int px = 0; px < pixelsPerBit; px++) {
                    int x = xPos + px;
                    
                    if (x < 0 || x >= SPRITE_MAX_X) continue;
                    
                    if (spritePixelMask[x] != 0) {
                        registers[0x1E] |= spritePixelMask[x] | spriteMask;
                    }
                    spritePixelMask[x] |= spriteMask;
                    spriteLine[x] = info;
                }
            }
        } else {
            // ==================== SPRITE HIRES ====================
            
            for (int bit = 0; bit < 24; bit++) {
                if (!(spriteData & (1 << (23 - bit)))) continue;
                
                // *** FIX: Calcolo corretto posizione X ***
                int pixelsPerBit = isXExpanded ? 2 : 1;
                int xPos = spriteX + (bit * pixelsPerBit);
                
                for (int px = 0; px < pixelsPerBit; px++) {
                    int x = xPos + px;
                    
                    if (x < 0 || x >= SPRITE_MAX_X) continue;
                    
                    if (spritePixelMask[x] != 0) {
                        registers[0x1E] |= spritePixelMask[x] | spriteMask;
                    }
                    spritePixelMask[x] |= spriteMask;
                    spriteLine[x] = spriteInfo;
                }
            }
        }
    }
}

// MODE 0 - Standard Text 
void vic2::mode0(uint8_t* buffer, int y, uint16_t vc) {
    // Pre-fetch costanti (una sola lettura registro)
    const uint8_t bgColor = registers[0x21] & 0x0F;
    const int currentScreenY = y >> 3;  // Shift invece di divisione
    
    // Pre-calcola puntatore base destinazione
    uint8_t* dstBase = buffer + (y * (SCREEN_WIDTH >> 1));


    if (idle) {
        // In idle: carattere da $3FFF, colori tutti 0
        uint8_t charByte = getRawMemoryByte((memoryBank * 0x4000) + 0x3FFF);
        charByte = getCharsetByte(charByte, rc);  // Usa RC corrente
        
        for (int x = 0; x < 40; x++) {
            uint8_t* dst = dstBase + (x << 2);
            
            // Tutti i pixel con color 0 (nero) come foreground
            *dst++ = ((charByte & 0x80) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x40) ? 0 : bgColor);
            *dst++ = ((charByte & 0x20) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x10) ? 0 : bgColor);
            *dst++ = ((charByte & 0x08) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x04) ? 0 : bgColor);
            *dst++ = ((charByte & 0x02) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x01) ? 0 : bgColor);
        }
        return;
    }
    
    // === CURSORE: Gestione blink ottimizzata ===
    static uint32_t lastBlinkTime = 0;
    static bool blinkState = true;
    const uint32_t now = millis();
    
    // Aggiorna blink solo quando necessario (risparmia chiamate millis())
    if ((now - lastBlinkTime) >= 500) {
        blinkState = !blinkState;
        lastBlinkTime = now;
    }
    
    // Pre-fetch coordinate cursore una sola volta
    const bool hasCursor = (_cpu && simpleCursor);
    const uint8_t cursorX = hasCursor ? _cpu->mem_read(0x00D3) : 0xFF;
    const uint8_t cursorY = hasCursor ? _cpu->mem_read(0x00D6) : 0xFF;
    const bool cursorOnThisRow = (hasCursor && currentScreenY == cursorY && blinkState);
    
    // === LOOP PRINCIPALE: Rendering 40 caratteri ===
    for (int x = 0; x < 40; x++) {
        // Fetch dati carattere (usa buffer se disponibile)
        uint8_t charCode = bufferValid ? videoMatrixBuffer[x] : 
                           getRawMemoryByte(getVideoMatrix() + vc + x);
        const uint8_t colorCode = bufferValid ? colorRamBuffer[x] :
                                  _cpu->colorram[(vc + x) & 0x3FF] & 0x0F;
        
        // Gestione reverse: cursore O bit 7 del carattere
        const bool isCursor = (cursorOnThisRow && x == cursorX);
        bool isReverse = (charCode & 0x80) || isCursor;
        charCode &= 0x7F;  // Maschera bit reverse
        
        // Fetch pattern carattere dalla ROM/RAM
        uint8_t charByte = getCharsetByte(charCode, rc);
        if (isReverse) charByte = ~charByte;
        
        // === RENDERING DIRETTO: 8 pixel -> 4 byte (COMPLETAMENTE UNROLLED) ===
        uint8_t* dst = dstBase + (x << 2);  // x * 4 (shift invece di moltiplicazione)
        
        // Pixel 0-1 (bit 7-6)
        *dst++ = ((charByte & 0x80) ? colorCode : bgColor) << 4 |
                 ((charByte & 0x40) ? colorCode : bgColor);
        
        // Pixel 2-3 (bit 5-4)
        *dst++ = ((charByte & 0x20) ? colorCode : bgColor) << 4 |
                 ((charByte & 0x10) ? colorCode : bgColor);
        
        // Pixel 4-5 (bit 3-2)
        *dst++ = ((charByte & 0x08) ? colorCode : bgColor) << 4 |
                 ((charByte & 0x04) ? colorCode : bgColor);
        
        // Pixel 6-7 (bit 1-0)
        *dst++ = ((charByte & 0x02) ? colorCode : bgColor) << 4 |
                 ((charByte & 0x01) ? colorCode : bgColor);
    }
}


// MODE 1 - Multicolor Text
void vic2::mode1(uint8_t* buffer, int y, uint16_t vc) {

    if (idle) {
        uint8_t charByte = getRawMemoryByte((memoryBank * 0x4000) + 0x3FFF);
        charByte = getCharsetByte(charByte, rc);
        
        // Multicolor con tutti colori = 0
        uint8_t bgColor = registers[0x21] & 0x0F;
        uint8_t* dstBase = buffer + (y * (SCREEN_WIDTH >> 1));
        
        for (int x = 0; x < 40; x++) {
            uint8_t* dst = dstBase + (x << 2);
            
            // Tratta come hires con fg=0
            *dst++ = ((charByte & 0x80) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x40) ? 0 : bgColor);
            *dst++ = ((charByte & 0x20) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x10) ? 0 : bgColor);
            *dst++ = ((charByte & 0x08) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x04) ? 0 : bgColor);
            *dst++ = ((charByte & 0x02) ? 0 : bgColor) << 4 | 
                     ((charByte & 0x01) ? 0 : bgColor);
        }
        return;
    }

    uint8_t bgColor = registers[0x21] & 0x0F;
    uint8_t mc1 = registers[0x22] & 0x0F;
    uint8_t mc2 = registers[0x23] & 0x0F;
    
    int bufferOffset = y * (SCREEN_WIDTH >> 1);
    uint8_t* dstBase = buffer + bufferOffset;
    
    for (int x = 0; x < 40; x++) {
        uint8_t charCode = bufferValid ? videoMatrixBuffer[x] : 
                          getRawMemoryByte(getVideoMatrix() + vc + x);
        uint8_t colorCode = bufferValid ? colorRamBuffer[x] :
                           _cpu->colorram[(vc + x) & 0x3FF] & 0x0F;
        
        uint8_t charByte = getCharsetByte(charCode, rc);
        uint8_t* dst = dstBase + (x << 2);
        
        if (colorCode & 0x08) {
            // Multicolor mode - 4 pixel pairs
            uint8_t fgColor = colorCode & 0x07;
            
            for (int pixelPair = 0; pixelPair < 4; pixelPair++) {
                uint8_t colorIndex = (charByte >> (6 - pixelPair * 2)) & 0x03;
                uint8_t color = (colorIndex == 0) ? bgColor : 
                               (colorIndex == 1) ? mc1 : 
                               (colorIndex == 2) ? mc2 : fgColor;
                *dst++ = (color << 4) | color;  // 2 pixel identici
            }
        } else {
            // Standard mode - 8 pixel singoli
            uint8_t p0 = (charByte & 0x80) ? colorCode : bgColor;
            uint8_t p1 = (charByte & 0x40) ? colorCode : bgColor;
            *dst++ = (p0 << 4) | p1;
            
            uint8_t p2 = (charByte & 0x20) ? colorCode : bgColor;
            uint8_t p3 = (charByte & 0x10) ? colorCode : bgColor;
            *dst++ = (p2 << 4) | p3;
            
            uint8_t p4 = (charByte & 0x08) ? colorCode : bgColor;
            uint8_t p5 = (charByte & 0x04) ? colorCode : bgColor;
            *dst++ = (p4 << 4) | p5;
            
            uint8_t p6 = (charByte & 0x02) ? colorCode : bgColor;
            uint8_t p7 = (charByte & 0x01) ? colorCode : bgColor;
            *dst++ = (p6 << 4) | p7;
        }
    }
}


// MODE 2 - Standard Bitmap (per giochi)
void vic2::mode2(uint8_t* buffer, int y, uint16_t vc) {
    uint16_t bitmapBase = getBitmapBase();
    
    int bufferOffset = y * (SCREEN_WIDTH >> 1);
    uint8_t* dstBase = buffer + bufferOffset;
    
    for (int x = 0; x < 40; x++) {
        uint8_t screenData = bufferValid ? videoMatrixBuffer[x] :
                            getRawMemoryByte(getVideoMatrix() + vc + x);
        uint8_t fgColor = (screenData >> 4) & 0x0F;
        uint8_t bgColor = screenData & 0x0F;
        
        uint16_t bitmapAddr = bitmapBase + (vc + x) * 8 + rc;
        uint8_t bitmapByte = getRawMemoryByte(bitmapAddr);
        
        // Rendering DIRETTO
        uint8_t* dst = dstBase + (x << 2);
        
        // Unroll completo
        uint8_t p0 = (bitmapByte & 0x80) ? fgColor : bgColor;
        uint8_t p1 = (bitmapByte & 0x40) ? fgColor : bgColor;
        *dst++ = (p0 << 4) | p1;
        
        uint8_t p2 = (bitmapByte & 0x20) ? fgColor : bgColor;
        uint8_t p3 = (bitmapByte & 0x10) ? fgColor : bgColor;
        *dst++ = (p2 << 4) | p3;
        
        uint8_t p4 = (bitmapByte & 0x08) ? fgColor : bgColor;
        uint8_t p5 = (bitmapByte & 0x04) ? fgColor : bgColor;
        *dst++ = (p4 << 4) | p5;
        
        uint8_t p6 = (bitmapByte & 0x02) ? fgColor : bgColor;
        uint8_t p7 = (bitmapByte & 0x01) ? fgColor : bgColor;
        *dst++ = (p6 << 4) | p7;
    }
}


// MODE 3 - Multicolor Bitmap (giochi grafici)
void vic2::mode3(uint8_t* buffer, int y, uint16_t vc) {

      if (idle) {
        uint8_t bitmapByte = getRawMemoryByte((memoryBank * 0x4000) + 0x3FFF);
        uint8_t bgColor = registers[0x21] & 0x0F;
        uint8_t* dstBase = buffer + (y * (SCREEN_WIDTH >> 1));
        
        for (int x = 0; x < 40; x++) {
            uint8_t* dst = dstBase + (x << 2);
            
            // Multicolor bitmap in idle: solo bgColor e nero
            for (int pixelPair = 0; pixelPair < 4; pixelPair++) {
                uint8_t idx = (bitmapByte >> (6 - pixelPair * 2)) & 0x03;
                uint8_t c = (idx == 0) ? bgColor : 0;  // 00=bg, altri=nero
                *dst++ = (c << 4) | c;
            }
        }
        return;
    }
    
    uint8_t bgColor = registers[0x21] & 0x0F;
    uint16_t bitmapBase = getBitmapBase();
    
    int bufferOffset = y * (SCREEN_WIDTH >> 1);
    uint8_t* dstBase = buffer + bufferOffset;
    
    for (int x = 0; x < 40; x++) {
        uint8_t screenData = bufferValid ? videoMatrixBuffer[x] :
                            getRawMemoryByte(getVideoMatrix() + vc + x);
        uint8_t colorData = bufferValid ? colorRamBuffer[x] :
                           _cpu->colorram[(vc + x) & 0x3FF] & 0x0F;
        
        // Pre-calcola i 4 colori possibili
        uint8_t color0 = bgColor;
        uint8_t color1 = (screenData >> 4) & 0x0F;
        uint8_t color2 = screenData & 0x0F;
        uint8_t color3 = colorData;
        
        uint16_t bitmapAddr = bitmapBase + (vc + x) * 8 + rc;
        uint8_t bitmapByte = getRawMemoryByte(bitmapAddr);
        
        // Rendering DIRETTO (4 pixel pair = 4 byte)
        uint8_t* dst = dstBase + (x << 2);
        
        // Pixel pair 0 (bit 7-6)
        uint8_t idx0 = (bitmapByte >> 6) & 0x03;
        uint8_t c0 = (idx0 == 0) ? color0 : (idx0 == 1) ? color1 : (idx0 == 2) ? color2 : color3;
        *dst++ = (c0 << 4) | c0;
        
        // Pixel pair 1 (bit 5-4)
        uint8_t idx1 = (bitmapByte >> 4) & 0x03;
        uint8_t c1 = (idx1 == 0) ? color0 : (idx1 == 1) ? color1 : (idx1 == 2) ? color2 : color3;
        *dst++ = (c1 << 4) | c1;
        
        // Pixel pair 2 (bit 3-2)
        uint8_t idx2 = (bitmapByte >> 2) & 0x03;
        uint8_t c2 = (idx2 == 0) ? color0 : (idx2 == 1) ? color1 : (idx2 == 2) ? color2 : color3;
        *dst++ = (c2 << 4) | c2;
        
        // Pixel pair 3 (bit 1-0)
        uint8_t idx3 = bitmapByte & 0x03;
        uint8_t c3 = (idx3 == 0) ? color0 : (idx3 == 1) ? color1 : (idx3 == 2) ? color2 : color3;
        *dst++ = (c3 << 4) | c3;
    }
}


// MODE 4 - Extended Color Text
void vic2::mode4(uint8_t* buffer, int y, uint16_t vc) {
    // Pre-carica i 4 background color
    uint8_t bgColors[4] = {
        static_cast<uint8_t>(registers[0x21] & 0x0F),
        static_cast<uint8_t>(registers[0x22] & 0x0F),
        static_cast<uint8_t>(registers[0x23] & 0x0F),
        static_cast<uint8_t>(registers[0x24] & 0x0F)
    };
    
    int bufferOffset = y * (SCREEN_WIDTH >> 1);
    uint8_t* dstBase = buffer + bufferOffset;
    
    for (int x = 0; x < 40; x++) {
        uint8_t screenData = bufferValid ? videoMatrixBuffer[x] :
                            getRawMemoryByte(getVideoMatrix() + vc + x);
        uint8_t colorCode = bufferValid ? colorRamBuffer[x] :
                           _cpu->colorram[(vc + x) & 0x3FF] & 0x0F;
        
        uint8_t bgColorIndex = (screenData >> 6) & 0x03;
        uint8_t charCode = screenData & 0x3F;
        uint8_t bgColor = bgColors[bgColorIndex];
        
        uint8_t charByte = getCharsetByte(charCode, rc);
        
        // Rendering DIRETTO
        uint8_t* dst = dstBase + (x << 2);
        
        uint8_t p0 = (charByte & 0x80) ? colorCode : bgColor;
        uint8_t p1 = (charByte & 0x40) ? colorCode : bgColor;
        *dst++ = (p0 << 4) | p1;
        
        uint8_t p2 = (charByte & 0x20) ? colorCode : bgColor;
        uint8_t p3 = (charByte & 0x10) ? colorCode : bgColor;
        *dst++ = (p2 << 4) | p3;
        
        uint8_t p4 = (charByte & 0x08) ? colorCode : bgColor;
        uint8_t p5 = (charByte & 0x04) ? colorCode : bgColor;
        *dst++ = (p4 << 4) | p5;
        
        uint8_t p6 = (charByte & 0x02) ? colorCode : bgColor;
        uint8_t p7 = (charByte & 0x01) ? colorCode : bgColor;
        *dst++ = (p6 << 4) | p7;
    }
}


// MODE 5 - Invalid Text (ECM=1, BMM=0, MCM=1)
// Su VIC-II reale: si comporta come Extended Color Text ma con glitch
void vic2::mode5(uint8_t* buffer, int y, uint16_t vc) {
    // Modalità 5: simile a Extended Color Text (mode4) ma con comportamenti strani
    uint8_t bgColors[4] = {
        static_cast<uint8_t>(registers[0x21] & 0x0F),
        static_cast<uint8_t>(registers[0x22] & 0x0F),
        static_cast<uint8_t>(registers[0x23] & 0x0F),
        static_cast<uint8_t>(registers[0x24] & 0x0F)
    };
    
    int bufferOffset = y * (SCREEN_WIDTH >> 1);
    uint8_t* dstBase = buffer + bufferOffset;
    
    for (int x = 0; x < 40; x++) {
        // Fetch come in modalità Extended Color Text
        uint8_t screenData = bufferValid ? videoMatrixBuffer[x] :
                            getRawMemoryByte(getVideoMatrix() + vc + x);
        uint8_t colorCode = bufferValid ? colorRamBuffer[x] :
                           _cpu->colorram[(vc + x) & 0x3FF] & 0x0F;
        
        // In modalità 5, i bit 6-7 selezionano bg color, bit 0-5 char code
        uint8_t bgColorIndex = (screenData >> 6) & 0x03;
        uint8_t charCode = screenData & 0x3F;
        
        // Ma MCM=1 potrebbe influenzare l'interpretazione
        // Su VIC reale, può mostrare glitch o pixel casuali
        uint8_t bgColor = bgColors[bgColorIndex];
        
        // Fetch carattere (usa charset base)
        uint8_t charByte = getCharsetByte(charCode, rc);
        
        // INVALID MODE: MCM=1 potrebbe far interpretare come multicolor
        // ma ECM=1 normalmente disabilita multicolor. Su hardware reale,
        // questo produce glitch.
        
        // renderizzo come Extended Color Text normale
        // (questo funziona per molti giochi)
        uint8_t* dst = dstBase + (x << 2);
        
        uint8_t p0 = (charByte & 0x80) ? colorCode : bgColor;
        uint8_t p1 = (charByte & 0x40) ? colorCode : bgColor;
        *dst++ = (p0 << 4) | p1;
        
        uint8_t p2 = (charByte & 0x20) ? colorCode : bgColor;
        uint8_t p3 = (charByte & 0x10) ? colorCode : bgColor;
        *dst++ = (p2 << 4) | p3;
        
        uint8_t p4 = (charByte & 0x08) ? colorCode : bgColor;
        uint8_t p5 = (charByte & 0x04) ? colorCode : bgColor;
        *dst++ = (p4 << 4) | p5;
        
        uint8_t p6 = (charByte & 0x02) ? colorCode : bgColor;
        uint8_t p7 = (charByte & 0x01) ? colorCode : bgColor;
        *dst++ = (p6 << 4) | p7;
    }
}


// MODE 6 - Invalid Bitmap 1 (ECM=1, BMM=1, MCM=0)
void vic2::mode6(uint8_t* buffer, int y, uint16_t vc) {
    // Simile a Standard Bitmap (mode2) ma con ECM=1
    uint16_t bitmapBase = getBitmapBase();
    
    int bufferOffset = y * (SCREEN_WIDTH >> 1);
    uint8_t* dstBase = buffer + bufferOffset;
    
    for (int x = 0; x < 40; x++) {
        uint8_t screenData = bufferValid ? videoMatrixBuffer[x] :
                            getRawMemoryByte(getVideoMatrix() + vc + x);
        
        // In modalità bitmap, screenData contiene i colori
        uint8_t fgColor = (screenData >> 4) & 0x0F;
        uint8_t bgColor = screenData & 0x0F;
        
        // Ma ECM=1 potrebbe alterare l'interpretazione
        // Per sicurezza, usiamo i colori di background estesi
        uint8_t bgColors[4] = {
            static_cast<uint8_t>(registers[0x21] & 0x0F),
            static_cast<uint8_t>(registers[0x22] & 0x0F),
            static_cast<uint8_t>(registers[0x23] & 0x0F),
            static_cast<uint8_t>(registers[0x24] & 0x0F)
        };
        
        // Seleziona bg color in base ai bit alti?
        uint8_t bgColorIndex = (screenData >> 6) & 0x03;
        bgColor = bgColors[bgColorIndex];
        
        uint16_t bitmapAddr = bitmapBase + (vc + x) * 8 + rc;
        uint8_t bitmapByte = getRawMemoryByte(bitmapAddr);
        
        uint8_t* dst = dstBase + (x << 2);
        
        // Rendering standard bitmap
        uint8_t p0 = (bitmapByte & 0x80) ? fgColor : bgColor;
        uint8_t p1 = (bitmapByte & 0x40) ? fgColor : bgColor;
        *dst++ = (p0 << 4) | p1;
        
        uint8_t p2 = (bitmapByte & 0x20) ? fgColor : bgColor;
        uint8_t p3 = (bitmapByte & 0x10) ? fgColor : bgColor;
        *dst++ = (p2 << 4) | p3;
        
        uint8_t p4 = (bitmapByte & 0x08) ? fgColor : bgColor;
        uint8_t p5 = (bitmapByte & 0x04) ? fgColor : bgColor;
        *dst++ = (p4 << 4) | p5;
        
        uint8_t p6 = (bitmapByte & 0x02) ? fgColor : bgColor;
        uint8_t p7 = (bitmapByte & 0x01) ? fgColor : bgColor;
        *dst++ = (p6 << 4) | p7;
    }
}


// MODE 7 - Invalid Bitmap 2 (ECM=1, BMM=1, MCM=1)
void vic2::mode7(uint8_t* buffer, int y, uint16_t vc) {
    // Simile a Multicolor Bitmap (mode3) ma con ECM=1
    uint8_t bgColor = registers[0x21] & 0x0F;
    uint16_t bitmapBase = getBitmapBase();
    
    int bufferOffset = y * (SCREEN_WIDTH >> 1);
    uint8_t* dstBase = buffer + bufferOffset;
    
    for (int x = 0; x < 40; x++) {
        uint8_t screenData = bufferValid ? videoMatrixBuffer[x] :
                            getRawMemoryByte(getVideoMatrix() + vc + x);
        uint8_t colorData = bufferValid ? colorRamBuffer[x] :
                           _cpu->colorram[(vc + x) & 0x3FF] & 0x0F;
        
        // Usa i 4 colori estesi
        uint8_t bgColors[4] = {
            static_cast<uint8_t>(registers[0x21] & 0x0F),
            static_cast<uint8_t>(registers[0x22] & 0x0F),
            static_cast<uint8_t>(registers[0x23] & 0x0F),
            static_cast<uint8_t>(registers[0x24] & 0x0F)
        };
        
        uint8_t color0 = bgColors[0];  // $D021
        uint8_t color1 = (screenData >> 4) & 0x0F;  // Hi-nibble
        uint8_t color2 = screenData & 0x0F;         // Lo-nibble
        uint8_t color3 = colorData;                 // Color RAM
        
        uint16_t bitmapAddr = bitmapBase + (vc + x) * 8 + rc;
        uint8_t bitmapByte = getRawMemoryByte(bitmapAddr);
        
        uint8_t* dst = dstBase + (x << 2);
        
        // Multicolor bitmap (4 pixel pairs)
        uint8_t idx0 = (bitmapByte >> 6) & 0x03;
        uint8_t c0 = (idx0 == 0) ? color0 : (idx0 == 1) ? color1 : (idx0 == 2) ? color2 : color3;
        *dst++ = (c0 << 4) | c0;
        
        uint8_t idx1 = (bitmapByte >> 4) & 0x03;
        uint8_t c1 = (idx1 == 0) ? color0 : (idx1 == 1) ? color1 : (idx1 == 2) ? color2 : color3;
        *dst++ = (c1 << 4) | c1;
        
        uint8_t idx2 = (bitmapByte >> 2) & 0x03;
        uint8_t c2 = (idx2 == 0) ? color0 : (idx2 == 1) ? color1 : (idx2 == 2) ? color2 : color3;
        *dst++ = (c2 << 4) | c2;
        
        uint8_t idx3 = bitmapByte & 0x03;
        uint8_t c3 = (idx3 == 0) ? color0 : (idx3 == 1) ? color1 : (idx3 == 2) ? color2 : color3;
        *dst++ = (c3 << 4) | c3;
    }
}


void vic2::doRasterLine() {
    updateRaster();
    
    if (rasterLine == 0) {
        vcbase = 0;
        vc = 0;
        rc = 0;
        denLatch = false;
        idle = true;
        borderFlag = true;
        vcInitializedThisFrame = false;
        
        // Reset frame ready all'inizio del frame
        frameBufferReady = false;
    }
    
    if (!rendering_started) return;
    
    renderSprites(rasterLine);
    
    // Renderizza SEMPRE le linee visibili (anche se in border)
    int displayLine = -1;
    if (rasterLine >= 51 && rasterLine <= 250) {
        displayLine = rasterLine - 51;
    }

    // Inizializza al primo badline
    if (!vcInitializedThisFrame && badline) {
        vc = 0;
        vcbase = 0;
        rc = 0;
        idle = false;
        vcInitializedThisFrame = true;
    }

    if (badline) {
        idle = false;
        rc = 0;
        fetchVideoMatrix();
    }
    
    vc = vcbase;
    
    // Gestisce linee fuori display
    if (displayLine < 0 || displayLine >= 200) {
        if (!idle) {
            vc = (vc + 40) & 0x3FF;
        }
        
        if (!idle || badline) {
            rc = (rc + 1) & 0x07;
        }
        
        if (rc == 0 && rasterLine > 48) {
            vcbase = vc;
            idle = true;
        }
        
        if (badline) {
            idle = false;
        }
        
        return;
    }
    
    // Renderizza SEMPRE, anche se borderFlag è true
    // (Il border viene disegnato comunque in modalità bitmap)
    
    if (borderFlag && !getBMM()) {
        // Solo in TEXT mode usa il border solido
        uint8_t borderColor = registers[0x20] & 0x0F;
        uint8_t packedBorder = (borderColor << 4) | borderColor;
        
        int bufferOffset = displayLine * (SCREEN_WIDTH >> 1);
        memset(videoBuffer + bufferOffset, packedBorder, SCREEN_WIDTH >> 1);
    } else {
        // Modalità normale: renderizza sempre
        uint8_t mode = (getECM() ? 4 : 0) | (getBMM() ? 2 : 0) | (getMCM() ? 1 : 0);
        (this->*vicModes[mode])(videoBuffer, displayLine, vc);
    }
    
    if (lineHasSprites) {
        applySpritesOptimized(displayLine);
    }
    
    if (!idle) {
        vc = (vc + 40) & 0x3FF;
    }
    
    if (!idle || badline) {
        rc = (rc + 1) & 0x07;
    }
    
    if (rc == 0 && displayLine > 0) {
        vcbase = vc;
        idle = true;
    }
    
    if (badline) {
        idle = false;
    }
    
    // Setta frameBufferReady SEMPRE alla linea 199
    if (displayLine == 199) {
        frameBufferReady = true;
    }
}

void vic2::updateDisplay() {
    uint32_t currentTime = micros();
    
    // Se DMA è attivo, verifica se è finito
    if (dmaInProgress) {
        if (tft->dmaBusy()) {
            // DMA ancora in corso - NON fare nulla
            return;
        } else {
            // DMA finito!
            dmaInProgress = false;
        }
    }
    
    // Timeout DMA (se bloccato da >100ms, forza reset)
    static uint32_t dmaStartTime = 0;
    if (dmaInProgress) {
        if (dmaStartTime == 0) {
            dmaStartTime = currentTime;
        } else if (currentTime - dmaStartTime > 100000) {  // 100ms timeout
            Serial.println("DMA TIMEOUT! Force reset");
            dmaInProgress = false;
            dmaStartTime = 0;
        }
    } else {
        dmaStartTime = 0;
    }
    
    // Limita refresh a 20 FPS (50ms)
    if (currentTime - lastDisplayUpdate < DISPLAY_INTERVAL_US) {
        return;
    }
    
    // Verifica che il frame sia pronto
    if (!frameBufferReady) {
        return;
    }
    
    // ============================================
    // A QUESTO PUNTO:
    // - DMA è finito (dmaInProgress = false)
    // - Il nuovo frame è pronto (frameBufferReady = true)
    // - È il momento giusto per convertire e avviare DMA
    // ============================================
    
    // Converti e avvia DMA (non bloccante!)
    convertAndDisplaySingleBuffer();
    
    // Reset flag SOLO DOPO che DMA è partito
    if (dmaInProgress) {
        frameBufferReady = false;
        lastDisplayUpdate = currentTime;
    }
}


// Sprite application
void vic2::applySpritesOptimized(int displayLine) {
    uint8_t bgColorReg = registers[0x21] & 0x0F;
    
    for (int screenX = 0; screenX < SCREEN_WIDTH; screenX++) {
        // Leggi da spriteLine[24 + screenX] 
        int spriteBufferX = 24 + screenX;
        
        if (spriteBufferX >= SPRITE_MAX_X) break;  // Safety check
        
        uint16_t sprite = spriteLine[spriteBufferX];
        if (!(sprite & 0x8000)) continue;
        
        uint8_t spriteColor = sprite & 0x0F;
        bool spritePriority = (sprite & 0x4000) != 0;
        uint8_t spriteNumber = (sprite >> 8) & 0x07;
        
        // Leggi colore background
        uint8_t bgColor = getPixel4bit(videoBuffer, screenX, displayLine);
        
        // Collision detection
        bool isForeground = (bgColor != bgColorReg);
        
        if (isForeground) {
            uint8_t oldReg = registers[0x1F];
            registers[0x1F] |= (1 << spriteNumber);
            
            if ((registers[0x1F] & ~oldReg) && (registers[0x1A] & 0x02)) {
                registers[0x19] |= 0x02;
                if (registers[0x19] & 0x7F) {
                    registers[0x19] |= 0x80;
                }
            }
        }
        
        // Priority logic
        uint8_t finalColor;
        
        if (spritePriority) {
            finalColor = isForeground ? bgColor : spriteColor;
        } else {
            finalColor = spriteColor;
        }
        
        setPixel4bit(videoBuffer, screenX, displayLine, finalColor);
    }
    
    // Sprite-sprite collision IRQ
    if (registers[0x1E] && (registers[0x1A] & 0x04)) {
        registers[0x19] |= 0x04;
        if (registers[0x19] & 0x7F) {
            registers[0x19] |= 0x80;
        }
    }
}


inline void vic2::setPixel4bitFast(uint8_t* buffer, int pixelIndex, uint8_t colorIndex) {
    int byteIndex = pixelIndex >> 1;
    
    if (pixelIndex & 1) {
        buffer[byteIndex] = (buffer[byteIndex] & 0xF0) | (colorIndex & 0x0F);
    } else {
        buffer[byteIndex] = (buffer[byteIndex] & 0x0F) | ((colorIndex & 0x0F) << 4);
    }
}

void vic2::getScrollOffsets(uint8_t& outScrollX, uint8_t& outScrollY) {
    outScrollX = scrollX;
    outScrollY = scrollY;
    
    if (outScrollX > 7) outScrollX = 0;
    if (outScrollY > 7) outScrollY = 0;
}


