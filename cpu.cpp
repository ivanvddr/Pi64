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

#include "cpu.h"
#include "kernal.h"
#include "basic.h"
#include "char_rom.h"
#include "vic2.h"
#include "PS2Keyboard.h"
#include "CIA1.h"
#include "CIA2.h"
#include "SID.h"
#include <stdint.h>
#include <string.h>
#include <Arduino.h>

// flag modifier macros
#define setcarry() cpustatus |= FLAG_CARRY
#define clearcarry() cpustatus &= (~FLAG_CARRY)
#define setzero() cpustatus |= FLAG_ZERO
#define clearzero() cpustatus &= (~FLAG_ZERO)
#define setinterrupt() cpustatus |= FLAG_INTERRUPT
#define clearinterrupt() cpustatus &= (~FLAG_INTERRUPT)
#define setdecimal() cpustatus |= FLAG_DECIMAL
#define cleardecimal() cpustatus &= (~FLAG_DECIMAL)
#define setoverflow() cpustatus |= FLAG_OVERFLOW
#define clearoverflow() cpustatus &= (~FLAG_OVERFLOW)
#define setsign() cpustatus |= FLAG_SIGN
#define clearsign() cpustatus &= (~FLAG_SIGN)
#define setbreak() cpustatus |= FLAG_BREAK
#define clearbreak() cpustatus &= (~FLAG_BREAK)

// flag calculation macros
#define zerocalc(n)      \
	{                    \
		if ((n)&0x00FF)  \
			clearzero(); \
		else             \
			setzero();   \
	}

#define signcalc(n)      \
	{                    \
		if ((n)&0x0080)  \
			setsign();   \
		else             \
			clearsign(); \
	}

#define carrycalc(n)      \
	{                     \
		if ((n)&0xFF00)   \
			setcarry();   \
		else              \
			clearcarry(); \
	}

#define overflowcalc(n, m, o)                             \
	{                                                     \
		if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) \
			setoverflow();                                \
		else                                              \
			clearoverflow();                              \
	}

// variabili di supporto
int32_t clockgoal6502 = 0;
uint8_t opcode;


// Tabella dispatch function pointer (STATICA)
cpu::OpcodeHandler cpu::opcodeTable[256];
bool cpu::opcodeTableInitialized = false;


// Tabella cicli come costante (condivisa)
const uint8_t ticktable[256] = {
	/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
	/* 0 */ 7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6, /* 0 */
	/* 1 */ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7, /* 1 */
	/* 2 */ 6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6, /* 2 */
	/* 3 */ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7, /* 3 */
	/* 4 */ 6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6, /* 4 */
	/* 5 */ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7, /* 5 */
	/* 6 */ 6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6, /* 6 */
	/* 7 */ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7, /* 7 */
	/* 8 */ 2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, /* 8 */
	/* 9 */ 2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5, /* 9 */
	/* A */ 2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, /* A */
	/* B */ 2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4, /* B */
	/* C */ 2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, /* C */
	/* D */ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7, /* D */
	/* E */ 2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, /* E */
	/* F */ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7	/* F */
};

// ============================================================================
// COSTRUTTORE: Inizializza tabella dispatch
// ============================================================================
cpu::cpu() : _vic2(nullptr), _cia1(nullptr), _cia2(nullptr), _sid(nullptr), _pla(nullptr) {
}

