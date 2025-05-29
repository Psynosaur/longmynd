/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: main.c                                                                      */
/*    - an implementation of the Serit NIM controlling software for the MiniTiouner Hardware          */
/*    - the top level (main) and command line procesing                                               */
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

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- INCLUDES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "main.h"
#include "ftdi.h"
#include "stv0910.h"
#include "stv0910_regs.h"
#include "stv0910_utils.h"
#include "stv6120.h"
#include "stvvglna.h"
#include "nim.h"
#include "errors.h"
#include "fifo.h"
#include "ftdi_usb.h"
#include "udp.h"
#include "beep.h"
#include "ts.h"
#include "mymqtt.h"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- DEFINES ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

/* Milliseconds between each i2c control loop */
#define I2C_LOOP_MS 500

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

static longmynd_config_t longmynd_config;
/* = {

    freq_index : 0,
    sr_index : 0,
    new_config : false,
    mutex : PTHREAD_MUTEX_INITIALIZER
    };
*/

static longmynd_status_t longmynd_status;
/* = {
    .service_name = {'\0'},
    .service_provider_name = {'\0'},
    .last_updated_monotonic = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .signal = PTHREAD_COND_INITIALIZER,
    .ts_packet_count_nolock = 0};
*/

/* Dual-tuner status structures */
static longmynd_status_t longmynd_status_tuner1;
static longmynd_status_t longmynd_status_tuner2;

static pthread_t thread_ts_parse;
static pthread_t thread_ts;
static pthread_t thread_i2c;
static pthread_t thread_beep;

/* Dual-tuner threads */
static pthread_t thread_ts_tuner2;
static pthread_t thread_ts_parse_tuner2;
static pthread_t thread_i2c_tuner2;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* NB: This overwrites any multiple-frequency config */
void config_set_frequency(uint32_t frequency)
{
    if (frequency <= 2450000 && frequency >= 144000)
    {
        pthread_mutex_lock(&longmynd_config.mutex);

        longmynd_config.freq_requested[0] = frequency;
        longmynd_config.freq_requested[1] = 0;
        longmynd_config.freq_requested[2] = 0;
        longmynd_config.freq_requested[3] = 0;
        longmynd_config.freq_index = 0;
        longmynd_config.new_config = true;

        pthread_mutex_unlock(&longmynd_config.mutex);
    }
}

/* NB: This overwrites any multiple-symbolrate config */
void config_set_symbolrate(uint32_t symbolrate)
{
    if (symbolrate <= 27500 && symbolrate >= 33)
    {
        pthread_mutex_lock(&longmynd_config.mutex);

        longmynd_config.sr_requested[0] = symbolrate;
        longmynd_config.sr_requested[1] = 0;
        longmynd_config.sr_requested[2] = 0;
        longmynd_config.sr_requested[3] = 0;
        longmynd_config.sr_index = 0;
        longmynd_config.new_config = true;

        pthread_mutex_unlock(&longmynd_config.mutex);
    }
}

/* NB: This overwrites any multiple-frequency or multiple-symbolrate config */
void config_set_frequency_and_symbolrate(uint32_t frequency, uint32_t symbolrate)
{
    if (frequency <= 2450000 && frequency >= 144000 && symbolrate <= 27500 && symbolrate >= 33)
    {
        pthread_mutex_lock(&longmynd_config.mutex);

        longmynd_config.freq_requested[0] = frequency;
        longmynd_config.freq_requested[1] = 0;
        longmynd_config.freq_requested[2] = 0;
        longmynd_config.freq_requested[3] = 0;
        longmynd_config.freq_index = 0;

        longmynd_config.sr_requested[0] = symbolrate;
        longmynd_config.sr_requested[1] = 0;
        longmynd_config.sr_requested[2] = 0;
        longmynd_config.sr_requested[3] = 0;
        longmynd_config.sr_index = 0;

        longmynd_config.new_config = true;

        pthread_mutex_unlock(&longmynd_config.mutex);
    }
}

void config_set_lnbv(bool enabled, bool horizontal)
{
    pthread_mutex_lock(&longmynd_config.mutex);

    longmynd_config.polarisation_supply = enabled;
    longmynd_config.polarisation_horizontal = horizontal;
    longmynd_config.new_config = true;

    pthread_mutex_unlock(&longmynd_config.mutex);
}

void config_set_swport(bool sport)
{
    pthread_mutex_lock(&longmynd_config.mutex);

    printf("swport: %d\n",sport);
    longmynd_config.port_swap = sport;
    longmynd_config.new_config = true;

    pthread_mutex_unlock(&longmynd_config.mutex);
}

void config_set_tsip(char *tsip)
{
    pthread_mutex_lock(&longmynd_config.mutex);

    strcpy(longmynd_config.ts_ip_addr, tsip);
    udp_ts_init(tsip,1234);
    longmynd_config.new_config = true;

    pthread_mutex_unlock(&longmynd_config.mutex);
}

void config_reinit(bool increment_frsr)
{
    pthread_mutex_lock(&longmynd_config.mutex);

    if (increment_frsr)
    {
        /* Cycle symbolrate for a given frequency */
        do
        {
            /* Increment modulus 4 */
            longmynd_config.sr_index = (longmynd_config.sr_index + 1) & 0x3;
            /* Check if we've just cycled all symbolrates */
            if (longmynd_config.sr_index == 0)
            {
                /* Cycle frequences once we've tried all symbolrates */
                do
                {
                    /* Increment modulus 4 */
                    longmynd_config.freq_index = (longmynd_config.freq_index + 1) & 0x3;
                } while (longmynd_config.freq_requested[longmynd_config.freq_index] == 0);
            }
        } while (longmynd_config.sr_requested[longmynd_config.sr_index] == 0);
    }

    longmynd_config.new_config = true;

    pthread_mutex_unlock(&longmynd_config.mutex);

    if (increment_frsr)
    {
        printf("Flow: Config cycle: Frequency [%d] = %d KHz, Symbol Rate [%d] = %d KSymbols/s\n",
               longmynd_config.freq_index, longmynd_config.freq_requested[longmynd_config.freq_index],
               longmynd_config.sr_index, longmynd_config.sr_requested[longmynd_config.sr_index]);
    }
}

/* -------------------------------------------------------------------------------------------------- */
/* Dual-tuner configuration functions                                                                */
/* -------------------------------------------------------------------------------------------------- */

/* NB: This overwrites any multiple-frequency config for tuner 2 */
void config_set_frequency_tuner2(uint32_t frequency)
{
    if (frequency <= 2450000 && frequency >= 144000)
    {
        pthread_mutex_lock(&longmynd_config.mutex);

        longmynd_config.freq_requested_tuner2[0] = frequency;
        longmynd_config.freq_requested_tuner2[1] = 0;
        longmynd_config.freq_requested_tuner2[2] = 0;
        longmynd_config.freq_requested_tuner2[3] = 0;
        longmynd_config.freq_index_tuner2 = 0;
        longmynd_config.new_config_tuner2 = true;

        pthread_mutex_unlock(&longmynd_config.mutex);
    }
}

/* NB: This overwrites any multiple-symbolrate config for tuner 2 */
void config_set_symbolrate_tuner2(uint32_t symbolrate)
{
    if (symbolrate <= 27500 && symbolrate >= 33)
    {
        pthread_mutex_lock(&longmynd_config.mutex);

        longmynd_config.sr_requested_tuner2[0] = symbolrate;
        longmynd_config.sr_requested_tuner2[1] = 0;
        longmynd_config.sr_requested_tuner2[2] = 0;
        longmynd_config.sr_requested_tuner2[3] = 0;
        longmynd_config.sr_index_tuner2 = 0;
        longmynd_config.new_config_tuner2 = true;

        pthread_mutex_unlock(&longmynd_config.mutex);
    }
}

/* NB: This overwrites any multiple-frequency or multiple-symbolrate config for tuner 2 */
void config_set_frequency_and_symbolrate_tuner2(uint32_t frequency, uint32_t symbolrate)
{
    if (frequency <= 2450000 && frequency >= 144000 && symbolrate <= 27500 && symbolrate >= 33)
    {
        pthread_mutex_lock(&longmynd_config.mutex);

        longmynd_config.freq_requested_tuner2[0] = frequency;
        longmynd_config.freq_requested_tuner2[1] = 0;
        longmynd_config.freq_requested_tuner2[2] = 0;
        longmynd_config.freq_requested_tuner2[3] = 0;
        longmynd_config.freq_index_tuner2 = 0;

        longmynd_config.sr_requested_tuner2[0] = symbolrate;
        longmynd_config.sr_requested_tuner2[1] = 0;
        longmynd_config.sr_requested_tuner2[2] = 0;
        longmynd_config.sr_requested_tuner2[3] = 0;
        longmynd_config.sr_index_tuner2 = 0;

        longmynd_config.new_config_tuner2 = true;

        pthread_mutex_unlock(&longmynd_config.mutex);
    }
}

void config_set_lnbv_tuner2(bool enabled, bool horizontal)
{
    pthread_mutex_lock(&longmynd_config.mutex);

    longmynd_config.polarisation_supply_tuner2 = enabled;
    longmynd_config.polarisation_horizontal_tuner2 = horizontal;
    longmynd_config.new_config_tuner2 = true;

    pthread_mutex_unlock(&longmynd_config.mutex);
}

void config_reinit_tuner2(bool increment_frsr)
{
    pthread_mutex_lock(&longmynd_config.mutex);

    if (increment_frsr)
    {
        /* Cycle symbolrate for a given frequency for tuner 2 */
        do
        {
            /* Increment modulus 4 */
            longmynd_config.sr_index_tuner2 = (longmynd_config.sr_index_tuner2 + 1) & 0x3;
            /* Check if we've just cycled all symbolrates */
            if (longmynd_config.sr_index_tuner2 == 0)
            {
                /* Cycle frequences once we've tried all symbolrates */
                do
                {
                    /* Increment modulus 4 */
                    longmynd_config.freq_index_tuner2 = (longmynd_config.freq_index_tuner2 + 1) & 0x3;
                } while (longmynd_config.freq_requested_tuner2[longmynd_config.freq_index_tuner2] == 0);
            }
        } while (longmynd_config.sr_requested_tuner2[longmynd_config.sr_index_tuner2] == 0);
    }

    longmynd_config.new_config_tuner2 = true;

    pthread_mutex_unlock(&longmynd_config.mutex);
}

