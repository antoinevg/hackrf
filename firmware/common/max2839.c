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
 * 'gcc -DTEST -DDEBUG -O2 -o test max2839.c' prints out what test
 * program would do if it had a real spi library
 *
 * 'gcc -DTEST -DBUS_PIRATE -O2 -o test max2839.c' prints out bus
 * pirate commands to do the same thing.
 */

#include "max2839.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "max2839_regs.def" // private register def macros
#include "selftest.h"
#include "trait_max283x.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

static uint8_t requested_lna_gain = 0;
static uint8_t requested_vga_gain = 0;

/* Default register values. */
static const uint16_t max2839_regs_default[MAX2839_NUM_REGS] = {
	0x000,  /* 0 */
	0x00c,  /* 1:  data sheet says 0x00c but read 0x20c or 0x22c*/
	0x080,  /* 2 */
	0x1b0,  /* 3:  data sheet says 0x1b9 but read 0x1b0 or 0x1b9 */
	0x3e6,  /* 4 */
	0x100,  /* 5 */
	0x000,  /* 6 */
	0x208,  /* 7 */
	0x220,  /* 8:  data sheet says 0x220 but read 0x000 */
	0x018,  /* 9 */
	0x00c,  /* 10 */
	0x004,  /* 11: data sheet says 0x004 but read 0x000 */
	0x24f,  /* 12 */
	0x150,  /* 13 */
	0x3c5,  /* 14 */
	0x201,  /* 15 */
	0x01c,  /* 16 */
	0x155,  /* 17 */
	0x155,  /* 18 */
	0x153,  /* 19 */
	0x249,  /* 20 */
	0x02d,  /* 21: data sheet says 0x02d but read 0x13d */
	0x1a9,  /* 22 */
	0x24f,  /* 23 */
	0x180,  /* 24 */
	0x00a,  /* 25: data sheet says 0x000 but read 0x00a */
	0x3c0,  /* 26 */
	0x200,  /* 27: data sheet says 0x200 but read 0x22a or 0x22f */
	0x0c0,  /* 28 */
	0x03f,  /* 29: data sheet says 0x03f but read 0x07f or 0x17f */
	0x300,  /* 30: data sheet says 0x300 but read 0x398 or 0x31a */
	0x340}; /* 31: data sheet says 0x340 but read 0x359 */

/*
 * All of the discrepancies listed above are in fields that either don't matter
 * or are undocumented except "set to recommended value". We set them to the
 * data sheet defaults even though the inital part we tested started up with
 * different settings.
 */

static const uint8_t max2839_regs_skip_verify[] = {1, 3, 8, 11, 21, 25, 27, 29, 30, 31};

/* Forward declarations. */
static void max2839_reg_write(trait_max283x_t* const trait, uint8_t r, uint16_t v);
static void max2839_regs_commit(trait_max283x_t* const trait);
static void max2839_set_mode(trait_max283x_t* const trait, const max283x_mode_t new_mode);

/* impl max2839_driver_t { ... */

static uint16_t max2839_read(max2839_driver_t* const self, uint8_t r)
{
	uint16_t value = (1 << 15) | (r << 10);
	spi_bus_transfer(self->bus, &value, 1);
	return value & 0x3ff;
}

static inline void max2839_reg_commit(max2839_driver_t* const self, uint8_t r)
{
	max2839_reg_write(&self->trait, r, self->regs[r]);
}

/* Set up all registers according to defaults specified in docs. */
static void max2839_init(max2839_driver_t* const self)
{
	self->target_init(self);
	max2839_set_mode(&self->trait, (max283x_mode_t) MAX2839_MODE_SHUTDOWN);

	memcpy(self->regs, max2839_regs_default, sizeof(self->regs));
	self->regs_dirty = 0xffffffff;

	/* Write default register values to chip. */
	max2839_regs_commit(&self->trait);

	/* Read back registers to verify. */
	selftest.max283x_readback_total_registers = MAX2839_NUM_REGS;
	for (int r = 0; r < MAX2839_NUM_REGS; r++) {
		for (unsigned int i = 0; i < sizeof(max2839_regs_skip_verify); i++) {
			if (max2839_regs_skip_verify[i] == r) {
				goto next;
			}
		}
		uint16_t value = max2839_read(self, r);
		if (value != self->regs[r]) {
			selftest.max283x_readback_bad_value = value;
			selftest.max283x_readback_expected_value = self->regs[r];
			break;
		}
next:
		selftest.max283x_readback_register_count = r + 1;
	}

	if (selftest.max283x_readback_register_count < MAX2839_NUM_REGS) {
		selftest.report.pass = false;
	}
}

