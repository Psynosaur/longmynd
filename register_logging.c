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

    /* P2 demodulator control registers */
    { RSTV0910_P2_DEMOD,      "RSTV0910_P2_DEMOD",      "P2 demodulator control" },
    { RSTV0910_P2_DMDMODCOD,  "RSTV0910_P2_DMDMODCOD",  "P2 demodulator MODCOD" },
    { RSTV0910_P2_DMDCFGMD,   "RSTV0910_P2_DMDCFGMD",   "P2 demodulator config mode" },
    { RSTV0910_P2_DMDCFG2,    "RSTV0910_P2_DMDCFG2",    "P2 demodulator config 2" },
    { RSTV0910_P2_DMDT0M,     "RSTV0910_P2_DMDT0M",     "P2 demodulator T0 minimum" },
    { RSTV0910_P2_DMDFLYW,    "RSTV0910_P2_DMDFLYW",    "P2 demodulator flywheel" },
    { RSTV0910_P2_DMDCFG3,    "RSTV0910_P2_DMDCFG3",    "P2 demodulator config 3" },
    { RSTV0910_P2_DMDCFG4,    "RSTV0910_P2_DMDCFG4",    "P2 demodulator config 4" },
    { RSTV0910_P2_CORRELMANT, "RSTV0910_P2_CORRELMANT", "P2 correlation mantissa" },
    { RSTV0910_P2_CORRELABS,  "RSTV0910_P2_CORRELABS",  "P2 correlation absolute" },
    { RSTV0910_P2_CORRELEXP,  "RSTV0910_P2_CORRELEXP",  "P2 correlation exponent" },

    /* P2 PLH and AGC registers */
    { RSTV0910_P2_PLHMODCOD,  "RSTV0910_P2_PLHMODCOD",  "P2 PLH MODCOD" },
    { RSTV0910_P2_DMDREG,     "RSTV0910_P2_DMDREG",     "P2 demodulator register" },
    { RSTV0910_P2_AGCNADJ,    "RSTV0910_P2_AGCNADJ",    "P2 AGC N adjustment" },
    { RSTV0910_P2_AGCKS,      "RSTV0910_P2_AGCKS",      "P2 AGC KS" },
    { RSTV0910_P2_AGCKQ,      "RSTV0910_P2_AGCKQ",      "P2 AGC KQ" },
    { RSTV0910_P2_AGCK8,      "RSTV0910_P2_AGCK8",      "P2 AGC K8PSK" },
    { RSTV0910_P2_AGCK16,     "RSTV0910_P2_AGCK16",     "P2 AGC K16APSK" },
    { RSTV0910_P2_AGCK32,     "RSTV0910_P2_AGCK32",     "P2 AGC K32APSK" },
    { RSTV0910_P2_AGC2O,      "RSTV0910_P2_AGC2O",      "P2 AGC2 output" },
    { RSTV0910_P2_AGC2REF,    "RSTV0910_P2_AGC2REF",    "P2 AGC2 reference" },
    { RSTV0910_P2_AGC1ADJ,    "RSTV0910_P2_AGC1ADJ",    "P2 AGC1 adjustment" },
    { RSTV0910_P2_AGCRSADJ,   "RSTV0910_P2_AGCRSADJ",   "P2 AGC RS adjustment" },

    /* P2 carrier frequency registers */
    { RSTV0910_P2_CARCFG,     "RSTV0910_P2_CARCFG",     "P2 carrier configuration" },
    { RSTV0910_P2_ACLC,       "RSTV0910_P2_ACLC",       "P2 automatic carrier loop control" },
    { RSTV0910_P2_BCLC,       "RSTV0910_P2_BCLC",       "P2 bandwidth carrier loop control" },
    { RSTV0910_P2_CARFREQ,    "RSTV0910_P2_CARFREQ",    "P2 carrier frequency control" },
    { RSTV0910_P2_CARHDR,     "RSTV0910_P2_CARHDR",     "P2 carrier header" },
    { RSTV0910_P2_LDT,        "RSTV0910_P2_LDT",        "P2 lock detector threshold" },

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

    /* P2 timing registers */
    { RSTV0910_P2_RTC,        "RSTV0910_P2_RTC",        "P2 timing control" },
    { RSTV0910_P2_RTCS2,      "RSTV0910_P2_RTCS2",      "P2 timing control S2" },
    { RSTV0910_P2_TMGTHRISE,  "RSTV0910_P2_TMGTHRISE",  "P2 timing threshold rise" },
    { RSTV0910_P2_TMGTHFALL,  "RSTV0910_P2_TMGTHFALL",  "P2 timing threshold fall" },

    /* P2 symbol rate and timing registers */
    { RSTV0910_P2_SFRUPRATIO, "RSTV0910_P2_SFRUPRATIO", "P2 symbol rate upper ratio" },
    { RSTV0910_P2_SFRLOWRATIO, "RSTV0910_P2_SFRLOWRATIO", "P2 symbol rate lower ratio" },
    { RSTV0910_P2_KREFTMG,    "RSTV0910_P2_KREFTMG",    "P2 K reference timing" },
    { RSTV0910_P2_SFRSTEP,    "RSTV0910_P2_SFRSTEP",    "P2 symbol rate step" },
    { RSTV0910_P2_TMGCFG2,    "RSTV0910_P2_TMGCFG2",    "P2 timing configuration 2" },
    { RSTV0910_P2_KREFTMG2,   "RSTV0910_P2_KREFTMG2",   "P2 K reference timing 2" },

    /* P2 symbol rate measurement registers */
    { RSTV0910_P2_SFRLOW1,    "RSTV0910_P2_SFRLOW1",    "P2 symbol rate low MSB" },
    { RSTV0910_P2_SFRLOW0,    "RSTV0910_P2_SFRLOW0",    "P2 symbol rate low LSB" },
    { RSTV0910_P2_SFR3,       "RSTV0910_P2_SFR3",       "P2 symbol rate 3" },
    { RSTV0910_P2_SFR2,       "RSTV0910_P2_SFR2",       "P2 symbol rate 2" },
    { RSTV0910_P2_SFR1,       "RSTV0910_P2_SFR1",       "P2 symbol rate 1" },
    { RSTV0910_P2_SFR0,       "RSTV0910_P2_SFR0",       "P2 symbol rate 0" },

    /* P2 timing measurement registers */
    { RSTV0910_P2_TMGREG2,    "RSTV0910_P2_TMGREG2",    "P2 timing register 2" },
    { RSTV0910_P2_TMGREG1,    "RSTV0910_P2_TMGREG1",    "P2 timing register 1" },
    { RSTV0910_P2_TMGREG0,    "RSTV0910_P2_TMGREG0",    "P2 timing register 0" },
    { RSTV0910_P2_TMGLOCK1,   "RSTV0910_P2_TMGLOCK1",   "P2 timing lock 1" },
    { RSTV0910_P2_TMGLOCK0,   "RSTV0910_P2_TMGLOCK0",   "P2 timing lock 0" },
    { RSTV0910_P2_TMGOBS,     "RSTV0910_P2_TMGOBS",     "P2 timing observation" },
    { RSTV0910_P2_EQUALCFG,   "RSTV0910_P2_EQUALCFG",   "P2 equalizer configuration" },
    { RSTV0910_P2_EQUAI1,     "RSTV0910_P2_EQUAI1",     "P2 equalizer AI 1" },
    { RSTV0910_P2_EQUAQ1,     "RSTV0910_P2_EQUAQ1",     "P2 equalizer AQ 1" },
    { RSTV0910_P2_EQUAI2,     "RSTV0910_P2_EQUAI2",     "P2 equalizer AI 2" },
    { RSTV0910_P2_EQUAQ2,     "RSTV0910_P2_EQUAQ2",     "P2 equalizer AQ 2" },
    { RSTV0910_P2_EQUAI3,     "RSTV0910_P2_EQUAI3",     "P2 equalizer AI 3" },
    { RSTV0910_P2_EQUAQ3,     "RSTV0910_P2_EQUAQ3",     "P2 equalizer AQ 3" },
    { RSTV0910_P2_EQUAI4,     "RSTV0910_P2_EQUAI4",     "P2 equalizer AI 4" },
    { RSTV0910_P2_EQUAQ4,     "RSTV0910_P2_EQUAQ4",     "P2 equalizer AQ 4" },
    { RSTV0910_P2_EQUAI5,     "RSTV0910_P2_EQUAI5",     "P2 equalizer AI 5" },
    { RSTV0910_P2_EQUAQ5,     "RSTV0910_P2_EQUAQ5",     "P2 equalizer AQ 5" },
    { RSTV0910_P2_EQUAI6,     "RSTV0910_P2_EQUAI6",     "P2 equalizer AI 6" },
    { RSTV0910_P2_EQUAQ6,     "RSTV0910_P2_EQUAQ6",     "P2 equalizer AQ 6" },
    { RSTV0910_P2_EQUAI7,     "RSTV0910_P2_EQUAI7",     "P2 equalizer AI 7" },
    { RSTV0910_P2_EQUAQ7,     "RSTV0910_P2_EQUAQ7",     "P2 equalizer AQ 7" },
    { RSTV0910_P2_EQUAI8,     "RSTV0910_P2_EQUAI8",     "P2 equalizer AI 8" },
    { RSTV0910_P2_EQUAQ8,     "RSTV0910_P2_EQUAQ8",     "P2 equalizer AQ 8" },

    /* P2 noise estimation registers */
    { RSTV0910_P2_NOSCFGF1,   "RSTV0910_P2_NOSCFGF1",   "P2 noise configuration F1" },
    { RSTV0910_P2_CAR2CFG,    "RSTV0910_P2_CAR2CFG",    "P2 carrier 2 configuration" },
    { RSTV0910_P2_CFR2CFR1,   "RSTV0910_P2_CFR2CFR1",   "P2 CFR2 to CFR1" },
    { RSTV0910_P2_CAR3CFG,    "RSTV0910_P2_CAR3CFG",    "P2 carrier 3 configuration" },
    { RSTV0910_P2_CFR22,      "RSTV0910_P2_CFR22",      "P2 carrier frequency 22" },
    { RSTV0910_P2_CFR21,      "RSTV0910_P2_CFR21",      "P2 carrier frequency 21" },
    { RSTV0910_P2_CFR20,      "RSTV0910_P2_CFR20",      "P2 carrier frequency 20" },
    { RSTV0910_P2_ACLC2S2Q,   "RSTV0910_P2_ACLC2S2Q",   "P2 ACLC2 S2Q" },
    { RSTV0910_P2_ACLC2S28,   "RSTV0910_P2_ACLC2S28",   "P2 ACLC2 S28" },
    { RSTV0910_P2_ACLC2S216A, "RSTV0910_P2_ACLC2S216A", "P2 ACLC2 S216A" },
    { RSTV0910_P2_ACLC2S232A, "RSTV0910_P2_ACLC2S232A", "P2 ACLC2 S232A" },

    /* P2 additional noise and measurement registers */
    { RSTV0910_P2_BCLC2S2Q,   "RSTV0910_P2_BCLC2S2Q",   "P2 BCLC2 S2Q" },
    { RSTV0910_P2_BCLC2S28,   "RSTV0910_P2_BCLC2S28",   "P2 BCLC2 S28" },
    { RSTV0910_P2_BCLC2S216A, "RSTV0910_P2_BCLC2S216A", "P2 BCLC2 S216A" },
    { RSTV0910_P2_BCLC2S232A, "RSTV0910_P2_BCLC2S232A", "P2 BCLC2 S232A" },
    { RSTV0910_P2_PLROOT2,    "RSTV0910_P2_PLROOT2",    "P2 PL root 2" },
    { RSTV0910_P2_PLROOT1,    "RSTV0910_P2_PLROOT1",    "P2 PL root 1" },
    { RSTV0910_P2_PLROOT0,    "RSTV0910_P2_PLROOT0",    "P2 PL root 0" },
    { RSTV0910_P2_MODCODLST0, "RSTV0910_P2_MODCODLST0", "P2 MODCOD list 0" },
    { RSTV0910_P2_MODCODLST1, "RSTV0910_P2_MODCODLST1", "P2 MODCOD list 1" },
    { RSTV0910_P2_MODCODLST2, "RSTV0910_P2_MODCODLST2", "P2 MODCOD list 2" },
    { RSTV0910_P2_MODCODLST3, "RSTV0910_P2_MODCODLST3", "P2 MODCOD list 3" },
    { RSTV0910_P2_MODCODLST4, "RSTV0910_P2_MODCODLST4", "P2 MODCOD list 4" },
    { RSTV0910_P2_MODCODLST5, "RSTV0910_P2_MODCODLST5", "P2 MODCOD list 5" },
    { RSTV0910_P2_MODCODLST6, "RSTV0910_P2_MODCODLST6", "P2 MODCOD list 6" },
    { RSTV0910_P2_MODCODLST7, "RSTV0910_P2_MODCODLST7", "P2 MODCOD list 7" },
    { RSTV0910_P2_MODCODLST8, "RSTV0910_P2_MODCODLST8", "P2 MODCOD list 8" },
    { RSTV0910_P2_MODCODLST9, "RSTV0910_P2_MODCODLST9", "P2 MODCOD list 9" },
    { RSTV0910_P2_MODCODLSTA, "RSTV0910_P2_MODCODLSTA", "P2 MODCOD list A" },
    { RSTV0910_P2_MODCODLSTB, "RSTV0910_P2_MODCODLSTB", "P2 MODCOD list B" },
    { RSTV0910_P2_MODCODLSTC, "RSTV0910_P2_MODCODLSTC", "P2 MODCOD list C" },
    { RSTV0910_P2_MODCODLSTD, "RSTV0910_P2_MODCODLSTD", "P2 MODCOD list D" },
    { RSTV0910_P2_MODCODLSTE, "RSTV0910_P2_MODCODLSTE", "P2 MODCOD list E" },
    { RSTV0910_P2_MODCODLSTF, "RSTV0910_P2_MODCODLSTF", "P2 MODCOD list F" },

    /* P2 noise estimation configuration */
    { RSTV0910_P2_NOSCFGF2,   "RSTV0910_P2_NOSCFGF2",   "P2 noise configuration F2" },
    { RSTV0910_P2_NOSTHRES1,  "RSTV0910_P2_NOSTHRES1",  "P2 noise threshold 1" },
    { RSTV0910_P2_NOSTHRES2,  "RSTV0910_P2_NOSTHRES2",  "P2 noise threshold 2" },
    { RSTV0910_P2_NOSDIFF1,   "RSTV0910_P2_NOSDIFF1",   "P2 noise difference 1" },
    { RSTV0910_P2_RAINFADE,   "RSTV0910_P2_RAINFADE",   "P2 rain fade" },
    { RSTV0910_P2_NOSRAMCFG,  "RSTV0910_P2_NOSRAMCFG",  "P2 noise RAM configuration" },
    { RSTV0910_P2_NOSRAMPOS,  "RSTV0910_P2_NOSRAMPOS",  "P2 noise RAM position" },
    { RSTV0910_P2_NOSRAMVAL,  "RSTV0910_P2_NOSRAMVAL",  "P2 noise RAM value" },

    /* P2 PLH statistics */
    { RSTV0910_P2_DMDPLHSTAT, "RSTV0910_P2_DMDPLHSTAT", "P2 demodulator PLH statistics" },

    /* P2 Viterbi registers */
    { RSTV0910_P2_VITSCALE,   "RSTV0910_P2_VITSCALE",   "P2 Viterbi scale" },
    { RSTV0910_P2_FECM,       "RSTV0910_P2_FECM",       "P2 FEC mode" },
    { RSTV0910_P2_VTH12,      "RSTV0910_P2_VTH12",      "P2 Viterbi threshold 1/2" },
    { RSTV0910_P2_VTH23,      "RSTV0910_P2_VTH23",      "P2 Viterbi threshold 2/3" },
    { RSTV0910_P2_VTH34,      "RSTV0910_P2_VTH34",      "P2 Viterbi threshold 3/4" },
    { RSTV0910_P2_VTH56,      "RSTV0910_P2_VTH56",      "P2 Viterbi threshold 5/6" },
    { RSTV0910_P2_VTH67,      "RSTV0910_P2_VTH67",      "P2 Viterbi threshold 6/7" },
    { RSTV0910_P2_VTH78,      "RSTV0910_P2_VTH78",      "P2 Viterbi threshold 7/8" },
    { RSTV0910_P2_PRVIT,      "RSTV0910_P2_PRVIT",      "P2 Viterbi puncture rate" },
    { RSTV0910_P2_VAVSRVIT,   "RSTV0910_P2_VAVSRVIT",   "P2 Viterbi average/status" },
    { RSTV0910_P2_VSTATUSVIT, "RSTV0910_P2_VSTATUSVIT", "P2 Viterbi status" },

    /* P2 Transport stream registers */
    { RSTV0910_P2_TSPIDFLT1,  "RSTV0910_P2_TSPIDFLT1",  "P2 TS PID filter 1" },
    { RSTV0910_P2_TSPIDFLT0,  "RSTV0910_P2_TSPIDFLT0",  "P2 TS PID filter 0" },
    { RSTV0910_P2_PDELCTRL0,  "RSTV0910_P2_PDELCTRL0",  "P2 packet delineator control 0" },
    { RSTV0910_P2_PDELCTRL1,  "RSTV0910_P2_PDELCTRL1",  "P2 packet delineator control 1" },
    { RSTV0910_P2_PDELCTRL2,  "RSTV0910_P2_PDELCTRL2",  "P2 packet delineator control 2" },
    { RSTV0910_P2_HYSTTHRESH, "RSTV0910_P2_HYSTTHRESH", "P2 hysteresis threshold" },
    { RSTV0910_P2_UPLCCST0,   "RSTV0910_P2_UPLCCST0",   "P2 UPL constant 0" },
    { RSTV0910_P2_ISIENTRY,   "RSTV0910_P2_ISIENTRY",   "P2 ISI entry" },
    { RSTV0910_P2_ISIBITENA,  "RSTV0910_P2_ISIBITENA",  "P2 ISI bit enable" },
    { RSTV0910_P2_TSINSDELM,  "RSTV0910_P2_TSINSDELM",  "P2 TS insertion/deletion MSB" },
    { RSTV0910_P2_TSINSDELL,  "RSTV0910_P2_TSINSDELL",  "P2 TS insertion/deletion LSB" },

    /* P1 demodulator control registers (mirror of P2) */
    { RSTV0910_P1_DEMOD,      "RSTV0910_P1_DEMOD",      "P1 demodulator control" },
    { RSTV0910_P1_DMDMODCOD,  "RSTV0910_P1_DMDMODCOD",  "P1 demodulator MODCOD" },
    { RSTV0910_P1_DMDCFGMD,   "RSTV0910_P1_DMDCFGMD",   "P1 demodulator config mode" },
    { RSTV0910_P1_DMDCFG2,    "RSTV0910_P1_DMDCFG2",    "P1 demodulator config 2" },
    { RSTV0910_P1_DMDT0M,     "RSTV0910_P1_DMDT0M",     "P1 demodulator T0 minimum" },
    { RSTV0910_P1_DMDFLYW,    "RSTV0910_P1_DMDFLYW",    "P1 demodulator flywheel" },
    { RSTV0910_P1_DMDCFG3,    "RSTV0910_P1_DMDCFG3",    "P1 demodulator config 3" },
    { RSTV0910_P1_DMDCFG4,    "RSTV0910_P1_DMDCFG4",    "P1 demodulator config 4" },
    { RSTV0910_P1_CORRELMANT, "RSTV0910_P1_CORRELMANT", "P1 correlation mantissa" },
    { RSTV0910_P1_CORRELABS,  "RSTV0910_P1_CORRELABS",  "P1 correlation absolute" },

    /* P1 timing registers */
    { RSTV0910_P1_RTC,        "RSTV0910_P1_RTC",        "P1 timing control" },
    { RSTV0910_P1_RTCS2,      "RSTV0910_P1_RTCS2",      "P1 timing control S2" },
    { RSTV0910_P1_TMGTHRISE,  "RSTV0910_P1_TMGTHRISE",  "P1 timing threshold rise" },
    { RSTV0910_P1_TMGTHFALL,  "RSTV0910_P1_TMGTHFALL",  "P1 timing threshold fall" },

    /* P1 Viterbi registers */
    { RSTV0910_P1_VITSCALE,   "RSTV0910_P1_VITSCALE",   "P1 Viterbi scale" },
    { RSTV0910_P1_FECM,       "RSTV0910_P1_FECM",       "P1 FEC mode" },
    { RSTV0910_P1_VTH12,      "RSTV0910_P1_VTH12",      "P1 Viterbi threshold 1/2" },
    { RSTV0910_P1_VTH23,      "RSTV0910_P1_VTH23",      "P1 Viterbi threshold 2/3" },
    { RSTV0910_P1_VTH34,      "RSTV0910_P1_VTH34",      "P1 Viterbi threshold 3/4" },
    { RSTV0910_P1_VTH56,      "RSTV0910_P1_VTH56",      "P1 Viterbi threshold 5/6" },
    { RSTV0910_P1_VTH67,      "RSTV0910_P1_VTH67",      "P1 Viterbi threshold 6/7" },
    { RSTV0910_P1_VTH78,      "RSTV0910_P1_VTH78",      "P1 Viterbi threshold 7/8" },
    { RSTV0910_P1_PRVIT,      "RSTV0910_P1_PRVIT",      "P1 Viterbi puncture rate" },
    { RSTV0910_P1_VAVSRVIT,   "RSTV0910_P1_VAVSRVIT",   "P1 Viterbi average/status" },
    { RSTV0910_P1_VSTATUSVIT, "RSTV0910_P1_VSTATUSVIT", "P1 Viterbi status" },

    /* P1 Transport stream registers */
    { RSTV0910_P1_TSPIDFLT1,  "RSTV0910_P1_TSPIDFLT1",  "P1 TS PID filter 1" },
    { RSTV0910_P1_TSPIDFLT0,  "RSTV0910_P1_TSPIDFLT0",  "P1 TS PID filter 0" },
    { RSTV0910_P1_PDELCTRL0,  "RSTV0910_P1_PDELCTRL0",  "P1 packet delineator control 0" },
    { RSTV0910_P1_PDELCTRL1,  "RSTV0910_P1_PDELCTRL1",  "P1 packet delineator control 1" },
    { RSTV0910_P1_PDELCTRL2,  "RSTV0910_P1_PDELCTRL2",  "P1 packet delineator control 2" },
    { RSTV0910_P1_HYSTTHRESH, "RSTV0910_P1_HYSTTHRESH", "P1 hysteresis threshold" },
    { RSTV0910_P1_UPLCCST0,   "RSTV0910_P1_UPLCCST0",   "P1 UPL constant 0" },
    { RSTV0910_P1_ISIENTRY,   "RSTV0910_P1_ISIENTRY",   "P1 ISI entry" },
    { RSTV0910_P1_ISIBITENA,  "RSTV0910_P1_ISIBITENA",  "P1 ISI bit enable" },
    { RSTV0910_P1_TSINSDELM,  "RSTV0910_P1_TSINSDELM",  "P1 TS insertion/deletion MSB" },
    { RSTV0910_P1_TSINSDELL,  "RSTV0910_P1_TSINSDELL",  "P1 TS insertion/deletion LSB" },

    /* Reed-Solomon and BCH registers */
    { RSTV0910_RCINSDEL1,     "RSTV0910_RCINSDEL1",     "Reed-Solomon insertion/deletion 1" },
    { RSTV0910_RCINSDEL0,     "RSTV0910_RCINSDEL0",     "Reed-Solomon insertion/deletion 0" },

    /* LDPC and BCH error correction registers */
    { RSTV0910_CFGEXT,        "RSTV0910_CFGEXT",        "Configuration extension" },
    { RSTV0910_GENCFG,        "RSTV0910_GENCFG",        "General configuration" },
    { RSTV0910_LDPCERR1,      "RSTV0910_LDPCERR1",      "LDPC error count MSB" },
    { RSTV0910_LDPCERR0,      "RSTV0910_LDPCERR0",      "LDPC error count LSB" },
    { RSTV0910_BCHERR,        "RSTV0910_BCHERR",        "BCH error count" },

    /* P2 additional registers */
    { RSTV0910_P2_LOCKTIME3,  "RSTV0910_P2_LOCKTIME3",  "P2 demodulator lock time 3" },
    { RSTV0910_P2_LOCKTIME2,  "RSTV0910_P2_LOCKTIME2",  "P2 demodulator lock time 2" },
    { RSTV0910_P2_LOCKTIME1,  "RSTV0910_P2_LOCKTIME1",  "P2 demodulator lock time 1" },
    { RSTV0910_P2_LOCKTIME0,  "RSTV0910_P2_LOCKTIME0",  "P2 demodulator lock time 0" },

    /* P2 Transport stream debug registers */
    { RSTV0910_P2_TSDEBUGL,   "RSTV0910_P2_TSDEBUGL",   "P2 transport stream debug LSB" },
    { RSTV0910_P2_TSSYNC,     "RSTV0910_P2_TSSYNC",     "P2 transport stream sync" },

    /* P2 Transport stream state registers */
    { RSTV0910_P2_TSSTATEM,   "RSTV0910_P2_TSSTATEM",   "P2 transport stream state MSB" },
    { RSTV0910_P2_TSSTATEL,   "RSTV0910_P2_TSSTATEL",   "P2 transport stream state LSB" },

    /* P1 additional registers */
    { RSTV0910_P1_LOCKTIME3,  "RSTV0910_P1_LOCKTIME3",  "P1 demodulator lock time 3" },
    { RSTV0910_P1_LOCKTIME2,  "RSTV0910_P1_LOCKTIME2",  "P1 demodulator lock time 2" },
    { RSTV0910_P1_LOCKTIME1,  "RSTV0910_P1_LOCKTIME1",  "P1 demodulator lock time 1" },
    { RSTV0910_P1_LOCKTIME0,  "RSTV0910_P1_LOCKTIME0",  "P1 demodulator lock time 0" },

    /* P1 Transport stream debug registers */
    { RSTV0910_P1_TSDEBUGL,   "RSTV0910_P1_TSDEBUGL",   "P1 transport stream debug LSB" },
    { RSTV0910_P1_TSSYNC,     "RSTV0910_P1_TSSYNC",     "P1 transport stream sync" },

    /* P1 Transport stream state registers */
    { RSTV0910_P1_TSSTATEM,   "RSTV0910_P1_TSSTATEM",   "P1 transport stream state MSB" },
    { RSTV0910_P1_TSSTATEL,   "RSTV0910_P1_TSSTATEL",   "P1 transport stream state LSB" },

    /* P1 LDPC registers */
    { RSTV0910_P1_MAXEXTRAITER, "RSTV0910_P1_MAXEXTRAITER", "P1 maximum extra iterations" },

    /* Test registers (high range) - removed duplicates */

    /* P2 additional carrier and timing registers */
    { 0xf230, "RSTV0910_P2_UNKNOWN_230", "P2 unknown register 0x230" },
    { 0xf231, "RSTV0910_P2_UNKNOWN_231", "P2 unknown register 0x231" },
    { 0xf232, "RSTV0910_P2_UNKNOWN_232", "P2 unknown register 0x232" },
    { 0xf233, "RSTV0910_P2_UNKNOWN_233", "P2 unknown register 0x233" },
    { 0xf234, "RSTV0910_P2_UNKNOWN_234", "P2 unknown register 0x234" },
    { 0xf235, "RSTV0910_P2_UNKNOWN_235", "P2 unknown register 0x235" },
    { 0xf236, "RSTV0910_P2_UNKNOWN_236", "P2 unknown register 0x236" },
    { 0xf237, "RSTV0910_P2_UNKNOWN_237", "P2 unknown register 0x237" },
    { 0xf238, "RSTV0910_P2_CARCFG",      "P2 carrier configuration" },
    { 0xf23b, "RSTV0910_P2_UNKNOWN_23B", "P2 unknown register 0x23b" },
    { 0xf23c, "RSTV0910_P2_UNKNOWN_23C", "P2 unknown register 0x23c" },

    /* P2 symbol rate and timing registers (missing ones) */
    { 0xf257, "RSTV0910_P2_UNKNOWN_257", "P2 unknown register 0x257" },
    { 0xf25d, "RSTV0910_P2_UNKNOWN_25D", "P2 unknown register 0x25d" },
    { 0xf260, "RSTV0910_P2_UNKNOWN_260", "P2 unknown register 0x260" },
    { 0xf261, "RSTV0910_P2_UNKNOWN_261", "P2 unknown register 0x261" },

    /* P2 equalizer and noise registers (missing ones) */
    { 0xf280, "RSTV0910_P2_UNKNOWN_280", "P2 unknown register 0x280" },
    { 0xf281, "RSTV0910_P2_UNKNOWN_281", "P2 unknown register 0x281" },
    { 0xf282, "RSTV0910_P2_UNKNOWN_282", "P2 unknown register 0x282" },
    { 0xf283, "RSTV0910_P2_UNKNOWN_283", "P2 unknown register 0x283" },
    { 0xf284, "RSTV0910_P2_UNKNOWN_284", "P2 unknown register 0x284" },
    { 0xf285, "RSTV0910_P2_UNKNOWN_285", "P2 unknown register 0x285" },
    { 0xf286, "RSTV0910_P2_UNKNOWN_286", "P2 unknown register 0x286" },
    { 0xf287, "RSTV0910_P2_UNKNOWN_287", "P2 unknown register 0x287" },
    { 0xf288, "RSTV0910_P2_UNKNOWN_288", "P2 unknown register 0x288" },
    { 0xf289, "RSTV0910_P2_UNKNOWN_289", "P2 unknown register 0x289" },
    { 0xf28a, "RSTV0910_P2_UNKNOWN_28A", "P2 unknown register 0x28a" },
    { 0xf28b, "RSTV0910_P2_UNKNOWN_28B", "P2 unknown register 0x28b" },
    { 0xf28c, "RSTV0910_P2_UNKNOWN_28C", "P2 unknown register 0x28c" },
    { 0xf28d, "RSTV0910_P2_UNKNOWN_28D", "P2 unknown register 0x28d" },

    /* P2 carrier and modulation registers (missing ones) */
    { 0xf2c0, "RSTV0910_P2_UNKNOWN_2C0", "P2 unknown register 0x2c0" },
    { 0xf2c1, "RSTV0910_P2_UNKNOWN_2C1", "P2 unknown register 0x2c1" },
    { 0xf2c2, "RSTV0910_P2_UNKNOWN_2C2", "P2 unknown register 0x2c2" },
    { 0xf2c3, "RSTV0910_P2_UNKNOWN_2C3", "P2 unknown register 0x2c3" },
    { 0xf2c4, "RSTV0910_P2_UNKNOWN_2C4", "P2 unknown register 0x2c4" },
    { 0xf2c5, "RSTV0910_P2_UNKNOWN_2C5", "P2 unknown register 0x2c5" },
    { 0xf2c6, "RSTV0910_P2_UNKNOWN_2C6", "P2 unknown register 0x2c6" },
    { 0xf2c7, "RSTV0910_P2_UNKNOWN_2C7", "P2 unknown register 0x2c7" },
    { 0xf2c8, "RSTV0910_P2_UNKNOWN_2C8", "P2 unknown register 0x2c8" },
    { 0xf2c9, "RSTV0910_P2_UNKNOWN_2C9", "P2 unknown register 0x2c9" },
    { 0xf2ca, "RSTV0910_P2_UNKNOWN_2CA", "P2 unknown register 0x2ca" },
    { 0xf2cb, "RSTV0910_P2_UNKNOWN_2CB", "P2 unknown register 0x2cb" },
    { 0xf2cc, "RSTV0910_P2_UNKNOWN_2CC", "P2 unknown register 0x2cc" },
    { 0xf2cd, "RSTV0910_P2_UNKNOWN_2CD", "P2 unknown register 0x2cd" },
    { 0xf2ce, "RSTV0910_P2_UNKNOWN_2CE", "P2 unknown register 0x2ce" },
    { 0xf2cf, "RSTV0910_P2_UNKNOWN_2CF", "P2 unknown register 0x2cf" },
    { 0xf2d0, "RSTV0910_P2_UNKNOWN_2D0", "P2 unknown register 0x2d0" },
    { 0xf2d1, "RSTV0910_P2_UNKNOWN_2D1", "P2 unknown register 0x2d1" },
    { 0xf2d2, "RSTV0910_P2_UNKNOWN_2D2", "P2 unknown register 0x2d2" },
    { 0xf2d3, "RSTV0910_P2_UNKNOWN_2D3", "P2 unknown register 0x2d3" },
    { 0xf2d4, "RSTV0910_P2_UNKNOWN_2D4", "P2 unknown register 0x2d4" },
    { 0xf2d5, "RSTV0910_P2_UNKNOWN_2D5", "P2 unknown register 0x2d5" },
    { 0xf2d6, "RSTV0910_P2_UNKNOWN_2D6", "P2 unknown register 0x2d6" },
    { 0xf2d7, "RSTV0910_P2_UNKNOWN_2D7", "P2 unknown register 0x2d7" },
    { 0xf2d8, "RSTV0910_P2_UNKNOWN_2D8", "P2 unknown register 0x2d8" },
    { 0xf2e1, "RSTV0910_P2_UNKNOWN_2E1", "P2 unknown register 0x2e1" },

    /* P2 additional configuration registers */
    { 0xf300, "RSTV0910_P2_UNKNOWN_300", "P2 unknown register 0x300" },
    { 0xf301, "RSTV0910_P2_UNKNOWN_301", "P2 unknown register 0x301" },
    { 0xf302, "RSTV0910_P2_UNKNOWN_302", "P2 unknown register 0x302" },
    { 0xf303, "RSTV0910_P2_UNKNOWN_303", "P2 unknown register 0x303" },
    { 0xf304, "RSTV0910_P2_UNKNOWN_304", "P2 unknown register 0x304" },
    { 0xf305, "RSTV0910_P2_UNKNOWN_305", "P2 unknown register 0x305" },
    { 0xf306, "RSTV0910_P2_UNKNOWN_306", "P2 unknown register 0x306" },
    { 0xf307, "RSTV0910_P2_UNKNOWN_307", "P2 unknown register 0x307" },

    /* P2 Viterbi additional registers */
    { 0xf33a, "RSTV0910_P2_UNKNOWN_33A", "P2 unknown register 0x33a" },
    { 0xf33f, "RSTV0910_P2_UNKNOWN_33F", "P2 unknown register 0x33f" },
    { 0xf340, "RSTV0910_P2_UNKNOWN_340", "P2 unknown register 0x340" },
    { 0xf341, "RSTV0910_P2_UNKNOWN_341", "P2 unknown register 0x341" },
    { 0xf342, "RSTV0910_P2_UNKNOWN_342", "P2 unknown register 0x342" },
    { 0xf343, "RSTV0910_P2_UNKNOWN_343", "P2 unknown register 0x343" },
    { 0xf344, "RSTV0910_P2_UNKNOWN_344", "P2 unknown register 0x344" },
    { 0xf345, "RSTV0910_P2_UNKNOWN_345", "P2 unknown register 0x345" },

    /* P2 additional control registers */
    { 0xf360, "RSTV0910_P2_UNKNOWN_360", "P2 unknown register 0x360" },
    { 0xf361, "RSTV0910_P2_UNKNOWN_361", "P2 unknown register 0x361" },
    { 0xf362, "RSTV0910_P2_UNKNOWN_362", "P2 unknown register 0x362" },
    { 0xf363, "RSTV0910_P2_UNKNOWN_363", "P2 unknown register 0x363" },
    { 0xf364, "RSTV0910_P2_UNKNOWN_364", "P2 unknown register 0x364" },
    { 0xf365, "RSTV0910_P2_UNKNOWN_365", "P2 unknown register 0x365" },
    { 0xf366, "RSTV0910_P2_UNKNOWN_366", "P2 unknown register 0x366" },
    { 0xf367, "RSTV0910_P2_UNKNOWN_367", "P2 unknown register 0x367" },
    { 0xf368, "RSTV0910_P2_UNKNOWN_368", "P2 unknown register 0x368" },
    { 0xf369, "RSTV0910_P2_UNKNOWN_369", "P2 unknown register 0x369" },
    { 0xf36a, "RSTV0910_P2_UNKNOWN_36A", "P2 unknown register 0x36a" },
    { 0xf36b, "RSTV0910_P2_UNKNOWN_36B", "P2 unknown register 0x36b" },
    { 0xf36c, "RSTV0910_P2_UNKNOWN_36C", "P2 unknown register 0x36c" },
    { 0xf36d, "RSTV0910_P2_UNKNOWN_36D", "P2 unknown register 0x36d" },
    { 0xf36e, "RSTV0910_P2_UNKNOWN_36E", "P2 unknown register 0x36e" },
    { 0xf36f, "RSTV0910_P2_UNKNOWN_36F", "P2 unknown register 0x36f" },

    /* Additional high-address registers for complete coverage */
    { RSTV0910_TSGENERAL,     "RSTV0910_TSGENERAL",     "Transport stream general configuration" },
    { RSTV0910_RCSTATUS,      "RSTV0910_RCSTATUS",      "Reed-Solomon status" },
    { RSTV0910_RCSPEED,       "RSTV0910_RCSPEED",       "Reed-Solomon speed" },
    { RSTV0910_RCINSDEL1,     "RSTV0910_RCINSDEL1",     "Reed-Solomon insertion/deletion 1" },
    { RSTV0910_RCINSDEL0,     "RSTV0910_RCINSDEL0",     "Reed-Solomon insertion/deletion 0" },

    /* LDPC and BCH registers */
    { RSTV0910_CFGEXT,        "RSTV0910_CFGEXT",        "Configuration extension" },
    { RSTV0910_GENCFG,        "RSTV0910_GENCFG",        "General configuration" },
    { RSTV0910_LDPCERR1,      "RSTV0910_LDPCERR1",      "LDPC error count MSB" },
    { RSTV0910_LDPCERR0,      "RSTV0910_LDPCERR0",      "LDPC error count LSB" },
    { RSTV0910_BCHERR,        "RSTV0910_BCHERR",        "BCH error count" },

    /* P1 maximum extra iterations */
    { RSTV0910_P1_MAXEXTRAITER, "RSTV0910_P1_MAXEXTRAITER", "P1 maximum extra iterations" },

    /* P2 LDPC iteration registers (0xfab0-0xfaf3 range) */
    { 0xfab0, "RSTV0910_P2_NBITER_SF0",  "P2 LDPC iterations SF0" },
    { 0xfab1, "RSTV0910_P2_NBITER_SF1",  "P2 LDPC iterations SF1" },
    { 0xfab2, "RSTV0910_P2_NBITER_SF2",  "P2 LDPC iterations SF2" },
    { 0xfab3, "RSTV0910_P2_NBITER_SF3",  "P2 LDPC iterations SF3" },
    { 0xfab4, "RSTV0910_P2_NBITER_SF4",  "P2 LDPC iterations SF4" },
    { 0xfab5, "RSTV0910_P2_NBITER_SF5",  "P2 LDPC iterations SF5" },
    { 0xfab6, "RSTV0910_P2_NBITER_SF6",  "P2 LDPC iterations SF6" },
    { 0xfab7, "RSTV0910_P2_NBITER_SF7",  "P2 LDPC iterations SF7" },
    { 0xfab8, "RSTV0910_P2_NBITER_SF8",  "P2 LDPC iterations SF8" },
    { 0xfab9, "RSTV0910_P2_NBITER_SF9",  "P2 LDPC iterations SF9" },
    { 0xfaba, "RSTV0910_P2_NBITER_SF10", "P2 LDPC iterations SF10" },
    { 0xfabb, "RSTV0910_P2_NBITER_SF11", "P2 LDPC iterations SF11" },
    { 0xfabc, "RSTV0910_P2_NBITER_SF12", "P2 LDPC iterations SF12" },
    { 0xfabd, "RSTV0910_P2_NBITER_SF13", "P2 LDPC iterations SF13" },
    { 0xfabe, "RSTV0910_P2_NBITER_SF14", "P2 LDPC iterations SF14" },
    { 0xfabf, "RSTV0910_P2_NBITER_SF15", "P2 LDPC iterations SF15" },
    { 0xfac0, "RSTV0910_P2_NBITER_SF16", "P2 LDPC iterations SF16" },
    { 0xfac1, "RSTV0910_P2_NBITER_SF17", "P2 LDPC iterations SF17" },
    { 0xfac2, "RSTV0910_P2_NBITER_SF18", "P2 LDPC iterations SF18" },
    { 0xfac3, "RSTV0910_P2_NBITER_SF19", "P2 LDPC iterations SF19" },
    { 0xfac4, "RSTV0910_P2_NBITER_SF20", "P2 LDPC iterations SF20" },
    { 0xfac5, "RSTV0910_P2_NBITER_SF21", "P2 LDPC iterations SF21" },
    { 0xfac6, "RSTV0910_P2_NBITER_SF22", "P2 LDPC iterations SF22" },
    { 0xfac7, "RSTV0910_P2_NBITER_SF23", "P2 LDPC iterations SF23" },
    { 0xfac8, "RSTV0910_P2_NBITER_SF24", "P2 LDPC iterations SF24" },
    { 0xfac9, "RSTV0910_P2_NBITER_SF25", "P2 LDPC iterations SF25" },
    { 0xfaca, "RSTV0910_P2_NBITER_SF26", "P2 LDPC iterations SF26" },
    { 0xfacb, "RSTV0910_P2_NBITER_SF27", "P2 LDPC iterations SF27" },

    /* P1 LDPC iteration registers (0xfb00-0xfb53 range) */
    { 0xfb00, "RSTV0910_P1_NBITER_SF0",  "P1 LDPC iterations SF0" },
    { 0xfb01, "RSTV0910_P1_NBITER_SF1",  "P1 LDPC iterations SF1" },
    { 0xfb02, "RSTV0910_P1_NBITER_SF2",  "P1 LDPC iterations SF2" },
    { 0xfb03, "RSTV0910_P1_NBITER_SF3",  "P1 LDPC iterations SF3" },
    { 0xfb04, "RSTV0910_P1_NBITER_SF4",  "P1 LDPC iterations SF4" },
    { 0xfb05, "RSTV0910_P1_NBITER_SF5",  "P1 LDPC iterations SF5" },
    { 0xfb06, "RSTV0910_P1_NBITER_SF6",  "P1 LDPC iterations SF6" },
    { 0xfb07, "RSTV0910_P1_NBITER_SF7",  "P1 LDPC iterations SF7" },
    { 0xfb08, "RSTV0910_P1_NBITER_SF8",  "P1 LDPC iterations SF8" },
    { 0xfb09, "RSTV0910_P1_NBITER_SF9",  "P1 LDPC iterations SF9" },
    { 0xfb0a, "RSTV0910_P1_NBITER_SF10", "P1 LDPC iterations SF10" },
    { 0xfb0b, "RSTV0910_P1_NBITER_SF11", "P1 LDPC iterations SF11" },
    { 0xfb0c, "RSTV0910_P1_NBITER_SF12", "P1 LDPC iterations SF12" },
    { 0xfb0d, "RSTV0910_P1_NBITER_SF13", "P1 LDPC iterations SF13" },
    { 0xfb0e, "RSTV0910_P1_NBITER_SF14", "P1 LDPC iterations SF14" },
    { 0xfb0f, "RSTV0910_P1_NBITER_SF15", "P1 LDPC iterations SF15" },
    { 0xfb10, "RSTV0910_P1_NBITER_SF16", "P1 LDPC iterations SF16" },
    { 0xfb11, "RSTV0910_P1_NBITER_SF17", "P1 LDPC iterations SF17" },
    { 0xfb12, "RSTV0910_P1_NBITER_SF18", "P1 LDPC iterations SF18" },
    { 0xfb13, "RSTV0910_P1_NBITER_SF19", "P1 LDPC iterations SF19" },
    { 0xfb14, "RSTV0910_P1_NBITER_SF20", "P1 LDPC iterations SF20" },
    { 0xfb15, "RSTV0910_P1_NBITER_SF21", "P1 LDPC iterations SF21" },
    { 0xfb16, "RSTV0910_P1_NBITER_SF22", "P1 LDPC iterations SF22" },
    { 0xfb17, "RSTV0910_P1_NBITER_SF23", "P1 LDPC iterations SF23" },
    { 0xfb18, "RSTV0910_P1_NBITER_SF24", "P1 LDPC iterations SF24" },
    { 0xfb19, "RSTV0910_P1_NBITER_SF25", "P1 LDPC iterations SF25" },
    { 0xfb1a, "RSTV0910_P1_NBITER_SF26", "P1 LDPC iterations SF26" },
    { 0xfb1b, "RSTV0910_P1_NBITER_SF27", "P1 LDPC iterations SF27" },

    /* High-address test registers */
    { RSTV0910_TSTOUT,        "RSTV0910_TSTOUT",        "Test output" },
    { RSTV0910_TSTIN,         "RSTV0910_TSTIN",         "Test input" },
    { RSTV0910_P2_TSTDMD,     "RSTV0910_P2_TSTDMD",     "P2 test demodulator" },
    { RSTV0910_P2_TCTL1,      "RSTV0910_P2_TCTL1",      "P2 test control 1" },
    { RSTV0910_P2_TCTL4,      "RSTV0910_P2_TCTL4",      "P2 test control 4" },
    { RSTV0910_P2_TPKTDELIN,  "RSTV0910_P2_TPKTDELIN",  "P2 test packet delineator" },
    { RSTV0910_P1_TSTDMD,     "RSTV0910_P1_TSTDMD",     "P1 test demodulator" },
    { RSTV0910_P1_TCTL1,      "RSTV0910_P1_TCTL1",      "P1 test control 1" },
    { RSTV0910_P1_TCTL4,      "RSTV0910_P1_TCTL4",      "P1 test control 4" },
    { RSTV0910_P1_TPKTDELIN,  "RSTV0910_P1_TPKTDELIN",  "P1 test packet delineator" },
    { RSTV0910_TSTTSRS,       "RSTV0910_TSTTSRS",       "Test transport stream Reed-Solomon" },

    /* Final test register at highest address */
    { RSTV0910_TSTRES0,       "RSTV0910_TSTRES0",       "Test reset register 0" },

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