// ============================================================================
// INIZIALIZZAZIONE TABELLA DISPATCH (256 ENTRY)
// ============================================================================
void cpu::initOpcodeTable() {
    // Default: tutti NOP
    for (int i = 0; i < 256; i++) {
        opcodeTable[i] = &cpu::op_nop;
    }
    
    // ===== LDA - Load Accumulator =====
    opcodeTable[0xA9] = &cpu::op_lda_imm;   // LDA #
    opcodeTable[0xA5] = &cpu::op_lda_zp;    // LDA zp
    opcodeTable[0xB5] = &cpu::op_lda_zpx;   // LDA zp,X
    opcodeTable[0xAD] = &cpu::op_lda_abs;   // LDA abs
    opcodeTable[0xBD] = &cpu::op_lda_absx;  // LDA abs,X
    opcodeTable[0xB9] = &cpu::op_lda_absy;  // LDA abs,Y
    opcodeTable[0xA1] = &cpu::op_lda_indx;  // LDA (ind,X)
    opcodeTable[0xB1] = &cpu::op_lda_indy;  // LDA (ind),Y
    
    // ===== LDX - Load X =====
    opcodeTable[0xA2] = &cpu::op_ldx_imm;   // LDX #
    opcodeTable[0xA6] = &cpu::op_ldx_zp;    // LDX zp
    opcodeTable[0xB6] = &cpu::op_ldx_zpy;   // LDX zp,Y
    opcodeTable[0xAE] = &cpu::op_ldx_abs;   // LDX abs
    opcodeTable[0xBE] = &cpu::op_ldx_absy;  // LDX abs,Y
    
    // ===== LDY - Load Y =====
    opcodeTable[0xA0] = &cpu::op_ldy_imm;   // LDY #
    opcodeTable[0xA4] = &cpu::op_ldy_zp;    // LDY zp
    opcodeTable[0xB4] = &cpu::op_ldy_zpx;   // LDY zp,X
    opcodeTable[0xAC] = &cpu::op_ldy_abs;   // LDY abs
    opcodeTable[0xBC] = &cpu::op_ldy_absx;  // LDY abs,X
    
    // ===== STA - Store Accumulator =====
    opcodeTable[0x85] = &cpu::op_sta_zp;    // STA zp
    opcodeTable[0x95] = &cpu::op_sta_zpx;   // STA zp,X
    opcodeTable[0x8D] = &cpu::op_sta_abs;   // STA abs
    opcodeTable[0x9D] = &cpu::op_sta_absx;  // STA abs,X
    opcodeTable[0x99] = &cpu::op_sta_absy;  // STA abs,Y
    opcodeTable[0x81] = &cpu::op_sta_indx;  // STA (ind,X)
    opcodeTable[0x91] = &cpu::op_sta_indy;  // STA (ind),Y
    
    // ===== STX - Store X =====
    opcodeTable[0x86] = &cpu::op_stx_zp;    // STX zp
    opcodeTable[0x96] = &cpu::op_stx_zpy;   // STX zp,Y
    opcodeTable[0x8E] = &cpu::op_stx_abs;   // STX abs
    
    // ===== STY - Store Y =====
    opcodeTable[0x84] = &cpu::op_sty_zp;    // STY zp
    opcodeTable[0x94] = &cpu::op_sty_zpx;   // STY zp,X
    opcodeTable[0x8C] = &cpu::op_sty_abs;   // STY abs
    
    // ===== ADC - Add with Carry =====
    opcodeTable[0x69] = &cpu::op_adc_imm;   // ADC #
    opcodeTable[0x65] = &cpu::op_adc_zp;    // ADC zp
    opcodeTable[0x75] = &cpu::op_adc_zpx;   // ADC zp,X
    opcodeTable[0x6D] = &cpu::op_adc_abs;   // ADC abs
    opcodeTable[0x7D] = &cpu::op_adc_absx;  // ADC abs,X
    opcodeTable[0x79] = &cpu::op_adc_absy;  // ADC abs,Y
    opcodeTable[0x61] = &cpu::op_adc_indx;  // ADC (ind,X)
    opcodeTable[0x71] = &cpu::op_adc_indy;  // ADC (ind),Y
    
    // ===== SBC - Subtract with Carry =====
    opcodeTable[0xE9] = &cpu::op_sbc_imm;   // SBC #
    opcodeTable[0xE5] = &cpu::op_sbc_zp;    // SBC zp
    opcodeTable[0xF5] = &cpu::op_sbc_zpx;   // SBC zp,X
    opcodeTable[0xED] = &cpu::op_sbc_abs;   // SBC abs
    opcodeTable[0xFD] = &cpu::op_sbc_absx;  // SBC abs,X
    opcodeTable[0xF9] = &cpu::op_sbc_absy;  // SBC abs,Y
    opcodeTable[0xE1] = &cpu::op_sbc_indx;  // SBC (ind,X)
    opcodeTable[0xF1] = &cpu::op_sbc_indy;  // SBC (ind),Y
    opcodeTable[0xEB] = &cpu::op_sbc_imm;   // SBC # (illegal)
    
    // ===== AND - Logical AND =====
    opcodeTable[0x29] = &cpu::op_and_imm;   // AND #
    opcodeTable[0x25] = &cpu::op_and_zp;    // AND zp
    opcodeTable[0x35] = &cpu::op_and_zpx;   // AND zp,X
    opcodeTable[0x2D] = &cpu::op_and_abs;   // AND abs
    opcodeTable[0x3D] = &cpu::op_and_absx;  // AND abs,X
    opcodeTable[0x39] = &cpu::op_and_absy;  // AND abs,Y
    opcodeTable[0x21] = &cpu::op_and_indx;  // AND (ind,X)
    opcodeTable[0x31] = &cpu::op_and_indy;  // AND (ind),Y
    
    // ===== ORA - Logical OR =====
    opcodeTable[0x09] = &cpu::op_ora_imm;   // ORA #
    opcodeTable[0x05] = &cpu::op_ora_zp;    // ORA zp
    opcodeTable[0x15] = &cpu::op_ora_zpx;   // ORA zp,X
    opcodeTable[0x0D] = &cpu::op_ora_abs;   // ORA abs
    opcodeTable[0x1D] = &cpu::op_ora_absx;  // ORA abs,X
    opcodeTable[0x19] = &cpu::op_ora_absy;  // ORA abs,Y
    opcodeTable[0x01] = &cpu::op_ora_indx;  // ORA (ind,X)
    opcodeTable[0x11] = &cpu::op_ora_indy;  // ORA (ind),Y
    
    // ===== EOR - Logical XOR =====
    opcodeTable[0x49] = &cpu::op_eor_imm;   // EOR #
    opcodeTable[0x45] = &cpu::op_eor_zp;    // EOR zp
    opcodeTable[0x55] = &cpu::op_eor_zpx;   // EOR zp,X
    opcodeTable[0x4D] = &cpu::op_eor_abs;   // EOR abs
    opcodeTable[0x5D] = &cpu::op_eor_absx;  // EOR abs,X
    opcodeTable[0x59] = &cpu::op_eor_absy;  // EOR abs,Y
    opcodeTable[0x41] = &cpu::op_eor_indx;  // EOR (ind,X)
    opcodeTable[0x51] = &cpu::op_eor_indy;  // EOR (ind),Y
    
    // ===== CMP - Compare Accumulator =====
    opcodeTable[0xC9] = &cpu::op_cmp_imm;   // CMP #
    opcodeTable[0xC5] = &cpu::op_cmp_zp;    // CMP zp
    opcodeTable[0xD5] = &cpu::op_cmp_zpx;   // CMP zp,X
    opcodeTable[0xCD] = &cpu::op_cmp_abs;   // CMP abs
    opcodeTable[0xDD] = &cpu::op_cmp_absx;  // CMP abs,X
    opcodeTable[0xD9] = &cpu::op_cmp_absy;  // CMP abs,Y
    opcodeTable[0xC1] = &cpu::op_cmp_indx;  // CMP (ind,X)
    opcodeTable[0xD1] = &cpu::op_cmp_indy;  // CMP (ind),Y
    
    // ===== CPX - Compare X =====
    opcodeTable[0xE0] = &cpu::op_cpx_imm;   // CPX #
    opcodeTable[0xE4] = &cpu::op_cpx_zp;    // CPX zp
    opcodeTable[0xEC] = &cpu::op_cpx_abs;   // CPX abs
    
    // ===== CPY - Compare Y =====
    opcodeTable[0xC0] = &cpu::op_cpy_imm;   // CPY #
    opcodeTable[0xC4] = &cpu::op_cpy_zp;    // CPY zp
    opcodeTable[0xCC] = &cpu::op_cpy_abs;   // CPY abs
    
    // ===== BIT - Bit Test =====
    opcodeTable[0x24] = &cpu::op_bit_zp;    // BIT zp
    opcodeTable[0x2C] = &cpu::op_bit_abs;   // BIT abs
    
    // ===== ASL - Arithmetic Shift Left =====
    opcodeTable[0x0A] = &cpu::op_asl_acc;   // ASL A
    opcodeTable[0x06] = &cpu::op_asl_zp;    // ASL zp
    opcodeTable[0x16] = &cpu::op_asl_zpx;   // ASL zp,X
    opcodeTable[0x0E] = &cpu::op_asl_abs;   // ASL abs
    opcodeTable[0x1E] = &cpu::op_asl_absx;  // ASL abs,X
    
    // ===== LSR - Logical Shift Right =====
    opcodeTable[0x4A] = &cpu::op_lsr_acc;   // LSR A
    opcodeTable[0x46] = &cpu::op_lsr_zp;    // LSR zp
    opcodeTable[0x56] = &cpu::op_lsr_zpx;   // LSR zp,X
    opcodeTable[0x4E] = &cpu::op_lsr_abs;   // LSR abs
    opcodeTable[0x5E] = &cpu::op_lsr_absx;  // LSR abs,X
    
    // ===== ROL - Rotate Left =====
    opcodeTable[0x2A] = &cpu::op_rol_acc;   // ROL A
    opcodeTable[0x26] = &cpu::op_rol_zp;    // ROL zp
    opcodeTable[0x36] = &cpu::op_rol_zpx;   // ROL zp,X
    opcodeTable[0x2E] = &cpu::op_rol_abs;   // ROL abs
    opcodeTable[0x3E] = &cpu::op_rol_absx;  // ROL abs,X
    
    // ===== ROR - Rotate Right =====
    opcodeTable[0x6A] = &cpu::op_ror_acc;   // ROR A
    opcodeTable[0x66] = &cpu::op_ror_zp;    // ROR zp
    opcodeTable[0x76] = &cpu::op_ror_zpx;   // ROR zp,X
    opcodeTable[0x6E] = &cpu::op_ror_abs;   // ROR abs
    opcodeTable[0x7E] = &cpu::op_ror_absx;  // ROR abs,X
    
    // ===== INC/DEC =====
    opcodeTable[0xE6] = &cpu::op_inc_zp;    // INC zp
    opcodeTable[0xF6] = &cpu::op_inc_zpx;   // INC zp,X
    opcodeTable[0xEE] = &cpu::op_inc_abs;   // INC abs
    opcodeTable[0xFE] = &cpu::op_inc_absx;  // INC abs,X
    opcodeTable[0xE8] = &cpu::op_inx;       // INX
    opcodeTable[0xC8] = &cpu::op_iny;       // INY
    
    opcodeTable[0xC6] = &cpu::op_dec_zp;    // DEC zp
    opcodeTable[0xD6] = &cpu::op_dec_zpx;   // DEC zp,X
    opcodeTable[0xCE] = &cpu::op_dec_abs;   // DEC abs
    opcodeTable[0xDE] = &cpu::op_dec_absx;  // DEC abs,X
    opcodeTable[0xCA] = &cpu::op_dex;       // DEX
    opcodeTable[0x88] = &cpu::op_dey;       // DEY
    
    // ===== BRANCHES =====
    opcodeTable[0x10] = &cpu::op_bpl;       // BPL
    opcodeTable[0x30] = &cpu::op_bmi;       // BMI
    opcodeTable[0x50] = &cpu::op_bvc;       // BVC
    opcodeTable[0x70] = &cpu::op_bvs;       // BVS
    opcodeTable[0x90] = &cpu::op_bcc;       // BCC
    opcodeTable[0xB0] = &cpu::op_bcs;       // BCS
    opcodeTable[0xD0] = &cpu::op_bne;       // BNE
    opcodeTable[0xF0] = &cpu::op_beq;       // BEQ
    
    // ===== JUMPS/CALLS =====
    opcodeTable[0x4C] = &cpu::op_jmp_abs;   // JMP abs
    opcodeTable[0x6C] = &cpu::op_jmp_ind;   // JMP ind
    opcodeTable[0x20] = &cpu::op_jsr;       // JSR
    opcodeTable[0x60] = &cpu::op_rts;       // RTS
    opcodeTable[0x40] = &cpu::op_rti;       // RTI
    opcodeTable[0x00] = &cpu::op_brk;       // BRK
    
    // ===== STACK =====
    opcodeTable[0x48] = &cpu::op_pha;       // PHA
    opcodeTable[0x68] = &cpu::op_pla;       // PLA
    opcodeTable[0x08] = &cpu::op_php;       // PHP
    opcodeTable[0x28] = &cpu::op_plp;       // PLP
    
    // ===== TRANSFERS =====
    opcodeTable[0xAA] = &cpu::op_tax;       // TAX
    opcodeTable[0x8A] = &cpu::op_txa;       // TXA
    opcodeTable[0xA8] = &cpu::op_tay;       // TAY
    opcodeTable[0x98] = &cpu::op_tya;       // TYA
    opcodeTable[0xBA] = &cpu::op_tsx;       // TSX
    opcodeTable[0x9A] = &cpu::op_txs;       // TXS
    
    // ===== FLAGS =====
    opcodeTable[0x18] = &cpu::op_clc;       // CLC
    opcodeTable[0x38] = &cpu::op_sec;       // SEC
    opcodeTable[0x58] = &cpu::op_cli;       // CLI
    opcodeTable[0x78] = &cpu::op_sei;       // SEI
    opcodeTable[0xB8] = &cpu::op_clv;       // CLV
    opcodeTable[0xD8] = &cpu::op_cld;       // CLD
    opcodeTable[0xF8] = &cpu::op_sed;       // SED
    
    // ===== ILLEGAL OPCODES - SLO =====
    opcodeTable[0x07] = &cpu::op_slo_zp;
    opcodeTable[0x17] = &cpu::op_slo_zpx;
    opcodeTable[0x0F] = &cpu::op_slo_abs;
    opcodeTable[0x1F] = &cpu::op_slo_absx;
    opcodeTable[0x1B] = &cpu::op_slo_absy;
    opcodeTable[0x03] = &cpu::op_slo_indx;
    opcodeTable[0x13] = &cpu::op_slo_indy;
    
    // ===== ILLEGAL OPCODES - RLA =====
    opcodeTable[0x27] = &cpu::op_rla_zp;
    opcodeTable[0x37] = &cpu::op_rla_zpx;
    opcodeTable[0x2F] = &cpu::op_rla_abs;
    opcodeTable[0x3F] = &cpu::op_rla_absx;
    opcodeTable[0x3B] = &cpu::op_rla_absy;
    opcodeTable[0x23] = &cpu::op_rla_indx;
    opcodeTable[0x33] = &cpu::op_rla_indy;
    
    // ===== ILLEGAL OPCODES - SRE =====
    opcodeTable[0x47] = &cpu::op_sre_zp;
    opcodeTable[0x57] = &cpu::op_sre_zpx;
    opcodeTable[0x4F] = &cpu::op_sre_abs;
    opcodeTable[0x5F] = &cpu::op_sre_absx;
    opcodeTable[0x5B] = &cpu::op_sre_absy;
    opcodeTable[0x43] = &cpu::op_sre_indx;
    opcodeTable[0x53] = &cpu::op_sre_indy;
    
    // ===== ILLEGAL OPCODES - RRA =====
    opcodeTable[0x67] = &cpu::op_rra_zp;
    opcodeTable[0x77] = &cpu::op_rra_zpx;
    opcodeTable[0x6F] = &cpu::op_rra_abs;
    opcodeTable[0x7F] = &cpu::op_rra_absx;
    opcodeTable[0x7B] = &cpu::op_rra_absy;
    opcodeTable[0x63] = &cpu::op_rra_indx;
    opcodeTable[0x73] = &cpu::op_rra_indy;
    
    // ===== ILLEGAL OPCODES - LAX =====
    opcodeTable[0xA7] = &cpu::op_lax_zp;
    opcodeTable[0xB7] = &cpu::op_lax_zpy;
    opcodeTable[0xAF] = &cpu::op_lax_abs;
    opcodeTable[0xBF] = &cpu::op_lax_absy;
    opcodeTable[0xA3] = &cpu::op_lax_indx;
    opcodeTable[0xB3] = &cpu::op_lax_indy;
    
    // ===== ILLEGAL OPCODES - SAX =====
    opcodeTable[0x87] = &cpu::op_sax_zp;
    opcodeTable[0x97] = &cpu::op_sax_zpy;
    opcodeTable[0x8F] = &cpu::op_sax_abs;
    opcodeTable[0x83] = &cpu::op_sax_indx;
    
    // ===== ILLEGAL OPCODES - DCP =====
    opcodeTable[0xC7] = &cpu::op_dcp_zp;
    opcodeTable[0xD7] = &cpu::op_dcp_zpx;
    opcodeTable[0xCF] = &cpu::op_dcp_abs;
    opcodeTable[0xDF] = &cpu::op_dcp_absx;
    opcodeTable[0xDB] = &cpu::op_dcp_absy;
    opcodeTable[0xC3] = &cpu::op_dcp_indx;
    opcodeTable[0xD3] = &cpu::op_dcp_indy;
    
    // ===== ILLEGAL OPCODES - ISC =====
    opcodeTable[0xE7] = &cpu::op_isc_zp;
    opcodeTable[0xF7] = &cpu::op_isc_zpx;
    opcodeTable[0xEF] = &cpu::op_isc_abs;
    opcodeTable[0xFF] = &cpu::op_isc_absx;
    opcodeTable[0xFB] = &cpu::op_isc_absy;
    opcodeTable[0xE3] = &cpu::op_isc_indx;
    opcodeTable[0xF3] = &cpu::op_isc_indy;
    
    // ===== ILLEGAL OPCODES - Altri =====
    opcodeTable[0x0B] = &cpu::op_anc_imm;
    opcodeTable[0x2B] = &cpu::op_anc_imm;
    opcodeTable[0x6B] = &cpu::op_arr_imm;
    opcodeTable[0x4B] = &cpu::op_alr_imm;
    opcodeTable[0x9E] = &cpu::op_shx_absy;
    opcodeTable[0x9C] = &cpu::op_shy_absx;
    opcodeTable[0xBB] = &cpu::op_las_absy;
    opcodeTable[0x9B] = &cpu::op_tas_absy;
    opcodeTable[0x9F] = &cpu::op_ahx_absy;
    opcodeTable[0x93] = &cpu::op_ahx_indy;
    opcodeTable[0x8B] = &cpu::op_xaa_imm;
    opcodeTable[0xCB] = &cpu::op_axs_imm;
    opcodeTable[0xAB] = &cpu::op_lxa_imm;
    
    // ===== NOP variants (illegal) =====
    opcodeTable[0x1A] = &cpu::op_nop;
    opcodeTable[0x3A] = &cpu::op_nop;
    opcodeTable[0x5A] = &cpu::op_nop;
    opcodeTable[0x7A] = &cpu::op_nop;
    opcodeTable[0xDA] = &cpu::op_nop;
    opcodeTable[0xFA] = &cpu::op_nop;
    opcodeTable[0x80] = &cpu::op_nop_imm;
    opcodeTable[0x82] = &cpu::op_nop_imm;
    opcodeTable[0x89] = &cpu::op_nop_imm;
    opcodeTable[0xC2] = &cpu::op_nop_imm;
    opcodeTable[0xE2] = &cpu::op_nop_imm;
    opcodeTable[0x04] = &cpu::op_nop_zp;
    opcodeTable[0x44] = &cpu::op_nop_zp;
    opcodeTable[0x64] = &cpu::op_nop_zp;
    opcodeTable[0x14] = &cpu::op_nop_zpx;
    opcodeTable[0x34] = &cpu::op_nop_zpx;
    opcodeTable[0x54] = &cpu::op_nop_zpx;
    opcodeTable[0x74] = &cpu::op_nop_zpx;
    opcodeTable[0xD4] = &cpu::op_nop_zpx;
    opcodeTable[0xF4] = &cpu::op_nop_zpx;
    opcodeTable[0x0C] = &cpu::op_nop_abs;
    opcodeTable[0x1C] = &cpu::op_nop_absx;
    opcodeTable[0x3C] = &cpu::op_nop_absx;
    opcodeTable[0x5C] = &cpu::op_nop_absx;
    opcodeTable[0x7C] = &cpu::op_nop_absx;
    opcodeTable[0xDC] = &cpu::op_nop_absx;
    opcodeTable[0xFC] = &cpu::op_nop_absx;
    
    // ===== JAM opcodes (halt) =====
    opcodeTable[0x02] = &cpu::op_nop;
    opcodeTable[0x12] = &cpu::op_nop;
    opcodeTable[0x22] = &cpu::op_nop;
    opcodeTable[0x32] = &cpu::op_nop;
    opcodeTable[0x42] = &cpu::op_nop;
    opcodeTable[0x52] = &cpu::op_nop;
    opcodeTable[0x62] = &cpu::op_nop;
    opcodeTable[0x72] = &cpu::op_nop;
    opcodeTable[0x92] = &cpu::op_nop;
    opcodeTable[0xB2] = &cpu::op_nop;
    opcodeTable[0xD2] = &cpu::op_nop;
    opcodeTable[0xF2] = &cpu::op_nop;
}

