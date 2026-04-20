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

/*
 * 'gcc -DTEST -DDEBUG -O2 -o test max2837.c' prints out what test
 * program would do if it had a real spi library
 *
 * 'gcc -DTEST -DBUS_PIRATE -O2 -o test max2837.c' prints out bus
 * pirate commands to do the same thing.
 */

#include "max2837.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fixed_point.h"
#include "max2837_regs.def" // private register def macros
#include "selftest.h"
#include "trait_max283x.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Default register values. */
static const uint16_t max2837_regs_default[MAX2837_NUM_REGS] = {
	0x150, /* 0 */
	0x002, /* 1:  data sheet says 0x002 but read 0x1c2 */
	0x1f4, /* 2 */
	0x1b9, /* 3 */
	0x00a, /* 4 */
	0x080, /* 5 */
	0x006, /* 6 */
	0x000, /* 7 */
	0x080, /* 8 */
	0x018, /* 9 */
	0x058, /* 10 */
	0x016, /* 11 */
	0x24f, /* 12 */
	0x150, /* 13 */
	0x1c5, /* 14 */
	0x081, /* 15 */
	0x01c, /* 16 */
	0x155, /* 17 */
	0x155, /* 18 */
	0x153, /* 19 */
	0x241, /* 20 */
	/*
	 * Charge Pump Common Mode Enable bit (0) of register 21 must be set or TX
	 * does not work.  Page 1 of the SPI doc says not to set it (0x02c), but
	 * page 21 says it should be set by default (0x02d).
	 */
	0x02d,  /* 21 */
	0x1a9,  /* 22 */
	0x24f,  /* 23 */
	0x180,  /* 24 */
	0x100,  /* 25:  data sheet says 0x100 but read 0x10a */
	0x3ca,  /* 26 */
	0x3e3,  /* 27:  data sheet says 0x100 but read 0x3f3 */
	0x0c0,  /* 28 */
	0x3f0,  /* 29 */
	0x080,  /* 30:  data sheet says 0x080 but read 0x092 */
	0x000}; /* 31:  data sheet says 0x000 but read 0x1ae */

static const uint8_t max2837_regs_skip_verify[] = {1, 25, 27, 30, 31};

/* Forward declarations. */
static void max2837_reg_write(trait_max283x_t* const trait, uint8_t r, uint16_t v);
static void max2837_regs_commit(trait_max283x_t* const trait);
static void max2837_set_mode(trait_max283x_t* const trait, const max283x_mode_t new_mode);

/* impl max2837_driver_t { ... */

static uint16_t max2837_read(max2837_driver_t* const self, uint8_t r)
{
	uint16_t value = (1 << 15) | (r << 10);
	spi_bus_transfer(self->bus, &value, 1);
	return value & 0x3ff;
}

/* Set up all registers according to defaults specified in docs. */
static inline void max2837_reg_commit(max2837_driver_t* const self, uint8_t r)
{
	max2837_reg_write(&self->trait, r, self->regs[r]);
}

static void max2837_init(max2837_driver_t* const self)
{
	self->target_init(self);
	max2837_set_mode(&self->trait, (max283x_mode_t) MAX2837_MODE_SHUTDOWN);

	memcpy(self->regs, max2837_regs_default, sizeof(self->regs));
	self->regs_dirty = 0xffffffff;

	/* Write default register values to chip. */
	max2837_regs_commit(&self->trait);

	/* Read back registers to verify. */
	selftest.max283x_readback_total_registers = MAX2837_NUM_REGS;
	for (int r = 0; r < MAX2837_NUM_REGS; r++) {
		for (unsigned int i = 0; i < sizeof(max2837_regs_skip_verify); i++) {
			if (max2837_regs_skip_verify[i] == r) {
				goto next;
			}
		}
		uint16_t value = max2837_read(self, r);
		if (value != self->regs[r]) {
			selftest.max283x_readback_bad_value = value;
			selftest.max283x_readback_expected_value = self->regs[r];
			break;
		}
next:
		selftest.max283x_readback_register_count = r + 1;
	}

	if (selftest.max283x_readback_register_count < MAX2837_NUM_REGS) {
		selftest.report.pass = false;
	}
}

