---
name: armpi-webkit-dans-korin
description: "Plan du 18/11 : toutes les libs tierces en paquets (.so + .a, en-têtes, .pc), outils de construction portés, puis WebKit et Jet construits dans Korin sur le ZBook ; docs/etude/WEBKIT-DANS-KORIN.md, rien de fait"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026) : mettre au plan la mise à disposition de **toutes les bibliothèques utilisées,
en .so et en .a**, pour pouvoir **construire jusqu'à WebKit dans Korin, sur le ZBook**. Plan en 6 étapes :
paquets lib<nom>/<nom>-dev, pkg-config, outils (cmake, ninja, perl, ruby, gperf, unifdef, bison, flex, gn, meson),
libs reconstruites dans Korin, JSC → WebCore → WebKit2 → Jet, recette selfhost. Détail :
docs/etude/WEBKIT-DANS-KORIN.md. Rien de fait.

**Why:** avance [[armpi-idee-libs-gcc]] (13/11, « quand stable ») et vise un Korin qui se reconstruit lui-même.

**How to apply:** points durs : mémoire de WebCore (pas d'échange dans Korin ?), FAT32 lent et sans liens (construire
sur ext4 : [[armpi-systeme-fichiers]]), ports de perl et ruby. Voir [[armpi-webkit]], [[armpi-dlopen]], [[armpi-paquets]].
