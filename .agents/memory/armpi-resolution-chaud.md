---
name: armpi-resolution-chaud
description: "Résolution à chaud (31/10/2026) : minuteur dans Elegant, liste non filtrée par l'EDID, branche à valider sur le PocketTerm"
metadata:
  node_type: memory
  type: project
  originSessionId: da3accfc-99f7-4ba0-bcf9-ffe2ca5e25b1
  modified: 2026-10-31T22:12:02.428Z
---

Dans main depuis le 31/10/2026 (be5560b) : validée sous QEMU (QEMU suit vraiment la taille du framebuffer, DMA 2D compris avec `dispdma_qemu`), pas encore sur le Pi. Détail : docs/DESIGN-ELEGANT.md 16.1.

Choix à garder :
- le minuteur de retour est dans Elegant, pas dans le panneau : il joue même panneau mort ou écran noir ;
- la liste n'est jamais filtrée par l'EDID, seulement annotée (natif, écran) : l'utilisateur tourne en 1920x1080 sur un écran 3,5" et le firmware met à l'échelle ; la cacher le priverait de son mode ;
- pas de libération de mémoire DMA : tampon arrière pris une fois pour 2560x1600 ;
- pas de tag « release » au firmware avant le changement : `fb_set` sans release marchait déjà sur le Pi avec `ecran=`.

**How to apply:** sur le Pi, si l'écran reste noir, regarder d'abord s'il faut un release du framebuffer (tag 0x00048001). Essais QEMU : après un changement, l'op `screen LxH` de qemu_drive.py recale les coordonnées (une image démarrée en 640x480 aussi). Un client rdpd de test doit envoyer quelque chose toutes les 12 s (SILENT_MS). Voir [[armpi-pieges-fasmarm]], [[armpi-pocketterm]].
