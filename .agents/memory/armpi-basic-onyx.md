---
name: armpi-basic-onyx
description: "BASIC d'Onyx porté (VM, /bin/basic, QBasic, QBStudio, GPIO Lab, Turtle Quest, GpioKit) ; dans main depuis le 1er/11/2026 ; autre outil que FreeBASIC"
metadata:
  node_type: memory
  type: project
  originSessionId: 9ed91976-a0b2-4ae5-9ef1-99977bb1fbf0
  modified: 2026-10-31T22:28:00.312Z
---

VM dans `src/lib/basic`, runtime `src/apps/basic`, doc `docs/BASIC.md`. L'utilisateur juge le BASIC sur les `.bas` d'Onyx **inchangés** (31/10/2026) : la 3D et les kits doivent marcher comme dans Onyx, d'où `src/apps/basic/baskits.h` (noms d'Onyx `UIKit.window`, `FileKit.*` faits dans le runtime) et `tools/kits/abi2bas.py` (les autres kits).

**Why:** les exemples d'Onyx et le code généré par QBStudio appellent ces noms ; c'est l'interface du langage, pas une couche sous une app.

**How to apply:** GpioKit a le préfixe `gp_` (`gk_` est à GpuKit). `basic -g` fait passer la 3D dans `gk_render` à blanc sous QEMU : le repli CPU cache les refus de GpuKit, l'écran seul ne prouve rien. GpuKit ne découpe pas : `bas3dclip.h`. Sous Terminal `isatty` ment : `SYS_PROC_FLAGS` bit 1. `ui_menu_key` rend 0 sans menu. Clavier invité AZERTY sous `qemu_drive type`. Une commande Bash longue avec heredoc peut être coupée : écrire par l'outil Write. Manettes branchées (pads 0..2 = pilotes, 3 = clavier mappé, connue au premier événement). Reste : GPIO et GPU jamais essayés sur le Pi. Voir [[armpi-freebasic]], [[armpi-ports-sans-pont]].
