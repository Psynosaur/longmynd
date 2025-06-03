/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: tuner2.h                                                                   */
/*    - Tuner 2 specific function declarations and definitions                                        */
/*    - Independent demodulator and TS processing for second tuner                                   */
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

#ifndef TUNER2_H
#define TUNER2_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "main.h"
#include "ts.h"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- DEFINITIONS -------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* TS parse buffer structure for tuner 2 */
typedef struct {
    uint8_t *buffer;
    uint32_t length;
    bool waiting;
    pthread_mutex_t mutex;
    pthread_cond_t signal;
} ts_parse_buffer_t;

/* Tuner 2 specific constants */
#define TUNER2_DEFAULT_TS_FIFO "longmynd_tuner2_ts"
#define TUNER2_DEFAULT_STATUS_FIFO "longmynd_tuner2_status"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

/* Tuner 2 TS parse buffer - separate from tuner 1 */
extern ts_parse_buffer_t longmynd_ts_parse_buffer_tuner2;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- FUNCTION DECLARATIONS ---------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* Tuner 2 thread functions */
void *loop_ts_tuner2(void *arg);
void *loop_ts_parse_tuner2(void *arg);

/* Tuner 2 hardware initialization functions */
uint8_t tuner2_hardware_initialize_modules(const longmynd_config_t *config, longmynd_status_t *status_cpy);
uint8_t tuner2_hardware_configure_lna_and_polarization(const longmynd_config_t *config, longmynd_status_t *status_cpy);
uint8_t tuner2_hardware_start_demodulator_scan(longmynd_status_t *status_cpy);

/* Tuner 2 status and reporting functions */
uint8_t tuner2_do_report(longmynd_status_t *status);
uint8_t tuner2_update_status_synchronization(longmynd_status_t *status, longmynd_status_t *status_cpy, 
                                             uint32_t *last_ts_packet_count);

/* Tuner 2 demodulator state machine functions */
uint8_t tuner2_handle_demod_hunting_state(const longmynd_config_t *config_cpy, longmynd_status_t *status_cpy);
uint8_t tuner2_handle_demod_header_state(const longmynd_config_t *config_cpy, longmynd_status_t *status_cpy);
uint8_t tuner2_handle_demod_locked_states(const longmynd_config_t *config_cpy, longmynd_status_t *status_cpy);

/* Tuner 2 timeout and error handling */
uint8_t tuner2_handle_ts_timeout(const longmynd_config_t *config_cpy, longmynd_status_t *status_cpy, 
                                 uint64_t current_monotonic);
uint8_t tuner2_handle_frequency_symbolrate_cycling(const longmynd_config_t *config_cpy, longmynd_status_t *status_cpy);

#endif /* TUNER2_H */