/* impl trait_max283x_t for max2837_driver_t { ... */

/*
 * Set up pins for GPIO and SPI control, configure SSP peripheral for SPI, and
 * set our own default register configuration.
 */
static void max2837_setup(trait_max283x_t* const trait)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	max2837_init(self);

	/* Use SPI control instead of B1-B7 pins for gain settings. */
	set_MAX2837_TXVGA_GAIN_SPI_EN(self, 1);
	set_MAX2837_TXVGA_GAIN_MSB_SPI_EN(self, 1);
	//set_MAX2837_TXVGA_GAIN(0x3f); /* maximum attenuation */
	set_MAX2837_TXVGA_GAIN(self, 0x00); /* minimum attenuation */
	set_MAX2837_VGAMUX_enable(self, 1);
	set_MAX2837_VGA_EN(self, 1);
	set_MAX2837_HPC_RXGAIN_EN(self, 0);
	set_MAX2837_HPC_STOP(self, MAX2837_STOP_1K);
	set_MAX2837_LNAgain_SPI_EN(self, 1);
	set_MAX2837_LNAgain(self, MAX2837_LNAgain_MAX); /* maximum gain */
	set_MAX2837_VGAgain_SPI_EN(self, 1);
	set_MAX2837_VGA(self, 0x18); /* reasonable gain for noisy 2.4GHz environment */

	/* maximum rx output common-mode voltage */
	set_MAX2837_BUFF_VCM(self, MAX2837_BUFF_VCM_1_25);

	/* configure baseband filter for 8 MHz TX */
	set_MAX2837_LPF_EN(self, 1);
	set_MAX2837_ModeCtrl(self, MAX2837_ModeCtrl_RxLPF);
	set_MAX2837_FT(self, MAX2837_FT_5M);

	max2837_regs_commit(trait);
}

static uint16_t max2837_num_regs(trait_max283x_t* const trait)
{
	(void) trait;
	return MAX2837_NUM_REGS;
}

static uint16_t max2837_data_regs_max_value(trait_max283x_t* const trait)
{
	(void) trait;
	return MAX2837_DATA_REGS_MAX_VALUE;
}

static void max2837_write(trait_max283x_t* const trait, uint8_t r, uint16_t v)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	uint16_t value = (r << 10) | (v & 0x3ff);
	spi_bus_transfer(self->bus, &value, 1);
}

static uint16_t max2837_reg_read(trait_max283x_t* const trait, uint8_t r)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	if ((self->regs_dirty >> r) & 0x1) {
		self->regs[r] = max2837_read(self, r);
	};
	return self->regs[r];
}

static void max2837_reg_write(trait_max283x_t* const trait, uint8_t r, uint16_t v)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	self->regs[r] = v;
	max2837_write(trait, r, v);
	MAX2837_REG_SET_CLEAN(self, r);
}

static void max2837_regs_commit(trait_max283x_t* const trait)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	int r;
	for (r = 0; r < MAX2837_NUM_REGS; r++) {
		if ((self->regs_dirty >> r) & 0x1) {
			max2837_reg_commit(self, r);
		}
	}
}

static void max2837_set_mode(trait_max283x_t* const trait, const max283x_mode_t new_mode)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	self->set_mode(self, (max2837_mode_t) new_mode);
}

static max283x_mode_t max2837_mode(trait_max283x_t* const trait)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	return (max283x_mode_t) self->mode;
}

static void max2837_start(trait_max283x_t* const trait)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	set_MAX2837_EN_SPI(self, 1);
	max2837_regs_commit(trait);
	max2837_set_mode(trait, (max283x_mode_t) MAX2837_MODE_STANDBY);
}

