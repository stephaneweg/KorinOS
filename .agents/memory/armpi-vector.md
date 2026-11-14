---
name: armpi-vector
description: "Vector, éditeur vectoriel natif (Skia, SVG) : dans main le 02/11/2026, QEMU seulement, loin de la parité Inkscape ; pièges d'essai et suite resvg"
metadata:
  node_type: memory
  type: project
  originSessionId: ff4ab670-4ce6-4f68-8a21-b238dcb9644d
  modified: 2026-11-02T09:01:16.100Z
---

Vector (`src/apps/vector`, plan `docs/PLAN-VECTOR-NATIVE.md`, interface `docs/UX-VECTOR.md`) : fusionné dans `main` le 02/11/2026 à la demande de l'utilisateur (d37f45b), environ 12 000 lignes pour 150 000 estimées. Rien sur le Pi. État et manques à jour dans son `README.md`. L'utilisateur a choisi ce plan (« Plan A ») et veut aller jusqu'au bout.

**Why:** éditeur au moins aussi bon qu'Inkscape, écrit pour ARMPI, clean-room : ne jamais lire les sources d'Inkscape, lib2geom, Potrace, inkex.

**How to apply:** suite resvg : `tools/vector/suite.py` (`prep`, `run`, `get`, `cmp`) avec un clone local de linebender/resvg-test-suite ; dernier score 1 055/1 618 sans texte, texte seul 166/355, corrections suivantes non remesurées ; compter 700 s et un seul QEMU à la fois. Essais QEMU : vider `/etc/preload.txt` sur la carte d'essai, sinon `vfs` reste pris 90 s et tout semble figé ; `init2` lance les lignes de startup.txt en parallèle (`--delay`). Touche AZERTY : `key a` de qemu_drive donne Q. Enfants d'une `ui_view` : coordonnées de fenêtre. `ui_popup_local` veut des libellés séparés par NUL. Ne pas copier `build/sd.img` pendant un `build.ps1` d'une autre session. Voir [[armpi-ports-theme]], [[armpi-uikit-fenetres]].
