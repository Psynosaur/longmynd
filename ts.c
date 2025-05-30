/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: ts.c                                                                        */
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

#include <string.h>
#include <time.h>

/* Windows compatibility for CLOCK_MONOTONIC */
#ifdef _WIN32
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#endif

#include "main.h"
#include "errors.h"
#include "udp.h"
#include "fifo.h"
#include "ftdi.h"
#include "ftdi_usb.h"
#include "ts.h"

#include "libts.h"
#include "stv0910.h"

/* External function declaration */
extern uint64_t monotonic_ms(void);

#define TS_FRAME_SIZE 20*512 // 512 is base USB FTDI frame

uint8_t *ts_buffer_ptr = NULL;
bool ts_buffer_waiting;

typedef struct {
    uint8_t *buffer;
    uint32_t length;
    bool waiting;
    pthread_mutex_t mutex;
    pthread_cond_t signal;
} longmynd_ts_parse_buffer_t;

static longmynd_ts_parse_buffer_t longmynd_ts_parse_buffer = {
    .buffer = NULL,
    .length = 0,
    .waiting = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .signal = PTHREAD_COND_INITIALIZER
};

/* Tuner 2 dedicated TS parse buffer */
static longmynd_ts_parse_buffer_t longmynd_ts_parse_buffer_tuner2 = {
    .buffer = NULL,
    .length = 0,
    .waiting = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .signal = PTHREAD_COND_INITIALIZER
};



