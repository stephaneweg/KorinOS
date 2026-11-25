---
name: armpi-musescore
description: "MuseScore Studio 4.7.7 sur Korin PC (24-25/11) : tools/musescore, Qt statique, son par AudioKit, menus dans la barre globale, en français ; converti, affiché et joué sous QEMU PC ; musescore 4.7.7-0.1 publié le 25/11"
metadata:
  type: project
  modified: 2026-11-25
---

Demande de l'utilisateur du 24/11 : MuseScore dans Korin, sur le port de Qt ([[armpi-qt]]) ; son par **AudioKit**,
menus par la **barre de menus globale**. Étude et état : docs/etude/MUSESCORE.md.

**Fait (branche claude/vigilant-brahmagupta-lhridm, cloud)** : tools/musescore (env, fetch, mkpatch, build, stage,
pctest, cmake/FindHarfBuzz.cmake, korin/ : pilote AudioKit, thème de plateforme), patches/musescore.patch (22 fichiers).
Sous QEMU PC : convertisseur (PDF), interface en français, partition (Leland, Edwin), menus dans la barre globale,
lecture (curseur et compteur avancent par le pilote AudioKit). App /apps/musescore.app (264 Mo), paquet
[musescore] release = 1, **publié le 25/11** (4.7.7-0.1, catégorie Musique) : share/ dans musescore-data-1 et -2
(split 90 : l'archive entière, 124,6 Mo, dépassait les 100 Mo d'une branche de transit).

**Pièges** : FreeType 2.14.3 (régression amont : limite 10 x ppem sous FT_Set_Transform) -> korin-transform-ppem.patch
(recette freetype-dev + tools/qt/deps.sh ; freetype-dev à republier) ; deux HarfBuzz si MuseScore télécharge le sien
(MUE_COMPILE_USE_SYSTEM_*) ; cloud gardé (stub incomplet) ; ports MIDI factices remis à jour ; LANG=C de la session ->
korin_set_lang ; écran de bienvenue sauté ; raccourcis dans le texte des menus (QPA) ; HOME=/home sous telnet,
premier lancement passé par ~/.config/MuseScore/MuseScore4.ini.

**Retours du ZBook (25/11)** : partitions affichées ; l'engrenage de lecture ouvrait un popup qui disparaissait
(PopupView = Qt::Tool sans cadre : UIKit lui donnait le focus, la fenêtre principale le reprenait, PopupViewCloseController
le fermait) -> QPA : popup d'UIKit sans focus, focus de Qt à part, marge transparente (WF_ALPHA, format() de la fenêtre) ;
« Play ne marche pas » pas reproduit sous QEMU (clic et espace jouent) : pilote AudioKit passé aux écritures partielles
(ak_write prend ce qui tient) ; si ça reste, lire HOME:/.local/share/MuseScore/MuseScore4/logs/MuseScore_*.log
(« playback not allowed » = sons pas chargés). Publiés : qtbase-dev 6.10.2-1 (--only libqkorin.a : les .a refaits
diffèrent par leurs dates seulement), musescore 4.7.7-0.2. Vu : la fenêtre principale descend de 12 px au premier
clic dans la partition (Elegant ?), pas encore regardé.

**25/11 (suite)** : lecture refusée sur le ZBook = chargement de MS Basic.sf3 par FluidSynth, par piste (« invalid
audio source params » n'est que le premier essai) -> **synthé d'AudioKit** (demande de l'utilisateur : comme Koton) :
korin/korinfluidsynth.inc + korinaudiodriver 44,1 kHz ; MS Basic retirée du paquet (needs soundfonts ; replaces
musescore-data-2) ; menus : stateChanged (Enregistrer grisé). Publiés : musescore 4.7.7-0.3, musescore-data-1 1.0.1.
Essais : tools/musescore/pctest.py --score, tools/python/pctest.py spawn2:, son par --qemu "-audiodev wav,id=snd0,
path=X.wav -device intel-hda -device hda-output,audiodev=snd0" (la chaîne QEMU divise par 4 : sontest 10000 -> 2500).
Barre d'outils « au-dessus » des autres fenêtres : plus reproduit par l'utilisateur.

**Reste** : paroles des partitions MuseScore 2 (police absente, quelques lettres fausses), essai sur le ZBook (vitesse,
son réel), MIDI USB, dialogues de fichiers d'UIKit, publication.

**How to apply:** `. ~/korin-cloud/env.sh` ; Qt d'abord (tools/qt), puis `sh tools/musescore/build.sh`, `stage.sh`,
`python3 -I tools/musescore/pctest.py prep` et `run "chmod +x /apps/musescore.app/main" "spawn:/apps/musescore.app/main
/home/ms/Dawn.mscx" "sleep:220" "shot:ms"`. Une faute : addr2line sur ~/korin-mscore/x86_64/install/bin/mscore.
