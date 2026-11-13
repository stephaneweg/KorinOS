---
name: korin-freebasic
description: Écrire, compiler (fbc de /gcc, pibuild) et installer un programme ou une app en FreeBASIC sous Korin OS - console, graphismes gfxlib2 (SCREEN, SCREENRES) ou interface UIKit par les .bi des kits. À utiliser pour tout programme en BASIC (FreeBASIC) sur Korin.
user-invocable: true
---

# FreeBASIC sous Korin OS

**FreeBASIC 1.10.1** est installé dans `/gcc`, à côté de GCC : `fbc` traduit le BASIC en C (`-gen gcc`) que le GCC
de Korin compile. Le résultat est un ELF statique ordinaire, comme un programme C. (Ne pas confondre avec le BASIC
d'Onyx, `/bin/basic` et l'app QBasic, qui est une autre langue, interprétée.) Si `/gcc/bin/fbc` manque :
`pkg add freebasic` (il lui faut `gcc`).

## Compiler

```sh
fbc hello.bas -x /tmp/hello          # sans -x : l'exécutable à côté du source
fbc -O 2 gfx.bas -x /tmp/gfx         # optimisé
fbc -mt prog.bas -x /tmp/prog        # avec threads (runtime multi-thread)
```

Compter quelques secondes sur le Pi, une minute sous QEMU. Un programme de console se lance ensuite dans le shell
(`/tmp/hello`) ; un programme graphique **jamais au premier plan** (il ne rend pas la main) :
`(/tmp/gfx >/dev/null 2>&1 &)`.

