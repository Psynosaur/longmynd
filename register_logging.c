/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: register_logging.c                                                          */
/* Copyright 2024 Ohan Smit                                                                           */
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

#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include "register_logging.h"
#include "stv6120_regs.h"
#include "stv0910_regs.h"

/* -------------------------------------------------------------------------------------------------- */
/* GLOBAL VARIABLES                                                                                   */
/* -------------------------------------------------------------------------------------------------- */

bool register_logging_enabled = true;  /* Runtime enable/disable flag */
static register_context_t current_context = REG_CONTEXT_UNKNOWN;

/* Rate limiting for demod sequence logging */
static uint64_t last_demod_sequence_log_time = 0;
static bool demod_sequence_suppressed = false;

/* -------------------------------------------------------------------------------------------------- */
/* STV6120 REGISTER NAME LOOKUP TABLE                                                                */
/* -------------------------------------------------------------------------------------------------- */

static const stv6120_register_info_t stv6120_register_table[] = {
    { STV6120_CTRL1,  "STV6120_CTRL1",  "K divider, RDIV, output shape, MCLK divider" },
    { STV6120_CTRL2,  "STV6120_CTRL2",  "DC loop, shutdown, synthesizer, reference, baseband gain" },
    { STV6120_CTRL3,  "STV6120_CTRL3",  "N divider LSB (tuner 1)" },
    { STV6120_CTRL4,  "STV6120_CTRL4",  "F divider bits 6-0, N divider MSB (tuner 1)" },
    { STV6120_CTRL5,  "STV6120_CTRL5",  "F divider bits 14-7 (tuner 1)" },
    { STV6120_CTRL6,  "STV6120_CTRL6",  "ICP current, F divider bits 17-15 (tuner 1)" },
    { STV6120_CTRL7,  "STV6120_CTRL7",  "RC clock, P divider, CF filter (tuner 1)" },
    { STV6120_CTRL8,  "STV6120_CTRL8",  "TCAL, calibration time, CFHF filter (tuner 1)" },
    { STV6120_STAT1,  "STV6120_STAT1",  "Status register (tuner 1)" },
    { STV6120_CTRL9,  "STV6120_CTRL9",  "Status register (tuner 1)" },
    { STV6120_CTRL10, "STV6120_CTRL10", "Path control, LNA control" },
    { STV6120_CTRL11, "STV6120_CTRL11", "N divider LSB (tuner 2)" },
    { STV6120_CTRL12, "STV6120_CTRL12", "F divider bits 6-0, N divider MSB (tuner 2)" },
    { STV6120_CTRL13, "STV6120_CTRL13", "F divider bits 14-7 (tuner 2)" },
    { STV6120_CTRL14, "STV6120_CTRL14", "ICP current, F divider bits 17-15 (tuner 2)" },
    { STV6120_CTRL15, "STV6120_CTRL15", "RC clock, P divider, CF filter (tuner 2)" },
    { STV6120_CTRL16, "STV6120_CTRL16", "TCAL, calibration time, CFHF filter (tuner 2)" },
    { STV6120_CTRL17, "STV6120_CTRL17", "Status register (tuner 2)" },
    { STV6120_STAT2,  "STV6120_STAT2",  "Status register 2" },
    { STV6120_CTRL18, "STV6120_CTRL18", "Test register" },
    { STV6120_CTRL19, "STV6120_CTRL19", "Test register" },
    { STV6120_CTRL20, "STV6120_CTRL20", "VCO 1 amplifier control" },
    { STV6120_CTRL21, "STV6120_CTRL21", "Test register" },
    { STV6120_CTRL22, "STV6120_CTRL22", "Test register" },
    { STV6120_CTRL23, "STV6120_CTRL23", "VCO 2 amplifier control" },
    { 0xFF, NULL, NULL }  /* End marker */
};

/* -------------------------------------------------------------------------------------------------- */
/* STV0910 REGISTER NAME LOOKUP TABLE (PARTIAL - KEY REGISTERS)                                     */
/* -------------------------------------------------------------------------------------------------- */