/* impl trait_max283x_t for max2839_driver_t { ... */

/*
 * Set up pins for GPIO and SPI control, configure SSP peripheral for SPI, and
 * set our own default register configuration.
 */
static void max2839_setup(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	max2839_init(self);

	/* Use SPI control instead of B0-B7 pins for gain settings. */
	set_MAX2839_LNAgain_SPI(self, 1);
	set_MAX2839_VGAgain_SPI(self, 1);
	set_MAX2839_TX_VGA_Gain_SPI(self, 1);

	/* enable RXINB */
	set_MAX2839_MIMO_SELECT(self, 1);

	/* set gains for unused RXINA path to minimum */
	set_MAX2839_LNA1gain(self, MAX2839_LNA1gain_M32);
	set_MAX2839_Rx1_VGAgain(self, 0x3f);

	/* set maximum RX output common-mode voltage */
	set_MAX2839_RX_VCM(self, MAX2839_RX_VCM_1_35);

	/* set HPF corner frequency to 1 kHz */
	set_MAX2839_HPC_STOP(self, MAX2839_STOP_1K);

	/*
	 * There are two LNA band settings, but we only use one of them.
	 * Switching to the other one doesn't make the overall spectrum any
	 * flatter but adds a surprise step in the middle.
	 */
	set_MAX2839_LNAband(self, MAX2839_LNAband_2_4);

	max2839_regs_commit(trait);
}

static uint16_t max2839_num_regs(trait_max283x_t* const trait)
{
	(void) trait;
	return MAX2839_NUM_REGS;
}

static uint16_t max2839_data_regs_max_value(trait_max283x_t* const trait)
{
	(void) trait;
	return MAX2839_DATA_REGS_MAX_VALUE;
}

static void max2839_write(trait_max283x_t* const trait, uint8_t r, uint16_t v)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	uint16_t value = (r << 10) | (v & 0x3ff);
	spi_bus_transfer(self->bus, &value, 1);
}

static uint16_t max2839_reg_read(trait_max283x_t* const trait, uint8_t r)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	// always read actual value from SPI for now
	//if ((self->regs_dirty >> r) & 0x1) {
	self->regs[r] = max2839_read(self, r);
	//};
	return self->regs[r];
}

static void max2839_reg_write(trait_max283x_t* const trait, uint8_t r, uint16_t v)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	self->regs[r] = v;
	max2839_write(trait, r, v);
	MAX2839_REG_SET_CLEAN(self, r);
}

static void max2839_regs_commit(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	int r;
	for (r = 0; r < MAX2839_NUM_REGS; r++) {
		if ((self->regs_dirty >> r) & 0x1) {
			max2839_reg_commit(self, r);
		}
	}
}

static void max2839_set_mode(trait_max283x_t* const trait, const max283x_mode_t new_mode)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	self->set_mode(self, (max2839_mode_t) new_mode);
}

static max283x_mode_t max2839_mode(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	return (max283x_mode_t) self->mode;
}

static void max2839_start(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	set_MAX2839_chip_enable(self, 1);
	max2839_regs_commit(trait);
	max2839_set_mode(trait, (max283x_mode_t) MAX2839_MODE_STANDBY);
}

static void max2839_tx(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	// FIXME does this do anything without LPFmode_SPI set?
	// do we need it to?
	set_MAX2839_LPFmode(self, MAX2839_LPFmode_TxLPF);
	max2839_regs_commit(trait);
	max2839_set_mode(trait, (max283x_mode_t) MAX2839_MODE_TX);
}

