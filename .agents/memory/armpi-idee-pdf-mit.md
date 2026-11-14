---
name: armpi-idee-pdf-mit
description: "Pour plus tard : se passer de MuPDF (AGPL) pour passer le lecteur PDF en MIT ; PDFium ou moteur maison"
metadata:
  node_type: memory
  type: project
  originSessionId: 65543494-9bfa-4f4d-875d-14e44f3daaf8
  modified: 2026-11-03T01:52:44.247Z
---

Idée de l'utilisateur (3/11/2026), pour plus tard : retirer MuPDF de `src/apps/pdf` pour que le lecteur soit sous MIT, comme le reste de Korin OS.

**Why:** MuPDF impose l'AGPL-3.0 à toute l'app ; c'est la seule app à cause de laquelle ce n'est pas du MIT avec Quake (voir `docs/LICENCES.md`).

**How to apply:** pistes à étudier : PDFium (BSD/Apache, gros, s'appuie sur Skia et FreeType déjà présents) ou un moteur maison sur Skia. Le code de `src/apps/pdf/engine.h` isole déjà le moteur. Pas lancé.
