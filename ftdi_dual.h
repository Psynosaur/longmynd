/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: ftdi_dual.h                                                                 */
/*    - Dual FTDI device management for independent tuner operation                                   */
/*    - Extends existing FTDI functionality to support two separate USB devices                       */
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

#ifndef FTDI_DUAL_H
#define FTDI_DUAL_H

#include <stdint.h>
#include <stdbool.h>

/* Tuner identification constants */
#define TUNER_1_ID 1
#define TUNER_2_ID 2

/* FTDI device context structure for dual-tuner support */
typedef struct {
    uint8_t tuner_id;
    uint8_t usb_bus;
    uint8_t usb_addr;
    bool initialized;
    bool active;
    /* USB device handles for this tuner */
    void *usb_device_handle_i2c;
    void *usb_device_handle_ts;
    void *usb_context_i2c;
    void *usb_context_ts;
} ftdi_device_context_t;

/* Global device contexts */
extern ftdi_device_context_t ftdi_tuner1_context;
extern ftdi_device_context_t ftdi_tuner2_context;

/* Dual FTDI initialization functions */
uint8_t ftdi_dual_init(uint8_t tuner1_bus, uint8_t tuner1_addr, 
                       uint8_t tuner2_bus, uint8_t tuner2_addr);
uint8_t ftdi_init_tuner(uint8_t tuner_id, uint8_t usb_bus, uint8_t usb_addr);
uint8_t ftdi_select_tuner(uint8_t tuner_id);
uint8_t ftdi_get_current_tuner(void);

/* Tuner-aware I2C operations */
uint8_t ftdi_i2c_read_reg16_tuner(uint8_t tuner_id, uint8_t addr, uint16_t reg, uint8_t *val);
uint8_t ftdi_i2c_write_reg16_tuner(uint8_t tuner_id, uint8_t addr, uint16_t reg, uint8_t val);
uint8_t ftdi_i2c_read_reg8_tuner(uint8_t tuner_id, uint8_t addr, uint8_t reg, uint8_t *val);
uint8_t ftdi_i2c_write_reg8_tuner(uint8_t tuner_id, uint8_t addr, uint8_t reg, uint8_t val);

/* Tuner-aware GPIO operations */
uint8_t ftdi_gpio_write_tuner(uint8_t tuner_id, uint8_t pin_id, bool pin_value);
uint8_t ftdi_nim_reset_tuner(uint8_t tuner_id);
uint8_t ftdi_set_polarisation_supply_tuner(uint8_t tuner_id, bool supply_enable, bool supply_horizontal);

/* Tuner-aware TS operations */
uint8_t ftdi_usb_ts_read_tuner(uint8_t tuner_id, uint8_t *buffer, uint16_t *len, uint32_t frame_size);

/* Device management */
uint8_t ftdi_cleanup_tuner(uint8_t tuner_id);
uint8_t ftdi_dual_cleanup(void);
bool ftdi_is_tuner_active(uint8_t tuner_id);

/* Context switching helpers */
uint8_t ftdi_switch_context(uint8_t tuner_id);
uint8_t ftdi_restore_context(void);

/* USB device handle management */
uint8_t ftdi_store_usb_handles(uint8_t tuner_id);
uint8_t ftdi_switch_usb_handles(uint8_t tuner_id);

#endif
