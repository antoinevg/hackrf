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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fixed_point.h"

typedef enum {
	MAX283x_MODE_SHUTDOWN,
	MAX283x_MODE_STANDBY,
	MAX283x_MODE_TX,
	MAX283x_MODE_RX,
	MAX283x_MODE_RX_CAL,
	MAX283x_MODE_TX_CAL,
	MAX283x_MODE_CLKOUT,
} max283x_mode_t;

typedef enum {
	MAX283x_RX_HPF_100_HZ = 0,
	MAX283x_RX_HPF_4_KHZ = 1,
	MAX283x_RX_HPF_30_KHZ = 2,
	MAX283x_RX_HPF_600_KHZ = 3,
} max283x_rx_hpf_freq_t;

#define container_of(ptr, type, member) ((type*) ((char*) (ptr) -offsetof(type, member)))

typedef struct {
	const struct _vtable_max283x_t* vtable;
} trait_max283x_t;

typedef struct _vtable_max283x_t {
	/* Initialize chip. */
	void (*setup)(trait_max283x_t* const trait);

	/* Returns the number of registers supported by the driver. */
	uint16_t (*num_regs)(trait_max283x_t* const trait);

	/* Returns the maximum data register value supported by the driver. */
	uint16_t (*data_regs_max_value)(trait_max283x_t* const trait);

	/* Returns the maximum data register value supported by the driver. */
	uint16_t (*reg_read)(trait_max283x_t* const trait, uint8_t r);

	/* Read a register via SPI. Save a copy to memory and return
	 * value. Mark clean. */
	void (*reg_write)(trait_max283x_t* const trait, uint8_t r, uint16_t v);

	/* Write all dirty registers via SPI from memory. Mark all clean. Some
	 * operations require registers to be written in a certain order. Use
	 * provided routines for those operations. */
	void (*regs_commit)(trait_max283x_t* const trait);

	max283x_mode_t (*mode)(trait_max283x_t* const trait);
	void (*set_mode)(trait_max283x_t* const trait, const max283x_mode_t new_mode);

	/* Turn on/off all chip functions. Does not control oscillator and CLKOUT */
	void (*start)(trait_max283x_t* const trait);
	void (*stop)(trait_max283x_t* const trait);

	/* Set frequency in Hz. Frequency setting is a multi-step function
	 * where order of register writes matters. */
	// TODO void (*set_frequency)(trait_max283x_t* const trait, uint32_t freq);
	fp_40_24_t (*set_frequency)(
		trait_max283x_t* const trait,
		fp_40_24_t freq,
		bool program);

	uint32_t (*set_lpf_bandwidth)(
		trait_max283x_t* const trait,
		const max283x_mode_t mode,
		const uint32_t bandwidth_hz);

	bool (*set_lna_gain)(trait_max283x_t* const trait, const uint32_t gain_db);
	bool (*set_vga_gain)(trait_max283x_t* const trait, const uint32_t gain_db);
	bool (*set_txvga_gain)(trait_max283x_t* const trait, const uint32_t gain_db);

	void (*tx)(trait_max283x_t* const trait);
	void (*rx)(trait_max283x_t* const trait);

	/* Set MAX2831 receiver high-pass filter corner frequency in Hz */
	void (*set_rx_hpf_frequency)(
		trait_max283x_t* const trait,
		const max283x_rx_hpf_freq_t freq);

	/* Perform MAX2831 TX and RX calibration. */
	void (*tx_calibration)(trait_max283x_t* const trait);
	void (*rx_calibration)(trait_max283x_t* const trait);

} vtable_max283x_t;

static inline void trait_max283x_setup(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->setup(trait);
}

static inline uint16_t trait_max283x_num_regs(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->num_regs(trait);
}

static inline uint16_t trait_max283x_data_regs_max_value(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->data_regs_max_value(trait);
}

static inline uint16_t trait_max283x_reg_read(trait_max283x_t* const trait, uint8_t r)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->reg_read(trait, r);
}

static inline void trait_max283x_reg_write(
	trait_max283x_t* const trait,
	uint8_t r,
	uint16_t v)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->reg_write(trait, r, v);
}

static inline void trait_max283x_regs_commit(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->regs_commit(trait);
}

static inline max283x_mode_t trait_max283x_mode(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->mode(trait);
}

static inline void trait_max283x_set_mode(
	trait_max283x_t* const trait,
	const max283x_mode_t new_mode)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->set_mode(trait, new_mode);
}

static inline void trait_max283x_start(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->start(trait);
}

static inline void trait_max283x_stop(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->stop(trait);
}

static inline fp_40_24_t trait_max283x_set_frequency(
	trait_max283x_t* const trait,
	fp_40_24_t freq,
	bool program)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->set_frequency(trait, freq, program);
}

static inline uint32_t trait_max283x_set_lpf_bandwidth(
	trait_max283x_t* const trait,
	const max283x_mode_t mode,
	const uint32_t bandwidth_hz)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->set_lpf_bandwidth(trait, mode, bandwidth_hz);
}

static inline bool trait_max283x_set_lna_gain(
	trait_max283x_t* const trait,
	const uint32_t gain_db)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->set_lna_gain(trait, gain_db);
}

static inline bool trait_max283x_set_vga_gain(
	trait_max283x_t* const trait,
	const uint32_t gain_db)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->set_vga_gain(trait, gain_db);
}

static inline bool trait_max283x_set_txvga_gain(
	trait_max283x_t* const trait,
	const uint32_t gain_db)
{
	if (trait == NULL) {
		return 0;
	}
	return trait->vtable->set_txvga_gain(trait, gain_db);
}

static inline void trait_max283x_tx(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->tx(trait);
}

static inline void trait_max283x_rx(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->rx(trait);
}

static inline void trait_max283x_set_rx_hpf_frequency(
	trait_max283x_t* const trait,
	const max283x_rx_hpf_freq_t freq)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->set_rx_hpf_frequency(trait, freq);
}

static inline void trait_max283x_tx_calibration(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->tx_calibration(trait);
}

static inline void trait_max283x_rx_calibration(trait_max283x_t* const trait)
{
	if (trait == NULL) {
		return;
	}
	trait->vtable->rx_calibration(trait);
}
