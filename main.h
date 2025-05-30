/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: main.h                                                                      */
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

#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <pthread.h>

/* states of the main loop state machine */
#define STATE_INIT               0
#define STATE_DEMOD_HUNTING      1
#define STATE_DEMOD_FOUND_HEADER 2
#define STATE_DEMOD_S            3
#define STATE_DEMOD_S2           4

/* define the various status reports */
#define STATUS_STATE               1
#define STATUS_LNA_GAIN            2
#define STATUS_PUNCTURE_RATE       3
#define STATUS_POWER_I             4
#define STATUS_POWER_Q             5
#define STATUS_CARRIER_FREQUENCY   6
#define STATUS_CONSTELLATION_I     7
#define STATUS_CONSTELLATION_Q     8
#define STATUS_SYMBOL_RATE         9
#define STATUS_VITERBI_ERROR_RATE 10
#define STATUS_BER                11
#define STATUS_MER                12
#define STATUS_SERVICE_NAME       13
#define STATUS_SERVICE_PROVIDER_NAME  14
#define STATUS_TS_NULL_PERCENTAGE 15
#define STATUS_ES_PID             16
#define STATUS_ES_TYPE            17
#define STATUS_MODCOD             18
#define STATUS_SHORT_FRAME        19
#define STATUS_PILOTS             20
#define STATUS_ERRORS_LDPC_COUNT  21
#define STATUS_ERRORS_BCH_COUNT   22
#define STATUS_ERRORS_BCH_UNCORRECTED   23
#define STATUS_LNB_SUPPLY         24
#define STATUS_LNB_POLARISATION_H 25
#define STATUS_AGC1_GAIN          26
#define STATUS_AGC2_GAIN          27
#define STATUS_MATYPE1            28
#define STATUS_MATYPE2            29
#define STATUS_ROLLOFF            30
#define STATUS_TS_PACKET_COUNT    31
#define STATUS_TS_LOCK            32
#define STATUS_TS_BITRATE         33

/* The number of constellation peeks we do for each background loop */
#define NUM_CONSTELLATIONS 16

#define NUM_ELEMENT_STREAMS 16

/* Dual UDP configuration structure as specified in implementation plan */
typedef struct {
    char tuner1_ip[16];
    uint16_t tuner1_port;
    char tuner2_ip[16];
    uint16_t tuner2_port;
    bool dual_tuner_enabled;
} dual_udp_config_t;

typedef struct {
    bool port_swap;
    uint8_t port;
    float halfscan_ratio;
    uint8_t freq_index=0;
    uint8_t sr_index=0;
    uint32_t freq_requested[4];
    uint32_t sr_requested[4];
    bool beep_enabled;

    uint8_t device_usb_bus;
    uint8_t device_usb_addr;

    // Dual-tuner support
    bool dual_tuner_enabled;
    uint8_t device2_usb_bus;
    uint8_t device2_usb_addr;
    bool auto_detect_second_device;

    bool ts_use_ip;
    bool ts_reset;
    char ts_fifo_path[128];
    char ts_ip_addr[16];
    int ts_ip_port;

    // Second tuner TS output
    char ts2_fifo_path[128];
    char ts2_ip_addr[16];
    int ts2_ip_port;

    bool status_use_ip;
    bool status_use_mqtt;
    char status_fifo_path[128];
    char status2_fifo_path[128];  // Second tuner status FIFO path
    char status_ip_addr[16];
    int status_ip_port;

    bool polarisation_supply;
    bool polarisation_horizontal; // false -> 13V, true -> 18V

    // Dual-tuner specific configuration
    uint32_t freq_requested_tuner2[4];
    uint32_t sr_requested_tuner2[4];
    uint8_t freq_index_tuner2;
    uint8_t sr_index_tuner2;
    bool polarisation_supply_tuner2;
    bool polarisation_horizontal_tuner2;
    bool new_config_tuner2;
    bool tuners_initialized;  // Track if tuners have been initialized before

    int ts_timeout;

    bool new_config=false;
    pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
} longmynd_config_t;

typedef struct {
    uint8_t state;
    uint8_t demod_state;
    bool lna_ok;
    uint16_t lna_gain;
    uint16_t agc1_gain;
    uint16_t agc2_gain;
    uint8_t power_i;
    uint8_t power_q;
    uint32_t frequency_requested;
    int32_t frequency_offset;
    bool polarisation_supply;
    bool polarisation_horizontal; // false -> 13V, true -> 18V
    uint32_t symbolrate_requested;
    uint32_t symbolrate;
    uint32_t viterbi_error_rate; // DVB-S1
    uint32_t bit_error_rate; // DVB-S2
    int32_t modulation_error_rate; // DVB-S2
    bool errors_bch_uncorrected;
    uint32_t errors_bch_count;
    uint32_t errors_ldpc_count;
    int8_t constellation[NUM_CONSTELLATIONS][2]; // { i, q }
    uint8_t puncture_rate;
    char service_name[255]={'\0'};
    char service_provider_name[255]={'\0'};
    uint8_t ts_null_percentage;
    uint16_t ts_elementary_streams[NUM_ELEMENT_STREAMS][2]; // { pid, type }
    uint32_t modcod;
    uint32_t matype1;
    uint32_t matype2;
    bool short_frame;
    bool pilots;
    uint8_t rolloff;
    uint64_t last_ts_or_reinit_monotonic;

    uint64_t last_updated_monotonic=0;
    pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t signal=PTHREAD_COND_INITIALIZER;

    uint32_t ts_packet_count_nolock=0;
    uint32_t ts_packet_count_total=0;
    bool ts_lock=false;
    uint32_t ts_bitrate_kbps=0;
    uint64_t ts_last_bitrate_calc_monotonic=0;
} longmynd_status_t;

typedef struct {
    uint8_t *main_state_ptr;
    uint8_t *main_err_ptr;
    uint8_t thread_err;
    longmynd_config_t *config;
    longmynd_status_t *status;
    uint8_t tuner_id;  // 1 for tuner 1, 2 for tuner 2 (dual-tuner support)

    /* Dual-tuner synchronization */
    pthread_mutex_t *dual_sync_mutex;
    pthread_cond_t *dual_sync_cond;
    bool *top_demod_ready;
} thread_vars_t;

void config_set_frequency(uint32_t frequency);
void config_set_symbolrate(uint32_t symbolrate);
void config_set_frequency_and_symbolrate(uint32_t frequency, uint32_t symbolrate);
void config_set_lnbv(bool enabled, bool horizontal);
void config_reinit(bool increment_frsr);
void config_set_swport(bool sport);
void config_set_tsip(char *tsip);

/* Dual-tuner configuration functions */
void config_set_frequency_tuner2(uint32_t frequency);
void config_set_symbolrate_tuner2(uint32_t symbolrate);
void config_set_frequency_and_symbolrate_tuner2(uint32_t frequency, uint32_t symbolrate);
void config_set_lnbv_tuner2(bool enabled, bool horizontal);
void config_reinit_tuner2(bool increment_frsr);

/* Dual-tuner aware reporting functions */
uint8_t do_report_dual(longmynd_status_t *status, uint8_t demod);

/* MQTT dual-tuner functions */
extern bool dual_tuner_mqtt_enabled;
void mqtt_set_dual_tuner_mode(bool enabled);
void mqtt_process_dual_command(const char *topic, const char *payload);
uint8_t mqtt_status_write_tuner(uint8_t tuner_id, uint8_t message, uint32_t data, bool *output_ready);
uint8_t mqtt_status_string_write_tuner(uint8_t tuner_id, uint8_t message, char *data, bool *output_ready);

#endif