static void max2839_rx(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	set_MAX2839_LPFmode(self, MAX2839_LPFmode_RxLPF);
	max2839_regs_commit(trait);
	max2839_set_mode(trait, (max283x_mode_t) MAX2839_MODE_RX);
}

static void max2839_stop(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	set_MAX2839_chip_enable(self, 0);
	max2839_regs_commit(trait);
	max2839_set_mode(trait, (max283x_mode_t) MAX2839_MODE_SHUTDOWN);
}

/* Assume 40 MHz reference clock with R divider of 1. */
#define PFD_FREQ_HZ (40ULL * (1000ULL * 1000ULL))

#define MIN_FREQ FP_MHZ(2000)
#define MAX_FREQ FP_MHZ(3000)

static fp_40_24_t max2839_set_frequency(
	trait_max283x_t* const trait,
	fp_40_24_t freq,
	bool program)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	uint8_t band;
	uint64_t div;

	freq = MIN(freq, MAX_FREQ);
	freq = MAX(freq, MIN_FREQ);

	/* Select band. Allow tuning outside specified bands. */
	if (freq < FP_MHZ(2400)) {
		band = MAX2839_LOGEN_BSW_2_3;
	} else if (freq < FP_MHZ(2500)) {
		band = MAX2839_LOGEN_BSW_2_4;
	} else if (freq < FP_MHZ(2600)) {
		band = MAX2839_LOGEN_BSW_2_5;
	} else {
		band = MAX2839_LOGEN_BSW_2_6;
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
		set_MAX2839_LOGEN_BSW(self, band);

		/*
		 * Write order matters here, so commit INT and FRAC_HI before
		 * committing FRAC_LO, which is the trigger for VCO auto-select.
		 */
		set_MAX2839_SYN_INT(self, (div >> 20) & 0xff);
		set_MAX2839_SYN_FRAC_HI(self, (div >> 10) & 0x3ff);
		max2839_regs_commit(trait);
		set_MAX2839_SYN_FRAC_LO(self, div & 0x3ff);
		max2839_regs_commit(trait);
	}

	return ((PFD_FREQ_HZ * 3) / 4) * (div << 4);
}

typedef struct {
	uint32_t bandwidth_hz;
	uint32_t ft;
} max2839_ft_t;

// clang-format off
static const max2839_ft_t max2839_ft[] = {
	{  1750000, MAX2839_FT_1_75M },
	{  2500000, MAX2839_FT_2_5M },
	{  3500000, MAX2839_FT_3_5M },
	{  5000000, MAX2839_FT_5M },
	{  5500000, MAX2839_FT_5_5M },
	{  6000000, MAX2839_FT_6M },
	{  7000000, MAX2839_FT_7M },
	{  8000000, MAX2839_FT_8M },
	{  9000000, MAX2839_FT_9M },
	{ 10000000, MAX2839_FT_10M },
	{ 12000000, MAX2839_FT_12M },
	{ 14000000, MAX2839_FT_14M },
	{ 15000000, MAX2839_FT_15M },
	{ 20000000, MAX2839_FT_20M },
	{ 24000000, MAX2839_FT_24M },
	{ 28000000, MAX2839_FT_28M },
	{        0, 0 },
};
//clang-format on

static uint32_t max2839_set_lpf_bandwidth(trait_max283x_t* const trait, max283x_mode_t mode, const uint32_t bandwidth_hz)
{
	(void) mode;

	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	const max2839_ft_t* p = max2839_ft;
	while( p->bandwidth_hz != 0 ) {
		if( p->bandwidth_hz >= bandwidth_hz ) {
			break;
		}
		p++;
	}

	if( p->bandwidth_hz != 0 ) {
		set_MAX2839_FT(self, p->ft);
		max2839_regs_commit(trait);
	}

	return p->bandwidth_hz;
}

