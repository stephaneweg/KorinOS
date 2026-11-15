---
name: korin-kit-gpiokit
description: GpioKit, le connecteur à 40 broches du Raspberry Pi sous Korin OS — modes, niveaux, tirages, fronts horodatés, PWM et servos, I2C et SPI par le service gpio, avec un simulateur en mémoire (fonctions gp_*, pas gk_ qui est GpuKit). À charger pour tout programme qui pilote des broches GPIO, un capteur I2C ou un périphérique SPI.
---

# GpioKit : le connecteur à 40 broches

Chaque appel est une requête au service `gpio`, qui tient le matériel et sait à qui appartient chaque broche. Le kit
(`/lib/gpiokit.so`, en assembleur) sert GPIO Lab et les instructions GPIO du BASIC. Il a un **simulateur** :
carte en mémoire propre au programme, entrées posées à la main, PWM, I2C avec un écran SSD1306 (`0x3C`) et un
capteur BME280 (`0x76`), SPI rebouclé. Pris d'office sous QEMU ou sans service `gpio` ; `gp_sim (1)` le choisit sur le Pi.

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide de l'API (exemples, pièges) | `/apps/help.app/api/gpiokit_fr.md` | `src/apps/help/data/api/gpiokit_fr.md` |
| Table du kit | — | `src/kits/gpiokit/gpiokit.abi`, constantes `gpiokit.inc` |
| En-tête C | `/devtools/gcc/include/kits/gpiokit_abi.h` | généré depuis le `.abi` |
| Contexte (BASIC, GPIO Lab, état des essais) | — | `docs/BASIC.md`, `docs/KITS.md` |

Usages réels : `src/apps/gpiolab/main.cpp` (modes, PWM, fronts, I2C, `gp_release`), `src/apps/basic/main.cpp`.

## Ouvrir le kit

C / C++ :

```c
#include <gpiokit_abi.h>
if (!gpiokit_kit_open ()) return 1;     /* /lib/gpiokit.so absent */
long what = gp_available ();            /* 1 le connecteur, 2 le simulateur, 0 rien */
```

`pibuild` lie déjà `libgpiokit.a` ; à la main : `gcc -I/devtools/gcc/include/kits main.c -lgpiokit`.

Assembleur : `include '../../kits/gpiokit/gpiokit.inc'`, `kit_use '../../kits/gpiokit/gpiokit.abi'` dans le
segment de code, `bl kits_open` au démarrage, `kit_data` dans les données.

Les noms des fonctions sont les mêmes dans tous les langages ; les skills `korin-lang-*` expliquent le mécanisme.

## L'essentiel

