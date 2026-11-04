# GpioKit: the 40-pin header

**GpioKit** gives programs the Raspberry Pi's 40-pin header: inputs and outputs, pull resistors, time-stamped edges, PWM and servos, I2C and SPI buses. Each call is a request to the `gpio` service, which holds the hardware and knows who owns each pin. The kit is written in assembly, in `SD0:/lib/gpiokit.so`. It is what **GPIO Lab** and the GPIO statements of BASIC use.

The kit has a **simulator**: a board in memory, private to the program, with inputs set by hand, the PWM waveforms, an I2C bus carrying an **SSD1306** display (address `0x3C`) and a **BME280** sensor (address `0x76`), and a looped-back SPI (what goes out comes back). It is taken automatically under QEMU (which emulates neither PWM, nor I2C, nor SPI) or without a `gpio` service; `gp_sim (1)` also selects it on the Pi. All the examples on this page therefore run under QEMU.

Pins are named by their **GPIO number** (BCM numbering, 0 to 27), not by their place on the header: the LED "on pin 11" is GPIO 17. `gp_header_gpio` and `gp_header_pin` convert between the two.

## Opening the kit

```c
#include <gpiokit_abi.h>

if (!gpiokit_kit_open ())
	return 1;			/* SD0:/lib/gpiokit.so missing */
long what = gp_available ();	/* 1 the header, 2 the simulator, 0 nothing */
```

Nothing to add to the link. Every function takes `long` arguments and returns a `long`; errors are negative `GP_E*` values, which `gp_error` spells out. Returned texts (`gp_error`, `gp_mode_name`, `gp_header_label`...) are pointers to strings of the kit: `(const char *) gp_error (r)`.

| Error | Value | Meaning |
|---|---|---|
| `GP_EPERM` | -1 | system pin, or held by someone other than this program |
| `GP_EIO` | -5 | nobody answers on the bus |
| `GP_ENOMEM` | -12 | out of memory |
| `GP_EFAULT` | -14 | invalid buffer |
| `GP_EBUSY` | -16 | another program holds the pin or the bus |
| `GP_ENODEV` | -19 | no GPIO here: `gp_sim (1)` simulates |
| `GP_EINVAL` | -22 | wrong pin, mode, frequency or duty cycle |

**Ownership**: a pin belongs to the first program that uses it, until `gp_mode (pin, GP_M_FREE)`, `gp_release ()` or the end of the program; it then becomes an input without pull again. Some pins belong to the system and are never given out: 0 and 1 (HAT EEPROM), 14 and 15 (serial console), and 2, 3, 4 while the PocketTerm touch screen driver runs (its controller is on I2C bus 1).

## The header

| Function | Arguments | Result |
|---|---|---|
| `gp_available` | none | 1 the header, 2 the simulator, 0 nothing |
| `gp_sim` | 1 simulator, 0 the header | what `gp_available` would return afterwards |
| `gp_header_gpio` | header pin, 1 to 40 | GPIO number; -1: power or ground |
| `gp_header_pin` | GPIO | header pin; -1: none |
| `gp_header_label` | header pin | "3V3", "5V", "GND", "GPIO17"... |
| `gp_gpio_function` | GPIO | what else it is ("SDA1", "PWM0"...), or "" |
| `gp_info` | array of `GPI_SIZE` (40) bytes per pin, slots | number of pins described (`GP_PINS`, 28) |
| `gp_mode_name` | `GP_M_*` mode | its name |
| `gp_release` | none | 0: everything the program holds is given back |

A `gp_info` entry (`GPI_*` constants): GPIO number (`GPI_PIN`, byte), mode (`GPI_MODE`, byte), level (`GPI_LEVEL`, byte), flags (`GPI_FLAGS`, byte: `GP_F_RESERVED` system pin, `GP_F_PWM` can do PWM, `GP_F_EDGES` its edges are recorded), owner pid (`GPI_OWNER`, 32 bits, 0: nobody), frequency (`GPI_FREQ`) and duty cycle (`GPI_DUTY`, in 1/10,000) in PWM, reason for the reservation (`GPI_REASON`, 24 bytes of text).

