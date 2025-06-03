/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: tuner2.c                                                                   */
/*    - Tuner 2 specific thread functions and operations                                              */
/*    - Implements independent demodulator and TS processing for second tuner                        */
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

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- INCLUDES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "main.h"
#include "ftdi_dual.h"
#include "ftdi.h"
#include "ftdi_usb.h"
#include "stv0910.h"
#include "stv6120.h"
#include "stvvglna.h"
#include "nim.h"
#include "errors.h"
#include "fifo.h"
#include "udp.h"
#include "libts.h"

#define TS_FRAME_SIZE 20*512 // 512 is base USB FTDI frame

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- UTILITY FUNCTIONS -------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

static inline void timespec_add_ns(struct timespec *ts, int32_t ns)
{
    if((ts->tv_nsec + ns) >= 1e9)
    {
        ts->tv_sec = ts->tv_sec + 1;
        ts->tv_nsec = (ts->tv_nsec + ns) - 1e9;
    }
    else
    {
        ts->tv_nsec = ts->tv_nsec + ns;
    }
}

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- FORWARD DECLARATIONS ----------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* TS parsing callback functions */
static void ts_callback_sdt_service(uint8_t *service_provider_name_ptr, uint32_t *service_provider_name_length_ptr,
                                    uint8_t *service_name_ptr, uint32_t *service_name_length_ptr);
static void ts_callback_pmt_pids(uint32_t *ts_pmt_index_ptr, uint32_t *ts_pmt_es_pid, uint32_t *ts_pmt_es_type);
static void ts_callback_ts_stats(uint32_t *ts_packet_total_count_ptr, uint32_t *ts_null_percentage_ptr);

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

/* TS parse buffer structure for tuner 2 */
typedef struct {
    uint8_t *buffer;
    uint32_t length;
    bool waiting;
    pthread_mutex_t mutex;
    pthread_cond_t signal;
} ts_parse_buffer_t;

/* Tuner 2 TS parse buffer - separate from tuner 1 */
static ts_parse_buffer_t longmynd_ts_parse_buffer_tuner2 = {
    .buffer = NULL,
    .length = 0,
    .waiting = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .signal = PTHREAD_COND_INITIALIZER
};

/* Global status pointer for tuner 2 callbacks */
static longmynd_status_t *ts_longmynd_status_tuner2;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
void *loop_ts_tuner2(void *arg)
{
/* -------------------------------------------------------------------------------------------------- */
/* Tuner 2 transport stream processing loop                                                          */
/* Handles TS data reading and output for the second tuner                                           */
/* arg: thread_vars_t structure with tuner 2 configuration                                           */
/* return: NULL                                                                                       */
/* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars = (thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    longmynd_config_t *config = thread_vars->config;
    longmynd_status_t *status = thread_vars->status2;  /* Use tuner 2 status */

    uint8_t *buffer;
    uint16_t len = 0;
    uint8_t (*ts_write)(uint8_t *, uint32_t, bool *);
    bool fifo_ready;

    *err = ERROR_NONE;

    printf("Flow: Tuner 2 TS thread started\n");

    /* Allocate buffer for TS data */
    buffer = (uint8_t *)malloc(TS_FRAME_SIZE);
    if (buffer == NULL) {
        *err = ERROR_TS_BUFFER_MALLOC;
        return NULL;
    }

    /* Initialize output method based on configuration */
    if (config->tuner2_ts_use_ip) {
        if (*err == ERROR_NONE)
            *err = udp_ts_init(config->tuner2_ts_ip_addr, config->tuner2_ts_ip_port);
        ts_write = udp_ts_write;
        fifo_ready = true;
    } else {
        *err = fifo_ts_init(config->tuner2_ts_fifo_path, &fifo_ready);
        ts_write = fifo_ts_write;
    }

    while (*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE) {
        /* Handle reset flag for tuner 2 */
        if (config->tuner2_ts_reset) {
            do {
                if (*err == ERROR_NONE) 
                    *err = ftdi_usb_ts_read_tuner(TUNER_2_ID, buffer, &len, TS_FRAME_SIZE);
            } while (*err == ERROR_NONE && len > 2);

            pthread_mutex_lock(&status->mutex);
            
            status->service_name[0] = '\0';
            status->service_provider_name[0] = '\0';
            status->ts_null_percentage = 100;
            status->ts_packet_count_nolock = 0;

            for (int j = 0; j < NUM_ELEMENT_STREAMS; j++) {
                status->ts_elementary_streams[j][0] = 0;
            }

            pthread_mutex_unlock(&status->mutex);

            config->tuner2_ts_reset = false;
        }

        /* Read TS data from tuner 2 */
        if (*err == ERROR_NONE) {
            *err = ftdi_usb_ts_read_tuner(TUNER_2_ID, buffer, &len, TS_FRAME_SIZE);
        }

        /* Process and output TS data */
        if (*err == ERROR_NONE && len > 2) {
            if (config->tuner2_ts_use_ip || fifo_ready) {
                *err = ts_write(&buffer[2], len - 2, &fifo_ready);
            } else if (!config->tuner2_ts_use_ip && !fifo_ready) {
                /* Try opening the fifo again */
                *err = fifo_ts_init(config->tuner2_ts_fifo_path, &fifo_ready);
            }

            /* Feed data to tuner 2 TS parser */
            if (longmynd_ts_parse_buffer_tuner2.waiting
                && longmynd_ts_parse_buffer_tuner2.buffer != NULL
                && pthread_mutex_trylock(&longmynd_ts_parse_buffer_tuner2.mutex) == 0) {
                
                memcpy(longmynd_ts_parse_buffer_tuner2.buffer, &buffer[2], len - 2);
                longmynd_ts_parse_buffer_tuner2.length = len - 2;
                pthread_cond_signal(&longmynd_ts_parse_buffer_tuner2.signal);
                longmynd_ts_parse_buffer_tuner2.waiting = false;

                pthread_mutex_unlock(&longmynd_ts_parse_buffer_tuner2.mutex);
            }

            status->ts_packet_count_nolock += (len - 2);
        }
    }

    free(buffer);
    printf("Flow: Tuner 2 TS thread ended\n");

    return NULL;
}

