/* GpioKit : le connecteur à 40 broches du Pi (docs/KITS.md, docs/BASIC.md ;
 * d'après le GPIOKit d'Onyx, préfixe gp_ : gk_ est à GpuKit), en C commun au
 * Pi et au PC ; remplace gpiokit.asm, même table (gpiokit.abi), mêmes résultats.
 *
 * Deux côtés, comme dans Onyx :
 * - le connecteur : chaque appel est une requête au service « gpio » ;
 * - le simulateur : une carte en mémoire, à ce process : les entrées mises à
 *   la main (gp_sim_input), les ondes du PWM, un bus I2C avec un écran SSD1306
 *   à 0x3C et un capteur BME280 à 0x76 (l'étalonnage de sa fiche technique),
 *   SPI rebouclée. Pris d'office sans service ou sous QEMU ; gp_sim le choisit.
 * Variables par process (données du kit). Aucun pointeur dans les données
 * initialisées : les textes sont des tableaux de taille fixe. */
#include "../common/korin_kit.h"

/* gpiokit.inc */
#define GP_PINS 28
#define GP_M_FREE 0
#define GP_M_IN 1
#define GP_M_PULLUP 2
#define GP_M_PULLDOWN 3
#define GP_M_OUT 4
#define GP_M_PWM 5
#define GP_M_I2C 6
#define GP_M_SPI 7
#define GP_M_ALT0 8
#define GP_RISING 1
#define GP_FALLING 2
#define GP_BOTH 3
#define GP_EPERM -1
#define GP_EIO -5
#define GP_ENOMEM -12
#define GP_EFAULT -14
#define GP_EBUSY -16
#define GP_ENODEV -19
#define GP_EINVAL -22
#define GP_F_RESERVED 1
#define GP_F_PWM 2
#define GP_F_EDGES 4
#define GPI_PIN 0
#define GPI_MODE 1
#define GPI_LEVEL 2
#define GPI_FLAGS 3
#define GPI_OWNER 4
#define GPI_FREQ 8
#define GPI_DUTY 12
#define GPI_REASON 16
#define GPI_SIZE 40
#define GPE_PIN 0
#define GPE_EDGE 1
#define GPE_LEVEL 2
#define GPE_LOST 3
#define GPE_US 8
#define GPE_SIZE 16

/* src/inc/gpio.inc */
#define GPIO_OP_INFO 1
#define GPIO_OP_MODE 2
#define GPIO_OP_WRITE 3
#define GPIO_OP_READ 4
#define GPIO_OP_READALL 5
#define GPIO_OP_PWM 6
#define GPIO_OP_EDGES 7
#define GPIO_OP_EVENTS 8
#define GPIO_OP_RELEASE 9
#define GPIO_OP_I2C_OPEN 10
#define GPIO_OP_I2C_CLOSE 11
#define GPIO_OP_I2C_XFER 12
#define GPIO_OP_SPI_OPEN 13
#define GPIO_OP_SPI_CLOSE 14
#define GPIO_OP_SPI_XFER 15
#define GPIO_MSG_DATA 48
#define GPIO_MSG_BYTES 460
#define PIW_MODE 0
#define PIW_FLAGS 1
#define PIW_REASON 2
#define PIW_LEVEL 3
#define PIW_OWNER 4
#define PIW_FREQ 8
#define PIW_DUTY 12
#define PIW_SIZE 16

/* gpiokit.asm : une broche simulée (SP_*, 16 octets) */
struct sim_pin {
	uint8_t mode;   /* SP_MODE */
	uint8_t out;    /* SP_OUT */
	uint8_t edges;  /* SP_EDGES */
	uint8_t last;   /* SP_LAST */
	int8_t ext;     /* SP_EXT : -1 rien ne la tient */
	uint8_t pad[3];
	uint32_t freq;  /* SP_FREQ */
	uint32_t duty;  /* SP_DUTY */
};
#define SIMQ 256 /* fronts gardés */

