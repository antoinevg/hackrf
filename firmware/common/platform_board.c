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

#include "platform_board.h"

#include <stddef.h>

#include "platform_detect.h"
#include "platform_gpio.h"
#include "max2831.h"
#include "max2831_target.h"
#include "max2837.h"
#include "max2837_target.h"
#include "max2839.h"
#include "max2839_target.h"

// TODO compare against #1710 where it lives in drivers.c
extern spi_bus_t spi_bus_ssp1;

const platform_board_t* platform_board(void)
{
	static const platform_board_t* _platform_board;
	if (_platform_board != NULL) {
		return _platform_board;
	}

	static platform_board_t board;
	board_id_t board_id = detected_platform();

	const platform_gpio_t* gpio = platform_gpio();

	// max283x
	switch (board_id) {
#if defined(PRALINE)
	case BOARD_ID_PRALINE:
		static max2831_driver_t _max2831;
		board.dyn_max283x = max2831_driver_new(&_max2831);
		// configure bus
		_max2831.bus = &spi_bus_ssp1;
		// TODO do we actually need these to be configurable?
		_max2831.target_init = max2831_target_init;
		_max2831.set_mode = max2831_target_set_mode;
		// configure gpio
		_max2831.gpio_enable = gpio->max283x_enable;
		_max2831.gpio_rxtx = gpio->max283x_rx_enable;
		_max2831.gpio_rxhp = gpio->max2831_rxhp;
		_max2831.gpio_ld = gpio->max2831_ld;
		break;
#endif
#if defined(HACKRF_ONE)
	case BOARD_ID_HACKRF1_R9:
		static max2839_driver_t _max2839;
		board.dyn_max283x = max2839_driver_new(&_max2839);
		_max2839.bus = &spi_bus_ssp1;
		_max2839.target_init = max2839_target_init;
		_max2839.set_mode = max2839_target_set_mode;
		_max2839.gpio_enable = gpio->max283x_enable;
		_max2839.gpio_rxtx = gpio->max283x_rx_enable;
		break;
	case BOARD_ID_HACKRF1_OG:
		static max2837_driver_t _max2837;
		board.dyn_max283x = max2837_driver_new(&_max2837);
		_max2837.bus = &spi_bus_ssp1;
		_max2837.target_init = max2837_target_init;
		_max2837.set_mode = max2837_target_set_mode;
		_max2837.gpio_enable = gpio->max283x_enable;
		_max2837.gpio_rx_enable = gpio->max283x_rx_enable;
		_max2837.gpio_tx_enable = gpio->max283x_tx_enable;
		break;
#endif
	default:
		break;
	}

	// mixer
	// ...

	_platform_board = &board;

	return _platform_board;
}
