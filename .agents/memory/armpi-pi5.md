---
name: armpi-pi5
description: "Portage de Korin sur Pi 5 — pas de Pi 5 chez l'utilisateur ; préparé à l'aveugle le 04/11 (sd, xHCI RP1, alpha, DTB, Mesa --v71) ; reste GPU 7.1"
metadata:
  node_type: memory
  type: project
  originSessionId: ed6ed7cb-e887-4f7d-94cf-bb5a2f1b8dbc
  modified: 2026-11-04T16:02:46.026Z
---

L'utilisateur n'a **pas encore de Pi 5** (04/11/2026) : tout le code Pi 5 est écrit à l'aveugle, d'après
C:\Projects\Onyx\docs\PI5-PORT.md (Onyx validé en partie sur un Pi 500) et Circle. État détaillé : docs/PI5.md.

Préparé le 04/11, committé dans main (95fdc177) : sd essaie 0x1000FFF000 ;
usbd prend le 1er xHCI du RP1 (DMA +0x10_0000_0000) ; src/svc/inc/board.inc (board_pi5) fait sortir v3d,
pwmaudio, gpio en code 0 ; framebuffer_ignore_alpha + tag 0x00048007 dans le noyau pi5 ; initramfs dans [all] ;
DTB bcm2712 + overlays dans firmware/ ; gltest --dry --v71 PASS sous QEMU (Mesa contient déjà le v71).
Astuce : dev_map refuse les adresses de l'autre carte → un service peut essayer les deux sans détecter la carte.
Pi 4 vérifié : noyau identique octet pour octet, démarrage QEMU OK.

Corrigé le 04/11 après comparaison à Circle (795e9d91) : RNG200, EL2, pile BSS, pciex4_reset=0, IRQ SD 305, alpha opaque.
Reste : service v3d 7.1 + GpuKit 7.1 (gros), 2e xHCI, MACB, SDIO, son, GPIO RP1.
Banc QEMU : testimage.py pose remote.txt « essai », qemu-run.py le passe par ARMPI_REMOTE_PW.

**Why:** ne pas refaire l'inventaire ; rien n'est vérifiable sur Pi 5 tant qu'il n'en a pas.
**How to apply:** tout changement Pi 5 garde le Pi 4 intact (noyau comparé, boot QEMU) ; voir [[armpi-projet]].