/* ---- variables par process (mêmes valeurs de départ que l'asm) */
static long g_sim = -1;              /* -1 pas encore décidé, 0 le connecteur, 1 le simulateur */
static long g_chan;                  /* canal du service (0 : pas ouvert) */
static uint64_t g_qhead, g_qtail, g_qlost; /* tête, queue, fronts perdus */
static uint64_t g_i2c, g_spi;        /* bus ouverts (simulateur) */
static uint64_t g_col, g_page;       /* colonne, page du SSD1306 */
static uint64_t g_col_hi = 127, g_page_hi = 7, g_col_lo, g_page_lo;
static uint64_t g_ocmd, g_oargs, g_oarg1; /* sa commande, arguments attendus, premier argument */
static uint64_t g_bme_cur;
static char g_label[16];
static struct sim_pin g_pins[GP_PINS];
static uint8_t g_q[SIMQ * GPE_SIZE] __attribute__((aligned(8)));
static uint8_t g_oled_mem[1024];
static uint8_t g_bme_reg[256];
static uint8_t g_msg[SVC_MSG] __attribute__((aligned(8)));

/* ---- textes (gpiokit.asm, fin du segment de code) */

/* s_r1, s_r2, s_r3, s_gnd, s_3v3, s_5v à la suite, comme dans l'asm : gp_info
 * copie toujours 23 octets depuis le début du texte */
static const char s_pwr[] = "HAT ID EEPROM\0" "serial console\0" "touch screen\0" "GND\0" "3V3\0" "5V";
#define S_R1 0
#define S_R2 14
#define S_R3 29
#define S_GND 42
#define S_3V3 46
#define S_5V 50

static const char oled_one[] = {0x20, (char)0x81, (char)0x8D, (char)0xA8, (char)0xD3,
				(char)0xD5, (char)0xD9, (char)0xDA, (char)0xDB, 0};

/* broche du connecteur 1..40 -> GPIO, -1 / -2 : alimentation, masse */
static const int8_t header[41] = {
	0,
	-1, -1,  2, -1,  3, -2,  4, 14, -2, 15,
	17, 18, 27, -2, 22, 23, -1, 24, 10, -2,
	 9, 25, 11,  8, -2,  7,  0,  1,  5, -2,
	 6, 12, 13, -2, 19, 16, 26, 20, -2, 21,
};

/* T1..3, P1..9 en 16 bits, poids faible d'abord (fiche BST-BME280-DS002), puis H2..H6 */
#define LE16(v) (uint8_t)(uint16_t)(v), (uint8_t)((uint16_t)(v) >> 8)
static const uint8_t bme_cal[31] = {
	LE16(27504), LE16(26435), LE16(-1000),
	LE16(36477), LE16(-10685), LE16(3024), LE16(2855), LE16(140), LE16(-7),
	LE16(15500), LE16(-14600), LE16(6000),
	362 & 0xFF, 362 >> 8, 0,
	313 >> 4, (313 & 15) | ((50 & 15) << 4), 50 >> 4,
	30,
};

static const char mode_names[15][10] = {
	"free", "input", "pull-up", "pull-down", "output", "PWM", "I2C", "SPI",
	"ALT0", "ALT1", "ALT2", "ALT3", "ALT4", "ALT5", "?",
};

static const char func_names[20][10] = {
	"ID_SD", "ID_SC", "SDA1", "SCL1", "GPCLK0", "", "", "SPI0 CE1", "SPI0 CE0", "SPI0 MISO",
	"SPI0 MOSI", "SPI0 SCLK", "PWM0", "PWM1", "TXD0", "RXD0", "", "", "PWM0", "PWM1",
};

static const char s_none[] = "";

static void sim_reset(void);

/* ---- le service */

/* -> 1 canal ouvert, 0 pas de service */
static long svc_chan(void)
{
	if (g_chan)
		return 1;
	long c = korin_sys2(SYS_SVC_OPEN, "gpio", 0);
	if (c < 0)
		return 0;
	g_chan = c; /* (comme l'asm : un canal 0 serait rouvert à chaque appel) */
	return 1;
}

