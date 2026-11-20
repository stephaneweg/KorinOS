---
name: armpi-craft
description: "suite Craft de storytold (Rust, egui) sur Korin PC : lanceur Craft, Word/Grid/Deck/CAD/Design construites, reste passé au cloud (20/11/2026)"
metadata:
  type: project
---

20/11/2026 : l'utilisateur veut **toute la suite Craft** (github.com/storytold) sur **Korin PC seulement** (pas le Pi),
ouverte par **un lanceur unique**. Le reste est **passé à une session du cloud** : fiche docs/agents/CRAFT-HANDOVER.md
(priorités, diagnostics, pièges) ; état et fonctionnement : docs/CRAFT.md ; hôte egui et eframe : docs/EGUI.md.

- Fait : hôte egui (`src/lib/egui-korin`), eframe de Korin (`src/lib/eframe-korin`), lanceur **Craft**
  (`src/apps/craft`), catégorie « Craft » cachée (`src/sys/common/applist.c`). Essayés sous QEMU PC : Craft,
  WordCraft (saisie, correcteur), GridCraft ; construites sans essai : DeckCraft, CADCraft, DesignCraft.
- Tout se refait par `bash tools/craft/all.sh` (WSL ou cloud) ; apps et révisions : `tools/craft/apps.txt`.
- rustix 1.1.6 compile pour Korin (`tools/craft/rustix_korin.py` : Korin comme Horizon sauf ce que libposix a), avec
  cap-primitives et fs4 ; libc.patch a gagné openat, fstatat, linkat, symlinkat, fchownat.
- Pièges : une construction à la fois (copies de craft-libs partagées) ; ne pas éditer build.sh pendant qu'il tourne ;
  règles de korin_cfg.py trop larges = doubles définitions ; hdrshot épinglé =0.2.0 (son patch).

Lié : [[armpi-rust]], [[armpi-python-rust-ext]].
