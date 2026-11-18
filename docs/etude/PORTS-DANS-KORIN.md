# Plan : construire jusqu'à WebKit (Jet) dans Korin, sur le ZBook

Demandé le 18/11/2026. Rien n'est fait. But : que Korin PC sache reconstruire lui-même, avec le GCC de
`/devtools`, toutes les bibliothèques tierces qu'il utilise, puis WebKit et Jet. En chemin, **toutes ces
bibliothèques sont livrées en paquets** (`.so` et `.a`, en-têtes, `.pc`), pour n'importe quelle app construite
dans Korin. Reprend et avance l'idée du 13/11 (`armpi-idee-libs-gcc` : « quand ce sera stable »).

## 1. Ce que la construction croisée utilise aujourd'hui (WSL)

| Groupe | Bibliothèques | Où |
|---|---|---|
| C de base | zlib, OpenSSL, SQLite, libxml2, curl | `tools/webkit/build-deps.sh` (`tools/webkit/ports`) |
| Texte et images | ICU, libpng, FreeType, HarfBuzz, libjpeg-turbo, libwebp | idem |
| Dessin | Skia (gn + ninja) | idem |
| Média | FFmpeg (variante `jet`), libvpx, dav1d, Opus selon la configuration | `tools/mediakit/ffmpeg.sh`, `tools/webkit/build-media.sh` |
| GPU | Mesa (iris, armgl, GlKit : `libglkit.a`), ANGLE (dans l'arbre de WebKit) | `tools/mesa` |
| Système | libposix, newlib, libstdc++, les kits (`lib<kit>.a`, `include/kits`) | déjà dans `/devtools/gcc` |

Outils de construction demandés par WebKit (`build-webcore.sh`) : **cmake, ninja, perl, python3, ruby, gperf,
pkg-config, unifdef** ; Skia ajoute **gn** ; Mesa ajoute **meson** (Python), **bison, flex**, `mako` (Python).
Dans Korin aujourd'hui : python3, bash, git, make, GCC 14 et binutils. Les autres manquent.

## 2. Les étapes

| # | Étape | Contenu | Fini quand |
|---|---|---|---|
| 1 | **Paquets des bibliothèques** | pour chacune : `lib<nom>` (les `.so`) et `<nom>-dev` (`.a`, en-têtes, `.pc` ; dépend de `lib<nom>`), décision du 13/11 ; une seule construction croisée partagée par les ports (Jet, Python, Godot, SDL…) ; `.so` faits avec `-fPIC -shared` sur libposix (liaison au démarrage et `dlopen` existent : `armpi-dlopen`) ; versions dans `tools/pkg/versions.ini` | `gcc -o t t.c $(pkg-config --cflags --libs libcurl)` dans Korin, en statique et en dynamique |
| 2 | **pkg-config** et la mise en place de `/devtools` | `pkg-config` (ou `pkgconf`, en C) ; `PKG_CONFIG_PATH` réglé par `/devtools` ; les `.pc` réécrits pour les chemins de Korin | une app d'essai par bibliothèque se construit et tourne |
| 3 | **Outils de construction** | ports : cmake, ninja, perl, ruby, gperf, unifdef, bison, flex, gn, meson (paquets `devtools-*` ou dans `/devtools`) | cmake + ninja construisent un projet d'essai ; perl et ruby passent leurs tests de base |
| 4 | **Les bibliothèques reconstruites dans Korin** | les scripts de `tools/webkit/ports` (et FFmpeg, Mesa) lancés dans Korin par bash ; résultat comparé à la construction croisée (mêmes symboles, mêmes tests) | `build-deps.sh` passe dans Korin |
| 5 | **JSC, puis WebCore, WebKit2, Jet** | `build-jsc.sh`, `build-webcore.sh`, `build-jet.sh` dans Korin ; sources unifiées de WebCore en parallèle limité | un `jet.app` construit dans Korin ouvre Wikipédia, Gmail et YouTube |
| 6 | **Selfhost** | une recette (`tools/webkit/selfhost.sh`) qui enchaîne tout dans Korin ; temps noté | refait d'un bout à l'autre sur le ZBook |

## 3. Points durs

* **Mémoire et temps** : WebCore compilé en sources unifiées demande plusieurs Go par tâche (WSL tombe à 12 tâches
  sur 7 Go). Sur le ZBook, compter les tâches sur la RAM libre ; pas de mémoire d'échange dans Korin : à vérifier,
  et peut-être à faire (fichier d'échange) avant l'étape 5.
* **Fichiers** : des dizaines de milliers de fichiers et de longs chemins ; FAT32 est lent pour ça et sans liens
  symboliques : construire sur `/home` en ext4 (le journal ext4 est au plan), ou sur exFAT.
* **perl et ruby** sont gros à porter (ruby : générateur offlineasm de JSC ; perl : générateurs de WebCore).
* **Skia** se construit par gn ; **Mesa** par meson : deux systèmes de plus.
* Les `.so` tiers : vérifier la place de la TLS, `__dso_handle` et le dérouleur C++ (`armpi-dlopen`).

## 4. Hors de ce plan

* Le Pi : les mêmes paquets pour aarch64 plus tard (même construction, `ARCH=aarch64`), mais pas WebKit dans
  le Pi (mémoire).
* Construire GCC lui-même dans Korin (selfhost complet de la chaîne) : non demandé.
