---
name: armpi-fichiers-paralleles
description: "22-23/11/2026 : cache des noms dans vfs, index par dossier et 8 fils dans fatfs, BLK_DRIVER_V3 et 8 files NVMe ; dans main et dans pc 2026.11.93 (publié le 23/11, avec le noyau du compteur des pages) ; QEMU seulement, à essayer sur le ZBook"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-23T00:00:00.000Z
---

Demande de l'utilisateur du 22/11 (docs/agents/FICHIERS-PERF.md, points 1, 2, 4, 5) : le volume système FAT32 s'effondrait sous 10 compilations de WebKit. Fait sur `claude/serene-newton-051kkg` (commit 7695b10), détail et mesures dans FICHIERS-PERF.md, « Fait ».

- `vfs` : `ncache.c` (présents / absents par chemin du service ; open en lecture d'un inconnu = appel au nom du programme, `d` = pid, pour apprendre ; présent connu = SVC_FORWARD comme avant).
- `fatfs` : `dirindex.c` (index par répertoire, FAT32 mis à jour en place, exFAT jeté au changement) ; 8 fils (PC), lecteurs en parallèle, verrou relâché pendant `blk_request` en lecture, zone de transfert par fil (`PARTAGE` = cache + 8 zones), `fo->busy`, fermeture qui écrit = refaite en écrivain (`E_RETRY_W`), avis BIRTH sans verrou, FORK et END dans l'ordre (`recv`).
- `blkserv` : `concurrency` (BLK_DRIVER_V3, à la fin de la struct) ; `nvme` : jusqu'à 8 files (`ioqs[]`, `q_take`).
- QEMU sans KVM : 3 000 noms, 200 op/s → 947 (1 process), 4 130 (4), 4 810 (8).

**Why:** la construction de WebKit dans Korin, et tout ce qui fait beaucoup d'open/stat.

**How to apply:** pas encore essayé sur le ZBook : garder les anciens vfs, fatfs, nvme (ils sont dans l'initrd). Piège trouvé : l'index d'un dossier vide n'a pas de cases (`nb = 0`) : tester `!d->nb`. Reste : point 3 (caches proportionnés), lectures aléatoires non cachées ≈ 250 ms sous QEMU (à comprendre). Voir [[armpi-cache-fatfs]], [[armpi-devtools]].