/* -------------------------------------------------------------------------------------------------- */
void *loop_ts(void *arg) {
/* -------------------------------------------------------------------------------------------------- */
/* Runs a loop to query the Minitiouner TS endpoint, and output it to the requested interface         */
/* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars=(thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    longmynd_config_t *config = thread_vars->config;
    longmynd_status_t *status = thread_vars->status;

    uint8_t *buffer;
    uint16_t len=0;
    uint8_t (*ts_write)(uint8_t*,uint32_t,bool*);
    bool fifo_ready;

    *err=ERROR_NONE;

    buffer = malloc(TS_FRAME_SIZE);
    if(buffer == NULL)
    {
        *err=ERROR_TS_BUFFER_MALLOC;
    }

    if(thread_vars->config->ts_use_ip) {
        if (thread_vars->config->dual_tuner_enabled) {
            /* Dual-tuner mode: UDP sockets already initialized by udp_ts_init_dual() */
            /* Just assign the appropriate write function for each tuner */
            if (thread_vars->tuner_id == 2) {
                ts_write = udp_ts_write_tuner2;
            } else {
                ts_write = udp_ts_write_tuner1;
            }
        } else {
            /* Single-tuner mode: Initialize UDP socket */
            *err=udp_ts_init(thread_vars->config->ts_ip_addr, thread_vars->config->ts_ip_port);
            ts_write = udp_ts_write;
        }
    } else {
        if (thread_vars->tuner_id == 2) {
            /* Tuner 2: Use tuner 2 FIFO */
            *err=fifo_ts2_init(thread_vars->config->ts2_fifo_path, &fifo_ready);
            ts_write = fifo_ts2_write;
        } else {
            /* Tuner 1: Use tuner 1 FIFO */
            *err=fifo_ts_init(thread_vars->config->ts_fifo_path, &fifo_ready);
            ts_write = fifo_ts_write;
        }
    }

    while(*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE){
        /* If reset flag is active (eg. just started or changed station), then clear out the ts buffer */
        if(config->ts_reset) {
            do {
                /* Use tuner-specific FTDI read function */
                if (thread_vars->tuner_id == 2) {
                    if (*err==ERROR_NONE) *err=ftdi_usb_ts_read_tuner2(buffer, &len, TS_FRAME_SIZE);
                } else {
                    if (*err==ERROR_NONE) *err=ftdi_usb_ts_read(buffer, &len, TS_FRAME_SIZE);
                }
            } while (*err==ERROR_NONE && len>2);

            pthread_mutex_lock(&status->mutex);

            /* FIXED: Only clear service names on initial startup, not on every config change */
            /* This prevents frequent clearing of service names in dual-tuner mode */
            static bool first_reset = true;
            if (first_reset || (status->service_name[0] == '\0' && status->service_provider_name[0] == '\0')) {
                printf("TS: Clearing service names during initial TS reset\n");
                status->service_name[0] = '\0';
                status->service_provider_name[0] = '\0';
                first_reset = false;
            } else {
                printf("TS: Preserving existing service names during TS reset: '%s' / '%s'\n",
                       status->service_name, status->service_provider_name);
            }

            status->ts_null_percentage = 100;
            status->ts_packet_count_nolock = 0;

            /* Reset TS status information */
            status->ts_packet_count_total = 0;
            status->ts_lock = false;
            status->ts_bitrate_kbps = 0;
            status->ts_last_bitrate_calc_monotonic = 0;

            for (int j=0; j<NUM_ELEMENT_STREAMS; j++) {
                status->ts_elementary_streams[j][0] = 0;
            }

            pthread_mutex_unlock(&status->mutex);

           config->ts_reset = false;
        }


        /* Use tuner-specific FTDI read function */
        if (thread_vars->tuner_id == 2) {
            *err=ftdi_usb_ts_read_tuner2(buffer, &len, TS_FRAME_SIZE);
        } else {
            *err=ftdi_usb_ts_read(buffer, &len, TS_FRAME_SIZE);
        }

        //if(len>2) fprintf(stderr,"len %d\n",len);
        /* if there is ts data then we send it out to the required output. But, we have to lose the first 2 bytes */
        /* that are the usual FTDI 2 byte response and not part of the TS */
        if ((*err==ERROR_NONE) && (len>2)) {
            static uint32_t read_count = 0;
            read_count++;
            if (read_count % 200 == 1) {  /* Log every 200th read to avoid spam */
                printf("DEBUG: Tuner%d TS read #%u: len=%u, data_len=%u, first_ts_byte=0x%02x\n",
                       thread_vars->tuner_id, read_count, len, len-2, len > 2 ? buffer[2] : 0);
            }

/*
            uint32_t matype1,matype2;
            pthread_mutex_lock(&status->mutex);
         stv0910_read_matype(1, &matype1,&matype2);
         pthread_mutex_unlock(&status->mutex);
         */

        if(thread_vars->config->ts_use_ip && (status->matype1&0xC0)>>6 == 3)
        {
            /* Use tuner-specific UDP write function */
            if (thread_vars->tuner_id == 2) {
                ts_write = udp_ts_write_tuner2;
            } else {
                ts_write = udp_ts_write_tuner1;
            }
            //ts_write = udp_bb_write;
        }
        if(thread_vars->config->ts_use_ip && (status->matype1&0xC0)>>6 == 1)
        {
            /* Use tuner-specific BB write function */
            if (thread_vars->tuner_id == 2) {
                ts_write = udp_bb_write_tuner2;
            } else {
                ts_write = udp_bb_write_tuner1;
            }
        }


            /* Check if TS streaming is enabled before writing */
            bool streaming_allowed = false;
            pthread_mutex_lock(&config->mutex);
            streaming_allowed = config->ts_streaming_enabled;
            pthread_mutex_unlock(&config->mutex);

            if (streaming_allowed && (thread_vars->config->ts_use_ip || fifo_ready))
            {
                static uint32_t write_count = 0;
                write_count++;
                if (write_count % 200 == 1) {  /* Log every 200th write */
                    printf("DEBUG: Tuner%d calling ts_write #%u: data_len=%u, first_byte=0x%02x\n",
                           thread_vars->tuner_id, write_count, len-2, len > 2 ? buffer[2] : 0);
                }
                *err=ts_write(&buffer[2],len-2,&fifo_ready);
            }
            else if (!streaming_allowed)
            {
                /* TS streaming not yet enabled - wait for initial tuning to complete */
                static uint32_t wait_count = 0;
                wait_count++;
                if (wait_count % 1000 == 1) {  /* Log every 1000th wait */
                    printf("DEBUG: Tuner%d waiting for TS streaming to be enabled (initial tuning)\n", thread_vars->tuner_id);
                }
            }
            else if(!thread_vars->config->ts_use_ip && !fifo_ready)
            {
                /* Try opening the fifo again */
                if (thread_vars->tuner_id == 2) {
                    *err=fifo_ts2_init(thread_vars->config->ts2_fifo_path, &fifo_ready);
                } else {
                    *err=fifo_ts_init(thread_vars->config->ts_fifo_path, &fifo_ready);
                }
            }

            /* Route TS data to tuner-specific parse buffer */
            if (thread_vars->tuner_id == 2) {
                /* Tuner 2: Use dedicated tuner 2 parse buffer */
                if(longmynd_ts_parse_buffer_tuner2.waiting
                    && longmynd_ts_parse_buffer_tuner2.buffer != NULL
                    && pthread_mutex_trylock(&longmynd_ts_parse_buffer_tuner2.mutex) == 0)
                {
                    memcpy(longmynd_ts_parse_buffer_tuner2.buffer, &buffer[2],len-2);
                    longmynd_ts_parse_buffer_tuner2.length = len-2;
                    pthread_cond_signal(&longmynd_ts_parse_buffer_tuner2.signal);
                    longmynd_ts_parse_buffer_tuner2.waiting = false;

                    pthread_mutex_unlock(&longmynd_ts_parse_buffer_tuner2.mutex);
                }
            } else {
                /* Tuner 1: Use original parse buffer */
                if(longmynd_ts_parse_buffer.waiting
                    && longmynd_ts_parse_buffer.buffer != NULL
                    && pthread_mutex_trylock(&longmynd_ts_parse_buffer.mutex) == 0)
                {
                    memcpy(longmynd_ts_parse_buffer.buffer, &buffer[2],len-2);
                    longmynd_ts_parse_buffer.length = len-2;
                    pthread_cond_signal(&longmynd_ts_parse_buffer.signal);
                    longmynd_ts_parse_buffer.waiting = false;

                    pthread_mutex_unlock(&longmynd_ts_parse_buffer.mutex);
                }
            }

            status->ts_packet_count_nolock += (len-2);

            /* Update TS status information */
            pthread_mutex_lock(&status->mutex);

            /* Update total packet count */
            status->ts_packet_count_total += (len-2) / 188; /* 188 bytes per TS packet */

            /* Calculate bitrate every 5 seconds */
            uint64_t current_time_monotonic = monotonic_ms();

            if (status->ts_last_bitrate_calc_monotonic == 0) {
                status->ts_last_bitrate_calc_monotonic = current_time_monotonic;
            } else if ((current_time_monotonic - status->ts_last_bitrate_calc_monotonic) >= 5000) { /* 5 seconds */
                uint64_t time_diff_ms = current_time_monotonic - status->ts_last_bitrate_calc_monotonic;
                uint32_t bytes_received = (len-2);
                if (time_diff_ms > 0) {
                    status->ts_bitrate_kbps = (bytes_received * 8) / time_diff_ms; /* Convert to kbps */
                }
                status->ts_last_bitrate_calc_monotonic = current_time_monotonic;
            }

            /* Update TS lock status - consider locked if we're receiving data */
            status->ts_lock = true;

            pthread_mutex_unlock(&status->mutex);
        }

    }

    free(buffer);

    return NULL;
}

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

