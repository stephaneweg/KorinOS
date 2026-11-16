---
name: armpi-idee-cao
description: Plan (fin) : CAO native façon Fusion/Shapr3D, successeur de 3DForge d'Onyx, sur OpenCASCADE + PlaneGCS, interface de 3DForge gardée
metadata:
  type: project
---

Décidé le 13/11/2026 : une app de CAO libre, aussi facile que Fusion ou Shapr3D, native pour Korin (C++ sur UIKit/GlKit, Pi et PC). Successeur de **3DForge** d'Onyx (Onyx/docs/3dforge/README.md, user/apps/3dforge), que l'utilisateur jugeait pas assez bon pour les congés et pas aussi flexible que Fusion : la cause est son moteur Manifold (maillages : congés seulement sur arêtes droites/circulaires, pas de STEP) et l'esquisse sans solveur. On garde son interface validée (clic-déplacer-clic, cotes sur le dessin, timeline, mode Fabrication G-code GRBL pour sa Two Trees) ; moteur **OpenCASCADE**, esquisses **PlaneGCS**.

Pas de port possible de FreeCAD (Qt/PySide partout) ni de Dune 3D (GTK4) ; SolveSpace trop daté.

**Why:** l'utilisateur veut une CAO libre construite en app native pour Korin.

**How to apply:** à la fin du plan de docs/etude/PC-PILOTES.md (commit après dea4dd30), après les bibliothèques partagées ([[armpi-idee-libs-gcc]]) ; d'abord OCCT pour Korin, puis une étude de design avec maquettes validées. Voir [[armpi-x86-pc]].

**Lancé le 14/11** : voir [[armpi-cao]] (moteur porté, étude en attente de validation).
