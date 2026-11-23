# Koton et Arabesque sous Linux

Deux applications de Korin portées sous Linux (essayées sur Fedora 45, x86_64) : **Koton** (« KotonStudio », le
studio de composition, `src/apps/koton`) et **Arabesque** (la composition algorithmique par fractales,
`src/apps/arabesque`). L'interface est en **GTK 4**, le son en **FluidSynth** (sortie PipeWire, PulseAudio ou ALSA).

Les sources des deux apps ne sont **pas copiées** : `make` compile celles de `src/apps/<app>` telles quelles. Ce qui
change, ce sont les deux kits de Korin qu'elles appellent, refaits pour Linux :

| Dossier | Contenu |
|---|---|
| `common/uikit_gtk.cpp` | UIKit (le kit de l'interface de Korin) sur GTK 4 : les ~100 fonctions `ui_*` dont les deux apps se servent, même sémantique (`src/apps/help/data/api/uikit_fr.md`) |
| `common/audiokit_fluid.cpp` | AudioKit (le son) sur FluidSynth : SoundFont, synthés, flux de sortie, export WAV (et MP3 si `libmp3lame.so.0` est là) |
| `common/korin_paths.cpp` | les chemins de Korin (`HOME:/`, `/apps/<app>.app/`...) traduits pour Linux |
| `common/korin_thread.cpp` | les fils de haute priorité (SCHED_FIFO refusé sans droit temps réel : le fil part quand même) |
| `arabesque/linux_main.cpp`, `koton/linux_main.cpp` | l'entrée de chaque programme : les chemins, puis le `main` de Korin, ou un mode sans fenêtre |
| `bin/` | les binaires construits (x86_64, Fedora 45) |

Le moteur musical (composition, formats `.kson`, `.sq`, `.arb`, `.ifs`, `.stl`, exports MIDI, FM Song, WAV) est celui
de Korin, sans changement. Arabesque se compile avec `-ffp-contract=off` comme sous Korin (`tools/office/build.sh`) :
même graine, même `.mid`.

## Dépendances (Fedora)

    sudo dnf install gcc-c++ make pkgconf python3 gtk4-devel fluidsynth-devel openssl-devel fluid-soundfont-gm

À l'exécution : `gtk4`, `fluidsynth-libs`, `openssl-libs`, une SoundFont General MIDI (`fluid-soundfont-gm` pose
`/usr/share/soundfonts/FluidR3_GM.sf2`, prise par défaut). Pour l'export MP3 d'Arabesque : `lame-libs`.

## Construire

    cd linux-ports
    make -j8          # -> bin/arabesque, bin/koton, bin/koton-plugins/<id>/, bin/llm
    make clean        # (efface build/ ; les binaires de bin/ restent)

`make` génère d'abord les en-têtes C des kits depuis leurs `.abi` (`tools/kits/abi2c.py`, comme pour Korin).