static longmynd_status_t *ts_longmynd_status;
static longmynd_status_t *ts_longmynd_status_tuner2;

static void ts_callback_sdt_service(
    uint8_t *service_provider_name_ptr, uint32_t *service_provider_name_length_ptr,
    uint8_t *service_name_ptr, uint32_t *service_name_length_ptr
)
{
    pthread_mutex_lock(&ts_longmynd_status->mutex);

    memcpy(ts_longmynd_status->service_name, service_name_ptr, *service_name_length_ptr);
    ts_longmynd_status->service_name[*service_name_length_ptr] = '\0';

    memcpy(ts_longmynd_status->service_provider_name, service_provider_name_ptr, *service_provider_name_length_ptr);
    ts_longmynd_status->service_provider_name[*service_provider_name_length_ptr] = '\0';

    printf("TS: SDT parsed - Service: '%s', Provider: '%s'\n",
           ts_longmynd_status->service_name, ts_longmynd_status->service_provider_name);

    pthread_mutex_unlock(&ts_longmynd_status->mutex);
}

static void ts_callback_pmt_pids(uint32_t *ts_pmt_index_ptr, uint32_t *ts_pmt_es_pid, uint32_t *ts_pmt_es_type)
{
    pthread_mutex_lock(&ts_longmynd_status->mutex);

    ts_longmynd_status->ts_elementary_streams[*ts_pmt_index_ptr][0] = *ts_pmt_es_pid;
    ts_longmynd_status->ts_elementary_streams[*ts_pmt_index_ptr][1] = *ts_pmt_es_type;

    pthread_mutex_unlock(&ts_longmynd_status->mutex);
}

