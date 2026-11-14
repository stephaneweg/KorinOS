---
name: armpi-idee-unionfs
description: "Idée pour plus tard : UnionFS, deux volumes vus comme un seul, le plus récent gagne en cas de conflit de noms"
metadata:
  node_type: memory
  type: project
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-11-01T01:31:12.102Z
---

Idée de l'utilisateur, notée le 01/11/2026, **pour plus tard** (pas à faire maintenant) : un UnionFS qui monte deux block devices avec leurs FS et les présente comme un seul volume virtuel. Un nouveau fichier va sur le volume qui a le plus de place libre ; si un même nom existe des deux côtés, on prend le plus récent des deux. Architecture : au montage, VFS parle à UnionFS, et UnionFS parle aux FS des volumes (une couche entre VFS et les FS).

**Why:** cas évident sur la carte : FAT32 4 Go (SD0:) et exFAT UserFS 115 Go (SD1:) vus comme un seul volume.
**How to apply:** à proposer quand on revient sur les chemins virtuels ou la place sur la carte. Voir [[armpi-pocketterm]] et [[armpi-gcc-sur-armpi]] (chemins virtuels).