static const stv0910_register_info_t stv0910_register_table[] = {
    /* System registers */
    { RSTV0910_MID,           "RSTV0910_MID",           "Chip identification" },
    { RSTV0910_DID,           "RSTV0910_DID",           "Device identification" },
    { RSTV0910_DACR1,         "RSTV0910_DACR1",         "DAC register 1" },
    { RSTV0910_DACR2,         "RSTV0910_DACR2",         "DAC register 2" },
    { RSTV0910_PADCFG,        "RSTV0910_PADCFG",        "PAD configuration" },
    { RSTV0910_OUTCFG2,       "RSTV0910_OUTCFG2",       "Output configuration 2" },
    { RSTV0910_OUTCFG,        "RSTV0910_OUTCFG",        "Output configuration" },

    /* IRQ Status and Mask registers */
    { RSTV0910_IRQSTATUS3,    "RSTV0910_IRQSTATUS3",    "IRQ status register 3" },
    { RSTV0910_IRQSTATUS2,    "RSTV0910_IRQSTATUS2",    "IRQ status register 2" },
    { RSTV0910_IRQSTATUS1,    "RSTV0910_IRQSTATUS1",    "IRQ status register 1" },
    { RSTV0910_IRQSTATUS0,    "RSTV0910_IRQSTATUS0",    "IRQ status register 0" },
    { RSTV0910_IRQMASK3,      "RSTV0910_IRQMASK3",      "IRQ mask register 3" },
    { RSTV0910_IRQMASK2,      "RSTV0910_IRQMASK2",      "IRQ mask register 2" },
    { RSTV0910_IRQMASK1,      "RSTV0910_IRQMASK1",      "IRQ mask register 1" },
    { RSTV0910_IRQMASK0,      "RSTV0910_IRQMASK0",      "IRQ mask register 0" },

    /* I2C and control registers */
    { RSTV0910_I2CCFG,        "RSTV0910_I2CCFG",        "I2C configuration" },
    { RSTV0910_P1_I2CRPT,     "RSTV0910_P1_I2CRPT",     "P1 I2C repeater control" },
    { RSTV0910_P2_I2CRPT,     "RSTV0910_P2_I2CRPT",     "P2 I2C repeater control" },

    /* GPIO configuration registers */
    { RSTV0910_GPIO0CFG,      "RSTV0910_GPIO0CFG",      "GPIO 0 configuration" },
    { RSTV0910_GPIO1CFG,      "RSTV0910_GPIO1CFG",      "GPIO 1 configuration" },
    { RSTV0910_GPIO2CFG,      "RSTV0910_GPIO2CFG",      "GPIO 2 configuration" },
    { RSTV0910_GPIO3CFG,      "RSTV0910_GPIO3CFG",      "GPIO 3 configuration" },
    { RSTV0910_GPIO4CFG,      "RSTV0910_GPIO4CFG",      "GPIO 4 configuration" },
    { RSTV0910_GPIO5CFG,      "RSTV0910_GPIO5CFG",      "GPIO 5 configuration" },
    { RSTV0910_GPIO6CFG,      "RSTV0910_GPIO6CFG",      "GPIO 6 configuration" },
    { RSTV0910_GPIO7CFG,      "RSTV0910_GPIO7CFG",      "GPIO 7 configuration" },
    { RSTV0910_GPIO8CFG,      "RSTV0910_GPIO8CFG",      "GPIO 8 configuration" },
    { RSTV0910_GPIO9CFG,      "RSTV0910_GPIO9CFG",      "GPIO 9 configuration" },
    { RSTV0910_GPIO10CFG,     "RSTV0910_GPIO10CFG",     "GPIO 10 configuration" },
    { RSTV0910_GPIO11CFG,     "RSTV0910_GPIO11CFG",     "GPIO 11 configuration" },
    { RSTV0910_GPIO12CFG,     "RSTV0910_GPIO12CFG",     "GPIO 12 configuration" },
    { RSTV0910_GPIO13CFG,     "RSTV0910_GPIO13CFG",     "GPIO 13 configuration" },
    { RSTV0910_GPIO14CFG,     "RSTV0910_GPIO14CFG",     "GPIO 14 configuration" },
    { RSTV0910_GPIO15CFG,     "RSTV0910_GPIO15CFG",     "GPIO 15 configuration" },
    { RSTV0910_GPIO16CFG,     "RSTV0910_GPIO16CFG",     "GPIO 16 configuration" },
    { RSTV0910_GPIO17CFG,     "RSTV0910_GPIO17CFG",     "GPIO 17 configuration" },
    { RSTV0910_GPIO18CFG,     "RSTV0910_GPIO18CFG",     "GPIO 18 configuration" },
    { RSTV0910_GPIO19CFG,     "RSTV0910_GPIO19CFG",     "GPIO 19 configuration" },
    { RSTV0910_GPIO20CFG,     "RSTV0910_GPIO20CFG",     "GPIO 20 configuration" },
    { RSTV0910_GPIO21CFG,     "RSTV0910_GPIO21CFG",     "GPIO 21 configuration" },
    { RSTV0910_GPIO22CFG,     "RSTV0910_GPIO22CFG",     "GPIO 22 configuration" },

    /* P2 (TOP) Demodulator registers */
    { RSTV0910_P2_DMDISTATE,  "RSTV0910_P2_DMDISTATE",  "P2 demodulator state control" },
    { RSTV0910_P2_SFRINIT1,   "RSTV0910_P2_SFRINIT1",   "P2 symbol rate init MSB" },
    { RSTV0910_P2_SFRINIT0,   "RSTV0910_P2_SFRINIT0",   "P2 symbol rate init LSB" },

    /* P2 Carrier frequency registers */
    { RSTV0910_P2_LDT2,       "RSTV0910_P2_LDT2",       "P2 carrier lock threshold 2" },
    { RSTV0910_P2_CFRICFG,    "RSTV0910_P2_CFRICFG",    "P2 carrier frequency init config" },
    { RSTV0910_P2_CFRUP1,     "RSTV0910_P2_CFRUP1",     "P2 carrier frequency upper limit MSB" },
    { RSTV0910_P2_CFRUP0,     "RSTV0910_P2_CFRUP0",     "P2 carrier frequency upper limit LSB" },
    { RSTV0910_P2_CFRIBASE1,  "RSTV0910_P2_CFRIBASE1",  "P2 carrier frequency init base MSB" },
    { RSTV0910_P2_CFRIBASE0,  "RSTV0910_P2_CFRIBASE0",  "P2 carrier frequency init base LSB" },
    { RSTV0910_P2_CFRLOW1,    "RSTV0910_P2_CFRLOW1",    "P2 carrier frequency lower limit MSB" },
    { RSTV0910_P2_CFRLOW0,    "RSTV0910_P2_CFRLOW0",    "P2 carrier frequency lower limit LSB" },
    { RSTV0910_P2_CFRINIT1,   "RSTV0910_P2_CFRINIT1",   "P2 carrier frequency init MSB" },
    { RSTV0910_P2_CFRINIT0,   "RSTV0910_P2_CFRINIT0",   "P2 carrier frequency init LSB" },
    { RSTV0910_P2_CFRINC1,    "RSTV0910_P2_CFRINC1",    "P2 carrier frequency increment MSB" },
    { RSTV0910_P2_CFRINC0,    "RSTV0910_P2_CFRINC0",    "P2 carrier frequency increment LSB" },
    { RSTV0910_P2_CFR2,       "RSTV0910_P2_CFR2",       "P2 carrier frequency 2" },
    { RSTV0910_P2_CFR1,       "RSTV0910_P2_CFR1",       "P2 carrier frequency 1" },
    { RSTV0910_P2_CFR0,       "RSTV0910_P2_CFR0",       "P2 carrier frequency 0" },

    /* P2 Transport stream registers */
    { RSTV0910_P2_TSCFGH,     "RSTV0910_P2_TSCFGH",     "P2 transport stream config high" },
    { RSTV0910_P2_TSCFGM,     "RSTV0910_P2_TSCFGM",     "P2 transport stream config medium" },
    { RSTV0910_P2_TSCFGL,     "RSTV0910_P2_TSCFGL",     "P2 transport stream config low" },

    /* P1 (BOTTOM) Demodulator registers */
    { RSTV0910_P1_DMDISTATE,  "RSTV0910_P1_DMDISTATE",  "P1 demodulator state control" },
    { RSTV0910_P1_SFRINIT1,   "RSTV0910_P1_SFRINIT1",   "P1 symbol rate init MSB" },
    { RSTV0910_P1_SFRINIT0,   "RSTV0910_P1_SFRINIT0",   "P1 symbol rate init LSB" },

    /* P1 Carrier frequency registers */
    { RSTV0910_P1_LDT2,       "RSTV0910_P1_LDT2",       "P1 carrier lock threshold 2" },
    { RSTV0910_P1_CFRICFG,    "RSTV0910_P1_CFRICFG",    "P1 carrier frequency init config" },
    { RSTV0910_P1_CFRUP1,     "RSTV0910_P1_CFRUP1",     "P1 carrier frequency upper limit MSB" },
    { RSTV0910_P1_CFRUP0,     "RSTV0910_P1_CFRUP0",     "P1 carrier frequency upper limit LSB" },
    { RSTV0910_P1_CFRIBASE1,  "RSTV0910_P1_CFRIBASE1",  "P1 carrier frequency init base MSB" },
    { RSTV0910_P1_CFRIBASE0,  "RSTV0910_P1_CFRIBASE0",  "P1 carrier frequency init base LSB" },
    { RSTV0910_P1_CFRLOW1,    "RSTV0910_P1_CFRLOW1",    "P1 carrier frequency lower limit MSB" },
    { RSTV0910_P1_CFRLOW0,    "RSTV0910_P1_CFRLOW0",    "P1 carrier frequency lower limit LSB" },
    { RSTV0910_P1_CFRINIT1,   "RSTV0910_P1_CFRINIT1",   "P1 carrier frequency init MSB" },
    { RSTV0910_P1_CFRINIT0,   "RSTV0910_P1_CFRINIT0",   "P1 carrier frequency init LSB" },
    { RSTV0910_P1_CFRINC1,    "RSTV0910_P1_CFRINC1",    "P1 carrier frequency increment MSB" },
    { RSTV0910_P1_CFRINC0,    "RSTV0910_P1_CFRINC0",    "P1 carrier frequency increment LSB" },
    { RSTV0910_P1_CFR2,       "RSTV0910_P1_CFR2",       "P1 carrier frequency 2" },
    { RSTV0910_P1_CFR1,       "RSTV0910_P1_CFR1",       "P1 carrier frequency 1" },
    { RSTV0910_P1_CFR0,       "RSTV0910_P1_CFR0",       "P1 carrier frequency 0" },

    /* P1 Transport stream registers */
    { RSTV0910_P1_TSCFGH,     "RSTV0910_P1_TSCFGH",     "P1 transport stream config high" },
    { RSTV0910_P1_TSCFGM,     "RSTV0910_P1_TSCFGM",     "P1 transport stream config medium" },
    { RSTV0910_P1_TSCFGL,     "RSTV0910_P1_TSCFGL",     "P1 transport stream config low" },

    /* Status registers */
    { RSTV0910_STRSTATUS1,    "RSTV0910_STRSTATUS1",    "Stream status register 1" },
    { RSTV0910_STRSTATUS2,    "RSTV0910_STRSTATUS2",    "Stream status register 2" },
    { RSTV0910_STRSTATUS3,    "RSTV0910_STRSTATUS3",    "Stream status register 3" },

    /* FSK registers */
    { RSTV0910_FSKTFC2,       "RSTV0910_FSKTFC2",       "FSK transmit frequency control 2" },
    { RSTV0910_FSKTFC1,       "RSTV0910_FSKTFC1",       "FSK transmit frequency control 1" },
    { RSTV0910_FSKTFC0,       "RSTV0910_FSKTFC0",       "FSK transmit frequency control 0" },
    { RSTV0910_FSKTDELTAF1,   "RSTV0910_FSKTDELTAF1",   "FSK transmit delta frequency 1" },
    { RSTV0910_FSKTDELTAF0,   "RSTV0910_FSKTDELTAF0",   "FSK transmit delta frequency 0" },
    { RSTV0910_FSKTCTRL,      "RSTV0910_FSKTCTRL",      "FSK transmit control" },
    { RSTV0910_FSKRFC2,       "RSTV0910_FSKRFC2",       "FSK receive frequency control 2" },
    { RSTV0910_FSKRFC1,       "RSTV0910_FSKRFC1",       "FSK receive frequency control 1" },
    { RSTV0910_FSKRFC0,       "RSTV0910_FSKRFC0",       "FSK receive frequency control 0" },
    { RSTV0910_FSKRK1,        "RSTV0910_FSKRK1",        "FSK receive K1 parameter" },
    { RSTV0910_FSKRK2,        "RSTV0910_FSKRK2",        "FSK receive K2 parameter" },
    { RSTV0910_FSKRAGCR,      "RSTV0910_FSKRAGCR",      "FSK receive AGC reference" },
    { RSTV0910_FSKRALPHA,     "RSTV0910_FSKRALPHA",     "FSK receive alpha parameter" },
    { RSTV0910_FSKRPLTH1,     "RSTV0910_FSKRPLTH1",     "FSK receive PLL threshold 1" },
    { RSTV0910_FSKRPLTH0,     "RSTV0910_FSKRPLTH0",     "FSK receive PLL threshold 0" },
    { RSTV0910_FSKRSTEPP,     "RSTV0910_FSKRSTEPP",     "FSK receive step plus" },
    { RSTV0910_FSKRSTEPM,     "RSTV0910_FSKRSTEPM",     "FSK receive step minus" },
    { RSTV0910_FSKRDTH1,      "RSTV0910_FSKRDTH1",      "FSK receive detection threshold 1" },
    { RSTV0910_FSKRDTH0,      "RSTV0910_FSKRDTH0",      "FSK receive detection threshold 0" },
    { RSTV0910_FSKRLOSS,      "RSTV0910_FSKRLOSS",      "FSK receive loss threshold" },

    /* PLL and clock registers */
    { RSTV0910_NCOARSE,       "RSTV0910_NCOARSE",       "PLL N coarse divider" },
    { RSTV0910_NCOARSE1,      "RSTV0910_NCOARSE1",      "PLL N coarse divider 1" },
    { RSTV0910_NCOARSE2,      "RSTV0910_NCOARSE2",      "PLL N coarse divider 2" },
    { RSTV0910_SYNTCTRL,      "RSTV0910_SYNTCTRL",      "Synthesizer control" },
    { RSTV0910_FILTCTRL,      "RSTV0910_FILTCTRL",      "Filter control" },
    { RSTV0910_PLLSTAT,       "RSTV0910_PLLSTAT",       "PLL status" },
    { RSTV0910_STOPCLK1,      "RSTV0910_STOPCLK1",      "Stop clock control 1" },
    { RSTV0910_STOPCLK2,      "RSTV0910_STOPCLK2",      "Stop clock control 2" },
    { RSTV0910_PREGCTL,       "RSTV0910_PREGCTL",       "Power regulator control" },

    /* Test and tuner registers */
    { RSTV0910_TSTTNR0,       "RSTV0910_TSTTNR0",       "Test tuner register 0" },
    { RSTV0910_TSTTNR1,       "RSTV0910_TSTTNR1",       "Test tuner register 1" },
    { RSTV0910_TSTTNR2,       "RSTV0910_TSTTNR2",       "Test tuner register 2" },
    { RSTV0910_TSTTNR3,       "RSTV0910_TSTTNR3",       "Test tuner register 3" },

    /* P2 demodulator registers */
    { RSTV0910_P2_IQCONST,    "RSTV0910_P2_IQCONST",    "P2 IQ constellation control" },
    { RSTV0910_P2_NOSCFG,     "RSTV0910_P2_NOSCFG",     "P2 noise configuration" },
    { RSTV0910_P2_ISYMB,      "RSTV0910_P2_ISYMB",      "P2 I symbol" },
    { RSTV0910_P2_QSYMB,      "RSTV0910_P2_QSYMB",      "P2 Q symbol" },
    { RSTV0910_P2_AGC1CFG,    "RSTV0910_P2_AGC1CFG",    "P2 AGC1 configuration" },
    { RSTV0910_P2_AGC1CN,     "RSTV0910_P2_AGC1CN",     "P2 AGC1 control" },
    { RSTV0910_P2_AGC1REF,    "RSTV0910_P2_AGC1REF",    "P2 AGC1 reference" },
    { RSTV0910_P2_IDCCOMP,    "RSTV0910_P2_IDCCOMP",    "P2 I DC compensation" },
    { RSTV0910_P2_QDCCOMP,    "RSTV0910_P2_QDCCOMP",    "P2 Q DC compensation" },
    { RSTV0910_P2_POWERI,     "RSTV0910_P2_POWERI",     "P2 I power" },
    { RSTV0910_P2_POWERQ,     "RSTV0910_P2_POWERQ",     "P2 Q power" },
    { RSTV0910_P2_AGC1AMM,    "RSTV0910_P2_AGC1AMM",    "P2 AGC1 AMM value" },
    { RSTV0910_P2_AGC1QUAD,   "RSTV0910_P2_AGC1QUAD",   "P2 AGC1 quadrature value" },
    { RSTV0910_P2_AGCIQIN1,   "RSTV0910_P2_AGCIQIN1",   "P2 AGC IQ input MSB" },
    { RSTV0910_P2_AGCIQIN0,   "RSTV0910_P2_AGCIQIN0",   "P2 AGC IQ input LSB" },

    /* Additional commonly accessed registers */
    { RSTV0910_P2_LDI,        "RSTV0910_P2_LDI",        "P2 lock detector integrator" },
    { RSTV0910_P1_LDI,        "RSTV0910_P1_LDI",        "P1 lock detector integrator" },
    { RSTV0910_P2_TMGCFG,     "RSTV0910_P2_TMGCFG",     "P2 timing configuration" },
    { RSTV0910_P1_TMGCFG,     "RSTV0910_P1_TMGCFG",     "P1 timing configuration" },
    { RSTV0910_P2_CARFREQ,    "RSTV0910_P2_CARFREQ",    "P2 carrier frequency control" },
    { RSTV0910_P1_CARFREQ,    "RSTV0910_P1_CARFREQ",    "P1 carrier frequency control" },
    { RSTV0910_P2_DMDSTATE,   "RSTV0910_P2_DMDSTATE",   "P2 demodulator state" },
    { RSTV0910_P1_DMDSTATE,   "RSTV0910_P1_DMDSTATE",   "P1 demodulator state" },
    { RSTV0910_P2_DSTATUS,    "RSTV0910_P2_DSTATUS",    "P2 demodulator status" },
    { RSTV0910_P1_DSTATUS,    "RSTV0910_P1_DSTATUS",    "P1 demodulator status" },
    { RSTV0910_P2_DSTATUS2,   "RSTV0910_P2_DSTATUS2",   "P2 demodulator status 2" },
    { RSTV0910_P1_DSTATUS2,   "RSTV0910_P1_DSTATUS2",   "P1 demodulator status 2" },
    { RSTV0910_P2_DSTATUS3,   "RSTV0910_P2_DSTATUS3",   "P2 demodulator status 3" },
    { RSTV0910_P1_DSTATUS3,   "RSTV0910_P1_DSTATUS3",   "P1 demodulator status 3" },
    { RSTV0910_P2_VERROR,     "RSTV0910_P2_VERROR",     "P2 Viterbi error rate" },
    { RSTV0910_P1_VERROR,     "RSTV0910_P1_VERROR",     "P1 Viterbi error rate" },

    { 0xFFFF, NULL, NULL }  /* End marker */
};

