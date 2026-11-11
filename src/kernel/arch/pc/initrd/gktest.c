/* gktest : GpioKit écrit en C (src/kits/gpiokit/gpiokit.c) sur le noyau
 * x86-64 : sans service gpio, le kit prend son simulateur (broches, I2C avec
 * un BME280 à 0x76 et un SSD1306 à 0x3C). « run gktest ». */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c)
{
	i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
	return ret;
}
#define S1(n, a) sc(n, (u64)(a), 0, 0)
#define S2(n, a, b) sc(n, (u64)(a), (u64)(b), 0)

static void put(const char *s)
{
	u64 n = 0;
	while (s[n])
		n++;
	S2(SYS_PRINT, s, n);
}

static void putn(i64 v)
{
	char b[24];
	int i = 23, neg = v < 0;
	u64 u = neg ? (u64)-v : (u64)v;
	b[i] = 0;
	do
		b[--i] = (char)('0' + u % 10);
	while (u /= 10);
	if (neg)
		b[--i] = '-';
	put(&b[i]);
}

static int fails, passes;

static void check(const char *what, int ok, i64 got)
{
	if (ok) {
		passes++;
		return;
	}
	fails++;
	put("gktest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

static int eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

typedef i64 (*fn)(u64, u64, u64, u64, u64);

enum { AVAILABLE, EDGES, ERROR, EVENTS, GPIO_FUNCTION, HEADER_GPIO, HEADER_LABEL, HEADER_PIN, I2C_CLOSE,
       I2C_GUESS, I2C_OPEN, I2C_READ, I2C_REG_READ, I2C_REG_WRITE, I2C_SCAN, I2C_WRITE, I2C_WRITE_READ, INFO,
       MODE, MODE_NAME, NOW_US, PWM, READ, READ_ALL, RELEASE, SERVO, SIM, SIM_DISPLAY, SIM_INPUT, SPI_CLOSE,
       SPI_OPEN, SPI_TRANSFER, TOGGLE, WRITE };

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("gktest : GpioKit en C sur x86-64 (simulateur)\n");
	i64 t = S2(SYS_LIB_OPEN, "gpiokit", 34);
	check("lib_open gpiokit (34 fonctions)", t >= 0x5000000000LL, t);
	const u32 *tab = (const u32 *)t;
#define F(i) ((fn)(t + tab[2 + (i)]))
	check("sans service : le simulateur", F(AVAILABLE)(0, 0, 0, 0, 0) == 2, 0);
	check("connecteur : broche 11 = GPIO 17", F(HEADER_GPIO)(11, 0, 0, 0, 0) == 17, 0);
	check("GPIO 17 = broche 11", F(HEADER_PIN)(17, 0, 0, 0, 0) == 11, 0);
	check("étiquette de la broche 1", eq((const char *)F(HEADER_LABEL)(1, 0, 0, 0, 0), "3V3"), 0);
	check("nom du mode OUT", F(MODE_NAME)(4, 0, 0, 0, 0) != 0, 0);
	check("texte d'une erreur", ((const char *)F(ERROR)((u64)-22, 0, 0, 0, 0))[0] != 0, 0);

	check("i2c_open", F(I2C_OPEN)(0, 0, 0, 0, 0) == 0, 0);
	uint8_t bits[16] = {0};
	i64 n = F(I2C_SCAN)((u64)bits, 0, 0, 0, 0);
	check("i2c_scan : 0x3C et 0x76", n == 2 && (bits[0x3C / 8] & (1 << (0x3C % 8))) && (bits[0x76 / 8] & (1 << (0x76 % 8))), n);
	check("BME280 : identifiant 0x60", F(I2C_REG_READ)(0x76, 0xD0, 0, 0, 0) == 0x60, 0);
	check("adresse vide : GP_EIO", F(I2C_REG_READ)(0x50, 0, 0, 0, 0) == -5, 0);

	check("mode OUT", F(MODE)(17, 4, 0, 0, 0) == 0, 0);
	F(WRITE)(17, 1, 0, 0, 0);
	i64 a = F(READ)(17, 0, 0, 0, 0);
	F(WRITE)(17, 0, 0, 0, 0);
	check("sortie 1 puis 0", a == 1 && F(READ)(17, 0, 0, 0, 0) == 0, a);
	check("toggle", F(TOGGLE)(17, 0, 0, 0, 0) == 1, 0);
	check("pull-up : 1", F(MODE)(27, 2, 0, 0, 0) == 0 && F(READ)(27, 0, 0, 0, 0) == 1, 0);
	check("pull-down : 0", F(MODE)(22, 3, 0, 0, 0) == 0 && F(READ)(22, 0, 0, 0, 0) == 0, 0);
	F(SIM_INPUT)(22, 1, 0, 0, 0);
	check("entrée tenue à 1 (sim_input)", F(READ)(22, 0, 0, 0, 0) == 1, 0);
	check("broche réservée : GP_EPERM", F(MODE)(14, 4, 0, 0, 0) == -1, 0);
	check("PWM sur 18", F(PWM)(18, 1000, 5000, 0, 0) == 0, 0);
	check("PWM sur 17 refusé", F(PWM)(17, 1000, 5000, 0, 0) == -22, 0);
	check("release", F(RELEASE)(0, 0, 0, 0, 0) == 0, 0);

	put("gktest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