static void max2837_tx(trait_max283x_t* const trait)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	set_MAX2837_ModeCtrl(self, MAX2837_ModeCtrl_TxLPF);
	max2837_regs_commit(trait);
	max2837_set_mode(trait, (max283x_mode_t) MAX2837_MODE_TX);
}

static void max2837_rx(trait_max283x_t* const trait)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	set_MAX2837_ModeCtrl(self, MAX2837_ModeCtrl_RxLPF);
	max2837_regs_commit(trait);
	max2837_set_mode(trait, (max283x_mode_t) MAX2837_MODE_RX);
}

static void max2837_stop(trait_max283x_t* const trait)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	set_MAX2837_EN_SPI(self, 0);
	max2837_regs_commit(trait);
	max2837_set_mode(trait, (max283x_mode_t) MAX2837_MODE_SHUTDOWN);
}

/* Assume 40 MHz reference clock with R divider of 1. */
#define PFD_FREQ_HZ (40ULL * (1000ULL * 1000ULL))

#define MIN_FREQ FP_MHZ(2000)
#define MAX_FREQ FP_MHZ(3000)

static fp_40_24_t max2837_set_frequency(
	trait_max283x_t* const trait,
	fp_40_24_t freq,
	bool program)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	uint8_t band;
	uint8_t lna_band;
	uint64_t div;

	freq = MIN(freq, MAX_FREQ);
	freq = MAX(freq, MIN_FREQ);

	/* Select band. Allow tuning outside specified bands. */
	if (freq < FP_MHZ(2400)) {
		band = MAX2837_LOGEN_BSW_2_3;
		lna_band = MAX2837_LNAband_2_4;
	} else if (freq < FP_MHZ(2500)) {
		band = MAX2837_LOGEN_BSW_2_4;
		lna_band = MAX2837_LNAband_2_4;
	} else if (freq < FP_MHZ(2600)) {
		band = MAX2837_LOGEN_BSW_2_5;
		lna_band = MAX2837_LNAband_2_6;
	} else {
		band = MAX2837_LOGEN_BSW_2_6;
		lna_band = MAX2837_LNAband_2_6;
	}

	fp_40_24_t vco = (freq * 4) / 3;

	vco += ((PFD_FREQ_HZ * FP_ONE_HZ) >> 21); /* round to nearest frequency */
	div = vco / PFD_FREQ_HZ;

	/*
	 * Shift from 40.24 fixed-point to 44.20 to match 20-bit fractional
	 * divider.
	 */
	div = div >> 4;

	if (program) {
		/* Band settings */
		set_MAX2837_LOGEN_BSW(self, band);
		set_MAX2837_LNAband(self, lna_band);

		/*
		 * Write order matters here, so commit INT and FRAC_HI before
		 * committing FRAC_LO, which is the trigger for VCO auto-select.
		 */
		set_MAX2837_SYN_INT(self, (div >> 20) & 0xff);
		set_MAX2837_SYN_FRAC_HI(self, (div >> 10) & 0x3ff);
		max2837_regs_commit(trait);
		set_MAX2837_SYN_FRAC_LO(self, div & 0x3ff);
		max2837_regs_commit(trait);
	}

	return ((PFD_FREQ_HZ * 3) / 4) * (div << 4);
}

typedef struct {
	uint32_t bandwidth_hz;
	uint32_t ft;
} max2837_ft_t;

