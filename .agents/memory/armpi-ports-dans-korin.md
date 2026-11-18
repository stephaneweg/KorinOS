---
name: armpi-ports-dans-korin
description: "Plan du 18/11 : toutes les libs tierces en paquets (.so + .a, en-têtes, .pc), outils de construction portés, puis tous les ports (SDL, SuperTuxKart, Python, Forge, Mesa, Godot, WebKit…) construits dans Korin sur le ZBook ; docs/etude/PORTS-DANS-KORIN.md, rien de fait"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026) : mettre au plan la mise à disposition de **toutes les bibliothèques utilisées,
en .so et en .a**, pour pouvoir **construire jusqu'à WebKit dans Korin, sur le ZBook**. Plan en 6 étapes :
paquets lib<nom>/<nom>-dev, pkg-config, outils (cmake, ninja, perl, ruby, gperf, unifdef, bison, flex, gn, meson),
libs reconstruites dans Korin, JSC → WebCore → WebKit2 → Jet, recette selfhost. Détail :
docs/etude/PORTS-DANS-KORIN.md. Rien de fait.

**Why:** avance [[armpi-idee-libs-gcc]] (13/11, « quand stable ») et vise un Korin qui se reconstruit lui-même.

**How to apply:** points durs : mémoire de WebCore (le ZBook a 32 Go : assez), FAT32 lent et sans liens (construire
sur ext4 : [[armpi-systeme-fichiers]]), ports de perl et ruby. Voir [[armpi-webkit]], [[armpi-dlopen]], [[armpi-paquets]].

**18/11, précision de l'utilisateur** : WebKit n'était qu'un exemple ; cela vaut pour tous les ports (SDL, SuperTux, SuperTuxKart…). Ordre proposé : SDL → SuperTux → CPython → STK → OCCT/Forge → Mesa → Godot → WebKit/Jet. Outils ajoutés : autotools, m4, scons. Hors d'atteinte : rustc, runtime .NET.
