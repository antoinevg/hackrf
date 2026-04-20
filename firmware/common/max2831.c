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

// TODO below no longer seems to work since some time now ...

/*
 * 'gcc -DTEST -DDEBUG -O2 -o test max2831.c' prints out what test
 * program would do if it had a real spi library
 *
 * 'gcc -DTEST -DBUS_PIRATE -O2 -o test max2831.c' prints out bus
 * pirate commands to do the same thing.
 */

#include "max2831.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "adc.h"
#include "fixed_point.h"
#include "max2831_regs.def" // private register def macros
#include "platform_gpio.h"
#include "selftest.h"
#include "trait_max283x.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Default register values. */
static const uint16_t max2831_regs_default[MAX2831_NUM_REGS] = {
	0x1740, /* 0: enable fractional mode (Table 16 recommends 0x0740, clearing unknown bit) */
	0x119a, /* 1 */
	0x1003, /* 2 */
	0x0079, /* 3: PLL divider settings for 2437 MHz */
	0x3666, /* 4: PLL divider settings for 2437 MHz */
	0x00a4, /* 5: divide reference frequency by 2 */
	0x0060, /* 6: enable TX power detector */
	0x1022, /* 7: 110% TX LPF bandwidth */
	0x2021, /* 8: pin control of RX gain, 11 MHz LPF bandwidth */
	0x03b5, /* 9: pin control of TX gain */
	0x1d80, /* 10: 3.5 us PA enable delay, zero PA bias */
	0x0074, /* 11: LNA high gain, RX VGA moderate gain (Table 27 recommends 0x007f, maximum gain) */
	0x0140, /* 12: TX VGA minimum */
	0x0e92, /* 13 */
	0x0100, /* 14: reference clock output disabled */
	0x0145, /* 15: RX IQ common mode 1.1 V */
};

/* Forward declarations. */
static void max2831_reg_write(trait_max283x_t* const trait, uint8_t r, uint16_t v);
static void max2831_regs_commit(trait_max283x_t* const trait);
static uint32_t max2831_set_lpf_bandwidth(
	trait_max283x_t* const trait,
	const max283x_mode_t mode,
	const uint32_t bandwidth_hz);
static void max2831_set_mode(trait_max283x_t* const trait, const max283x_mode_t new_mode);

/* impl max2831_driver_t { ... */

static inline void max2831_reg_commit(max2831_driver_t* const self, uint8_t r)
{
	max2831_reg_write(&self->trait, r, self->regs[r]);
}

/* Set up all registers according to defaults specified in docs. */
static void max2831_init(max2831_driver_t* const self)
{
	self->target_init(self);
	max2831_set_mode(&self->trait, (max283x_mode_t) MAX2831_MODE_SHUTDOWN);

	memcpy(self->regs, max2831_regs_default, sizeof(self->regs));
	self->regs_dirty = 0xffff;

	/* Write default register values to chip. */
	max2831_regs_commit(&self->trait);
}

/* impl trait_max283x_t for max2831_driver_t { ... */

/*
 * Set up pins for GPIO and SPI control, configure SSP peripheral for SPI, and
 * set our own default register configuration.
 */
static void max2831_setup(trait_max283x_t* const trait)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	max2831_init(self);

	/* Use SPI control instead of B1-B7 pins for gain settings. */
	set_MAX2831_RXVGA_GAIN_SPI_EN(self, 1);
	set_MAX2831_TXVGA_GAIN_SPI_EN(self, 1);

	//set_MAX2831_TXVGA_GAIN(0x3f); /* maximum gain */
	set_MAX2831_TXVGA_GAIN(self, 0x00); /* minimum gain */
	set_MAX2831_RX_HPF_SEL(self, MAX2831_RX_HPF_30_KHZ);
	set_MAX2831_LNA_GAIN(self, MAX2831_LNA_GAIN_MAX); /* maximum gain */
	set_MAX2831_RXVGA_GAIN(self, 0x18);

	/* maximum rx output common-mode voltage */
	//set_MAX2831_RXIQ_VCM(self, MAX2831_RXIQ_VCM_1_2);

	/* configure baseband filter for 8 MHz TX */
	set_MAX2831_LPF_COARSE(self, MAX2831_RX_LPF_7_5M);
	set_MAX2831_RX_LPF_FINE_ADJ(self, MAX2831_RX_LPF_FINE_100);
	set_MAX2831_TX_LPF_FINE_ADJ(self, MAX2831_TX_LPF_FINE_100);

	/* clock output disable */
	set_MAX2831_CLKOUT_PIN_EN(self, 0);

	max2831_regs_commit(trait);
}

static uint16_t max2831_num_regs(trait_max283x_t* const trait)
{
	(void) trait;
	return MAX2831_NUM_REGS;
}

