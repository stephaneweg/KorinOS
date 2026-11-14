---
name: armpi-cao
description: CAO native (successeur de 3DForge) : OCCT 8.0.1 + PlaneGCS portés (tools/cao), caotest passe sous QEMU PC le 14/11, paquets PC occt-dev/planegcs-dev/caotest publiés le 15/11 ; étude et maquettes en attente de validation, app pas écrite
metadata:
  type: project
---

Chantier lancé le 14/11/2026 (idée : [[armpi-idee-cao]]) ; code poussé sur `claude/x86-64-pc` le 15/11 à la demande de
l'utilisateur (« le code dans la branche x86-64-pc, les paquets dans main »). Document : `docs/DESIGN-CAO.md` (§1-14 étude, §15 moteur).

**Moteur, fait et essayé sous QEMU PC seulement** (pas sur le ZBook ni le Pi) :
- `tools/cao/` : env.sh (OCCT 8.0.1, Eigen 5.0.1, FreeCAD 1.1.4 ; SHA-256 fixés), fetch.sh, korin.cmake (chaîne CMake
  sur libposix), build-occt.sh, build-planegcs.sh, build-test.sh, occt-korin.patch (une ligne : OSD_Path connaît
  `__armpi__`), planegcs/compat (Boost remplacé par un union-find, Base::Console par fprintf), test/caotest.cpp,
  pctest.py (reprend tools/python/pctest.py). Tout est construit sous `~/korin-cao/$ARCH` (WSL), rien de lourd dans le dépôt.
- Rien n'a manqué à libposix. OCCT : 30 .a statiques (268 Mo), STEP/IGES/STL ; TKXCAF tire TKV3d/TKService (sans GL ni FreeType).
- caotest (esquisse PlaneGCS 0 DDL + conflit désigné, boîte, congé droit, union, congé sur arête elliptique, retrait des
  trous, STEP écrit et relu, IGES, STL) : PASSE sous QEMU (TCG, sans KVM) en 6 s ; 119 ms natif Linux sur un i7-11800H.
- aarch64 (Pi) : tout se construit avec ARCH=aarch64, sans changement (OCCT 54 min en -j6) ; **pas lancé** ni sous QEMU
  raspi4b ni sur le Pi ; pas de paquets Pi.
- **Paquets PC publiés le 15/11** (main, packages/pc, 101 paquets) : occt-dev (+ occt-dev-data-1..4 dans la Release,
  pc-occt-dev-*.opk), planegcs-dev, caotest, tous 1.0.0 ; tools/pc/sysroot_cao.sh (après sysroot_gcc.sh) ; publication
  partielle par mkrepo --keep-unbuilt sur un arbre de la seule CAO (build/pc/cao-pkgroot), avec un lanceur qui ne garde
  que ces paquets (sinon [pc] ramasse etc/pkg/abi que mkrepo écrit dans l'arbre). Pas de lib<nom> : statique seulement.

**Étude de design** faite avec un agent designer (demande de l'utilisateur : « l'interface la plus professionnelle
possible ») ; maquettes (13 écrans, Givre/Onyx Noir) : https://claude.ai/artifact/Mudrvp7z3EWPnHAvogwFZc ; générateur
Python resté hors du dépôt (scratchpad de la session). **En attente de validation ; ne pas écrire l'app avant.**
Questions ouvertes (§13 du document) : nom (« Forge » proposé), barre à groupes, souris, menu radial, touches de Fusion,
procédés de fabrication de la v1.

**Pièges :** OCCT en -j12 a fait tomber WSL (7,8 Go) → env.sh plafonne à 6 ; `-DCMAKE_CXX_FLAGS` écrase les options du
fichier de chaîne (passer par `CMAKE_CXX_FLAGS_RELEASE`) ; l'OVMF d'Ubuntu n'a pas le shell UEFI : `sh tools/cloud/setup.sh qemu`
(edk2 de QEMU) puis `. ~/korin-cloud/env.sh` avant pctest ; `wsl sh -c '…$x…'` perd les `$` (passer par un fichier,
`wsl -e sh fichier`, et `MSYS_NO_PATHCONV=1` dans Git Bash).

**Why:** l'utilisateur veut une CAO libre native, aussi facile que Fusion/Shapr3D, qui dépasse les limites de 3DForge.

**How to apply:** attendre la validation des maquettes et les réponses aux questions ; ensuite les étapes du §14 (d'abord
les manques d'UIKit : widgets au-dessus d'une surface GL, surface GL redimensionnable, jetons TJ_HOVER/TJ_AXIS_*).
Voir [[armpi-x86-pc]], [[armpi-idee-libs-gcc]], [[armpi-ports-uikit-os]], [[armpi-ports-theme]].