/* -------------------------------------------------------------------------------------------------- */
uint64_t monotonic_ms(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Returns current value of a monotonic timer in milliseconds                                         */
    /* return: monotonic timer in milliseconds                                                            */
    /* -------------------------------------------------------------------------------------------------- */
    struct timespec tp;

    if (clock_gettime(CLOCK_MONOTONIC, &tp) != 0)
    {
        return 0;
    }

    return (uint64_t)tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t process_command_line(int argc, char *argv[], longmynd_config_t *config)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* processes the command line arguments, sets up the parameters in main from them and error checks    */
    /* All the required parameters are passed in                                                          */
    /* return: error code                                                                                 */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t param;
    bool main_usb_set = false;
    bool ts_ip_set = false;
    bool ts_fifo_set = false;
    bool status_ip_set = false;
    bool status_mqtt_set = false;
    bool status_fifo_set = false;

    /* Defaults */
    config->port_swap = false;
    config->halfscan_ratio = 1.5;
    config->beep_enabled = false;
    config->device_usb_addr = 0;
    config->device_usb_bus = 0;

    // Dual-tuner defaults
    config->dual_tuner_enabled = false;
    config->device2_usb_bus = 0;
    config->device2_usb_addr = 0;
    config->auto_detect_second_device = false;
    strcpy(config->ts2_ip_addr, "230.0.0.3");
    config->ts2_ip_port = 1234;

    // Initialize dual-tuner configuration arrays
    for (int i = 0; i < 4; i++) {
        config->freq_requested_tuner2[i] = 0;
        config->sr_requested_tuner2[i] = 0;
    }
    config->freq_index_tuner2 = 0;
    config->sr_index_tuner2 = 0;
    config->polarisation_supply_tuner2 = config->polarisation_supply;  // Copy from main tuner
    config->polarisation_horizontal_tuner2 = config->polarisation_horizontal;  // Copy from main tuner
    config->new_config_tuner2 = false;

    config->ts_use_ip = false;
    config->status_use_mqtt = false;
    strcpy(config->ts_fifo_path, "longmynd_main_ts");
    config->status_use_ip = false;
    strcpy(config->status_fifo_path, "longmynd_main_status");
    config->polarisation_supply = false;
    char polarisation_str[8];
    config->ts_timeout = 50 * 1000;

    param = 1;
    while (param < argc - 2)
    {
        if (argv[param][0] == '-')
        {
            switch (argv[param++][1])
            {
            case 'u':
                config->device_usb_bus = (uint8_t)strtol(argv[param++], NULL, 10);
                config->device_usb_addr = (uint8_t)strtol(argv[param], NULL, 10);
                main_usb_set = true;
                break;
            case 'i':
                strncpy(config->ts_ip_addr, argv[param++], (16 - 1));
                config->ts_ip_port = (uint16_t)strtol(argv[param], NULL, 10);
                config->ts_use_ip = true;
                ts_ip_set = true;
                break;
            case 't':
                strncpy(config->ts_fifo_path, argv[param], (128 - 1));
                ts_fifo_set = true;
                break;
            case 'I':
                strncpy(config->status_ip_addr, argv[param++], (16 - 1));
                config->status_ip_port = (uint16_t)strtol(argv[param], NULL, 10);
                config->status_use_ip = true;
                status_ip_set = true;
                break;
            case 'M':
                strncpy(config->status_ip_addr, argv[param++], (16 - 1));
                config->status_ip_port = (uint16_t)strtol(argv[param], NULL, 10);
                config->status_use_mqtt = true;
                status_mqtt_set = true;
                break;
            case 's':
                strncpy(config->status_fifo_path, argv[param], (128 - 1));
                status_fifo_set = true;
                break;
            case 'p':
                strncpy(polarisation_str, argv[param], (8 - 1));
                config->polarisation_supply = true;
                break;
            case 'w':
                config->port_swap = true;
                param--; /* there is no data for this so go back */
                break;
            case 'S':
                config->halfscan_ratio = strtof(argv[param], NULL);
                break;
            case 'b':
                config->beep_enabled = true;
                param--; /* there is no data for this so go back */
                break;
            case 'r':
                config->ts_timeout = strtol(argv[param], NULL, 10);
                break;
            case 'd':
                config->dual_tuner_enabled = true;
                param--; /* there is no data for this so go back */
                break;
            case 'D':
                config->dual_tuner_enabled = true;
                config->auto_detect_second_device = true;
                param--; /* there is no data for this so go back */
                break;
            case 'U':
                config->device2_usb_bus = (uint8_t)strtol(argv[param++], NULL, 10);
                config->device2_usb_addr = (uint8_t)strtol(argv[param], NULL, 10);
                config->dual_tuner_enabled = true;
                break;
            case 'j':
                strncpy(config->ts2_ip_addr, argv[param++], (16 - 1));
                config->ts2_ip_port = (uint16_t)strtol(argv[param], NULL, 10);
                /* Only consume IP and port for tuner 2 */
                /* Tuner 2 frequency and symbol rate will be copied from tuner 1 later if not explicitly provided */
                printf("Flow: Tuner 2 TS output configured: IP=%s, Port=%d\n",
                       config->ts2_ip_addr, config->ts2_ip_port);
                /* Mark that we need to copy tuner 1 values later */
                config->freq_requested_tuner2[0] = 0;  /* Will be set later */
                config->sr_requested_tuner2[0] = 0;    /* Will be set later */
                for (int i = 1; i < 4; i++) {
                    config->freq_requested_tuner2[i] = 0;
                    config->sr_requested_tuner2[i] = 0;
                }
                config->freq_index_tuner2 = 0;
                config->sr_index_tuner2 = 0;
                config->dual_tuner_enabled = true;
                break;
            }
        }
        param++;
    }

    if ((argc - param) < 2)
    {
        err = ERROR_ARGS_INPUT;
        printf("ERROR: Main Frequency and Main Symbol Rate not found.\n");
    }

    if (err == ERROR_NONE)
    {
        /* Check Scanwidth */
        if (config->halfscan_ratio < 0.0 || config->halfscan_ratio > 100.0)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: Scan width not valid.\n");
        }
    }

    if (err == ERROR_NONE)
    {
        /* Parse frequencies requested */
        char *arg_ptr = argv[param];
        char *comma_ptr;
        for (int i = 0; (i < 4) && (err == ERROR_NONE); i++)
        {
            /* Look for comma */
            comma_ptr = strchr(arg_ptr, ',');
            if (comma_ptr != NULL)
            {
                /* Set comma to NULL to end string here */
                *comma_ptr = '\0';
            }

            /* Parse up to NULL */
            config->freq_requested[i] = (uint32_t)strtol(arg_ptr, NULL, 10);

            if (config->freq_requested[i] == 0)
            {
                err = ERROR_ARGS_INPUT;
                printf("ERROR: Main Frequency not in a valid format.\n");
            }

            if (comma_ptr == NULL)
            {
                /* No further commas, zero out rest of the config */
                for (i++; i < 4; i++)
                {
                    config->freq_requested[i] = 0;
                }
                /* Implicit drop out of wider loop here */
            }
            else
            {
                /* Move arg_ptr to other side of the comma and carry on */
                arg_ptr = comma_ptr + sizeof(char);
            }
        }
        param++;
    }

    if (err == ERROR_NONE)
    {
        /* Parse Symbolrates requested */
        char *arg_ptr = argv[param];
        char *comma_ptr;
        for (int i = 0; (i < 4) && (err == ERROR_NONE); i++)
        {
            /* Look for comma */
            comma_ptr = strchr(arg_ptr, ',');
            if (comma_ptr != NULL)
            {
                /* Set comma to NULL to end string here */
                *comma_ptr = '\0';
            }

            /* Parse up to NULL */
            config->sr_requested[i] = (uint32_t)strtol(arg_ptr, NULL, 10);

            if (config->sr_requested[0] == 0)
            {
                err = ERROR_ARGS_INPUT;
                printf("ERROR: Main Symbol Rate not in a valid format.\n");
            }

            if (comma_ptr == NULL)
            {
                /* No further commas, zero out rest of the config */
                for (i++; i < 4; i++)
                {
                    config->sr_requested[i] = 0;
                }
                /* Implicit drop out of wider loop here */
            }
            else
            {
                /* Move arg_ptr to other side of the comma and carry on */
                arg_ptr = comma_ptr + sizeof(char);
            }
        }
    }

    /* Copy tuner 1 values to tuner 2 if tuner 2 values weren't provided with -j option */
    if (err == ERROR_NONE && config->dual_tuner_enabled && config->freq_requested_tuner2[0] == 0) {
        printf("Flow: Copying tuner 1 values to tuner 2 (frequency=%d KHz, symbol rate=%d KSymbols/s)\n",
               config->freq_requested[0], config->sr_requested[0]);
        for (int i = 0; i < 4; i++) {
            config->freq_requested_tuner2[i] = config->freq_requested[i];
            config->sr_requested_tuner2[i] = config->sr_requested[i];
        }
        config->freq_index_tuner2 = config->freq_index;
        config->sr_index_tuner2 = config->sr_index;
    }

    /* Process LNB Voltage Supply parameter */
    if (err == ERROR_NONE && config->polarisation_supply)
    {
        if (0 == strcasecmp("h", polarisation_str))
        {
            config->polarisation_horizontal = true;
        }
        else if (0 == strcasecmp("v", polarisation_str))
        {
            config->polarisation_horizontal = false;
        }
        else
        {
            config->polarisation_supply = false;
            err = ERROR_ARGS_INPUT;
            printf("ERROR: Polarisation voltage supply parameter not recognised\n");
        }
    }

    if (err == ERROR_NONE)
    {
        /* Check first frequency given */
        if (config->freq_requested[0] > 2450000)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: Freq (%d) must be <= 2450 MHz\n", config->freq_requested[0]);
        }
        else if (config->freq_requested[0] < 144000)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: Freq (%d) must be >= 144 MHz\n", config->freq_requested[0]);
        }
        else if (config->freq_requested[1] != 0)
        {
            /* A frequency list have been given */
            if (config->ts_timeout == -1)
            {
                err = ERROR_ARGS_INPUT;
                printf("ERROR: TS Timeout must be enabled when multiple frequencies are specified.\n");
            }
            /* Then check the other given frequencies */
            for (int i = 1; (i < 4) && (config->freq_requested[i] != 0); i++)
            {
                if (config->freq_requested[i] > 2450000)
                {
                    err = ERROR_ARGS_INPUT;
                    printf("ERROR: Freq (%d) must be <= 2450 MHz\n", config->freq_requested[i]);
                }
                else if (config->freq_requested[i] < 144000)
                {
                    err = ERROR_ARGS_INPUT;
                    printf("ERROR: Freq (%d) must be >= 144 MHz\n", config->freq_requested[i]);
                }
            }
        }
    }
    if (err == ERROR_NONE)
    {
        /* Check first symbolrate given */
        if (config->sr_requested[0] > 27500)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: SR (%d) must be <= 27 Msymbols/s\n", config->sr_requested[0]);
        }
        else if (config->sr_requested[0] < 33)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: SR (%d) must be >= 33 Ksymbols/s\n", config->sr_requested[0]);
        }
        else if (config->sr_requested[1] != 0)
        {
            /* A symbolrate list has been given */
            if (config->ts_timeout == -1)
            {
                err = ERROR_ARGS_INPUT;
                printf("ERROR: TS Timeout must be enabled when multiple symbolrates are specified.\n");
            }
            /* Then check the other given symbolrates */
            for (int i = 1; (i < 4) && (config->sr_requested[i] != 0); i++)
            {
                if (config->sr_requested[i] > 27500)
                {
                    err = ERROR_ARGS_INPUT;
                    printf("ERROR: SR (%d) must be <= 27 Msymbols/s\n", config->sr_requested[i]);
                }
                else if (config->sr_requested[i] < 33)
                {
                    err = ERROR_ARGS_INPUT;
                    printf("ERROR: SR (%d) must be >= 33 Ksymbols/s\n", config->sr_requested[i]);
                }
            }
        }
    }
    if (err == ERROR_NONE)
    {
        if (ts_ip_set && ts_fifo_set)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: Cannot set TS FIFO and TS IP address\n");
        }
        else if (status_ip_set && status_fifo_set)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: Cannot set Status FIFO and Status IP address\n");
        }
        else if (config->ts_use_ip && config->status_use_ip && (config->ts_ip_port == config->status_ip_port) && (0 == strcmp(config->ts_ip_addr, config->status_ip_addr)))
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: Cannot set Status IP & Port identical to TS IP & Port\n");
        }
        else if (config->ts_timeout != -1 && config->ts_timeout <= 500)
        {
            err = ERROR_ARGS_INPUT;
            printf("ERROR: TS Timeout if enabled must be >500ms.\n");
        }
        else
        { /* err==ERROR_NONE */
            printf("      Status: Main Frequency=%i KHz\n", config->freq_requested[0]);
            for (int i = 1; (i < 4) && (config->freq_requested[i] != 0); i++)
            {
                printf("              Alternative Frequency=%i KHz\n", config->freq_requested[i]);
            }
            printf("              Main Symbol Rate=%i KSymbols/s\n", config->sr_requested[0]);
            for (int i = 1; (i < 4) && (config->sr_requested[i] != 0); i++)
            {
                printf("              Alternative Symbol Rate=%i KSymbols/s\n", config->sr_requested[i]);
            }
            if (!main_usb_set)
                printf("              Using First Minitiouner detected on USB\n");
            else
                printf("              USB bus/device=%i,%i\n", config->device_usb_bus, config->device_usb_addr);

            // Dual-tuner configuration display
            if (config->dual_tuner_enabled) {
                printf("              Dual-tuner mode enabled\n");
                if (config->auto_detect_second_device) {
                    printf("              Second device: auto-detect\n");
                } else if (config->device2_usb_bus != 0 || config->device2_usb_addr != 0) {
                    printf("              Second device: USB bus/device=%i,%i\n",
                           config->device2_usb_bus, config->device2_usb_addr);
                }
                printf("              Tuner 2 TS output to IP=%s:%i\n",
                       config->ts2_ip_addr, config->ts2_ip_port);
                printf("              Tuner 2 Frequency=%i KHz\n", config->freq_requested_tuner2[0]);
                for (int i = 1; (i < 4) && (config->freq_requested_tuner2[i] != 0); i++)
                {
                    printf("              Tuner 2 Alternative Frequency=%i KHz\n", config->freq_requested_tuner2[i]);
                }
                printf("              Tuner 2 Symbol Rate=%i KSymbols/s\n", config->sr_requested_tuner2[0]);
                for (int i = 1; (i < 4) && (config->sr_requested_tuner2[i] != 0); i++)
                {
                    printf("              Tuner 2 Alternative Symbol Rate=%i KSymbols/s\n", config->sr_requested_tuner2[i]);
                }
            }

            if (!config->ts_use_ip)
                printf("              Main TS output to FIFO=%s\n", config->ts_fifo_path);
            else
                printf("              Main TS output to IP=%s:%i\n", config->ts_ip_addr, config->ts_ip_port);
            if (!config->status_use_ip)
                printf("              Main Status output to FIFO=%s\n", config->status_fifo_path);
            else
                printf("              Main Status output to IP=%s:%i\n", config->status_ip_addr, config->status_ip_port);
            if (config->port_swap)
                printf("              NIM inputs are swapped (Main now refers to BOTTOM F-Type\n");
            else
                printf("              Main refers to TOP F-Type\n");
            if (config->beep_enabled)
                printf("              MER Beep enabled\n");
            if (config->polarisation_supply)
                printf("              Polarisation Voltage Supply enabled: %s\n", (config->polarisation_horizontal ? "H, 18V" : "V, 13V"));
            if (config->ts_timeout != -1)
                printf("              TS Timeout Period =%i milliseconds\n", config->ts_timeout);
            else
                printf("              TS Timeout Disabled.\n");
        }
    }

    if (err != ERROR_NONE)
    {
        printf("Please refer to the longmynd manual page via:\n");
        printf("    man -l longmynd.1\n");
    }

    config->new_config = true;

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t do_report(longmynd_status_t *status)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* interrogates the demodulator to find the interesting info to report (single tuner mode)          */
    /*  status: the state struct                                                                          */
    /* return: error code                                                                                 */
    /* -------------------------------------------------------------------------------------------------- */
    return do_report_dual(status, STV0910_DEMOD_TOP);
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t do_report_dual(longmynd_status_t *status, uint8_t demod)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* interrogates the demodulator to find the interesting info to report (dual-tuner aware)           */
    /*  status: the state struct                                                                          */
    /*  demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator to read                      */
    /* return: error code                                                                                 */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    /* LNAs if present */
    if (status->lna_ok)
    {
        uint8_t lna_gain, lna_vgo;
        if (err == ERROR_NONE) {
            /* Use appropriate LNA input based on demodulator */
            uint8_t lna_input = (demod == STV0910_DEMOD_TOP) ? NIM_INPUT_TOP : NIM_INPUT_BOTTOM;
            stvvglna_read_agc(lna_input, &lna_gain, &lna_vgo);
        }
        status->lna_gain = (lna_gain << 5) | lna_vgo;
    }

    /* AGC1 Gain */
    if (err == ERROR_NONE)
        err = stv0910_read_agc1_gain(demod, &status->agc1_gain);

    /* AGC2 Gain */
    if (err == ERROR_NONE)
        err = stv0910_read_agc2_gain(demod, &status->agc2_gain);

    /* I,Q powers */
    if (err == ERROR_NONE)
        err = stv0910_read_power(demod, &status->power_i, &status->power_q);

    /* constellations */
    if (err == ERROR_NONE)
    {
        for (uint8_t count = 0; (err == ERROR_NONE && count < NUM_CONSTELLATIONS); count++)
        {
            err = stv0910_read_constellation(demod, &status->constellation[count][0], &status->constellation[count][1]);
        }
    }

    /* puncture rate */
    if (err == ERROR_NONE)
        err = stv0910_read_puncture_rate(demod, &status->puncture_rate);

    /* carrier frequency offset we are trying */
    if (err == ERROR_NONE)
        err = stv0910_read_car_freq(demod, &status->frequency_offset);

    /* symbol rate we are trying */
    if (err == ERROR_NONE)
        err = stv0910_read_sr(demod, &status->symbolrate);

    /* viterbi error rate */
    if (err == ERROR_NONE)
        err = stv0910_read_err_rate(demod, &status->viterbi_error_rate);

    /* BER */
    if (err == ERROR_NONE)
        err = stv0910_read_ber(demod, &status->bit_error_rate);

    /* BCH Uncorrected Flag */
    if (err == ERROR_NONE)
        err = stv0910_read_errors_bch_uncorrected(demod, &status->errors_bch_uncorrected);

    /* BCH Error Count */
    if (err == ERROR_NONE)
        err = stv0910_read_errors_bch_count(demod, &status->errors_bch_count);

    /* LDPC Error Count */
    if (err == ERROR_NONE)
        err = stv0910_read_errors_ldpc_count(demod, &status->errors_ldpc_count);

    if (err == ERROR_NONE)
        err = stv0910_read_matype(demod, &status->matype1,&status->matype2);

    /* MER */
    if (status->state == STATE_DEMOD_S || status->state == STATE_DEMOD_S2)
    {
        if (err == ERROR_NONE)
            err = stv0910_read_mer(demod, &status->modulation_error_rate);
    }
    else
    {
        status->modulation_error_rate = 0;
    }

    /* MODCOD, Short Frames, Pilots */
    if (err == ERROR_NONE)
        err = stv0910_read_modcod_and_type(demod, &status->modcod, &status->short_frame, &status->pilots,&status->rolloff);
    if (status->state != STATE_DEMOD_S2)
    {
        /* short frames & pilots only valid for S2 DEMOD state */
        status->short_frame = 0;
        status->pilots = 0;
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
void *loop_i2c(void *arg)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Runs a loop to configure and monitor the Minitiouner Receiver                                      */
    /*  Configuration is read from the configuration struct                                               */
    /*  Status is written to the status struct                                                            */
    /* -------------------------------------------------------------------------------------------------- */
    thread_vars_t *thread_vars = (thread_vars_t *)arg;
    longmynd_status_t *status = (longmynd_status_t *)thread_vars->status;
    uint8_t *err = &thread_vars->thread_err;

    *err = ERROR_NONE;

    longmynd_config_t config_cpy;
    longmynd_status_t status_cpy;

    uint32_t last_ts_packet_count = 0;

    uint64_t last_i2c_loop = monotonic_ms();
    while (*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE)
    {
        /* Receiver State Machine Loop Timer */
        do
        {
            /* Sleep for at least 10ms */
            usleep(100 * 1000);
        } while (monotonic_ms() < (last_i2c_loop + I2C_LOOP_MS));

        status_cpy.last_ts_or_reinit_monotonic = 0;

        /* Initialize status_cpy.state from current status to prevent uninitialized state machine errors */
        pthread_mutex_lock(&status->mutex);
        status_cpy.state = status->state;
        pthread_mutex_unlock(&status->mutex);



        /* Check if there's a new config - handle both main and tuner 2 config changes */
        if (thread_vars->config->new_config ||
            (config_cpy.dual_tuner_enabled && thread_vars->tuner_id == 2 && thread_vars->config->new_config_tuner2))
        {
            fprintf(stderr,"New Config !!!!!!!!!\n");
            /* Lock config struct */
            pthread_mutex_lock(&thread_vars->config->mutex);
            /* Clone status struct locally */
            memcpy(&config_cpy, thread_vars->config, sizeof(longmynd_config_t));
            /* Clear appropriate new config flag */
            if (thread_vars->tuner_id == 1) {
                thread_vars->config->new_config = false;
            } else if (thread_vars->tuner_id == 2) {
                thread_vars->config->new_config_tuner2 = false;
            }
            /* Set flag to clear ts buffer */
            thread_vars->config->ts_reset = true;
            pthread_mutex_unlock(&thread_vars->config->mutex);

            /* Set tuner-specific frequency and symbol rate */
            if (config_cpy.dual_tuner_enabled && thread_vars->tuner_id == 2) {
                /* Tuner 2: use tuner 2 specific configuration */
                status_cpy.frequency_requested = config_cpy.freq_requested_tuner2[config_cpy.freq_index_tuner2];
                status_cpy.symbolrate_requested = config_cpy.sr_requested_tuner2[config_cpy.sr_index_tuner2];
            } else {
                /* Tuner 1 or single tuner mode: use main configuration */
                status_cpy.frequency_requested = config_cpy.freq_requested[config_cpy.freq_index];
                status_cpy.symbolrate_requested = config_cpy.sr_requested[config_cpy.sr_index];
            }

            uint8_t tuner_err = ERROR_NONE; // Seperate to avoid triggering main() abort on handled tuner error.
            int32_t tuner_lock_attempts = STV6120_PLL_ATTEMPTS;
            do
            {
                /* init all the modules */
                if (*err == ERROR_NONE)
                    *err = nim_init();

                /* CRITICAL: Use dual-tuner initialization sequence if enabled */
                if (*err == ERROR_NONE) {
                    if (config_cpy.dual_tuner_enabled) {
                        /* CRITICAL: Use open_tuner TOP-first initialization sequence */
                        printf("Flow: Using CRITICAL dual-tuner initialization sequence (tuner %d)\n", thread_vars->tuner_id);

                        if (thread_vars->tuner_id == 1) {
                            /* Tuner 1 (TOP demodulator) - initialize with both symbol rates */
                            printf("      Status: Initializing dual demodulators with TOP-first sequence\n");
                            uint32_t sr_tuner1 = config_cpy.sr_requested[config_cpy.sr_index];
                            uint32_t sr_tuner2 = config_cpy.sr_requested_tuner2[config_cpy.sr_index_tuner2];
                            *err = stv0910_init_dual_sequence(sr_tuner1, sr_tuner2);

                            /* Signal that TOP demodulator is ready */
                            if (*err == ERROR_NONE && thread_vars->dual_sync_mutex && thread_vars->dual_sync_cond && thread_vars->top_demod_ready) {
                                pthread_mutex_lock(thread_vars->dual_sync_mutex);
                                *thread_vars->top_demod_ready = true;
                                pthread_cond_broadcast(thread_vars->dual_sync_cond);
                                pthread_mutex_unlock(thread_vars->dual_sync_mutex);
                                printf("      Status: TOP demodulator initialization complete - signaling BOTTOM demodulator\n");
                            }
                        } else {
                            /* Tuner 2 (BOTTOM demodulator) - wait for TOP to be initialized first */
                            printf("      Status: Tuner 2 waiting for TOP demodulator to be stable\n");

                            if (thread_vars->dual_sync_mutex && thread_vars->dual_sync_cond && thread_vars->top_demod_ready) {
                                pthread_mutex_lock(thread_vars->dual_sync_mutex);

                                /* Wait with timeout to prevent infinite hanging */
                                struct timespec timeout;
                                clock_gettime(CLOCK_REALTIME, &timeout);
                                timeout.tv_sec += 10; /* 10 second timeout */

                                int wait_result = 0;
                                while (!*thread_vars->top_demod_ready && wait_result == 0) {
                                    printf("      Status: Waiting for TOP demodulator initialization...\n");
                                    wait_result = pthread_cond_timedwait(thread_vars->dual_sync_cond, thread_vars->dual_sync_mutex, &timeout);
                                }

                                if (wait_result == ETIMEDOUT) {
                                    printf("      WARNING: Timeout waiting for TOP demodulator - proceeding anyway\n");
                                } else if (*thread_vars->top_demod_ready) {
                                    printf("      Status: TOP demodulator ready - proceeding with BOTTOM demodulator\n");
                                }

                                pthread_mutex_unlock(thread_vars->dual_sync_mutex);
                            } else {
                                /* Fallback to time-based delay if synchronization not available */
                                printf("      Status: Using fallback delay for TOP demodulator stability\n");
                                usleep(100000); /* 100ms delay */
                            }
                            /* No additional STV0910 initialization needed for tuner 2 */
                        }
                    } else {
                        /* Single tuner mode - original initialization */
                        *err = stv0910_init(config_cpy.sr_requested[config_cpy.sr_index], 0, config_cpy.halfscan_ratio, 0.0);
                    }
                }

                /* Initialize tuners - dual-tuner aware */
                if (*err == ERROR_NONE) {
                    if (config_cpy.dual_tuner_enabled) {
                        if (thread_vars->tuner_id == 1) {
                            /* Tuner 1: use tuner 1 frequency, turn off tuner 2 for now */
                            uint32_t freq_tuner1 = config_cpy.freq_requested[config_cpy.freq_index];
                            tuner_err = stv6120_init(freq_tuner1, 0, config_cpy.port_swap);
                        } else {
                            /* Tuner 2: use tuner 2 frequency, turn off tuner 1 for this instance */
                            uint32_t freq_tuner2 = config_cpy.freq_requested_tuner2[config_cpy.freq_index_tuner2];
                            tuner_err = stv6120_init(0, freq_tuner2, config_cpy.port_swap);
                        }
                    } else {
                        /* Single tuner mode - original behavior */
                        tuner_err = stv6120_init(config_cpy.freq_requested[config_cpy.freq_index], 0, config_cpy.port_swap);
                    }
                }

                /* Tuner Lock timeout on some NIMs - Print message and pause, do..while() handles the retry logic */
                if (*err == ERROR_NONE && tuner_err == ERROR_TUNER_LOCK_TIMEOUT)
                {
                    printf("Flow: Caught tuner lock timeout, %" PRIu32 " attempts at stv6120_init() remaining.\n", tuner_lock_attempts);
                    /* Power down the synthesizers to potentially improve success on retry. */
                    /* - Everything else gets powered down as well to stay within datasheet-defined states */
                    *err = stv6120_powerdown_both_paths();
                    if (*err == ERROR_NONE)
                        usleep(200 * 1000);
                }
            } while (*thread_vars->main_err_ptr == ERROR_NONE && *err == ERROR_NONE && tuner_err == ERROR_TUNER_LOCK_TIMEOUT && tuner_lock_attempts-- > 0);

            /* Propagate up tuner error from stv6120_init() */
            if (*err == ERROR_NONE)
                *err = tuner_err;

            /* we turn on the LNA we want and turn the other off (if they exist) */
            /* Dual-tuner aware LNA initialization with graceful degradation */
            uint8_t lna_top_err = ERROR_NONE;
            uint8_t lna_bottom_err = ERROR_NONE;
            bool lna_top_ok = false;
            bool lna_bottom_ok = false;

            if (*err == ERROR_NONE) {
                lna_top_err = stvvglna_init(NIM_INPUT_TOP, (config_cpy.port_swap) ? STVVGLNA_OFF : STVVGLNA_ON, &lna_top_ok);
                if (lna_top_err != ERROR_NONE) {
                    printf("WARNING: TOP LNA initialization failed (error %d)\n", lna_top_err);
                }
            }

            if (*err == ERROR_NONE) {
                lna_bottom_err = stvvglna_init(NIM_INPUT_BOTTOM, (config_cpy.port_swap) ? STVVGLNA_ON : STVVGLNA_OFF, &lna_bottom_ok);
                if (lna_bottom_err != ERROR_NONE) {
                    if (config_cpy.dual_tuner_enabled) {
                        printf("WARNING: BOTTOM LNA initialization failed (error %d) - dual-tuner mode may operate with reduced functionality\n", lna_bottom_err);
                        /* In dual-tuner mode, allow graceful degradation if second LNA fails */
                        lna_bottom_err = ERROR_NONE;
                    } else {
                        printf("WARNING: BOTTOM LNA initialization failed (error %d)\n", lna_bottom_err);
                    }
                }
            }

            /* Set overall LNA status based on available LNAs */
            status_cpy.lna_ok = lna_top_ok || lna_bottom_ok;

            /* Only fail if both LNAs fail in single-tuner mode, or if TOP LNA fails in dual-tuner mode */
            if (config_cpy.dual_tuner_enabled) {
                /* In dual-tuner mode, we need at least the TOP LNA working for tuner 1 */
                /* For tuner 2, allow graceful degradation if BOTTOM LNA fails */
                if (thread_vars->tuner_id == 1 && lna_top_err != ERROR_NONE) {
                    *err = lna_top_err;
                    printf("ERROR: TOP LNA initialization failed in dual-tuner mode - cannot continue\n");
                } else if (thread_vars->tuner_id == 2 && lna_bottom_err != ERROR_NONE) {
                    printf("WARNING: BOTTOM LNA initialization failed for tuner 2 - continuing with graceful degradation\n");
                    /* Don't set error for tuner 2 BOTTOM LNA failure - allow graceful degradation */
                }
            } else {
                /* In single-tuner mode, fail if any critical LNA fails */
                if (lna_top_err != ERROR_NONE || lna_bottom_err != ERROR_NONE) {
                    *err = (lna_top_err != ERROR_NONE) ? lna_top_err : lna_bottom_err;
                }
            }

            if (*err != ERROR_NONE)
                printf("ERROR: failed to init a device - is the NIM powered on?\n");

            /* Enable/Disable polarisation voltage supply - use tuner-specific values */
            if (*err == ERROR_NONE) {
                if (config_cpy.dual_tuner_enabled && thread_vars->tuner_id == 2) {
                    /* Tuner 2: use tuner 2 specific polarization settings */
                    *err = ftdi_set_polarisation_supply(config_cpy.polarisation_supply_tuner2, config_cpy.polarisation_horizontal_tuner2);
                    if (*err == ERROR_NONE) {
                        status_cpy.polarisation_supply = config_cpy.polarisation_supply_tuner2;
                        status_cpy.polarisation_horizontal = config_cpy.polarisation_horizontal_tuner2;
                    }
                } else {
                    /* Tuner 1 or single tuner mode: use main polarization settings */
                    *err = ftdi_set_polarisation_supply(config_cpy.polarisation_supply, config_cpy.polarisation_horizontal);
                    if (*err == ERROR_NONE) {
                        status_cpy.polarisation_supply = config_cpy.polarisation_supply;
                        status_cpy.polarisation_horizontal = config_cpy.polarisation_horizontal;
                    }
                }
            }

            /* now start the whole thing scanning for the signal */
            if (*err == ERROR_NONE)
            {
                if (config_cpy.dual_tuner_enabled) {
                    /* In dual-tuner mode, scan start is already handled by stv0910_init_dual_sequence */
                    /* Only tuner 1 should set the state since both demodulators are already scanning */
                    if (thread_vars->tuner_id == 1) {
                        printf("      Status: Dual-tuner scan already initiated by init sequence\n");
                        status_cpy.state = STATE_DEMOD_HUNTING;
                    } else {
                        printf("      Status: Tuner 2 scan already initiated - monitoring BOTTOM demodulator\n");
                        status_cpy.state = STATE_DEMOD_HUNTING;
                    }
                } else {
                    /* Single tuner mode - original behavior */
                    *err = stv0910_start_scan(STV0910_DEMOD_TOP);
                    status_cpy.state = STATE_DEMOD_HUNTING;
                }
            }

            status_cpy.last_ts_or_reinit_monotonic = monotonic_ms();
        }

        /* Main receiver state machine */
        switch (status_cpy.state)
        {
        case STATE_INIT:
            /* Initial state - wait for configuration to be processed */
            /* Do nothing until new_config triggers initialization */
            break;

        case STATE_DEMOD_HUNTING:
            /* Use dual-tuner aware reporting */
            if (*err == ERROR_NONE) {
                if (config_cpy.dual_tuner_enabled) {
                    uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                    *err = do_report_dual(&status_cpy, demod);
                } else {
                    *err = do_report(&status_cpy);
                }
            }
            /* process state changes - use correct demodulator for dual-tuner mode */
            if (*err == ERROR_NONE) {
                if (config_cpy.dual_tuner_enabled) {
                    uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                    *err = stv0910_read_scan_state(demod, &status_cpy.demod_state);
                } else {
                    *err = stv0910_read_scan_state(STV0910_DEMOD_TOP, &status_cpy.demod_state);
                }
            }
            if (status_cpy.demod_state == DEMOD_FOUND_HEADER)
            {
                status_cpy.state = STATE_DEMOD_FOUND_HEADER;
            }
            else if (status_cpy.demod_state == DEMOD_S2)
            {
                status_cpy.state = STATE_DEMOD_S2;
            }
            else if (status_cpy.demod_state == DEMOD_S)
            {
                status_cpy.state = STATE_DEMOD_S;
            }
            else if ((status_cpy.demod_state != DEMOD_HUNTING) && (*err == ERROR_NONE))
            {
                printf("ERROR: demodulator returned a bad scan state\n");
                *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
            }                                      /* no need for another else, all states covered */
            break;

        case STATE_DEMOD_FOUND_HEADER:
            /* Use dual-tuner aware reporting */
            if (*err == ERROR_NONE) {
                if (config_cpy.dual_tuner_enabled) {
                    uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                    *err = do_report_dual(&status_cpy, demod);
                } else {
                    *err = do_report(&status_cpy);
                }
            }
            /* process state changes - use correct demodulator for dual-tuner mode */
            if (config_cpy.dual_tuner_enabled) {
                uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                *err = stv0910_read_scan_state(demod, &status_cpy.demod_state);
            } else {
                *err = stv0910_read_scan_state(STV0910_DEMOD_TOP, &status_cpy.demod_state);
            }
            if (status_cpy.demod_state == DEMOD_HUNTING)
            {
                status_cpy.state = STATE_DEMOD_HUNTING;
            }
            else if (status_cpy.demod_state == DEMOD_S2)
            {
                status_cpy.state = STATE_DEMOD_S2;
            }
            else if (status_cpy.demod_state == DEMOD_S)
            {
                status_cpy.state = STATE_DEMOD_S;
            }
            else if ((status_cpy.demod_state != DEMOD_FOUND_HEADER) && (*err == ERROR_NONE))
            {
                printf("ERROR: demodulator returned a bad scan state\n");
                *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
            }                                      /* no need for another else, all states covered */
            break;

        case STATE_DEMOD_S2:
            /* Use dual-tuner aware reporting */
            if (*err == ERROR_NONE) {
                if (config_cpy.dual_tuner_enabled) {
                    uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                    *err = do_report_dual(&status_cpy, demod);
                } else {
                    *err = do_report(&status_cpy);
                }
            }
            /* process state changes - use correct demodulator for dual-tuner mode */
            if (config_cpy.dual_tuner_enabled) {
                uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                *err = stv0910_read_scan_state(demod, &status_cpy.demod_state);
            } else {
                *err = stv0910_read_scan_state(STV0910_DEMOD_TOP, &status_cpy.demod_state);
            }
            if (status_cpy.demod_state == DEMOD_HUNTING)
            {
                status_cpy.state = STATE_DEMOD_HUNTING;
            }
            else if (status_cpy.demod_state == DEMOD_FOUND_HEADER)
            {
                status_cpy.state = STATE_DEMOD_FOUND_HEADER;
            }
            else if (status_cpy.demod_state == DEMOD_S)
            {
                status_cpy.state = STATE_DEMOD_S;
            }
            else if ((status_cpy.demod_state != DEMOD_S2) && (*err == ERROR_NONE))
            {
                printf("ERROR: demodulator returned a bad scan state\n");
                *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
            }                                      /* no need for another else, all states covered */
            break;

        case STATE_DEMOD_S:
            /* Use dual-tuner aware reporting */
            if (*err == ERROR_NONE) {
                if (config_cpy.dual_tuner_enabled) {
                    uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                    *err = do_report_dual(&status_cpy, demod);
                } else {
                    *err = do_report(&status_cpy);
                }
            }
            /* process state changes - use correct demodulator for dual-tuner mode */
            if (config_cpy.dual_tuner_enabled) {
                uint8_t demod = (thread_vars->tuner_id == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;
                *err = stv0910_read_scan_state(demod, &status_cpy.demod_state);
            } else {
                *err = stv0910_read_scan_state(STV0910_DEMOD_TOP, &status_cpy.demod_state);
            }
            if (status_cpy.demod_state == DEMOD_HUNTING)
            {
                status_cpy.state = STATE_DEMOD_HUNTING;
            }
            else if (status_cpy.demod_state == DEMOD_FOUND_HEADER)
            {
                status_cpy.state = STATE_DEMOD_FOUND_HEADER;
            }
            else if (status_cpy.demod_state == DEMOD_S2)
            {
                status_cpy.state = STATE_DEMOD_S2;
            }
            else if ((status_cpy.demod_state != DEMOD_S) && (*err == ERROR_NONE))
            {
                printf("ERROR: demodulator returned a bad scan state\n");
                *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
            }                                      /* no need for another else, all states covered */
            break;

        default:
            *err = ERROR_STATE; /* we should never get here so panic if we do */
            break;
        }

        if (status->ts_packet_count_nolock > 0 && last_ts_packet_count != status->ts_packet_count_nolock)
        {
            status_cpy.last_ts_or_reinit_monotonic = monotonic_ms();
            last_ts_packet_count = status->ts_packet_count_nolock;
        }

        /* Copy local status data over global object */
        pthread_mutex_lock(&status->mutex);

        /* Copy out other vars */
        status->state = status_cpy.state;
        status->demod_state = status_cpy.demod_state;
        status->lna_ok = status_cpy.lna_ok;
        status->lna_gain = status_cpy.lna_gain;
        status->agc1_gain = status_cpy.agc1_gain;
        status->agc2_gain = status_cpy.agc2_gain;
        status->power_i = status_cpy.power_i;
        status->power_q = status_cpy.power_q;
        status->frequency_requested = status_cpy.frequency_requested;
        status->frequency_offset = status_cpy.frequency_offset;
        status->polarisation_supply = status_cpy.polarisation_supply;
        status->polarisation_horizontal = status_cpy.polarisation_horizontal;
        status->symbolrate_requested = status_cpy.symbolrate_requested;
        status->symbolrate = status_cpy.symbolrate;
        status->viterbi_error_rate = status_cpy.viterbi_error_rate;
        status->bit_error_rate = status_cpy.bit_error_rate;
        status->modulation_error_rate = status_cpy.modulation_error_rate;
        status->errors_bch_uncorrected = status_cpy.errors_bch_uncorrected;
        status->errors_bch_count = status_cpy.errors_bch_count;
        status->errors_ldpc_count = status_cpy.errors_ldpc_count;
        memcpy(status->constellation, status_cpy.constellation, (sizeof(uint8_t) * NUM_CONSTELLATIONS * 2));
        status->puncture_rate = status_cpy.puncture_rate;
        status->modcod = status_cpy.modcod;
        status->matype1 = status_cpy.matype1;
        status->matype2 = status_cpy.matype2;
        status->short_frame = status_cpy.short_frame;
        status->pilots = status_cpy.pilots;
        status->rolloff = status_cpy.rolloff;
        if (status_cpy.last_ts_or_reinit_monotonic != 0)
        {
            status->last_ts_or_reinit_monotonic = status_cpy.last_ts_or_reinit_monotonic;
        }

        /* Set monotonic value to signal new data */
        status->last_updated_monotonic = monotonic_ms();
        /* Trigger pthread signal */
        pthread_cond_signal(&status->signal);
        pthread_mutex_unlock(&status->mutex);

        last_i2c_loop = monotonic_ms();
    }
    return NULL;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t status_all_write(longmynd_status_t *status, uint8_t (*status_write)(uint8_t, uint32_t, bool *), uint8_t (*status_string_write)(uint8_t, char *, bool *), bool *output_ready_ptr)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Reads the past status struct out to the passed write function                                      */
    /*  Returns: error code                                                                               */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    /* Main status */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_STATE, status->state, output_ready_ptr);
    /* LNAs if present */
    if (status->lna_ok)
    {
        if (err == ERROR_NONE && *output_ready_ptr)
            err = status_write(STATUS_LNA_GAIN, status->lna_gain, output_ready_ptr);
    }
    /* AGC1 Gain */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_AGC1_GAIN, status->agc1_gain, output_ready_ptr);
    /* AGC2 Gain */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_AGC2_GAIN, status->agc2_gain, output_ready_ptr);
    /* I,Q powers */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_POWER_I, status->power_i, output_ready_ptr);
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_POWER_Q, status->power_q, output_ready_ptr);
    /* constellations */
    for (uint8_t count = 0; count < NUM_CONSTELLATIONS; count++)
    {
        if (err == ERROR_NONE && *output_ready_ptr)
            err = status_write(STATUS_CONSTELLATION_I, status->constellation[count][0], output_ready_ptr);
        if (err == ERROR_NONE && *output_ready_ptr)
            err = status_write(STATUS_CONSTELLATION_Q, status->constellation[count][1], output_ready_ptr);
    }
    /* puncture rate */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_PUNCTURE_RATE, status->puncture_rate, output_ready_ptr);
    /* carrier frequency offset we are trying */
    /* note we now have the offset, so we need to add in the freq we tried to set it to */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_CARRIER_FREQUENCY, (uint32_t)(status->frequency_requested + (status->frequency_offset / 1000)), output_ready_ptr);
    /* LNB Voltage Supply Enabled: true / false */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_LNB_SUPPLY, status->polarisation_supply, output_ready_ptr);
    /* LNB Voltage Supply is Horizontal Polarisation: true / false */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_LNB_POLARISATION_H, status->polarisation_horizontal, output_ready_ptr);
    /* symbol rate we are trying */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_SYMBOL_RATE, status->symbolrate, output_ready_ptr);
    /* viterbi error rate */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_VITERBI_ERROR_RATE, status->viterbi_error_rate, output_ready_ptr);
    /* BER */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_BER, status->bit_error_rate, output_ready_ptr);
    /* MER */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_MER, status->modulation_error_rate, output_ready_ptr);
    /* BCH Uncorrected Errors Flag */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_ERRORS_BCH_UNCORRECTED, status->errors_bch_uncorrected, output_ready_ptr);
    /* BCH Corrected Errors Count */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_ERRORS_BCH_COUNT, status->errors_bch_count, output_ready_ptr);
    /* LDPC Corrected Errors Count */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_ERRORS_LDPC_COUNT, status->errors_ldpc_count, output_ready_ptr);
    /* Service Name */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_string_write(STATUS_SERVICE_NAME, status->service_name, output_ready_ptr);
    /* Service Provider Name */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_string_write(STATUS_SERVICE_PROVIDER_NAME, status->service_provider_name, output_ready_ptr);
    /* TS Null Percentage */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_TS_NULL_PERCENTAGE, status->ts_null_percentage, output_ready_ptr);
    /* TS Elementary Stream PIDs */
    for (uint8_t count = 0; count < NUM_ELEMENT_STREAMS; count++)
    {
        if (status->ts_elementary_streams[count][0] > 0)
        {
            if (err == ERROR_NONE && *output_ready_ptr)
                err = status_write(STATUS_ES_PID, status->ts_elementary_streams[count][0], output_ready_ptr);
            if (err == ERROR_NONE && *output_ready_ptr)
                err = status_write(STATUS_ES_TYPE, status->ts_elementary_streams[count][1], output_ready_ptr);
        }
    }
    /* MODCOD */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_MODCOD, status->modcod, output_ready_ptr);
    /* Short Frames */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_SHORT_FRAME, status->short_frame, output_ready_ptr);
    /* Pilots */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_PILOTS, status->pilots, output_ready_ptr);
    // MATYPE
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_MATYPE1, status->matype1, output_ready_ptr);
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_MATYPE2, status->matype2, output_ready_ptr);
    if (err == ERROR_NONE && *output_ready_ptr)
        err = status_write(STATUS_ROLLOFF, status->rolloff, output_ready_ptr);
    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t status_all_write_tuner(uint8_t tuner_id, longmynd_status_t *status, bool *output_ready_ptr)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Writes status for a specific tuner via MQTT with tuner-specific topics                            */
    /*  tuner_id: 1 for tuner 1, 2 for tuner 2                                                          */
    /*  status: status structure to write                                                                */
    /*  output_ready_ptr: output ready flag                                                              */
    /*  Returns: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    /* Main status */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_STATE, status->state, output_ready_ptr);
    /* LNAs if present */
    if (status->lna_ok)
    {
        if (err == ERROR_NONE && *output_ready_ptr)
            err = mqtt_status_write_tuner(tuner_id, STATUS_LNA_GAIN, status->lna_gain, output_ready_ptr);
    }
    /* AGC1 Gain */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_AGC1_GAIN, status->agc1_gain, output_ready_ptr);
    /* AGC2 Gain */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_AGC2_GAIN, status->agc2_gain, output_ready_ptr);
    /* I,Q powers */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_POWER_I, status->power_i, output_ready_ptr);
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_POWER_Q, status->power_q, output_ready_ptr);
    /* symbol rate we are trying */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_SYMBOL_RATE, status->symbolrate, output_ready_ptr);
    /* BER */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_BER, status->bit_error_rate, output_ready_ptr);
    /* MER */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_MER, status->modulation_error_rate, output_ready_ptr);
    /* MODCOD */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_write_tuner(tuner_id, STATUS_MODCOD, status->modcod, output_ready_ptr);
    /* Service Name */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_string_write_tuner(tuner_id, STATUS_SERVICE_NAME, status->service_name, output_ready_ptr);
    /* Service Provider Name */
    if (err == ERROR_NONE && *output_ready_ptr)
        err = mqtt_status_string_write_tuner(tuner_id, STATUS_SERVICE_PROVIDER_NAME, status->service_provider_name, output_ready_ptr);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