static uint16_t max2831_data_regs_max_value(trait_max283x_t* const trait)
{
	(void) trait;
	return MAX2831_DATA_REGS_MAX_VALUE;
}

static void max2831_write(trait_max283x_t* const trait, uint8_t r, uint16_t v)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	uint32_t word = (((uint32_t) v & 0x3fff) << 4) | (r & 0xf);
	uint16_t values[2] = {word >> 9, word & 0x1ff};
	spi_bus_transfer(self->bus, values, 2);
}

static uint16_t max2831_reg_read(trait_max283x_t* const trait, uint8_t r)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	return self->regs[r];
}

static void max2831_reg_write(trait_max283x_t* const trait, uint8_t r, uint16_t v)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	self->regs[r] = v;
	max2831_write(trait, r, v);
	MAX2831_REG_SET_CLEAN(self, r);
}

static void max2831_regs_commit(trait_max283x_t* const trait)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	int r;
	for (r = 0; r < MAX2831_NUM_REGS; r++) {
		if ((self->regs_dirty >> r) & 0x1) {
			max2831_reg_commit(self, r);
		}
	}
}

static void max2831_set_mode(
	trait_max283x_t* const trait,
	const max283x_mode_t new_max283x_mode)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);
	const max2831_mode_t new_mode = (max2831_mode_t) new_max283x_mode;

	// Only change calibration bits if necessary to reduce SPI activity.
	bool tx_cal = (new_mode == MAX2831_MODE_TX_CALIBRATION);
	bool rx_cal = (new_mode == MAX2831_MODE_RX_CALIBRATION);
	if (get_MAX2831_TX_CAL_MODE_EN(self) != tx_cal) {
		set_MAX2831_TX_CAL_MODE_EN(self, tx_cal);
		max2831_regs_commit(trait);
	}
	if (get_MAX2831_RX_CAL_MODE_EN(self) != rx_cal) {
		set_MAX2831_RX_CAL_MODE_EN(self, rx_cal);
		max2831_regs_commit(trait);
	}

	self->set_mode(self, new_mode);
	max2831_set_lpf_bandwidth(trait, (max283x_mode_t) new_mode, self->desired_lpf_bw);
}

static max283x_mode_t max2831_mode(trait_max283x_t* const trait)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	return (max283x_mode_t) self->mode;
}

static void max2831_start(trait_max283x_t* const trait)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	max2831_regs_commit(trait);
	max2831_set_mode(trait, (max283x_mode_t) MAX2831_MODE_STANDBY);

	/* Read RSSI with ADC. */
	uint16_t rssi_1 = selftest.max2831_mux_rssi_1 = adc_read(1);

	/* Switch to temperature sensor. */
	set_MAX2831_RSSI_MUX(self, MAX2831_RSSI_MUX_TEMP);
	max2831_regs_commit(trait);

	/* Read temperature. */
	uint16_t temp = selftest.max2831_mux_temp = adc_read(1);

	/* Switch back to RSSI. */
	set_MAX2831_RSSI_MUX(self, MAX2831_RSSI_MUX_RSSI);
	max2831_regs_commit(trait);

	/* Read RSSI again. */
	uint16_t rssi_2 = selftest.max2831_mux_rssi_2 = adc_read(1);

	/* If the ADC results are as expected, we know our writes are working. */
	bool rssi_1_good = (rssi_1 < 10);
	bool rssi_2_good = (rssi_2 < 10);
	bool temp_good = (temp > 100) && (temp < 500); // -40 to +85C

	selftest.max2831_mux_test_ok = rssi_1_good & rssi_2_good & temp_good;

	if (!selftest.max2831_mux_test_ok) {
		selftest.report.pass = false;
	}
}

static void max2831_tx(trait_max283x_t* const trait)
{
	max2831_regs_commit(trait);
	max2831_set_mode(trait, (max283x_mode_t) MAX2831_MODE_TX);
}

static void max2831_rx(trait_max283x_t* const trait)
{
	max2831_regs_commit(trait);
	max2831_set_mode(trait, (max283x_mode_t) MAX2831_MODE_RX);
}

static void max2831_tx_calibration(trait_max283x_t* const trait)
{
	max2831_regs_commit(trait);
	max2831_set_mode(trait, (max283x_mode_t) MAX2831_MODE_TX_CALIBRATION);
}

static void max2831_rx_calibration(trait_max283x_t* const trait)
{
	max2831_regs_commit(trait);
	max2831_set_mode(trait, (max283x_mode_t) MAX2831_MODE_RX_CALIBRATION);
}

static void max2831_stop(trait_max283x_t* const trait)
{
	max2831_regs_commit(trait);
	max2831_set_mode(trait, (max283x_mode_t) MAX2831_MODE_SHUTDOWN);
}

