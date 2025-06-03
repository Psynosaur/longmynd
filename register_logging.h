/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: register_logging.h                                                          */
/* Copyright 2024 Heather Lomond                                                                      */
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

#ifndef REGISTER_LOGGING_H
#define REGISTER_LOGGING_H

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------------------------------- */
/* REGISTER LOGGING CONFIGURATION                                                                     */
/* -------------------------------------------------------------------------------------------------- */

/* Compile-time enable/disable register logging */
#ifndef ENABLE_REGISTER_LOGGING
#define ENABLE_REGISTER_LOGGING 1  /* Set to 0 to disable all register logging */
#endif

/* Rate limiting configuration for demod sequence logging (in milliseconds) */
#ifndef DEMOD_SEQUENCE_LOG_INTERVAL_MS
#define DEMOD_SEQUENCE_LOG_INTERVAL_MS 5000  /* Log demod sequence once every 5 seconds */
#endif

/* Runtime enable/disable register logging */
extern bool register_logging_enabled;

/* -------------------------------------------------------------------------------------------------- */
/* REGISTER LOGGING CONTEXT TYPES                                                                     */
/* -------------------------------------------------------------------------------------------------- */

typedef enum {
    REG_CONTEXT_INIT,
    REG_CONTEXT_FREQUENCY_TUNING,
    REG_CONTEXT_PLL_CONFIGURATION,
    REG_CONTEXT_PLL_CALIBRATION,
    REG_CONTEXT_SYMBOL_RATE_SETUP,
    REG_CONTEXT_DEMOD_CONTROL,
    REG_CONTEXT_STATE_TRANSITION,
    REG_CONTEXT_CARRIER_LOOP,
    REG_CONTEXT_TIMING_LOOP,
    REG_CONTEXT_TRANSPORT_STREAM,
    REG_CONTEXT_AGC_CONTROL,
    REG_CONTEXT_LNA_CONTROL,
    REG_CONTEXT_POWER_MANAGEMENT,
    REG_CONTEXT_SCAN_CONTROL,
    REG_CONTEXT_ERROR_CORRECTION,
    REG_CONTEXT_STATUS_READ,
    REG_CONTEXT_UNKNOWN
} register_context_t;

/* -------------------------------------------------------------------------------------------------- */
/* STV6120 REGISTER NAME LOOKUP STRUCTURE                                                            */
/* -------------------------------------------------------------------------------------------------- */

typedef struct {
    uint8_t address;
    const char *name;
    const char *description;
} stv6120_register_info_t;

/* -------------------------------------------------------------------------------------------------- */
/* STV0910 REGISTER NAME LOOKUP STRUCTURE                                                            */
/* -------------------------------------------------------------------------------------------------- */

typedef struct {
    uint16_t address;
    const char *name;
    const char *description;
} stv0910_register_info_t;

/* -------------------------------------------------------------------------------------------------- */
/* FUNCTION PROTOTYPES                                                                                */
/* -------------------------------------------------------------------------------------------------- */

/* Initialization and control */
void register_logging_init(void);
void register_logging_enable(bool enable);
bool register_logging_is_enabled(void);
void register_logging_set_demod_suppression_disabled(bool disabled);

/* Context management */
void register_logging_set_context(register_context_t context);
register_context_t register_logging_get_context(void);
const char *register_logging_context_to_string(register_context_t context);

/* STV6120 register logging */
void log_stv6120_register_write(uint8_t reg, uint8_t val, register_context_t context);
void log_stv6120_register_read(uint8_t reg, uint8_t val, register_context_t context);

/* STV0910 register logging */
void log_stv0910_register_write(uint16_t reg, uint8_t val, register_context_t context);
void log_stv0910_register_read(uint16_t reg, uint8_t val, register_context_t context);

/* Register name lookup functions */
const char *get_stv6120_register_name(uint8_t reg);
const char *get_stv6120_register_description(uint8_t reg);
const char *get_stv0910_register_name(uint16_t reg);
const char *get_stv0910_register_description(uint16_t reg);

/* Utility functions */
uint64_t get_timestamp_ms(void);
void log_register_sequence_start(const char *sequence_name);
void log_register_sequence_end(const char *sequence_name);

/* -------------------------------------------------------------------------------------------------- */
/* CONVENIENCE MACROS FOR REGISTER LOGGING                                                           */
/* -------------------------------------------------------------------------------------------------- */

#if ENABLE_REGISTER_LOGGING

#define LOG_STV6120_WRITE(reg, val, context) \
    do { \
        if (register_logging_is_enabled()) { \
            log_stv6120_register_write((reg), (val), (context)); \
        } \
    } while(0)

#define LOG_STV6120_READ(reg, val, context) \
    do { \
        if (register_logging_is_enabled()) { \
            log_stv6120_register_read((reg), (val), (context)); \
        } \
    } while(0)

#define LOG_STV0910_WRITE(reg, val, context) \
    do { \
        if (register_logging_is_enabled()) { \
            log_stv0910_register_write((reg), (val), (context)); \
        } \
    } while(0)

#define LOG_STV0910_READ(reg, val, context) \
    do { \
        if (register_logging_is_enabled()) { \
            log_stv0910_register_read((reg), (val), (context)); \
        } \
    } while(0)

#define LOG_SEQUENCE_START(name) \
    do { \
        if (register_logging_is_enabled()) { \
            log_register_sequence_start(name); \
        } \
    } while(0)

#define LOG_SEQUENCE_END(name) \
    do { \
        if (register_logging_is_enabled()) { \
            log_register_sequence_end(name); \
        } \
    } while(0)

#define SET_REG_CONTEXT(context) \
    do { \
        if (register_logging_is_enabled()) { \
            register_logging_set_context(context); \
        } \
    } while(0)

#else

/* Disabled versions - compile to nothing */
#define LOG_STV6120_WRITE(reg, val, context) do { } while(0)
#define LOG_STV6120_READ(reg, val, context) do { } while(0)
#define LOG_STV0910_WRITE(reg, val, context) do { } while(0)
#define LOG_STV0910_READ(reg, val, context) do { } while(0)
#define LOG_SEQUENCE_START(name) do { } while(0)
#define LOG_SEQUENCE_END(name) do { } while(0)
#define SET_REG_CONTEXT(context) do { } while(0)

#endif /* ENABLE_REGISTER_LOGGING */

#endif /* REGISTER_LOGGING_H */
