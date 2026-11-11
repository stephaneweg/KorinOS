---
name: korin-lang-freebasic
description: Écrire un programme FreeBASIC 1.10.1 pour Korin OS — fbc dans /gcc (GCC en backend), gfxlib2 dessinée dans une fenêtre d'UIKit, les kits par les .bi de /gcc/include/freebasic/kits, construction par pibuild. À charger pour tout programme .bas compilé par fbc (pas le BASIC d'Onyx : voir korin-lang-qbasic).
---

# FreeBASIC sous Korin

Deux BASIC différents : **FreeBASIC** (`/gcc/bin/fbc`, compilateur, ELF natif) et le **BASIC d'Onyx** (`/bin/basic`,
VM, `#import`) — ce dernier a sa propre compétence, `korin-lang-qbasic`. Ne pas mélanger leurs syntaxes.

## Où lire

| Sujet | Dans le dépôt | Sur la carte |
|---|---|---|
| FreeBASIC sous Korin (outils, kits, pilote graphique, limites) | `docs/GCC.md`, section « FreeBASIC » | — |
| pibuild (règles des `.bas`) | `docs/GCC.md`, section « pibuild » ; `tools/gcc/pibuild.mk` | `/gcc/share/pibuild.mk` |
| Les `.bi` des kits | générés par `tools/gcc/kits.sh` (`tools/kits/abi2bi.py`) ; `tools/kits/bi/uikit.bi` écrit à la main | `/gcc/include/freebasic/kits/` |
| Les fonctions d'un kit (noms C) | compétences `korin-kit-<kit>`, `src/apps/help/data/api/<kit>_fr.md` | `SD0:/apps/help.app/api/<kit>_fr.md` |
| Essais | `tools/gcc/test/basic/` : `hello.bas`, `gfx.bas`, `filethread.bas`, `kitui.bas`, `pibasic/` | `/gcc/test/basic/` |

## Outils

Paquet `freebasic` (`pkg add freebasic`, il demande `gcc`) : `/gcc/bin/fbc`, `/gcc/lib/freebasic/armpi-aarch64/`,
`/gcc/include/freebasic/` (dont `kits/`). Cible : `armpi-aarch64`.

```sh
/gcc/bin/fbc hello.bas -x /tmp/hello        # sans -x : l'exécutable à côté du source
/gcc/bin/fbc -O 2 gfx.bas -x /tmp/gfx
/gcc/bin/fbc -mt filethread.bas            # avec threads (libfbmt)
pibuild app <dossier> <nom>                # -> /apps/<nom>.app/main (+ app.txt, data/)
pibuild bin <dossier> <nom>                # -> /bin/<nom>
```

`fbc` traduit en C (`-gen gcc`, le défaut), le GCC de Korin compile, `gcc` lie (specs : `crt0posix.o`, `libposix`,
`posix.ld`). Le résultat est un ELF statique ordinaire. Temps sur le Pi : `hello.bas` 3,3 s ; sous QEMU 42 s.
Sur le PC (WSL), refaire le compilateur : `sh tools/gcc/fbc.sh` après `tools/gcc/build.sh` et `tools/gcc/kits.sh`.

## Structure d'un programme

Pour `pibuild` : un dossier avec `main.bas` (ou `<dossier>.bas`) en module principal ; **tous** les `.bas` du dossier et
des sous-dossiers sont compilés par `fbc -O 2` en une seule commande et liés. Pour une app : `app.txt` et `data/`
comme pour une app C (compétence `korin-lang-c`). Rien n'est écrit à côté des sources (objets dans `/tmp/pibuild/<nom>`).
Exemple à deux modules : `tools/gcc/test/basic/pibasic/` (`main.bas`, `lib/add.bas`, `lib/add.bi`).

Deux façons d'avoir une fenêtre :
- **gfxlib2** (`SCREEN`, `SCREENRES`, `LINE`, `PRINT`, `INKEY`, `MULTIKEY`, `GETMOUSE`…) : le pilote
  `tools/gcc/fbc/gfxlib2/armpi/gfx_driver_uikit.c` copie le tampon de FreeBASIC dans une fenêtre d'UIKit ; cadre et
  titre d'Elegant, donc du thème. Pas besoin d'inclure un kit.
- **UIKit directement** (`#include once "kits/uikit.bi"`) : widgets, menus et thème du système, comme en C.

## Utiliser les kits

`#include once "kits/<kit>.bi"` suffit : chaque `.bi` porte un `#inclib` vers le relais `/gcc/lib/lib<kit>.a`.
`.bi` présents dans `build/gcc/sdroot/gcc/include/freebasic/kits/` : `uikit`, `netkit`, `audiokit`, `gpukit`, `mediakit`,
`archivekit`, `documentkit`, `gpiokit`, `printerkit`, `emukit`, `buskit` (chacun avec son `_abi.bi`).

- Les fonctions ont les **noms C** des kits (`ui_window`, `ak_note_mhz`, `gp_mode`, `fmt_ip`…) ; tout argument est un
  `longint` 64 bits. Une chaîne : `kit_str( s )` ; un pointeur : `kit_ptr( @x )` (macros de `uikit.bi`).
