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

#ifndef UTILITIES_H
#define UTILITIES_H

class cpu;

class Utilities {

  public:
    // Costruttore che riceve puntatore a cpu
    explicit Utilities(cpu* _cpu);
    bool program_injected = false;
    char petsciiToAscii(uint8_t val);
    void printVideoMemHexAscii();
    void inject_basic_program();
    void inject_bitmap_mode_program();
    void inject_bitmap_test_program();
    void inject_generic_program(const char* programText, uint16_t startAddr = 0x0801);
    void inject_run_command();
    void inject_list_command();

  private:
    cpu* _cpu;

  };

#endif