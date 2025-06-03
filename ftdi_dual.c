/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: ftdi_dual.c                                                                 */
/*    - Dual FTDI device management for independent tuner operation                                   */
/*    - Implements context switching between two separate FTDI USB devices                            */
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
#include <pthread.h>
#include "ftdi_dual.h"
#include "ftdi.h"
#include "ftdi_usb.h"
#include "errors.h"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

/* Device contexts for both tuners */
ftdi_device_context_t ftdi_tuner1_context = {
    .tuner_id = TUNER_1_ID,
    .usb_bus = 0,
    .usb_addr = 0,
    .initialized = false,
    .active = false
};

ftdi_device_context_t ftdi_tuner2_context = {
    .tuner_id = TUNER_2_ID,
    .usb_bus = 0,
    .usb_addr = 0,
    .initialized = false,
    .active = false
};

/* Current active tuner context */
static uint8_t current_tuner_id = TUNER_1_ID;
static pthread_mutex_t ftdi_context_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_dual_init(uint8_t tuner1_bus, uint8_t tuner1_addr, 
                       uint8_t tuner2_bus, uint8_t tuner2_addr)
{
/* -------------------------------------------------------------------------------------------------- */
/* Initialize both FTDI devices for dual-tuner operation                                             */
/* tuner1_bus/addr: USB bus and address for tuner 1 device                                           */
/* tuner2_bus/addr: USB bus and address for tuner 2 device                                           */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    printf("Flow: FTDI dual init - Tuner 1: %d,%d  Tuner 2: %d,%d\n", 
           tuner1_bus, tuner1_addr, tuner2_bus, tuner2_addr);

    /* Initialize tuner 1 (always required) */
    if (err == ERROR_NONE) {
        err = ftdi_init_tuner(TUNER_1_ID, tuner1_bus, tuner1_addr);
        if (err == ERROR_NONE) {
            ftdi_tuner1_context.usb_bus = tuner1_bus;
            ftdi_tuner1_context.usb_addr = tuner1_addr;
            ftdi_tuner1_context.initialized = true;
            ftdi_tuner1_context.active = true;
            current_tuner_id = TUNER_1_ID;
        }
    }

    /* Initialize tuner 2 (if different device specified) */
    if (err == ERROR_NONE && (tuner2_bus != 0 || tuner2_addr != 0)) {
        if (tuner2_bus != tuner1_bus || tuner2_addr != tuner1_addr) {
            err = ftdi_init_tuner(TUNER_2_ID, tuner2_bus, tuner2_addr);
            if (err == ERROR_NONE) {
                ftdi_tuner2_context.usb_bus = tuner2_bus;
                ftdi_tuner2_context.usb_addr = tuner2_addr;
                ftdi_tuner2_context.initialized = true;
                ftdi_tuner2_context.active = true;
            }
        } else {
            printf("ERROR: Tuner 2 cannot use same USB device as Tuner 1\n");
            err = ERROR_FTDI_USB_BAD_DEVICE_NUM;
        }
    }

    if (err != ERROR_NONE) {
        printf("ERROR: FTDI dual init failed\n");
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_init_tuner(uint8_t tuner_id, uint8_t usb_bus, uint8_t usb_addr)
{
/* -------------------------------------------------------------------------------------------------- */
/* Initialize a specific tuner's FTDI device                                                         */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* usb_bus/addr: USB bus and address for the device                                                  */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    printf("Flow: FTDI init tuner %d at %d,%d\n", tuner_id, usb_bus, usb_addr);

    /* Switch to the target tuner context */
    pthread_mutex_lock(&ftdi_context_mutex);
    
    /* Use existing FTDI initialization but with tuner-specific context */
    err = ftdi_init(usb_bus, usb_addr);
    
    if (err == ERROR_NONE) {
        printf("      Status: Tuner %d FTDI initialized successfully\n", tuner_id);
    } else {
        printf("ERROR: Failed to initialize FTDI for tuner %d\n", tuner_id);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
static uint8_t ftdi_select_tuner_internal(uint8_t tuner_id)
{
/* -------------------------------------------------------------------------------------------------- */
/* Internal function to select tuner - assumes mutex is already held                                 */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    if (tuner_id == TUNER_1_ID && ftdi_tuner1_context.initialized) {
        current_tuner_id = TUNER_1_ID;
    } else if (tuner_id == TUNER_2_ID && ftdi_tuner2_context.initialized) {
        current_tuner_id = TUNER_2_ID;
    } else {
        printf("ERROR: Cannot select tuner %d - not initialized\n", tuner_id);
        err = ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_select_tuner(uint8_t tuner_id)
{
/* -------------------------------------------------------------------------------------------------- */
/* Select the active tuner for subsequent operations                                                 */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    pthread_mutex_lock(&ftdi_context_mutex);
    err = ftdi_select_tuner_internal(tuner_id);
    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_get_current_tuner(void)
{
/* -------------------------------------------------------------------------------------------------- */
/* Get the currently selected tuner ID                                                               */
/* return: current tuner ID (TUNER_1_ID or TUNER_2_ID)                                              */
/* -------------------------------------------------------------------------------------------------- */
    return current_tuner_id;
}

/* -------------------------------------------------------------------------------------------------- */
bool ftdi_is_tuner_active(uint8_t tuner_id)
{
/* -------------------------------------------------------------------------------------------------- */
/* Check if a specific tuner is active and initialized                                               */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* return: true if tuner is active, false otherwise                                                  */
/* -------------------------------------------------------------------------------------------------- */
    if (tuner_id == TUNER_1_ID) {
        return ftdi_tuner1_context.initialized && ftdi_tuner1_context.active;
    } else if (tuner_id == TUNER_2_ID) {
        return ftdi_tuner2_context.initialized && ftdi_tuner2_context.active;
    }
    return false;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_i2c_read_reg16_tuner(uint8_t tuner_id, uint8_t addr, uint16_t reg, uint8_t *val)
{
/* -------------------------------------------------------------------------------------------------- */
/* Read 16-bit I2C register with tuner context switching                                             */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* addr: I2C device address                                                                          */
/* reg: 16-bit register address                                                                      */
/* val: pointer to store read value                                                                  */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_i2c_read_reg16(addr, reg, val);
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_i2c_write_reg16_tuner(uint8_t tuner_id, uint8_t addr, uint16_t reg, uint8_t val)
{
/* -------------------------------------------------------------------------------------------------- */
/* Write 16-bit I2C register with tuner context switching                                            */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* addr: I2C device address                                                                          */
/* reg: 16-bit register address                                                                      */
/* val: value to write                                                                               */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_i2c_write_reg16(addr, reg, val);
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_i2c_read_reg8_tuner(uint8_t tuner_id, uint8_t addr, uint8_t reg, uint8_t *val)
{
/* -------------------------------------------------------------------------------------------------- */
/* Read 8-bit I2C register with tuner context switching                                              */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* addr: I2C device address                                                                          */
/* reg: 8-bit register address                                                                       */
/* val: pointer to store read value                                                                  */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_i2c_read_reg8(addr, reg, val);
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_i2c_write_reg8_tuner(uint8_t tuner_id, uint8_t addr, uint8_t reg, uint8_t val)
{
/* -------------------------------------------------------------------------------------------------- */
/* Write 8-bit I2C register with tuner context switching                                             */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* addr: I2C device address                                                                          */
/* reg: 8-bit register address                                                                       */
/* val: value to write                                                                               */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_i2c_write_reg8(addr, reg, val);
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_gpio_write_tuner(uint8_t tuner_id, uint8_t pin_id, bool pin_value)
{
/* -------------------------------------------------------------------------------------------------- */
/* Write GPIO pin with tuner context switching                                                       */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* pin_id: GPIO pin identifier                                                                       */
/* pin_value: value to write to pin                                                                  */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_gpio_write(pin_id, pin_value);
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_nim_reset_tuner(uint8_t tuner_id)
{
/* -------------------------------------------------------------------------------------------------- */
/* Reset NIM for specific tuner                                                                      */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_nim_reset();
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_set_polarisation_supply_tuner(uint8_t tuner_id, bool supply_enable, bool supply_horizontal)
{
/* -------------------------------------------------------------------------------------------------- */
/* Set polarisation supply for specific tuner                                                        */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* supply_enable: enable/disable supply                                                              */
/* supply_horizontal: horizontal/vertical polarisation                                               */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_set_polarisation_supply(supply_enable, supply_horizontal);
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_usb_ts_read_tuner(uint8_t tuner_id, uint8_t *buffer, uint16_t *len, uint32_t frame_size)
{
/* -------------------------------------------------------------------------------------------------- */
/* Read transport stream data for specific tuner                                                     */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* buffer: buffer to store TS data                                                                   */
/* len: pointer to length of data read                                                               */
/* frame_size: expected frame size                                                                   */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    uint8_t saved_tuner = current_tuner_id;

    if (!ftdi_is_tuner_active(tuner_id)) {
        return ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_lock(&ftdi_context_mutex);

    if (current_tuner_id != tuner_id) {
        err = ftdi_select_tuner_internal(tuner_id);
    }

    if (err == ERROR_NONE) {
        err = ftdi_usb_ts_read(buffer, len, frame_size);
    }

    /* Restore previous context if needed */
    if (saved_tuner != tuner_id && err == ERROR_NONE) {
        ftdi_select_tuner_internal(saved_tuner);
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_cleanup_tuner(uint8_t tuner_id)
{
/* -------------------------------------------------------------------------------------------------- */
/* Cleanup resources for specific tuner                                                              */
/* tuner_id: TUNER_1_ID or TUNER_2_ID                                                                */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    pthread_mutex_lock(&ftdi_context_mutex);

    if (tuner_id == TUNER_1_ID) {
        ftdi_tuner1_context.initialized = false;
        ftdi_tuner1_context.active = false;
        printf("Flow: Tuner 1 FTDI cleanup completed\n");
    } else if (tuner_id == TUNER_2_ID) {
        ftdi_tuner2_context.initialized = false;
        ftdi_tuner2_context.active = false;
        printf("Flow: Tuner 2 FTDI cleanup completed\n");
    } else {
        err = ERROR_FTDI_USB_BAD_DEVICE_NUM;
    }

    pthread_mutex_unlock(&ftdi_context_mutex);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t ftdi_dual_cleanup(void)
{
/* -------------------------------------------------------------------------------------------------- */
/* Cleanup all dual FTDI resources                                                                   */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    printf("Flow: FTDI dual cleanup\n");

    /* Cleanup both tuners */
    if (ftdi_tuner1_context.initialized) {
        ftdi_cleanup_tuner(TUNER_1_ID);
    }

    if (ftdi_tuner2_context.initialized) {
        ftdi_cleanup_tuner(TUNER_2_ID);
    }

    /* Reset to default state */
    current_tuner_id = TUNER_1_ID;

    return err;
}