## Inputs and outputs

| Function | Arguments | Result |
|---|---|---|
| `gp_mode` | GPIO, mode | 0 / `GP_E*` |
| `gp_write` | GPIO, level 0 or 1 | 0 / `GP_E*` |
| `gp_read` | GPIO | 0, 1, or `GP_EINVAL` |
| `gp_toggle` | output GPIO | its new level |
| `gp_read_all` | none | all levels at once (bit n: GPIO n) |

Modes: `GP_M_FREE` (give the pin back), `GP_M_IN` (floating input), `GP_M_PULLUP`, `GP_M_PULLDOWN` (input pulled up, down), `GP_M_OUT` (output, starts low), `GP_M_PWM`, `GP_M_I2C`, `GP_M_SPI` (set by `gp_pwm`, `gp_i2c_open`, `gp_spi_open`), `GP_M_ALT0` + n (alternate function n, 0 to 5, by hand).

## Edges

An edge (a change from 0 to 1 or from 1 to 0) is recorded with its time, in microseconds, without the program having to poll the pin.

| Function | Arguments | Result |
|---|---|---|
| `gp_edges` | GPIO, `GP_RISING` \| `GP_FALLING` (`GP_BOTH`), 0: none any more | 0 / `GP_E*` |
| `gp_events` | array of `GPE_SIZE` (16) bytes per edge, slots, wait in ms (0 to 1000) | number of edges read |
| `gp_now_us` | none | the edge clock, in microseconds |

An edge: GPIO (`GPE_PIN`, byte), direction (`GPE_EDGE`, `GP_RISING` or `GP_FALLING`), level just after (`GPE_LEVEL`), 1 if edges were lost before this one (`GPE_LOST`), time (`GPE_US`, 64 bits at +8).

## PWM and servos

| Function | Arguments | Result |
|---|---|---|
| `gp_pwm` | GPIO 12, 13, 18 or 19, frequency in Hz, duty in 1/10,000 | 0 / `GP_E*` |
| `gp_servo` | PWM GPIO, pulse in µs (500 to 2500) | 0 / `GP_E*`; 50 Hz |

The Pi has two PWM channels: GPIO 12 and 18 share the first, 13 and 19 the second. A duty of 2,500 is 25%.

## I2C

I2C bus 1 (GPIO 2 = SDA, 3 = SCL), in software, around 100 kHz.

| Function | Arguments | Result |
|---|---|---|
| `gp_i2c_open` | frequency in Hz (0: 100 kHz) | 0 / `GP_E*` (an error on the PocketTerm: the touch screen holds the bus) |
| `gp_i2c_close` | none | 0: the bus is given back |
| `gp_i2c_scan` | 16-byte buffer | number of devices; bit a of the buffer: address a answered |
| `gp_i2c_guess` | address | what usually answers there ("BME280 / BMP280 sensor"...), or "" |
| `gp_i2c_write` | address, data, bytes | bytes written / `GP_EIO` |
| `gp_i2c_read` | address, buffer, bytes | bytes read / `GP_EIO` |
| `gp_i2c_write_read` | address, data to write, bytes, buffer, bytes | bytes read (write then read with no stop in between) |
| `gp_i2c_reg_read` | address, register | value 0 to 255 / `GP_E*` |
| `gp_i2c_reg_write` | address, register, value | 0 / `GP_E*` |

## SPI

SPI 0 (GPIO 10 MOSI, 9 MISO, 11 SCLK; CE0 = GPIO 8, CE1 = GPIO 7).

| Function | Arguments | Result |
|---|---|---|
| `gp_spi_open` | frequency in Hz (0: 1 MHz), mode 0 to 3 | 0 / `GP_E*` |
| `gp_spi_transfer` | CE (0 or 1), data out (0: zeros), data in (0: discarded), bytes | bytes exchanged |
| `gp_spi_close` | none | 0 |

## The simulator