## Paquets rpm

    cargo install cargo-generate-rpm --locked      # une fois (écrit le rpm sans rpmbuild ni sudo)
    python3 -I packaging/make-rpms.py              # -> rpm/korin-koton-<v>-<r>.x86_64.rpm, rpm/korin-arabesque-<v>-<r>.x86_64.rpm (r : RELEASE du script)
    sudo dnf install --nogpgcheck rpm/*.rpm        # paquets non signés

Version : celle du paquet PC de Korin (`tools/pkg/versions.ini`, section `[pc]`). Les binaires cherchent leurs données
en remontant de leur dossier : les paquets gardent l'arborescence du dépôt sous `/opt/korin` (`linux-ports/bin`,
`src/apps/<app>/data`, les démos ou préréglages, `src/apps/fmtracker/ins`, `skel/sd0/res/lang`), avec un lanceur
`/usr/bin/<app>`, une entrée `org.korin.<app>.desktop` et l'icône de Korin en PNG (`tools/icons/rendered/<app>.rgba`).

## Lancer

    bin/arabesque [fichier.arb | .ifs | .stl | .sca | .mid | .png | .bmp]
    bin/koton [chanson.kson | .sq]

Les binaires trouvent les fichiers livrés avec les apps dans le dépôt (`src/apps/<app>/data`, les préréglages
d'Arabesque, les démos de Koton) : laissez `bin/` dans `linux-ports/`, ou donnez la racine du dépôt par `KORIN_ROOT`.

Sans fenêtre :

    bin/arabesque --render préréglage.arb sortie.mid [--wav x.wav] [--mp3 x.mp3] [--fms x.fms]
    bin/koton --export-wav chanson.kson sortie.wav

`--render` recompose le morceau d'un préréglage (même chemin que Morceau > Ouvrir un préréglage) et l'exporte ;
`--export-wav` est Fichier > Exporter en WAV de Koton. Les deux acceptent `--soundfont fichier.sf2`.

### Les chemins de Korin

| Korin | Linux |
|---|---|
| `HOME:/` (les chansons, les préréglages, `settings.json`) | `~/Korin/` (ou `$KORIN_HOME`) |
| `/apps/<app>.app/` | `src/apps/<app>/data/` du dépôt |
| `/apps/koton.app/plugins` | `bin/koton-plugins/` |
| `/home/public/arabesque`, `/home/public/koton` | `src/apps/arabesque/presets`, `src/apps/koton/demos` |
| `/res/soundfonts` | `/usr/share/soundfonts` (la SoundFont « GeneralUser-GS » de Koton : la SoundFont par défaut) |
| `/res/lang/fr.txt` | `skel/sd0/res/lang/fr.txt` (+ `src/apps/<app>/lang/fr.txt`) : Koton est en français si `LANG=fr...` |
| `/bin/llm` | `bin/llm` |

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
| `KORIN_SCRIPT` | essai sans les mains : `1500:menu:11;4000:shot:/tmp/a.png;5000:exit` (`menu:<id>`, `key:<hid>[:mods]`, `wset:<widget>:<valeur>`, `shot:` / `shot2:` une capture PNG, `close`, `exit`) ; `KORIN_DEBUG_WIDGETS=1` liste les widgets |

## Ce qui est porté

- **Arabesque** : toute la fenêtre (image et tracé, séquence, grille d'accords, les cinq onglets), les fenêtres de
  l'accord, des réglages de l'image et de la cadence, la composition en direct, la lecture, les exports MIDI, FM
  Song, WAV, MP3 (avec `lame-libs`), les préréglages `.arb`, les images calculées, BMP et PNG ouverts.
- **Koton** : tout le studio (arrangement, éditeurs, mixage, chaîne sonore, générateurs, dialogues), la lecture sur
  FluidSynth (un synthé par piste, comme sous Korin), l'export WAV, les **11 greffons** (des process à part, comme
  sous Korin : `bin/koton-plugins/`), **« Composer avec l'IA »** (`bin/llm`, OpenSSL, certificats du système ; clés
  d'API dans `~/Korin/koton/settings.json`, pas de coffre à secrets sous Linux).

## Différences et limites

- L'interface est celle de Korin, faite de widgets GTK 4 placés au pixel près là où UIKit les met ; les vues dessinées
  par l'app (l'arrangement de Koton, l'image d'Arabesque) le sont par Cairo et Pango. Sur un écran HiDPI, elles sont
  agrandies (un peu floues).
- Pas de menu au nom de l'app ni de menu Aide automatiques (la barre de Korin les ajoute) ; Quitter : la case de
  fermeture, Ctrl+Q.
- Les dialogues sont ceux de GTK (fichiers, couleurs, messages) : boutons en français.
- Les fonctions de UIKit et d'AudioKit dont ces deux apps ne se servent pas ne sont pas refaites (arbres, barres
  d'onglets, surfaces partagées, capture audio, Opus/FLAC...).
- Comme sous Korin : pas d'entrée MIDI matérielle, « Obtenir une SoundFont » de Koton ne télécharge rien.
- Un dossier se lit trié par nom (sous Korin, l'ordre de la carte) : les listes de styles et de tracés gardent leur ordre.
- La lecture et l'export sonnent comme FluidSynth avec la SoundFont choisie, pas comme le synthé d'AudioKit.

## Vérifié

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