static void ts_callback_ts_stats(uint32_t *ts_packet_total_count_ptr, uint32_t *ts_null_percentage_ptr)
{
    if(*ts_packet_total_count_ptr > 0)
    {
        pthread_mutex_lock(&ts_longmynd_status->mutex);

        ts_longmynd_status->ts_null_percentage = *ts_null_percentage_ptr;

        pthread_mutex_unlock(&ts_longmynd_status->mutex);
    }
}

/* -------------------------------------------------------------------------------------------------- */
/* Tuner 2 dedicated callback functions                                                               */
/* -------------------------------------------------------------------------------------------------- */

static void ts_callback_sdt_service_tuner2(
    uint8_t *service_provider_name_ptr, uint32_t *service_provider_name_length_ptr,
    uint8_t *service_name_ptr, uint32_t *service_name_length_ptr
)
{
    pthread_mutex_lock(&ts_longmynd_status_tuner2->mutex);

    memcpy(ts_longmynd_status_tuner2->service_name, service_name_ptr, *service_name_length_ptr);
    ts_longmynd_status_tuner2->service_name[*service_name_length_ptr] = '\0';

    memcpy(ts_longmynd_status_tuner2->service_provider_name, service_provider_name_ptr, *service_provider_name_length_ptr);
    ts_longmynd_status_tuner2->service_provider_name[*service_provider_name_length_ptr] = '\0';

    printf("TS: Tuner2 SDT parsed - Service: '%s', Provider: '%s'\n",
           ts_longmynd_status_tuner2->service_name, ts_longmynd_status_tuner2->service_provider_name);

    pthread_mutex_unlock(&ts_longmynd_status_tuner2->mutex);
}

static void ts_callback_pmt_pids_tuner2(uint32_t *ts_pmt_index_ptr, uint32_t *ts_pmt_es_pid, uint32_t *ts_pmt_es_type)
{
    pthread_mutex_lock(&ts_longmynd_status_tuner2->mutex);

    ts_longmynd_status_tuner2->ts_elementary_streams[*ts_pmt_index_ptr][0] = *ts_pmt_es_pid;
    ts_longmynd_status_tuner2->ts_elementary_streams[*ts_pmt_index_ptr][1] = *ts_pmt_es_type;

    pthread_mutex_unlock(&ts_longmynd_status_tuner2->mutex);
}

static void ts_callback_ts_stats_tuner2(uint32_t *ts_packet_total_count_ptr, uint32_t *ts_null_percentage_ptr)
{
    if(*ts_packet_total_count_ptr > 0)
    {
        pthread_mutex_lock(&ts_longmynd_status_tuner2->mutex);

        ts_longmynd_status_tuner2->ts_null_percentage = *ts_null_percentage_ptr;

        pthread_mutex_unlock(&ts_longmynd_status_tuner2->mutex);
    }
}

