---
name: armpi-retrait-asm-remplace
description: "Demande du 13/11 : retirer l'asm devenu inutile une fois remplacé par le C (noyau, kits, services, commandes), après validation"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-13T00:53:38.737Z
---

Le 13/11/2026, l'utilisateur a demandé de **retirer l'asm qui ne sert plus** dès qu'il est remplacé par du C :
- dans le noyau : PS/2 et moniteur du noyau du PC (après validation de la console), et tout asm du noyau rendu inutile ;
- les **kits** portés en C (AppKit, UIKit, AudioKit… : la version asm) ;
- les **services** portés en C (init, init2, vfs, fatfs, ramfs, imgblk, busd, hwmanager, hidmanager, netd, vterm, telnetd, ftpd, vncd, rdpd…) et les interrupteurs `INIT_C`, `FILES_C`, `APPKIT_C` des modules (le C devient le seul chemin) ;
- par extension les commandes de /bin portées en C.

Ce qui ne sert qu'au Pi reste en asm et n'est pas concerné (gbemu.asm et snesemu.asm (décision du 13/11), sd, wifi, genet, gpio, gt911, son du Pi, v3d, GpuKit, fasmarm, gpu*, dmareach). Voir [[armpi-pas-de-c-pour-pi-seul]].

**Quand :** quand le C remplaçant est validé (sur le vrai Pi pour ce qui tourne sur le Pi). Pas pendant qu'un agent travaille sur les mêmes fichiers.

**Attention :** selfhost et pibuild assemblent l'asm sur le Pi ; sans l'asm, ils doivent construire le C par le GCC de l'OS (vérifier avant de retirer). build.ps1 (`$svc_c`, modules) et les scripts de comparaison asm/C (compare.sh, fs_compare.sh) sont à simplifier en même temps.

Voir [[armpi-x86-pc]], [[armpi-renommage-francais]].

Le noyau du Pi reste en asm jusqu'au chantier « noyau du Pi sur le noyau commun en C », placé tout à la fin du plan (13/11, voir [[armpi-x86-pc]]).