**Une app** (dans le lanceur) : le dossier `/sd0/home/projects/<nom>/` avec `main.bas` (ou `<nom>.bas`) et
`app.txt` (même manifeste qu'en C : compétence `korin-app`), puis

```sh
pibuild app /sd0/home/projects/<nom> <nom>        # -> /apps/<nom>.app/main ; « bin » -> /bin/<nom>
```

Tous les `.bas` du dossier et des sous-dossiers sont compilés par `fbc -O 2` en un seul appel (`main.bas` est le
module principal) ; un `.bas` ou un `.bi` changé refait tout le programme. Réussi quand la dernière ligne est
`Installe : /apps/<nom>.app/main`.

## Ce qui marche

* Console : `PRINT`, `INPUT` par la sortie et l'entrée standard. Le terminal de Korin n'interprète pas les
  séquences de FreeBASIC : `CLS`, `LOCATE`, `COLOR` sont **sans effet en mode texte**, `INKEY` ne marche qu'en mode
  graphique.
* Fichiers (`OPEN`, `DIR`, `NAME`, `KILL`, `MKDIR`), threads (`-mt`, `THREADCREATE`, `MUTEX*`, `CONDWAIT`), `EXEC`,
  `SHELL`, dates, `WSTRING`.
* **gfxlib2** (`SCREEN 13`, `SCREENRES 640, 480, 32`, `LINE`, `PSET`, `CIRCLE`, `PAINT`, `PALETTE`, `DRAW STRING`,
  `GETMOUSE`, `MULTIKEY`, `INKEY`) : la fenêtre est une fenêtre d'UIKit avec le cadre du thème. Le plus simple pour un
  jeu ou un dessin. Limites : pas d'OpenGL, pas de plein écran, `SETMOUSE` sans effet, pas de double-clic dans
  `SCREENEVENT`.
* **Les kits** : `#include once "kits/uikit.bi"` (et `netkit.bi`, `audiokit.bi`, `gpukit.bi`, `mediakit.bi`,
  `archivekit.bi`, `documentkit.bi`, `gpiokit.bi`, `printerkit.bi`, `emukit.bi`, `buskit.bi`, dans
  `/gcc/include/freebasic/kits/`). Chaque `.bi` porte son `#inclib` : l'inclure suffit (il lui faut les relais `/gcc/lib/lib<kit>.a` du paquet
  `gcc` ≥ 1.0.10 : s'ils manquent, `pkg update gcc`). Mêmes fonctions qu'en C
  (guide : compétence `korin`, `reference/uikit_en.md`), tous les arguments en `longint` : `kit_str (s)` passe une
  chaîne, `kit_ptr (p)` un pointeur. FreeBASIC ignore la casse : une constante qui porterait le nom d'une fonction
  n'est pas déclarée.

## Le modèle : une app UIKit (widgets, thème)

Pour une vraie app (thème du bureau, widgets, mode poche), UIKit plutôt que gfxlib2. `ui_w_event` ne rend que le
widget (le code `UN_*` arrive dans un second registre que FreeBASIC ne voit pas) : on prend le code par un
**rappel** posé par `ui_w_on`.

```basic
' main.bas -- <ce que fait l'app> (Korin OS, UIKit depuis FreeBASIC)
#include once "kits/uikit.bi"

dim shared ui as ui_vars
dim shared jet as ulong ptr			' les jetons du thème (TJ_* : décalage en octets)
dim shared as longint label, button
dim shared as integer count

function tok (byval tj as longint) as longint
	return jet[tj \ 4] and &hFFFFFF
end function

sub show_count ()
	dim s as string = "Clics : " & count
	ui_w_set_text (label, kit_str (s))
end sub

' rappel d'un widget : appelé par ui_w_event avec le widget et son code UN_*
function on_button cdecl (byval w as longint, byval code as longint) as longint
	if code = UN_ACTIVATE then count += 1 : show_count ()
	return 0
end function

' la mise en page, d'après la taille (aussi après un changement de taille)
function on_resize cdecl (byval w as longint, byval h as longint) as longint
	dim ctl as longint = ui_metric (UMT_CTL)
	ui_fill (0, 0, w, h, tok (TJ_FACE))
	ui_w_move (label, 24, h \ 2 - ctl - 8, w - 48, ctl)
	ui_w_move (button, (w - 160) \ 2, h \ 2 + 8, 160, ctl)
	ui_w_draw (0)
	ui_update_all ()
	return 0
end function

if uikit_open (@ui) = 0 then end 1
jet = cast (ulong ptr, ui_jetons ())
if ui_window_ex (-1, -1, 420, 240, kit_str ("Compteur"), WF_RESIZABLE) <= 0 then end 1
ui_min_size (300, 200)
label = ui_label (0, 0, 10, 10, kit_str (""), ULB_CENTER or ULB_BOLD)
button = ui_pushbutton (0, 0, 10, 10, kit_str ("Cliquer"), UB_DEFAULT)
ui_w_on (button, cast (longint, @on_button), 0)
ui_on_resize (cast (longint, @on_resize), 0)
show_count ()
on_resize (ui.w, ui.h)

do
	dim t as longint = ui_event (200)
	if t = WEV_CLOSE then exit do
	if t > 0 then
		ui_w_event (0)				' l'événement au widget concerné (rappels, taille)
	else
		ui_w_idle ()
	end if
	ui_view_flush ()
loop
```

Les règles des apps de Korin valent ici aussi (compétence `korin-app`) : couleurs par les jetons, coin haut droit
libre (`FRAME_CAP_W`), mise en page qui suit `w` et `h` (mode poche), textes en français, noms en anglais.
Liste exacte des fonctions et constantes : `grep -n -i "ui_xxx" /gcc/include/freebasic/kits/uikit_abi.bi`.

## Si ça ne marche pas

* `as` introuvable : `fbc` le prend dans `/gcc/aarch64-onyx-elf/bin` ; ne pas changer `PATH`.
* Une bibliothèque `.a` faite par `fbc` : son `#inclib` est à répéter dans le programme.
* Une fonction de kit déclarée avec huit arguments facultatifs : son nombre d'arguments ne se lisait pas dans le
  `.abi` ; voir la page du kit dans le guide.