- UIKit : `dim shared ui as ui_vars` puis `uikit_open( @ui )` (ouvre le kit et fait `ui_init`, rend 0 si les polices
  manquent). `ui.w`, `ui.h`, `ui.surf` (pixels `0x00RRGGBB`), `ui.font_b(0)`… ; un champ d'évènement :
  `ui_ev_field( ui, EV_KIND )`. Les autres kits s'ouvrent par `<kit>_kit_open( )` (ex. `netkit_kit_open( )`).
- Constantes du `.abi`/`.inc` (`WEV_CLOSE`, `EV_KIND`, `TJ_FACE`…) en `const … as longint`.
- Thème : `ui_jetons( )` rend l'adresse des jetons ; une couleur = `*cast( ulong ptr, ui_jetons( ) + TJ_FACE )`
  (même règle qu'en C : pas de couleur en dur ; l'essai `kitui.bas` en a une, ne pas l'imiter).

Appels types :

```basic
ui_window( 420, 200, kit_str( "Titre" ) )                  ' UIKit
ui_button( 24, 100, 160, 40, kit_str( "Fermer" ), 1 )
if( netkit_kit_open( ) ) then fmt_ip( &h0A000202, kit_str( ip ) )   ' NetKit, ip = space( 32 )
```

AudioKit, GpuKit et MediaKit : leurs `.bi` compilent, leurs fonctions **n'ont pas été appelées** depuis FreeBASIC
(docs/GCC.md) ; GpioKit, PrinterKit, EmuKit, BusKit : `.bi` générés, aucun essai documenté (à vérifier).

## Exemple

`tools/gcc/test/basic/kitui.bas` (essayé sous QEMU et sur le Pi), réduit à UIKit :

```basic
#include once "kits/uikit.bi"

dim shared ui as ui_vars
if( uikit_open( @ui ) = 0 ) then
	print "UIKit absent"
	end 1
end if

ui_window( 420, 200, kit_str( "UIKit depuis BASIC" ) )
dim as ulong face = *cast( ulong ptr, ui_jetons( ) + TJ_FACE )
ui_fill( 0, 0, ui.w, ui.h, face )
ui_text( kit_str( "Bonjour depuis FreeBASIC" ), 24, 60, &h202830, 20, kit_ptr( @ui.font_b(0) ), 400 )
ui_button( 24, 100, 160, 40, kit_str( "Fermer" ), 1 )
ui_update_all( )

do
	var t = ui_event( 100 )
	if( t = WEV_CLOSE ) then exit do
	if( (t = WEV_MOUSE) andalso (ui_ev_field( ui, EV_KIND ) = 1) ) then exit do
	if( t = WEV_KEY ) then exit do
loop
```

(La lecture du jeton `TJ_FACE` remplace le `&hF0F2F5` de l'original ; la couleur du texte reste en dur comme dans
l'essai — prendre plutôt le jeton du texte, voir `korin-kit-uikit`. Forme exacte de la lecture : à vérifier à la
compilation.) Pour gfxlib2 : `tools/gcc/test/basic/gfx.bas` (`screen 13` ou `screenres 640, 480, 32`).

## Pièges

- **FreeBASIC ne distingue pas les majuscules** : une constante du même nom qu'une fonction du kit n'est pas écrite dans
  le `.bi` (le script le signale).
- Fonction dont le commentaire `.abi` renvoie à une autre (« comme », « idem ») : déclarée avec 8 arguments facultatifs,
  le nombre réel ne se lit pas.
- `uikit.bi` : **une seule fenêtre par programme** ; après `ui_fill`/dessin dans `ui.surf`, appeler `ui_update` ou
  `ui_update_all` pour montrer.
- Console : le terminal de Korin ne connaît pas les séquences d'échappement — `CLS`, `LOCATE`, `COLOR` sans effet en mode
  texte, `INKEY` seulement en mode graphique ; `PRINT`/`INPUT` passent par stdout/stdin.
- gfxlib2 : `SETMOUSE` sans effet, pas de double-clic dans `SCREENEVENT`, pas d'OpenGL, pas de plein écran. Non essayés :
  `SCREEN 1, 2, 7, 9, 11, 12`, modes 15/16/24 bits, mode plus grand que l'écran, tactile, clavier/souris physiques.
- `.fbctinf` désactivé : le `#inclib` d'une bibliothèque `.a` faite par `fbc` est **à répéter dans le programme**.
- `pibuild` : `fbc` ne garde les `#inclib` que d'une passe à l'autre, donc un `.bas` ou un `.bi` changé refait tout.
- Pas de `fork` (`EXEC` passe par `posix_spawnp`) ; `SHELL` coupe aux espaces, sans `sh` ni guillemets ; pas de
  `ThreadCall` (libffi absente) ; `EXEPATH` par `__posix_exe_path`.
- Chemins : `VOL:/...` compte comme absolu, comme `/...`.
