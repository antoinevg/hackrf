/*
 * Copyright 2025-2026 Great Scott Gadgets <info@greatscottgadgets.com>
 *
 * This file is part of HackRF.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <stdint.h>

#include "gpio.h"
#include "spi_bus.h"
#include "trait_max283x.h"

/* 16 registers, each containing 14 bits of data. */
#define MAX2831_NUM_REGS            16
#define MAX2831_DATA_REGS_MAX_VALUE 16384

typedef enum {
	MAX2831_MODE_SHUTDOWN,
	MAX2831_MODE_STANDBY,
	MAX2831_MODE_TX,
	MAX2831_MODE_RX,
	MAX2831_MODE_RX_CALIBRATION,
	MAX2831_MODE_TX_CALIBRATION,
} max2831_mode_t;

typedef enum {
	MAX2831_RX_HPF_100_HZ = 0,
	MAX2831_RX_HPF_4_KHZ = 1,
	MAX2831_RX_HPF_30_KHZ = 2,
	MAX2831_RX_HPF_600_KHZ = 3,
} max2831_rx_hpf_freq_t;

typedef struct _max2831_driver_t {
	trait_max283x_t
		trait; // TODO decide trait or trait_max283x in case we want to support multiple traits?

	spi_bus_t* bus;
	gpio_t gpio_enable;
	gpio_t gpio_rxtx;
	gpio_t gpio_rxhp;
	gpio_t gpio_ld;
	void (*target_init)(struct _max2831_driver_t* const drv);
	void (*set_mode)(
		struct _max2831_driver_t* const drv,
		const max2831_mode_t new_mode);
	max2831_mode_t mode;
	uint16_t regs[MAX2831_NUM_REGS];
	uint16_t regs_dirty;
	uint32_t desired_lpf_bw;
} max2831_driver_t;

trait_max283x_t* max2831_driver_new(max2831_driver_t* const self);