// ============================================================================
// SETUP E RESET
// ============================================================================

void cpu::setVic(vic2* vic2) {
   _vic2 = vic2;
}

void cpu::setSid(SID* sid) {
   _sid = sid;
}

void cpu::setPLA(PLA* pla) {
   _pla = pla;
}

void cpu::setupCIA1(CIA1* cia1) {
    _cia1 = cia1;
    Serial.printf("CIA1 + PS/2 Keyboard system ready!\n");
}

void cpu::setupCIA2(CIA2* cia2) {
    _cia2 = cia2;
    Serial.printf("CIA2 system ready!\n");
}

void cpu::resetCPU() {
    if (!opcodeTableInitialized) {
        initOpcodeTable();
        opcodeTableInitialized = true;
        Serial.println("CPU: Dispatch table initialized");
    }
    memset(memory, 0, sizeof(memory));
    memset(colorram, 0, sizeof(colorram));    
    memory[0] = 0x2F;
    memory[1] = 0x37;
    pc = (uint16_t)kernal[0x1FFC] | ((uint16_t)kernal[0x1FFD] << 8);  
    a = 0;
    x = 0; 
    y = 0;
    sp = 0xFF;
    cpustatus = FLAG_CONSTANT | FLAG_INTERRUPT;
    if (_cia2) {
        _cia2->reset();
    }
}

