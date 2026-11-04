# GpioKit : le connecteur à 40 broches

**GpioKit** donne aux programmes le connecteur à 40 broches du Raspberry Pi : entrées et sorties, résistances de tirage, fronts horodatés, PWM et servomoteurs, bus I2C et SPI. Chaque appel est une requête au service `gpio`, qui tient le matériel et sait à qui appartient chaque broche. Le kit est en assembleur, dans `SD0:/lib/gpiokit.so`. C'est lui que servent **GPIO Lab** et les instructions GPIO du BASIC.

Le kit a un **simulateur** : une carte en mémoire, propre au programme, avec des entrées qu'on pose à la main, les ondes du PWM, un bus I2C qui porte un écran **SSD1306** (adresse `0x3C`) et un capteur **BME280** (adresse `0x76`), et un SPI rebouclé (ce qui part revient). Il est pris d'office sous QEMU (qui n'émule ni PWM, ni I2C, ni SPI) ou sans service `gpio` ; `gp_sim (1)` le choisit aussi sur le Pi. Tous les exemples de cette page tournent donc sous QEMU.

Les broches se désignent par leur **numéro de GPIO** (numérotation BCM, 0 à 27), pas par leur place sur le connecteur : la LED « sur la broche 11 » est le GPIO 17. `gp_header_gpio` et `gp_header_pin` passent de l'un à l'autre.

## Ouvrir le kit

```c
#include <gpiokit_abi.h>

if (!gpiokit_kit_open ())
	return 1;			/* SD0:/lib/gpiokit.so absent */
long what = gp_available ();	/* 1 le connecteur, 2 le simulateur, 0 rien */
```

Rien à ajouter au lien. Chaque fonction prend des `long` et rend un `long` ; les erreurs sont des `GP_E*` négatifs, que `gp_error` dit en clair. Les textes rendus (`gp_error`, `gp_mode_name`, `gp_header_label`...) sont des pointeurs vers des chaînes du kit : `(const char *) gp_error (r)`.

| Erreur | Valeur | Sens |
|---|---|---|
| `GP_EPERM` | -1 | broche du système, ou tenue par un autre que ce programme |
| `GP_EIO` | -5 | personne ne répond sur le bus |
| `GP_ENOMEM` | -12 | plus de mémoire |
| `GP_EFAULT` | -14 | tampon invalide |
| `GP_EBUSY` | -16 | un autre programme tient la broche ou le bus |
| `GP_ENODEV` | -19 | pas de GPIO ici : `gp_sim (1)` simule |
| `GP_EINVAL` | -22 | broche, mode, fréquence ou rapport faux |

**Propriété** : une broche appartient au programme qui s'en sert le premier, jusqu'à `gp_mode (broche, GP_M_FREE)`, `gp_release ()` ou la fin du programme ; elle redevient alors une entrée sans tirage. Certaines sont au système et ne sont jamais données : 0 et 1 (EEPROM des cartes HAT), 14 et 15 (console série), et 2, 3, 4 tant que le pilote de l'écran tactile du PocketTerm tourne (son contrôleur est sur le bus I2C 1).

## Le connecteur

| Fonction | Arguments | Résultat |
|---|---|---|
| `gp_available` | aucun | 1 le connecteur, 2 le simulateur, 0 rien |
| `gp_sim` | 1 simulateur, 0 le connecteur | ce que rendrait `gp_available` ensuite |
| `gp_header_gpio` | broche du connecteur, 1 à 40 | numéro de GPIO ; -1 : alimentation ou masse |
| `gp_header_pin` | GPIO | broche du connecteur ; -1 : aucune |
| `gp_header_label` | broche du connecteur | « 3V3 », « 5V », « GND », « GPIO17 »... |
| `gp_gpio_function` | GPIO | ce qu'il est aussi (« SDA1 », « PWM0 »...), ou « » |
| `gp_info` | tableau de `GPI_SIZE` (40) octets par broche, places | nombre de broches décrites (`GP_PINS`, 28) |
| `gp_mode_name` | mode `GP_M_*` | son nom |
| `gp_release` | aucun | 0 : tout ce que le programme tient est rendu |

Une entrée de `gp_info` (constantes `GPI_*`) : numéro de GPIO (`GPI_PIN`, octet), mode (`GPI_MODE`, octet), niveau (`GPI_LEVEL`, octet), drapeaux (`GPI_FLAGS`, octet : `GP_F_RESERVED` au système, `GP_F_PWM` peut faire du PWM, `GP_F_EDGES` ses fronts sont relevés), pid du propriétaire (`GPI_OWNER`, 32 bits, 0 : personne), fréquence (`GPI_FREQ`) et rapport (`GPI_DUTY`, en 1/10 000) en PWM, raison de la réservation (`GPI_REASON`, 24 octets de texte).

## Entrées et sorties

| Fonction | Arguments | Résultat |
|---|---|---|
| `gp_mode` | GPIO, mode | 0 / `GP_E*` |
| `gp_write` | GPIO, niveau 0 ou 1 | 0 / `GP_E*` |
| `gp_read` | GPIO | 0, 1, ou `GP_EINVAL` |
| `gp_toggle` | GPIO en sortie | son nouveau niveau |
| `gp_read_all` | aucun | tous les niveaux d'un coup (bit n : GPIO n) |

Les modes : `GP_M_FREE` (rendre la broche), `GP_M_IN` (entrée flottante), `GP_M_PULLUP`, `GP_M_PULLDOWN` (entrée tirée vers le haut, vers le bas), `GP_M_OUT` (sortie, commence basse), `GP_M_PWM`, `GP_M_I2C`, `GP_M_SPI` (posés par `gp_pwm`, `gp_i2c_open`, `gp_spi_open`), `GP_M_ALT0` + n (fonction alternative n, de 0 à 5, à la main).

## Fronts

Un front (passage de 0 à 1 ou de 1 à 0) est relevé avec son heure, en microsecondes, sans que le programme ait à sonder la broche.

| Fonction | Arguments | Résultat |
|---|---|---|
| `gp_edges` | GPIO, `GP_RISING` \| `GP_FALLING` (`GP_BOTH`), 0 : plus rien | 0 / `GP_E*` |
| `gp_events` | tableau de `GPE_SIZE` (16) octets par front, places, attente en ms (0 à 1000) | nombre de fronts lus |
| `gp_now_us` | aucun | l'horloge des fronts, en microsecondes |

Un front : GPIO (`GPE_PIN`, octet), sens (`GPE_EDGE`, `GP_RISING` ou `GP_FALLING`), niveau juste après (`GPE_LEVEL`), 1 si des fronts ont été perdus avant celui-ci (`GPE_LOST`), heure (`GPE_US`, 64 bits à +8).

## PWM et servomoteurs

| Fonction | Arguments | Résultat |
|---|---|---|
| `gp_pwm` | GPIO 12, 13, 18 ou 19, fréquence en Hz, rapport en 1/10 000 | 0 / `GP_E*` |
| `gp_servo` | GPIO PWM, impulsion en µs (500 à 2500) | 0 / `GP_E*` ; 50 Hz |

Le Pi a deux canaux PWM : le GPIO 12 et le 18 partagent le premier, le 13 et le 19 le second. Le rapport 2 500 vaut 25 %.

## I2C

Le bus I2C 1 (GPIO 2 = SDA, 3 = SCL), en logiciel, vers 100 kHz.

| Fonction | Arguments | Résultat |
|---|---|---|
| `gp_i2c_open` | fréquence en Hz (0 : 100 kHz) | 0 / `GP_E*` (une erreur sur le PocketTerm : le tactile tient le bus) |
| `gp_i2c_close` | aucun | 0 : le bus est rendu |
| `gp_i2c_scan` | tampon de 16 octets | nombre d'appareils ; bit a du tampon : l'adresse a répond |
| `gp_i2c_guess` | adresse | ce qui y répond d'habitude (« BME280 / BMP280 sensor »...), ou « » |
| `gp_i2c_write` | adresse, données, octets | octets écrits / `GP_EIO` |
| `gp_i2c_read` | adresse, tampon, octets | octets lus / `GP_EIO` |
| `gp_i2c_write_read` | adresse, à écrire, octets, tampon, octets | octets lus (écriture puis lecture sans arrêt entre les deux) |
| `gp_i2c_reg_read` | adresse, registre | valeur 0 à 255 / `GP_E*` |
| `gp_i2c_reg_write` | adresse, registre, valeur | 0 / `GP_E*` |

## SPI

SPI 0 (GPIO 10 MOSI, 9 MISO, 11 SCLK ; CE0 = GPIO 8, CE1 = GPIO 7).

| Fonction | Arguments | Résultat |
|---|---|---|
| `gp_spi_open` | fréquence en Hz (0 : 1 MHz), mode 0 à 3 | 0 / `GP_E*` |
| `gp_spi_transfer` | CE (0 ou 1), envoi (0 : des zéros), réception (0 : perdue), octets | octets échangés |
| `gp_spi_close` | aucun | 0 |

## Le simulateur

| Fonction | Arguments | Résultat |
|---|---|---|
| `gp_sim_input` | GPIO, niveau 0 ou 1 (-1 : rien ne la tient) | 0 ; comme un bouton ou un fil branché sur la broche |
| `gp_sim_display` | tampon de 1024 octets | 1 : ce que montre le SSD1306 simulé (128 x 64 points, 8 lignes de 128 octets, bit 0 en haut) ; 0 : pas d'écran |

Une entrée simulée sans rien qui la tienne suit son tirage : 1 en `GP_M_PULLUP`, 0 sinon. Le BME280 simulé donne environ 25 °C, 1006 hPa et 45 % d'humidité, avec l'étalonnage de sa fiche technique : un programme écrit pour le vrai capteur le lit tel quel.

## Exemple : le connecteur et un PWM

Montre les 40 broches, puis met un PWM à 25 % sur le GPIO 18 et le relit par `gp_info`.

```c
#include <stdio.h>
#include <gpiokit_abi.h>

int main (void)
{
	if (!gpiokit_kit_open ())
		return 1;
	long av = gp_available ();
	printf ("GPIO : %s\n", av == 1 ? "connecteur" : av == 2 ? "simulateur" : "aucun");
	for (int p = 1; p <= 40; p += 2)
	{
		const char *l = (const char *) gp_header_label (p), *r = (const char *) gp_header_label (p + 1);
		const char *fl = (const char *) gp_gpio_function (gp_header_gpio (p));
		const char *fr = (const char *) gp_gpio_function (gp_header_gpio (p + 1));
		printf ("%8s %-6s %2d | %-2d %-6s %s\n", gp_header_gpio (p) >= 0 ? fl : "", l, p, p + 1, r,
			gp_header_gpio (p + 1) >= 0 ? fr : "");
	}

	long r = gp_pwm (18, 1000, 2500);		/* 1 kHz, 25 % */
	if (r < 0)
	{
		printf ("PWM : %s\n", (const char *) gp_error (r));
		return 1;
	}
	unsigned char info[GP_PINS][GPI_SIZE];
	long n = gp_info ((long) info, GP_PINS);
	for (long i = 0; i < n; i++)
	{
		unsigned char *e = info[i];
		if (e[GPI_MODE] == GP_M_PWM)
			printf ("GPIO %d : %s, %u Hz, %u %%\n", e[GPI_PIN], (const char *) gp_mode_name (e[GPI_MODE]),
				*(unsigned *) (e + GPI_FREQ), *(unsigned *) (e + GPI_DUTY) / 100);
	}
	gp_release ();
	return 0;
}
```

## Exemple : une LED et un bouton

Une LED sur le GPIO 17 clignote ; un bouton sur le GPIO 27 (tiré vers le haut, il met la broche à la masse quand on l'appuie) est suivi par ses fronts. Sous le simulateur, le programme « appuie » lui-même avec `gp_sim_input`.

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
	printf ("bouton au repos : %ld\n", gp_read (BUTTON));
	unsigned long t0 = (unsigned long) gp_now_us ();
	for (int i = 0; i < 10; i++)
	{
		printf ("LED %ld\n", gp_toggle (LED));
		if (sim && i == 3)
			gp_sim_input (BUTTON, 0);	/* appuye */
		if (sim && i == 6)
			gp_sim_input (BUTTON, -1);	/* relache : le tirage le remonte */
		unsigned char ev[8][GPE_SIZE];
		long n = gp_events ((long) ev, 8, 250);	/* attend 250 ms au plus */
		for (long k = 0; k < n; k++)
		{
			unsigned long us = *(unsigned long *) (ev[k] + GPE_US);
			printf ("  GPIO %d %s a %lu ms%s\n", ev[k][GPE_PIN],
				ev[k][GPE_EDGE] == GP_FALLING ? "descend" : "monte", (us - t0) / 1000,
				ev[k][GPE_LOST] ? " (fronts perdus avant)" : "");
		}
	}
	gp_write (LED, 0);
	gp_release ();
	return 0;
}
```

## Exemple : capteur I2C et SPI

Parcourt le bus I2C, lit la température d'un BME280 (formule entière de sa fiche technique), puis échange quatre octets sur le SPI (rebouclé dans le simulateur : ils reviennent).

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
		printf ("I2C : %s\n", (const char *) gp_error (r));
		return 1;
	}
	unsigned char map[16];
	printf ("%ld appareil(s) sur le bus\n", gp_i2c_scan ((long) map));
	for (int a = 0; a < 128; a++)
		if (map[a / 8] >> (a % 8) & 1)
			printf ("  0x%02X  %s\n", a, (const char *) gp_i2c_guess (a));

	if (gp_i2c_reg_read (BME, 0xD0) == 0x60)		/* identifiant du BME280 */
	{
		gp_i2c_reg_write (BME, 0xF4, 0x27);		/* mesure continue */
		unsigned char reg = 0x88, c[6], d[3];
		gp_i2c_write_read (BME, (long) &reg, 1, (long) c, 6);	/* T1, T2, T3 */
		reg = 0xFA;
		gp_i2c_write_read (BME, (long) &reg, 1, (long) d, 3);	/* temperature brute */
		int t1 = c[0] | c[1] << 8, t2 = (short) (c[2] | c[3] << 8), t3 = (short) (c[4] | c[5] << 8);
		int adc = d[0] << 12 | d[1] << 4 | d[2] >> 4;
		int v1 = (((adc >> 3) - (t1 << 1)) * t2) >> 11;
		int v2 = (((((adc >> 4) - t1) * ((adc >> 4) - t1)) >> 12) * t3) >> 14;
		int t = ((v1 + v2) * 5 + 128) >> 8;			/* centiemes de degre */
		printf ("BME280 : %d,%02d C\n", t / 100, t % 100);
	}
	gp_i2c_close ();

	if (gp_spi_open (0, 0) == 0)
	{
		unsigned char out[4] = { 0xDE, 0xAD, 0xBE, 0xEF }, in[4] = { 0 };
		long n = gp_spi_transfer (0, (long) out, (long) in, 4);
		printf ("SPI : %ld octets, recu %02X %02X %02X %02X\n", n, in[0], in[1], in[2], in[3]);
		gp_spi_close ();
	}
	gp_release ();
	return 0;
}
```

## Pièges

- **GPIO et broche** : `gp_mode (11, ...)` touche le GPIO 11 (broche 23 du connecteur), pas la broche 11. Passez par `gp_header_gpio` si vous pensez en places.
- **Rendre ce qu'on tient** : une broche reste au programme jusqu'à sa fin. Un programme qui tourne longtemps appelle `gp_release ()` (ou `gp_mode (g, GP_M_FREE)`) dès qu'il n'en a plus besoin, sinon GPIO Lab et les autres reçoivent `GP_EBUSY`.
- **Le PocketTerm** : son écran tactile occupe le bus I2C 1 (GPIO 2, 3, 4) ; `gp_i2c_open` y rend une erreur.
- **Sous QEMU**, `gp_sim (0)` n'apporte rien : sans PWM, I2C ni SPI émulés, le kit reste sur le simulateur.
- **Les fronts** sont relevés au plus toutes les millisecondes : une impulsion plus brève peut passer inaperçue (`GPE_LOST` signale les fronts perdus, pas ceux jamais vus).
- **Tension** : les broches du Pi sont en 3,3 V. Un montage en 5 V les détruit.

## Voir aussi

- **GPIO Lab** : voir et piloter les broches à la souris, avec le simulateur.
- [libc et couche POSIX](libc_fr.md) : threads, pour lire des fronts sans bloquer une interface.
- [UIKit](uikit_fr.md) : pour mettre l'état des broches dans une fenêtre.