| Function | Arguments | Result |
|---|---|---|
| `gp_sim_input` | GPIO, level 0 or 1 (-1: nothing drives it) | 0; like a button or a wire connected to the pin |
| `gp_sim_display` | 1024-byte buffer | 1: what the simulated SSD1306 shows (128 x 64 dots, 8 rows of 128 bytes, bit 0 at the top); 0: no display |

A simulated input that nothing drives follows its pull: 1 in `GP_M_PULLUP`, 0 otherwise. The simulated BME280 reads about 25 °C, 1006 hPa and 45% humidity, with the calibration of its datasheet: a program written for the real sensor reads it unchanged.

## Example: the header and a PWM

Shows the 40 pins, then puts a 25% PWM on GPIO 18 and reads it back through `gp_info`.

```c
#include <stdio.h>
#include <gpiokit_abi.h>

int main (void)
{
	if (!gpiokit_kit_open ())
		return 1;
	long av = gp_available ();
	printf ("GPIO: %s\n", av == 1 ? "header" : av == 2 ? "simulator" : "none");
	for (int p = 1; p <= 40; p += 2)
	{
		const char *l = (const char *) gp_header_label (p), *r = (const char *) gp_header_label (p + 1);
		const char *fl = (const char *) gp_gpio_function (gp_header_gpio (p));
		const char *fr = (const char *) gp_gpio_function (gp_header_gpio (p + 1));
		printf ("%8s %-6s %2d | %-2d %-6s %s\n", gp_header_gpio (p) >= 0 ? fl : "", l, p, p + 1, r,
			gp_header_gpio (p + 1) >= 0 ? fr : "");
	}

	long r = gp_pwm (18, 1000, 2500);		/* 1 kHz, 25% */
	if (r < 0)
	{
		printf ("PWM: %s\n", (const char *) gp_error (r));
		return 1;
	}
	unsigned char info[GP_PINS][GPI_SIZE];
	long n = gp_info ((long) info, GP_PINS);
	for (long i = 0; i < n; i++)
	{
		unsigned char *e = info[i];
		if (e[GPI_MODE] == GP_M_PWM)
			printf ("GPIO %d: %s, %u Hz, %u %%\n", e[GPI_PIN], (const char *) gp_mode_name (e[GPI_MODE]),
				*(unsigned *) (e + GPI_FREQ), *(unsigned *) (e + GPI_DUTY) / 100);
	}
	gp_release ();
	return 0;
}
```

## Example: an LED and a button

An LED on GPIO 17 blinks; a button on GPIO 27 (pulled up, it connects the pin to ground when pressed) is followed through its edges. Under the simulator, the program "presses" it itself with `gp_sim_input`.

```c
#include <stdio.h>
#include <gpiokit_abi.h>

#define LED	17
#define BUTTON	27

int main (void)
{
	if (!gpiokit_kit_open ())
		return 1;
	long r;
	if ((r = gp_mode (LED, GP_M_OUT)) < 0 || (r = gp_mode (BUTTON, GP_M_PULLUP)) < 0
		|| (r = gp_edges (BUTTON, GP_BOTH)) < 0)
	{
		printf ("%s\n", (const char *) gp_error (r));
		return 1;
	}
	int sim = gp_available () == 2;
	printf ("button at rest: %ld\n", gp_read (BUTTON));
	unsigned long t0 = (unsigned long) gp_now_us ();
	for (int i = 0; i < 10; i++)
	{
		printf ("LED %ld\n", gp_toggle (LED));
		if (sim && i == 3)
			gp_sim_input (BUTTON, 0);	/* pressed */
		if (sim && i == 6)
			gp_sim_input (BUTTON, -1);	/* released: the pull-up raises it */
		unsigned char ev[8][GPE_SIZE];
		long n = gp_events ((long) ev, 8, 250);	/* waits 250 ms at most */
		for (long k = 0; k < n; k++)
		{
			unsigned long us = *(unsigned long *) (ev[k] + GPE_US);
			printf ("  GPIO %d %s at %lu ms%s\n", ev[k][GPE_PIN],
				ev[k][GPE_EDGE] == GP_FALLING ? "falls" : "rises", (us - t0) / 1000,
				ev[k][GPE_LOST] ? " (edges lost before)" : "");
		}
	}
	gp_write (LED, 0);
	gp_release ();
	return 0;
}
```

