#ifndef JSON_OUTPUT_H
#define JSON_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

/* -------------------------------------------------------------------------------------------------- */
/* JSON OUTPUT CONFIGURATION                                                                          */
/* -------------------------------------------------------------------------------------------------- */

/* Compile-time enable/disable JSON output */
#ifndef ENABLE_JSON_OUTPUT
#define ENABLE_JSON_OUTPUT 1  /* Set to 0 to disable all JSON output */
#endif

/* Default JSON output interval in milliseconds */
#define JSON_OUTPUT_DEFAULT_INTERVAL_MS 1000

/* JSON output format options */
typedef enum {
    JSON_FORMAT_FULL,     /* Complete JSON with all fields */
    JSON_FORMAT_COMPACT,  /* Condensed JSON with abbreviated field names */
    JSON_FORMAT_MINIMAL   /* Essential fields only */
} json_format_t;

/* JSON output configuration structure */
typedef struct {
    bool enabled;                    /* Enable/disable JSON output */
    json_format_t format;           /* Output format type */
    uint32_t interval_ms;           /* Output interval in milliseconds */
    bool include_constellation;     /* Include constellation data */
    bool include_timestamp;         /* Include timestamp in output */
    bool pretty_print;              /* Pretty print JSON (with indentation) */
} json_output_config_t;

/* -------------------------------------------------------------------------------------------------- */
/* JSON OUTPUT FUNCTIONS                                                                              */
/* -------------------------------------------------------------------------------------------------- */

/* Initialization and configuration */
void json_output_init(void);
void json_output_set_config(const json_output_config_t *config);
void json_output_get_config(json_output_config_t *config);

/* Enable/disable functions */
void json_output_enable(bool enabled);
bool json_output_is_enabled(void);

/* Format configuration */
void json_output_set_format(json_format_t format);
void json_output_set_interval(uint32_t interval_ms);
void json_output_set_include_constellation(bool include);

/* Main JSON output function */
void json_output_demod_cycle(uint8_t tuner, const longmynd_status_t *status);

/* JSON formatting functions */
int json_format_demod_status_full(char *buffer, size_t buffer_size, uint8_t tuner, 
                                  const longmynd_status_t *status, uint64_t timestamp);
int json_format_demod_status_compact(char *buffer, size_t buffer_size, uint8_t tuner, 
                                     const longmynd_status_t *status, uint64_t timestamp);
int json_format_demod_status_minimal(char *buffer, size_t buffer_size, uint8_t tuner, 
                                     const longmynd_status_t *status, uint64_t timestamp);

/* Utility functions */
uint64_t json_get_timestamp_ms(void);
const char *json_get_demod_state_name(uint8_t demod_state);
const char *json_get_state_name(uint8_t state);
bool json_should_output_now(void);

/* -------------------------------------------------------------------------------------------------- */
/* CONVENIENCE MACROS FOR JSON OUTPUT                                                                */
/* -------------------------------------------------------------------------------------------------- */

#if ENABLE_JSON_OUTPUT

#define JSON_OUTPUT_DEMOD_CYCLE(tuner, status) \
    do { \
        if (json_output_is_enabled()) { \
            json_output_demod_cycle((tuner), (status)); \
        } \
    } while(0)

#define JSON_OUTPUT_INIT() \
    do { \
        json_output_init(); \
    } while(0)

#define JSON_OUTPUT_ENABLE(enabled) \
    do { \
        json_output_enable(enabled); \
    } while(0)

#else

/* Disabled versions - compile to nothing */
#define JSON_OUTPUT_DEMOD_CYCLE(tuner, status) do { } while(0)
#define JSON_OUTPUT_INIT() do { } while(0)
#define JSON_OUTPUT_ENABLE(enabled) do { } while(0)

#endif /* ENABLE_JSON_OUTPUT */

/* -------------------------------------------------------------------------------------------------- */
/* JSON FIELD DEFINITIONS                                                                             */
/* -------------------------------------------------------------------------------------------------- */

/* JSON field names for full format */
#define JSON_FIELD_TIMESTAMP        "timestamp"
#define JSON_FIELD_TUNER           "tuner"
#define JSON_FIELD_SIGNAL          "signal"
#define JSON_FIELD_LOCK            "lock"
#define JSON_FIELD_ERRORS          "errors"
#define JSON_FIELD_FREQUENCY       "frequency"
#define JSON_FIELD_MODULATION      "modulation"
#define JSON_FIELD_CONSTELLATION   "constellation"

/* Signal sub-fields */
#define JSON_FIELD_POWER_I         "power_i"
#define JSON_FIELD_POWER_Q         "power_q"
#define JSON_FIELD_AGC1_GAIN       "agc1_gain"
#define JSON_FIELD_AGC2_GAIN       "agc2_gain"
#define JSON_FIELD_LNA_GAIN        "lna_gain"

/* Lock sub-fields */
#define JSON_FIELD_DEMOD_STATE     "demod_state"
#define JSON_FIELD_STATE_NAME      "state_name"
#define JSON_FIELD_LOCKED          "locked"

/* Error sub-fields */
#define JSON_FIELD_VITERBI_RATE    "viterbi_rate"
#define JSON_FIELD_BER             "ber"
#define JSON_FIELD_MER             "mer"
#define JSON_FIELD_BCH_UNCORRECTED "bch_uncorrected"
#define JSON_FIELD_BCH_COUNT       "bch_count"
#define JSON_FIELD_LDPC_COUNT      "ldpc_count"

/* Frequency sub-fields */
#define JSON_FIELD_FREQ_REQUESTED  "requested"
#define JSON_FIELD_FREQ_OFFSET     "offset"
#define JSON_FIELD_FREQ_ACTUAL     "actual"

/* Modulation sub-fields */
#define JSON_FIELD_SYMBOL_RATE     "symbol_rate"
#define JSON_FIELD_MODCOD          "modcod"
#define JSON_FIELD_SHORT_FRAME     "short_frame"
#define JSON_FIELD_PILOTS          "pilots"
#define JSON_FIELD_ROLLOFF         "rolloff"

/* Compact field names (abbreviated) */
#define JSON_FIELD_COMPACT_TIMESTAMP    "ts"
#define JSON_FIELD_COMPACT_TUNER       "t"
#define JSON_FIELD_COMPACT_POWER_I     "pi"
#define JSON_FIELD_COMPACT_POWER_Q     "pq"
#define JSON_FIELD_COMPACT_AGC1        "a1"
#define JSON_FIELD_COMPACT_AGC2        "a2"
#define JSON_FIELD_COMPACT_LNA         "lna"
#define JSON_FIELD_COMPACT_DEMOD_STATE "ds"
#define JSON_FIELD_COMPACT_LOCKED      "lck"
#define JSON_FIELD_COMPACT_VITERBI     "vit"
#define JSON_FIELD_COMPACT_BER         "ber"
#define JSON_FIELD_COMPACT_MER         "mer"
#define JSON_FIELD_COMPACT_FREQ        "freq"
#define JSON_FIELD_COMPACT_SR          "sr"
#define JSON_FIELD_COMPACT_MODCOD      "mc"

/* Buffer sizes */
#define JSON_BUFFER_SIZE_FULL      2048
#define JSON_BUFFER_SIZE_COMPACT   1024
#define JSON_BUFFER_SIZE_MINIMAL   512

#endif /* JSON_OUTPUT_H */
