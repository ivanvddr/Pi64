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

#include "cpu.h"
#include "Utilities.h"
#include <stdint.h>
#include <Arduino.h>

// Implementazione costruttore
Utilities::Utilities(cpu* _cpu) : _cpu(_cpu) {
    // Verifica che il puntatore non sia nullo
    if(_cpu == nullptr) {
        Serial.println("Errore: puntatore a CPU nullo!");
    }
}


char Utilities::petsciiToAscii(uint8_t val) {
  static const char petscii_upper[64] = {
    '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', '[', '#', ']', '^', '\\', // sostituzioni!
    ' ', '!', '"', '#', '$', '%', '&', '\'',
    '(', ')', '*', '+', ',', '-', '.', '/',
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', ':', ';', '<', '=', '>', '?'
  };
  if (val < 0x40)
    return petscii_upper[val];
  else if (val >= 0x41 && val <= 0x5A)
    return val;  // ASCII compatibile
  else if (val == 0x20)
    return ' ';
  else
    return '.';
}

void Utilities::printVideoMemHexAscii() {
  const uint16_t videoStart = VIDEOADDR; // indirizzo video C64

  const uint16_t videoEnd = videoStart + 1024; // area video è di 1000 bytes (40x25)

  for (uint16_t addr = videoStart; addr < videoEnd; addr += 16) {
    Serial.print("0x");
    if (addr < 0x1000) Serial.print('0'); // per allineare gli indirizzi
    Serial.print(addr, HEX);
    Serial.print(": ");

    // stampa i byte in HEX
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t val = _cpu->mem_read(addr + i);
      if (val < 0x10) Serial.print('0');
      Serial.print(val, HEX);
      Serial.print(" ");
    }

    Serial.print(" |");

    for (int i = 0; i < 16; i++) {
      uint8_t val = _cpu->mem_read(addr + i);
      Serial.print(petsciiToAscii(val));
    }

    Serial.println("|");
  }

  Serial.println();
}



void Utilities::inject_basic_program() {
    uint16_t addr = 0x0801;
    uint8_t* p = _cpu->memory + addr;  // Puntatore all'indirizzo base + offset

    // Riga 10: 10 PRINT "CIAO"
    uint16_t line20_ptr = 0x080E;
    *p++ = line20_ptr & 0xFF;     // Puntatore alla prossima riga (LSB)
    *p++ = line20_ptr >> 8;       // Puntatore alla prossima riga (MSB)
    *p++ = 10; *p++ = 0x00;       // Numero riga 10 (0x0A 0x00)
    *p++ = 0x99;                  // Token PRINT
    *p++ = 0x20;                  // Spazio
    *p++ = 0x22;                  // "
    *p++ = 'C'; *p++ = 'I'; *p++ = 'A'; *p++ = 'O';  // CIAO (PETSCII)
    *p++ = 0x22;                  // "
    *p++ = 0x00;                  // Fine riga

    // Riga 20: 20 GOTO 10
    uint16_t end_ptr = 0x0816;
    *p++ = end_ptr & 0xFF;
    *p++ = end_ptr >> 8;
    *p++ = 20; *p++ = 0x00;       // Numero riga 20 (0x14 0x00)
    *p++ = 0x89;  // GOTO
    *p++ = 0x20;  // Spazio
    *p++ = '1';   // 0x31 (ASCII)
    *p++ = '0';   // 0x30 (ASCII)
    *p++ = 0x00;  // Fine riga

    // Fine programma (doppio 0)
    *p++ = 0x00;
    *p++ = 0x00;

    // Aggiorna puntatori di sistema del BASIC
    _cpu->memory[0x002B] = 0x01;  // Start of BASIC (LSB)
    _cpu->memory[0x002C] = 0x08;  // Start of BASIC (MSB)
    _cpu->memory[0x002D] = 0x16;  // End of BASIC (LSB)
    _cpu->memory[0x002E] = 0x08;  // End of BASIC (MSB)

}


