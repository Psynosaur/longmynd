/* -------------------------------------------------------------------------------------------------- */
/* JSON OUTPUT MODULE FOR LONGMYND2                                                                   */
/* -------------------------------------------------------------------------------------------------- */
/* Provides structured JSON output for demodulator cycle telemetry data                              */
/* Supports multiple output formats: full, compact, and minimal                                       */
/* Configurable output intervals and field inclusion                                                  */
/* Designed for external tool consumption and monitoring                                              */
/* -------------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include "json_output.h"
#include "main.h"

/* -------------------------------------------------------------------------------------------------- */
/* GLOBAL VARIABLES                                                                                   */
/* -------------------------------------------------------------------------------------------------- */

static json_output_config_t json_config = {
    .enabled = false,
    .format = JSON_FORMAT_FULL,
    .interval_ms = JSON_OUTPUT_DEFAULT_INTERVAL_MS,
    .include_constellation = false,
    .include_timestamp = true,
    .pretty_print = false
};

static uint64_t last_json_output_time = 0;

/* -------------------------------------------------------------------------------------------------- */
/* UTILITY FUNCTIONS                                                                                  */
/* -------------------------------------------------------------------------------------------------- */

uint64_t json_get_timestamp_ms(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets current timestamp in milliseconds                                                          */
    /* return: timestamp in milliseconds                                                               */
    /* -------------------------------------------------------------------------------------------------- */
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
#endif
}

const char *json_get_demod_state_name(uint8_t demod_state)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Converts demod state number to string name                                                      */
    /* demod_state: demodulator state number (hardware states from STV0910)                           */
    /* return: string name of demod state                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    switch (demod_state) {
        case 0: return "hunting";
        case 1: return "found_header";
        case 2: return "demod_s2";
        case 3: return "demod_s";
        default: return "unknown";
    }
}

const char *json_get_state_name(uint8_t state)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Converts receiver state number to string name                                                   */
    /* state: receiver state number                                                                    */
    /* return: string name of receiver state                                                           */
    /* -------------------------------------------------------------------------------------------------- */
    switch (state) {
        case 0: return "init";
        case 1: return "hunting";
        case 2: return "found_header";
        case 3: return "demod_s";
        case 4: return "demod_s2";
        default: return "unknown";
    }
}

bool json_should_output_now(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Checks if enough time has passed for next JSON output                                           */
    /* return: true if JSON should be output now                                                       */
    /* -------------------------------------------------------------------------------------------------- */
    if (!json_config.enabled) {
        return false;
    }
    
    uint64_t current_time = json_get_timestamp_ms();
    if (current_time - last_json_output_time >= json_config.interval_ms) {
        last_json_output_time = current_time;
        return true;
    }
    
    return false;
}

/* -------------------------------------------------------------------------------------------------- */
/* CONFIGURATION FUNCTIONS                                                                            */
/* -------------------------------------------------------------------------------------------------- */

void json_output_init(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Initializes JSON output module with default configuration                                       */
    /* -------------------------------------------------------------------------------------------------- */
    last_json_output_time = 0;
}

void json_output_set_config(const json_output_config_t *config)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Sets JSON output configuration                                                                   */
    /* config: pointer to configuration structure                                                      */
    /* -------------------------------------------------------------------------------------------------- */
    if (config != NULL) {
        json_config = *config;
    }
}

void json_output_get_config(json_output_config_t *config)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Gets current JSON output configuration                                                           */
    /* config: pointer to configuration structure to fill                                              */
    /* -------------------------------------------------------------------------------------------------- */
    if (config != NULL) {
        *config = json_config;
    }
}

void json_output_enable(bool enabled)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Enables or disables JSON output                                                                 */
    /* enabled: true to enable, false to disable                                                       */
    /* -------------------------------------------------------------------------------------------------- */
    json_config.enabled = enabled;
}

bool json_output_is_enabled(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Checks if JSON output is enabled                                                                */
    /* return: true if enabled, false if disabled                                                      */
    /* -------------------------------------------------------------------------------------------------- */
    return json_config.enabled;
}

void json_output_set_format(json_format_t format)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Sets JSON output format                                                                         */
    /* format: JSON format type                                                                        */
    /* -------------------------------------------------------------------------------------------------- */
    json_config.format = format;
}

