---
name: armpi-craft
description: "suite Craft de storytold (Rust, egui) sur Korin PC : les 17 apps construites et ouvertes sous QEMU, lanceur Suite ArtCraft, paquets décrits, publication à faire (21/11/2026)"
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

21/11/2026 (session du cloud) : **les 17 apps sont construites et s'ouvrent sous QEMU PC**. Le lanceur s'appelle
**Suite ArtCraft** (Bureautique, seule entrée dans les applications de Korin, demande de l'utilisateur). Paquets
(demande de l'utilisateur) : `craft` (le lanceur et le commun) + **un paquet par app qui dépend de `craft`**
(`tools/pkg/packages-pc.ini`, `release = 1`), vérifiés par `mkrepo.py --no-sign` ; **pas encore publiés** (clé et `gh`
absents du cloud ; republier `pc` d'abord : panel et menubar qui cachent « Craft »).
- Nouveau : `src/lib/winit-korin` (winit sans fenêtre), `tools/craft/egui036.py` (l'hôte en egui 0.36 : Craft
  Anything, AniCraft), patchs mio/socket2/tokio/open/rustix 1.1.5/process-wrap (nix sur libc,
  `tools/craft/crate/process-wrap-korin_nix.rs`), PdfCraft sans aws-lc (pas de RSA privé).
- L'utilisateur veut ensuite (après publication) : **les menus des apps dans la barre de menus de Korin** (backend
  Korin de `craft-menu`), **le titre à gauche contre l'icône** et les contrôles de droite qui ne recouvrent plus les
  boutons de la fenêtre (`craft-window`). Fiche : docs/agents/CRAFT-HANDOVER.md, « Ce qui reste ».