static void max2839_configure_rx_gain(trait_max283x_t* const trait)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	/*
	 * restrict requested LNA gain to valid MAX2839 settings:
	 * 0, 8, 16, 24, 32, or 40
	 */
	if (requested_lna_gain > 40) {
		requested_lna_gain = 40;
	}
	requested_lna_gain &= 0x38;

	/*
	 * restrict requested VGA gain to valid MAX2839 settings:
	 * even number, 0 through 62
	 */
	if (requested_vga_gain > 62) {
		requested_vga_gain = 62;
	}
	requested_vga_gain &= 0x3e;

	/*
	 * MAX2839 has lower full-scale RX output voltage than MAX2839, so we
	 * adjust the VGA (baseband) gain to compensate.
	 */
	uint8_t vga_gain = requested_vga_gain + 3;
	uint8_t lna_gain = requested_lna_gain;

	/*
	 * If that adjustment puts VGA gain out of range, use LNA gain to
	 * compensate.  MAX2839 VGA gain can be any number from 0 through 63.
	 */
	if (vga_gain > 63) {
		if (lna_gain <= 32) {
			vga_gain -= 8;
			lna_gain += 8;
		} else {
			vga_gain = 63;
		}
	}

	/*
	 * MAX2839 lacks max-24 dB and max-40 dB LNA gain settings, so we use
	 * VGA gain to compensate.
	 */
	if (lna_gain == 0) {
		lna_gain = 8;
		vga_gain = (vga_gain >= 8) ? vga_gain - 8 : 0;
	}
	if (lna_gain == 16) {
		if (vga_gain > 32) {
			vga_gain -= 8;
			lna_gain += 8;
		} else {
			vga_gain += 8;
			lna_gain -= 8;
		}
	}

	uint16_t val;
	switch (lna_gain) {
	case 40:
		val = MAX2839_LNA2gain_MAX;
		break;
	case 32:
		val = MAX2839_LNA2gain_M8;
		break;
	case 24:
	case 16:
		val = MAX2839_LNA2gain_M16;
		break;
	case 8:
	case 0:
	default:
		val = MAX2839_LNA2gain_M32;
		break;
	}
	set_MAX2839_LNA2gain(self, val);
	set_MAX2839_Rx2_VGAgain(self, (63 - vga_gain));
	max2839_regs_commit(trait);
}

static bool max2839_set_lna_gain(trait_max283x_t* const trait, const uint32_t gain_db)
{
	if ((gain_db & 0x7) || gain_db > 40) {
		return false;
	}
	requested_lna_gain = gain_db;
	max2839_configure_rx_gain(trait);
	return true;
}

static bool max2839_set_vga_gain(trait_max283x_t* const trait, const uint32_t gain_db)
{
	if ((gain_db & 0x1) || gain_db > 62) {
		return false;
	}
	requested_vga_gain = gain_db;
	max2839_configure_rx_gain(trait);
	return true;
}

static bool max2839_set_txvga_gain(trait_max283x_t* const trait, const uint32_t gain_db)
{
	max2839_driver_t* self = container_of(trait, max2839_driver_t, trait);

	uint16_t val = 0;
	val = 47 - gain_db;

	set_MAX2839_TX_VGA_GAIN(self, val);
	max2839_reg_commit(self, 29);
	return true;
}

static const vtable_max283x_t vtable_max2839 = {
	.setup = max2839_setup,
	.num_regs = max2839_num_regs,
	.data_regs_max_value = max2839_data_regs_max_value,
	.reg_read = max2839_reg_read,
	.reg_write = max2839_reg_write,
	.regs_commit = max2839_regs_commit,
	.mode = max2839_mode,
	.set_mode = max2839_set_mode,
	.start = max2839_start,
	.stop = max2839_stop,
	.set_frequency = max2839_set_frequency,
	.set_lpf_bandwidth = max2839_set_lpf_bandwidth,
	.set_lna_gain = max2839_set_lna_gain,
	.set_vga_gain = max2839_set_vga_gain,
	.set_txvga_gain = max2839_set_txvga_gain,
	.tx = max2839_tx,
	.rx = max2839_rx,
	.set_rx_hpf_frequency = NULL,
	.tx_calibration = NULL,
	.rx_calibration = NULL,
};

trait_max283x_t* max2839_driver_new(max2839_driver_t* const self)
{
	*self = (max2839_driver_t) {
		.trait = { .vtable = &vtable_max2839 },
	};
	return &self->trait;
}
