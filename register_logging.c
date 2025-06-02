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
    { RSTV0910_OUTCFG,        "RSTV0910_OUTCFG",        "Output configuration" },
    { RSTV0910_OUTCFG2,       "RSTV0910_OUTCFG2",       "Output configuration 2" },
    
    /* P2 (TOP) Demodulator registers */
    { RSTV0910_P2_DMDISTATE,  "RSTV0910_P2_DMDISTATE",  "P2 demodulator state control" },
    { RSTV0910_P2_SFRINIT1,   "RSTV0910_P2_SFRINIT1",   "P2 symbol rate init MSB" },
    { RSTV0910_P2_SFRINIT0,   "RSTV0910_P2_SFRINIT0",   "P2 symbol rate init LSB" },
    { RSTV0910_P2_CFRUP1,     "RSTV0910_P2_CFRUP1",     "P2 carrier frequency upper limit MSB" },
    { RSTV0910_P2_CFRUP0,     "RSTV0910_P2_CFRUP0",     "P2 carrier frequency upper limit LSB" },
    { RSTV0910_P2_CFRLOW1,    "RSTV0910_P2_CFRLOW1",    "P2 carrier frequency lower limit MSB" },
    { RSTV0910_P2_CFRLOW0,    "RSTV0910_P2_CFRLOW0",    "P2 carrier frequency lower limit LSB" },
    { RSTV0910_P2_TSCFGH,     "RSTV0910_P2_TSCFGH",     "P2 transport stream config high" },
    { RSTV0910_P2_TSCFGM,     "RSTV0910_P2_TSCFGM",     "P2 transport stream config medium" },
    { RSTV0910_P2_TSCFGL,     "RSTV0910_P2_TSCFGL",     "P2 transport stream config low" },
    
    /* P1 (BOTTOM) Demodulator registers */
    { RSTV0910_P1_DMDISTATE,  "RSTV0910_P1_DMDISTATE",  "P1 demodulator state control" },
    { RSTV0910_P1_SFRINIT1,   "RSTV0910_P1_SFRINIT1",   "P1 symbol rate init MSB" },
    { RSTV0910_P1_SFRINIT0,   "RSTV0910_P1_SFRINIT0",   "P1 symbol rate init LSB" },
    { RSTV0910_P1_CFRUP1,     "RSTV0910_P1_CFRUP1",     "P1 carrier frequency upper limit MSB" },
    { RSTV0910_P1_CFRUP0,     "RSTV0910_P1_CFRUP0",     "P1 carrier frequency upper limit LSB" },
    { RSTV0910_P1_CFRLOW1,    "RSTV0910_P1_CFRLOW1",    "P1 carrier frequency lower limit MSB" },
    { RSTV0910_P1_CFRLOW0,    "RSTV0910_P1_CFRLOW0",    "P1 carrier frequency lower limit LSB" },
    { RSTV0910_P1_TSCFGH,     "RSTV0910_P1_TSCFGH",     "P1 transport stream config high" },
    { RSTV0910_P1_TSCFGM,     "RSTV0910_P1_TSCFGM",     "P1 transport stream config medium" },
    { RSTV0910_P1_TSCFGL,     "RSTV0910_P1_TSCFGL",     "P1 transport stream config low" },
    
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
