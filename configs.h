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
#ifndef CONFIGS_H
#define CONFIGS_H

#include <stdint.h>
#include <TFT_eSPI.h>
#include "ps2.h"

// =====================
// Configurazione HW
// =====================

// Display SPI (ILI9341)
#define TFT_CS   17
#define TFT_DC   21
#define TFT_RST  20  // Se non usato, metti -1

#define DEBUG

const int C64_CLOCK_HZ = 985248;  // Clock PAL C64
const int CYCLES_PER_FRAME = C64_CLOCK_HZ / 50;
const int RASTER_LINES = 312;     // Linee totali PAL
const int CYCLES_PER_LINE = CYCLES_PER_FRAME / RASTER_LINES;  // ~63 cicli per linea
const int LINES_PER_CALLBACK = 8; // Processa 8 linee per volta (312/8 = 39 callbacks per frame)

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define SECTION_HEIGHT 40
const int NR_SECTIONS = SCREEN_HEIGHT / SECTION_HEIGHT;

#define PICO_CLOCK_KHZ 276000
//#define PICO_CLOCK_KHZ 300000

// PS/2 Keyboard
#define PS2_CLOCK_PIN 28
#define PS2_DATA_PIN 27
#define SKIP_DEBOUNCE_MS 300
#define KB_LAYOUT PS2_LAYOUT_IT

// SID Audio (PWM + filtro RC verso PAM8403)
#define SID_INTERNAL_RATE 44100   // Sample rate interno
#define SID_OUTPUT_RATE 22050     // Output PWM (per non sovraccaricare)
#define DECIMATION_FACTOR 2       // 44100 / 22050
#define SID_SAMPLE_RATE 22050
//#define SID_SAMPLE_RATE 44100
//#define SID_SAMPLE_RATE 31250
//#define SID_SAMPLE_RATE 18000  // Ottimizzato per PolyBLEP selettivo @ 250MHz
#define AUDIO_PIN  15
#define SID_BATCH_SIZE 32

// SD Card
#define SD_CS      13
#define SD_MISO      12
#define SD_MOSI      11
#define SD_SCLK      10

// Macro swap RGB
inline uint16_t swapBytes(uint16_t color) {
    return (color << 8) | (color >> 8);
}

// Cursor semplificato
const bool simpleCursor = true;

#define SKIP_BUTTON_PIN 14
#define REBOOT_BUTTON_PIN 26

#define KB_TOGGLE_PIN 22  

// Opzionale: Configurazione tastiera virtuale
#define VIRTUAL_KB_Y_START 200  // Posizione Y dove inizia tastiera
#define VIRTUAL_KB_HEIGHT  40   // Altezza area tastiera

// Palette C64 standard in formato RGB565 giá con swap per TFT_eSPI
/*
inline const uint16_t c64_palette[16] = {
    0x0000,  // 0: Black
    0xFFFF,  // 1: White  
    //0x0088,  // 2: Red        (era 0x8800)
    0xA569,  
    0x1C97,  // 3: Cyan       (era 0xAAFF)
    0xDBB9,  // 4: Purple     (era 0x8C1F)
    0xE005,  // 5: Green      (era 0x05E0)
    0x1F00,  // 6: Blue       (era 0x001F)
    0xE0FF,  // 7: Yellow     (era 0xFFE0)
    0x20DA,  // 8: Orange     (era 0xDA20)
    0x8061,  // 9: Brown      (era 0x6180)
    0xAFFC,  // 10: Light Red (era 0xFCAF)
    0x2842,  // 11: Dark Grey (era 0x4228)
    0x1084,  // 12: Grey      (era 0x8410)
    0xE5AF,  // 13: Light Green (era 0xAFE5)
    0x1F84,  // 14: Light Blue (era 0x841F)
    0x18C6   // 15: Light Grey (era 0xC618)
};*/

inline const uint16_t c64_palette[16] = {
0x0000,
0xFFFF,
0x689A,
0xF86D,
0xB4A2,
0x4B5D,
0x3352,
0xB0CE,
0x47A3,
0xA26A,
0xEECB,
0x0C63,
0x518C,
0x139F,
0xF86D,
0x75AD
};


#endif