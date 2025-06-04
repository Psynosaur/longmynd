/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: stv0910.h                                                                   */
/* Copyright 2019 Heather Lomond                                                                      */
/* -------------------------------------------------------------------------------------------------- */
/*
    This file is part of longmynd.

    Longmynd is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Longmynd is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with longmynd.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef STV0910_H
#define STV0910_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define DEMOD_HUNTING 0
#define DEMOD_FOUND_HEADER 1
#define DEMOD_S2 2
#define DEMOD_S 3

#define STV0910_PLL_LOCK_TIMEOUT 100 

#define STV0910_SCAN_BLIND_BEST_GUESS 0x15

#define STV0910_DEMOD_TOP 1
#define STV0910_DEMOD_BOTTOM 2

#define STV0910_PUNCTURE_1_2 0x0d
#define STV0910_PUNCTURE_2_3 0x12
#define STV0910_PUNCTURE_3_4 0x15
#define STV0910_PUNCTURE_5_6 0x18
#define STV0910_PUNCTURE_6_7 0x19
#define STV0910_PUNCTURE_7_8 0x1a

/* DVB-S2 MODCOD definitions (from dddvb) */
typedef enum {
    FE_DUMMY_PLF = 0,
    FE_QPSK_14,
    FE_QPSK_13,
    FE_QPSK_25,
    FE_QPSK_12,
    FE_QPSK_35,
    FE_QPSK_23,
    FE_QPSK_34,
    FE_QPSK_45,
    FE_QPSK_56,
    FE_QPSK_89,
    FE_QPSK_910,
    FE_8PSK_35,
    FE_8PSK_23,
    FE_8PSK_34,
    FE_8PSK_56,
    FE_8PSK_89,
    FE_8PSK_910,
    FE_16APSK_23,
    FE_16APSK_34,
    FE_16APSK_45,
    FE_16APSK_56,
    FE_16APSK_89,
    FE_16APSK_910,
    FE_32APSK_34,
    FE_32APSK_45,
    FE_32APSK_56,
    FE_32APSK_89,
    FE_32APSK_910
} fe_stv0910_modcod_t;

uint8_t stv0910_read_car_freq(uint8_t, int32_t*);
uint8_t stv0910_read_constellation(uint8_t, int8_t*, int8_t*);
uint8_t stv0910_read_sr(uint8_t demod, uint32_t*);
uint8_t stv0910_read_puncture_rate(uint8_t, uint8_t*);
uint8_t stv0910_read_agc1_gain(uint8_t, uint16_t*);
uint8_t stv0910_read_agc2_gain(uint8_t, uint16_t*);
uint8_t stv0910_read_power(uint8_t, uint8_t*, uint8_t*);
uint8_t stv0910_read_err_rate(uint8_t, uint32_t*);
uint8_t stv0910_read_ber(uint8_t, uint32_t*);
uint8_t stv0910_read_errors_bch_uncorrected(uint8_t demod, bool *errors_bch_uncorrected);
uint8_t stv0910_read_errors_bch_count(uint8_t demod, uint32_t *errors_bch_count);
uint8_t stv0910_read_errors_ldpc_count(uint8_t demod, uint32_t *errors_ldpc_count);
uint8_t stv0910_read_mer(uint8_t, int32_t*);
uint8_t stv0910_read_modcod_and_type(uint8_t, uint32_t*, bool*, bool*,uint8_t *);
uint8_t stv0910_read_matype(uint8_t demod, uint32_t *matype1,uint32_t *matype2) ;
uint8_t stv0910_init(uint32_t, uint32_t, float, float);
uint8_t stv0910_init_regs(void);
uint8_t stv0910_quick_init_regs(void);
uint8_t stv0910_setup_timing_loop(uint8_t, uint32_t);
uint8_t stv0910_setup_carrier_loop(uint8_t, uint32_t);
uint8_t stv0910_read_scan_state(uint8_t, uint8_t *);
uint8_t stv0910_start_scan(uint8_t);
uint8_t stv0910_setup_search_params(uint8_t);
uint8_t stv0910_setup_clocks();

/* New dynamic clock management functions */
uint8_t stv0910_set_mclock_dynamic(uint32_t master_clock);
uint32_t stv0910_get_current_mclock(void);

/* New optimized carrier loop functions */
uint8_t stv0910_setup_carrier_loop_optimized(uint8_t demod, uint32_t symbol_rate, fe_stv0910_modcod_t modcod, uint8_t pilots);
uint8_t stv0910_get_optim_cloop(fe_stv0910_modcod_t modcod, uint32_t symbol_rate, uint8_t pilots);

/* New mutex-protected register access functions (dddvb style) */
uint8_t stv0910_write_shared_reg(uint16_t reg, uint8_t mask, uint8_t val);
uint8_t stv0910_read_shared_reg(uint16_t reg, uint8_t *val);
uint8_t stv0910_write_shared_reg_field(uint32_t field, uint8_t field_val);
uint8_t stv0910_read_shared_reg_field(uint32_t field, uint8_t *field_val);

/* Mutex management functions */
void stv0910_mutex_init(void);
void stv0910_mutex_destroy(void);

#endif