static uint8_t *sigterm_handler_err_ptr;
void sigterm_handler(int sig)
{
    /* -------------------------------------------------------------------------------------------------- */
    /*    Runs on SIGTERM or SIGINT (Ctrl+C).                                                             */
    /*    Sets main error variable to cause all threads to cleanly exit                                   */
    /* -------------------------------------------------------------------------------------------------- */
    (void)sig;
    /* There are some internally handled errors, so we blindly set here to ensure we exit */
    *sigterm_handler_err_ptr = ERROR_SIGNAL_TERMINATE;
}

/* -------------------------------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    /* -------------------------------------------------------------------------------------------------- */
    /*    command line processing                                                                         */
    /*    module initialisation                                                                           */
    /*    Print out of status information to requested interface, triggered by pthread condition variable */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t (*status_write)(uint8_t, uint32_t, bool *);
    uint8_t (*status_string_write)(uint8_t, char *, bool *);
    bool status_output_ready = true;

    sigterm_handler_err_ptr = &err;
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    /* Ignore SIGPIPE on closed pipes */
    signal(SIGPIPE, SIG_IGN);

    printf("Flow: main\n");

    if (err == ERROR_NONE)
        err = process_command_line(argc, argv, &longmynd_config);

    /* first setup the fifos, udp socket, ftdi and usb */
    if (longmynd_config.status_use_ip)
    {
        if (err == ERROR_NONE)
            err = udp_status_init(longmynd_config.status_ip_addr, longmynd_config.status_ip_port);
        status_write = udp_status_write;
        status_string_write = udp_status_string_write;
    }
    else if (longmynd_config.status_use_mqtt)
    {
        if (err == ERROR_NONE)
            err = mqttinit(longmynd_config.status_ip_addr);
        if(err>0) fprintf(stderr,"MQTT Broker not reachable\n");
        status_write = mqtt_status_write;
        status_string_write = mqtt_status_string_write;
    }
    else
    {
        if (err == ERROR_NONE)
            err = fifo_status_init(longmynd_config.status_fifo_path, &status_output_ready);
        status_write = fifo_status_write;
        status_string_write = fifo_status_string_write;
    }

    // Initialize FTDI devices (single or dual-tuner mode)
    if (err == ERROR_NONE) {
        if (longmynd_config.dual_tuner_enabled) {
            printf("Flow: Initializing dual-tuner mode\n");
            err = ftdi_init_dual(longmynd_config.device_usb_bus, longmynd_config.device_usb_addr,
                                longmynd_config.device2_usb_bus, longmynd_config.device2_usb_addr,
                                longmynd_config.auto_detect_second_device);
        } else {
            printf("Flow: Initializing single-tuner mode\n");
            err = ftdi_init(longmynd_config.device_usb_bus, longmynd_config.device_usb_addr);
        }
    }

    // Initialize UDP streaming (single or dual-tuner mode)
    if (err == ERROR_NONE && longmynd_config.ts_use_ip) {
        if (longmynd_config.dual_tuner_enabled) {
            printf("Flow: Initializing dual UDP streaming\n");
            printf("      Tuner 1: %s:%d\n", longmynd_config.ts_ip_addr, longmynd_config.ts_ip_port);
            printf("      Tuner 2: %s:%d\n", longmynd_config.ts2_ip_addr, longmynd_config.ts2_ip_port);
            err = udp_ts_init_dual(longmynd_config.ts_ip_addr, longmynd_config.ts_ip_port,
                                  longmynd_config.ts2_ip_addr, longmynd_config.ts2_ip_port);
        } else {
            printf("Flow: Initializing single UDP streaming\n");
            printf("      TS output: %s:%d\n", longmynd_config.ts_ip_addr, longmynd_config.ts_ip_port);
            err = udp_ts_init(longmynd_config.ts_ip_addr, longmynd_config.ts_ip_port);
        }
    }

    // Initialize MQTT (enable dual-tuner mode if configured)
    if (err == ERROR_NONE && longmynd_config.status_use_ip) {
        if (longmynd_config.dual_tuner_enabled) {
            printf("Flow: Enabling MQTT dual-tuner mode\n");
            mqtt_set_dual_tuner_mode(true);
        }
        printf("Flow: Initializing MQTT broker: %s\n", longmynd_config.status_ip_addr);
        err = mqttinit(longmynd_config.status_ip_addr);
    }

    // Initialize status structures for dual-tuner mode
    pthread_mutex_t dual_sync_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t dual_sync_cond = PTHREAD_COND_INITIALIZER;
    bool top_demod_ready = false;

    // Initialize status structures with proper initial state
    longmynd_status.state = STATE_INIT;
    longmynd_config.new_config = true; // Ensure initialization runs on first loop

    if (longmynd_config.dual_tuner_enabled) {
        printf("Flow: Initializing dual-tuner status structures\n");
        memcpy(&longmynd_status_tuner1, &longmynd_status, sizeof(longmynd_status_t));
        memcpy(&longmynd_status_tuner2, &longmynd_status, sizeof(longmynd_status_t));

        // Ensure both tuner status structures have proper initial state
        longmynd_status_tuner1.state = STATE_INIT;
        longmynd_status_tuner2.state = STATE_INIT;

        // Initialize mutexes and condition variables for dual status structures
        pthread_mutex_init(&longmynd_status_tuner1.mutex, NULL);
        pthread_cond_init(&longmynd_status_tuner1.signal, NULL);
        pthread_mutex_init(&longmynd_status_tuner2.mutex, NULL);
        pthread_cond_init(&longmynd_status_tuner2.signal, NULL);

        // Initialize dual-tuner synchronization
        pthread_mutex_init(&dual_sync_mutex, NULL);
        pthread_cond_init(&dual_sync_cond, NULL);
        printf("Flow: Dual-tuner synchronization initialized\n");
    }

    thread_vars_t thread_vars_ts;
        thread_vars_ts.main_err_ptr = &err;
        thread_vars_ts.thread_err = ERROR_NONE;
        thread_vars_ts.config = &longmynd_config;
        thread_vars_ts.status = longmynd_config.dual_tuner_enabled ? &longmynd_status_tuner1 : &longmynd_status;
        thread_vars_ts.tuner_id = 1;
        thread_vars_ts.dual_sync_mutex = longmynd_config.dual_tuner_enabled ? &dual_sync_mutex : NULL;
        thread_vars_ts.dual_sync_cond = longmynd_config.dual_tuner_enabled ? &dual_sync_cond : NULL;
        thread_vars_ts.top_demod_ready = longmynd_config.dual_tuner_enabled ? &top_demod_ready : NULL;

    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_ts, NULL, loop_ts, (void *)&thread_vars_ts))
        {
            //pthread_setname_np(thread_ts, "TS Transport 1");
        }
        else
        {
            fprintf(stderr, "Error creating loop_ts pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    thread_vars_t thread_vars_ts_parse;
        thread_vars_ts_parse.main_err_ptr = &err;
        thread_vars_ts_parse.thread_err = ERROR_NONE;
        thread_vars_ts_parse.config = &longmynd_config;
        thread_vars_ts_parse.status = longmynd_config.dual_tuner_enabled ? &longmynd_status_tuner1 : &longmynd_status;
        thread_vars_ts_parse.tuner_id = 1;
        thread_vars_ts_parse.dual_sync_mutex = longmynd_config.dual_tuner_enabled ? &dual_sync_mutex : NULL;
        thread_vars_ts_parse.dual_sync_cond = longmynd_config.dual_tuner_enabled ? &dual_sync_cond : NULL;
        thread_vars_ts_parse.top_demod_ready = longmynd_config.dual_tuner_enabled ? &top_demod_ready : NULL;

    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_ts_parse, NULL, loop_ts_parse, (void *)&thread_vars_ts_parse))
        {
            //pthread_setname_np(thread_ts_parse, "TS Parse 1");
        }
        else
        {
            fprintf(stderr, "Error creating loop_ts_parse pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    // Create main I2C thread first (critical for dual-tuner synchronization)
    thread_vars_t thread_vars_i2c;
        thread_vars_i2c.main_err_ptr = &err;
        thread_vars_i2c.thread_err = ERROR_NONE;
        thread_vars_i2c.config = &longmynd_config;
        thread_vars_i2c.status = longmynd_config.dual_tuner_enabled ? &longmynd_status_tuner1 : &longmynd_status;
        thread_vars_i2c.tuner_id = 1;
        thread_vars_i2c.dual_sync_mutex = longmynd_config.dual_tuner_enabled ? &dual_sync_mutex : NULL;
        thread_vars_i2c.dual_sync_cond = longmynd_config.dual_tuner_enabled ? &dual_sync_cond : NULL;
        thread_vars_i2c.top_demod_ready = longmynd_config.dual_tuner_enabled ? &top_demod_ready : NULL;

    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_i2c, NULL, loop_i2c, (void *)&thread_vars_i2c))
        {
            //pthread_setname_np(thread_i2c, "Receiver 1");
            printf("Flow: Created tuner 1 I2C thread (TOP demodulator)\n");
        }
        else
        {
            fprintf(stderr, "Error creating loop_i2c pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    // Create second tuner threads if dual-tuner mode is enabled
    thread_vars_t thread_vars_ts_tuner2;
    thread_vars_t thread_vars_ts_parse_tuner2;
    thread_vars_t thread_vars_i2c_tuner2;

    if (err == ERROR_NONE && longmynd_config.dual_tuner_enabled) {
        printf("Flow: Creating dual-tuner threads\n");

        // Second tuner TS thread
        thread_vars_ts_tuner2.main_err_ptr = &err;
        thread_vars_ts_tuner2.thread_err = ERROR_NONE;
        thread_vars_ts_tuner2.config = &longmynd_config;
        thread_vars_ts_tuner2.status = &longmynd_status_tuner2;
        thread_vars_ts_tuner2.tuner_id = 2;
        thread_vars_ts_tuner2.dual_sync_mutex = &dual_sync_mutex;
        thread_vars_ts_tuner2.dual_sync_cond = &dual_sync_cond;
        thread_vars_ts_tuner2.top_demod_ready = &top_demod_ready;

        if (0 == pthread_create(&thread_ts_tuner2, NULL, loop_ts, (void *)&thread_vars_ts_tuner2)) {
            //pthread_setname_np(thread_ts_tuner2, "TS Transport 2");
        } else {
            fprintf(stderr, "Error creating loop_ts tuner2 pthread\n");
            err = ERROR_THREAD_ERROR;
        }

        // Second tuner TS parse thread
        if (err == ERROR_NONE) {
            thread_vars_ts_parse_tuner2.main_err_ptr = &err;
            thread_vars_ts_parse_tuner2.thread_err = ERROR_NONE;
            thread_vars_ts_parse_tuner2.config = &longmynd_config;
            thread_vars_ts_parse_tuner2.status = &longmynd_status_tuner2;
            thread_vars_ts_parse_tuner2.tuner_id = 2;
            thread_vars_ts_parse_tuner2.dual_sync_mutex = &dual_sync_mutex;
            thread_vars_ts_parse_tuner2.dual_sync_cond = &dual_sync_cond;
            thread_vars_ts_parse_tuner2.top_demod_ready = &top_demod_ready;

            if (0 == pthread_create(&thread_ts_parse_tuner2, NULL, loop_ts_parse, (void *)&thread_vars_ts_parse_tuner2)) {
                //pthread_setname_np(thread_ts_parse_tuner2, "TS Parse 2");
            } else {
                fprintf(stderr, "Error creating loop_ts_parse tuner2 pthread\n");
                err = ERROR_THREAD_ERROR;
            }
        }

        // Second tuner I2C thread
        if (err == ERROR_NONE) {
            thread_vars_i2c_tuner2.main_err_ptr = &err;
            thread_vars_i2c_tuner2.thread_err = ERROR_NONE;
            thread_vars_i2c_tuner2.config = &longmynd_config;
            thread_vars_i2c_tuner2.status = &longmynd_status_tuner2;
            thread_vars_i2c_tuner2.tuner_id = 2;
            thread_vars_i2c_tuner2.dual_sync_mutex = &dual_sync_mutex;
            thread_vars_i2c_tuner2.dual_sync_cond = &dual_sync_cond;
            thread_vars_i2c_tuner2.top_demod_ready = &top_demod_ready;

            if (0 == pthread_create(&thread_i2c_tuner2, NULL, loop_i2c, (void *)&thread_vars_i2c_tuner2)) {
                //pthread_setname_np(thread_i2c_tuner2, "Receiver 2");
                printf("Flow: Created tuner 2 I2C thread (BOTTOM demodulator)\n");
            } else {
                fprintf(stderr, "Error creating loop_i2c tuner2 pthread\n");
                err = ERROR_THREAD_ERROR;
            }
        }
    }



    thread_vars_t thread_vars_beep;
        thread_vars_beep.main_err_ptr = &err;
        thread_vars_beep.thread_err = ERROR_NONE;
        thread_vars_beep.config = &longmynd_config;
        thread_vars_beep.status = &longmynd_status;

    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_beep, NULL, loop_beep, (void *)&thread_vars_beep))
        {
            // pthread_setname_np(thread_beep, "Beep Audio");
        }
        else
        {
            fprintf(stderr, "Error creating loop_beep pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    uint64_t last_status_sent_monotonic = 0;
    uint64_t last_status_sent_monotonic_tuner1 = 0;
    uint64_t last_status_sent_monotonic_tuner2 = 0;
    longmynd_status_t longmynd_status_cpy;
    longmynd_status_t longmynd_status_tuner1_cpy;
    longmynd_status_t longmynd_status_tuner2_cpy;

    if (err == ERROR_NONE)
    {
        /* Initialise TS data re-init timer to prevent immediate reset */
        if (longmynd_config.dual_tuner_enabled) {
            pthread_mutex_lock(&longmynd_status_tuner1.mutex);
            longmynd_status_tuner1.last_ts_or_reinit_monotonic = monotonic_ms();
            pthread_mutex_unlock(&longmynd_status_tuner1.mutex);

            pthread_mutex_lock(&longmynd_status_tuner2.mutex);
            longmynd_status_tuner2.last_ts_or_reinit_monotonic = monotonic_ms();
            pthread_mutex_unlock(&longmynd_status_tuner2.mutex);
        } else {
            pthread_mutex_lock(&longmynd_status.mutex);
            longmynd_status.last_ts_or_reinit_monotonic = monotonic_ms();
            pthread_mutex_unlock(&longmynd_status.mutex);
        }
    }
    while (err == ERROR_NONE)
    {
        bool status_updated = false;

        if (longmynd_config.dual_tuner_enabled) {
            /* Handle dual-tuner status publishing */

            /* Check tuner 1 status */
            if (longmynd_status_tuner1.last_updated_monotonic != last_status_sent_monotonic_tuner1) {
                pthread_mutex_lock(&longmynd_status_tuner1.mutex);
                memcpy(&longmynd_status_tuner1_cpy, &longmynd_status_tuner1, sizeof(longmynd_status_t));
                pthread_mutex_unlock(&longmynd_status_tuner1.mutex);

                if (longmynd_config.status_use_mqtt) {
                    /* Send tuner 1 status via MQTT with tuner-specific topics */
                    err = status_all_write_tuner(1, &longmynd_status_tuner1_cpy, &status_output_ready);
                } else if (longmynd_config.status_use_ip || status_output_ready) {
                    /* Send tuner 1 status via UDP/FIFO (backward compatibility) */
                    err = status_all_write(&longmynd_status_tuner1_cpy, status_write, status_string_write, &status_output_ready);
                }

                last_status_sent_monotonic_tuner1 = longmynd_status_tuner1_cpy.last_updated_monotonic;
                status_updated = true;
            }

            /* Check tuner 2 status */
            if (err == ERROR_NONE && longmynd_status_tuner2.last_updated_monotonic != last_status_sent_monotonic_tuner2) {
                pthread_mutex_lock(&longmynd_status_tuner2.mutex);
                memcpy(&longmynd_status_tuner2_cpy, &longmynd_status_tuner2, sizeof(longmynd_status_t));
                pthread_mutex_unlock(&longmynd_status_tuner2.mutex);

                if (longmynd_config.status_use_mqtt) {
                    /* Send tuner 2 status via MQTT with tuner-specific topics */
                    err = status_all_write_tuner(2, &longmynd_status_tuner2_cpy, &status_output_ready);
                }

                last_status_sent_monotonic_tuner2 = longmynd_status_tuner2_cpy.last_updated_monotonic;
                status_updated = true;
            }
        } else {
            /* Handle single-tuner status publishing (backward compatibility) */
            if (longmynd_status.last_updated_monotonic != last_status_sent_monotonic) {
                pthread_mutex_lock(&longmynd_status.mutex);
                memcpy(&longmynd_status_cpy, &longmynd_status, sizeof(longmynd_status_t));
                pthread_mutex_unlock(&longmynd_status.mutex);

                if (longmynd_config.status_use_ip || status_output_ready) {
                    err = status_all_write(&longmynd_status_cpy, status_write, status_string_write, &status_output_ready);
                } else if (!longmynd_config.status_use_ip && !status_output_ready) {
                    err = fifo_status_init(longmynd_config.status_fifo_path, &status_output_ready);
                }

                last_status_sent_monotonic = longmynd_status_cpy.last_updated_monotonic;
                status_updated = true;
            }
        }

        if (!status_updated) {
            /* Sleep 10ms */
            usleep(100 * 1000);
        }
        /* Check for errors on threads */
        if (err == ERROR_NONE) {
            bool thread_error = false;

            /* Check main threads */
            if (thread_vars_ts.thread_err != ERROR_NONE ||
                thread_vars_ts_parse.thread_err != ERROR_NONE ||
                thread_vars_beep.thread_err != ERROR_NONE ||
                thread_vars_i2c.thread_err != ERROR_NONE) {
                thread_error = true;
            }

            /* Check dual-tuner threads if enabled */
            if (longmynd_config.dual_tuner_enabled) {
                if (thread_vars_ts_tuner2.thread_err != ERROR_NONE ||
                    thread_vars_ts_parse_tuner2.thread_err != ERROR_NONE ||
                    thread_vars_i2c_tuner2.thread_err != ERROR_NONE) {
                    thread_error = true;
                }
            }

            if (thread_error) {
                err = ERROR_THREAD_ERROR;
            }
        }

        /* Handle TS timeout for dual-tuner mode */
        if (longmynd_config.ts_timeout != -1) {
            if (longmynd_config.dual_tuner_enabled) {
                /* Check timeout for both tuners */
                uint64_t current_time = monotonic_ms();
                if (current_time > (longmynd_status_tuner1.last_ts_or_reinit_monotonic + longmynd_config.ts_timeout)) {
                    pthread_mutex_lock(&longmynd_status_tuner1.mutex);
                    longmynd_status_tuner1.last_ts_or_reinit_monotonic = current_time;
                    pthread_mutex_unlock(&longmynd_status_tuner1.mutex);
                }
                if (current_time > (longmynd_status_tuner2.last_ts_or_reinit_monotonic + longmynd_config.ts_timeout)) {
                    pthread_mutex_lock(&longmynd_status_tuner2.mutex);
                    longmynd_status_tuner2.last_ts_or_reinit_monotonic = current_time;
                    pthread_mutex_unlock(&longmynd_status_tuner2.mutex);
                }
            } else {
                /* Single-tuner timeout handling */
                if (monotonic_ms() > (longmynd_status.last_ts_or_reinit_monotonic + longmynd_config.ts_timeout)) {
                    pthread_mutex_lock(&longmynd_status.mutex);
                    longmynd_status.last_ts_or_reinit_monotonic = monotonic_ms();
                    pthread_mutex_unlock(&longmynd_status.mutex);
                }
            }
        }
    }

    printf("Flow: Main loop aborted, waiting for threads.\n");

    /* No fatal errors are currently possible here, so don't currently check return values */
    pthread_join(thread_ts_parse, NULL);
    pthread_join(thread_ts, NULL);
    pthread_join(thread_i2c, NULL);
    pthread_join(thread_beep, NULL);

    /* Join dual-tuner threads if they were created */
    if (longmynd_config.dual_tuner_enabled) {
        printf("Flow: Waiting for dual-tuner threads.\n");
        pthread_join(thread_ts_parse_tuner2, NULL);
        pthread_join(thread_ts_tuner2, NULL);
        pthread_join(thread_i2c_tuner2, NULL);

        /* Cleanup dual-tuner mutexes and condition variables */
        pthread_mutex_destroy(&longmynd_status_tuner1.mutex);
        pthread_cond_destroy(&longmynd_status_tuner1.signal);
        pthread_mutex_destroy(&longmynd_status_tuner2.mutex);
        pthread_cond_destroy(&longmynd_status_tuner2.signal);

        /* Cleanup dual-tuner synchronization */
        pthread_mutex_destroy(&dual_sync_mutex);
        pthread_cond_destroy(&dual_sync_cond);
    }

    printf("Flow: All threads accounted for. Exiting cleanly.\n");

    return err;
}