/* op, a, b, c -> résultat (+0 du message), GP_ENODEV sans service ou si l'appel échoue */
static long hw(long op, long a, long b, long c)
{
	if (!svc_chan())
		return GP_ENODEV;
	uint64_t *m = (uint64_t *)g_msg;
	m[0] = (uint64_t)op;
	m[1] = (uint64_t)a;
	m[2] = (uint64_t)b;
	m[3] = (uint64_t)c;
	if (korin_sys2(SYS_SVC_CALL, g_chan, g_msg) < 0)
		return GP_ENODEV;
	return (long)m[0];
}

static long info_call(void)
{
	return hw(GPIO_OP_INFO, 0, 0, 0);
}

/* -> 1 si le simulateur sert ; premier appel : le service dit s'il y a un connecteur */
static long sim_on(void)
{
	if (g_sim >= 0)
		return g_sim == 1;
	if (svc_chan() && info_call() == 0 && ((uint64_t *)g_msg)[1] == 0) { /* +8 : QEMU */
		g_sim = 0;
		return 0;
	}
	sim_reset();
	g_sim = 1;
	return 1;
}

/* ---- l'horloge */

unsigned long gp_now_us(void)
{
#if defined(__aarch64__)
	uint64_t cnt, frq;
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(cnt));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	uint64_t s = cnt / frq;
	uint64_t r = cnt - s * frq;
	return s * 1000000 + r * 1000000 / frq;
#else
	/* le PC n'a pas encore d'horloge à la microseconde ici : les millisecondes */
	return (unsigned long)korin_sys0(SYS_TIME_MS) * 1000;
#endif
}

/* ---- ce qui pilote les broches */

long gp_available(void)
{
	return sim_on() ? 2 : 1;
}

long gp_sim(long on)
{
	sim_on();
	if (on) {
		g_sim = 1;
		sim_reset();
		return 2;
	}
	g_sim = 0; /* le connecteur, s'il y a un service */
	if (svc_chan())
		return 1;
	g_sim = 1;
	return 2;
}

const char *gp_error(long code)
{
	switch (code) {
	case 0: return "done";
	case GP_EPERM: return "the system uses this pin, or it is not this program's";
	case GP_EIO: return "no answer on the bus";
	case GP_ENOMEM: return "out of memory";
	case GP_EFAULT: return "a bad buffer";
	case GP_EBUSY: return "another program has it";
	case GP_ENODEV: return "no GPIO on this system (use the simulator)";
	case GP_EINVAL: return "not possible on this pin, or a value out of range";
	}
	return code < 0 ? "failed" : "done";
}

static void sim_free_all(void);

long gp_release(void)
{
	if (!sim_on())
		return hw(GPIO_OP_RELEASE, 0, 0, 0);
	sim_free_all();
	return 0;
}

/* ---- les broches */

static struct sim_pin *sim_row(unsigned long pin)
{
	return &g_pins[pin];
}

/* -> 1 réservée (EEPROM, console) */
static long sim_reserved(unsigned long pin)
{
	return pin <= 1 || pin == 14 || pin == 15;
}

/* -> niveau */
static long sim_level(unsigned long pin)
{
	struct sim_pin *p = sim_row(pin);
	unsigned mode = p->mode;
	int32_t ext = p->ext;
	if (mode == GP_M_OUT)
		return p->out;
	if (mode == GP_M_PWM) {
		uint32_t duty = p->duty;
		if (!duty)
			return 0;
		if (duty >= 10000)
			return 1;
		uint32_t f = p->freq ? p->freq : 1;
		uint64_t period = 1000000 / (uint64_t)f; /* la période, en µs */
		if (!period)
			period = 1;
		uint64_t now = gp_now_us();
		uint64_t t = now % period; /* temps dans la période */
		return t < (uint64_t)p->duty * period / 10000;
	}
	if (mode == GP_M_I2C || mode == GP_M_SPI)
		return 1;
	if (mode == GP_M_PULLUP)
		return ext >= 0 ? ext : 1;
	if (mode == GP_M_PULLDOWN)
		return ext >= 0 ? ext : 0;
	if (sim_reserved(pin)) /* (la ligne au repos de l'UART, de l'EEPROM) */
		return 1;
	ext = p->ext;
	return ext >= 0 ? ext : 0;
}