void Utilities::inject_bitmap_test_program() {
    uint16_t addr = 0x0801;
    uint8_t* p = _cpu->memory + addr;
    uint8_t* line_start;

    // -----------------------------
    // Riga 10: 10 POKE 53265, PEEK(53265) OR 32
    line_start = p;
    p += 2; // Saltiamo il puntatore, lo riempiremo dopo
    *p++ = 10; *p++ = 0x00; // Numero riga

    *p++ = 0x97; // POKE
    *p++ = 0x20; // spazio
    *p++ = '5'; *p++ = '3'; *p++ = '2'; *p++ = '6'; *p++ = '5';
    *p++ = ',';  
    *p++ = 0x20; // spazio
    *p++ = 0xC2; // PEEK
    *p++ = '(';
    *p++ = '5'; *p++ = '3'; *p++ = '2'; *p++ = '6'; *p++ = '5';
    *p++ = ')';
    *p++ = 0x20; // spazio
    *p++ = 0xB0; // OR
    *p++ = 0x20; // spazio
    *p++ = '3'; *p++ = '2';
    *p++ = 0x00; // Fine riga

    // Calcola e inserisce il puntatore alla prossima riga
    uint16_t next_line_ptr = addr + (p - (_cpu->memory + addr));
    line_start[0] = next_line_ptr & 0xFF;
    line_start[1] = next_line_ptr >> 8;

    // -----------------------------
    // Riga 20: 20 POKE 53270, PEEK(53270) OR 16
    line_start = p;
    p += 2; // Saltiamo il puntatore
    *p++ = 20; *p++ = 0x00; // Numero riga

    *p++ = 0x97; // POKE
    *p++ = 0x20;
    *p++ = '5'; *p++ = '3'; *p++ = '2'; *p++ = '7'; *p++ = '0';
    *p++ = ',';
    *p++ = 0x20;
    *p++ = 0xC2; // PEEK
    *p++ = '(';
    *p++ = '5'; *p++ = '3'; *p++ = '2'; *p++ = '7'; *p++ = '0';
    *p++ = ')';
    *p++ = 0x20;
    *p++ = 0xB0; // OR
    *p++ = 0x20;
    *p++ = '1'; *p++ = '6';
    *p++ = 0x00; // Fine riga

    // Calcola e inserisce il puntatore alla prossima riga
    next_line_ptr = addr + (p - (_cpu->memory + addr));
    line_start[0] = next_line_ptr & 0xFF;
    line_start[1] = next_line_ptr >> 8;

    // -----------------------------
    // Riga 30: 30 POKE 53272, 24
    line_start = p;
    p += 2; // Saltiamo il puntatore
    *p++ = 30; *p++ = 0x00; // Numero riga

    *p++ = 0x97; // POKE
    *p++ = 0x20;
    *p++ = '5'; *p++ = '3'; *p++ = '2'; *p++ = '7'; *p++ = '2';
    *p++ = ',';
    *p++ = 0x20;
    *p++ = '2'; *p++ = '4';
    *p++ = 0x00; // Fine riga

    // Calcola e inserisce il puntatore alla prossima riga
    next_line_ptr = addr + (p - (_cpu->memory + addr));
    line_start[0] = next_line_ptr & 0xFF;
    line_start[1] = next_line_ptr >> 8;

    // -----------------------------
    // Riga 40: 40 GOTO 10
    line_start = p;
    p += 2; // Saltiamo il puntatore
    *p++ = 40; *p++ = 0x00; // Numero riga

    *p++ = 0x89; // GOTO
    *p++ = 0x20;
    *p++ = '1'; *p++ = '0';
    *p++ = 0x00; // Fine riga

    // Calcola dove punterebbe la prossima riga (dopo questa)
    uint16_t after_last_line = addr + (p - (_cpu->memory + addr));
    
    // Il puntatore dell'ultima riga punta alla posizione del terminatore
    line_start[0] = after_last_line & 0xFF;
    line_start[1] = after_last_line >> 8;

    // Aggiungiamo il terminatore finale del programma (0x00 0x00)
    *p++ = 0x00;
    *p++ = 0x00;

    // Aggiorna puntatori BASIC
    uint16_t program_end = addr + (p - (_cpu->memory + addr));
    
    _cpu->memory[0x002B] = 0x01; // Start BASIC low
    _cpu->memory[0x002C] = 0x08; // Start BASIC high
    _cpu->memory[0x002D] = program_end & 0xFF;        // End BASIC low
    _cpu->memory[0x002E] = program_end >> 8;          // End BASIC high
    _cpu->memory[0x002F] = program_end & 0xFF;        // Variables start low
    _cpu->memory[0x0030] = program_end >> 8;          // Variables start high

    // Debug: stampa la lunghezza effettiva del programma
    Serial.printf("Programma BASIC creato, lunghezza: %d bytes\n", program_end - addr);
}

