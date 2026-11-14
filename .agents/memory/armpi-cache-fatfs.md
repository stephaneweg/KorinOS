---
name: armpi-cache-fatfs
description: "Cache de données de fatfs (dans main depuis le 01/11/2026, b5317ea, en service sur le Pi) et mesures qui écartent la lecture à la demande pour l'instant"
metadata:
  node_type: memory
  type: project
  originSessionId: cca7ebb8-6e05-48cd-858b-b93a3299b832
  modified: 2026-11-01T14:01:25.603Z
---

Cache sous le système de fichiers, blocs de 64 Ko, `cc_*` dans `fatfs.asm`, op 18 `FS_CACHE`, commandes `fscache` et `cachetest` : fusionné dans main le 01/11 (b5317ea), en service sur le Pi. QEMU : hello 27 s → 3,7 s. Sur le Pi avec 74f4f09 (utilisateur) : hello 3,3 s → 2,24 s dès le 3e lancement. Ensuite (01/11) limite mémoire corrigée sur la même branche : `SVF_MEMORY`, `SVC_SIG_LOWMEM`, `mem_watch`, `mem_wait` dans le noyau, essayée sous QEMU, puis sur le Pi (voir [[armpi-vitesse-sd]]). Le 1/8 valait 119 Mo car sd0 monte avant la zone haute : revu en marche. `memhog` : essai de pénurie. Note complète dans le dossier de notes de la session local_95ecdb41 (`fichiers-en-memoire-dotnet.md`).

Un hello lit 33 Mo à chaque lancement (hôte 15, CoreLib 17) mais n'en touche que 8, dispersés en 267 grappes de 64 Ko. Avec le plancher actuel d'environ 8,5 ms par lecture de fichier, la lecture à la demande serait plus lente que tout lire.

Revu le 01/11 avec le plancher tombé : 64 Ko coûtent environ 3,7 ms, donc 267 grappes environ 1 s contre 1,65 s pour 33 Mo (estimé, pas mesuré). Le gain possible sur hello reste sous 0,85 s (écart entre 1er et 2e lancement, cache déjà à 123 Mo après le démarrage) et au 1er lancement seulement, face à 2,1 s hors disque : toujours écartée.

**Why:** tranche l'ordre des chantiers : plancher par requête, cache, et lecture à la demande seulement sur mesure, si le premier lancement compte encore.

**How to apply:** ne pas repartir sur la lecture à la demande sans mesure récente du plancher. Décision de l'utilisateur du 01/11/2026 : **cache actif par défaut, plafonné à 256 Mo pour sd0** ; cela complète son choix du 31/10 sur les DLL cochées, voir [[armpi-but-dotnet]]. Tout outil qui écrirait directement sur `blk.sd0pN` rendrait le cache faux. Voir [[armpi-chantier-perf]].