/* un front si son niveau a changé */
static void sim_edge_check(unsigned long pin)
{
	struct sim_pin *p = sim_row(pin);
	long level = sim_level(pin);
	if (level == (long)p->last)
		return;
	p->last = (uint8_t)level;
	unsigned edge = level ? GP_RISING : GP_FALLING;
	if (!(p->edges & edge))
		return;
	uint64_t next = (g_qhead + 1) & (SIMQ - 1);
	if (next == g_qtail) {
		g_qlost = 1;
		return;
	}
	uint8_t *e = g_q + g_qhead * GPE_SIZE;
	e[GPE_PIN] = (uint8_t)pin;
	e[GPE_EDGE] = (uint8_t)edge;
	e[GPE_LEVEL] = (uint8_t)level;
	e[GPE_LOST] = (uint8_t)g_qlost;
	*(uint32_t *)(e + 4) = 0;
	g_qlost = 0;
	g_qhead = next;
	*(uint64_t *)(e + GPE_US) = gp_now_us();
}

/* la broche libre */
static void sim_free(unsigned long pin)
{
	struct sim_pin *p = sim_row(pin);
	p->mode = 0;
	p->out = 0;
	p->edges = 0;
	p->freq = 0;
	p->duty = 0;
	p->last = (uint8_t)sim_level(pin);
}

static void sim_free_all(void)
{
	for (unsigned long i = 0; i < GP_PINS; i++)
		sim_free(i);
	g_i2c = 0;
	g_spi = 0;
}

/* la carte à l'allumage : tout libre, rien ne tient les entrées */
static void sim_reset(void)
{
	for (unsigned long i = 0; i < GP_PINS; i++)
		sim_row(i)->ext = -1;
	sim_free_all();
	g_qhead = 0;
	g_qtail = 0;
	g_qlost = 0;
	g_col_hi = 127;
	g_page_hi = 7;
}

long gp_mode(unsigned long pin, unsigned long mode)
{
	if (!sim_on())
		return hw(GPIO_OP_MODE, (long)pin, (long)mode, 0);
	if (pin >= GP_PINS || mode > GP_M_ALT0 + 5)
		return GP_EINVAL;
	if (sim_reserved(pin))
		return GP_EPERM;
	if (mode == GP_M_PWM || mode == GP_M_I2C || mode == GP_M_SPI)
		return GP_EINVAL;
	sim_free(pin);
	sim_row(pin)->mode = (uint8_t)mode;
	sim_edge_check(pin);
	return 0;
}

long gp_write(unsigned long pin, long level)
{
	if (!sim_on())
		return hw(GPIO_OP_WRITE, (long)pin, level, 0);
	if (pin >= GP_PINS)
		return GP_EINVAL;
	struct sim_pin *p = sim_row(pin);
	if (p->mode != GP_M_OUT)
		return GP_EINVAL;
	p->out = level != 0;
	sim_edge_check(pin);
	return 0;
}

long gp_read(unsigned long pin)
{
	if (!sim_on())
		return hw(GPIO_OP_READ, (long)pin, 0, 0);
	if (pin >= GP_PINS)
		return GP_EINVAL;
	return sim_level(pin);
}

long gp_toggle(unsigned long pin)
{
	long r = gp_read(pin);
	if (r < 0)
		return r;
	long v = r ^ 1;
	r = gp_write(pin, v);
	if (r < 0)
		return r;
	return v;
}

long gp_read_all(void)
{
	if (!sim_on())
		return hw(GPIO_OP_READALL, 0, 0, 0);
	unsigned long all = 0;
	for (unsigned long i = 0; i < GP_PINS; i++)
		all |= (unsigned long)sim_level(i) << i;
	return (long)all;
}

/* -> 1 si elle peut faire du PWM */
static long gp_is_pwm(unsigned long pin)
{
	return pin == 12 || pin == 13 || pin == 18 || pin == 19;
}