/* Assume 40 MHz reference clock with R divider of 2. */
#define PFD_FREQ_HZ (20ULL * (1000ULL * 1000ULL))

#define MIN_FREQ FP_MHZ(2000)
#define MAX_FREQ FP_MHZ(3000)

static fp_40_24_t max2831_set_frequency(
	trait_max283x_t* const trait,
	fp_40_24_t freq,
	bool program)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	uint64_t div;

	freq = MIN(freq, MAX_FREQ);
	freq = MAX(freq, MIN_FREQ);

	freq += ((PFD_FREQ_HZ * FP_ONE_HZ) >> 21); /* round to nearest frequency */
	div = freq / PFD_FREQ_HZ;

	/*
	 * Shift from 40.24 fixed-point to 44.20 to match 20-bit fractional
	 * divider.
	 */
	div = div >> 4;

	if (program) {
		//set_MAX2831_SYN_REF_DIV(self, MAX2831_SYN_REF_DIV_2);
		set_MAX2831_SYN_INT(self, (div >> 20) & 0xff);
		set_MAX2831_SYN_FRAC_HI(self, (div >> 6) & 0x3fff);
		set_MAX2831_SYN_FRAC_LO(self, div & 0x3f);
		max2831_regs_commit(trait);
	}

	return PFD_FREQ_HZ * (div << 4);
}

typedef struct {
	uint32_t bandwidth_hz;
	uint8_t ft;
} max2831_ft_t;

typedef struct {
	uint8_t percent;
	uint8_t ft_fine;
} max2831_ft_fine_t;

// clang-format off
/* measured -0.5 dB complex baseband bandwidth for each register setting */
static const max2831_ft_t max2831_rx_ft[] = {
	{ 11600000, MAX2831_RX_LPF_7_5M },
	{ 15100000, MAX2831_RX_LPF_8_5M },
	{ 22600000, MAX2831_RX_LPF_15M },
	{ 28300000, MAX2831_RX_LPF_18M },
	{        0, 0 },
};

static const max2831_ft_fine_t max2831_rx_ft_fine[] = {
	{  90, MAX2831_RX_LPF_FINE_90 },
	{  95, MAX2831_RX_LPF_FINE_95 },
	{ 100, MAX2831_RX_LPF_FINE_100 },
	{ 105, MAX2831_RX_LPF_FINE_105 },
	{ 110, MAX2831_RX_LPF_FINE_110 },
	{   0, 0 },
};

/* measured -0.5 dB complex baseband bandwidth for each register setting */
static const max2831_ft_t max2831_tx_ft[] = {
	{ 11900000, MAX2831_TX_LPF_8M },
	{ 15800000, MAX2831_TX_LPF_11M },
	{ 23600000, MAX2831_TX_LPF_16_5M },
	{ 31300000, MAX2831_TX_LPF_22_5M },
	{        0, 0 },
};

static const max2831_ft_fine_t max2831_tx_ft_fine[] = {
	{  90, MAX2831_TX_LPF_FINE_90 },
	{  95, MAX2831_TX_LPF_FINE_95 },
	{ 100, MAX2831_TX_LPF_FINE_100 },
	{ 105, MAX2831_TX_LPF_FINE_105 },
	{ 110, MAX2831_TX_LPF_FINE_110 },
	{ 115, MAX2831_TX_LPF_FINE_115 },
	{   0, 0 },
};
//clang-format on

static uint32_t max2831_set_lpf_bandwidth(trait_max283x_t* const trait, const max283x_mode_t max283x_mode, const uint32_t bandwidth_hz)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);
	const max2831_mode_t mode = (max2831_mode_t)max283x_mode;

	const max2831_ft_t* coarse;
	const max2831_ft_fine_t* fine;

	self->desired_lpf_bw = bandwidth_hz;

	if (mode == MAX2831_MODE_RX) {
		coarse = max2831_rx_ft;
		fine = max2831_rx_ft_fine;
	} else {
		coarse = max2831_tx_ft;
		fine = max2831_tx_ft_fine;
	}

	/* Find coarse and fine settings for LPF. */
	bool found = false;
	const max2831_ft_fine_t* f = fine;
	for (; coarse->bandwidth_hz != 0; coarse++) {
		uint32_t coarse_aux = coarse->bandwidth_hz / 100;
		for (f = fine; f->percent != 0; f++) {
			if ((coarse_aux * f->percent) >= self->desired_lpf_bw) {
				found = true;
				break;
			}
		}
		if (found) break;
	}

	/*
	 * Use the widest setting if a wider bandwidth than our maximum is
	 * requested.
	 */
	if (!found) {
		coarse--;
		f--;
	}

	/* Program found settings. */
	set_MAX2831_LPF_COARSE(self, coarse->ft);
	if (mode == MAX2831_MODE_RX) {
		set_MAX2831_RX_LPF_FINE_ADJ(self, f->ft_fine);
	} else {
		set_MAX2831_TX_LPF_FINE_ADJ(self, f->ft_fine);
	}
	max2831_regs_commit(trait);

	return coarse->bandwidth_hz * f->percent / 100;
}