void json_output_set_interval(uint32_t interval_ms)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Sets JSON output interval                                                                       */
    /* interval_ms: output interval in milliseconds                                                    */
    /* -------------------------------------------------------------------------------------------------- */
    json_config.interval_ms = interval_ms;
}

void json_output_set_include_constellation(bool include)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Sets whether to include constellation data in JSON output                                       */
    /* include: true to include constellation data                                                     */
    /* -------------------------------------------------------------------------------------------------- */
    json_config.include_constellation = include;
}

/* -------------------------------------------------------------------------------------------------- */
/* JSON FORMATTING FUNCTIONS                                                                          */
/* -------------------------------------------------------------------------------------------------- */

int json_format_demod_status_full(char *buffer, size_t buffer_size, uint8_t tuner, 
                                  const longmynd_status_t *status, uint64_t timestamp)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Formats demodulator status as full JSON                                                         */
    /* buffer: output buffer                                                                           */
    /* buffer_size: size of output buffer                                                              */
    /* tuner: tuner number                                                                             */
    /* status: status structure                                                                        */
    /* timestamp: timestamp in milliseconds                                                            */
    /* return: number of characters written, or -1 on error                                           */
    /* -------------------------------------------------------------------------------------------------- */
    if (buffer == NULL || status == NULL || buffer_size == 0) {
        return -1;
    }

    bool is_locked = (status->demod_state == 2 || status->demod_state == 3);
    double actual_freq = status->frequency_requested + (status->frequency_offset / 1000.0);
    
    return snprintf(buffer, buffer_size,
        "{\n"
        "  \"timestamp\": %llu,\n"
        "  \"tuner\": %u,\n"
        "  \"signal\": {\n"
        "    \"power_i\": %u,\n"
        "    \"power_q\": %u,\n"
        "    \"agc1_gain\": %u,\n"
        "    \"agc2_gain\": %u,\n"
        "    \"lna_gain\": %u\n"
        "  },\n"
        "  \"lock\": {\n"
        "    \"demod_state\": %u,\n"
        "    \"state_name\": \"%s\",\n"
        "    \"locked\": %s\n"
        "  },\n"
        "  \"errors\": {\n"
        "    \"viterbi_rate\": %u,\n"
        "    \"ber\": %u,\n"
        "    \"mer\": %d,\n"
        "    \"bch_uncorrected\": %s,\n"
        "    \"bch_count\": %u,\n"
        "    \"ldpc_count\": %u\n"
        "  },\n"
        "  \"frequency\": {\n"
        "    \"requested\": %u,\n"
        "    \"offset\": %d,\n"
        "    \"actual\": %.1f\n"
        "  },\n"
        "  \"modulation\": {\n"
        "    \"symbol_rate\": %u,\n"
        "    \"modcod\": %u,\n"
        "    \"short_frame\": %s,\n"
        "    \"pilots\": %s,\n"
        "    \"rolloff\": %u\n"
        "  }\n"
        "}",
        (unsigned long long)timestamp,
        tuner,
        status->power_i,
        status->power_q,
        status->agc1_gain,
        status->agc2_gain,
        status->lna_gain,
        status->demod_state,
        json_get_demod_state_name(status->demod_state),
        is_locked ? "true" : "false",
        status->viterbi_error_rate,
        status->bit_error_rate,
        status->modulation_error_rate,
        status->errors_bch_uncorrected ? "true" : "false",
        status->errors_bch_count,
        status->errors_ldpc_count,
        status->frequency_requested,
        status->frequency_offset,
        actual_freq,
        status->symbolrate,
        status->modcod,
        status->short_frame ? "true" : "false",
        status->pilots ? "true" : "false",
        status->rolloff
    );
}

