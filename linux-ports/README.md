# Koton, Arabesque, Budget mensuel, TerraCube et Forge sous Linux

Des applications de Korin portées sous Linux (essayées sur Fedora 45, x86_64) : **Koton** (« KotonStudio », le
studio de composition, `src/apps/koton`), **Arabesque** (la composition algorithmique par fractales,
`src/apps/arabesque`), **Budget mensuel** (`src/apps/budget`, avec la liaison bancaire par Enable Banking),
**TerraCube** (le bac à sable de blocs en 3D, `src/apps/terracube`) et **Forge** (la CAO, `src/apps/forge`).
L'interface est en **GTK 4**, le son en **FluidSynth** (sortie PipeWire, PulseAudio ou ALSA), le réseau sur
**OpenSSL**, le coffre à secrets sur **libsecret** (le trousseau du bureau), la 3D de TerraCube en **OpenGL ES 3**
(EGL : le pilote du GPU, ou Mesa).

Les sources des apps ne sont **pas copiées** : `make` compile celles de `src/apps/<app>` telles quelles. Ce qui
change, ce sont les kits de Korin qu'elles appellent, refaits pour Linux :

| Dossier | Contenu |
|---|---|
| `common/uikit_gtk.cpp` | UIKit (le kit de l'interface de Korin) sur GTK 4 : les ~100 fonctions `ui_*` dont les deux apps se servent, même sémantique (`src/apps/help/data/api/uikit_fr.md`) |
| `common/audiokit_fluid.cpp` | AudioKit (le son) sur FluidSynth : SoundFont, synthés, flux de sortie, export WAV (et MP3 si `libmp3lame.so.0` est là) |
| `common/korin_paths.cpp` | les chemins de Korin (`HOME:/`, `/apps/<app>.app/`...) traduits pour Linux ; Jet (le navigateur de Korin) remplacé par celui du système (`xdg-open`, sinon `gio open`) |
| `common/korin_thread.cpp` | les fils de haute priorité (SCHED_FIFO refusé sans droit temps réel : le fil part quand même) |
| `common/korin_sys.cpp` | les appels système de Korin que les apps font elles-mêmes (`armpi_syscall`) : l'horloge (`SYS_TIME_MS`, `SYS_CLOCK_GET`), le coffre à secrets |
| `common/korin_vault.cpp` | le coffre à secrets de Korin (`src/lib/keyvault/keyvault.h` : `kv_set`, `kv_get`...) sur le Secret Service, par libsecret ; pour Budget, TerraCube et Forge (le jeton du MCP) |
| `common/glkit_egl.cpp` | GlKit (`armgl_*` de `tools/mesa/armgl/armgl.h`) sur EGL : un contexte OpenGL ES 3.0 et des surfaces hors écran (pbuffers, sinon FBO) ; TerraCube relit son image et la recopie dans la fenêtre comme sous Korin, le HUD de UIKit dessiné dedans |
| `common/kits_misc.cpp` | les bouts de NetKit (`net_info` : l'adresse de la machine) et d'ArchiveKit (`ar_deflate_new`, `ar_crc32`... sur zlib : les PNG des captures) dont TerraCube et Forge se servent ; les sockets sont celles de POSIX |
| `common/wayland/*.xml` | deux protocoles de Wayland (pointeur verrouillé, déplacements relatifs), pour la souris prise de TerraCube (`ui_pointer_lock`) |
| `arabesque/`, `koton/`, `budget/`, `terracube/`, `forge/` : `linux_main.cpp` | l'entrée de chaque programme : les chemins, puis le `main` de Korin, ou un mode sans fenêtre |
| `bin/` | les binaires construits (x86_64, Fedora 45) |

Le moteur musical (composition, formats `.kson`, `.sq`, `.arb`, `.ifs`, `.stl`, exports MIDI, FM Song, WAV) est celui
de Korin, sans changement. Arabesque se compile avec `-ffp-contract=off` comme sous Korin (`tools/office/build.sh`) :
même graine, même `.mid`.

## Dépendances (Fedora)

    sudo dnf install gcc-c++ make pkgconf python3 gtk4-devel fluidsynth-devel openssl-devel libsecret-devel fluid-soundfont-gm \
        libglvnd-devel wayland-devel zlib-ng-compat-devel opencascade-devel eigen3-devel boost-devel

Sans `libsecret-devel`, Budget se construit sans coffre à secrets (la session bancaire va alors dans `banque.json` et
la clé reste dans son `.pem`, comme sur un Korin sans coffre ; `bin/budget --vault-status` dit lequel des deux).

À l'exécution : `gtk4`, `openssl-libs` ; pour Koton et Arabesque `fluidsynth-libs` et une SoundFont General MIDI
(`fluid-soundfont-gm` pose `/usr/share/soundfonts/FluidR3_GM.sf2`, prise par défaut), pour l'export MP3 d'Arabesque
`lame-libs` ; pour Budget `libsecret` et un trousseau (GNOME : déjà là), `xdg-utils` pour le navigateur ; pour
TerraCube `libglvnd-egl`, `libglvnd-gles` et un pilote OpenGL ES 3.0 (Mesa ou celui du GPU), `fluidsynth-libs` (la
sortie du son) ; pour Forge `opencascade-foundation`, `opencascade-modeling`, `opencascade-draw` n'est pas utile
(voir plus bas).

## Construire

    cd linux-ports
    make -j8          # -> bin/arabesque, bin/koton, bin/koton-plugins/<id>/, bin/llm, bin/budget, bin/terracube, bin/forge
    make clean        # (efface build/ ; les binaires de bin/ restent)

`make` génère d'abord les en-têtes C des kits depuis leurs `.abi` (`tools/kits/abi2c.py`, comme pour Korin).

## Paquets rpm

    cargo install cargo-generate-rpm --locked      # une fois (écrit le rpm sans rpmbuild ni sudo)
    python3 -I packaging/make-rpms.py              # -> rpm/korin-{koton,arabesque,budget}-<v>-<r>.x86_64.rpm (r : RELEASE du script)
    sudo dnf install --nogpgcheck rpm/*.rpm        # paquets non signés

Version : celle du paquet PC de Korin (`tools/pkg/versions.ini`, section `[pc]`). Les binaires cherchent leurs données
en remontant de leur dossier : les paquets gardent l'arborescence du dépôt sous `/opt/korin` (`linux-ports/bin`,
`src/apps/<app>/data`, les démos ou préréglages, `src/apps/fmtracker/ins`, `skel/sd0/res/lang`), avec un lanceur
`/usr/bin/<app>`, une entrée `org.korin.<app>.desktop` et l'icône de Korin en PNG (`tools/icons/rendered/<app>.rgba`).

## Lancer

    bin/arabesque [fichier.arb | .ifs | .stl | .sca | .mid | .png | .bmp]
    bin/koton [chanson.kson | .sq]
    bin/budget [--today AAAA-MM-JJ] [--month Novembre]

Les binaires trouvent les fichiers livrés avec les apps dans le dépôt (`src/apps/<app>/data`, les préréglages
d'Arabesque, les démos de Koton) : laissez `bin/` dans `linux-ports/`, ou donnez la racine du dépôt par `KORIN_ROOT`.

Sans fenêtre :

    bin/arabesque --render préréglage.arb sortie.mid [--wav x.wav] [--mp3 x.mp3] [--fms x.fms]
    bin/koton --export-wav chanson.kson sortie.wav

`--render` recompose le morceau d'un préréglage (même chemin que Morceau > Ouvrir un préréglage) et l'exporte ;
`--export-wav` est Fichier > Exporter en WAV de Koton. Les deux acceptent `--soundfont fichier.sf2`.

Budget, sans fenêtre : `bin/budget --vault-status` (où vont la clé de la banque et la session), `bin/budget
--vault-forget` (les retire du trousseau ; `banque.json` et le `.pem` restent).

### Les chemins de Korin

| Korin | Linux |
|---|---|
| `HOME:/` (les chansons, les préréglages, `settings.json`, `budget/budget.json`) | `~/Korin/` (ou `$KORIN_HOME`) |
| `/apps/<app>.app/` | `src/apps/<app>/data/` du dépôt |
| `/apps/koton.app/plugins` | `bin/koton-plugins/` |
| `/home/public/arabesque`, `/home/public/koton` | `src/apps/arabesque/presets`, `src/apps/koton/demos` |
| `/res/soundfonts` | `/usr/share/soundfonts` (la SoundFont « GeneralUser-GS » de Koton : la SoundFont par défaut) |
| `/res/lang/fr.txt` | `skel/sd0/res/lang/fr.txt` (+ `src/apps/<app>/lang/fr.txt`) : Koton est en français si `LANG=fr...` |
| `/bin/llm` | `bin/llm` |
| `/apps/jet.app/main` (Jet, le navigateur) | le navigateur du système : `xdg-open <adresse>` (ou `gio open`, ou `$KORIN_BROWSER`) |

### La SoundFont

Par défaut `/usr/share/soundfonts/FluidR3_GM.sf2`. Pour en prendre une autre :
**Koton** : Fichier > SoundFont... (gardée dans `~/Korin/koton/settings.json`, prise au prochain lancement, comme sous
Korin). **Arabesque** : menu **Son > Choisir la SoundFont...** (ajouté par le port ; changée tout de suite, gardée
dans `~/.config/korin-ports/settings.ini`). Pour un lancement : `--soundfont x.sf2` ou `KORIN_SOUNDFONT=x.sf2`.

### Variables d'environnement

| Variable | Effet |
|---|---|
| `KORIN_HOME` | le dossier de `HOME:/` (`~/Korin`) |
| `KORIN_ROOT` | la racine du dépôt (si `bin/` est déplacé) |
| `KORIN_SOUNDFONT` | la SoundFont |
| `KORIN_THEME` | `givre` (clair) ou `noir` ; sinon le schéma de couleurs du bureau |
| `KORIN_AUDIO_DRIVER` | le pilote de FluidSynth essayé d'abord (`pipewire`, puis `pulseaudio`, `alsa`) |
| `KORIN_FLUID_GAIN` | le gain des synthés (0,5) |
| `KORIN_FLUID_VERBOSE` | les messages de FluidSynth et l'état de la sortie |
| `KORIN_BROWSER` | la commande qui ouvre une adresse ou un dossier (à la place de `xdg-open`) |
| `KORIN_GL_VERBOSE` | TerraCube : ce que donne EGL (le fournisseur, le GPU, pbuffers ou FBO) |
| `LIBGL_ALWAYS_SOFTWARE=1` | TerraCube rendu par le processeur (llvmpipe de Mesa, avec `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json` sur une machine NVIDIA) : la distance de vue réduite, comme softpipe sous Korin |
| `KORIN_NO_POINTER_LOCK=1` | TerraCube : la souris prise sans le verrou de Wayland (le repli) |
| `KORIN_SCRIPT` | essai sans les mains : `1500:menu:11;4000:shot:/tmp/a.png;5000:exit` (`menu:<id>`, `key:<hid>[:mods]`, `kdown:<hid>` / `kup:<hid>` une touche tenue, `mrel:dx,dy` la souris prise qui bouge, `click:x,y[,clics[,3 : bouton droit]]`, `wset:<widget>:<valeur>`, `wact:<widget>`, `shot:` une capture PNG ; pour la dernière fenêtre ouverte, un dialogue : `key2:`, `chars2:<texte>`, `click2:`, `press2:<texte d'un bouton>`, `shot2:` ; `dump:` / `dump2:` l'état réel des widgets, les captures pouvant être en retard ; `close`, `exit`) ; `KORIN_DEBUG_WIDGETS=1` liste les widgets |

## Ce qui est porté

- **Arabesque** : toute la fenêtre (image et tracé, séquence, grille d'accords, les cinq onglets), les fenêtres de
  l'accord, des réglages de l'image et de la cadence, la composition en direct, la lecture, les exports MIDI, FM
  Song, WAV, MP3 (avec `lame-libs`), les préréglages `.arb`, les images calculées, BMP et PNG ouverts.
- **Koton** : tout le studio (arrangement, éditeurs, mixage, chaîne sonore, générateurs, dialogues), la lecture sur
  FluidSynth (un synthé par piste, comme sous Korin), l'export WAV, les **11 greffons** (des process à part, comme
  sous Korin : `bin/koton-plugins/`), **« Composer avec l'IA »** (`bin/llm`, OpenSSL, certificats du système ; clés
  d'API dans `~/Korin/koton/settings.json`, pas de coffre à secrets pour Koton sous Linux).
- **Budget mensuel** : toute la fenêtre (les trois onglets, les grilles, le modèle, les dialogues), le même
  `budget.json` que sous Korin et Windows (`~/Korin/budget/`, copies du jour dans `sauvegardes/`), et **la banque** :
  Enable Banking en HTTPS (OpenSSL, certificat vérifié contre les racines du système), la page d'autorisation de la
  banque ouverte dans le **navigateur du système** (bouton « Ouvrir dans le navigateur »), le retour sur
  `http://localhost:…` reçu par Budget comme sous Korin (la fenêtre se ferme toute seule), la synchronisation, la
  fenêtre des opérations à classer avec sa liste déroulante. La clé privée et la session vont dans le **trousseau du
  bureau** (Secret Service, par libsecret : schéma `org.korin.KeyVault`, attributs `app=budget`, `name=bank-key` ou
  `bank-session`, `home=` le dossier de `HOME:/`), comme dans le coffre de Korin ; sans trousseau, comme avant
  (`banque.json` et le `.pem`). Un `banque.json` de l'app Windows ou de Korin sert tel quel (copier aussi le `.pem`).

- **TerraCube** : tout le jeu (créatif et survie, fabrication dessinée dans le jeu, console, météo, liquides,
  circuits de cuivre, créatures), le rendu OpenGL ES 3.0 par le GPU (EGL de libglvnd : NVIDIA, Intel par Mesa, ou
  llvmpipe), le HUD de UIKit par-dessus la 3D (dessiné dans l'image relue, comme sous Korin), le son synthétisé dans
  le jeu (flux d'AudioKit sur la sortie de FluidSynth), le pilotage par **scripts** (port 4730, 127.0.0.1 ; les
  exemples Python sont dans `src/apps/terracube/data/python`) et par un **agent IA** (MCP, port 7312, jeton dans le
  trousseau du bureau, sinon `~/Korin/terracube/mcp.txt`), les mondes dans `~/Korin/terracube/worlds`.
  **La souris prise** (« Cliquez dans la vue pour jouer, Échap rend la souris ») : sous Wayland, le pointeur est
  verrouillé par le compositeur et ses déplacements relatifs lus tels quels (`pointer-constraints` et
  `relative-pointer`, que GNOME, KDE et wlroots ont) ; sans eux (X11), un repli : le curseur caché, les
  déplacements pris entre deux positions du pointeur, qui s'arrête au bord de la fenêtre.

## Différences et limites

- L'interface est celle de Korin, faite de widgets GTK 4 placés au pixel près là où UIKit les met ; les vues dessinées
  par l'app (l'arrangement de Koton, l'image d'Arabesque) le sont par Cairo et Pango. Sur un écran HiDPI, elles sont
  agrandies (un peu floues).
- Pas de menu au nom de l'app ni de menu Aide automatiques (la barre de Korin les ajoute) ; Quitter : la case de
  fermeture, Ctrl+Q.
- Les dialogues sont ceux de GTK (fichiers, couleurs, messages) : boutons en français.
- Budget : la barre d'état et les réglages de la banque disent le chemin de Linux (`~/Korin/budget`) ; quelques
  messages rares (un `banque.json` illisible) écrivent encore « HOME:/budget ». Fichier > Dossier des données ouvre
  `~/Korin/budget` dans le gestionnaire de fichiers.
- Les fonctions de UIKit et d'AudioKit dont ces apps ne se servent pas ne sont pas refaites (arbres, barres
  d'onglets, surfaces partagées, capture audio, Opus/FLAC...).
- Comme sous Korin : pas d'entrée MIDI matérielle, « Obtenir une SoundFont » de Koton ne télécharge rien.
- **Le clavier** : comme sous Korin, une touche a le code HID de sa **position** (`EV_CODE`, d'après le code matériel
  evdev), le caractère tapé suit la disposition : sur un AZERTY, TerraCube marche en ZQSD comme sous Korin. Les
  raccourcis des menus (Ctrl+S, Ctrl+Z...) se lisent par la lettre de la disposition (sous Korin : par la position,
  `uk_misc.c`) ; ce qu'une app lit elle-même dans `EV_CODE` reste la position, comme sous Korin.
- TerraCube : l'image passe par le processeur à chaque image (rendu hors écran, `glReadPixels`, textes, recopie dans
  la fenêtre, puis GTK l'affiche), comme sous Korin ; à 1024 × 640, 58 images/s (le jeu en fait 60 au plus) sur une NVIDIA T1200. Plein écran
  (F9) : une fenêtre sans bord de la taille de l'écran, pas le plein écran de GTK. Les exemples BASIC visent le BASIC
  de Korin.
- Un dossier se lit trié par nom (sous Korin, l'ordre de la carte) : les listes de styles et de tracés gardent leur ordre.
- La lecture et l'export sonnent comme FluidSynth avec la SoundFont choisie, pas comme le synthé d'AudioKit.

## Vérifié

- TerraCube (23/11) : `bin/terracube --selftest` passe (73 vérifications : protocole, MCP dont la capture, console,
  survie, fabrication, circuits, liquides, créatures, sauvegarde et relecture, image rendue) sur le GPU NVIDIA (EGL
  de NVIDIA), sur l'Intel par Mesa (iris) et par le processeur (llvmpipe), avec les trois rendus de GTK
  (`GSK_RENDERER=vulkan`, `ngl`, `cairo`) ; une partie lancée par `KORIN_SCRIPT` (clic dans la vue : la souris prise
  par Wayland, marche avec la touche de position W, capture : la 3D et le HUD) ; un script Python par le port 4730
  (`post`, `fill`, `get_block`) et un client MCP (`initialize`, `tools/list`, `get_block`, `screenshot`). Pas essayé
  à la main : la souris réelle dans la partie, le son à l'oreille. Le selftest relancé sur un monde déjà sauvé attend
  120 s avant de se lancer (un tronçon au bord de la vue jamais maillé : le même sous Korin) : prendre un
  `KORIN_HOME` neuf.

- Les 20 préréglages d'Arabesque (`presets/*.arb` et `exemples/nocturne.arb`) recomposés par `--render` : même `.mid`
  à chaque fois, et le même compilé en `-O0` ; compilé avec FMA (`-march=native -ffp-contract=fast`), « nuit » diffère :
  `-ffp-contract=off` compte. Pas comparé à un `.mid` produit par Korin lui-même (pas de Korin sous la main).
- WAV et MP3 d'Arabesque, WAV de Koton (`petite-valse.kson`) : non silencieux.
- Les greffons de Koton : `plug/kptest.cpp` compilé pour Linux, 107 essais réussis sur 107 (process, mémoire
  partagée, process tué, hôte mort).
- Les deux fenêtres lancées et pilotées par `KORIN_SCRIPT` (captures) : composition et lecture d'Arabesque, ses trois
  fenêtres de plus, lecture d'une démo de Koton, ses dialogues ; la sortie PipeWire joue (crête non nulle).
- Pas essayé à la main : la souris (glisser, molette), le glisser-déposer de fichiers, les dialogues de fichiers et
  l'IA avec une vraie clé.
- Budget (23/11), contre un faux serveur d'Enable Banking (aucune donnée réelle) et avec un `KORIN_HOME` d'essai :
  la fenêtre et les chiffres d'un mois ; Réglages de la banque > Relier mon compte (le « navigateur » : un faux, par
  `KORIN_BROWSER`, qui suit l'adresse jusqu'au retour local) -> relié, la session dans le trousseau et pas dans
  `banque.json` ; Synchroniser (F5) -> solde, deux pages d'opérations, la fenêtre de classement, la liste déroulante
  (clic, clic droit, Alt+Bas, F4, flèches, Début / Fin, Entrée, Échap, clic sur un choix), la saisie avec complétion
  (« me » -> Médical), Appliquer -> paiements des postes fixes, dépenses courantes, règles retenues ; sans Secret
  Service (`DBUS_SESSION_BUS_ADDRESS` faux) : la session dans `banque.json`, comme avant. TLS : certificat vérifié
  contre api.enablebanking.com (401 sans jeton, comme attendu), refusé pour un nom faux. Pas essayé : la vraie
  banque, le vrai navigateur (seulement trouvé : `xdg-open`).
