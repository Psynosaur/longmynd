/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: nim.c                                                                       */
/*    - an implementation of the Serit NIM controlling software for the MiniTiouner Hardware          */
/*    - handlers for the nim module itself                                                            */
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

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- INCLUDES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "nim.h"
#include "ftdi.h"
#include "ftdi_dual.h"
#include "errors.h"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

/* The tuner and LNAs are accessed by turning on the I2C bus repeater in the demodulator
   this reduces the noise on the tuner I2C lines as they are inactive when the repeater
   is turned off. We need to keep track of this when we access the NIM  */
bool repeater_on;

/* Global flag to track dual tuner mode for automatic function selection */
static bool dual_tuner_mode = false;
static uint8_t primary_tuner_id = TUNER_1_ID;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
void nim_set_dual_tuner_mode(bool enabled, uint8_t primary_tuner) {
/* -------------------------------------------------------------------------------------------------- */
/* Set dual tuner mode for automatic function selection                                              */
/* enabled: true to enable dual tuner mode, false for single tuner mode                             */
/* primary_tuner: TUNER_1_ID or TUNER_2_ID - which tuner controls the shared demodulator            */
/* -------------------------------------------------------------------------------------------------- */
    dual_tuner_mode = enabled;
    primary_tuner_id = primary_tuner;
    printf("Flow: NIM dual tuner mode %s, primary tuner %d\n",
           enabled ? "ENABLED" : "DISABLED", primary_tuner);
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_demod(uint16_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads a demodulator register and takes care of the i2c bus repeater                                */
/*    reg: which demod register to read                                                               */
/*    val: where to put the result                                                                    */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    /* In dual tuner mode, use tuner-aware function */
    if (dual_tuner_mode) {
        return nim_read_demod_tuner(primary_tuner_id, reg, val);
    }

    /* Single tuner mode - use original implementation */
    /* if we are not using the tuner or lna any more then we can turn off
       the repeater to reduce noise
       this is bit 7 of the Px_I2CRPT register. Other bits define I2C speed etc. */
    if (repeater_on) {
        repeater_on=false;
        err=nim_write_demod(0xf12a,0x38);
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_reg16(NIM_DEMOD_ADDR,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: demod read 0x%.4x\n",reg);

    /* note we don't turn the repeater off as there might be other r/w to tuner/LNAs */

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_demod(uint16_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to a demodulator register and takes care of the i2c bus repeater                            */
/*    reg: which demod register to write to                                                           */
/*    val: what to write to it                                                                        */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    /* In dual tuner mode, use tuner-aware function */
    if (dual_tuner_mode) {
        return nim_write_demod_tuner(primary_tuner_id, reg, val);
    }

    /* Single tuner mode - use original implementation */
    if (repeater_on && reg != 0xf12a) {
        repeater_on=false;
        err=ftdi_i2c_write_reg16(NIM_DEMOD_ADDR, 0xf12a, 0x38);
    }
    if (err==ERROR_NONE) err=ftdi_i2c_write_reg16(NIM_DEMOD_ADDR,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: demod write 0x%.4x, 0x%.2x\n",reg,val);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_lna(uint8_t lna_addr, uint8_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads from the specified lna taking care of the i2c bus repeater                                   */
/*  lna_addr: i2c address of the lna to access                                                        */
/*       reg: which lna register to read                                                              */
/*       val: where to put the result                                                                 */
/*    return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_reg8(lna_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: lna read 0x%.2x, 0x%.2x\n",lna_addr,reg);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_lna(uint8_t lna_addr, uint8_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to the specified lna taking care of the i2c bus repeater                                    */
/*  lna_addr: i2c address of the lna to access                                                        */
/*       reg: which lna register to write to                                                          */
/*       val: what to write to it                                                                     */
/*    return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_write_reg8(lna_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: lna write 0x%.2x, 0x%.2x,0x%.2x\n",lna_addr,reg,val);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_tuner(uint8_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads from the stv0910 (tuner) taking care of the i2c bus repeater                                 */
/*    reg: which tuner register to read from                                                          */
/*    val: where to put the result                                                                    */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_reg8(NIM_TUNER_ADDR,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: tuner read 0x%.2x\n",reg);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_tuner(uint8_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to the stv0910 (tuner) taking care of the i2c bus repeater                                  */
/*    reg: which tuner register to write to                                                           */
/*    val: what to write to it                                                                        */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_write_reg8(NIM_TUNER_ADDR,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: tuner write %i,%i\n",reg,val);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_demod_tuner(uint8_t tuner_id, uint16_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads from the demodulator using tuner-aware I2C functions                                        */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/*      reg: which demod register to read from                                                       */
/*      val: where to put the result                                                                 */
/*   return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    /* if we are not using the tuner or lna any more then we can turn off
       the repeater to reduce noise
       this is bit 7 of the Px_I2CRPT register. Other bits define I2C speed etc. */
    if (repeater_on) {
        repeater_on=false;
        err=nim_write_demod_tuner(tuner_id, 0xf12a, 0x38);
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_reg16_tuner(tuner_id, NIM_DEMOD_ADDR, reg, val);
    if (err!=ERROR_NONE) printf("ERROR: demod read tuner %d 0x%.4x\n", tuner_id, reg);

    /* note we don't turn the repeater off as there might be other r/w to tuner/LNAs */

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_demod_tuner(uint8_t tuner_id, uint16_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to the demodulator using tuner-aware I2C functions                                         */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/*      reg: which demod register to write to                                                        */
/*      val: what to write to it                                                                     */
/*   return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (repeater_on && reg != 0xf12a) {
        repeater_on=false;
        err=ftdi_i2c_write_reg16_tuner(tuner_id, NIM_DEMOD_ADDR, 0xf12a, 0x38);
    }
    if (err==ERROR_NONE) err=ftdi_i2c_write_reg16_tuner(tuner_id, NIM_DEMOD_ADDR, reg, val);
    if (err!=ERROR_NONE) printf("ERROR: demod write tuner %d 0x%.4x, 0x%.2x\n", tuner_id, reg, val);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_demod_bulk_start(uint8_t tuner_id) {
/* -------------------------------------------------------------------------------------------------- */
/* starts a bulk write session to optimize multiple consecutive writes to the same tuner             */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/*   return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (dual_tuner_mode) {
        /* Lock the FTDI context for the entire bulk operation */
        err = ftdi_bulk_write_start(tuner_id);
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_demod_bulk_end(void) {
/* -------------------------------------------------------------------------------------------------- */
/* ends a bulk write session and releases the FTDI context lock                                      */
/*   return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (dual_tuner_mode) {
        /* Release the FTDI context lock */
        err = ftdi_bulk_write_end();
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_demod_bulk(uint16_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to demodulator register during a bulk write session (no context switching overhead)       */
/*    reg: which demod register to write to                                                          */
/*    val: what to write to it                                                                       */
/* return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (dual_tuner_mode) {
        /* Use direct I2C write without context switching overhead */
        if (repeater_on && reg != 0xf12a) {
            repeater_on=false;
            err=ftdi_i2c_write_reg16(NIM_DEMOD_ADDR, 0xf12a, 0x38);
        }
        if (err==ERROR_NONE) err=ftdi_i2c_write_reg16(NIM_DEMOD_ADDR, reg, val);
        if (err!=ERROR_NONE) printf("ERROR: demod bulk write 0x%.4x, 0x%.2x\n", reg, val);
    } else {
        /* Single tuner mode - use standard function */
        err = nim_write_demod(reg, val);
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_init_tuner(uint8_t tuner_id) {
/* -------------------------------------------------------------------------------------------------- */
/* initialises the nim using tuner-aware I2C functions                                               */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/*   return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
    uint8_t val;

    printf("Flow: NIM init tuner %d\n", tuner_id);

    repeater_on = false;

    /* check we can read and write a register */
    /* check to see if we can write and readback to a demod register */
    if (err==ERROR_NONE) err=nim_write_demod_tuner(tuner_id, 0xf536, 0xaa); /* random reg with alternating bits */
    if (err==ERROR_NONE) err=nim_read_demod_tuner(tuner_id, 0xf536, &val);
    if (err==ERROR_NONE) {
        if (val!=0xaa) { /* check alternating bits ok */
            printf("ERROR: nim_init_tuner %d didn't r/w to the modulator\n", tuner_id);
            err=ERROR_NIM_INIT;
        }
    }

    /* we always want to start with the i2c repeater turned off */
    if (err!=ERROR_NONE) err=nim_write_demod_tuner(tuner_id, 0xf12a, 0x38);

    if (err!=ERROR_NONE) printf("ERROR: nim_init_tuner %d\n", tuner_id);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_init() {
/* -------------------------------------------------------------------------------------------------- */
/* initialises the nim (at the highest level)                                                         */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
    uint8_t val;

    printf("Flow: NIM init\n");

    repeater_on = false;

    /* check we can read and write a register */
    /* check to see if we can write and readback to a demod register */
    if (err==ERROR_NONE) err=nim_write_demod(0xf536,0xaa); /* random reg with alternating bits */
    if (err==ERROR_NONE) err=nim_read_demod(0xf536,&val);
    if (err==ERROR_NONE) {
        if (val!=0xaa) { /* check alternating bits ok */
            printf("ERROR: nim_init didn't r/w to the modulator\n");
            err=ERROR_NIM_INIT;
        }
    }

    /* we always want to start with the i2c repeater turned off */
    if (err!=ERROR_NONE) err=nim_write_demod(0xf12a,0x38);

    if (err!=ERROR_NONE) printf("ERROR: nim_init\n");

    return err;
}