uint16_t cpu::getpc() {
    return (pc);
}

uint8_t cpu::getop() {
    return (opcode);
}

void cpu::stopBasic() {
    push16(pc);
    setcarry();
    setzero();
    pc = 0xA832;
}

// ============================================================================
// STACK OPERATIONS (inline mantenute)
// ============================================================================

inline void cpu::push16(uint16_t pushval) {
	mem_write(BASE_STACK + sp, (pushval >> 8) & 0xFF);
	mem_write(BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
	sp -= 2;
}

inline void cpu::push8(uint8_t pushval) {
	mem_write(BASE_STACK + sp--, pushval);
}

inline uint16_t cpu::pull16() {
	uint16_t temp16;
	temp16 = mem_read(BASE_STACK + ((sp + 1) & 0xFF)) | ((uint16_t)mem_read(BASE_STACK + ((sp + 2) & 0xFF)) << 8);
	sp += 2;
	return (temp16);
}

inline uint8_t cpu::pull8() {
	return (mem_read(BASE_STACK + ++sp));
}

// ============================================================================
// INTERRUPT HANDLERS
// ============================================================================

void cpu::nmi() {
    push16(pc);    
    push8((cpustatus & ~FLAG_BREAK) | FLAG_CONSTANT);    
    setinterrupt();    
    pc = (uint16_t)mem_read(0xFFFA) | ((uint16_t)mem_read(0xFFFB) << 8);
}

void cpu::irq() {
    if (cpustatus & FLAG_INTERRUPT) {
        return;
    }
    
    push16(pc);
    push8((cpustatus & ~FLAG_BREAK) | FLAG_CONSTANT);
    setinterrupt();
    pc = (uint16_t)mem_read(0xFFFE) | ((uint16_t)mem_read(0xFFFF) << 8);
}

// ============================================================================
// MAIN CPU EXECUTION LOOP (ULTRA OTTIMIZZATO)
// ============================================================================

int cpu::execCPU(int tickcount) {
    clockgoal6502 = tickcount;
    int realtickcount = 0;
    
    int instrCount = 0;
    const int INTERRUPT_CHECK_INTERVAL = 8;
    
    while (clockgoal6502 > 0) {
        if ((instrCount++ & (INTERRUPT_CHECK_INTERVAL - 1)) == 0) {
            // NMI Check
            if (_cia2 && _cia2->hasNMI()) {
                nmi();
            }
            
            // IRQ Check
            if (!(cpustatus & FLAG_INTERRUPT)) {
                bool irq_pending = false;
                
                if (_vic2 && _vic2->hasIRQ()) {
                    irq_pending = true;
                }
                
                if (_cia1 && !irq_pending && _cia1->hasIRQ()) {
                    uint8_t cia1_icr = _cia1->read(0x0D);
                    if (cia1_icr & 0x80) {
                        irq_pending = true;
                    }
                }
                
                if (irq_pending) {
                    // NON fare auto-ACK qui
                    irq();
                }
            }
        }
        
        opcode = mem_read(pc++);
        cpustatus |= FLAG_CONSTANT;
        (this->*opcodeTable[opcode])();
        
        // Aggiorna il timer dell'auto-ACK VIC
        //if (_vic2) {
        //    _vic2->stepCPU();
        //}
        
        int cycles = ticktable[opcode];
        clockgoal6502 -= cycles;
        realtickcount += cycles;
    }
    
    return realtickcount;
}

// ============================================================================
// ISTRUZIONI FUSE: LDA (Load Accumulator)
// ============================================================================

void cpu::op_lda_imm() {
    a = mem_read(pc++);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lda_zp() {
    uint16_t addr = mem_read(pc++);
    a = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lda_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    a = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lda_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    a = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lda_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    a = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lda_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    a = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lda_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    a = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lda_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    a = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ISTRUZIONI FUSE: LDX (Load X Register)
// ============================================================================

void cpu::op_ldx_imm() {
    x = mem_read(pc++);
    zerocalc(x);
    signcalc(x);
}

void cpu::op_ldx_zp() {
    uint16_t addr = mem_read(pc++);
    x = mem_read(addr);
    zerocalc(x);
    signcalc(x);
}

void cpu::op_ldx_zpy() {
    uint16_t addr = (mem_read(pc++) + y) & 0xFF;
    x = mem_read(addr);
    zerocalc(x);
    signcalc(x);
}

void cpu::op_ldx_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    x = mem_read(addr);
    zerocalc(x);
    signcalc(x);
}

void cpu::op_ldx_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    x = mem_read(addr);
    zerocalc(x);
    signcalc(x);
}

// ============================================================================
// ISTRUZIONI FUSE: LDY (Load Y Register)
// ============================================================================

void cpu::op_ldy_imm() {
    y = mem_read(pc++);
    zerocalc(y);
    signcalc(y);
}

void cpu::op_ldy_zp() {
    uint16_t addr = mem_read(pc++);
    y = mem_read(addr);
    zerocalc(y);
    signcalc(y);
}

void cpu::op_ldy_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    y = mem_read(addr);
    zerocalc(y);
    signcalc(y);
}