Arguments et résultats `long`. Erreurs `GP_E*` négatives : `GP_EPERM` -1 (broche du système ou d'un autre),
`GP_EIO` -5 (personne sur le bus), `GP_EBUSY` -16, `GP_ENODEV` -19, `GP_EINVAL` -22. Les textes rendus sont au
kit : `(const char *) gp_error (r)`. **Les broches sont des numéros de GPIO (BCM, 0 à 27)**, pas des places.

Connecteur :
- `gp_available()`, `gp_sim(1|0)` → ce que rendra `gp_available`
- `gp_header_gpio(place 1..40)` → GPIO (-1 : alimentation/masse) ; `gp_header_pin(gpio)` ; `gp_header_label(place)`
- `gp_gpio_function(gpio)` (« SDA1 »…), `gp_mode_name(mode)`
- `gp_info(buf, places)` → `GP_PINS` (28) entrées de `GPI_SIZE` (40) o : `GPI_PIN`, `GPI_MODE`, `GPI_LEVEL`,
  `GPI_FLAGS` (`GP_F_RESERVED`, `GP_F_PWM`, `GP_F_EDGES`), `GPI_OWNER` (pid), `GPI_FREQ`, `GPI_DUTY`
- `gp_release()` → rend tout ce que le programme tient

Entrées et sorties :
- `gp_mode(gpio, mode)` : `GP_M_FREE`, `GP_M_IN`, `GP_M_PULLUP`, `GP_M_PULLDOWN`, `GP_M_OUT` (commence basse),
  `GP_M_ALT0` + n ; (`GP_M_PWM`, `GP_M_I2C`, `GP_M_SPI` sont posés par leurs fonctions)
- `gp_write(gpio, 0|1)`, `gp_read(gpio)` → 0/1, `gp_toggle(gpio)` → nouveau niveau, `gp_read_all()` (bit n = GPIO n)

Fronts :
- `gp_edges(gpio, GP_RISING | GP_FALLING)` (`GP_BOTH` ; 0 : arrêt)
- `gp_events(buf, places, attente ms 0..1000)` → fronts de `GPE_SIZE` (16) o : `GPE_PIN`, `GPE_EDGE`,
  `GPE_LEVEL`, `GPE_LOST`, `GPE_US` (64 bits à +8) ; `gp_now_us()` (même horloge)

PWM : `gp_pwm(gpio 12|13|18|19, Hz, rapport /10000)` ; `gp_servo(gpio, impulsion 500..2500 µs)` (50 Hz).

I2C (bus 1, GPIO 2 SDA / 3 SCL, en logiciel) : `gp_i2c_open(Hz, 0 : 100 kHz)`, `gp_i2c_scan(buf16)` (bit a :
l'adresse a répond), `gp_i2c_guess(adr)`, `gp_i2c_reg_read(adr, reg)`, `gp_i2c_reg_write(adr, reg, v)`,
`gp_i2c_write(adr, buf, n)`, `gp_i2c_read(adr, buf, n)`, `gp_i2c_write_read(adr, out, n, in, m)`, `gp_i2c_close()`.

SPI 0 (MOSI 10, MISO 9, SCLK 11, CE0 8, CE1 7) : `gp_spi_open(Hz 0 : 1 MHz, mode 0..3)`,
`gp_spi_transfer(ce 0|1, envoi ou 0, réception ou 0, octets)`, `gp_spi_close()`.

Simulateur : `gp_sim_input(gpio, 0|1|-1)` (bouton ; -1 : suit son tirage), `gp_sim_display(buf1024)` → 1 :
l'image du SSD1306 simulé (128 × 64, 8 lignes de 128 o, bit 0 en haut).

## Exemple

Une LED et un bouton suivi par ses fronts (adapté de « Exemple : une LED et un bouton » du guide) :

```c
#include <stdio.h>
#include <gpiokit_abi.h>

#define LED	17		/* GPIO 17 = broche 11 du connecteur */
#define BUTTON	27

int main (void)
{
	if (!gpiokit_kit_open ()) return 1;
	long r;
	if ((r = gp_mode (LED, GP_M_OUT)) < 0 || (r = gp_mode (BUTTON, GP_M_PULLUP)) < 0
		|| (r = gp_edges (BUTTON, GP_BOTH)) < 0)
	{
		printf ("%s\n", (const char *) gp_error (r));
		return 1;
	}
	int sim = gp_available () == 2;
	for (int i = 0; i < 10; i++)
	{
		printf ("LED %ld\n", gp_toggle (LED));
		if (sim && i == 3) gp_sim_input (BUTTON, 0);	/* appuyé */
		if (sim && i == 6) gp_sim_input (BUTTON, -1);	/* relâché */
		unsigned char ev[8][GPE_SIZE];
		long n = gp_events ((long) ev, 8, 250);		/* 250 ms au plus */
		for (long k = 0; k < n; k++)
			printf ("  GPIO %d %s\n", ev[k][GPE_PIN], ev[k][GPE_EDGE] == GP_FALLING ? "descend" : "monte");
	}
	gp_write (LED, 0);
	gp_release ();
	return 0;
}
```

## Pièges

- **GPIO, pas place** : `gp_mode (11, …)` touche le GPIO 11 (broche 23). Passer par `gp_header_gpio` si l'on pense
  en places du connecteur.
- **Propriété** : une broche est au premier programme qui s'en sert, jusqu'à `GP_M_FREE`, `gp_release ()` ou sa fin
  (elle redevient entrée sans tirage). Un programme qui dure rend ce qu'il tient, sinon les autres ont `GP_EBUSY`.
- **Broches du système**, jamais données (`GP_EPERM`) : 0 et 1 (EEPROM des HAT), 14 et 15 (console série), et 2,
  3, 4 tant que le tactile du PocketTerm tourne : sur le PocketTerm, `gp_i2c_open` échoue.
- **Deux canaux PWM** : GPIO 12 et 18 partagent le premier, 13 et 19 le second. Rapport en 1/10 000 (2500 = 25 %).
- **Fronts** relevés au plus toutes les millisecondes : une impulsion plus brève peut passer inaperçue ;
  `GPE_LOST` signale les fronts perdus, pas ceux jamais vus. `gp_events` attend : dans une app graphique, un thread
  ou une attente 0.
- **Sous QEMU**, `gp_sim (0)` n'apporte rien (ni PWM, ni I2C, ni SPI émulés). `docs/BASIC.md` note le service comme
  jamais essayé sur le vrai Pi (PWM, I2C, SPI réels) : en cas de doute, vérifier d'abord sous le simulateur.
- **3,3 V** : un montage en 5 V détruit les broches du Pi.
- **Préfixe** : `gp_` est GpioKit ; `gk_` est GpuKit.