/* -------------------------------------------------------------------------------------------------- */
/* CONTEXT STRING LOOKUP TABLE                                                                        */
/* -------------------------------------------------------------------------------------------------- */

static const char *context_strings[] = {
    "INIT",
    "FREQ_TUNING",
    "PLL_CONFIG",
    "PLL_CAL",
    "SYMBOL_RATE",
    "DEMOD_CTRL",
    "STATE_TRANS",
    "CARRIER_LOOP",
    "TIMING_LOOP",
    "TRANSPORT_STREAM",
    "AGC_CTRL",
    "LNA_CTRL",
    "POWER_MGMT",
    "SCAN_CTRL",
    "ERROR_CORR",
    "STATUS_READ",
    "UNKNOWN"
};

/* -------------------------------------------------------------------------------------------------- */
/* UTILITY FUNCTIONS                                                                                  */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
static bool should_log_demod_sequence(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Checks if enough time has passed to log demod sequence again                                    */
    /* return: true if demod sequence should be logged                                                 */
    /* -------------------------------------------------------------------------------------------------- */
    uint64_t current_time = get_timestamp_ms();

    if (current_time - last_demod_sequence_log_time >= DEMOD_SEQUENCE_LOG_INTERVAL_MS) {
        last_demod_sequence_log_time = current_time;

        /* If we were suppressing logs, indicate that we're resuming */
        if (demod_sequence_suppressed) {
            printf("[%llu] STV0910: Resuming demod sequence logging (suppressed for %llu ms)\n",
                   (unsigned long long)current_time,
                   (unsigned long long)(current_time - (last_demod_sequence_log_time - DEMOD_SEQUENCE_LOG_INTERVAL_MS)));
            demod_sequence_suppressed = false;
        }

        return true;
    }

    /* Mark that we're suppressing logs */
    if (!demod_sequence_suppressed) {
        printf("[%llu] STV0910: Suppressing demod sequence logging for %d ms\n",
               (unsigned long long)current_time,
               DEMOD_SEQUENCE_LOG_INTERVAL_MS);
        demod_sequence_suppressed = true;
    }

    return false;
}