void cpu::op_ldy_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    y = mem_read(addr);
    zerocalc(y);
    signcalc(y);
}

void cpu::op_ldy_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    y = mem_read(addr);
    zerocalc(y);
    signcalc(y);
}

// ============================================================================
// ISTRUZIONI FUSE: STA (Store Accumulator)
// ============================================================================

void cpu::op_sta_zp() {
    uint16_t addr = mem_read(pc++);
    mem_write(addr, a);
}

void cpu::op_sta_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    mem_write(addr, a);
}

void cpu::op_sta_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    mem_write(addr, a);
}

void cpu::op_sta_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    mem_write(addr, a);
}

void cpu::op_sta_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    mem_write(addr, a);
}

void cpu::op_sta_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    mem_write(addr, a);
}

void cpu::op_sta_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    mem_write(addr, a);
}

// ============================================================================
// ISTRUZIONI FUSE: STX (Store X Register)
// ============================================================================

void cpu::op_stx_zp() {
    uint16_t addr = mem_read(pc++);
    mem_write(addr, x);
}

void cpu::op_stx_zpy() {
    uint16_t addr = (mem_read(pc++) + y) & 0xFF;
    mem_write(addr, x);
}

void cpu::op_stx_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    mem_write(addr, x);
}

// ============================================================================
// ISTRUZIONI FUSE: STY (Store Y Register)
// ============================================================================

void cpu::op_sty_zp() {
    uint16_t addr = mem_read(pc++);
    mem_write(addr, y);
}

void cpu::op_sty_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    mem_write(addr, y);
}

void cpu::op_sty_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    mem_write(addr, y);
}

// ============================================================================
// ISTRUZIONI FUSE: ADC (Add with Carry)
// ============================================================================

