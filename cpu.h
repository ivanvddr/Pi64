/*
 * Questo file fa parte del progetto Pi64.
 *
 * Copyright (C) 2025 Ivan Vettori
 *
 * Questo file si ispira e trae spunti dal progetto Fake6502 CPU emulator
 * degli autori Mike Chambers e David MHS Webster
 *
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 */

#ifndef CPU_H
#define CPU_H

#include "configs.h"
#include "SID_shared.h"
#include "PLA.h"
#include "vic2.h"
#include "cia1.h"
#include "cia2.h"
#include "kernal.h"
#include "basic.h"
#include "char_rom.h"
#include <stdint.h>

class vic2;
class CIA1;
class CIA2;
class PS2Keyboard;
class SID;
class PLA;

#define VIDEOADDR 0x400
#define VIDEOLEN 1024
#define RAMSIZE 65536
#define FLAG_CARRY 0x01
#define FLAG_ZERO 0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL 0x08
#define FLAG_BREAK 0x10
#define FLAG_CONSTANT 0x20
#define FLAG_OVERFLOW 0x40
#define FLAG_SIGN 0x80
#define BASE_STACK 0x100

class cpu {
  friend class Loader;
  friend class vic2;
 
public:
  inline void prepareSidPlayer() {
      pc = 0x0000;
      a = 0;
      x = 0;
      y = 0;
      sp = 0xFF;
      cpustatus = 0x24;
  }
  
  cpu();
  int execCPU(int tickcount=1);
  void resetCPU();
  uint16_t getpc();
  uint8_t getop();
  
  inline uint8_t mem_read(uint16_t addr) __attribute__((always_inline)) {
      return mem_read_fast(addr);    
  }

  inline void mem_write(uint16_t addr, uint8_t val) __attribute__((always_inline)) {
      mem_write_fast(addr, val);
  }
  
  uint8_t memory[RAMSIZE];
  uint8_t colorram[1024];
  
  void stopBasic();
  void setVic(vic2* vic2);
  void setupCIA1(CIA1* cia1);
  void setupCIA2(CIA2* cia2);
  void setSid(SID* sid);
  void setPLA(PLA* pla);
  void nmi();
  void irq();
  PLA* _pla;

private:
  vic2* _vic2;  
  CIA1* _cia1;
  CIA2* _cia2;
  SID* _sid;
  
  // ========================================================================
  // OTTIMIZZAZIONE: Dispatch table con function pointer
  // ========================================================================
  typedef void (cpu::*OpcodeHandler)();
  static OpcodeHandler opcodeTable[256];
  static bool opcodeTableInitialized;
  static void initOpcodeTable();
  
  // Stack operations
  inline void push16(uint16_t pushval) __attribute__((always_inline));
  inline void push8(uint8_t pushval) __attribute__((always_inline));
  inline uint16_t pull16() __attribute__((always_inline));
  inline uint8_t pull8() __attribute__((always_inline));
  
  // CPU registers
  uint16_t pc;
  uint8_t sp, a, x, y, cpustatus;

  // ========================================================================
  // ISTRUZIONI FUSE: Tutte le istruzioni con addressing mode incorporato
  // ========================================================================
  
  // LDA - Load Accumulator
  void op_lda_imm();
  void op_lda_zp();
  void op_lda_zpx();
  void op_lda_abs();
  void op_lda_absx();
  void op_lda_absy();
  void op_lda_indx();
  void op_lda_indy();
  
  // LDX - Load X
  void op_ldx_imm();
  void op_ldx_zp();
  void op_ldx_zpy();
  void op_ldx_abs();
  void op_ldx_absy();
  
  // LDY - Load Y
  void op_ldy_imm();
  void op_ldy_zp();
  void op_ldy_zpx();
  void op_ldy_abs();
  void op_ldy_absx();
  
  // STA - Store Accumulator
  void op_sta_zp();
  void op_sta_zpx();
  void op_sta_abs();
  void op_sta_absx();
  void op_sta_absy();
  void op_sta_indx();
  void op_sta_indy();
  
  // STX - Store X
  void op_stx_zp();
  void op_stx_zpy();
  void op_stx_abs();
  
  // STY - Store Y
  void op_sty_zp();
  void op_sty_zpx();
  void op_sty_abs();
  
  // ADC - Add with Carry
  void op_adc_imm();
  void op_adc_zp();
  void op_adc_zpx();
  void op_adc_abs();
  void op_adc_absx();
  void op_adc_absy();
  void op_adc_indx();
  void op_adc_indy();
  
  // SBC - Subtract with Carry
  void op_sbc_imm();
  void op_sbc_zp();
  void op_sbc_zpx();
  void op_sbc_abs();
  void op_sbc_absx();
  void op_sbc_absy();
  void op_sbc_indx();
  void op_sbc_indy();
  