// clang-format off
static const max2837_ft_t max2837_ft[] = {
	{  1750000, MAX2837_FT_1_75M },
	{  2500000, MAX2837_FT_2_5M },
	{  3500000, MAX2837_FT_3_5M },
	{  5000000, MAX2837_FT_5M },
	{  5500000, MAX2837_FT_5_5M },
	{  6000000, MAX2837_FT_6M },
	{  7000000, MAX2837_FT_7M },
	{  8000000, MAX2837_FT_8M },
	{  9000000, MAX2837_FT_9M },
	{ 10000000, MAX2837_FT_10M },
	{ 12000000, MAX2837_FT_12M },
	{ 14000000, MAX2837_FT_14M },
	{ 15000000, MAX2837_FT_15M },
	{ 20000000, MAX2837_FT_20M },
	{ 24000000, MAX2837_FT_24M },
	{ 28000000, MAX2837_FT_28M },
	{        0, 0 },
};
//clang-format on

static uint32_t max2837_set_lpf_bandwidth(trait_max283x_t* const trait, const max283x_mode_t mode, const uint32_t bandwidth_hz)
{
	(void) mode;

	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	const max2837_ft_t* p = max2837_ft;
	while (p->bandwidth_hz != 0) {
		if (p->bandwidth_hz >= bandwidth_hz) {
			break;
		}
		p++;
	}

	if (p->bandwidth_hz != 0) {
		set_MAX2837_FT(self, p->ft);
		max2837_regs_commit(trait);
	}

	return p->bandwidth_hz;
}

static bool max2837_set_lna_gain(trait_max283x_t* const trait, const uint32_t gain_db)
{
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	uint16_t val;
	switch (gain_db) {
		case 40:
			val = MAX2837_LNAgain_MAX;
			break;
		case 32:
			val = MAX2837_LNAgain_M8;
			break;
		case 24:
			val = MAX2837_LNAgain_M16;
			break;
		case 16:
			val = MAX2837_LNAgain_M24;
			break;
		case 8:
			val = MAX2837_LNAgain_M32;
			break;
		case 0:
			val = MAX2837_LNAgain_M40;
			break;
		default:
			return false;
	}
	set_MAX2837_LNAgain(self, val);
	max2837_reg_commit(self, 1);
	return true;
}

static bool max2837_set_vga_gain(trait_max283x_t* const trait, const uint32_t gain_db) {
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	if ((gain_db & 0x1) || gain_db > 62) { /* 0b11111*2 */
		return false;
	}

	set_MAX2837_VGA(self, 31-(gain_db >> 1) );
	max2837_reg_commit(self, 5);
	return true;
}

static bool max2837_set_txvga_gain(trait_max283x_t* const trait, const uint32_t gain_db) {
	max2837_driver_t* self = container_of(trait, max2837_driver_t, trait);

	uint16_t val = 0;
	if (gain_db < 16) {
		val = 31 - gain_db;
		val |= (1 << 5); // bit6: 16db
	} else {
		val = 31 - (gain_db - 16);
	}

	set_MAX2837_TXVGA_GAIN(self, val);
	max2837_reg_commit(self, 29);
	return true;
}

static const vtable_max283x_t vtable_max2837 = {
	.setup = max2837_setup,
	.num_regs = max2837_num_regs,
	.data_regs_max_value = max2837_data_regs_max_value,
	.reg_read = max2837_reg_read,
	.reg_write = max2837_reg_write,
	.regs_commit = max2837_regs_commit,
	.mode = max2837_mode,
	.set_mode = max2837_set_mode,
	.start = max2837_start,
	.stop = max2837_stop,
	.set_frequency = max2837_set_frequency,
	.set_lpf_bandwidth = max2837_set_lpf_bandwidth,
	.set_lna_gain = max2837_set_lna_gain,
	.set_vga_gain = max2837_set_vga_gain,
	.set_txvga_gain = max2837_set_txvga_gain,
	.tx = max2837_tx,
	.rx = max2837_rx,
	.set_rx_hpf_frequency = NULL,
	.tx_calibration = NULL,
	.rx_calibration = NULL,
};

trait_max283x_t* max2837_driver_new(max2837_driver_t* const self)
{
	*self = (max2837_driver_t) {
		.trait = { .vtable = &vtable_max2837 },
	};
	return &self->trait;
}