/* -------------------------------------------------------------------------------------------------- */
uint64_t get_timestamp_ms(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets current timestamp in milliseconds                                                          */
    /* return: timestamp in milliseconds                                                               */
    /* -------------------------------------------------------------------------------------------------- */
#ifdef _WIN32
    /* Windows implementation */
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    /* Convert from 100ns intervals since 1601 to milliseconds since 1970 */
    return (uli.QuadPart / 10000ULL) - 11644473600000ULL;
#else
    /* Unix/Linux implementation */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
#endif
}

/* -------------------------------------------------------------------------------------------------- */
void register_logging_init(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Initializes the register logging system                                                         */
    /* -------------------------------------------------------------------------------------------------- */
    register_logging_enabled = true;
    current_context = REG_CONTEXT_INIT;
    
    if (register_logging_enabled) {
        printf("[%llu] REGISTER_LOG: Logging system initialized\n", 
               (unsigned long long)get_timestamp_ms());
    }
}

/* -------------------------------------------------------------------------------------------------- */
void register_logging_enable(bool enable)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Enables or disables register logging at runtime                                                 */
    /* enable: true to enable, false to disable                                                        */
    /* -------------------------------------------------------------------------------------------------- */
    register_logging_enabled = enable;
    
    printf("[%llu] REGISTER_LOG: Logging %s\n", 
           (unsigned long long)get_timestamp_ms(),
           enable ? "ENABLED" : "DISABLED");
}