long gp_info(uint8_t *out, unsigned long places)
{
	unsigned long n = places < GP_PINS ? places : GP_PINS;
	long sim = sim_on();
	if (!sim) {
		long r = info_call();
		if (r < 0)
			return r;
	}
	for (unsigned long i = 0; i < n; i++, out += GPI_SIZE) {
		memset(out, 0, GPI_SIZE);
		out[GPI_PIN] = (uint8_t)i;
		unsigned reason;
		if (!sim) {
			const uint8_t *w = g_msg + GPIO_MSG_DATA + i * PIW_SIZE;
			out[GPI_MODE] = w[PIW_MODE];
			out[GPI_LEVEL] = w[PIW_LEVEL];
			out[GPI_FLAGS] = w[PIW_FLAGS];
			*(uint32_t *)(out + GPI_OWNER) = *(const uint32_t *)(w + PIW_OWNER);
			*(uint32_t *)(out + GPI_FREQ) = *(const uint32_t *)(w + PIW_FREQ);
			*(uint32_t *)(out + GPI_DUTY) = *(const uint32_t *)(w + PIW_DUTY);
			reason = w[PIW_REASON];
		} else {
			struct sim_pin *p = sim_row(i);
			out[GPI_MODE] = p->mode;
			*(uint32_t *)(out + GPI_FREQ) = p->freq;
			*(uint32_t *)(out + GPI_DUTY) = p->duty;
			unsigned flags = p->edges ? GP_F_EDGES : 0;
			if (gp_is_pwm(i))
				flags |= GP_F_PWM;
			out[GPI_FLAGS] = (uint8_t)flags;
			out[GPI_LEVEL] = (uint8_t)sim_level(i);
			reason = 0;
			if (sim_reserved(i)) {
				out[GPI_FLAGS] |= GP_F_RESERVED;
				reason = i <= 1 ? 1 : 2;
			}
		}
		if (reason) {
			unsigned off = reason == 1 ? S_R1 : reason == 2 ? S_R2 : S_R3;
			memcpy(out + GPI_REASON, s_pwr + off, 23);
		}
	}
	return (long)n;
}

const char *gp_mode_name(unsigned long mode)
{
	if (mode > GP_M_ALT0 + 5)
		mode = GP_M_ALT0 + 6;
	return mode_names[mode];
}

/* ---- PWM */

long gp_pwm(unsigned long pin, unsigned long hz, unsigned long duty)
{
	if (!sim_on())
		return hw(GPIO_OP_PWM, (long)pin, (long)hz, (long)duty);
	if (!gp_is_pwm(pin) || !hz || hz > 1000000 || duty > 10000)
		return GP_EINVAL;
	unsigned long other = pin == 12 ? 18 : pin == 18 ? 12 : pin == 13 ? 19 : 13; /* l'autre broche du canal */
	if (sim_row(other)->mode == GP_M_PWM)
		return GP_EBUSY;
	if (sim_row(pin)->mode != GP_M_PWM)
		sim_free(pin);
	struct sim_pin *p = sim_row(pin);
	p->mode = GP_M_PWM;
	p->freq = (uint32_t)hz;
	p->duty = (uint32_t)duty;
	return 0;
}

long gp_servo(unsigned long pin, unsigned long us)
{
	if (us < 500) /* 50 Hz : 20 000 µs */
		us = 500;
	if (us > 2500)
		us = 2500;
	return gp_pwm(pin, 50, us >> 1); /* (µs / 20000 en 1/10000) */
}

/* ---- les fronts */

long gp_edges(unsigned long pin, unsigned long edges)
{
	if (!sim_on())
		return hw(GPIO_OP_EDGES, (long)pin, (long)edges, 0);
	if (pin >= GP_PINS || edges > GP_BOTH)
		return GP_EINVAL;
	sim_row(pin)->edges = (uint8_t)edges;
	return 0;
}

long gp_events(uint8_t *out, unsigned long places, unsigned long ms)
{
	if (ms > 1000)
		ms = 1000;
	unsigned long end = ms + (unsigned long)korin_sys0(SYS_TIME_MS); /* la fin de l'attente */
	for (;;) {
		if (!sim_on()) {
			long r = hw(GPIO_OP_EVENTS, (long)places, 0, 0);
			if (r < 0)
				return r;
			if (r) {
				memcpy(out, g_msg + GPIO_MSG_DATA, (size_t)r << 4);
				return r;
			}
		} else {
			uint64_t tail = g_qtail;
			unsigned long n = 0;
			while (tail != g_qhead && n < places) {
				memcpy(out, g_q + tail * GPE_SIZE, GPE_SIZE);
				out += GPE_SIZE;
				tail = (tail + 1) & (SIMQ - 1);
				n++;
			}
			g_qtail = tail;
			if (n)
				return (long)n;
		}
		if ((unsigned long)korin_sys0(SYS_TIME_MS) >= end)
			return 0;
		korin_sys1(SYS_SLEEP, 2);
	}
}

