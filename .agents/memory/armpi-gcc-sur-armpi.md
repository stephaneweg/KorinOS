---
name: armpi-gcc-sur-armpi
description: "Portage de GCC/G++ sur ARMPI : décisions de l'utilisateur du 30/10/2026 (pile, chemins virtuels, dates, rename, shell, outils, signaux, /gcc/lib)"
metadata:
  node_type: memory
  type: project
  originSessionId: 52ea7130-db86-4847-a36b-ee89fc65ff48
  modified: 2026-10-30T12:55:51.005Z
---

L'utilisateur a validé le 30/10/2026 le portage de GCC et G++ **sur** ARMPI (analyse : aucun blocage de fond, `fork` inutile grâce à `posix_spawn`). L'OS reste en assembleur ; GCC est un programme ordinaire précompilé, hors selfhost, comme WebKit.

Phases : 1) GCC/G++ sans make, essai QEMU (faite) ; 2) mesures sur le Pi (faites le 30/10/2026, résultats dans docs/GCC.md) ; 3) make, shell, outils.

Décisions :
- Pile : **64 Mo** par `USER_STACK_SIZE` dans le noyau, pour tous les programmes (région `VMK_STACK` à la demande).
- Chemins : système **configurable** chemin virtuel = chemin physique (`/gcc`, `/apps`, `/bin`...), pas un cas particulier pour `/gcc`.
- Dates de fichiers : à corriger (fatfs, vfs, ramfs, stat.c).
- `rename` : vrai renommage dans l'entrée FAT, plus de copie.
- Shell : **maison, en assembleur, dans le selfhost** (confirmé le 30/10/2026, jugé faisable) : pas de port de bash.
- Outils Unix : prévus de toute façon, à faire à cette occasion.
- Signaux asynchrones : à faire aussi, utiles pour `present` app → Elegant.
- Kits : couche d'adaptation dans `/gcc/lib` pour que les programmes compilés avec GCC utilisent les kits.

Test d'acceptation du chantier, demandé par l'utilisateur : quand tout sera fait, **Koton Studio doit se construire sur le Pi**.

Quand le chantier sera terminé (pas avant) : mettre toute la chaîne d'outils dans `sdcard/gcc`.

Matériel : le Pi de l'utilisateur a **8 Go** ; Jet se précharge en 6 s (repère de vitesse des fichiers).

**Why:** porter les apps Onyx sans les réécrire en assembleur, en compilant sur l'appareil.
**How to apply:** suivre ces décisions sans les rediscuter ; voir [[armpi-toolchain-onyx]], [[armpi-selfhost-jalons]], [[armpi-feuille-de-route]].