/* -------------------------------------------------------------------------------------------------- */
bool register_logging_is_enabled(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Returns current logging enable state                                                            */
    /* return: true if logging is enabled                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    return register_logging_enabled;
}

/* -------------------------------------------------------------------------------------------------- */
void register_logging_set_context(register_context_t context)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Sets the current register operation context                                                     */
    /* context: the context for subsequent register operations                                         */
    /* -------------------------------------------------------------------------------------------------- */
    current_context = context;
}

/* -------------------------------------------------------------------------------------------------- */
register_context_t register_logging_get_context(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets the current register operation context                                                     */
    /* return: current context                                                                         */
    /* -------------------------------------------------------------------------------------------------- */
    return current_context;
}

/* -------------------------------------------------------------------------------------------------- */
const char *register_logging_context_to_string(register_context_t context)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Converts context enum to string                                                                 */
    /* context: context to convert                                                                     */
    /* return: string representation of context                                                        */
    /* -------------------------------------------------------------------------------------------------- */
    if (context >= 0 && context < (sizeof(context_strings) / sizeof(context_strings[0]))) {
        return context_strings[context];
    }
    return "INVALID";
}

/* -------------------------------------------------------------------------------------------------- */
/* REGISTER NAME LOOKUP FUNCTIONS                                                                     */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
const char *get_stv6120_register_name(uint8_t reg)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets the name of an STV6120 register                                                            */
    /* reg: register address                                                                           */
    /* return: register name string or "UNKNOWN_REG" if not found                                      */
    /* -------------------------------------------------------------------------------------------------- */
    for (int i = 0; stv6120_register_table[i].name != NULL; i++) {
        if (stv6120_register_table[i].address == reg) {
            return stv6120_register_table[i].name;
        }
    }
    return "UNKNOWN_REG";
}

