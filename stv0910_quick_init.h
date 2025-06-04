/*
    longmynd - STV0910 Quick Initialization
    
    Copyright 2019-2025 Heather Lomond
    
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

#ifndef STV0910_QUICK_INIT_H
#define STV0910_QUICK_INIT_H

#include "stv0910_regs.h"

typedef struct{
    uint16_t reg;
    uint8_t  val;
}STRegQuick;

/* Essential registers for quick initialization - based on dddvb approach */
#define STV0910_QUICK_REGS 45

static STRegQuick STV0910QuickRegs[STV0910_QUICK_REGS] = {
    /* Core system registers - essential for basic operation */
    { RSTV0910_PADCFG,            0x05 }, /* AGCRF_1 inverted, Push-pull, AGCRF_2 same*/
    { RSTV0910_SYNTCTRL,          0x02 }, /* Synthesizer control */
    { RSTV0910_TSGENERAL,         0x00 }, /* TS general config */
    { RSTV0910_CFGEXT,            0x02 }, /* Configuration extension */
    { RSTV0910_GENCFG,            0x15 }, /* General configuration - dual demod enabled */
    
    /* I2C repeater configuration */
    { RSTV0910_P1_I2CRPT,         0x24 }, /* I2C repeater P1 off */
    { RSTV0910_P2_I2CRPT,         0x24 }, /* I2C repeater P2 off */
    { RSTV0910_I2CCFG,            0x88 }, /* I2C oversampling ratio */
    
    /* Output configuration */
    { RSTV0910_OUTCFG,            0x00 }, /* Output configuration */
    
    /* P1 Essential registers */
    { RSTV0910_P1_TNRCFG2,        0x02 }, /* IQSWAP = 0 */
    { RSTV0910_P1_CAR3CFG,        0x02 }, /* Carrier 3 config */
    { RSTV0910_P1_DMDCFG4,        0x04 }, /* Demod config 4 */
    { RSTV0910_P1_TSPIDFLT1,      0x00 }, /* TS PID filter */
    { RSTV0910_P1_TMGCFG2,        0x80 }, /* Timing config 2 */
    
    /* P1 TS configuration */
    { RSTV0910_P1_TSCFGH,         0x20 }, /* TS config high */
    { RSTV0910_P1_TSCFGM,         0xC0 }, /* TS config medium - manual speed */
    { RSTV0910_P1_TSCFGL,         0x60 }, /* TS config low */
    { RSTV0910_P1_TSSPEED,        0x28 }, /* TS speed */
    { RSTV0910_P1_TSINSDELM,      0x17 }, /* TS insertion/deletion */
    { RSTV0910_P1_TSINSDELL,      0xff }, /* TS insertion/deletion low */
    
    /* P2 Essential registers */
    { RSTV0910_P2_TNRCFG2,        0x82 }, /* IQSWAP = 1 */
    { RSTV0910_P2_CAR3CFG,        0x02 }, /* Carrier 3 config */
    { RSTV0910_P2_DMDCFG4,        0x04 }, /* Demod config 4 */
    { RSTV0910_P2_TSPIDFLT1,      0x00 }, /* TS PID filter */
    { RSTV0910_P2_TMGCFG2,        0x80 }, /* Timing config 2 */
    
    /* P2 TS configuration */
    { RSTV0910_P2_TSCFGH,         0x20 }, /* TS config high */
    { RSTV0910_P2_TSCFGM,         0xC0 }, /* TS config medium - manual speed */
    { RSTV0910_P2_TSCFGL,         0x60 }, /* TS config low */
    { RSTV0910_P2_TSSPEED,        0x28 }, /* TS speed */
    { RSTV0910_P2_TSINSDELM,      0x17 }, /* TS insertion/deletion */
    { RSTV0910_P2_TSINSDELL,      0xff }, /* TS insertion/deletion low */
    
    /* Clock configuration - essential for proper operation */
    { RSTV0910_NCOARSE,           0x13 }, /* Clock config - will be overwritten by set_mclock */
    { RSTV0910_NCOARSE2,          0x04 }, /* Clock config 2 */
    { RSTV0910_NCOARSE1,          0x2D }, /* Clock config 1 - will be overwritten by set_mclock */
    
    /* AGC configuration - minimal essential settings */
    { RSTV0910_P1_AGC2REF,        0x38 }, /* AGC2 reference */
    { RSTV0910_P2_AGC2REF,        0x38 }, /* AGC2 reference */
    
    /* Carrier loop essential settings */
    { RSTV0910_P1_CARFREQ,        0x79 }, /* Carrier frequency config */
    { RSTV0910_P2_CARFREQ,        0x79 }, /* Carrier frequency config */
    { RSTV0910_P1_CARHDR,         0x1c }, /* Carrier header config */
    { RSTV0910_P2_CARHDR,         0x1c }, /* Carrier header config */
    
    /* Timing loop essential settings */
    { RSTV0910_P1_TMGCFG,         0xd2 }, /* Timing config */
    { RSTV0910_P2_TMGCFG,         0xd2 }, /* Timing config */
    
    /* Error correction essential settings */
    { RSTV0910_P1_PRVIT,          0x2F }, /* Puncture rate Viterbi */
    { RSTV0910_P2_PRVIT,          0x2F }, /* Puncture rate Viterbi */
    
    /* Final register - used as terminator */
    { RSTV0910_TSTTSRS,           0x00 }, /* Test TS RS - terminator */
};

/* Function declarations */
uint8_t stv0910_quick_init_regs(void);

#endif
