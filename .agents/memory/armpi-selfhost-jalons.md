---
name: armpi-selfhost-jalons
description: "ARMPI — ne lancer le selfhost (build/selfhost_run.sh, ~10 min) qu'aux jalons importants, pas à chaque étape"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-10-29T02:12:01.363Z
---

Le selfhost (ARMPI se réassemble lui-même dans QEMU, sorties comparées octet par octet à Windows) ne se lance qu'aux **jalons importants** : fin d'un chantier de la feuille de route, changement de l'assembleur FASMARM, ou gros changement du chemin des fichiers (vfs, FatFS, files.asm). Pas à chaque étape ni à chaque commit.

**Why:** demandé par l'utilisateur le 28/10/2026 ; il prend ~10 min et bloque QEMU (lancer un build pendant qu'il tourne fausse son résultat).
**How to apply:** entre deux jalons, valider par l'assemblage et un démarrage QEMU ; ajouter quand même les nouveaux programmes à la liste des jobs ; dire à l'utilisateur quand le prochain selfhost est dû. Voir [[armpi-projet]].

Périmètre (29/10/2026) : le code d'ARMPI et les apps natives d'Onyx réécrites en assembleur sont auto-hébergés ; les programmes tiers portés en C (mbedTLS, WebKit/Jet…) sont des binaires précompilés sur PC (chaîne d'Onyx), hors selfhost.

Prochain selfhost décidé par l'utilisateur (29/10/2026) : après la section 4 (fichiers hors du noyau), pas avant (sauf celui déjà en cours pour la réorganisation des sources).
