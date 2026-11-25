---
name: armpi-musescore
description: "MuseScore Studio 4.7.7 sur Korin PC (24-25/11) : tools/musescore, Qt statique, son par AudioKit, menus dans la barre globale, en français ; converti, affiché et joué sous QEMU PC ; paquet musescore prêt, pas publié"
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
[musescore] release = 1 dans packages-pc.ini, version 4.7.7-0.1, pas publié.

**Pièges** : FreeType 2.14.3 (régression amont : limite 10 x ppem sous FT_Set_Transform) -> korin-transform-ppem.patch
(recette freetype-dev + tools/qt/deps.sh ; freetype-dev à republier) ; deux HarfBuzz si MuseScore télécharge le sien
(MUE_COMPILE_USE_SYSTEM_*) ; cloud gardé (stub incomplet) ; ports MIDI factices remis à jour ; LANG=C de la session ->
korin_set_lang ; écran de bienvenue sauté ; raccourcis dans le texte des menus (QPA) ; HOME=/home sous telnet,
premier lancement passé par ~/.config/MuseScore/MuseScore4.ini.

**Reste** : paroles des partitions MuseScore 2 (police absente, quelques lettres fausses), essai sur le ZBook (vitesse,
son réel), MIDI USB, dialogues de fichiers d'UIKit, publication.

**How to apply:** `. ~/korin-cloud/env.sh` ; Qt d'abord (tools/qt), puis `sh tools/musescore/build.sh`, `stage.sh`,
`python3 -I tools/musescore/pctest.py prep` et `run "chmod +x /apps/musescore.app/main" "spawn:/apps/musescore.app/main
/home/ms/Dawn.mscx" "sleep:220" "shot:ms"`. Une faute : addr2line sur ~/korin-mscore/x86_64/install/bin/mscore.