/* -------------------------------------------------------------------------------------------------- */
const char *get_stv6120_register_description(uint8_t reg)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets the description of an STV6120 register                                                     */
    /* reg: register address                                                                           */
    /* return: register description string or "Unknown register" if not found                         */
    /* -------------------------------------------------------------------------------------------------- */
    for (int i = 0; stv6120_register_table[i].name != NULL; i++) {
        if (stv6120_register_table[i].address == reg) {
            return stv6120_register_table[i].description;
        }
    }
    return "Unknown register";
}

/* -------------------------------------------------------------------------------------------------- */
const char *get_stv0910_register_name(uint16_t reg)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets the name of an STV0910 register                                                            */
    /* reg: register address                                                                           */
    /* return: register name string or "UNKNOWN_REG" if not found                                      */
    /* -------------------------------------------------------------------------------------------------- */
    for (int i = 0; stv0910_register_table[i].name != NULL; i++) {
        if (stv0910_register_table[i].address == reg) {
            return stv0910_register_table[i].name;
        }
    }
    return "UNKNOWN_REG";
}

/* -------------------------------------------------------------------------------------------------- */
const char *get_stv0910_register_description(uint16_t reg)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets the description of an STV0910 register                                                     */
    /* reg: register address                                                                           */
    /* return: register description string or "Unknown register" if not found                         */
    /* -------------------------------------------------------------------------------------------------- */
    for (int i = 0; stv0910_register_table[i].name != NULL; i++) {
        if (stv0910_register_table[i].address == reg) {
            return stv0910_register_table[i].description;
        }
    }
    return "Unknown register";
}

