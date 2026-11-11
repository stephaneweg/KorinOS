---
name: korin-lang-asm
description: Écrire un programme ou une app en assembleur AArch64 pour Korin OS avec FASMARM — en-tête d'app, appels système par la macro sys, utilisation des kits (kit_use, kits_open, kit_data), boucle UIKit, pièges de FASMARM (dw = 32 bits, dd = 64 bits), construction dans l'OS (fasmarm, pibuild) ou sur le PC (build.ps1). À charger pour tout code .asm.
---

# L'assembleur sous Korin (FASMARM)

Le système est écrit ainsi : noyau, services, kits, la plupart des commandes et beaucoup d'apps. Les sources sont sur
la carte, dans `SD0:/src` (sauf `src/extra`), et s'y assemblent.

## Où lire

| Quoi | Où (dans le dépôt ; sur la carte sous `SD0:/src/…`) |
|---|---|
| ABI : appels système, numéros, conventions | `docs/ABI.md`, `src/inc/syscalls.inc` (seule source des numéros) |
| Macros (`sys`, `app_header`, `prologue`, `movl`…) | `src/inc/armpi.inc` |
| Utiliser un kit, écrire un kit | `docs/KITS.md` (« Utiliser un kit dans un programme ») |
| Table d'un kit (ses fonctions, dans l'ordre) | `src/kits/<kit>/<kit>.abi`, et ses constantes dans `<kit>.inc` |
| Une petite app complète | `src/apps/welcome/welcome.asm` (239 lignes : fenêtre, menu, souris, clavier, thème) |
| Une petite commande | `src/bin/hello/hello.asm`, puis `src/bin/<cmd>/` |
| FASMARM | `tools/fasmarm/ReadMe.txt` |

## Structure d'une app

```asm
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'         ; constantes, macros jeton, ui_data
segment readable executable
kit_use '../../kits/appkit/appkit.abi'       ; relais d'AppKit (toujours)
kit_use '../../kits/uikit/uikit.abi'         ; et des kits employés
start:	stp	x0,x1,[sp,-16]!                 ; x0 = la ligne d'arguments
	bl	kits_open                       ; ouvre les kits (message et fin si absent)
	ldp	x0,x1,[sp],16
	adr	x0,ui_win
	bl	ui_init
	mov	x0,-1                           ; x, y : au choix d'Elegant
	mov	x1,-1
	mov	x2,560                          ; w, h
	mov	x3,340
	adr	x4,title
	mov	x5,0                            ; drapeaux WF_*
	bl	ui_window_ex
	tbnz	x0,63,quit                      ; négatif : erreur
.loop:	mov	x0,1000
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	quit
	b	.loop                           ; WEV_MENU, WEV_MOUSE, WEV_KEY : voir welcome.asm
quit:	mov	x0,0
	sys	SYS_EXIT
segment readable writeable
kit_data                                    ; tables d'import des kits
title	db 'Bonjour',0
ui_data                                     ; variables de l'interface
```

Les chemins des `include` sont relatifs au dossier de la source. Placez l'app dans `src/apps/<nom>/<nom>.asm` (sur
la carte : `SD0:/src/apps/<nom>/`) : `../../inc` et `../../kits` y existent.

## Conventions

- **Appel système :** `sys SYS_xxx`, jamais `svc 0` avec un numéro en dur.
  - Le numéro va dans `x8` et les arguments dans `x0`…`x5`. Le résultat revient dans `x0`, **négatif en cas
    d'erreur**.
  - Tous les registres sauf `x0` sont gardés, et les registres FP/NEON ne sont jamais touchés.
  - Avec AppKit importé, `sys` envoie les appels de fichiers (`0x80`–`0x8E`), `SYS_PRINT` et `SYS_FORK` à AppKit, ce
    qui écrase `x16`. Un `svc 0` brut sur ces numéros rend `E_NOSYS`.
- **Appel d'un kit :** `bl <fonction>`, avec les arguments dans `x0`… et le résultat dans `x0`, comme en C. NetKit
  garde tous les registres sauf `x0`.
- **Couleurs du thème :** la macro `jeton wN, TJ_x` (de `uikit.inc`) met la couleur du jeton dans `wN`. Jamais de
  couleur en dur.
- **Fonctions :** `prologue` et `epilogue` sauvent `x29`/`x30` ; une adresse se prend par `adr`.
- **Textes :** UTF-8 octet par octet (`'syst',0xC3,0xA8,'me'`). La traduction passe par `tr.inc` (`bl tr_x4`).

## Pièges de FASMARM

- **Tailles :** `db` 8 bits, `dh` 16, **`dw` 32**, **`dd` 64** ; réservations par `rb`, `rh`, `rw`, `rd`. Un mutex de
  32 bits s'écrit donc `dw 0`.
- **Une constante redéfinie** avec une autre valeur n'est pas signalée par FASMARM. `build.ps1` lance
  `tools/check_constants.py` pour la trouver.
- **Pas de fonction du même nom qu'une fonction d'un kit importé :** le relais porte déjà ce nom.
- **Le code d'un kit n'a aucune adresse absolue :** tout passe par `adr`.
- **Tables `.abi` :** on ajoute à la fin, on ne retire, ne déplace ni ne renomme jamais une entrée.
- **Mots réservés :** FASMARM refuse un symbole qui porte le nom d'un mot réservé (« reserved word used as symbol ») ;
  renommez-le.

## Construire

- **Dans Korin :**
  - `/bin/fasmarm <nom>.asm <sortie>` ;
  - ou `pibuild [app|bin] <dossier> <nom>`, qui assemble `<nom>.asm` s'il existe et installe dans
    `/apps/<nom>.app/main` (avec `app.txt`) ou dans `/bin/<nom>` ;
  - `selfhost` réassemble une série de sources du système pour vérifier la chaîne.
- **Sur le PC :** `.\build.ps1` assemble `src\<catégorie>\<nom>\<nom>.asm`. Une app va dans `sdroot\apps\<nom>.app\main`
  avec son `app.txt`.