  // AND - Logical AND
  void op_and_imm();
  void op_and_zp();
  void op_and_zpx();
  void op_and_abs();
  void op_and_absx();
  void op_and_absy();
  void op_and_indx();
  void op_and_indy();
  
  // ORA - Logical OR
  void op_ora_imm();
  void op_ora_zp();
  void op_ora_zpx();
  void op_ora_abs();
  void op_ora_absx();
  void op_ora_absy();
  void op_ora_indx();
  void op_ora_indy();
  
  // EOR - Logical XOR
  void op_eor_imm();
  void op_eor_zp();
  void op_eor_zpx();
  void op_eor_abs();
  void op_eor_absx();
  void op_eor_absy();
  void op_eor_indx();
  void op_eor_indy();
  
  // CMP - Compare Accumulator
  void op_cmp_imm();
  void op_cmp_zp();
  void op_cmp_zpx();
  void op_cmp_abs();
  void op_cmp_absx();
  void op_cmp_absy();
  void op_cmp_indx();
  void op_cmp_indy();
  
  // CPX/CPY - Compare X/Y
  void op_cpx_imm();
  void op_cpx_zp();
  void op_cpx_abs();
  void op_cpy_imm();
  void op_cpy_zp();
  void op_cpy_abs();
  
  // BIT - Bit Test
  void op_bit_zp();
  void op_bit_abs();
  
  // ASL - Arithmetic Shift Left
  void op_asl_acc();
  void op_asl_zp();
  void op_asl_zpx();
  void op_asl_abs();
  void op_asl_absx();
  
  // LSR - Logical Shift Right
  void op_lsr_acc();
  void op_lsr_zp();
  void op_lsr_zpx();
  void op_lsr_abs();
  void op_lsr_absx();
  
  // ROL - Rotate Left
  void op_rol_acc();
  void op_rol_zp();
  void op_rol_zpx();
  void op_rol_abs();
  void op_rol_absx();
  
  // ROR - Rotate Right
  void op_ror_acc();
  void op_ror_zp();
  void op_ror_zpx();
  void op_ror_abs();
  void op_ror_absx();
  
  // INC/DEC - Increment/Decrement
  void op_inc_zp();
  void op_inc_zpx();
  void op_inc_abs();
  void op_inc_absx();
  void op_dec_zp();
  void op_dec_zpx();
  void op_dec_abs();
  void op_dec_absx();
  void op_inx();
  void op_iny();
  void op_dex();
  void op_dey();
  
  // Branches
  void op_bpl();
  void op_bmi();
  void op_bvc();
  void op_bvs();
  void op_bcc();
  void op_bcs();
  void op_bne();
  void op_beq();
  
  // Jumps/Calls
  void op_jmp_abs();
  void op_jmp_ind();
  void op_jsr();
  void op_rts();
  void op_rti();
  void op_brk();
  
  // Stack
  void op_pha();
  void op_pla();
  void op_php();
  void op_plp();
  
  // Transfers
  void op_tax();
  void op_txa();
  void op_tay();
  void op_tya();
  void op_tsx();
  void op_txs();
  
  // Flags
  void op_clc();
  void op_sec();
  void op_cli();
  void op_sei();
  void op_clv();
  void op_cld();
  void op_sed();
  void op_nop();
  
  // NOP variants
  void op_nop_imm();
  void op_nop_zp();
  void op_nop_zpx();
  void op_nop_abs();
  void op_nop_absx();
  
  // ========================================================================
  // ILLEGAL OPCODES FUSED
  // ========================================================================
  
  // SLO (ASL + ORA)
  void op_slo_zp();
  void op_slo_zpx();
  void op_slo_abs();
  void op_slo_absx();
  void op_slo_absy();
  void op_slo_indx();
  void op_slo_indy();
  
  // RLA (ROL + AND)
  void op_rla_zp();
  void op_rla_zpx();
  void op_rla_abs();
  void op_rla_absx();
  void op_rla_absy();
  void op_rla_indx();
  void op_rla_indy();
  
  // SRE (LSR + EOR)
  void op_sre_zp();
  void op_sre_zpx();
  void op_sre_abs();
  void op_sre_absx();
  void op_sre_absy();
  void op_sre_indx();
  void op_sre_indy();
  
  // RRA (ROR + ADC)
  void op_rra_zp();
  void op_rra_zpx();
  void op_rra_abs();
  void op_rra_absx();
  void op_rra_absy();
  void op_rra_indx();
  void op_rra_indy();
  