/* -------------------------------------------------------------------------------------------------- */
/* REGISTER LOGGING FUNCTIONS                                                                         */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
void log_stv6120_register_write(uint8_t reg, uint8_t val, register_context_t context)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Logs an STV6120 register write operation                                                        */
    /* reg: register address                                                                           */
    /* val: value being written                                                                        */
    /* context: operation context                                                                      */
    /* -------------------------------------------------------------------------------------------------- */
    if (!register_logging_enabled) return;

    printf("[%llu] STV6120: Writing %s (0x%02x) = 0x%02x (%d) - %s [%s]\n",
           (unsigned long long)get_timestamp_ms(),
           get_stv6120_register_name(reg),
           reg,
           val,
           val,
           get_stv6120_register_description(reg),
           register_logging_context_to_string(context));
}

/* -------------------------------------------------------------------------------------------------- */
void log_stv6120_register_read(uint8_t reg, uint8_t val, register_context_t context)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Logs an STV6120 register read operation                                                         */
    /* reg: register address                                                                           */
    /* val: value that was read                                                                        */
    /* context: operation context                                                                      */
    /* -------------------------------------------------------------------------------------------------- */
    if (!register_logging_enabled) return;

    printf("[%llu] STV6120: Reading %s (0x%02x) = 0x%02x (%d) - %s [%s]\n",
           (unsigned long long)get_timestamp_ms(),
           get_stv6120_register_name(reg),
           reg,
           val,
           val,
           get_stv6120_register_description(reg),
           register_logging_context_to_string(context));
}

