---
name: armpi-qt
description: "Qt 6.10.2 sur Korin PC (24/11) : qtbase croisé, QPA korin sur UIKit (barre de menus globale), style korin, essayés sous QEMU ; paquets <module>-dev dans /devtools/qt ; pour MuseScore ensuite"
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

**Reste** : qtshadertools/qtdeclarative/qtsvg croisés + qtquick_test (jalon 3) ; publier les -dev (clé du dépôt) ;
glisser-déposer, presse-papiers image, curseurs bitmap ; MuseScore (jalon 5).

**How to apply:** `. ~/korin-cloud/env.sh` ; `sh tools/qt/host.sh`, `deps.sh`, `build.sh`, `test.sh`,
`python3 -I tools/qt/pctest.py prep|run`. Voir [[armpi-blender]], [[armpi-devtools]], [[armpi-paquets]].