/* ---- I2C */

long gp_i2c_open(long hz)
{
	if (!sim_on())
		return hw(GPIO_OP_I2C_OPEN, hz, 0, 0);
	g_i2c = 1;
	sim_row(2)->mode = GP_M_I2C;
	sim_row(3)->mode = GP_M_I2C;
	return 0;
}

long gp_i2c_close(void)
{
	if (!sim_on())
		return hw(GPIO_OP_I2C_CLOSE, 0, 0, 0);
	g_i2c = 0;
	sim_free(2);
	sim_free(3);
	return 0;
}

static long sim_i2c(unsigned long addr, const uint8_t *w, unsigned long wn, uint8_t *r, unsigned long rn);

/* -> octets lus (rien à lire : écrits) / GP_E* */
long gp_i2c_write_read(long addr, const uint8_t *w, unsigned long wn, uint8_t *r, unsigned long rn)
{
	if (sim_on())
		return sim_i2c((unsigned long)addr, w, wn, r, rn);
	if (wn > GPIO_MSG_BYTES || rn > GPIO_MSG_BYTES)
		return GP_EINVAL;
	memcpy(g_msg + GPIO_MSG_DATA, w, wn);
	long res = hw(GPIO_OP_I2C_XFER, addr, (long)wn, (long)rn);
	if (res < 0 || !rn)
		return res;
	memcpy(r, g_msg + GPIO_MSG_DATA, rn);
	return res;
}

long gp_i2c_write(long addr, const uint8_t *buf, unsigned long n)
{
	return gp_i2c_write_read(addr, buf, n, 0, 0);
}

long gp_i2c_read(long addr, uint8_t *buf, unsigned long n)
{
	return gp_i2c_write_read(addr, 0, 0, buf, n);
}

long gp_i2c_reg_read(long addr, long reg)
{
	uint8_t b[2];
	b[0] = (uint8_t)reg;
	long r = gp_i2c_write_read(addr, b, 1, b + 1, 1);
	if (r < 0)
		return r;
	return b[1];
}

long gp_i2c_reg_write(long addr, long reg, long val)
{
	uint8_t b[2];
	b[0] = (uint8_t)reg;
	b[1] = (uint8_t)val;
	long r = gp_i2c_write(addr, b, 2);
	if (r < 0)
		return r;
	return 0;
}

/* 16 o : chaque adresse 0x08..0x77 essayée par une lecture d'un octet */
long gp_i2c_scan(uint8_t *out)
{
	uint8_t tmp[16];
	memset(out, 0, 16);
	long count = 0;
	for (unsigned long a = 0x08; a < 0x78; a++) {
		long r = gp_i2c_read((long)a, tmp, 1);
		if (r == GP_EPERM || r == GP_ENODEV)
			return r;
		if (r < 0)
			continue;
		out[a >> 3] |= (uint8_t)(1u << (a & 7));
		count++;
	}
	return count;
}

const char *gp_i2c_guess(unsigned long a)
{
	if (a < 0x20)
		return s_none;
	if (a <= 0x27)
		return "MCP23017 / PCF8574 port expander";
	if (a == 0x3C || a == 0x3D)
		return "SSD1306 / SH1106 OLED display";
	if (a == 0x40)
		return "INA219 / HTU21D / PCA9685";
	if (a < 0x48)
		return s_none;
	if (a <= 0x4B)
		return "ADS1115 ADC / TMP102";
	if (a < 0x50)
		return s_none;
	if (a <= 0x57)
		return "AT24C EEPROM";
	if (a == 0x5C)
		return "AM2320 / BH1750";
	if (a == 0x68)
		return "DS3231 / DS1307 clock, MPU-6050";
	if (a == 0x69)
		return "MPU-6050 (AD0 high)";
	if (a == 0x76 || a == 0x77)
		return "BME280 / BMP280 sensor";
	return s_none;
}