void cpu::op_adc_imm() {
    uint8_t value = mem_read(pc++);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_adc_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_adc_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_adc_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_adc_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_adc_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_adc_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_adc_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (value & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (value & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a + value + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
        a = (uint8_t)(result & 0xFF);
    }
}

// ============================================================================
// ISTRUZIONI FUSE: SBC (Subtract with Carry)
// ============================================================================

void cpu::op_sbc_imm() {
    uint8_t value = mem_read(pc++);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_sbc_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_sbc_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_sbc_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_sbc_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_sbc_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_sbc_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_sbc_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr);
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

// ============================================================================
// ISTRUZIONI FUSE: AND (Logical AND)
// ============================================================================

void cpu::op_and_imm() {
    a &= mem_read(pc++);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_and_zp() {
    uint16_t addr = mem_read(pc++);
    a &= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_and_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    a &= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_and_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    a &= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_and_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    a &= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_and_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    a &= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_and_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    a &= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_and_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    a &= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ISTRUZIONI FUSE: ORA (Logical OR)
// ============================================================================

void cpu::op_ora_imm() {
    a |= mem_read(pc++);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_ora_zp() {
    uint16_t addr = mem_read(pc++);
    a |= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_ora_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    a |= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_ora_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    a |= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_ora_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    a |= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_ora_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    a |= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_ora_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    a |= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_ora_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    a |= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ISTRUZIONI FUSE: EOR (Logical XOR)
// ============================================================================

void cpu::op_eor_imm() {
    a ^= mem_read(pc++);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_eor_zp() {
    uint16_t addr = mem_read(pc++);
    a ^= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_eor_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    a ^= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_eor_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    a ^= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_eor_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    a ^= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_eor_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    a ^= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_eor_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    a ^= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_eor_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    a ^= mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ISTRUZIONI FUSE: CMP (Compare Accumulator)
// ============================================================================

void cpu::op_cmp_imm() {
    uint8_t value = mem_read(pc++);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cmp_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cmp_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cmp_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cmp_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cmp_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cmp_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cmp_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

// ============================================================================
// ISTRUZIONI FUSE: CPX/CPY (Compare X/Y)
// ============================================================================

void cpu::op_cpx_imm() {
    uint8_t value = mem_read(pc++);
    uint16_t result = (uint16_t)x - value;
    if (x >= value) setcarry(); else clearcarry();
    if (x == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cpx_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)x - value;
    if (x >= value) setcarry(); else clearcarry();
    if (x == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cpx_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)x - value;
    if (x >= value) setcarry(); else clearcarry();
    if (x == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cpy_imm() {
    uint8_t value = mem_read(pc++);
    uint16_t result = (uint16_t)y - value;
    if (y >= value) setcarry(); else clearcarry();
    if (y == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cpy_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)y - value;
    if (y >= value) setcarry(); else clearcarry();
    if (y == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_cpy_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)y - value;
    if (y >= value) setcarry(); else clearcarry();
    if (y == value) setzero(); else clearzero();
    signcalc(result);
}

// ============================================================================
// ISTRUZIONI FUSE: BIT (Bit Test)
// ============================================================================

void cpu::op_bit_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a & value;
    zerocalc(result);
    cpustatus = (cpustatus & 0x3F) | (value & 0xC0);
}

void cpu::op_bit_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (uint16_t)a & value;
    zerocalc(result);
    cpustatus = (cpustatus & 0x3F) | (value & 0xC0);
}

// ============================================================================
// ISTRUZIONI FUSE: ASL (Arithmetic Shift Left)
// ============================================================================

void cpu::op_asl_acc() {
    uint16_t result = (uint16_t)a << 1;
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    a = (uint8_t)(result & 0xFF);
}

void cpu::op_asl_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

void cpu::op_asl_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

void cpu::op_asl_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

void cpu::op_asl_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

// ============================================================================
// ISTRUZIONI FUSE: LSR (Logical Shift Right)
// ============================================================================

void cpu::op_lsr_acc() {
    if (a & 1) setcarry(); else clearcarry();
    a >>= 1;
    zerocalc(a);
    clearsign();
}

void cpu::op_lsr_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    zerocalc(result);
    clearsign();
    mem_write(addr, result);
}

void cpu::op_lsr_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    zerocalc(result);
    clearsign();
    mem_write(addr, result);
}

void cpu::op_lsr_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    zerocalc(result);
    clearsign();
    mem_write(addr, result);
}

void cpu::op_lsr_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    zerocalc(result);
    clearsign();
    mem_write(addr, result);
}

// ============================================================================
// ISTRUZIONI FUSE: ROL (Rotate Left)
// ============================================================================

void cpu::op_rol_acc() {
    uint16_t result = ((uint16_t)a << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    a = (uint8_t)(result & 0xFF);
}

void cpu::op_rol_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

void cpu::op_rol_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

void cpu::op_rol_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

void cpu::op_rol_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result & 0xFF);
}

// ============================================================================
// ISTRUZIONI FUSE: ROR (Rotate Right)
// ============================================================================

void cpu::op_ror_acc() {
    uint8_t result = (a >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (a & 1) setcarry(); else clearcarry();
    zerocalc(result);
    signcalc(result);
    a = result;
}

void cpu::op_ror_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_ror_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_ror_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_ror_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

// ============================================================================
// ISTRUZIONI FUSE: INC/DEC (Increment/Decrement Memory)
// ============================================================================

void cpu::op_inc_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t result = mem_read(addr) + 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_inc_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t result = mem_read(addr) + 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_inc_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t result = mem_read(addr) + 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_inc_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t result = mem_read(addr) + 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_dec_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t result = mem_read(addr) - 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_dec_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t result = mem_read(addr) - 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_dec_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t result = mem_read(addr) - 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_dec_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t result = mem_read(addr) - 1;
    zerocalc(result);
    signcalc(result);
    mem_write(addr, result);
}

void cpu::op_inx() {
    x++;
    zerocalc(x);
    signcalc(x);
}

void cpu::op_iny() {
    y++;
    zerocalc(y);
    signcalc(y);
}

void cpu::op_dex() {
    x--;
    zerocalc(x);
    signcalc(x);
}

void cpu::op_dey() {
    y--;
    zerocalc(y);
    signcalc(y);
}

// ============================================================================
// ISTRUZIONI FUSE: BRANCHES (ottimizzate con calcolo cicli)
// ============================================================================

void cpu::op_bpl() {
    int8_t offset = (int8_t)mem_read(pc++);
    if ((cpustatus & FLAG_SIGN) == 0) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

void cpu::op_bmi() {
    int8_t offset = (int8_t)mem_read(pc++);
    if (cpustatus & FLAG_SIGN) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

void cpu::op_bvc() {
    int8_t offset = (int8_t)mem_read(pc++);
    if ((cpustatus & FLAG_OVERFLOW) == 0) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

void cpu::op_bvs() {
    int8_t offset = (int8_t)mem_read(pc++);
    if (cpustatus & FLAG_OVERFLOW) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

void cpu::op_bcc() {
    int8_t offset = (int8_t)mem_read(pc++);
    if ((cpustatus & FLAG_CARRY) == 0) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

void cpu::op_bcs() {
    int8_t offset = (int8_t)mem_read(pc++);
    if (cpustatus & FLAG_CARRY) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

void cpu::op_bne() {
    int8_t offset = (int8_t)mem_read(pc++);
    if ((cpustatus & FLAG_ZERO) == 0) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

void cpu::op_beq() {
    int8_t offset = (int8_t)mem_read(pc++);
    if (cpustatus & FLAG_ZERO) {
        uint16_t oldpc = pc;
        pc += offset;
        clockgoal6502 -= ((oldpc & 0xFF00) != (pc & 0xFF00)) ? 2 : 1;
    }
}

// ============================================================================
// ISTRUZIONI FUSE: JUMPS/CALLS
// ============================================================================

void cpu::op_jmp_abs() {
    pc = mem_read(pc) | (mem_read(pc + 1) << 8);
}

void cpu::op_jmp_ind() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    uint16_t addr2 = (addr & 0xFF00) | ((addr + 1) & 0x00FF);
    pc = mem_read(addr) | (mem_read(addr2) << 8);
}

void cpu::op_jsr() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    push16(pc + 1);
    pc = addr;
}

void cpu::op_rts() {
    pc = pull16() + 1;
}

void cpu::op_rti() {
    cpustatus = pull8();
    pc = pull16();
}

void cpu::op_brk() {
    pc++;
    push16(pc);
    push8(cpustatus | FLAG_BREAK);
    setinterrupt();
    pc = mem_read(0xFFFE) | (mem_read(0xFFFF) << 8);
}

// ============================================================================
// ISTRUZIONI FUSE: STACK
// ============================================================================

void cpu::op_pha() {
    push8(a);
}

void cpu::op_pla() {
    a = pull8();
    zerocalc(a);
    signcalc(a);
}

void cpu::op_php() {
    push8(cpustatus | FLAG_BREAK);
}

void cpu::op_plp() {
    cpustatus = pull8() | FLAG_CONSTANT;
}

// ============================================================================
// ISTRUZIONI FUSE: TRANSFERS
// ============================================================================

void cpu::op_tax() {
    x = a;
    zerocalc(x);
    signcalc(x);
}

void cpu::op_txa() {
    a = x;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_tay() {
    y = a;
    zerocalc(y);
    signcalc(y);
}

void cpu::op_tya() {
    a = y;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_tsx() {
    x = sp;
    zerocalc(x);
    signcalc(x);
}

void cpu::op_txs() {
    sp = x;
}

// ============================================================================
// ISTRUZIONI FUSE: FLAGS
// ============================================================================

void cpu::op_clc() {
    clearcarry();
}

void cpu::op_sec() {
    setcarry();
}

void cpu::op_cli() {
    clearinterrupt();
}

void cpu::op_sei() {
    setinterrupt();
}

void cpu::op_clv() {
    clearoverflow();
}

void cpu::op_cld() {
    cleardecimal();
}

void cpu::op_sed() {
    setdecimal();
}

void cpu::op_nop() {
    // No operation
}

// ============================================================================
// NOP variants (illegal opcodes)
// ============================================================================

void cpu::op_nop_imm() {
    pc++; // Skip immediate byte
}

void cpu::op_nop_zp() {
    pc++; // Skip zero page address
}

void cpu::op_nop_zpx() {
    pc++; // Skip zero page address
}

void cpu::op_nop_abs() {
    pc += 2; // Skip absolute address
}

void cpu::op_nop_absx() {
    pc += 2; // Skip absolute address
}

// ============================================================================
// ILLEGAL OPCODES FUSED: SLO (ASL + ORA)
// ============================================================================

void cpu::op_slo_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a |= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_slo_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a |= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_slo_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a |= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_slo_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a |= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_slo_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a |= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_slo_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a |= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_slo_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr);
    uint16_t result = value << 1;
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a |= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ILLEGAL OPCODES FUSED: RLA (ROL + AND)
// ============================================================================

void cpu::op_rla_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a &= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_rla_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a &= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_rla_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a &= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_rla_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a &= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_rla_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a &= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_rla_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a &= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_rla_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr);
    uint16_t result = (value << 1) | (cpustatus & FLAG_CARRY);
    carrycalc(result);
    mem_write(addr, result & 0xFF);
    a &= (result & 0xFF);
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ILLEGAL OPCODES FUSED: SRE (LSR + EOR)
// ============================================================================

void cpu::op_sre_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    mem_write(addr, result);
    a ^= result;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_sre_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    mem_write(addr, result);
    a ^= result;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_sre_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    mem_write(addr, result);
    a ^= result;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_sre_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    mem_write(addr, result);
    a ^= result;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_sre_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    mem_write(addr, result);
    a ^= result;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_sre_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    mem_write(addr, result);
    a ^= result;
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ILLEGAL OPCODES FUSED: RRA (ROR + ADC)
// ============================================================================

void cpu::op_rra_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    mem_write(addr, result);
    
    // ADC part
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (result & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (result & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, result);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t res = (uint16_t)a + result + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(res);
        zerocalc(res);
        overflowcalc(res, a, result);
        signcalc(res);
        a = (uint8_t)(res & 0xFF);
    }
}

void cpu::op_rra_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    mem_write(addr, result);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (result & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (result & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, result);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t res = (uint16_t)a + result + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(res);
        zerocalc(res);
        overflowcalc(res, a, result);
        signcalc(res);
        a = (uint8_t)(res & 0xFF);
    }
}

void cpu::op_rra_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    mem_write(addr, result);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (result & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (result & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, result);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t res = (uint16_t)a + result + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(res);
        zerocalc(res);
        overflowcalc(res, a, result);
        signcalc(res);
        a = (uint8_t)(res & 0xFF);
    }
}

void cpu::op_rra_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    mem_write(addr, result);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (result & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (result & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, result);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t res = (uint16_t)a + result + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(res);
        zerocalc(res);
        overflowcalc(res, a, result);
        signcalc(res);
        a = (uint8_t)(res & 0xFF);
    }
}

void cpu::op_rra_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    mem_write(addr, result);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (result & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (result & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, result);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t res = (uint16_t)a + result + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(res);
        zerocalc(res);
        overflowcalc(res, a, result);
        signcalc(res);
        a = (uint8_t)(res & 0xFF);
    }
}

void cpu::op_rra_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    mem_write(addr, result);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (result & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (result & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, result);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t res = (uint16_t)a + result + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(res);
        zerocalc(res);
        overflowcalc(res, a, result);
        signcalc(res);
        a = (uint8_t)(res & 0xFF);
    }
}

void cpu::op_rra_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr);
    uint8_t result = (value >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    if (value & 1) setcarry(); else clearcarry();
    mem_write(addr, result);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) + (result & 0x0F) + (cpustatus & FLAG_CARRY);
        if (tmp > 9) tmp += 6;
        tmp = (tmp & 0x0F) + (a & 0xF0) + (result & 0xF0);
        if (tmp > 0x9F) tmp += 0x60;
        carrycalc(tmp);
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, result);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t res = (uint16_t)a + result + (uint16_t)(cpustatus & FLAG_CARRY);
        carrycalc(res);
        zerocalc(res);
        overflowcalc(res, a, result);
        signcalc(res);
        a = (uint8_t)(res & 0xFF);
    }
}

