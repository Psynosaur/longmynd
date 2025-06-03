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
#define STV0910_ESSENTIAL_REGS 88

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
    
    /* GPIO configuration - minimal set */
    { RSTV0910_GPIO0CFG,          0x82 }, /* GPIO 0 push-pull, force to 1, non inverting */ 
    { RSTV0910_GPIO1CFG,          0x82 }, /* GPIO 1 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO2CFG,          0x82 }, /* GPIO 2 push-pull, force to 1, non inverting */
    { RSTV0910_GPIO3CFG,          0x82 }, /* GPIO 3 push-pull, force to 1, non inverting */
    
    /* Status configuration */
    { RSTV0910_STRSTATUS1,        0x60 }, /* gpio2=demod 2 detect iq inversion, gpio1=demod 1 detect iq inversion */
    { RSTV0910_STRSTATUS2,        0x71 }, /* gpio4=demod 2 lock, gpio3=demod 1 lock */
    { RSTV0910_STRSTATUS3,        0x82 }, /* gpio6=demod 2 failed flag, gpio5=demod 1 failed flag */
    
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
    
    /* P2 (Top) Demodulator essential registers */
    { RSTV0910_P2_IQCONST,        0x00 },
    { RSTV0910_P2_NOSCFG,         0x34 }, /* 0x20 + 0x14 */
    { RSTV0910_P2_ISYMB,          0x0e },
    { RSTV0910_P2_QSYMB,          0xfc },
    { RSTV0910_P2_AGC1CFG,        0x54 },
    { RSTV0910_P2_AGC1CN,         0x99 },
    { RSTV0910_P2_AGC1REF,        0x58 },
    { RSTV0910_P2_DEMOD,          0x00 }, /* auto rolloff, auto spectral inversion, auto DVBs1 rolloff, 35% */
    { RSTV0910_P2_DMDMODCOD,      0x10 }, /* modcod auto */
    { RSTV0910_P2_DMDCFGMD,       0xc9 },
    { RSTV0910_P2_DMDCFG2,        0x3b }, /* parallel search (DVBS/S2, infinite relock tries */
    { RSTV0910_P2_DMDISTATE,      0x5c },
    { RSTV0910_P2_DMDT0M,         0x40 },
    { RSTV0910_P2_DMDCFG3,        0x08 }, /* if FIFO is full lose data */
    { RSTV0910_P2_DMDCFG4,        0x04 }, /* soft increment of tuner */
    
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
    
    /* Essential carrier loop registers */
    { RSTV0910_P2_CARCFG,         0x46 }, /* de_rotator is active, algorithm=citroes 2 */
    { RSTV0910_P2_ACLC,           0x2b }, /* alpha=2:0xb mostly fastest DVBS use */
    { RSTV0910_P2_BCLC,           0x1a }, /* beta=1:0xa DVBS use */
    { RSTV0910_P1_CARCFG,         0x46 }, /* de_rotator is active, algorithm=citroes 2 */
    { RSTV0910_P1_ACLC,           0x2b }, /* alpha=2:0xb mostly fastest DVBS use */
    { RSTV0910_P1_BCLC,           0x1a }, /* beta=1:0xa DVBS use */
    
    /* Essential timing loop registers */
    { RSTV0910_P2_TMGCFG,         0xd3 }, /* lock indicator fastest, DVBS2 usr SR calculated on 2ns PLHeader */
    { RSTV0910_P2_RTC,            0x68 }, /* DVBS1 alpha=6 (under mid), beta=8 (mid) */
    { RSTV0910_P2_RTCS2,          0x68 }, /* DVBS2 alpha=6 (under mid), beta=8 (mid) */
    { RSTV0910_P1_TMGCFG,         0xd3 }, /* lock indicator fastest, DVBS2 usr SR calculated on 2ns PLHeader */
    { RSTV0910_P1_RTC,            0x68 }, /* DVBS1 alpha=6 (under mid), beta=8 (mid) */
    { RSTV0910_P1_RTCS2,          0x68 }, /* DVBS2 alpha=6 (under mid), beta=8 (mid) */
    
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
