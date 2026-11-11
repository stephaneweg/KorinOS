---
name: korin-lang-qbasic
description: Écrire un programme dans le BASIC d'Onyx sous Korin OS — runtime /bin/basic (VM et code natif), éditeur QBasic, IDE QBStudio (.form), GPIO Lab, Turtle Quest ; graphisme, son, fenêtres, fichiers, GPIO et kits par #import. À charger pour tout .bas lancé par basic, QBasic, QBStudio ou GPIO Lab (pas FreeBASIC : voir korin-lang-freebasic).
---

# Le BASIC d'Onyx sous Korin

Langage à la QBasic étendu (classes, propriétés, fenêtres, kits), repris d'Onyx. **Ce n'est pas FreeBASIC** (`fbc`).

## Où lire

| Sujet | Dans le dépôt | Sur la carte |
|---|---|---|
| Vue d'ensemble, état, ce qui est ouvert | `docs/BASIC.md` | — |
| **Référence du langage** (tous les mots-clés) | `src/apps/qbasic/data/help.txt` | QBasic > Help > Keywords (F1) |
| QBStudio (`.form`, projets) | `src/apps/qbstudio/data/help.txt`, `src/apps/qbstudio/README.md` | F1 dans QBStudio |
| Runtime, options | `src/apps/basic/README.md` | — |
| Exemples | `skel/sd0/basic/examples/` (`uikit.bas`, `kits.bas`, `gui.bas`, `graphics.bas`, `gpio_*.bas`, `fr/`) | `SD0:/basic/examples/` (paquet `basic-samples`), QBasic > File > Examples |
| Projets QBStudio d'exemple | `skel/sd0/home/projects/converter`, `pages` | `SD0:/home/projects/` |
| Ce qu'un kit offre à `#import` | `src/lib/basic/bi/`, généré par `tools/kits/abi2bas.py` | `SD0:/lib/<kit>.bi` |

## Outils

| Outil | Où | Rôle |
|---|---|---|
| `basic` | `/bin/basic` (`src/apps/basic`) | le runtime : compile en bytecode, exécute (VM + JIT AArch64) |
| QBasic | `/apps/qbasic.app` (`src/apps/qbasic`) | éditeur ; ouvre `.bas` et `.bax` |
| QBStudio | `/apps/qbstudio.app` (`src/apps/qbstudio`) | IDE : fenêtre dessinée (`.form`) + code (`.bas`) |
| GPIO Lab | `/apps/gpiolab.app` (`src/apps/gpiolab`) | broches, I2C, simulateur ; sa vue Code exécute du BASIC |
| Turtle Quest | `/apps/turtle.app` (`src/apps/turtle`) | jeu pour apprendre : BASIC + mots de la tortue |

```sh
basic prog.bas [arguments]        # PRINT / INPUT sur la console tant qu'aucune fenêtre n'est ouverte
basic -c prog.bas                 # écrit prog.bax (bytecode, démarre sans analyse)
```

Autres options : `-m` (la VM seule), `-d dossier`, `-t` (texte sur la sortie sans terminal), `-p` (profil dans
`SD0:/basprof.txt`), `-i fd` (erreurs vers un éditeur).

Dans QBasic : F5 ou Ctrl+R exécute, Ctrl+K vérifie, F2 liste les SUB/FUNCTION, Ctrl+C arrête le programme.
Construire les outils eux-mêmes : `pibuild bin /src/apps/basic basic`, `pibuild app /src/apps/qbasic qbasic`
(ou `sh tools/office/build.sh basic qbasic gpiolab` sur le PC).

## Structure d'un programme

- Un `.bas` : module principal, puis `SUB` / `FUNCTION` (`DECLARE SUB` en tête si appelées avant). Noms relatifs = dossier
  du programme. `.bax` = la version compilée, lancée comme un `.bas` ; `CHAIN "part"` trouve `part.bas` ou `part.bax`.
- **App** : QBasic > File > Make App, ou QBStudio > Créer l'application → un exécutable autonome (`/bin/basic` + le
  programme attaché) dans `SD0:/apps/<nom>.app/` avec `app.txt`.