int json_format_demod_status_compact(char *buffer, size_t buffer_size, uint8_t tuner,
                                     const longmynd_status_t *status, uint64_t timestamp)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Formats demodulator status as compact JSON                                                      */
    /* buffer: output buffer                                                                           */
    /* buffer_size: size of output buffer                                                              */
    /* tuner: tuner number                                                                             */
    /* status: status structure                                                                        */
    /* timestamp: timestamp in milliseconds                                                            */
    /* return: number of characters written, or -1 on error                                           */
    /* -------------------------------------------------------------------------------------------------- */
    if (buffer == NULL || status == NULL || buffer_size == 0) {
        return -1;
    }

    bool is_locked = (status->demod_state == 2 || status->demod_state == 3);
    double actual_freq = status->frequency_requested + (status->frequency_offset / 1000.0);

    return snprintf(buffer, buffer_size,
        "{\"ts\":%llu,\"t\":%u,\"pi\":%u,\"pq\":%u,\"a1\":%u,\"a2\":%u,\"lna\":%u,"
        "\"ds\":%u,\"lck\":%s,\"vit\":%u,\"ber\":%u,\"mer\":%d,\"freq\":%.1f,\"sr\":%u,\"mc\":%u}",
        (unsigned long long)timestamp,
        tuner,
        status->power_i,
        status->power_q,
        status->agc1_gain,
        status->agc2_gain,
        status->lna_gain,
        status->demod_state,
        is_locked ? "true" : "false",
        status->viterbi_error_rate,
        status->bit_error_rate,
        status->modulation_error_rate,
        actual_freq,
        status->symbolrate,
        status->modcod
    );
}

int json_format_demod_status_minimal(char *buffer, size_t buffer_size, uint8_t tuner,
                                     const longmynd_status_t *status, uint64_t timestamp)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Formats demodulator status as minimal JSON                                                      */
    /* buffer: output buffer                                                                           */
    /* buffer_size: size of output buffer                                                              */
    /* tuner: tuner number                                                                             */
    /* status: status structure                                                                        */
    /* timestamp: timestamp in milliseconds                                                            */
    /* return: number of characters written, or -1 on error                                           */
    /* -------------------------------------------------------------------------------------------------- */
    if (buffer == NULL || status == NULL || buffer_size == 0) {
        return -1;
    }

    bool is_locked = (status->demod_state == 2 || status->demod_state == 3);

    return snprintf(buffer, buffer_size,
        "{\"ts\":%llu,\"t\":%u,\"lck\":%s,\"mer\":%d,\"freq\":%u,\"sr\":%u}",
        (unsigned long long)timestamp,
        tuner,
        is_locked ? "true" : "false",
        status->modulation_error_rate,
        status->frequency_requested,
        status->symbolrate
    );
}

/* -------------------------------------------------------------------------------------------------- */
/* MAIN JSON OUTPUT FUNCTION                                                                          */
/* -------------------------------------------------------------------------------------------------- */

void json_output_demod_cycle(uint8_t tuner, const longmynd_status_t *status)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Outputs demodulator cycle data as JSON to stdout                                                */
    /* tuner: tuner number                                                                             */
    /* status: status structure containing demodulator data                                            */
    /* -------------------------------------------------------------------------------------------------- */
    if (!json_config.enabled || status == NULL) {
        return;
    }

    if (!json_should_output_now()) {
        return;
    }

    char *buffer = NULL;
    size_t buffer_size = 0;
    int result = -1;
    uint64_t timestamp = json_get_timestamp_ms();

    /* Allocate buffer based on format */
    switch (json_config.format) {
        case JSON_FORMAT_FULL:
            buffer_size = JSON_BUFFER_SIZE_FULL;
            buffer = (char*)malloc(buffer_size);
            if (buffer != NULL) {
                result = json_format_demod_status_full(buffer, buffer_size, tuner, status, timestamp);
            }
            break;

        case JSON_FORMAT_COMPACT:
            buffer_size = JSON_BUFFER_SIZE_COMPACT;
            buffer = (char*)malloc(buffer_size);
            if (buffer != NULL) {
                result = json_format_demod_status_compact(buffer, buffer_size, tuner, status, timestamp);
            }
            break;

        case JSON_FORMAT_MINIMAL:
            buffer_size = JSON_BUFFER_SIZE_MINIMAL;
            buffer = (char*)malloc(buffer_size);
            if (buffer != NULL) {
                result = json_format_demod_status_minimal(buffer, buffer_size, tuner, status, timestamp);
            }
            break;

        default:
            break;
    }

    /* Output JSON to stdout if formatting succeeded */
    if (buffer != NULL && result > 0) {
        printf("%s\n", buffer);
        fflush(stdout);
    }

    /* Clean up */
    if (buffer != NULL) {
        free(buffer);
    }
}
