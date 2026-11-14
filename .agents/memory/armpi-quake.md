---
name: armpi-quake
description: "Quake 1 logiciel (02/11/2026) : moteur GPL d'id dans third_party/quake, quake.app sur EmuKit, asm FASMARM identique au C, LibreQuake ; branche claude/quake-soft-be7182, QEMU seulement"
metadata:
  node_type: memory
  type: project
  originSessionId: a5685b1b-c7ff-4d51-b24d-29ef497d69de
  modified: 2026-11-02T22:30:27.430Z
---

**Retiré de Korin le 5/11/2026** (demande de l utilisateur : ne répondait pas à ses attentes) ; app, tools/quake, third_party/quake et docs supprimés. Avant : dans main (fusion 1071ab3, 02/11/2026). Déposé sur le Pi par FTP, CRC vérifiés : quake.app, SD0:/quake/id1, emukit.so, uikit.so, sys/elegant (anciens en .old). Pi pas redémarré à ce stade, jeu pas lancé dessus. Doc : docs/QUAKE.md. 320 x 200 par défaut jusqu'à la mesure de vitesse sur le Pi.

Ajouts système réutilisables : `emu_game` / `emu_pointer` (EmuKit : un jeu sans ROM, toutes les touches à l'app), `WS_OP_POINTER_LOCK` et `ui_pointer_lock` (souris relative, genre `WEV_M_REL`).

**Why:** port tiers demandé par l'utilisateur, asm partout où c'est faisable. Voir [[armpi-emulateurs]], [[armpi-pas-de-pont-onyx]].

**How to apply:** FASMARM sait faire un objet liable par ld : `format ELF64`, `public`, `extrn` ; `tools/quake/fixreloc.py` renumérote les relocations (1 -> R_AARCH64_ABS64). Comparer par `tools/quake/check.sh`, et vérifier le contrôle par `mutate.sh` : une mutation « ratée » peut être équivalente ou ne pas assembler. Pièges : le tas de Quake reste dans le .bss (décalages sur 32 bits depuis pr_strings) ; un harnais à taille fixe ne voit pas le changement de taille en jeu ; lire le dernier essai du journal avant d'accuser l'asm ; QEMU sous charge : carte > 40 s, image noire et 0 fps sans blocage ; `timedemo` plafonne à 72 images/s dans EmuKit, F12 mesure ; le clavier QEMU passe par l'AZERTY de l'OS ; le Bash de cet outil mange des antislashs dans les heredocs Python : préférer Write ou Edit.
