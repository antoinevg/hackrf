/*
 * Copyright 2012-2026 Great Scott Gadgets <info@greatscottgadgets.com>
 * Copyright 2012 Will Code <willcode4@gmail.com>
 * Copyright 2014 Jared Boone <jared@sharebrained.com>
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

/* 32 registers, each containing 10 bits of data. */
#define MAX2839_NUM_REGS            32
#define MAX2839_DATA_REGS_MAX_VALUE 1024

typedef enum {
	MAX2839_MODE_SHUTDOWN,
	MAX2839_MODE_STANDBY,
	MAX2839_MODE_TX,
	MAX2839_MODE_RX,
	MAX2839_MODE_RX_CAL,
	MAX2839_MODE_TX_CAL,
	MAX2839_MODE_CLKOUT,
} max2839_mode_t;

typedef struct _max2839_driver_t {
	trait_max283x_t trait;

	spi_bus_t* bus;
	gpio_t gpio_enable;
	gpio_t gpio_rxtx;
	void (*target_init)(struct _max2839_driver_t* const drv);
	void (*set_mode)(
		struct _max2839_driver_t* const drv,
		const max2839_mode_t new_mode);
	max2839_mode_t mode;
	uint16_t regs[MAX2839_NUM_REGS];
	uint32_t regs_dirty;
} max2839_driver_t;

trait_max283x_t* max2839_driver_new(max2839_driver_t* const self);