- **Projet QBStudio** : `SD0:/home/projects/<nom>/` avec `project.ini` (`name`, `title`, `main = Main`,
  `files = Main.form Main.bas`, `category`, `compiled`), `Main.form` (un contrôle par ligne, l'indentation donne le parent :
  `Window Main "Titre" size=380x260 resizable`, `Column`, `Row`, `Label`…) et `Main.bas` avec des SUB
  `<contrôle>_<évènement>` (`convert_Click`, `celsius_Change`, `Main_Load`) ; les contrôles s'y lisent par
  `nom.Text`, `nom.Value`, `nom.Checked`, `nom.Focus`.

## Ce qui atteint le système

Tout est dans `help.txt` ; l'essentiel :

- **Graphisme** : `SCREEN 0|1|2|7|8|9|11|12|13`, `PSET`, `LINE … B|BF`, `CIRCLE … F` (rempli, Onyx), `PAINT`, `DRAW`,
  `GET`/`PUT`, `PALETTE`, `VIEW`, `WINDOW`, `PCOPY`, `DRAWTEXT x, y, t$`, `FULLSCREEN ON|OFF`, `COLOR fg, bg` (index ou
  `RGB(r,g,b)`). Entrées : `INKEY$`, `KEYDOWN("LEFT")`, `MOUSEX/MOUSEY/MOUSEB`, `PAD(n)`, `STICK(n)`, `STRIG(n)`.
- **Son** (prise jack 3,5 mm) : `PLAY "T120 O3 L4 CDE"` (`MB` en arrière-plan), `SOUND freq, ticks`, `BEEP`,
  `NOTEON voix, freq[, onde, volume]` / `NOTEOFF`.
- **Fenêtre et contrôles** : `WINDOW titre$, l, h[, 1]`, `BUTTON`, `LABEL`, `TEXTBOX`, `CHECKBOX`, `LISTBOX`, `DROPDOWN`,
  `PROGRESS`, `SLIDER` (rendent un id), `SETTEXT`, `GETTEXT$`, `SETVALUE`, `VALUE`, `MENUITEM`, `e = WAITEVENT` (-1 :
  fermée), `EVENT`.
- **Système** : `NOTIFY`, `MSGBOX`, `CLIPBOARD$`/`SETCLIPBOARD`, `OPENFILE$`, `SAVEFILE$`, `EXEC`, `LAUNCH`, `COMMAND$`,
  `SHELL "outil args"`.
- **Fichiers** : `OPEN … FOR INPUT|OUTPUT|APPEND|RANDOM|BINARY AS #n`, `GET`/`PUT`, `FIELD`, `KILL`, `NAME`, `MKDIR`,
  `FILES`, `FILEEXISTS`, `DIR$`, `ENVIRON$`.
- **GPIO** (GpioKit, service `gpio`) : `PINMODE 17, "OUT"` (`"PULLUP"`…), `PIN 17 = 1`, `PIN(27)`,
  `ON PIN (27, 2) GOSUB lbl` (1 montant, 2 descendant, 3 les deux), `PIN (27) OFF`, `PINFREE [n]`, `SERVO 18, angle`,
  `PWM`, `I2COPEN vitesse`, `I2CSCAN$`, `I2CREAD(adr, reg)`, `I2CREAD$(adr, reg, n)`, `I2CWRITE adr, reg, v`,
  `I2CSEND adr, s$`, `SPIOPEN`, `GPIOSIM 1` (simulateur, visible dans GPIO Lab). Mots français aussi (`I2COUVRIR`…).
  Syntaxe exacte de `PWM`, `SPIOPEN`, `PINCHANGED` : pas dans `help.txt`, à vérifier dans `src/lib/basic/bascomp.cpp`.

## Utiliser les kits

