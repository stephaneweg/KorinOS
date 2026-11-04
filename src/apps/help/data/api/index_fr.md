# Guide du développeur

Korin OS est écrit en assembleur AArch64, mais ses applications peuvent l'être en **C** et en **C++** : GCC 14 tourne dans le système lui-même (`/gcc`), et chaque bibliothèque partagée du système, un **kit**, s'appelle depuis le C comme une fonction ordinaire. Ce guide montre comment écrire une application, un service ou un kit, et décrit chaque kit avec des exemples complets à copier-coller : chaque exemple de ces pages se compile tel quel avec la chaîne de l'OS.

## Pour commencer

1. [Créer une application](app_fr.md) : les outils, le dossier d'une app, `app.txt`, la construction sous Korin OS ou sur le PC.
2. [Le squelette d'app en C++](skeleton_fr.md) : une app complète de 150 lignes à copier, qui suit le thème du bureau.
3. [La libc et la couche POSIX](libc_fr.md) : fichiers, threads, processus, sockets, ce qui marche et ce qui manque.

## Les kits

| Kit | Pour | En-tête C |
|---|---|---|
| [UIKit](uikit_fr.md) | fenêtres, événements, dessin, texte, widgets, dialogues, menus, presse-papiers, images | `uikit.h` |
| [AudioKit](audiokit_fr.md) | son : lecture, capture, formats, mixer, synthèse FM et MIDI | `audiokit_abi.h` |
| [MediaKit](mediakit_fr.md) | vidéo : WebM, MP4, VP8, VP9, AV1 | `mediakit.h` |
| [GpuKit](gpukit_fr.md) | le GPU V3D du Raspberry Pi, la 3D | `gpukit_abi.h` |
| [NetKit](netkit_fr.md) | réseau : sockets, DNS, état, heure, Wi-Fi | `netkit_abi.h` |
| [GpioKit](gpiokit_fr.md) | le connecteur à 40 broches : GPIO, PWM, I2C, SPI | `gpiokit_abi.h` |
| [ArchiveKit](archivekit_fr.md) | compression, ZIP, tar, gzip | `archivekit_abi.h` |
| [DocumentKit](documentkit_fr.md) | formats bureautiques : XML, encodages, OOXML, ODF, CSV | `documentkit_abi.h` |
| [PrinterKit](printerkit_fr.md) | impression : dialogue, pages, PDF, imprimantes réseau | `printerkit.h` |
| [EmuKit](emukit_fr.md) | le socle des émulateurs : fenêtre, cadence, son, manettes | `emukit.h` |

AppKit, le kit de base (fichiers, lancement des programmes, chaînes, langues), est déjà pris par la libc : un programme C n'a pas à l'ouvrir ([libc](libc_fr.md)).

## Aller plus loin

- [Écrire un service](service_fr.md) : un programme de `SD0:/sys` qui répond aux autres par un nom.
- [Les fonctions d'IPC](ipc_fr.md) : services nommés, canaux, mémoire partagée, attente.
- [Écrire un kit](kit_fr.md) : une bibliothèque partagée par tous les programmes.

## Aide d'une application

Une application a une aide si son dossier contient `help_fr.md` ou `help_en.md` (Markdown). Quand elle est au premier plan, la barre de menus ajoute alors un menu **Aide** qui ouvre ce fichier dans le lecteur d'aide ; le fichier de la langue du système est pris d'abord (`lang` de `SD0:/etc/system.ini`, `fr` sans clé), l'autre s'il manque. Si l'app publie déjà un menu nommé **Aide**, les lignes de l'aide vont en tête du sien.

```
monapp/
  main.cpp
  app.txt
  help_fr.md
  help_en.md
  help/
    fenetre.png
```

`pibuild` et `tools/office/build.sh` posent `help_*.md` et le dossier `help/` à côté de l'exécutable. Ce que le lecteur sait montrer :

| Écrire | Pour |
|---|---|
| `#`, `##`, `###` | les titres (la table des matières, à gauche, les liste) |
| `**gras**`, `*italique*`, `` `code` `` | le texte |
| `- élément`, `1. élément` | les listes, imbriquées de deux espaces |
| `> texte` | une remarque |
| trois accents graves, puis le code, puis trois accents graves | un bloc de code, avec son bouton **Copier** |
| `\| a \| b \|` puis `\|---\|---\|` | un tableau |
| `![légende](help/fenetre.png)` seul sur sa ligne | une image PNG ou BMP, réduite à la largeur de la page |
| `[texte](autre_fr.md#titre)` | un lien vers une page, un titre de page |
| `[texte](help:files)`, `[texte](app:files)` | l'aide d'une autre app ; lancer une app |
| `[texte](https://...)` | une page web, ouverte dans Jet |

Un titre se cible par son ancre : minuscules, espaces changés en `-`, ponctuation retirée (`## Aide d'une application` donne `#aide-dune-application`).

> **Remarque** : une page en anglais peut lier `autre_fr.md` : si le fichier manque, le lecteur prend `autre_en.md`, et l'inverse.