  // LAX (LDA + LDX)
  void op_lax_zp();
  void op_lax_zpy();
  void op_lax_abs();
  void op_lax_absy();
  void op_lax_indx();
  void op_lax_indy();
  
  // SAX (Store A AND X)
  void op_sax_zp();
  void op_sax_zpy();
  void op_sax_abs();
  void op_sax_indx();
  
  // DCP (DEC + CMP)
  void op_dcp_zp();
  void op_dcp_zpx();
  void op_dcp_abs();
  void op_dcp_absx();
  void op_dcp_absy();
  void op_dcp_indx();
  void op_dcp_indy();
  
  // ISC (INC + SBC)
  void op_isc_zp();
  void op_isc_zpx();
  void op_isc_abs();
  void op_isc_absx();
  void op_isc_absy();
  void op_isc_indx();
  void op_isc_indy();
  
  // Altri illegal opcodes
  void op_anc_imm();
  void op_arr_imm();
  void op_alr_imm();
  void op_axs_imm();
  void op_shx_absy();
  void op_shy_absx();
  void op_las_absy();
  void op_tas_absy();
  void op_ahx_absy();
  void op_ahx_indy();
  void op_xaa_imm();
  void op_lxa_imm();

  // ========================================================================
  // OTTIMIZZAZIONE: mem_read_fast e mem_write_fast inline
  // ========================================================================
  
  inline uint8_t mem_read_fast(uint16_t addr) {
    // FAST PATH 1: Zero page e stack (>80% degli accessi)
    if (addr < 0x0200) {
        return memory[addr];
    }
    
    // FAST PATH 2: RAM bassa (0x0200-0xA000)
    if (addr < 0xA000) {
        return memory[addr];
    }
    
    // SLOW PATH: ROM/IO area
    if (!_pla) return 0xFF;
    
    MemoryRegion region = _pla->memConfig.regions[addr >> 12];
    
    switch (region) {
        case MEM_RAM:
            return memory[addr];
            
        case MEM_KERNAL:
            return kernal[addr - 0xE000];
            
        case MEM_BASIC:
            return basic[addr - 0xA000];
            
        case MEM_VIC:
            if (addr < 0xD400) {
                return _vic2 ? _vic2->readRegister(addr & 0x3F) : 0xFF;
            }
            if (addr >= 0xD800 && addr < 0xDC00) {
                return colorram[addr - 0xD800] & 0x0F;
            }
            if (addr < 0xDD00) {
                return _cia1 ? _cia1->read(addr & 0x0F) : 0xFF;
            }
            if (addr < 0xDE00) {
                return _cia2 ? _cia2->read(addr & 0x0F) : 0xFF;
            }
            return sid_read_register(addr & 0x1F);
            
        case MEM_CHAR_ROM:
            return charROM[addr - 0xD000];
            
        case MEM_IO_UNMAPPED:
        default:
            return 0xFF;
      }
  }   

  inline void mem_write_fast(uint16_t addr, uint8_t value) {
    // FAST PATH: Port registers
    if (addr < 0x0002) {
        memory[addr] = value;
        if (addr == 0x0001 || addr == 0x0000) {
            _pla->onPortChanged();
        }
        return;
    }
    
    // FAST PATH 1: Zero page e stack
    if (addr < 0x0200) {
        memory[addr] = value;
        return;
    }
    
    // FAST PATH 2: RAM bassa
    if (addr < 0xA000) {
        memory[addr] = value;
        return;
    }
    
    // SLOW PATH: ROM/IO area
    MemoryRegion region = _pla->memConfig.regions[addr >> 12];
    
    switch (region) {
        case MEM_RAM:
            memory[addr] = value;
            break;
            
        case MEM_BASIC:
        case MEM_KERNAL:
            memory[addr] = value;
            break;
            
        case MEM_CHAR_ROM:
            if (addr >= 0xD800 && addr < 0xDC00) {
                colorram[addr - 0xD800] = value & 0x0F;
            } else {
                memory[addr] = value;
            }
            break;
            
        case MEM_VIC:
            if (addr < 0xD400) {
                if (_vic2) _vic2->writeRegister(addr & 0x3F, value);
                return;
            }
            
            if (addr >= 0xD800) {
                if (addr < 0xDC00) {
                    colorram[addr - 0xD800] = value & 0x0F;
                    return;
                }
                if (addr < 0xDD00) {
                    if (_cia1) _cia1->write(addr & 0x0F, value);
                    return;
                }
                if (addr < 0xDE00) {
                    if (_cia2) _cia2->write(addr & 0x0F, value);
                    return;
                }
                return;
            }
            
            sid_write_register(addr & 0x1F, value);
            break;
            
        case MEM_IO_UNMAPPED:
        default:
            break;
    }
  }
};

#endif