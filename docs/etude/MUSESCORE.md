# MuseScore Studio sur Korin PC

Chantier ouvert le 24/11/2026 à la demande de l'utilisateur : **MuseScore Studio dans Korin**, sur le port de Qt 6
([QT.md](QT.md)). Décisions de l'utilisateur : porter Qt plutôt que réécrire l'interface (QML) ; le **son par
AudioKit** ; les **menus dans la barre de menus globale** de Korin ; les dépendances en paquets `<nom>-dev`.

## 0. En bref

| Étape | État |
|---|---|
| Qt complété pour MuseScore : qt5compat, qtnetworkauth, PrintSupport ; `lrelease` de l'hôte (qttools) | **fait (24/11)** |
| Compilation croisée de MuseScore 4.7.7 (statique, 2 524 cibles) | **fait (24/11)** |
| Convertisseur sans fenêtre : `main -o grace.pdf Amazing_grace.mscz` | **fait (24/11), QEMU PC** : PDF de 50 Ko en 31 s (sans KVM) |
| Interface : fenêtre, palettes, inspecteur, partition (Leland, Edwin), **menus dans la barre globale**, **en français** | **fait (25/11), QEMU PC** |
| Lecture : **pilote AudioKit**, SoundFont MS Basic, curseur de lecture | **fait (25/11), QEMU PC** (le compteur avance ; le son lui-même n'est pas écouté sous QEMU) |
| App `/apps/musescore.app` (264 Mo), paquet `musescore` (Release) | **prêts**, pas publiés |
| Essai sur le ZBook (vitesse réelle, son) | à faire |

## 1. Construction (`tools/musescore`)

| Script | Rôle |
|---|---|
| `env.sh` | version (`v4.7.7`), dossiers (`~/korin-mscore`) ; reprend `tools/qt/env.sh` |
| `fetch.sh` | clone superficiel du tag (la source tenue sous git) |
| `mkpatch.sh` | `patches/musescore.patch` (git diff de la source) ; les fichiers neufs de Korin (`korin/FILES`) à part |
| `build.sh` | configure, construit, installe dans `~/korin-mscore/x86_64/install` |
| `stage.sh` | l'app : `main` (dépouillé), `share/` (données), traductions compilées, `app.txt`, icône |
| `pctest.py` | disque d'essai : le sysroot, l'app, des partitions de démonstration dans `/home/ms`, premier lancement passé |
| `cmake/FindHarfBuzz.cmake` | `harfbuzz::harfbuzz` depuis le paquet `harfbuzz-dev` (il n'a pas de configuration CMake) |
| `korin/` | pilote audio sur AudioKit, thème de plateforme Korin |

Ordre : Qt pour Korin (`tools/qt/deps.sh`, `host.sh`, `build.sh` avec `qt5compat qtnetworkauth`), puis
`sh tools/musescore/build.sh`, `sh tools/musescore/stage.sh`, `python3 -I tools/musescore/pctest.py prep|run`.
Construction complète : environ 1 h 30 sur 4 cœurs ; une retouche d'un fichier : la liaison (quelques minutes).

**Configuration** (`build.sh`) : `BUILD_CONFIGURATION=APP`, Qt statique (`QT_IS_STATIC`, le chemin de la version web :
`qt_import_qml_plugins`). Coupés : mises à jour, Muse Sounds (MuseSampler, propriétaire), VST et greffons audio,
crashpad, export vidéo (FFmpeg), import MNX (récupéré sur GitHub), Learn, multi-fenêtres, les essais. Gardé : le module
cloud (son remplaçant n'enregistre pas `IMuseScoreComService`, que le projet appelle sans vérifier). FreeType et
HarfBuzz : ceux des paquets `-dev`, comme Qt (`MUE_COMPILE_USE_SYSTEM_*`). LinguistTools coupé : les `.qm` sont faits
par `stage.sh` avec le `lrelease` de l'hôte (MuseScore, instruments, et `qtbase_*` de qttranslations).

## 2. La retouche de Korin (`patches/musescore.patch`)

* **Plateforme** : `OS_IS_KORIN` (CMake, d'après `KORIN` de la chaîne) et `Q_OS_KORIN` (C++) ; la fenêtre de Linux
  (`platform/linux/Main.qml`), qui prend la barre de menus de la plateforme quand `isGlobalMenuAvailable()`.
* **Thème de plateforme** (`korin/korinplatformtheme.*`) : barre de menus globale disponible (les menus passent par
  `Qt.labs.platform` → le `QPlatformMenuBar` de la QPA korin) ; clair ou sombre d'après le thème du bureau.
* **Son** (`korin/korinaudiodriver.*`) : un flux d'AudioKit en 16 bits stéréo, 44,1 ou 48 kHz ; un fil appelle le
  rappel de MuseScore (float), convertit et écrit quand `ak_space` a la place (AudioKit ne réveille personne) ; API par
  défaut « AudioKit ».
* **MIDI** : les ports factices de MuseScore (remis à jour : l'interface avait changé sans eux). Pas de MIDI USB.
* **Données** : `resolveAppDataPath` = `/apps/musescore.app/share`.
* **Langue** : `LANG` d'après `lang` de `/etc/system.ini` (français sans clé, comme UIKit), quand la session n'en a
  pas ou dit `C` : sans elle, QLocale ne voit que « C » et MuseScore reste en anglais.
* **Écran de bienvenue** sauté (ses pages vantent Muse Sounds et MuseHub) ; gestionnaire de plantage de MuseScore
  coupé (le noyau de Korin dit la faute : `addr2line` sur `install/bin/mscore`).
* Divers : `ALLOWED_APIS` (rendu logiciel), polices SMuFL dans les chemins XDG, nom des fils, FluidSynth
  (`portable_endian.h` : `__armpi__`).

## 3. Pièges trouvés

* **FreeType 2.14.3** refuse les glyphes rendus sous une matrice (`FT_Set_Transform`) quand l'image dépasse 10 fois le
  *ppem* (nouvelle limite de `ft_glyphslot_preset_bitmap`, régression amont, vue aussi sous Linux) ; Qt rend le texte
  agrandi exactement ainsi (petite taille × matrice) et recopiait alors un tampon nul (plantage dans
  `QFontEngineFT::loadGlyph`). Retouche `tools/webkit/ports/freetype/korin-transform-ppem.patch` (la limite ne vaut plus
  sous une matrice), appliquée par la recette de `freetype-dev` et par `tools/qt/deps.sh` en attendant sa republication ;
  et Qt ne recopie plus rien quand `FT_Render_Glyph` échoue (`qtbase.patch`).
* Deux HarfBuzz dans le même binaire statique (celui que MuseScore télécharge et celui de Qt) : options système.
* Les raccourcis de MuseScore sont dans le texte des entrées (« Nouveau…⇥Ctrl+N ») : la QPA les sépare et les écrit
  comme Korin (« Ctrl+Maj+S »).
* Polices absentes (« Times New Roman » des vieilles partitions) : la QPA a maintenant des alias et des replis.

## 4. Limites et suite

* **Paroles des partitions de MuseScore 2** (police absente) : quelques lettres encore fausses (« f°und ») après
  la migration vers Edwin ; le texte des partitions récentes est juste. À chercher dans le repli par caractère de
  MuseScore (`QFontProvider::inFont`, `QRawFont`).
* Lent sous QEMU sans KVM (démarrage ≈ 3 min, la lecture n'y tient pas le temps réel) : à mesurer sur le ZBook.
* Pas de MIDI (clavier USB), pas de Muse Sounds, pas de VST ; l'impression passe par PDF (pas de CUPS).
* Le dialogue d'ouverture de fichiers est celui de Qt (pas encore `ui_file_*` d'UIKit).
