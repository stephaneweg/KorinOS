---
name: armpi-qt
description: "Qt 6.10.2 sur Korin PC (24/11) : qtbase et Qt Quick croisés, QPA korin sur UIKit (barre de menus globale), style korin, essayés sous QEMU ; paquets <module>-dev 6.10.2 publiés le 25/11 ; pour MuseScore ensuite"
metadata:
  type: project
  modified: 2026-11-24
---

Demande de l'utilisateur du 24/11 : porter Qt (qtbase, Qt Quick) avec une QPA `korin` sur UIKit, puis MuseScore Studio
4.7 (interface QML). Étude et état : docs/etude/QT.md (§ 0).

**Décisions de l'utilisateur (24/11)** : porter Qt plutôt que réécrire l'interface de MuseScore ; son de MuseScore par
**AudioKit** (pilote dans sa couche audio) ; menus par la **barre de menus globale** (QPlatformMenuBar de la QPA) ;
dépendances = paquets **<nom>-dev** (.a + .h), et **Qt livré en paquets <module>-dev** installables (réutilisable) ;
**look d'UIKit** (style Qt korin, pas seulement la palette).

**Fait (24/11, branche claude/vigilant-brahmagupta-lhridm, cloud)** : tools/qt (env, fetch, host, deps, build,
mkpatch, test, stage, pctest, korin.cmake, patches/qtbase.patch, qpa/, style/, test/). qtbase statique pour Korin
(QT_FORCE_BUILD_TOOLS, /devtools/qt en staging), zlib/png/brotli/freetype/harfbuzz/openssl des .opk -dev publiés.
Sous QEMU PC : qtcore_test 16/16 ; qtwidgets_test : fenêtre, style korin, clic, saisie, liste déroulante, menus dans la
barre globale (choix, grisé, raccourci). libposix : getgrgid_r, resolv.h, IPV6_PKTINFO… (pour Qt).

**Pièges** : Q_OS_KORIN par __armpi__ (pas __linux__), mkspec korin-g++ ; KORIN_CPU x86-64-v3 (le -mno-avx512f de Jet
casse le test x86intrin) ; FindWrapSystemFreetype ne liait pas brotli/png/z ; **winId() non nul dès la construction**
(sinon les viewports des zones défilantes jamais recopiés) ; popups WF_NOFOCUS (sinon Qt les ferme) ; chmod +x des
binaires sur la FAT ; shell de Korin sans & ni VAR=x : --qpadebug, --selftest, watch: de pctest.

**Qt Quick fait (24/11, QEMU PC)** : qtshadertools, qtsvg, qtdeclarative croisés (QML sans JIT), qtquick_test
interactif (Controls en Fusion par défaut, QT_QUICK_CONTROLS_STYLE mis par la QPA). Paquets prêts dans
packages-pc.ini (pkgfiles.py, split [bl]*, release = 1), **publiés le 25/11** depuis le cloud (qtbase-dev + data-1..3,
qtshadertools-dev, qtsvg-dev, qtdeclarative-dev + data-1..10, parties en 1.0.0), avec freetype-dev 2.14.3-1 (retouche
korin-transform-ppem : seul ftbase.o change) et gcc 1.0.16 (libposix : getgrgid_r, resolv.h, IPv6). Hôte : qtsvg avant qtdeclarative (svgtoqml).

**25/11** : qt5compat, qtnetworkauth, PrintSupport (MuseScore) ; base de polices Korin (alias, replis) ; FreeType 2.14.3 retouché (korin-transform-ppem.patch, deps.sh) ; Qt ne recopie plus une image de glyphe ratée ; raccourcis du texte des menus. Voir [[armpi-musescore]].

**Impression (25/11, QEMU PC)** : plugin printsupport korin (tools/qt/printsupport, recopié par build.sh) : imprimantes
de printd, QPrintDialog -> dialogue de PrinterKit (crochet `qt_korin_print_dialog` faible dans qprintdialog_unix.cpp,
qtbase.patch), moteur vers PrinterKit (texte en contours). L'imprimante « PDF » vers un fichier : le PDF de Qt
(KORIN_PRINT_KIT=1 force printd). printd du PC : `sh tools/pc/apps.sh printd` (absent du sysroot sans PC_APPS ; les
pctest l'ajoutent) ; dans skel/pc/etc/session depuis le 25/11 (paquet pc 2026.11.110). Essai qtprint_test --direct/--kit/--dialog ; sortir un fichier de QEMU : hexdump + parse.

**Reste** : publier les -dev (clé du dépôt) ; construire une app Qt dans Korin ;
glisser-déposer, presse-papiers image, curseurs bitmap ; MuseScore (jalon 5).

**How to apply:** `. ~/korin-cloud/env.sh` ; `sh tools/qt/host.sh`, `deps.sh`, `build.sh`, `test.sh`,
`python3 -I tools/qt/pctest.py prep|run`. Voir [[armpi-blender]], [[armpi-devtools]], [[armpi-paquets]].
