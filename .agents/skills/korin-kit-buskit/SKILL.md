---
name: korin-kit-buskit
description: BusKit, le bus d'événements de Korin OS (service busd) — publier une valeur sous un sujet nommé, s'abonner à un nom ou à un préfixe, lire l'état courant (thème, réseau, Wi-Fi, USB, volumes, son, heure, musique) ; fonctions bus_*, événements WEV_BUS, commande bus. À charger pour tout programme qui réagit à un changement du système au lieu de sonder, ou qui prévient d'autres programmes.
---

# BusKit : le bus d'événements

`/lib/buskit.so`, client du service `bus` (**busd**, `/sys/svc/busd`, lancé par init2 en premier, relancé
s'il tombe). Un programme publie, les abonnés reçoivent `WEV_BUS` (13) **dans la file d'événements du programme**,
la même que celle des fenêtres : `ui_event` le rend comme les autres ; sans UIKit, `bus_wait`. Les appels directs
entre programmes restent les services nommés du noyau : le bus ne fait que prévenir.

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Référence (sujets du système, format de l'événement) | — | `docs/BUS.md` ; sondes restantes : `docs/POLLING.md` |
| Table du kit | — | `src/kits/buskit/buskit.abi`, constantes `buskit.inc` |
| En-tête C | `/devtools/gcc/include/kits/buskit_abi.h` | généré par `tools/kits/abi2c.py` |

Pas de guide `buskit_fr.md` dans l'aide. Usages réels : `src/apps/disks/main.cpp` (C++, abonné aux volumes dans la
boucle `ui_event`), `src/bin/bus/bus.asm` (`bus_wait`), `src/sys/menubar/menubar.asm`, `src/apps/clock/clock.asm`,
`src/apps/music/music.asm`, `src/svc/usbstorage/usbstorage.asm` (asm).

## Ouvrir le kit

C / C++ :

```c
#include <buskit_abi.h>
if (!buskit_kit_open ()) { /* kit absent : sonder comme avant */ }
```

`pibuild` lie déjà tout `/devtools/gcc/lib/lib*.a` ; à la main : `-lbuskit`. Assembleur : `include '../../kits/buskit/buskit.inc'`,
`kit_use '../../kits/buskit/buskit.abi'` dans le code, `bl kits_open`, `kit_data` dans les données. Mêmes noms dans
tous les langages (skills `korin-lang-*`).

## L'essentiel

Tous les arguments sont des `long` (chaînes : `(long) "korin.theme"`) ; erreurs `BUS_E*` négatives : `BUS_ENODEV`
(busd absent), `BUS_EINVAL` (nom, motif, valeur), `BUS_ENOENT`, `BUS_ENOMEM`, `BUS_EPERM`.

- `bus_available()` → 1 : busd répond
- `bus_publish(nom, valeur, octets | -1 pour une chaîne, BUS_RETAIN | BUS_DELETE)` → nombre d'abonnés prévenus
- `bus_subscribe(motif, BUS_CURRENT | 0)` → abonnement (> 0) ; motif : nom exact, préfixe + `*` (`korin.net.*`), ou `*`
- `bus_unsubscribe(abonnement | 0 = tous ceux du programme)` → 0
- `bus_get(nom, tampon, taille)` → octets de la valeur (copiée jusqu'à la taille)
- `bus_get_id(sujet, tampon, taille)` → octets ; `bus_name(sujet, tampon BUS_NAME_MAX)` → 0 (sujet = `BEV_TOPIC`)
- `bus_list(sujet_de_départ | 0, tampon 64)` → sujet suivant (0 : fin)
- `bus_wait(événement 32 o, attente ms)` → type (`WEV_BUS`…) ; 0 : rien ; pour un programme sans UIKit

Événement (32 octets) : +0 `WEV_BUS`, `BEV_SUB` (+4, l'abonnement), `BEV_TOPIC` (+8), `BEV_LEN` (+12, `BUS_DELETED`
= 0xFFFFFFFF : sujet retiré), `BEV_DATA` (+16, les 12 premiers octets de la valeur, `BUS_INLINE`). Avec UIKit :
`ui_ev_field(&ui, BEV_SUB)`, `ui.ev + BEV_DATA`. Limites : nom 63 octets (`BUS_NAME_MAX` 64, sans espace ni `*`),
valeur 256 octets (`BUS_VALUE_MAX`), 128 sujets et 256 abonnements dans tout le système, file de 64 événements.

Sujets du système (tous gardés) :

| Sujet | Valeur |
|---|---|
| `korin.theme` | `<thème>, <accent>` (panneau de configuration, après `/etc/theme.txt`) |
| `korin.net.state` | `up <genre> <adresse> <interface>` (`up wifi 192.168.0.16 wlan0`) ou `down` |
| `korin.net.wifi` | `<état> <barres 0-4> <ssid>` ; état `absent`, `starting`, `ready`, `joining`, `connected` |
| `korin.usb.device.<n>` | `vvvv:pppp cc/ss/pp ...` ; retiré au débranchement |
| `korin.storage.volume.<vol>` | `<source> <service>` (`blk.usb0p1 fatfs`) ; `<vol>` en minuscules ; retiré au démontage |
| `korin.audio.state` | `<génération> <volume> <muet> <sortie>` (`12 80 0 HDMI`) |
| `korin.clock.offset` | décalage de l'heure locale, comme `/etc/clock.cfg` |
| `korin.shell.home`, `korin.media.now`, `korin.media.control` | accueil du mode poche, lecteur de musique (`docs/MODE-POCHE.md`) |

Commande `bus` : `bus list`, `bus get <nom>`, `bus pub [-r] <nom> <valeur>`, `bus del <nom>`,
`bus watch [-c] <motif>` (`-c` : d'abord l'état courant ; Ctrl+C pour finir).

## Exemple

Suivre le réseau sans fenêtre (d'après `src/bin/bus/bus.asm` et `docs/BUS.md`) :

```c
#include <stdio.h>
#include <buskit_abi.h>

int main (void)
{
	if (!buskit_kit_open () || !bus_available ()) { printf ("busd absent\n"); return 1; }
	long sub = bus_subscribe ((long) "korin.net.*", BUS_CURRENT);	/* l'état courant d'abord */
	if (sub <= 0) return 1;
	unsigned char ev[32];
	char name[BUS_NAME_MAX], value[BUS_VALUE_MAX + 1];
	for (;;)
	{
		if (bus_wait ((long) ev, 60000) != WEV_BUS) continue;
		unsigned topic = *(unsigned *) (ev + BEV_TOPIC), len = *(unsigned *) (ev + BEV_LEN);
		if (bus_name (topic, (long) name) != 0) continue;
		if (len == BUS_DELETED) { printf ("%s : retiré\n", name); continue; }
		long n = bus_get_id (topic, (long) value, BUS_VALUE_MAX);
		if (n < 0) continue;
		value[n < BUS_VALUE_MAX ? n : BUS_VALUE_MAX] = 0;
		printf ("%s = %s\n", name, value);
	}
}
```

Avec UIKit, pas de `bus_wait` : `t = ui_event (ms)`, puis `if (t == WEV_BUS && ui_ev_field (&ui, BEV_SUB) == sub)`
(`src/apps/disks/main.cpp`).

## Pièges

- **Le thème** : ne pas s'abonner à `korin.theme` ni lire `theme.txt`. UIKit s'y abonne déjà dans `ui_init`, recharge
  les jetons, redessine les widgets et envoie **`WEV_THEME` (14)** : l'app redessine alors ce qu'elle dessine
  elle-même, avec `ui_tokens()`. Une app qui lit `theme.txt` elle-même ne change qu'à sa relance.
- **busd peut manquer ou être relancé** : prévoir le repli (sonder comme avant) ; après une relance, BusKit rouvre le
  canal seul, mais sujets et abonnements sont perdus : se réabonner, republier son état.
- `BUS_RETAIN` pour un **état** (un abonné `BUS_CURRENT` le reçoit tout de suite) ; sans lui, une simple notification.
- Une valeur de plus de 12 octets se relit par `bus_get_id` : `BEV_DATA` n'en a que le début.
- File pleine (64) : l'événement est perdu pour ce programme, la valeur reste lisible. Deux motifs qui couvrent un
  même sujet donnent deux événements.
- Réagir à l'événement en relisant l'état par le kit du domaine (NetKit pour le réseau…) : le bus prévient, il ne
  remplace pas l'API. Nommer ses sujets `<app>.<chose>` ; `korin.*` est au système (rien ne l'interdit encore).
- Grosses données : un fichier ou un tampon partagé, et le sujet dit où.
