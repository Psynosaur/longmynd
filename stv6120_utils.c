/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: stv6120_utils.c                                                             */
/*    - an implementation of the Serit NIM controlling software for the MiniTiouner Hardware          */
/*    - abstracting out the tuner (STV6120) register read and write routines                          */
/* H.L. Lomond 2018,2019                                                                              */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- INCLUDES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

#include <cstddef>
#include "nim.h"
#include "register_logging.h"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv6120_read_reg(uint8_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* passes the register read through to the underlying register reading routines                       */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;

    /* Perform the actual register read */
    err = nim_read_tuner(reg, val);

    /* Log the register read operation if successful */
    if (err == 0 && val != NULL) {
        LOG_STV6120_READ(reg, *val, register_logging_get_context());
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv6120_write_reg(uint8_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* passes the register write through to the underlying register writing routines                      */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;

    /* Log the register write operation */
    LOG_STV6120_WRITE(reg, val, register_logging_get_context());

    /* Perform the actual register write */
    err = nim_write_tuner(reg, val);

    return err;
}