`#import <Kit>` en tête (casse libre) rend un kit de `SD0:/lib` appelable par `Kit.nom(arguments)` ; la liste est dans
`SD0:/lib/<kit>.bi`. Les noms sont ceux d'**Onyx** pour UIKit (`UIKit.window`, `UIKit.button`…, `src/apps/basic/baskits.h`),
et ceux du `.abi` sans préfixe pour les kits générés : `audiokit netkit gpukit mediakit archivekit documentkit gpiokit
printerkit`, plus `uikit appkit` (avec un en-tête) ; `FileKit`, `ImageKit`, `SystemKit` sont écrits à la main
(`src/lib/basic/bi/`) et ne couvrent que ce que les exemples appellent.

- Chaîne pour un texte, nombre pour un nombre ; poignée ou pointeur dans une variable simple ou `#` (pas `%`, `&`).
- `BYREF v` où la fonction remplit un nombre ; `ADDRESSOF(Sub)` pour un rappel ; `p# = ALLOC(n)` / `DEALLOC p#` ;
  `CSTR$(p#)` ; `PEEKB…PEEKD`, `POKEB…POKES` ; structures : `DIM e AS FileKit.zip_entry`, `PEEKT`/`POKET`.
- Ce qu'un kit alloue se libère par sa fonction (`FileKit.free p#`). **Rien n'est vérifié** : une adresse fausse tue le
  programme.

Ex. : `#import SystemKit` puis `SystemKit.notify "Titre", "Texte"` ; `#import FileKit` puis
`size# = FileKit.file_size("SD:/config.txt")` (`kits.bas`).

## Exemple

Tiré de `skel/sd0/basic/examples/uikit.bas` (vérifié inchangé sous QEMU), réduit :

```basic
#import UIKit

DECLARE SUB Greet (widget)
DECLARE SUB Quit ()

DIM SHARED win, nameBox, greeting, list

win = UIKit.window("UIKit from BASIC", 360, 260, 1)	' 1 : redimensionnable
IF win = 0 THEN END
greeting = UIKit.label(win, 12, 12, 336, 20, "Your name:")
nameBox = UIKit.textbox(win, 12, 38, 236, 26, "", 0)
b = UIKit.button(win, 256, 38, 92, 26, "Greet", ADDRESSOF(Greet))
list = UIKit.listbox(win, 12, 74, 336, 140, "", 0)
UIKit.menu_item win, "App", "Quit", "Ctrl+Q", ADDRESSOF(Quit)
UIKit.focus nameBox

DO WHILE UIKit.window_wait(win)	' les évènements appellent les SUB
LOOP
END

SUB Greet (widget)
	n$ = UIKit.get_text(nameBox)
	IF n$ = "" THEN n$ = "you"
	UIKit.set_text greeting, "Hello, " + n$ + "!"
	UIKit.add_item list, "Greeted " + n$ + " at " + TIME$
END SUB

SUB Quit ()
	UIKit.window_close win
END SUB
```

Sans kit, la même chose
avec les mots intégrés : `WINDOW`, `BUTTON`, `WAITEVENT` (voir `gui.bas`).

## Pièges

- **Une fenêtre par programme** ; après `UIKit.window`, `PRINT` n'affiche plus rien.
- `INTEGER` fait 64 bits et **arrondit** au stockage (`7 / 2` → 4) ; un nom sans type est `REAL`.
- Le texte est en **Latin-1 / code page 437** pour BASIC ; QBStudio convertit l'UTF-8 de ses éditeurs.
- Une app faite par Make App **ne démarre pas depuis le menu** (journal : `usage: basic ...`, hypothèse : `argv[0]` sans
  chemin complet) ; QBasic garde `-i 3` et perd les erreurs d'exécution (QBStudio utilise `-i 2`, qui marche).
- Thème sombre : fond clair autour des contrôles d'un programme lancé jusqu'au premier redimensionnement.
- 3D par le GPU jamais vue (sous QEMU : repli CPU). GPIO : service jamais essayé sur le vrai Pi ; PWM, I2C, SPI réels
  jamais testés, le simulateur oui. Manettes : connues seulement à leur premier évènement ; `PAD(3)` = clavier mappé par
  `SD0:/etc/gamepad.ini`.
- QBasic, QBStudio, GPIO Lab, Turtle Quest : essayés sous QEMU ; les manques de chacun sont dans son `README.md`.