/* -------------------------------------------------------------------------------------------------- */
void *loop_ts_parse_tuner2(void *arg)
{
/* -------------------------------------------------------------------------------------------------- */
/* Tuner 2 transport stream parsing loop                                                             */
/* Parses MPEG-TS data for service information and statistics                                        */
/* arg: thread_vars_t structure with tuner 2 configuration                                           */
/* return: NULL                                                                                       */
/* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars = (thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    longmynd_status_t *status = thread_vars->status2;  /* Use tuner 2 status */

    *err = ERROR_NONE;

    printf("Flow: Tuner 2 TS parse thread started\n");

    /* Set global status pointer for callbacks */
    ts_longmynd_status_tuner2 = status;

    uint8_t *ts_buffer = (uint8_t *)malloc(TS_FRAME_SIZE);
    if (ts_buffer == NULL) {
        *err = ERROR_TS_BUFFER_MALLOC;
        return NULL;
    }

    /* Initialize tuner 2 TS parse buffer */
    longmynd_ts_parse_buffer_tuner2.buffer = ts_buffer;
    longmynd_ts_parse_buffer_tuner2.length = 0;
    longmynd_ts_parse_buffer_tuner2.waiting = false;

    struct timespec ts;

    pthread_mutex_lock(&longmynd_ts_parse_buffer_tuner2.mutex);

    while (*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE) {
        longmynd_ts_parse_buffer_tuner2.waiting = true;

        while (longmynd_ts_parse_buffer_tuner2.waiting && *thread_vars->main_err_ptr == ERROR_NONE) {
            /* Set timer for 100ms */
            clock_gettime(CLOCK_MONOTONIC, &ts);
            timespec_add_ns(&ts, 100 * 1000 * 1000);

            /* Mutex is unlocked during wait */
            pthread_cond_timedwait(&longmynd_ts_parse_buffer_tuner2.signal, 
                                  &longmynd_ts_parse_buffer_tuner2.mutex, &ts);
        }

        /* Parse TS data for tuner 2 */
        ts_parse(
            &ts_buffer[0], longmynd_ts_parse_buffer_tuner2.length,
            &ts_callback_sdt_service,
            &ts_callback_pmt_pids,
            &ts_callback_ts_stats,
            false
        );

        pthread_mutex_lock(&status->mutex);

        /* Trigger pthread signal for tuner 2 status update */
        pthread_cond_signal(&status->signal);

        pthread_mutex_unlock(&status->mutex);
    }

    pthread_mutex_unlock(&longmynd_ts_parse_buffer_tuner2.mutex);

    free(ts_buffer);
    printf("Flow: Tuner 2 TS parse thread ended\n");

    return NULL;
}

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- TS PARSING CALLBACK FUNCTIONS ------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

static void ts_callback_sdt_service(uint8_t *service_provider_name_ptr, uint32_t *service_provider_name_length_ptr,
                                    uint8_t *service_name_ptr, uint32_t *service_name_length_ptr)
{
    pthread_mutex_lock(&ts_longmynd_status_tuner2->mutex);

    memcpy(ts_longmynd_status_tuner2->service_name, service_name_ptr, *service_name_length_ptr);
    ts_longmynd_status_tuner2->service_name[*service_name_length_ptr] = '\0';

    memcpy(ts_longmynd_status_tuner2->service_provider_name, service_provider_name_ptr, *service_provider_name_length_ptr);
    ts_longmynd_status_tuner2->service_provider_name[*service_provider_name_length_ptr] = '\0';

    pthread_mutex_unlock(&ts_longmynd_status_tuner2->mutex);
}

static void ts_callback_pmt_pids(uint32_t *ts_pmt_index_ptr, uint32_t *ts_pmt_es_pid, uint32_t *ts_pmt_es_type)
{
    pthread_mutex_lock(&ts_longmynd_status_tuner2->mutex);

    ts_longmynd_status_tuner2->ts_elementary_streams[*ts_pmt_index_ptr][0] = *ts_pmt_es_pid;
    ts_longmynd_status_tuner2->ts_elementary_streams[*ts_pmt_index_ptr][1] = *ts_pmt_es_type;

    pthread_mutex_unlock(&ts_longmynd_status_tuner2->mutex);
}

static void ts_callback_ts_stats(uint32_t *ts_packet_total_count_ptr, uint32_t *ts_null_percentage_ptr)
{
    if(*ts_packet_total_count_ptr > 0)
    {
        pthread_mutex_lock(&ts_longmynd_status_tuner2->mutex);

        ts_longmynd_status_tuner2->ts_null_percentage = *ts_null_percentage_ptr;

        pthread_mutex_unlock(&ts_longmynd_status_tuner2->mutex);
    }
}