void Utilities::inject_generic_program(const char* programText, uint16_t startAddr) {
    // Tabella token BASIC V2 (parziale, si può estendere)
    struct TokenEntry { const char* keyword; uint8_t code; };
    const TokenEntry basicTokens[] = {
        { "END",   0x80 }, { "FOR",   0x81 }, { "NEXT",  0x82 }, { "DATA",  0x83 },
        { "INPUT#",0x84 }, { "INPUT", 0x85 }, { "DIM",   0x86 }, { "READ",  0x87 },
        { "LET",   0x88 }, { "GOTO",  0x89 }, { "RUN",   0x8A }, { "IF",    0x8B },
        { "RESTORE",0x8C},{ "GOSUB", 0x8D }, { "RETURN",0x8E }, { "REM",   0x8F },
        { "STOP",  0x90 }, { "ON",    0x91 }, { "WAIT",  0x92 }, { "LOAD",  0x93 },
        { "SAVE",  0x94 }, { "VERIFY",0x95 }, { "DEF",   0x96 }, { "POKE",  0x97 },
        { "PRINT#",0x98 }, { "PRINT", 0x99 }, { "CONT",  0x9A }, { "LIST",  0x9B },
        { "CLR",   0x9C }, { "CMD",   0x9D }, { "SYS",   0x9E }, { "OPEN",  0x9F },
        { "CLOSE", 0xA0 }, { "GET",   0xA1 }, { "NEW",   0xA2 }, { "TAB(",  0xA3 },
        { "TO",    0xA4 }, { "FN",    0xA5 }, { "SPC(",  0xA6 }, { "THEN",  0xA7 },
        { "NOT",   0xA8 }, { "STEP",  0xA9 }, { "+",     0xAA }, { "-",     0xAB },
        { "*",     0xAC }, { "/",     0xAD }, { "^",     0xAE }, { "AND",   0xAF },
        { "OR",    0xB0 }, { ">",     0xB1 }, { "=",     0xB2 }, { "<",     0xB3 },
        { "SGN",   0xB4 }, { "INT",   0xB5 }, { "ABS",   0xB6 }, { "USR",   0xB7 },
        { "FRE",   0xB8 }, { "POS",   0xB9 }, { "SQR",   0xBA }, { "RND",   0xBB },
        { "LOG",   0xBC }, { "EXP",   0xBD }, { "COS",   0xBE }, { "SIN",   0xBF },
        { "TAN",   0xC0 }, { "ATN",   0xC1 }, { "PEEK",  0xC2 }, { "LEN",   0xC3 },
        { "STR$",  0xC4 }, { "VAL",   0xC5 }, { "ASC",   0xC6 }, { "CHR$",  0xC7 },
        { "LEFT$", 0xC8 }, { "RIGHT$",0xC9 }, { "MID$",  0xCA }
    };
    const int tokenCount = sizeof(basicTokens) / sizeof(basicTokens[0]);

    uint16_t addr = startAddr;
    uint8_t* mem = _cpu->memory; // il tuo array di RAM C64

    // Copia locale per strtok
    char* progCopy = strdup(programText);
    char* line = strtok(progCopy, "\n");

    while (line) {
        // Numero di riga
        int lineNum = atoi(line);
        char* codePtr = strchr(line, ' ');
        if (!codePtr) break;
        codePtr++; // salta spazio dopo numero

        // Temp buffer per riga
        uint8_t tempLine[256];
        uint8_t* p = tempLine;

        // Numero di riga in little endian
        *p++ = lineNum & 0xFF;
        *p++ = lineNum >> 8;

        // Tokenizzazione semplice
        while (*codePtr) {
            bool tokenFound = false;

            // Prova tutti i token
            for (int t = 0; t < tokenCount; t++) {
                size_t len = strlen(basicTokens[t].keyword);
                if (strncasecmp(codePtr, basicTokens[t].keyword, len) == 0) {
                    *p++ = basicTokens[t].code;
                    codePtr += len;
                    tokenFound = true;
                    break;
                }
            }

            if (!tokenFound) {
                *p++ = *codePtr; // copia byte così com'è
                codePtr++;
            }
        }

        // Fine riga
        *p++ = 0x00;

        // Scrivi link alla prossima riga (correggiamo subito)
        uint16_t nextAddr = addr + 2 + (p - tempLine);
        mem[addr]   = nextAddr & 0xFF;
        mem[addr+1] = nextAddr >> 8;

        // Copia numero di riga + contenuto
        memcpy(mem + addr + 2, tempLine, p - tempLine);

        // Avanza
        addr = nextAddr;
        line = strtok(NULL, "\n");
    }

    // Fine programma
    mem[addr]   = 0x00;
    mem[addr+1] = 0x00;

    free(progCopy);

    // Aggiorna puntatori BASIC ($2B-$2E)
    _cpu->memory[0x002B] = startAddr & 0xFF;
    _cpu->memory[0x002C] = startAddr >> 8;
    _cpu->memory[0x002D] = addr & 0xFF;
    _cpu->memory[0x002E] = addr >> 8;

}



void Utilities::inject_run_command() {
    const char* cmd = "RUN\r";
    uint8_t len = strlen(cmd);
    _cpu->memory[0xC6] = len;             // Lunghezza buffer tastiera
    _cpu->memory[0x0277] = 0x00;          // Reset offset corrente (opzionale)
    for (uint8_t i = 0; i < len; i++) {
       _cpu->memory[0x0277 + i] = cmd[i];
    }
}

void Utilities::inject_list_command() {
    const char* cmd = "LIST\r";
    uint8_t len = strlen(cmd);
    _cpu->memory[0xC6] = len;             // Lunghezza buffer tastiera
    _cpu->memory[0x0277] = 0x00;          // Reset offset corrente (opzionale)
    for (uint8_t i = 0; i < len; i++) {
        _cpu->memory[0x0277 + i] = cmd[i];
    }
}