/* ---- l'écran et le capteur simulés */

/* octet de données */
static void oled_data(uint8_t b)
{
	uint64_t col = g_col, page = g_page;
	if (page < 8 && col < 128)
		g_oled_mem[(page << 7) + col] = b;
	col++;
	if (col > g_col_hi) {
		col = g_col_lo;
		page++;
		if (page > g_page_hi)
			page = g_page_lo;
	}
	g_col = col;
	g_page = page;
}

/* octet de commande */
static void oled_cmd(uint64_t b)
{
	if (g_oargs) {
		if (g_ocmd == 0x21) {
			if (g_oargs == 2) {
				g_oarg1 = b;
			} else {
				uint64_t lo = g_oarg1 & 127;
				g_col_lo = lo;
				g_col_hi = b & 127;
				g_col = lo;
			}
		} else if (g_ocmd == 0x22) {
			if (g_oargs == 2) {
				g_oarg1 = b;
			} else {
				uint64_t lo = g_oarg1 & 7;
				g_page_lo = lo;
				g_page_hi = b & 7;
				g_page = lo;
			}
		}
		g_oargs--;
		return;
	}
	g_ocmd = b;
	if (b == 0x21 || b == 0x22) {
		g_oargs = 2;
		return;
	}
	for (const char *o = oled_one; *o; o++) { /* les commandes à un argument */
		if ((uint8_t)*o == b) {
			g_oargs = 1;
			return;
		}
	}
	if (b - 0xB0 <= 7) {
		g_page = b - 0xB0;
		return;
	}
	if (b <= 0x0F) {
		g_col = (g_col & 0xF0) | b;
		return;
	}
	if (b - 0x10 <= 15)
		g_col = (g_col & 0x0F) | ((b - 0x10) << 4);
}

/* les registres du capteur (une fois) */
static void bme_init(void)
{
	if (g_bme_reg[0xD0] == 0x60)
		return;
	g_bme_reg[0xD0] = 0x60;
	memcpy(g_bme_reg + 0x88, bme_cal, 24); /* T1..3, P1..9 */
	g_bme_reg[0xA1] = 75;                  /* H1 */
	memcpy(g_bme_reg + 0xE1, bme_cal + 24, 7);
}

/* environ 25 degrés, 1006,5 hPa, 45 % : la température dérive un peu avec le temps */
static void bme_sample(void)
{
	uint64_t s = gp_now_us() / 1000000;
	uint64_t m = s % 40;
	uint64_t t = (m < 20 ? m : 40 - m) * 80 + 519888;
	uint64_t p = 415148;
	g_bme_reg[0xF7] = (uint8_t)(p >> 12);
	g_bme_reg[0xF8] = (uint8_t)(p >> 4);
	g_bme_reg[0xF9] = (uint8_t)((p & 15) << 4);
	g_bme_reg[0xFA] = (uint8_t)(t >> 12);
	g_bme_reg[0xFB] = (uint8_t)(t >> 4);
	g_bme_reg[0xFC] = (uint8_t)((t & 15) << 4);
	g_bme_reg[0xFD] = 27500 >> 8; /* h */
	g_bme_reg[0xFE] = 27500 & 0xFF;
}

/* -> octets / GP_EIO */
static long sim_i2c(unsigned long addr, const uint8_t *w, unsigned long wn, uint8_t *r, unsigned long rn)
{
	if (addr == 0x3C) {
		if (rn) { /* (son état : 0) */
			memset(r, 0, rn);
			return (long)rn;
		}
		if (!wn)
			return 0;
		uint8_t ctrl = w[0]; /* l'octet de contrôle : données ou commandes */
		for (unsigned long i = 1; i < wn; i++) {
			if (ctrl & 0x40)
				oled_data(w[i]);
			else
				oled_cmd(w[i]);
		}
		return (long)wn;
	}
	if (addr == 0x76) {
		bme_init();
		if (wn) {
			uint64_t cur = w[0];
			g_bme_cur = cur;
			for (unsigned long i = 1; i < wn; i++)
				g_bme_reg[(cur + i - 1) & 255] = w[i];
		}
		if (!rn)
			return (long)wn;
		bme_sample();
		uint64_t cur = g_bme_cur;
		for (unsigned long i = 0; i < rn; i++)
			r[i] = g_bme_reg[(cur + i) & 255];
		return (long)rn;
	}
	return GP_EIO;
}

