---
name: armpi-but-libc
description: "But de l'utilisateur pour la libc : une libc.a utilisée par des applications d'ARMPI écrites, compilées et liées par ld dans l'OS"
metadata:
  node_type: memory
  type: project
  originSessionId: 6cbe2b78-9032-4b62-950f-92e82490eae8
  modified: 2026-10-30T17:19:38.179Z
---

But énoncé par l'utilisateur le 30/10/2026 : avoir une `libc.a` (compilée sur le Pi ou non, mais sur le Pi c'est mieux pour le selfhost) utilisée par des applications écrites pour ARMPI, **codées, compilées et liées à la libc avec `ld` dans l'OS**. Une libc partagée en kit n'est donc pas le but.

**Why:** il veut développer sur l'appareil lui-même, sans le PC. Construire `libc.a` sur le Pi est un plus, pas une condition.

**How to apply:** viser l'édition de liens statique sous ARMPI. Le langage de ces applications (C ou assembleur FASMARM) n'était pas encore précisé à cette date : FASMARM ne produit pas d'objets liables (code faux sans erreur). Voir [[armpi-gcc-sur-armpi]], [[armpi-pieges-fasmarm]].
