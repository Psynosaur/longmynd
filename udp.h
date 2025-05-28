/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: udp.h                                                                      */
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

#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <stdbool.h>

uint8_t udp_status_init(char *udp_ip, int udp_port);
uint8_t udp_ts_init(char *udp_ip, int udp_port);

uint8_t udp_status_write(uint8_t message, uint32_t data, bool *output_ready);
uint8_t udp_status_string_write(uint8_t message, char *data, bool *output_ready);
uint8_t udp_ts_write(uint8_t *buffer, uint32_t len, bool *output_ready);
uint8_t udp_bb_write(uint8_t *buffer, uint32_t len, bool *output_ready);
uint8_t udp_close(void);

/* Dual-tuner UDP functions */
uint8_t udp_ts_init_dual(char *udp_ip1, int udp_port1, char *udp_ip2, int udp_port2);
uint8_t udp_ts_write_tuner1(uint8_t *buffer, uint32_t len, bool *output_ready);
uint8_t udp_ts_write_tuner2(uint8_t *buffer, uint32_t len, bool *output_ready);
uint8_t udp_bb_write_tuner1(uint8_t *buffer, uint32_t len, bool *output_ready);
uint8_t udp_bb_write_tuner2(uint8_t *buffer, uint32_t len, bool *output_ready);
uint8_t udp_close_dual(void);

#endif