static bool max2831_set_lna_gain(trait_max283x_t* const trait, const uint32_t gain_db) {
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	uint16_t val;
	switch(gain_db){
		case 40:  // MAX2837 compatibility
		case 33:
		case 32:  // MAX2837 compatibility
			val = MAX2831_LNA_GAIN_MAX;
			break;
		case 24:  // MAX2837 compatibility
		case 16:
			val = MAX2831_LNA_GAIN_M16;
			break;
		case 8:	  // MAX2837 compatibility
		case 0:
			val = MAX2831_LNA_GAIN_M33;
			break;
		default:
			return false;
	}
	set_MAX2831_LNA_GAIN(self, val);
	max2831_reg_commit(self, 11);
	return true;
}

static bool max2831_set_vga_gain(trait_max283x_t* const trait, const uint32_t gain_db) {
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	if( (gain_db & 0x1) || gain_db > 62) {/* 0b11111*2 */
		return false;
	}

	set_MAX2831_RXVGA_GAIN(self, (gain_db >> 1) );
	max2831_reg_commit(self, 11);
	return true;
}

static bool max2831_set_txvga_gain(trait_max283x_t* const trait, const uint32_t gain_db) {
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	uint16_t value = MIN((gain_db << 1) | 1, 0x3f);
	set_MAX2831_TXVGA_GAIN(self, value);
	max2831_reg_commit(self, 12);
	return true;
}

static void max2831_set_rx_hpf_frequency(trait_max283x_t* const trait, const max283x_rx_hpf_freq_t max283x_freq)
{
	max2831_driver_t* self = container_of(trait, max2831_driver_t, trait);

	const max2831_rx_hpf_freq_t freq = (max2831_rx_hpf_freq_t)max283x_freq;

	/**
	 * Frequency     RXHP     RX_HPF_SEL (D13:D12)
	 *
	 *   100  Hz     low      00
	 *     4 kHz     low      x1
	 *    30 kHz     low      10
	 *   600 kHz     high     xx
	 */
	switch (freq) {
	case MAX2831_RX_HPF_100_HZ:
	case MAX2831_RX_HPF_4_KHZ:
	case MAX2831_RX_HPF_30_KHZ:
		set_MAX2831_RX_HPF_SEL(self, freq);
		max2831_reg_commit(self, 7);
		gpio_clear(self->gpio_rxhp);
		break;
	case MAX2831_RX_HPF_600_KHZ:
		gpio_set(self->gpio_rxhp);
		break;
	}
}

static const vtable_max283x_t vtable_max2831 = {
	.setup = max2831_setup,
	.num_regs = max2831_num_regs,
	.data_regs_max_value = max2831_data_regs_max_value,
	.reg_read = max2831_reg_read,
	.reg_write = max2831_reg_write,
	.regs_commit = max2831_regs_commit,
	.mode = max2831_mode,
	.set_mode = max2831_set_mode,
	.start = max2831_start,
	.stop = max2831_stop,
	.set_frequency = max2831_set_frequency,
	.set_lpf_bandwidth = max2831_set_lpf_bandwidth,
	.set_lna_gain = max2831_set_lna_gain,
	.set_vga_gain = max2831_set_vga_gain,
	.set_txvga_gain = max2831_set_txvga_gain,
	.tx = max2831_tx,
	.rx = max2831_rx,
	.set_rx_hpf_frequency = max2831_set_rx_hpf_frequency,
	.tx_calibration = max2831_tx_calibration,
	.rx_calibration = max2831_rx_calibration,
};

trait_max283x_t* max2831_driver_new(max2831_driver_t* const self)
{
	// TODO an argument can be made to put the spi bus, callback and gpio assignment in platform_board instead

	*self = (max2831_driver_t) {
		.trait = { .vtable = &vtable_max2831 },
		//.bus = &spi_bus_ssp1,
		//.target_init = max2831_target_init,
		//.set_mode = max2831_target_set_mode,
	};
	/*
	const platform_gpio_t* gpio = platform_gpio();
	self->gpio_enable = gpio->max283x_enable;
	self->gpio_rxtx = gpio->max283x_rx_enable;
	self->gpio_rxhp = gpio->max2831_rxhp;
	self->gpio_ld = gpio->max2831_ld;
*/
	return &self->trait;
}
