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
#include "main.h"
#include "ftdi.h"
#include "stv0910.h"
#include "stv6120.h"
#include "stvvglna.h"
#include "nim.h"
#include "errors.h"
#include "fifo.h"
#include "udp.h"
#include "beep.h"
#include "ts.h"
#include "register_logging.h"
#include "json_output.h"
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

static longmynd_status_t longmynd_status2;  /* Tuner 2 status structure */
static pthread_t thread_ts_parse;
static pthread_t thread_ts;
static pthread_t thread_i2c;
static pthread_t thread_beep;

/* Tuner 2 thread variables */
static pthread_t thread_ts_tuner2;
static pthread_t thread_ts_parse_tuner2;
static thread_vars_t thread_vars_ts_tuner2;
static thread_vars_t thread_vars_ts_parse_tuner2;

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
    bool main_usb_set = false;
    bool ts_ip_set = false;
    bool ts_fifo_set = false;
    bool status_ip_set = false;
    bool status_mqtt_set = false;
    bool status_fifo_set = false;
    bool tuner2_ts_ip_set = false;

    /* Defaults */
    config->port_swap = false;
    config->halfscan_ratio = 1.5;
    config->beep_enabled = false;
    config->device_usb_addr = 0;
    config->device_usb_bus = 0;
    config->tuner2_device_usb_addr = 0;
    config->tuner2_device_usb_bus = 0;
    config->tuner2_enabled = false;

    /* Tuner 2 TS output defaults */
    config->tuner2_ts_use_ip = false;
    strcpy(config->tuner2_ts_fifo_path, "longmynd_tuner2_ts");
    strcpy(config->tuner2_ts_ip_addr, "230.0.0.3");
    config->tuner2_ts_ip_port = 1234;

    /* Tuner 2 status output defaults */
    config->tuner2_status_use_ip = false;
    config->tuner2_status_use_mqtt = false;
    strcpy(config->tuner2_status_fifo_path, "longmynd_tuner2_status");
    strcpy(config->tuner2_status_ip_addr, "230.0.0.4");
    config->tuner2_status_ip_port = 1235;
    config->ts_use_ip = false;
    config->status_use_mqtt = false;
    strcpy(config->ts_fifo_path, "longmynd_main_ts");
    config->status_use_ip = false;
    strcpy(config->status_fifo_path, "longmynd_main_status");
    config->polarisation_supply = false;
    char polarisation_str[8];
    config->ts_timeout = 50 * 1000;
    config->disable_demod_suppression = false;

    /* JSON output defaults */
    config->json_output_enabled = false;
    config->json_output_interval_ms = 1000;
    config->json_output_format = 0;  /* 0=full, 1=compact, 2=minimal */
    config->json_include_constellation = false;

    uint8_t param = 1;
    while (param < argc - 2)
    {
        if (argv[param][0] == '-')
        {
            /* Handle multi-character options first */
            if (strcmp(argv[param], "-u2") == 0)
            {
                param++;
                config->tuner2_device_usb_bus = (uint8_t)strtol(argv[param++], NULL, 10);
                config->tuner2_device_usb_addr = (uint8_t)strtol(argv[param], NULL, 10);
                config->tuner2_enabled = true;
                printf("Flow: Tuner 2 enabled with USB bus/device=%d,%d\n",
                       config->tuner2_device_usb_bus, config->tuner2_device_usb_addr);
            }
            else if (strcmp(argv[param], "-i2") == 0)
            {
                param++;
                strncpy(config->tuner2_ts_ip_addr, argv[param++], (16 - 1));
                config->tuner2_ts_ip_port = (uint16_t)strtol(argv[param], NULL, 10);
                config->tuner2_ts_use_ip = true;
                tuner2_ts_ip_set = true;
                printf("Flow: Tuner 2 TS output configured for IP=%s:%d\n",
                       config->tuner2_ts_ip_addr, config->tuner2_ts_ip_port);
            }
            else
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
            case 'D':
                config->disable_demod_suppression = true;
                param--; /* there is no data for this so go back */
                break;
            case 'j':
                config->json_output_enabled = true;
                param--; /* there is no data for this so go back */
                break;
            case 'J':
                config->json_output_interval_ms = (uint32_t)strtol(argv[param], NULL, 10);
                config->json_output_enabled = true;
                break;
            case 'F':
                if (strcmp(argv[param], "full") == 0) {
                    config->json_output_format = 0;
                } else if (strcmp(argv[param], "compact") == 0) {
                    config->json_output_format = 1;
                } else if (strcmp(argv[param], "minimal") == 0) {
                    config->json_output_format = 2;
                } else {
                    err = ERROR_ARGS_INPUT;
                    printf("ERROR: JSON format must be 'full', 'compact', or 'minimal'\n");
                }
                config->json_output_enabled = true;
                break;
            case 'C':
                config->json_include_constellation = true;
                param--; /* there is no data for this so go back */
                break;

                }
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
        const char *arg_ptr = argv[param];
        for (int i = 0; (i < 4) && (err == ERROR_NONE); i++)
        {
            /* Look for comma */
            char *comma_ptr = strchr(arg_ptr, ',');
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

            if (config->tuner2_enabled)
                printf("              Tuner 2 USB bus/device=%i,%i\n", config->tuner2_device_usb_bus, config->tuner2_device_usb_addr);
            if (!config->ts_use_ip)
                printf("              Main TS output to FIFO=%s\n", config->ts_fifo_path);
            else
                printf("              Main TS output to IP=%s:%i\n", config->ts_ip_addr, config->ts_ip_port);
            if (config->tuner2_enabled) {
                if (!config->tuner2_ts_use_ip)
                    printf("              Tuner 2 TS output to FIFO=%s\n", config->tuner2_ts_fifo_path);
                else
                    printf("              Tuner 2 TS output to IP=%s:%i\n", config->tuner2_ts_ip_addr, config->tuner2_ts_ip_port);
            }
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
            if (config->disable_demod_suppression)
                printf("              Demod Suppression Disabled\n");

            if (config->json_output_enabled) {
                const char *format_names[] = {"full", "compact", "minimal"};
                printf("              JSON Output Enabled: format=%s, interval=%ums\n",
                       format_names[config->json_output_format], config->json_output_interval_ms);
                if (config->json_include_constellation)
                    printf("              JSON includes constellation data\n");
            }
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
/* HARDWARE INITIALIZATION AND CONFIGURATION FUNCTIONS                                               */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
static uint8_t hardware_initialize_modules(const longmynd_config_t *config)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Initializes all hardware modules with retry logic for tuner PLL lock                           */
    /* Preserves exact hardware command sequences for STV6120 and STV0910                             */
    /* config: configuration parameters                                                                */
    /* status_cpy: local status copy for updates                                                       */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t tuner_err = ERROR_NONE; // Separate to avoid triggering main() abort on handled tuner error.
    int32_t tuner_lock_attempts = STV6120_PLL_ATTEMPTS;

    do
    {
        /* init all the modules - PRESERVE EXACT INITIALIZATION ORDER */
        if (err == ERROR_NONE)
            err = nim_init();
        /* we are only using the one demodulator so set the other to 0 to turn it off */
        if (err == ERROR_NONE)
            err = stv0910_init(config->sr_requested[config->sr_index], 0, config->halfscan_ratio, 0.0);
        /* we only use one of the tuners in STV6120 so freq for tuner 2=0 to turn it off */
        if (err == ERROR_NONE)
            tuner_err = stv6120_init(config->freq_requested[config->freq_index], 0, config->port_swap);

        /* Tuner Lock timeout on some NIMs - Print message and pause, do..while() handles the retry logic */
        if (err == ERROR_NONE && tuner_err == ERROR_TUNER_LOCK_TIMEOUT)
        {
            printf("Flow: Caught tuner lock timeout, %" PRIu32 " attempts at stv6120_init() remaining.\n", tuner_lock_attempts);
            /* Power down the synthesizers to potentially improve success on retry. */
            /* - Everything else gets powered down as well to stay within datasheet-defined states */
            err = stv6120_powerdown_both_paths();
            if (err == ERROR_NONE)
                usleep(200 * 1000); // PRESERVE EXACT TIMING
        }
    } while (err == ERROR_NONE && tuner_err == ERROR_TUNER_LOCK_TIMEOUT && tuner_lock_attempts-- > 0);

    /* Propagate up tuner error from stv6120_init() */
    if (err == ERROR_NONE)
        err = tuner_err;

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
static uint8_t hardware_configure_lna_and_polarization(const longmynd_config_t *config, longmynd_status_t *status_cpy)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Configures LNA and polarization voltage supply                                                  */
    /* Preserves exact hardware command sequences                                                      */
    /* config: configuration parameters                                                                */
    /* status_cpy: local status copy for updates                                                       */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    /* we turn on the LNA we want and turn the other off (if they exist) */
    if (err == ERROR_NONE)
        err = stvvglna_init(NIM_INPUT_TOP, (config->port_swap) ? STVVGLNA_OFF : STVVGLNA_ON, &status_cpy->lna_ok);
    if (err == ERROR_NONE)
        err = stvvglna_init(NIM_INPUT_BOTTOM, (config->port_swap) ? STVVGLNA_ON : STVVGLNA_OFF, &status_cpy->lna_ok);

    if (err != ERROR_NONE)
        printf("ERROR: failed to init a device - is the NIM powered on?\n");

    /* Enable/Disable polarisation voltage supply */
    if (err == ERROR_NONE)
        err = ftdi_set_polarisation_supply(config->polarisation_supply, config->polarisation_horizontal);
    if (err == ERROR_NONE)
    {
        status_cpy->polarisation_supply = config->polarisation_supply;
        status_cpy->polarisation_horizontal = config->polarisation_horizontal;
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
static uint8_t hardware_start_demodulator_scan(longmynd_status_t *status_cpy)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Starts the demodulator scanning process                                                         */
    /* Preserves exact hardware command sequences                                                      */
    /* status_cpy: local status copy for updates                                                       */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    /* now start the whole thing scanning for the signal */
    if (err == ERROR_NONE)
    {
        err = stv0910_start_scan(STV0910_DEMOD_TOP);
        status_cpy->state = STATE_DEMOD_HUNTING;
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t do_report(uint8_t tuner, longmynd_status_t *status)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* interrogates the demodulator to find the interesting info to report                                */
    /*   tuner: tuner number (1 or 2) to determine which demodulator to use                              */
    /*  status: the state struct                                                                          */
    /* return: error code                                                                                 */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t demod = (tuner == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;

    /* LNAs if present */
    if (status->lna_ok)
    {
        uint8_t lna_gain, lna_vgo;
        if (err == ERROR_NONE)
            stvvglna_read_agc(NIM_INPUT_TOP, &lna_gain, &lna_vgo);
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
/* CONFIGURATION MANAGEMENT FUNCTIONS                                                                 */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
static uint8_t handle_configuration_change(const thread_vars_t *thread_vars, longmynd_config_t *config_cpy,
                                          longmynd_status_t *status_cpy, uint8_t *err)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Handles new configuration changes and hardware reinitialization                                 */
    /* Preserves exact hardware command sequences and timing requirements                              */
    /* thread_vars: thread variables structure                                                         */
    /* config_cpy: local configuration copy                                                            */
    /* status_cpy: local status copy                                                                   */
    /* err: error code pointer                                                                         */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t local_err = ERROR_NONE;

    fprintf(stderr,"New Config !!!!!!!!!\n");
    /* Lock config struct - PRESERVE EXACT MUTEX USAGE */
    pthread_mutex_lock(&thread_vars->config->mutex);
    /* Clone status struct locally */
    memcpy(config_cpy, thread_vars->config, sizeof(longmynd_config_t));
    /* Clear new config flag */
    thread_vars->config->new_config = false;
    /* Set flag to clear ts buffer */
    thread_vars->config->ts_reset = true;
    pthread_mutex_unlock(&thread_vars->config->mutex);

    status_cpy->frequency_requested = config_cpy->freq_requested[config_cpy->freq_index];
    status_cpy->symbolrate_requested = config_cpy->sr_requested[config_cpy->sr_index];

    /* Initialize hardware modules with retry logic - PRESERVE EXACT SEQUENCES */
    if (*err == ERROR_NONE)
        local_err = hardware_initialize_modules(config_cpy);
    if (local_err != ERROR_NONE)
        *err = local_err;

    /* Configure LNA and polarization - PRESERVE EXACT SEQUENCES */
    if (*err == ERROR_NONE)
        local_err = hardware_configure_lna_and_polarization(config_cpy, status_cpy);
    if (local_err != ERROR_NONE)
        *err = local_err;

    /* Start demodulator scanning - PRESERVE EXACT SEQUENCES */
    if (*err == ERROR_NONE)
        local_err = hardware_start_demodulator_scan(status_cpy);
    if (local_err != ERROR_NONE)
        *err = local_err;

    status_cpy->last_ts_or_reinit_monotonic = monotonic_ms();

    return local_err;
}

/* -------------------------------------------------------------------------------------------------- */
/* STATUS SYNCHRONIZATION FUNCTIONS                                                                   */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
static void update_status_synchronization(longmynd_status_t *status, longmynd_status_t *status_cpy,
                                         uint32_t *last_ts_packet_count)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Updates TS packet tracking and synchronizes local status with global status                     */
    /* Preserves exact mutex usage and pthread signaling                                               */
    /* status: global status structure                                                                  */
    /* status_cpy: local status copy                                                                    */
    /* last_ts_packet_count: last TS packet count for tracking                                         */
    /* -------------------------------------------------------------------------------------------------- */

    /* Update TS packet tracking - PRESERVE EXACT LOGIC */
    if (status->ts_packet_count_nolock > 0 && *last_ts_packet_count != status->ts_packet_count_nolock)
    {
        status_cpy->last_ts_or_reinit_monotonic = monotonic_ms();
        *last_ts_packet_count = status->ts_packet_count_nolock;
    }

    /* Copy local status data over global object - PRESERVE EXACT MUTEX USAGE */
    pthread_mutex_lock(&status->mutex);

    /* Copy out other vars - PRESERVE EXACT FIELD ASSIGNMENTS */
    status->state = status_cpy->state;
    status->demod_state = status_cpy->demod_state;
    status->lna_ok = status_cpy->lna_ok;
    status->lna_gain = status_cpy->lna_gain;
    status->agc1_gain = status_cpy->agc1_gain;
    status->agc2_gain = status_cpy->agc2_gain;
    status->power_i = status_cpy->power_i;
    status->power_q = status_cpy->power_q;
    status->frequency_requested = status_cpy->frequency_requested;
    status->frequency_offset = status_cpy->frequency_offset;
    status->polarisation_supply = status_cpy->polarisation_supply;
    status->polarisation_horizontal = status_cpy->polarisation_horizontal;
    status->symbolrate_requested = status_cpy->symbolrate_requested;
    status->symbolrate = status_cpy->symbolrate;
    status->viterbi_error_rate = status_cpy->viterbi_error_rate;
    status->bit_error_rate = status_cpy->bit_error_rate;
    status->modulation_error_rate = status_cpy->modulation_error_rate;
    status->errors_bch_uncorrected = status_cpy->errors_bch_uncorrected;
    status->errors_bch_count = status_cpy->errors_bch_count;
    status->errors_ldpc_count = status_cpy->errors_ldpc_count;
    memcpy(status->constellation, status_cpy->constellation, (sizeof(uint8_t) * NUM_CONSTELLATIONS * 2));
    status->puncture_rate = status_cpy->puncture_rate;
    status->modcod = status_cpy->modcod;
    status->matype1 = status_cpy->matype1;
    status->matype2 = status_cpy->matype2;
    status->short_frame = status_cpy->short_frame;
    status->pilots = status_cpy->pilots;
    status->rolloff = status_cpy->rolloff;
    if (status_cpy->last_ts_or_reinit_monotonic != 0)
    {
        status->last_ts_or_reinit_monotonic = status_cpy->last_ts_or_reinit_monotonic;
    }

    /* Set monotonic value to signal new data - PRESERVE EXACT SIGNALING */
    status->last_updated_monotonic = monotonic_ms();
    /* Trigger pthread signal */
    pthread_cond_signal(&status->signal);
    pthread_mutex_unlock(&status->mutex);
}

/* -------------------------------------------------------------------------------------------------- */
/* RECEIVER STATE MACHINE FUNCTIONS                                                                   */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
static uint8_t process_demodulator_state_transition(uint8_t tuner, longmynd_status_t *status_cpy, uint8_t *err)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Processes demodulator state transitions and updates receiver state                              */
    /* Preserves exact state machine logic and error handling                                          */
    /*   tuner: tuner number (1 or 2) to determine which demodulator to use                           */
    /* status_cpy: local status copy                                                                   */
    /* err: error code pointer                                                                         */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t local_err = ERROR_NONE;
    uint8_t demod = (tuner == 1) ? STV0910_DEMOD_TOP : STV0910_DEMOD_BOTTOM;

    /* Read current demodulator scan state */
    if (*err == ERROR_NONE)
        local_err = stv0910_read_scan_state(demod, &status_cpy->demod_state);
    if (local_err != ERROR_NONE)
        *err = local_err;

    /* Process state transitions based on current state - PRESERVE EXACT LOGIC */
    switch (status_cpy->state)
    {
    case STATE_DEMOD_HUNTING:
        if (status_cpy->demod_state == DEMOD_FOUND_HEADER)
        {
            status_cpy->state = STATE_DEMOD_FOUND_HEADER;
        }
        else if (status_cpy->demod_state == DEMOD_S2)
        {
            status_cpy->state = STATE_DEMOD_S2;
        }
        else if (status_cpy->demod_state == DEMOD_S)
        {
            status_cpy->state = STATE_DEMOD_S;
        }
        else if ((status_cpy->demod_state != DEMOD_HUNTING) && (*err == ERROR_NONE))
        {
            printf("ERROR: demodulator returned a bad scan state\n");
            *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
        }
        break;

    case STATE_DEMOD_FOUND_HEADER:
        if (status_cpy->demod_state == DEMOD_HUNTING)
        {
            status_cpy->state = STATE_DEMOD_HUNTING;
        }
        else if (status_cpy->demod_state == DEMOD_S2)
        {
            status_cpy->state = STATE_DEMOD_S2;
        }
        else if (status_cpy->demod_state == DEMOD_S)
        {
            status_cpy->state = STATE_DEMOD_S;
        }
        else if ((status_cpy->demod_state != DEMOD_FOUND_HEADER) && (*err == ERROR_NONE))
        {
            printf("ERROR: demodulator returned a bad scan state\n");
            *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
        }
        break;

    case STATE_DEMOD_S2:
        if (status_cpy->demod_state == DEMOD_HUNTING)
        {
            status_cpy->state = STATE_DEMOD_HUNTING;
        }
        else if (status_cpy->demod_state == DEMOD_FOUND_HEADER)
        {
            status_cpy->state = STATE_DEMOD_FOUND_HEADER;
        }
        else if (status_cpy->demod_state == DEMOD_S)
        {
            status_cpy->state = STATE_DEMOD_S;
        }
        else if ((status_cpy->demod_state != DEMOD_S2) && (*err == ERROR_NONE))
        {
            printf("ERROR: demodulator returned a bad scan state\n");
            *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
        }
        break;

    case STATE_DEMOD_S:
        if (status_cpy->demod_state == DEMOD_HUNTING)
        {
            status_cpy->state = STATE_DEMOD_HUNTING;
        }
        else if (status_cpy->demod_state == DEMOD_FOUND_HEADER)
        {
            status_cpy->state = STATE_DEMOD_FOUND_HEADER;
        }
        else if (status_cpy->demod_state == DEMOD_S2)
        {
            status_cpy->state = STATE_DEMOD_S2;
        }
        else if ((status_cpy->demod_state != DEMOD_S) && (*err == ERROR_NONE))
        {
            printf("ERROR: demodulator returned a bad scan state\n");
            *err = ERROR_BAD_DEMOD_HUNT_STATE; /* not allowed to have any other states */
        }
        break;

    default:
        *err = ERROR_STATE; /* we should never get here so panic if we do */
        break;
    }

    return local_err;
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
    longmynd_status_t status_cpy_2;  /* Tuner 2 status copy */

    uint32_t last_ts_packet_count = 0;

    uint64_t last_i2c_loop = monotonic_ms();
    while (*err == ERROR_NONE && *thread_vars->main_err_ptr == ERROR_NONE)
    {
        /* Receiver State Machine Loop Timer - PRESERVE EXACT TIMING */
        do
        {
            /* Sleep for at least 10ms */
            usleep(100 * 1000);
        } while (monotonic_ms() < (last_i2c_loop + I2C_LOOP_MS));

        status_cpy.last_ts_or_reinit_monotonic = 0;

        /* Check if there's a new config */
        if (thread_vars->config->new_config)
        {
            handle_configuration_change(thread_vars, &config_cpy, &status_cpy, err);
        }

        /* Main receiver state machine - PRESERVE EXACT BEHAVIOR */
        /* Update status from hardware */
        if (*err == ERROR_NONE)
            *err = do_report(1, &status_cpy);

        /* Process state transitions */
        if (*err == ERROR_NONE)
            process_demodulator_state_transition(1, &status_cpy, err);

        /* Update TS packet tracking and synchronize status */
        update_status_synchronization(status, &status_cpy, &last_ts_packet_count);

        /* Output JSON demodulator cycle data if enabled */
        JSON_OUTPUT_DEMOD_CYCLE(1, &status_cpy);

        /* Tuner 2 status collection if enabled */
        if (config_cpy.tuner2_enabled && *err == ERROR_NONE)
        {
            /* Initialize tuner 2 status copy */
            status_cpy_2.last_ts_or_reinit_monotonic = 0;

            /* Update tuner 2 status from hardware */
            *err = do_report(2, &status_cpy_2);

            /* Process tuner 2 state transitions */
            if (*err == ERROR_NONE)
                process_demodulator_state_transition(2, &status_cpy_2, err);

            /* Output JSON demodulator cycle data for tuner 2 if enabled */
            JSON_OUTPUT_DEMOD_CYCLE(2, &status_cpy_2);
        }

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
/* MAIN FUNCTION INITIALIZATION HELPERS                                                               */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
static uint8_t initialize_signal_handlers(uint8_t *err_ptr)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Initializes signal handlers for clean shutdown                                                  */
    /* err_ptr: pointer to main error variable                                                         */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    sigterm_handler_err_ptr = err_ptr;
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    /* Ignore SIGPIPE on closed pipes (not available on Windows) */
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    return ERROR_NONE;
}

/* -------------------------------------------------------------------------------------------------- */
static uint8_t initialize_status_output(uint8_t (**status_write)(uint8_t, uint32_t, bool *),
                                       uint8_t (**status_string_write)(uint8_t, char *, bool *),
                                       bool *status_output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Initializes status output interface (FIFO, UDP, or MQTT)                                        */
    /* Preserves exact initialization logic and function pointer assignments                           */
    /* status_write: pointer to status write function pointer                                          */
    /* status_string_write: pointer to status string write function pointer                           */
    /* status_output_ready: status output ready flag                                                   */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    /* first setup the fifos, udp socket, ftdi and usb - PRESERVE EXACT LOGIC */
    if (longmynd_config.status_use_ip)
    {
        if (err == ERROR_NONE)
            err = udp_status_init(longmynd_config.status_ip_addr, longmynd_config.status_ip_port);
        *status_write = udp_status_write;
        *status_string_write = udp_status_string_write;
    }
    else if (longmynd_config.status_use_mqtt)
    {
        if (err == ERROR_NONE)
            err = mqttinit(longmynd_config.status_ip_addr);
        if(err>0) fprintf(stderr,"MQTT Broker not reachable\n");
        *status_write = mqtt_status_write;
        *status_string_write = mqtt_status_string_write;
    }
    else
    {
        if (err == ERROR_NONE)
            err = fifo_status_init(longmynd_config.status_fifo_path, status_output_ready);
        *status_write = fifo_status_write;
        *status_string_write = fifo_status_string_write;
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
static uint8_t initialize_worker_threads(uint8_t *err_ptr, thread_vars_t *thread_vars_ts,
                                        thread_vars_t *thread_vars_ts_parse, thread_vars_t *thread_vars_i2c,
                                        thread_vars_t *thread_vars_beep)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Initializes and starts all worker threads                                                       */
    /* Preserves exact thread creation logic and error handling                                        */
    /* err_ptr: pointer to main error variable                                                         */
    /* thread_vars_*: thread variable structures                                                       */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    /* Initialize thread variables - PRESERVE EXACT STRUCTURE INITIALIZATION */
    thread_vars_ts->main_err_ptr = err_ptr;
    thread_vars_ts->thread_err = ERROR_NONE;
    thread_vars_ts->config = &longmynd_config;
    thread_vars_ts->status = &longmynd_status;

    thread_vars_ts_parse->main_err_ptr = err_ptr;
    thread_vars_ts_parse->thread_err = ERROR_NONE;
    thread_vars_ts_parse->config = &longmynd_config;
    thread_vars_ts_parse->status = &longmynd_status;

    thread_vars_i2c->main_err_ptr = err_ptr;
    thread_vars_i2c->thread_err = ERROR_NONE;
    thread_vars_i2c->config = &longmynd_config;
    thread_vars_i2c->status = &longmynd_status;
    thread_vars_i2c->status2 = &longmynd_status2;  /* Tuner 2 status pointer */

    thread_vars_beep->main_err_ptr = err_ptr;
    thread_vars_beep->thread_err = ERROR_NONE;
    thread_vars_beep->config = &longmynd_config;
    thread_vars_beep->status = &longmynd_status;

    /* Create threads - PRESERVE EXACT CREATION ORDER AND ERROR HANDLING */
    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_ts, NULL, loop_ts, (void *)thread_vars_ts))
        {
            //pthread_setname_np(thread_ts, "TS Transport");
        }
        else
        {
            fprintf(stderr, "Error creating loop_ts pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_ts_parse, NULL, loop_ts_parse, (void *)thread_vars_ts_parse))
        {
            //pthread_setname_np(thread_ts_parse, "TS Parse");
        }
        else
        {
            fprintf(stderr, "Error creating loop_ts_parse pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_i2c, NULL, loop_i2c, (void *)thread_vars_i2c))
        {
            //pthread_setname_np(thread_i2c, "Receiver");
        }
        else
        {
            fprintf(stderr, "Error creating loop_i2c pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    if (err == ERROR_NONE)
    {
        if (0 == pthread_create(&thread_beep, NULL, loop_beep, (void *)thread_vars_beep))
        {
            // pthread_setname_np(thread_beep, "Beep Audio");
        }
        else
        {
            fprintf(stderr, "Error creating loop_beep pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    /* Create tuner 2 threads if enabled */
    if (err == ERROR_NONE && longmynd_config.tuner2_enabled) {
        /* Initialize tuner 2 thread variables */
        thread_vars_ts_tuner2.main_err_ptr = &err;
        thread_vars_ts_tuner2.thread_err = ERROR_NONE;
        thread_vars_ts_tuner2.config = &longmynd_config;
        thread_vars_ts_tuner2.status = &longmynd_status2;

        thread_vars_ts_parse_tuner2.main_err_ptr = &err;
        thread_vars_ts_parse_tuner2.thread_err = ERROR_NONE;
        thread_vars_ts_parse_tuner2.config = &longmynd_config;
        thread_vars_ts_parse_tuner2.status = &longmynd_status2;

        /* Create tuner 2 TS processing thread */
        if (0 == pthread_create(&thread_ts_tuner2, NULL, loop_ts_tuner2, (void *)&thread_vars_ts_tuner2))
        {
            printf("Flow: Tuner 2 TS processing thread created\n");
        }
        else
        {
            fprintf(stderr, "Error creating loop_ts_tuner2 pthread\n");
            err = ERROR_THREAD_ERROR;
        }

        /* Create tuner 2 TS parsing thread */
        if (err == ERROR_NONE && 0 == pthread_create(&thread_ts_parse_tuner2, NULL, loop_ts_parse_tuner2, (void *)&thread_vars_ts_parse_tuner2))
        {
            printf("Flow: Tuner 2 TS parsing thread created\n");
        }
        else
        {
            fprintf(stderr, "Error creating loop_ts_parse_tuner2 pthread\n");
            err = ERROR_THREAD_ERROR;
        }
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
static uint8_t run_main_status_loop(uint8_t (*status_write)(uint8_t, uint32_t, bool *),
                                   uint8_t (*status_string_write)(uint8_t, char *, bool *),
                                   bool *status_output_ready,
                                   const thread_vars_t *thread_vars_ts, const thread_vars_t *thread_vars_ts_parse,
                                   const thread_vars_t *thread_vars_i2c, const thread_vars_t *thread_vars_beep)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Runs the main status output loop and monitors thread health                                      */
    /* Preserves exact timing, mutex usage, and error handling                                         */
    /* status_write: status write function pointer                                                     */
    /* status_string_write: status string write function pointer                                       */
    /* status_output_ready: status output ready flag                                                   */
    /* thread_vars_*: thread variable structures                                                       */
    /* return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint64_t last_status_sent_monotonic = 0;
    longmynd_status_t longmynd_status_cpy;

    /* Initialise TS data re-init timer to prevent immediate reset - PRESERVE EXACT LOGIC */
    pthread_mutex_lock(&longmynd_status.mutex);
    longmynd_status.last_ts_or_reinit_monotonic = monotonic_ms();
    pthread_mutex_unlock(&longmynd_status.mutex);

    while (err == ERROR_NONE)
    {
        /* Test if new status data is available - PRESERVE EXACT LOGIC */
        if (longmynd_status.last_updated_monotonic != last_status_sent_monotonic)
        {
            /* Acquire lock on global status struct */
            pthread_mutex_lock(&longmynd_status.mutex);
            /* Clone status struct locally */
            memcpy(&longmynd_status_cpy, &longmynd_status, sizeof(longmynd_status_t));
            /* Release lock on global status struct */
            pthread_mutex_unlock(&longmynd_status.mutex);

            if (longmynd_config.status_use_ip || *status_output_ready)
            {
                /* Send all status via configured output interface from local copy */
                err = status_all_write(&longmynd_status_cpy, status_write, status_string_write, status_output_ready);
            }
            else if (!longmynd_config.status_use_ip && !*status_output_ready)
            {
                /* Try opening the fifo again */
                err = fifo_status_init(longmynd_config.status_fifo_path, status_output_ready);
            }

            /* Update monotonic timestamp last sent */
            last_status_sent_monotonic = longmynd_status_cpy.last_updated_monotonic;
        }
        else
        {
            /* Sleep 10ms - PRESERVE EXACT TIMING */
            usleep(100 * 1000);
        }

        /* Check for errors on threads - PRESERVE EXACT ERROR CHECKING */
        if (err == ERROR_NONE &&
            (thread_vars_ts->thread_err != ERROR_NONE || thread_vars_ts_parse->thread_err != ERROR_NONE ||
             thread_vars_beep->thread_err != ERROR_NONE || thread_vars_i2c->thread_err != ERROR_NONE))
        {
            err = ERROR_THREAD_ERROR;
        }

        /* TS timeout handling - PRESERVE EXACT TIMEOUT LOGIC */
        if (longmynd_config.ts_timeout != -1 &&
            monotonic_ms() > (longmynd_status.last_ts_or_reinit_monotonic + longmynd_config.ts_timeout))
        {
            /* Had a while with no TS data, reinit config to pull NIM search loops back in, or fix -S fascination */

            //printf("Flow: No-data timeout, re-init config.\n");
            //config_reinit(true); // !!!!!!!!!!!! FIXME IF DONT WORK

            /* We've queued up a reinit so reset the timer */
            pthread_mutex_lock(&longmynd_status.mutex);
            longmynd_status.last_ts_or_reinit_monotonic = monotonic_ms();
            pthread_mutex_unlock(&longmynd_status.mutex);
        }
    }

    return err;
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

    printf("Flow: main\n");

    /* Initialize register logging system */
    register_logging_init();

    /* Initialize JSON output system */
    JSON_OUTPUT_INIT();

    /* Initialize signal handlers */
    if (err == ERROR_NONE)
        err = initialize_signal_handlers(&err);

    /* Process command line arguments */
    if (err == ERROR_NONE)
        err = process_command_line(argc, argv, &longmynd_config);

    /* Configure register logging based on command line options */
    if (err == ERROR_NONE)
        register_logging_set_demod_suppression_disabled(longmynd_config.disable_demod_suppression);

    /* Configure JSON output based on command line options */
    if (err == ERROR_NONE) {
        json_output_config_t json_config;
        json_config.enabled = longmynd_config.json_output_enabled;
        json_config.format = (json_format_t)longmynd_config.json_output_format;
        json_config.interval_ms = longmynd_config.json_output_interval_ms;
        json_config.include_constellation = longmynd_config.json_include_constellation;
        json_config.include_timestamp = true;
        json_config.pretty_print = false;
        json_output_set_config(&json_config);
    }

    /* Initialize status output interface */
    if (err == ERROR_NONE)
        err = initialize_status_output(&status_write, &status_string_write, &status_output_ready);

    /* Initialize FTDI USB interface */
    if (err == ERROR_NONE)
        err = ftdi_init(longmynd_config.device_usb_bus, longmynd_config.device_usb_addr);

    /* Initialize tuner 2 FTDI interface if enabled */
    if (err == ERROR_NONE && longmynd_config.tuner2_enabled) {
        printf("Flow: Initializing Tuner 2 FTDI interface\n");
        err = ftdi_init_tuner2(longmynd_config.tuner2_device_usb_bus, longmynd_config.tuner2_device_usb_addr);
        if (err == ERROR_NONE) {
            printf("Flow: Tuner 2 FTDI device initialized successfully on USB bus/device=%d,%d\n",
                   longmynd_config.tuner2_device_usb_bus, longmynd_config.tuner2_device_usb_addr);
        } else {
            printf("ERROR: Failed to initialize Tuner 2 FTDI device\n");
        }
    }

    /* Initialize STV0910 mutex protection for thread-safe register access */
    if (err == ERROR_NONE) {
        stv0910_mutex_init();
        printf("Flow: STV0910 mutex protection initialized\n");
    }

    /* Initialize tuner 2 status structure if tuner 2 is enabled */
    if (err == ERROR_NONE && longmynd_config.tuner2_enabled) {
        memset(&longmynd_status2, 0, sizeof(longmynd_status_t));
        pthread_mutex_init(&longmynd_status2.mutex, NULL);
        pthread_cond_init(&longmynd_status2.signal, NULL);
        printf("Flow: Tuner 2 status structure initialized\n");
    }

    /* Initialize and start worker threads */
    thread_vars_t thread_vars_ts, thread_vars_ts_parse, thread_vars_i2c, thread_vars_beep;
    if (err == ERROR_NONE)
        err = initialize_worker_threads(&err, &thread_vars_ts, &thread_vars_ts_parse,
                                       &thread_vars_i2c, &thread_vars_beep);

    /* Run main status output loop */
    if (err == ERROR_NONE)
        err = run_main_status_loop(status_write, status_string_write, &status_output_ready,
                                  &thread_vars_ts, &thread_vars_ts_parse, &thread_vars_i2c, &thread_vars_beep);

    printf("Flow: Main loop aborted, waiting for threads.\n");

    /* No fatal errors are currently possible here, so don't currently check return values */
    pthread_join(thread_ts_parse, NULL);
    pthread_join(thread_ts, NULL);
    pthread_join(thread_i2c, NULL);
    pthread_join(thread_beep, NULL);

    /* Join tuner 2 threads if they were created */
    if (longmynd_config.tuner2_enabled) {
        pthread_join(thread_ts_parse_tuner2, NULL);
        pthread_join(thread_ts_tuner2, NULL);
        printf("Flow: Tuner 2 threads joined\n");
    }

    /* Cleanup STV0910 mutex protection */
    stv0910_mutex_destroy();
    printf("Flow: STV0910 mutex protection cleaned up\n");

    /* Cleanup tuner 2 status structure if it was initialized */
    if (longmynd_config.tuner2_enabled) {
        pthread_mutex_destroy(&longmynd_status2.mutex);
        pthread_cond_destroy(&longmynd_status2.signal);
        printf("Flow: Tuner 2 status structure cleaned up\n");
    }

    printf("Flow: All threads accounted for. Exiting cleanly.\n");

    return err;
}
