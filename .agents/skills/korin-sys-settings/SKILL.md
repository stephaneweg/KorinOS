---
name: korin-sys-settings
description: Régler Korin OS — thème et accent, son et sorties, langue, mode du bureau (bureau, poche, console), pays et fuseau, bus d'évènements (bus list/get/pub/watch), notifications, captures d'écran, lecture d'un son. À charger pour changer un réglage ou observer l'état du système.
---

# Réglages du système

Les réglages vivent dans des fichiers de `SD0:/etc`, et les services publient leur état sur le **bus**
(busd). **Gardez une copie** de tout fichier de `SD0:/etc` avant de le modifier, et dites à l'utilisateur ce qui
change. Référence : `docs/BUS.md`, `docs/SON.md`, `docs/MODE-POCHE.md`.

## Le bus

| Commande | Effet |
|---|---|
| `bus list` | les sujets et leur valeur |
| `bus get <nom>` | la valeur d'un sujet |
| `bus pub [-r] <nom> <valeur>` | publier (`-r` : valeur gardée) |
| `bus del <nom>` | retirer un sujet |
| `bus watch [-c] <motif>` | suivre les évènements jusqu'à Ctrl+C (`-c` : d'abord les valeurs gardées) ; motif `korin.usb.*` ou `*` |

Sujets : `korin.theme`, `korin.net.state`, `korin.net.wifi`, `korin.audio.state`, `korin.clock.offset`,
`korin.storage.volume.*`, `korin.usb.device.*`, `korin.media.*`. Un programme C passe par BusKit
(`korin-kit-buskit`).

## Thème

1. Écrire `SD0:/etc/theme.txt`. Lisez-le d'abord : il porte le thème (Givre, Onyx Noir…), l'accent, et
   `density = touch` en mode poche.
2. Puis `bus pub -r korin.theme "<thème>, <accent>"` : les apps ouvertes changent aussitôt.

Les apps ne lisent jamais `theme.txt` elles-mêmes : UIKit le fait pour elles (`ui_tokens`).

## Son

| Commande | Effet |
|---|---|
| `volume`, `volume 0..100`, `volume mute|unmute|toggle` | la sortie principale |
| `volume output [auto|jack|hdmi|usb|<n>]` | les sorties, ou la sortie préférée |
| `volume apps`, `volume app <nom|pid> 0..100|mute|unmute` | le volume de chaque programme |
| `soundctl …` | le mixer complet : volume d'une sortie, routes, sortie principale |
| `play <fichier> [--volume N] [--seek ms] [--output n]` | jouer un WAV, FLAC, MP3 ou Ogg Vorbis |

Le service `sound` retient les réglages dans `SD0:/etc/sound.txt`.

## Système, langue, région

- **`SD0:/etc/system.ini`** porte deux réglages, qui prennent effet au redémarrage :
  - `shell=` : `desktop` (le bureau), `poche` (une app devant, accueil, commutateur ; c'est le PocketTerm) ou
    `console` (pas d'interface graphique) ;
  - `lang=` (par exemple `en`) : sans cette ligne, l'interface est en français.
- **`SD0:/etc/options.txt`** porte `pays=FR`, `fuseau=Europe/Paris` (un nom IANA, ou `UTC±h`),
  `ntp=` (serveur, ou `ntp=non`) et `keyb=` (clavier).
- **L'heure** vient du NTP. `date [-u] [+FORMAT]` la lit, mais ne la règle pas.

## Bureau

- `notify [-a app] "titre" message` envoie une notification ; avec `-a`, la toucher ouvre l'app.
- `screenshot [fichier]` fait une capture de l'écran en BMP (`RAM0:/screen.bmp` par défaut). C'est utile pour vérifier
  ce qu'affiche une app.
- L'app **Réglages** (`control`, ses applets sont les apps de catégorie Réglages) et les Réglages rapides regroupent ces réglages en fenêtres.