/* -------------------------------------------------------------------------------------------------- */
void *loop_ts_parse(void *arg) {
/* -------------------------------------------------------------------------------------------------- */
/* Runs a loop to parse the MPEG-TS                                                                   */
/* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars=(thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    *err=ERROR_NONE;
    //longmynd_config_t *config = thread_vars->config;
    ts_longmynd_status = thread_vars->status;

    uint8_t *ts_buffer = malloc(TS_FRAME_SIZE);
    if(ts_buffer == NULL)
    {
        *err=ERROR_TS_BUFFER_MALLOC;
    }

    longmynd_ts_parse_buffer.buffer = ts_buffer;

    struct timespec ts;

    /* Set pthread timer on .signal to use monotonic clock */
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init (&longmynd_ts_parse_buffer.signal, &attr);
    pthread_condattr_destroy(&attr);

    pthread_mutex_lock(&longmynd_ts_parse_buffer.mutex);

    while(*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE)
    {
        longmynd_ts_parse_buffer.waiting = true;

        while(longmynd_ts_parse_buffer.waiting && *thread_vars->main_err_ptr == ERROR_NONE)
        {
            /* Set timer for 100ms */
            clock_gettime(CLOCK_MONOTONIC, &ts);
            timespec_add_ns(&ts, 100 * 1000*1000);

            /* Mutex is unlocked during wait */
            pthread_cond_timedwait(&longmynd_ts_parse_buffer.signal, &longmynd_ts_parse_buffer.mutex, &ts);
        }

        ts_parse(
            &ts_buffer[0], longmynd_ts_parse_buffer.length,
            &ts_callback_sdt_service,
            &ts_callback_pmt_pids,
            &ts_callback_ts_stats,
            false
        );

        pthread_mutex_lock(&ts_longmynd_status->mutex);

        /* Trigger pthread signal */
        pthread_cond_signal(&ts_longmynd_status->signal);

        pthread_mutex_unlock(&ts_longmynd_status->mutex);
    }

    pthread_mutex_unlock(&longmynd_ts_parse_buffer.mutex);

    free(ts_buffer);

    return NULL;
}

/* -------------------------------------------------------------------------------------------------- */
void *loop_ts_parse_tuner2(void *arg) {
/* -------------------------------------------------------------------------------------------------- */
/* Runs a loop to parse the MPEG-TS for tuner 2 (dedicated implementation)                           */
/* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars=(thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    *err=ERROR_NONE;
    //longmynd_config_t *config = thread_vars->config;
    ts_longmynd_status_tuner2 = thread_vars->status;

    uint8_t *ts_buffer = malloc(TS_FRAME_SIZE);
    if(ts_buffer == NULL)
    {
        *err=ERROR_TS_BUFFER_MALLOC;
    }

    longmynd_ts_parse_buffer_tuner2.buffer = ts_buffer;

    struct timespec ts;

    /* Set pthread timer on .signal to use monotonic clock */
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init (&longmynd_ts_parse_buffer_tuner2.signal, &attr);
    pthread_condattr_destroy(&attr);

    pthread_mutex_lock(&longmynd_ts_parse_buffer_tuner2.mutex);

    while(*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE)
    {
        longmynd_ts_parse_buffer_tuner2.waiting = true;

        while(longmynd_ts_parse_buffer_tuner2.waiting && *thread_vars->main_err_ptr == ERROR_NONE)
        {
            /* Set timer for 100ms */
            clock_gettime(CLOCK_MONOTONIC, &ts);
            timespec_add_ns(&ts, 100 * 1000*1000);

            /* Mutex is unlocked during wait */
            pthread_cond_timedwait(&longmynd_ts_parse_buffer_tuner2.signal, &longmynd_ts_parse_buffer_tuner2.mutex, &ts);
        }

        ts_parse(
            &ts_buffer[0], longmynd_ts_parse_buffer_tuner2.length,
            &ts_callback_sdt_service_tuner2,
            &ts_callback_pmt_pids_tuner2,
            &ts_callback_ts_stats_tuner2,
            false
        );

        pthread_mutex_lock(&ts_longmynd_status_tuner2->mutex);

        /* Trigger pthread signal */
        pthread_cond_signal(&ts_longmynd_status_tuner2->signal);

        pthread_mutex_unlock(&ts_longmynd_status_tuner2->mutex);
    }

    pthread_mutex_unlock(&longmynd_ts_parse_buffer_tuner2.mutex);

    free(ts_buffer);

    return NULL;
}