## Example: I2C sensor and SPI

Scans the I2C bus, reads the temperature of a BME280 (integer formula from its datasheet), then exchanges four bytes on SPI (looped back in the simulator: they come back).

```c
#include <stdio.h>
#include <gpiokit_abi.h>

#define BME	0x76

int main (void)
{
	if (!gpiokit_kit_open ())
		return 1;
	long r = gp_i2c_open (0);
	if (r < 0)
	{
		printf ("I2C: %s\n", (const char *) gp_error (r));
		return 1;
	}
	unsigned char map[16];
	printf ("%ld device(s) on the bus\n", gp_i2c_scan ((long) map));
	for (int a = 0; a < 128; a++)
		if (map[a / 8] >> (a % 8) & 1)
			printf ("  0x%02X  %s\n", a, (const char *) gp_i2c_guess (a));

	if (gp_i2c_reg_read (BME, 0xD0) == 0x60)		/* BME280 chip id */
	{
		gp_i2c_reg_write (BME, 0xF4, 0x27);		/* continuous measurement */
		unsigned char reg = 0x88, c[6], d[3];
		gp_i2c_write_read (BME, (long) &reg, 1, (long) c, 6);	/* T1, T2, T3 */
		reg = 0xFA;
		gp_i2c_write_read (BME, (long) &reg, 1, (long) d, 3);	/* raw temperature */
		int t1 = c[0] | c[1] << 8, t2 = (short) (c[2] | c[3] << 8), t3 = (short) (c[4] | c[5] << 8);
		int adc = d[0] << 12 | d[1] << 4 | d[2] >> 4;
		int v1 = (((adc >> 3) - (t1 << 1)) * t2) >> 11;
		int v2 = (((((adc >> 4) - t1) * ((adc >> 4) - t1)) >> 12) * t3) >> 14;
		int t = ((v1 + v2) * 5 + 128) >> 8;			/* hundredths of a degree */
		printf ("BME280: %d.%02d C\n", t / 100, t % 100);
	}
	gp_i2c_close ();

	if (gp_spi_open (0, 0) == 0)
	{
		unsigned char out[4] = { 0xDE, 0xAD, 0xBE, 0xEF }, in[4] = { 0 };
		long n = gp_spi_transfer (0, (long) out, (long) in, 4);
		printf ("SPI: %ld bytes, received %02X %02X %02X %02X\n", n, in[0], in[1], in[2], in[3]);
		gp_spi_close ();
	}
	gp_release ();
	return 0;
}
```

## Pitfalls

- **GPIO versus pin**: `gp_mode (11, ...)` acts on GPIO 11 (header pin 23), not on pin 11. Go through `gp_header_gpio` if you think in places.
- **Give back what you hold**: a pin stays with the program until it ends. A long-running program calls `gp_release ()` (or `gp_mode (g, GP_M_FREE)`) as soon as it no longer needs it, otherwise GPIO Lab and others get `GP_EBUSY`.
- **The PocketTerm**: its touch screen uses I2C bus 1 (GPIO 2, 3, 4); `gp_i2c_open` returns an error there.
- **Under QEMU**, `gp_sim (0)` brings nothing: with no emulated PWM, I2C or SPI, the kit stays on the simulator.
- **Edges** are sampled at most every millisecond: a shorter pulse may go unnoticed (`GPE_LOST` reports lost edges, not edges never seen).
- **Voltage**: the Pi's pins are 3.3 V. A 5 V circuit destroys them.

## See also

- **GPIO Lab**: see and drive the pins with the mouse, with the simulator.
- [libc and POSIX layer](libc_en.md): threads, to read edges without blocking a user interface.
- [UIKit](uikit_en.md): to show the pin state in a window.
