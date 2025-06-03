/*
    longmynd - Essential STV0910 Register Initialization
    
    Copyright 2019 Heather Lomond
    Copyright 2024 Longmynd Contributors
    
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

#ifndef STV0910_ESSENTIAL_REGS_H
#define STV0910_ESSENTIAL_REGS_H

#include "stv0910_regs.h"

typedef struct{
    uint16_t reg;
    uint8_t  val;
}STRegEssential;

/* Essential registers for fast initialization - based on open_tuner approach */
#define STV0910_ESSENTIAL_REGS 252

static STRegEssential STV0910EssentialRegs[STV0910_ESSENTIAL_REGS] = {
    /* Core system registers */
    { RSTV0910_DACR1,             0x00 }, /* DAC 1 freq=0, mode=0 */
    { RSTV0910_DACR2,             0x00 }, /* DAC 2 freq=0, mode=0 */
    { RSTV0910_PADCFG,            0x05 }, /* AGCRF_1 inverted, Push-pull, AGCRF_2 same*/
    { RSTV0910_OUTCFG2,           0x00 }, /* Transport stream signals not inverted */
    { RSTV0910_OUTCFG,            0x00 }, /* TS pins push-pull */
    
    /* IRQ control */
    { RSTV0910_IRQSTATUS3,        0x00 }, /* reset all pending IRQs */
    { RSTV0910_IRQSTATUS2,        0x00 }, /* reset all pending IRQs */
    { RSTV0910_IRQSTATUS1,        0x00 }, /* reset all pending IRQs */
    { RSTV0910_IRQSTATUS0,        0x00 }, /* reset all pending IRQs */
    { RSTV0910_IRQMASK3,          0x00 }, /* disable all IRQs */
    { RSTV0910_IRQMASK2,          0x00 }, /* disable all IRQs*/
    { RSTV0910_IRQMASK1,          0x00 }, /* disable all IRQs*/
    { RSTV0910_IRQMASK0,          0x00 }, /* disable all IRQs*/
    
    /* I2C and repeater config */
    { RSTV0910_I2CCFG,            0x88 }, /* i2c fastmode enabled, auto inc by 1 */
    { RSTV0910_P1_I2CRPT,         0x38 }, /* P1 repeater off, repeater speed, manual stop */
    { RSTV0910_P2_I2CRPT,         0x38 }, /* P2 repeater off, repeater speed, manual stop */
    
    /* GPIO configuration - complete set like open_tuner */
    { RSTV0910_GPIO0CFG,          0x82 }, /* GPIO 0 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO1CFG,          0x82 }, /* GPIO 1 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO2CFG,          0x82 }, /* GPIO 2 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO3CFG,          0x82 }, /* GPIO 3 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO4CFG,          0x82 }, /* GPIO 4 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO5CFG,          0x82 }, /* GPIO 5 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO6CFG,          0x82 }, /* GPIO 6 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO7CFG,          0x82 }, /* GPIO 7 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO8CFG,          0x82 }, /* GPIO 8 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO9CFG,          0x82 }, /* GPIO 9 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO10CFG,         0x82 }, /* GPIO 10 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO11CFG,         0x82 }, /* GPIO 11 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO12CFG,         0x82 }, /* GPIO 12 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO13CFG,         0x82 }, /* GPIO 13 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO14CFG,         0x82 }, /* GPIO 14 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO15CFG,         0x82 }, /* GPIO 15 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO16CFG,         0x82 }, /* GPIO 16 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO17CFG,         0x82 }, /* GPIO 17 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO18CFG,         0x82 }, /* GPIO 18 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO19CFG,         0x82 }, /* GPIO 19 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO20CFG,         0x82 }, /* GPIO 20 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO21CFG,         0x82 }, /* GPIO 21 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO22CFG,         0x82 }, /* GPIO 22 push-pull, force to 1, non inverting */
    
    /* Status configuration */
    { RSTV0910_STRSTATUS1,        0x60 }, /* gpio2=demod 2 detect iq inversion, gpio1=demod 1 detect iq inversion */
    { RSTV0910_STRSTATUS2,        0x71 }, /* gpio4=demod 2 lock, gpio3=demod 1 lock */
    { RSTV0910_STRSTATUS3,        0x82 }, /* gpio6=demod 2 failed flag, gpio5=demod 1 failed flag */
    
    /* FSK Registers - from open_tuner */
    { RSTV0910_FSKTFC2,           0x8c }, /* FSK is not being used so can ignore these */
    { RSTV0910_FSKTFC1,           0x45 },
    { RSTV0910_FSKTFC0,           0xc9 },
    { RSTV0910_FSKTDELTAF1,       0x01 },
    { RSTV0910_FSKTDELTAF0,       0x37 },
    { RSTV0910_FSKTCTRL,          0x08 }, /* modulator on when FSKTX_EN=1 */
    { RSTV0910_FSKRFC2,           0x10 },
    { RSTV0910_FSKRFC1,           0x45 },
    { RSTV0910_FSKRFC0,           0xc9 },
    { RSTV0910_FSKRK1,            0x38 },
    { RSTV0910_FSKRK2,            0x71 },
    { RSTV0910_FSKRAGCR,          0x28 },
    { RSTV0910_FSKRALPHA,         0x13 },
    { RSTV0910_FSKRPLTH1,         0x90 },
    { RSTV0910_FSKRPLTH0,         0xbe },
    { RSTV0910_FSKRSTEPP,         0x58 },
    { RSTV0910_FSKRSTEPM,         0x6f },
    { RSTV0910_FSKRDTH1,          0x00 },
    { RSTV0910_FSKRDTH0,          0xe9 },
    { RSTV0910_FSKRLOSS,          0x4d },

    /* Clock and power registers */
    { RSTV0910_NCOARSE,           0x39 }, /* charge pump (CP)=7, IDF=1 */
    { RSTV0910_NCOARSE1,          0x12 }, /* N_DIV = 0x12 */
    { RSTV0910_NCOARSE2,          0x04 }, /* ODF=0x4 f_ana = 135MHz */
    { RSTV0910_SYNTCTRL,          0xc2 }, /* stop all clocks except i2c, bypass pll, pll active, osc pad enabled */
    { RSTV0910_FILTCTRL,          0x01 }, /* filter FSK clock not inverted, if PLL bypassed, clock from CLK1 only */
    { RSTV0910_PLLSTAT,           0x07 }, /* LSB is PLL lock */
    { RSTV0910_STOPCLK1,          0x00 }, /* neither ADC interface clocks are inverted */
    { RSTV0910_STOPCLK2,          0x00 }, /* no clocks stopped */
    { RSTV0910_PREGCTL,           0x00 }, /* DCDC 3v3 to 2v5 on */
    { RSTV0910_TSTTNR0,           0x00 }, /* FSK analog cell off */
    { RSTV0910_TSTTNR1,           0x46 }, /* ADC1 power on */
    { RSTV0910_TSTTNR2,           0x4b }, /* I2C DiSEqC ADC 1 power off, diseqc clock div = 0xb */
    { RSTV0910_TSTTNR3,           0x46 }, /* ADC2 power on */
    
    /* P2 (Top) Demodulator registers - expanded from open_tuner */
    { RSTV0910_P2_IQCONST,        0x00 },
    { RSTV0910_P2_NOSCFG,         0x34 }, /* 0x20 + 0x14 */
    { RSTV0910_P2_ISYMB,          0x0e },
    { RSTV0910_P2_QSYMB,          0xfc },
    { RSTV0910_P2_AGC1CFG,        0x54 },
    { RSTV0910_P2_AGC1CN,         0x99 },
    { RSTV0910_P2_AGC1REF,        0x58 },
    { RSTV0910_P2_IDCCOMP,        0x0a },
    { RSTV0910_P2_QDCCOMP,        0x09 },
    { RSTV0910_P2_POWERI,         0x09 },
    { RSTV0910_P2_POWERQ,         0x0a },
    { RSTV0910_P2_AGC1AMM,        0xfd },
    { RSTV0910_P2_AGC1QUAD,       0xfd },
    { RSTV0910_P2_AGCIQIN1,       0x00 },
    { RSTV0910_P2_AGCIQIN0,       0x00 },
    { RSTV0910_P2_DEMOD,          0x00 }, /* auto rolloff, auto spectral inversion, auto DVBs1 rolloff, 35% */
    { RSTV0910_P2_DMDMODCOD,      0x10 }, /* modcod auto */
    { RSTV0910_P2_DSTATUS,        0x10 },
    { RSTV0910_P2_DSTATUS2,       0x80 },
    { RSTV0910_P2_DMDCFGMD,       0xc9 },
    { RSTV0910_P2_DMDCFG2,        0x3b }, /* parallel search (DVBS/S2, infinite relock tries */
    { RSTV0910_P2_DMDISTATE,      0x5c },
    { RSTV0910_P2_DMDT0M,         0x40 },
    { RSTV0910_P2_DMDSTATE,       0x1c },
    { RSTV0910_P2_DMDFLYW,        0x00 },
    { RSTV0910_P2_DSTATUS3,       0x80 },
    { RSTV0910_P2_DMDCFG3,        0x08 }, /* if FIFO is full lose data */
    { RSTV0910_P2_DMDCFG4,        0x04 }, /* soft increment of tuner */
    { RSTV0910_P2_CORRELMANT,     0x78 },
    { RSTV0910_P2_CORRELABS,      0x8c },
    { RSTV0910_P2_CORRELEXP,      0xaa },
    { RSTV0910_P2_PLHMODCOD,      0x10 },
    { RSTV0910_P2_DMDREG,         0x01 },
    { RSTV0910_P2_AGCNADJ,        0x00 },
    { RSTV0910_P2_AGCKS,          0x00 },
    { RSTV0910_P2_AGCKQ,          0x00 },
    { RSTV0910_P2_AGCK8,          0x00 },
    { RSTV0910_P2_AGCK16,         0x00 },
    { RSTV0910_P2_AGCK32,         0x00 },
    { RSTV0910_P2_AGC2O,          0x5b },
    { RSTV0910_P2_AGC2REF,        0x38 },
    { RSTV0910_P2_AGC1ADJ,        0x58 },
    { RSTV0910_P2_AGCRSADJ,       0x38 },
    { RSTV0910_P2_AGCRQADJ,       0x38 },
    { RSTV0910_P2_AGCR8ADJ,       0x38 },
    { RSTV0910_P2_AGCR1ADJ,       0x38 },
    { RSTV0910_P2_AGCR2ADJ,       0x38 },
    { RSTV0910_P2_AGCR3ADJ,       0x47 },
    { RSTV0910_P2_AGCREFADJ,      0x38 },
    { RSTV0910_P2_AGC2I1,         0x1c },
    { RSTV0910_P2_AGC2I0,         0x74 },
    
    /* P1 (Bottom) Demodulator essential registers */
    { RSTV0910_P1_IQCONST,        0x00 },
    { RSTV0910_P1_NOSCFG,         0x34 }, /* 0x20 + 0x14 */
    { RSTV0910_P1_ISYMB,          0x0e },
    { RSTV0910_P1_QSYMB,          0xf7 },
    { RSTV0910_P1_AGC1CFG,        0x54 },
    { RSTV0910_P1_AGC1CN,         0x99 },
    { RSTV0910_P1_AGC1REF,        0x58 },
    { RSTV0910_P1_DEMOD,          0x00 }, /* auto rolloff, auto spectral inversion, auto DVBs1 rolloff, 35% */
    { RSTV0910_P1_DMDMODCOD,      0x10 }, /* modcod auto */
    { RSTV0910_P1_DMDCFGMD,       0xc9 },
    { RSTV0910_P1_DMDCFG2,        0x3b }, /* parallel search (DVBS/S2, infinite relock tries */
    { RSTV0910_P1_DMDISTATE,      0x5c },   
    { RSTV0910_P1_DMDT0M,         0x40 },
    { RSTV0910_P1_DMDCFG3,        0x08 }, /* if FIFO is full lose data */
    { RSTV0910_P1_DMDCFG4,        0x04 }, /* soft increment of tuner */
    
    /* P2 Carrier loop registers - expanded from open_tuner */
    { RSTV0910_P2_CARCFG,         0x46 }, /* de_rotator is active, algorithm=citroes 2 */
    { RSTV0910_P2_ACLC,           0x2b }, /* alpha=2:0xb mostly fastest DVBS use */
    { RSTV0910_P2_BCLC,           0x1a }, /* beta=1:0xa DVBS use */
    { RSTV0910_P2_ACLCS2,         0x00 },
    { RSTV0910_P2_BCLCS2,         0x00 },
    { RSTV0910_P2_CARFREQ,        0x79 },
    { RSTV0910_P2_CARHDR,         0x1c },
    { RSTV0910_P2_LDT,            0xd0 },
    { RSTV0910_P2_LDT2,           0xb8 },
    { RSTV0910_P2_CFRICFG,        0xf9 },
    { RSTV0910_P2_CFRUP1,         0x0e },
    { RSTV0910_P2_CFRUP0,         0x69 },
    { RSTV0910_P2_CFRIBASE1,      0x01 },
    { RSTV0910_P2_CFRIBASE0,      0xf5 },
    { RSTV0910_P2_CFRLOW1,        0xf1 },
    { RSTV0910_P2_CFRLOW0,        0x97 },
    { RSTV0910_P2_CFRINIT1,       0x01 },
    { RSTV0910_P2_CFRINIT0,       0xf5 },
    { RSTV0910_P2_CFRINC1,        0x03 },
    { RSTV0910_P2_CFRINC0,        0x8e },
    { RSTV0910_P2_CFR2,           0x01 },
    { RSTV0910_P2_CFR1,           0xf5 },
    { RSTV0910_P2_CFR0,           0x00 },
    { RSTV0910_P2_LDI,            0xb6 },

    /* P2 Timing loop registers */
    { RSTV0910_P2_TMGCFG,         0xd3 },
    { RSTV0910_P2_RTC,            0x68 },
    { RSTV0910_P2_RTCS2,          0x68 },
    { RSTV0910_P2_TMGTHRISE,      0x1e },
    { RSTV0910_P2_TMGTHFALL,      0x08 },
    { RSTV0910_P2_SFRUPRATIO,     0x20 },
    { RSTV0910_P2_SFRLOWRATIO,    0xd0 },
    { RSTV0910_P2_KTTMG,          0xa0 },
    { RSTV0910_P2_KREFTMG,        0x80 },
    { RSTV0910_P2_SFRSTEP,        0x88 },
    { RSTV0910_P2_TMGCFG2,        0x80 },
    { RSTV0910_P2_TMGCFG3,        0x06 },
    { RSTV0910_P2_SFRINIT1,       0x38 },
    { RSTV0910_P2_SFRINIT0,       0xe3 },
    { RSTV0910_P2_SFRUP1,         0x3f },
    { RSTV0910_P2_SFRUP0,         0xff },
    { RSTV0910_P2_SFRLOW1,        0x2e },
    { RSTV0910_P2_SFRLOW0,        0x39 },
    { RSTV0910_P2_SFR3,           0x38 },
    { RSTV0910_P2_SFR2,           0xe3 },
    { RSTV0910_P2_SFR1,           0x00 },
    { RSTV0910_P2_SFR0,           0x00 },
    { RSTV0910_P2_TMGREG2,        0x00 },
    { RSTV0910_P2_TMGREG1,        0x00 },
    { RSTV0910_P2_TMGREG0,        0x00 },
    { RSTV0910_P2_TMGLOCK1,       0xe4 },
    { RSTV0910_P2_TMGLOCK0,       0x00 },
    { RSTV0910_P2_TMGOBS,         0x10 },

    /* P1 Carrier loop registers */
    { RSTV0910_P1_CARCFG,         0x46 }, /* de_rotator is active, algorithm=citroes 2 */
    { RSTV0910_P1_ACLC,           0x2b }, /* alpha=2:0xb mostly fastest DVBS use */
    { RSTV0910_P1_BCLC,           0x1a }, /* beta=1:0xa DVBS use */
    { RSTV0910_P1_ACLCS2,         0x00 },
    { RSTV0910_P1_BCLCS2,         0x00 },
    { RSTV0910_P1_CARFREQ,        0x79 },
    { RSTV0910_P1_CARHDR,         0x1c },
    { RSTV0910_P1_LDT,            0xd0 },
    { RSTV0910_P1_LDT2,           0xb8 },
    { RSTV0910_P1_CFRICFG,        0xf9 },
    { RSTV0910_P1_CFRUP1,         0x0e },
    { RSTV0910_P1_CFRUP0,         0x69 },
    { RSTV0910_P1_CFRIBASE1,      0x01 },
    { RSTV0910_P1_CFRIBASE0,      0xf5 },
    { RSTV0910_P1_CFRLOW1,        0xf1 },
    { RSTV0910_P1_CFRLOW0,        0x97 },
    { RSTV0910_P1_CFRINIT1,       0x01 },
    { RSTV0910_P1_CFRINIT0,       0xf5 },
    { RSTV0910_P1_CFRINC1,        0x03 },
    { RSTV0910_P1_CFRINC0,        0x8e },
    { RSTV0910_P1_CFR2,           0x01 },
    { RSTV0910_P1_CFR1,           0xf5 },
    { RSTV0910_P1_CFR0,           0x00 },
    { RSTV0910_P1_LDI,            0xb6 },
    
    /* P1 Timing loop registers */
    { RSTV0910_P1_TMGCFG,         0xd3 },
    { RSTV0910_P1_RTC,            0x68 },
    { RSTV0910_P1_RTCS2,          0x68 },
    { RSTV0910_P1_TMGTHRISE,      0x1e },
    { RSTV0910_P1_TMGTHFALL,      0x08 },
    { RSTV0910_P1_SFRUPRATIO,     0x20 },
    { RSTV0910_P1_SFRLOWRATIO,    0xd0 },
    { RSTV0910_P1_KTTMG,          0xa0 },
    { RSTV0910_P1_KREFTMG,        0x80 },
    { RSTV0910_P1_SFRSTEP,        0x88 },
    { RSTV0910_P1_TMGCFG2,        0x80 },
    { RSTV0910_P1_TMGCFG3,        0x06 },
    { RSTV0910_P1_SFRINIT1,       0x38 },
    { RSTV0910_P1_SFRINIT0,       0xe3 },
    { RSTV0910_P1_SFRUP1,         0x3f },
    { RSTV0910_P1_SFRUP0,         0xff },
    { RSTV0910_P1_SFRLOW1,        0x2e },
    { RSTV0910_P1_SFRLOW0,        0x39 },
    { RSTV0910_P1_SFR3,           0x38 },
    { RSTV0910_P1_SFR2,           0xe3 },
    { RSTV0910_P1_SFR1,           0x00 },
    { RSTV0910_P1_SFR0,           0x00 },
    { RSTV0910_P1_TMGREG2,        0x00 },
    { RSTV0910_P1_TMGREG1,        0x00 },
    { RSTV0910_P1_TMGREG0,        0x00 },
    
    /* Essential TS configuration */
    { RSTV0910_P2_TSSTATEM,       0xf0 }, /* deinterleaver on, reed-solomon on, descrambler on, TS enabled */
    { RSTV0910_P2_TSSTATEL,       0x12 },
    { RSTV0910_P2_TSCFGH,         0x80 }, /* TS config high */
    { RSTV0910_P1_TSSTATEM,       0xf0 }, /* deinterleaver on, reed-solomon on, descrambler on, TS enabled */
    { RSTV0910_P1_TSSTATEL,       0x12 },
    { RSTV0910_P1_TSCFGH,         0x80 }, /* TS config high */
    
    /* DiSEqC essential registers */
    { RSTV0910_P1_DISTXCFG,       0x02 }, /* DiSEqC TX config */
    { RSTV0910_P2_DISTXCFG,       0x02 }, /* DiSEqC TX config */
    
    /* General TS configuration */
    { RSTV0910_TSGENERAL,         0x00 }, /* enable output of second line in parallel line */
    
    /* Final test register */
    { RSTV0910_TSTTSRS,           0x00 }  /* Test transport stream Reed-Solomon */
};

#endif
