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
#include <unistd.h>

#include "main.h"
#include "errors.h"
#include "udp.h"
#include "fifo.h"
#include "ftdi.h"
#include "ftdi_usb.h"
#include "ts.h"

#include "libts.h"
#include "stv0910.h"

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
        *err=udp_ts_init(thread_vars->config->ts_ip_addr, thread_vars->config->ts_ip_port);
        ts_write = udp_ts_write;
    } else {
        *err=fifo_ts_init(thread_vars->config->ts_fifo_path, &fifo_ready);
        ts_write = fifo_ts_write;
    }

    while(*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE){
        /* If reset flag is active (eg. just started or changed station), then clear out the ts buffer */
        if(config->ts_reset) {
            do {
                if (*err==ERROR_NONE) *err=ftdi_usb_ts_read(buffer, &len, TS_FRAME_SIZE);
            } while (*err==ERROR_NONE && len>2);

            pthread_mutex_lock(&status->mutex);

            status->service_name[0] = '\0';
            status->service_provider_name[0] = '\0';
            status->ts_null_percentage = 100;
            status->ts_packet_count_nolock = 0;

            for (int j=0; j<NUM_ELEMENT_STREAMS; j++) {
                status->ts_elementary_streams[j][0] = 0;
            }

            pthread_mutex_unlock(&status->mutex);

           config->ts_reset = false;
        }


        *err=ftdi_usb_ts_read(buffer, &len, TS_FRAME_SIZE);

        //if(len>2) fprintf(stderr,"len %d\n",len);
        /* if there is ts data then we send it out to the required output. But, we have to lose the first 2 bytes */
        /* that are the usual FTDI 2 byte response and not part of the TS */
        if ((*err==ERROR_NONE) && (len>2)) {

/*
            uint32_t matype1,matype2;
            pthread_mutex_lock(&status->mutex);
         stv0910_read_matype(1, &matype1,&matype2);
         pthread_mutex_unlock(&status->mutex);
         */

        if(thread_vars->config->ts_use_ip && (status->matype1&0xC0)>>6 == 3)
        {


            ts_write = udp_ts_write;
            //ts_write = udp_bb_write;
        }
        if(thread_vars->config->ts_use_ip && (status->matype1&0xC0)>>6 == 1)
        {
            ts_write = udp_bb_write;
        }


            if(thread_vars->config->ts_use_ip || fifo_ready)
            {
                *err=ts_write(&buffer[2],len-2,&fifo_ready);
            }
            else if(!thread_vars->config->ts_use_ip && !fifo_ready)
            {
                /* Try opening the fifo again */
                *err=fifo_ts_init(thread_vars->config->ts_fifo_path, &fifo_ready);
            }

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

            status->ts_packet_count_nolock += (len-2);
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
static longmynd_status_t *ts_longmynd_status2;

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

/* Second tuner callback function declarations */
static void ts_callback_sdt_service2(
    uint8_t *service_provider_name_ptr, uint32_t *service_provider_name_length_ptr,
    uint8_t *service_name_ptr, uint32_t *service_name_length_ptr
);

static void ts_callback_pmt_pids2(uint32_t *ts_pmt_index_ptr, uint32_t *ts_pmt_es_pid, uint32_t *ts_pmt_es_type);

static void ts_callback_ts_stats2(uint32_t *ts_packet_total_count_ptr, uint32_t *ts_null_percentage_ptr);

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
/* Second tuner TS buffer management                                                                  */
/* -------------------------------------------------------------------------------------------------- */
uint8_t *ts_buffer_ptr2 = NULL;
bool ts_buffer_waiting2;

typedef struct {
    uint8_t *buffer;
    uint32_t length;
    bool waiting;
    pthread_mutex_t mutex;
    pthread_cond_t signal;
} longmynd_ts_parse_buffer2_t;

static longmynd_ts_parse_buffer2_t longmynd_ts_parse_buffer2 = {
    .buffer = NULL,
    .length = 0,
    .waiting = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .signal = PTHREAD_COND_INITIALIZER
};

/* -------------------------------------------------------------------------------------------------- */
void *loop_ts_tuner2(void *arg) {
/* -------------------------------------------------------------------------------------------------- */
/* Runs a loop to read the transport stream from the second tuner                                     */
/* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars=(thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    *err=ERROR_NONE;
    longmynd_config_t *config = thread_vars->config;
    longmynd_status_t *status = thread_vars->status;

    uint8_t *buffer;
    uint16_t len;
    bool fifo_ready = false;
    uint8_t (*ts_write)(uint8_t *, uint32_t, bool *);

    printf("Flow: Starting second tuner TS loop\n");

    buffer = malloc(TS_FRAME_SIZE);
    if(buffer == NULL)
    {
        *err=ERROR_TS_BUFFER_MALLOC;
    }

    if(thread_vars->config->ts2_use_ip) {
        *err=udp_ts2_init(thread_vars->config->ts2_ip_addr, thread_vars->config->ts2_ip_port);
        ts_write = udp_ts2_write;
    } else {
        // For now, second tuner only supports UDP streaming
        printf("ERROR: Second tuner only supports UDP streaming\n");
        *err = ERROR_ARGS_INPUT;
    }

    while(*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE){
        /* Only proceed if second tuner is enabled and initialized */
        if (!config->dual_tuner_enabled || !second_ftdi_initialized) {
            usleep(100000); /* 100ms */
            continue;
        }

        /* If reset flag is active, clear out the ts buffer */
        if(config->ts2_reset) {
            do {
                if (*err==ERROR_NONE) *err=ftdi_usb_ts_read2(buffer, &len, TS_FRAME_SIZE);
            } while (*err==ERROR_NONE && len>2);

            pthread_mutex_lock(&status->mutex);

            status->service_name2[0] = '\0';
            status->service_provider_name2[0] = '\0';
            status->ts_null_percentage2 = 100;
            status->ts2_packet_count_nolock = 0;

            for (int j=0; j<NUM_ELEMENT_STREAMS; j++) {
                status->ts_elementary_streams2[j][0] = 0;
            }

            pthread_mutex_unlock(&status->mutex);

           config->ts2_reset = false;
        }

        *err=ftdi_usb_ts_read2(buffer, &len, TS_FRAME_SIZE);

        /* if there is ts data then we send it out to the required output. But, we have to lose the first 2 bytes */
        /* that are the usual FTDI 2 byte response and not part of the TS */
        if ((*err==ERROR_NONE) && (len>2)) {

            // Handle different stream types based on MATYPE for tuner 2
            if(thread_vars->config->ts2_use_ip && (status->matype1_2&0xC0)>>6 == 3)
            {
                ts_write = udp_ts2_write;
            }
            if(thread_vars->config->ts2_use_ip && (status->matype1_2&0xC0)>>6 == 1)
            {
                ts_write = udp_bb2_write; // Would need to implement this function
            }

            if(thread_vars->config->ts2_use_ip)
            {
                *err=ts_write(&buffer[2],len-2,&fifo_ready);
            }

            // Send data to TS parser for tuner 2
            if(longmynd_ts_parse_buffer2.waiting
                && longmynd_ts_parse_buffer2.buffer != NULL
                && pthread_mutex_trylock(&longmynd_ts_parse_buffer2.mutex) == 0)
            {
                memcpy(longmynd_ts_parse_buffer2.buffer, &buffer[2],len-2);
                longmynd_ts_parse_buffer2.length = len-2;
                pthread_cond_signal(&longmynd_ts_parse_buffer2.signal);
                longmynd_ts_parse_buffer2.waiting = false;

                pthread_mutex_unlock(&longmynd_ts_parse_buffer2.mutex);
            }

            status->ts2_packet_count_nolock += (len-2);
        }
    }

    free(buffer);
    printf("Flow: Second tuner TS loop exiting\n");
    return NULL;
}

/* -------------------------------------------------------------------------------------------------- */
void *loop_ts_parse_tuner2(void *arg) {
/* -------------------------------------------------------------------------------------------------- */
/* Runs a loop to parse the MPEG-TS from the second tuner                                             */
/* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars=(thread_vars_t *)arg;
    uint8_t *err = &thread_vars->thread_err;
    *err=ERROR_NONE;
    ts_longmynd_status2 = thread_vars->status;

    uint8_t *ts_buffer2 = malloc(TS_FRAME_SIZE);
    if(ts_buffer2 == NULL)
    {
        *err=ERROR_TS_BUFFER_MALLOC;
    }

    printf("Flow: Starting second tuner TS parser\n");

    /* Allocate buffer for TS parsing */
    longmynd_ts_parse_buffer2.buffer = malloc(TS_FRAME_SIZE);
    if(longmynd_ts_parse_buffer2.buffer == NULL)
    {
        *err=ERROR_TS_BUFFER_MALLOC;
    }

    pthread_mutex_lock(&longmynd_ts_parse_buffer2.mutex);

    struct timespec ts;

    while(*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE)
    {
        /* Only proceed if second tuner is enabled and initialized */
        if (!thread_vars->config->dual_tuner_enabled || !second_ftdi_initialized) {
            usleep(100000); /* 100ms */
            continue;
        }

        longmynd_ts_parse_buffer2.waiting = true;

        while(longmynd_ts_parse_buffer2.waiting && *thread_vars->main_err_ptr == ERROR_NONE)
        {
            /* Set timer for 100ms */
            clock_gettime(CLOCK_MONOTONIC, &ts);
            timespec_add_ns(&ts, 100 * 1000*1000);

            /* Mutex is unlocked during wait */
            pthread_cond_timedwait(&longmynd_ts_parse_buffer2.signal, &longmynd_ts_parse_buffer2.mutex, &ts);
        }

        if (longmynd_ts_parse_buffer2.length > 0) {
            memcpy(ts_buffer2, longmynd_ts_parse_buffer2.buffer, longmynd_ts_parse_buffer2.length);

            // Parse TS data for tuner 2 - would need separate callback functions for tuner 2
            ts_parse(
                &ts_buffer2[0], longmynd_ts_parse_buffer2.length,
                &ts_callback_sdt_service2,  // Would need to implement
                &ts_callback_pmt_pids2,     // Would need to implement
                &ts_callback_ts_stats2,     // Would need to implement
                false
            );

            pthread_mutex_lock(&ts_longmynd_status2->mutex);

            /* Trigger pthread signal */
            pthread_cond_signal(&ts_longmynd_status2->signal);

            pthread_mutex_unlock(&ts_longmynd_status2->mutex);
        }
    }

    pthread_mutex_unlock(&longmynd_ts_parse_buffer2.mutex);

    if (longmynd_ts_parse_buffer2.buffer) {
        free(longmynd_ts_parse_buffer2.buffer);
        longmynd_ts_parse_buffer2.buffer = NULL;
    }

    free(ts_buffer2);
    printf("Flow: Second tuner TS parser exiting\n");
    return NULL;
}

/* -------------------------------------------------------------------------------------------------- */
/* Second tuner TS callback functions                                                                 */
/* -------------------------------------------------------------------------------------------------- */

static void ts_callback_sdt_service2(
    uint8_t *service_provider_name_ptr, uint32_t *service_provider_name_length_ptr,
    uint8_t *service_name_ptr, uint32_t *service_name_length_ptr
)
{
    pthread_mutex_lock(&ts_longmynd_status2->mutex);

    memcpy(ts_longmynd_status2->service_name2, service_name_ptr, *service_name_length_ptr);
    ts_longmynd_status2->service_name2[*service_name_length_ptr] = '\0';

    memcpy(ts_longmynd_status2->service_provider_name2, service_provider_name_ptr, *service_provider_name_length_ptr);
    ts_longmynd_status2->service_provider_name2[*service_provider_name_length_ptr] = '\0';

    pthread_mutex_unlock(&ts_longmynd_status2->mutex);
}

static void ts_callback_pmt_pids2(uint32_t *ts_pmt_index_ptr, uint32_t *ts_pmt_es_pid, uint32_t *ts_pmt_es_type)
{
    pthread_mutex_lock(&ts_longmynd_status2->mutex);

    ts_longmynd_status2->ts_elementary_streams2[*ts_pmt_index_ptr][0] = *ts_pmt_es_pid;
    ts_longmynd_status2->ts_elementary_streams2[*ts_pmt_index_ptr][1] = *ts_pmt_es_type;

    pthread_mutex_unlock(&ts_longmynd_status2->mutex);
}

static void ts_callback_ts_stats2(uint32_t *ts_packet_total_count_ptr, uint32_t *ts_null_percentage_ptr)
{
    if(*ts_packet_total_count_ptr > 0)
    {
        pthread_mutex_lock(&ts_longmynd_status2->mutex);

        ts_longmynd_status2->ts_null_percentage2 = *ts_null_percentage_ptr;

        pthread_mutex_unlock(&ts_longmynd_status2->mutex);
    }
}