// ============================================================================
// ILLEGAL OPCODES FUSED: LAX (LDA + LDX)
// ============================================================================

void cpu::op_lax_zp() {
    uint16_t addr = mem_read(pc++);
    a = x = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lax_zpy() {
    uint16_t addr = (mem_read(pc++) + y) & 0xFF;
    a = x = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lax_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    a = x = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lax_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    a = x = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lax_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    a = x = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lax_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    a = x = mem_read(addr);
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ILLEGAL OPCODES FUSED: SAX (Store A AND X)
// ============================================================================

void cpu::op_sax_zp() {
    uint16_t addr = mem_read(pc++);
    mem_write(addr, a & x);
}

void cpu::op_sax_zpy() {
    uint16_t addr = (mem_read(pc++) + y) & 0xFF;
    mem_write(addr, a & x);
}

void cpu::op_sax_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    mem_write(addr, a & x);
}

void cpu::op_sax_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    mem_write(addr, a & x);
}

// ============================================================================
// ILLEGAL OPCODES FUSED: DCP (DEC + CMP)
// ============================================================================

void cpu::op_dcp_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr) - 1;
    mem_write(addr, value);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_dcp_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr) - 1;
    mem_write(addr, value);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_dcp_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr) - 1;
    mem_write(addr, value);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_dcp_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr) - 1;
    mem_write(addr, value);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_dcp_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr) - 1;
    mem_write(addr, value);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_dcp_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr) - 1;
    mem_write(addr, value);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

