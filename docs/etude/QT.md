# Qt 6 sur Korin PC

Chantier ouvert le 24/11/2026 à la demande de l'utilisateur : **porter Qt 6 (qtbase, Qt Quick) avec une plateforme
`korin` sur UIKit**, pour porter ensuite **MuseScore Studio** (son interface est en QML). Ce document est l'étude de
départ et l'état du chantier (§ 0), tenu à jour au fil des jalons comme [BLENDER.md](BLENDER.md).

Décisions de l'utilisateur (24/11) :

* **porter Qt** (qtbase, Qt Quick) plutôt que réécrire l'interface de MuseScore sur UIKit, avec une **QPA `korin`**
  (la plateforme de Qt, l'équivalent de GHOST pour Blender) écrite sur UIKit ;
* le **son** de MuseScore passera par **AudioKit** : sa couche audio s'y adaptera (un pilote audio Korin) ;
* les **menus** passeront par la **barre de menus globale** de Korin : la QPA fournit un `QPlatformMenuBar` ;
* les **dépendances** sont celles des paquets `<nom>-dev` (`.a` et `.h`), et **Qt lui-même est livré en paquets
  `<module>-dev` installables** (`qtbase-dev`, `qtdeclarative-dev`…) dans `/devtools/qt`, pour être réutilisé par
  d'autres ports ;
* le **look** est celui d'UIKit : palette des jetons, et un **style Qt `korin`** qui dessine les contrôles comme UIKit.

## 0. En bref

| Jalon | Quoi | État |
|---|---|---|
| 0 | Sources de Qt 6.10.2, scripts `tools/qt`, Qt pour l'hôte (moc, rcc, qsb, qmlcachegen) | **fait (24/11)** |
| 1 | **qtbase** croisé pour Korin PC (statique) : Core, Gui, Widgets, Network (OpenSSL) ; `qtcore_test` sous QEMU | **fait (24/11), QEMU PC** : les 16 essais de `qtcore_test` passent (fichiers, regex, Unicode, JSON, fils, minuteries, réveil entre fils, `QProcess`) |
| 2 | **QPA `korin`** sur UIKit : fenêtres, rendu raster, entrées, presse-papier, thème, barre de menus globale ; style `korin` ; `qtwidgets_test` sous QEMU | **fait (24/11), QEMU PC** : fenêtre à bande de titre, style korin (Givre), clic, saisie, liste déroulante (popup), **menus dans la barre globale** (choix, grisé, raccourcis) |
| 3 | **Qt Quick** (qtshadertools, qtdeclarative, qtsvg) en rendu logiciel ; `qtquick_test` sous QEMU | **fait (24/11), QEMU PC** : QML en interpréteur (sans JIT), Qt Quick logiciel, Qt Quick Controls (Fusion, aux couleurs du thème), animation ; curseur glissé, bouton, liste déroulante QML |
| 4 | Paquets `qtbase-dev`, `qtshadertools-dev`, `qtdeclarative-dev`, `qtsvg-dev` (`/devtools/qt`), essai sur le ZBook | **prêts (24/11)** : `stage.sh`, motifs de `pkgfiles.py` dans `packages-pc.ini`, `pub.py --dry-run` de `qtbase-dev` bon ; **pas publiés** (clé du dépôt) ; construire une app Qt *dans* Korin pas encore essayé |
| 5 | **MuseScore Studio** 4.7 : convertisseur sans écran, puis l'interface, son par AudioKit | **fait (25/11), QEMU PC** : [MUSESCORE.md](MUSESCORE.md) |

**Cible : Korin PC (x86-64, le ZBook) d'abord**, comme Blender ; MuseScore demande 8 Go de mémoire. Le Pi pourra suivre
pour Qt lui-même (tout est en C++ portable, la QPA ne dépend que d'UIKit).

## 1. La version

**Qt 6.10.2**, celle de MuseScore Studio 4.7 (ses notes de version : « Qt updated to version 6.10.2 »). Modules :
`qtbase`, `qtshadertools` (l'outil `qsb`, demandé par Qt Quick même en rendu logiciel), `qtdeclarative` (QML, Qt
Quick, Qt Quick Controls), `qtsvg`. MuseScore demandera en plus `qt5compat` et peut-être `qtnetworkauth` : au jalon 5.

Sources : `download.qt.io/official_releases/qt/6.10/6.10.2/submodules`, sommes SHA-256 du site vérifiées par
`tools/qt/fetch.sh`. Licence : LGPL-3 (ou GPL) ; Qt est lié en statique dans les apps : une app liée doit donc être
sous une licence compatible (MuseScore : GPL-3) ou fournir ses objets pour une nouvelle liaison.

## 2. Construction

| Script | Rôle |
|---|---|
| `tools/qt/env.sh` | versions, dossiers (`~/korin-qt`), `JOBS` |
| `tools/qt/fetch.sh` | télécharge, vérifie et dépaquette les modules (`~/korin-qt/src`) |
| `tools/qt/host.sh` | Qt pour la machine de construction (`~/korin-qt/host`) : la compilation croisée en prend les outils (`QT_HOST_PATH`) |
| `tools/qt/korin.cmake` | chaîne CMake (repris de Blender) : `x86_64-onyx-elf-14.2`, `posix.specs`, Tiger Lake sans AVX-512, `-g0` |
| `tools/qt/build.sh` | Qt pour Korin, statique, dans `~/korin-qt/x86_64/qt` ; recopie la QPA ; applique les retouches |
| `tools/qt/mkpatch.sh` | retouches : la source de chaque module est tenue sous git ; `save` en fait `tools/qt/patches/<module>.patch` |
| `tools/qt/test.sh` | les programmes d'essai de `tools/qt/test` liés au Qt de Korin |

Ordre : `sh tools/cloud/setup.sh …` (chaînes), `ARCH=x86_64 sh tools/posix/build.sh`, `sh tools/pc/build.sh`,
`sh tools/pc/apps.sh` (`libuikit.a`), `sh tools/qt/host.sh`, `sh tools/qt/build.sh`, `sh tools/qt/test.sh`.

### 2.1 Korin pour Qt

Le compilateur de Korin définit `__armpi__` et `__unix__`, **pas `__linux__`** (comme pour Blender) :

* `qsystemdetection.h` (retouché) : `__armpi__` donne **`Q_OS_KORIN`**, un Unix générique (`Q_OS_UNIX`), sans les
  chemins propres à Linux (`/proc`, inotify, futex…) ;
* un **mkspec `korin-g++`** (`qplatformdefs.h` d'après celui de Haiku : `unistd.h`, `pthread.h`, sockets… sans
  `sys/ipc.h` ni `sys/shm.h`) ; `QT_QMAKE_TARGET_MKSPEC=korin-g++` ;
* CMake voit un « Linux » (`CMAKE_SYSTEM_NAME Linux`, comme Blender), avec `KORIN=ON` pour les retouches.

Fonctions de Qt coupées (`build.sh`) : OpenGL, Vulkan, EGL, xcb, Wayland, linuxfb, D-Bus, glib, ICU, fontconfig, SQL,
impression, mémoire partagée System V et POSIX, inotify, journald, syslog, backtrace.

**Dépendances** : celles qui ont un paquet `<nom>-dev` publié (`packages/pc/pkgs`) sont prises là, dépaquetées par
`tools/qt/deps.sh` dans `~/korin-qt/x86_64/devsys` (préfixe `/devtools/gcc`, comme dans Korin) : **zlib, libpng,
brotli, FreeType, HarfBuzz, OpenSSL** (`FEATURE_system_*`, OpenSSL lié en direct). Une app Qt construite dans Korin
prendra donc les mêmes paquets. Le reste est embarqué dans qtbase (`Qt6Bundled*.a` : PCRE2, libjpeg,
double-conversion, md4c) et livré dans `qtbase-dev`. `FindWrapSystemFreetype.cmake` (retouché, installé avec Qt) lie
aussi brotlidec, png16 et z quand `libfreetype.a` est statique : `FindFreetype` ne connaît pas ses dépendances privées.

**Processeur** : `-march=x86-64-v3 -mtune=tigerlake` (`KORIN_CPU` dans `env.sh`), pas le `-mno-avx512f` de Jet et
de Blender : le test `x86intrin` de Qt veut compiler ses chemins AVX-512, choisis à l'exécution d'après le processeur.

**Préfixe** : `/devtools/qt` (`CMAKE_INSTALL_PREFIX`), installé dans `~/korin-qt/x86_64/stage/devtools/qt`
(`CMAKE_STAGING_PREFIX`) ; les outils de Qt sont aussi construits **pour Korin** (`QT_FORCE_BUILD_TOOLS`), pour qu'une
app Qt se construise dans l'OS. La QPA se lie à `uikit` par son nom (`-luikit` : `/devtools/gcc/lib` dans Korin).

**libposix** (ajouts demandés par Qt, 24/11) : `getgrgid_r` (une seule identité, comme `getgrnam_r`), `<resolv.h>`
(vide de fonctions : `FEATURE_libresolv` coupé), `IPV6_PKTINFO`, `IPV6_RECVPKTINFO`, `IPV6_HOPLIMIT`,
`IPV6_RECVHOPLIMIT` et `struct in6_pktinfo` dans `<netinet/in.h>` (Korin est en IPv4 : un `setsockopt` les refuse).
Le mkspec ajoute `_POSIX_PIPE_BUF` (absent de newlib) et `<sys/ioctl.h>` ; `qstorageinfo_stub.cpp` remplace la version
Linux (ni `/proc` ni mountinfo).

## 3. La QPA `korin` (`tools/qt/qpa`)

Recopiée dans `qtbase/src/plugins/platforms/korin` par `build.sh` (`if(KORIN)` dans `platforms/CMakeLists.txt`) ;
plateforme par défaut (`QT_QPA_DEFAULT_PLATFORM=korin`), liée d'office dans les apps statiques.

| Fichier | Rôle |
|---|---|
| `korin_ui.{h,c}` | la couche C sur UIKit (`kq_*`), d'après `ghost_korin_ui` de Blender : fenêtres, pixels, événements, curseur, presse-papiers, menus, jetons du thème. L'en-tête généré d'UIKit ne sort pas de ce fichier |
| `qkorinintegration.*` | l'intégration (capacités, fabriques), l'écran (zone utile d'après `ui_screen`), la boucle d'événements, le curseur, le presse-papiers, la traduction des événements |
| `qkorinwindow.*` | les fenêtres et le backing store |
| `qkorintheme.*` | le thème (palette, polices) et la barre de menus globale |

**Fenêtres.** Une fenêtre de premier niveau de Qt est une fenêtre d'UIKit. Les fenêtres à cadre ont en haut une
**bande de titre de 36 px** (`KQ_TOP_BAND`, comme Blender) : Korin n'a pas de barre de titre, les boutons du cadre
d'Elegant sont sur le contenu, et la bande porte le titre aux couleurs du thème ; la géométrie de Qt est le contenu
sous la bande (`frameMargins` la compte), un appui dans la bande reste à Elegant (déplacer la fenêtre). Les menus
déroulants, listes et infobulles de Qt (`Qt::Popup`, `Qt::ToolTip`) sont des fenêtres sans cadre au-dessus de tout
(`WF_FRAMELESS | WF_TOP | WF_NOLIST`), placées là où Qt le veut (`ui_window_place`) ; un dialogue modal prend
`WF_MODAL`. UIKit ne cache pas une fenêtre : `hide` la ferme, `show` la refait.

**Rendu.** Raster : Qt dessine dans un `QImage` (`Format_RGB32`, le format des pixels d'UIKit, `0x00RRGGBB`) ; au
`flush`, la QPA recopie les rectangles changés dans les pixels de la fenêtre (`ui.surf`) sous la bande, et les montre
(`ui_update`). Qt Quick : **rendu logiciel** (`RhiBasedRendering` non annoncé, Qt Quick prend alors le moteur
« software ») ; Vulkan par VkKit pourra venir ensuite (RHI de Qt sur Vulkan, comme Blender).

**Boucle d'événements.** UIKit n'a pas de descripteur pour `poll` (et le `poll` de libposix regarde les descripteurs
sans attendre dans le noyau, `src/lib/posix/poll.c`). Le répartiteur de la QPA prend celui d'Unix de Qt (messages,
minuteries, notificateurs, `poll` sans attente), puis attend dans **`ui_event` par tranches de 5 ms** jusqu'à la
prochaine minuterie de Qt (`timerList.timerWait()`), au plus 50 ms ; un réveil depuis un autre fil (`wakeUp`) n'attend
donc pas plus d'une tranche.

**Entrées.** Clavier : codes HID → `Qt::Key` ; les lettres et les signes suivent la disposition (le caractère
d'UIKit : Ctrl+Z sur la touche Z de l'AZERTY), le reste la position ; le texte est le caractère d'UIKit (AltGr compris,
rien avec Ctrl, Alt ou Méta) ; la relâche reprend la touche de l'appui ; répétition = appui sans relâche ; pavé →
`KeypadModifier` ; AltGr ne donne pas `AltModifier`. Souris : position, trois boutons, molette (120 par cran), entrée
et sortie de fenêtre ; la place des fenêtres est relue à chaque appui et au focus (Elegant les déplace).

**Thème.** La palette vient des **jetons d'UIKit** (`ui_tokens`, jamais de couleur en dur) : `Window` = FACE, `Base` =
SURFACE, `Button` = RAISED, `Highlight` = ACCENT, `HighlightedText` = ACCENT_INK, infobulles SURFACE sur TEXTE1 comme
UIKit, texte grisé TEXTE3… ; relue à `WEV_THEME` (Givre, Onyx Noir à chaud). Style Fusion. Polices : Inter puis DejaVu
Sans, 14 px ; DejaVu Sans Mono ; les fichiers de `/fonts` (FreeType de Qt, `QT_QPA_FONTDIR`).

**Barre de menus globale.** Le thème rend un `QPlatformMenuBar` : un `QMenuBar` de QWidgets (et les barres natives
de Qt Quick) passe alors dans la barre de Korin au lieu de la fenêtre. Les menus sont publiés en texte (`ui_menu`,
format de [EGUI.md](../EGUI.md) : `M<titre>`, `I<id>[d][c]<tab><libellé><tab><raccourci>`, `-`, `S<titre>` … `E`),
regroupés en un envoi par passage dans la boucle, dans deux tampons en alternance (UIKit reconnaît un texte à son
adresse) ; un choix (`WEV_MENU`) émet `activated()` de l'entrée. Les mnémoniques `&` sont retirées, les raccourcis
écrits comme Korin (« Ctrl+Maj+S ») ; un sous-menu de sous-menu est aplati (« Exporter › PDF ») : la barre ne montre
qu'un niveau. Limites : 16 Ko de texte (`WS_MENU_MAX`), 160 lignes par menu ; `aboutToShow` n'est pas émis (la barre
ne dit pas quand un menu s'ouvre) : une app qui remplit un menu à son ouverture doit le tenir à jour d'avance.

**Polices** (25/11) : la base FreeType de la QPA a des alias (Times New Roman → DejaVu Serif, Arial/Helvetica →
DejaVu Sans, Courier → DejaVu Sans Mono…) et des replis selon le style puis Noto Symbols, Math et Emoji. **FreeType
2.14.3** refusait les glyphes rendus sous une matrice (texte agrandi : `Raster_Overflow`) : retouche
`tools/webkit/ports/freetype/korin-transform-ppem.patch`, et `tools/qt/deps.sh` reconstruit `libfreetype.a` retouché
tant que `freetype-dev` n'est pas republié ; Qt ne recopie plus d'image quand le rendu échoue ([MUSESCORE.md](MUSESCORE.md) § 3).
Les raccourcis écrits dans le texte d'une entrée de menu (« Libellé⇥Ctrl+N ») sont montrés à droite.

**Presse-papiers** : texte seulement (`ui_clip_*`). **Curseurs** : formes `UC_*` d'UIKit.

**Popups** : `WF_NOFOCUS` en plus : Elegant ne leur donne pas le clavier (sinon la fenêtre de l'app perdait le focus et
Qt fermait aussitôt ses popups). **`winId()`** est non nul dès la construction (l'adresse de la fenêtre de plateforme) :
QWidget le lit une seule fois et s'en sert pour trouver son parent natif ; avec 0 (la fenêtre d'UIKit n'existe qu'au
premier `show`), les zones défilantes n'étaient jamais recopiées (journal vide, listes déroulantes blanches).

**Style `korin`** (`tools/qt/style`, plugin de style statique, `StyleNames` = korin puis Fusion) : un `QProxyStyle`
sur Fusion qui dessine comme UIKit (`uk_*.c`) : boutons standard, principal (`setDefault`) et fantôme (`setFlat`),
champs à bordure ACCENT au focus, listes déroulantes, nombres « − + », cases et radios, curseur, progression, barres de
défilement fines sans flèches (nuances `ui_tone`), onglets en contrôle segmenté, groupes, menus, infobulles, en-têtes,
sélections ; métriques de `ui_metric` (densité tactile comprise). Les jetons viennent du thème de la QPA (deux
indications propres à Korin, `0x4B520001` les jetons, `0x4B520002` les métriques), sinon de la palette. Pas encore :
QToolButton, QToolBar, QDockWidget, QDial, QCalendarWidget, QSplitter, QToolBox (Fusion, aux couleurs des jetons).

**Qt Quick Controls** : la QPA met `QT_QUICK_CONTROLS_STYLE=Fusion` par défaut (Fusion suit la palette, donc les
jetons d'UIKit ; Basic est gris et sans thème). Un style Qt Quick « korin » (comme celui de QWidgets) pourra suivre.
**QML sans JIT** (`FEATURE_qml_jit=OFF`) : l'assembleur de JavaScriptCore ne connaît pas Korin ; à rouvrir si
MuseScore est lent.

**À faire** : glisser-déposer (`WEV_DROP`), image dans le presse-papiers, curseurs bitmap et curseur caché, pointeur
pris, IME, tablette, molette horizontale, transparence (`WF_ALPHA`), état agrandi dit par Elegant, échelle
(`ui_scale`), services (`openUrl` vers Jet), dialogues de fichiers natifs (`ui_file_*` d'UIKit).

## 4. Essais (`tools/qt/test`, `tools/qt/pctest.py`)

`sh tools/qt/test.sh` construit `qtcore_test`, `qtwidgets_test` (et `qtquick_test` quand Qt Quick est là) ;
`python3 -I tools/qt/pctest.py prep` fait un disque d'essai (le sysroot du PC, les programmes dans `/home/qt`, telnet) ;
`run` lance QEMU q35 et les commandes (`chmod +x` d'abord : la FAT n'a pas de droits ; le shell de Korin n'a ni `&` ni
`VAR=valeur commande` : `qtwidgets_test --qpadebug` met `KORIN_QPA_DEBUG` et `QT_LOGGING_RULES` lui-même ;
`--selftest` ouvre la liste déroulante et écrit ce qu'il voit ; `watch:S:CAPTURE:CMD` recueille la sortie).

## 5. Les paquets `<module>-dev`

`sh tools/qt/stage.sh` range chaque module d'après son `install_manifest.txt` dans `build/pc/devtools/<module>-dev`
(tout sous `devtools/qt`, licences dans `devtools/qt/share/doc/<module>`, outils dépouillés), puis
`python3 tools/pkg/pub.py <module>-dev --new --tree build/pc/devtools/<module>-dev` (avec la clé du dépôt), **dans
l'ordre** `qtbase-dev`, `qtshadertools-dev`, `qtsvg-dev`, `qtdeclarative-dev` (les besoins).

| Paquet | Taille (non compressée) | Besoins |
|---|---|---|
| `qtbase-dev` | 160 Mo (3 608 fichiers) | zlib-dev, libpng-dev, brotli-dev, freetype-dev, harfbuzz-dev, openssl-dev |
| `qtshadertools-dev` | 38 Mo | qtbase-dev |
| `qtsvg-dev` | 2,3 Mo | qtbase-dev |
| `qtdeclarative-dev` | 630 Mo (outils QML liés en statique : 20 à 40 Mo chacun) | qtbase-dev, qtshadertools-dev, qtsvg-dev |

Tous les modules s'installent dans le même `/devtools/qt` : `mkrepo.plan` donne un fichier au **premier** paquet dont
un motif le prend. `tools/qt/pkgfiles.py` écrit donc les motifs des modules (dossiers à eux seuls, sinon fichiers ou
préfixes d'au moins la moitié du nom), placés **avant** `[qtbase-dev]`, qui prend le reste (`devtools/qt/`) ; il
vérifie qu'aucun fichier de qtbase n'est pris. `qtbase-dev` et `qtdeclarative-dev` sont découpés (`split =
devtools/qt/[bl]* 90` : `bin/`, `lib/`, `libexec/` en parties `-data-N`) et vont dans la Release (`release = 1`).
On ne peut pas retirer d'outil du paquet : les fichiers CMake exportés par Qt vérifient que chacun existe.

## 6. MuseScore ensuite (jalon 5)

* **Interface** : QML (appshell, palettes, inspecteur, table de mixage, piano roll) en Qt Quick logiciel ; la vue de
  la partition est un `QQuickPaintedItem` dessiné par `QPainter` (raster, sans GPU).
* **Menus** : sous Linux et Windows, MuseScore dessine sa barre de menus en QML dans la fenêtre ; sous macOS il prend
  la barre native (`QMenuBar` par la plateforme). Sur Korin, on prendra le chemin de macOS : ses menus iront dans la
  barre globale par le `QPlatformMenuBar` de la QPA.
* **Son** : MuseScore a sa propre couche audio (`muse::audio`, `IAudioDriver`) : un **pilote Korin sur AudioKit**
  (flux de sortie d'AudioKit), FluidSynth (embarqué dans MuseScore) et sa SoundFont de base ; ni Muse Sounds
  (propriétaire), ni VST.
* Coupés : MuseScore.com, mises à jour, Muse Hub, VST. Premier essai : le convertisseur sans écran
  (`mscore -o partition.pdf partition.mscz`, QPA `offscreen`).