/* ---- SPI */

long gp_spi_open(long hz, unsigned long mode)
{
	if (!sim_on())
		return hw(GPIO_OP_SPI_OPEN, hz, (long)mode, 0);
	if (mode > 3)
		return GP_EINVAL;
	g_spi = 1;
	for (unsigned long i = 7; i < 12; i++)
		sim_row(i)->mode = GP_M_SPI;
	return 0;
}

long gp_spi_close(void)
{
	if (!sim_on())
		return hw(GPIO_OP_SPI_CLOSE, 0, 0, 0);
	g_spi = 0;
	for (unsigned long i = 7; i < 12; i++)
		sim_free(i);
	return 0;
}

/* par morceaux au service ; simulé : rebouclée */
long gp_spi_transfer(unsigned long ce, const uint8_t *tx, uint8_t *rx, unsigned long n)
{
	if (ce > 1)
		return GP_EINVAL;
	if (sim_on()) {
		if (!g_spi)
			return GP_EPERM;
		for (unsigned long i = 0; i < n; i++) {
			uint8_t b = tx ? tx[i] : 0;
			if (rx)
				rx[i] = b;
		}
		return (long)n;
	}
	for (unsigned long left = n; left;) {
		unsigned long part = left < GPIO_MSG_BYTES ? left : GPIO_MSG_BYTES;
		if (!tx) {
			memset(g_msg + GPIO_MSG_DATA, 0, part);
		} else {
			memcpy(g_msg + GPIO_MSG_DATA, tx, part);
			tx += part;
		}
		long r = hw(GPIO_OP_SPI_XFER, (long)ce, (long)part, 0);
		if (r < 0)
			return r;
		if (rx) {
			memcpy(rx, g_msg + GPIO_MSG_DATA, part);
			rx += part;
		}
		left -= part;
	}
	return (long)n;
}

/* ---- le connecteur tel qu'il est imprimé sur la carte */

long gp_header_gpio(unsigned long pin)
{
	if (pin - 1 > 39)
		return -1;
	long g = header[pin];
	return g >= 0 ? g : -1;
}

long gp_header_pin(long gpio)
{
	for (long i = 1; i <= 40; i++)
		if (header[i] == gpio)
			return i;
	return -1;
}

/* le texte « GPIOn » est dans un tampon par process */
const char *gp_header_label(unsigned long pin)
{
	if (pin - 1 > 39)
		return s_none;
	long g = header[pin];
	if (g < 0) {
		if (g == -2)
			return s_pwr + S_GND;
		if (pin == 1 || pin == 17)
			return s_pwr + S_3V3;
		return s_pwr + S_5V;
	}
	char *p = g_label;
	*p++ = 'G';
	*p++ = 'P';
	*p++ = 'I';
	*p++ = 'O';
	if (g >= 10) {
		*p++ = (char)('0' + g / 10);
		g %= 10;
	}
	*p++ = (char)('0' + g);
	*p = 0;
	return g_label;
}

const char *gp_gpio_function(unsigned long gpio)
{
	if (gpio > 19)
		return s_none;
	return func_names[gpio];
}

/* ---- le simulateur vu du programme */

long gp_sim_input(unsigned long pin, long level)
{
	if (pin >= GP_PINS)
		return GP_EINVAL;
	sim_on(); /* (à son premier appel : la carte remise à zéro) */
	sim_row(pin)->ext = level < 0 ? -1 : level != 0;
	sim_edge_check(pin);
	return 0;
}

long gp_sim_display(uint8_t *out)
{
	if (!sim_on())
		return 0;
	memcpy(out, g_oled_mem, 1024);
	return 1;
}