void cpu::op_dcp_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr) - 1;
    mem_write(addr, value);
    uint16_t result = (uint16_t)a - value;
    if (a >= value) setcarry(); else clearcarry();
    if (a == value) setzero(); else clearzero();
    signcalc(result);
}

// ============================================================================
// ILLEGAL OPCODES FUSED: ISC (INC + SBC)
// ============================================================================

void cpu::op_isc_zp() {
    uint16_t addr = mem_read(pc++);
    uint8_t value = mem_read(addr) + 1;
    mem_write(addr, value);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_isc_zpx() {
    uint16_t addr = (mem_read(pc++) + x) & 0xFF;
    uint8_t value = mem_read(addr) + 1;
    mem_write(addr, value);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_isc_abs() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint8_t value = mem_read(addr) + 1;
    mem_write(addr, value);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_isc_absx() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + x;
    pc += 2;
    uint8_t value = mem_read(addr) + 1;
    mem_write(addr, value);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_isc_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr) + 1;
    mem_write(addr, value);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

void cpu::op_isc_indx() {
    uint16_t zp = (mem_read(pc++) + x) & 0xFF;
    uint16_t addr = mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8);
    uint8_t value = mem_read(addr) + 1;
    mem_write(addr, value);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}


// ============================================================================
// ILLEGAL OPCODES FUSED: SRE (LSR + EOR)
// ============================================================================

void cpu::op_sre_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr);
    if (value & 1) setcarry(); else clearcarry();
    uint8_t result = value >> 1;
    mem_write(addr, result);
    a ^= result;
    zerocalc(a);
    signcalc(a);
}

// ============================================================================
// ILLEGAL OPCODES FUSED: ISC (INC + SBC)
// ============================================================================


void cpu::op_isc_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t value = mem_read(addr) + 1;
    mem_write(addr, value);
    
    if (cpustatus & FLAG_DECIMAL) {
        uint16_t tmp = (a & 0x0F) - (value & 0x0F) - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if ((int16_t)tmp < 0) tmp = ((tmp - 6) & 0x0F) | ((a & 0xF0) - (value & 0xF0) - 0x10);
        else tmp = (tmp & 0x0F) | ((a & 0xF0) - (value & 0xF0));
        if ((int16_t)tmp < 0) tmp -= 0x60;
        carrycalc(0x100 + (int16_t)a - (int16_t)value - ((cpustatus & FLAG_CARRY) ? 0 : 1));
        zerocalc(tmp);
        signcalc(tmp);
        overflowcalc(tmp, a, value);
        a = (uint8_t)(tmp & 0xFF);
    } else {
        uint16_t result = (uint16_t)a - value - ((cpustatus & FLAG_CARRY) ? 0 : 1);
        if (result < 0x100) setcarry(); else clearcarry();
        zerocalc(result);
        signcalc(result);
        overflowcalc(result, a, value);
        a = (uint8_t)(result & 0xFF);
    }
}

// ============================================================================
// ILLEGAL OPCODES: Altri opcodes speciali
// ============================================================================

void cpu::op_anc_imm() {
    a &= mem_read(pc++);
    zerocalc(a);
    signcalc(a);
    if (cpustatus & FLAG_SIGN) setcarry(); else clearcarry();
}

void cpu::op_arr_imm() {
    uint8_t value = mem_read(pc++);
    a &= value;
    a = (a >> 1) | ((cpustatus & FLAG_CARRY) << 7);
    zerocalc(a);
    signcalc(a);
    if ((a & 0x40) ^ ((a & 0x20) << 1)) setoverflow(); else clearoverflow();
    if (a & 0x40) setcarry(); else clearcarry();
}

void cpu::op_alr_imm() {
    uint8_t value = mem_read(pc++);
    a &= value;
    if (a & 1) setcarry(); else clearcarry();
    a >>= 1;
    zerocalc(a);
    clearsign();
}

void cpu::op_axs_imm() {
    uint8_t value = mem_read(pc++);
    uint16_t result = (a & x) - value;
    x = (uint8_t)result;
    if (result < 0x100) setcarry(); else clearcarry();
    zerocalc(x);
    signcalc(x);
}

void cpu::op_shx_absy() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint16_t hi = ((addr + y) & 0xFF00);
    uint8_t val = x & ((addr >> 8) + 1);
    mem_write((addr & 0x00FF) | hi, val);
}

void cpu::op_shy_absx() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    uint16_t hi = ((addr + x) & 0xFF00);
    uint8_t val = y & ((addr >> 8) + 1);
    mem_write((addr & 0x00FF) | hi, val);
}

void cpu::op_las_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t value = mem_read(addr);
    a = x = sp = (sp & value);
    zerocalc(a);
    signcalc(a);
}

void cpu::op_tas_absy() {
    uint16_t addr = mem_read(pc) | (mem_read(pc + 1) << 8);
    pc += 2;
    sp = a & x;
    uint16_t hi = ((addr + y) & 0xFF00);
    uint8_t val = sp & ((addr >> 8) + 1);
    mem_write((addr & 0x00FF) | hi, val);
}

void cpu::op_ahx_absy() {
    uint16_t addr = (mem_read(pc) | (mem_read(pc + 1) << 8)) + y;
    pc += 2;
    uint8_t val = a & x & ((addr >> 8) + 1);
    mem_write(addr, val);
}

void cpu::op_ahx_indy() {
    uint16_t zp = mem_read(pc++);
    uint16_t addr = (mem_read(zp) | (mem_read((zp + 1) & 0xFF) << 8)) + y;
    uint8_t val = a & x & ((addr >> 8) + 1);
    mem_write(addr, val);
}

void cpu::op_xaa_imm() {
    uint8_t value = mem_read(pc++);
    x = a;
    a &= value;
    zerocalc(a);
    signcalc(a);
}

void cpu::op_lxa_imm() {
    uint8_t value = mem_read(pc++);
    a = x = value;
    zerocalc(a);
    signcalc(a);
}