---
name: armpi-idee-mount-dossier
description: "Idée pour plus tard : VFS monte un device dans un dossier, comme mount de Linux, et non comme volume"
metadata:
  node_type: memory
  type: project
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-11-01T01:32:07.185Z
---

Idée de l'utilisateur, notée le 01/11/2026, **pour plus tard** (pas à faire maintenant) : permettre à VFS de monter un device non pas en volume (SD1:, USB0:) mais dans un dossier, comme `mount` de Linux.

**Why:** demandée à la suite de l'idée UnionFS, même sujet : le rangement des volumes vu des programmes.
**How to apply:** à traiter avec [[armpi-idee-unionfs]] quand on revient sur VFS et les chemins virtuels ([[armpi-gcc-sur-armpi]]).