/* -------------------------------------------------------------------------------------------------- */
void log_stv0910_register_write(uint16_t reg, uint8_t val, register_context_t context)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Logs an STV0910 register write operation                                                        */
    /* reg: register address                                                                           */
    /* val: value being written                                                                        */
    /* context: operation context                                                                      */
    /* -------------------------------------------------------------------------------------------------- */
    if (!register_logging_enabled) return;

    printf("[%llu] STV0910: Writing %s (0x%04x) = 0x%02x (%d) - %s [%s]\n",
           (unsigned long long)get_timestamp_ms(),
           get_stv0910_register_name(reg),
           reg,
           val,
           val,
           get_stv0910_register_description(reg),
           register_logging_context_to_string(context));
}

/* -------------------------------------------------------------------------------------------------- */
void log_stv0910_register_read(uint16_t reg, uint8_t val, register_context_t context)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Logs an STV0910 register read operation with rate limiting for demod sequences                  */
    /* reg: register address                                                                           */
    /* val: value that was read                                                                        */
    /* context: operation context                                                                      */
    /* -------------------------------------------------------------------------------------------------- */
    if (!register_logging_enabled) return;

    /* Apply rate limiting for demod control context */
    if (context == REG_CONTEXT_DEMOD_CONTROL) {
        if (!should_log_demod_sequence()) {
            return;  /* Skip logging this register read */
        }
    }

    printf("[%llu] STV0910: Reading %s (0x%04x) = 0x%02x (%d) - %s [%s]\n",
           (unsigned long long)get_timestamp_ms(),
           get_stv0910_register_name(reg),
           reg,
           val,
           val,
           get_stv0910_register_description(reg),
           register_logging_context_to_string(context));
}

/* -------------------------------------------------------------------------------------------------- */
void log_register_sequence_start(const char *sequence_name)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Logs the start of a register operation sequence                                                 */
    /* sequence_name: name of the sequence being started                                               */
    /* -------------------------------------------------------------------------------------------------- */
    if (!register_logging_enabled) return;

    printf("[%llu] SEQUENCE_START: %s\n",
           (unsigned long long)get_timestamp_ms(),
           sequence_name);
}

/* -------------------------------------------------------------------------------------------------- */
void log_register_sequence_end(const char *sequence_name)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Logs the end of a register operation sequence                                                   */
    /* sequence_name: name of the sequence being ended                                                 */
    /* -------------------------------------------------------------------------------------------------- */
    if (!register_logging_enabled) return;

    printf("[%llu] SEQUENCE_END: %s\n",
           (unsigned long long)get_timestamp_ms(),
           sequence_name);
}
