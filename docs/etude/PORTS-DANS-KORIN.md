# Plan : construire les ports dans Korin, sur le ZBook

Demandé le 18/11/2026 (d'abord pour WebKit, puis étendu à tous les ports). Rien n'est fait. But : que Korin PC
sache reconstruire lui-même, avec le GCC de `/devtools`, **toutes les bibliothèques tierces qu'il utilise et les
ports qui s'en servent** : WebKit et Jet, SDL 3 et sdl2-compat, SuperTux, SuperTuxKart, CPython, Godot, la CAO
(OCCT, PlaneGCS), Mesa, FFmpeg… En chemin, **toutes ces bibliothèques sont livrées en paquets** (`.so` et `.a`,
en-têtes, `.pc`), pour n'importe quelle app construite dans Korin. Reprend et avance l'idée du 13/11
(`armpi-idee-libs-gcc` : « quand ce sera stable »).

## 1. Ce que les constructions croisées utilisent aujourd'hui (WSL)

| Groupe | Bibliothèques | Utilisées par | Où |
|---|---|---|---|
| C de base | zlib, OpenSSL, SQLite, libxml2, curl, expat, xz, libffi, readline | presque tout ; Python | `tools/webkit/ports`, `tools/python`, `tools/openssl` |
| Texte et images | ICU, libpng, FreeType, HarfBuzz, libjpeg-turbo, libwebp | Jet, SDL, STK, Godot (les siennes) | `tools/webkit/build-deps.sh`, `tools/sdl/build-deps.sh` |
| Dessin | Skia | Jet, Avalonia, Vector | `tools/webkit/ports/skia` |
| Son et média | FFmpeg, OpenAL Soft, libogg, libvorbis, libsamplerate | Jet, Media Player, SuperTux, STK | `tools/mediakit`, `tools/sdl` |
| Jeux | SDL 3, sdl2-compat, PhysicsFS, Squirrel, (Bullet, AngelScript : dans STK) | SuperTux, STK, émulateurs | `tools/sdl` |
| GPU | Mesa (iris, armgl, GlKit), VkKit (anv), ANGLE, shaderc | Jet, Godot, STK, Avalonia | `tools/mesa`, `tools/sdl` |
| CAO | OpenCASCADE, PlaneGCS, Eigen (en-têtes) | Forge | `tools/cao` |
| Python | CPython et ses modules natifs, Tk | Vibe, claude-acp | `tools/python` |
| Système | libposix, newlib, libstdc++, les kits | tout | déjà dans `/devtools/gcc` |

Godot et .NET embarquent la plupart de leurs dépendances (`thirdparty/`) : ils demandent surtout les outils.

**Outils de construction** demandés : **cmake, ninja, pkg-config** (presque tout), **perl** (OpenSSL, WebKit),
**ruby, gperf, unifdef** (WebKit), **gn** (Skia), **meson, bison, flex** et `mako` (Mesa), **scons** (Godot, en
Python), **autoconf/automake/libtool, m4** (les ports en `./configure` : curl, xz, libffi, readline…).
Dans Korin aujourd'hui : python3, bash, git, make, GCC 14 et binutils. Les autres manquent.

Hors d'atteinte pour l'instant : ce qui demande **rustc** (rg, modules Rust de Python : cible Rust croisée
seulement) et le runtime **.NET** lui-même (clang, et construit par `dotnet`).

## 2. Les étapes

| # | Étape | Contenu | Fini quand |
|---|---|---|---|
| 1 | **Paquets des bibliothèques** | pour chacune : `lib<nom>` (les `.so`) et `<nom>-dev` (`.a`, en-têtes, `.pc` ; dépend de `lib<nom>`), décision du 13/11 ; une seule construction croisée partagée par tous les ports ; `.so` avec `-fPIC -shared` sur libposix (liaison au démarrage et `dlopen` existent : `armpi-dlopen`) ; versions dans `tools/pkg/versions.ini` | `gcc -o t t.c $(pkg-config --cflags --libs libcurl)` dans Korin, en statique et en dynamique |
| 2 | **pkg-config** et `/devtools` | `pkgconf` (en C) ; `PKG_CONFIG_PATH` réglé par `/devtools` ; `.pc` réécrits pour les chemins de Korin ; un fichier de chaîne CMake de Korin livré | une app d'essai par bibliothèque se construit et tourne |
| 3 | **Outils de construction** | ports : cmake, ninja, perl, m4, autoconf, automake, libtool, bison, flex, gperf, unifdef, ruby, gn, meson, scons (paquets `devtools-*`) | cmake + ninja et `./configure && make` construisent un projet d'essai ; perl et ruby passent leurs tests de base |
| 4 | **Les bibliothèques reconstruites dans Korin** | les scripts de `tools/*/ports` et `build-deps.sh` lancés dans Korin par bash ; résultat comparé à la construction croisée (mêmes symboles, mêmes tests) | toutes les bibliothèques du §1 se reconstruisent dans Korin |
| 5 | **Les ports, du plus petit au plus gros** | SDL 3 et sdl2-compat → SuperTux → CPython → SuperTuxKart → OCCT et Forge → Mesa → Godot (scons) → JSC, WebCore, WebKit2, Jet | chacun construit dans Korin tourne comme celui de la construction croisée (Jet : Wikipédia, Gmail, YouTube) |
| 6 | **Recettes** | un script par port lancé dans Korin, et une recette qui enchaîne tout ; temps notés. Rejoint l'idée `armpi-idee-emerge` (recettes à la Gentoo) | tout refait d'un bout à l'autre sur le ZBook |

## 3. Points durs

* **Mémoire et temps** : WebCore et OCCT demandent plusieurs Go par tâche (WSL tombe à 12 tâches sur 7 Go). Sur le
  ZBook, compter les tâches sur la RAM libre ; pas de mémoire d'échange dans Korin : à vérifier, et peut-être à
  faire (fichier d'échange) avant les gros ports.
* **Fichiers** : des dizaines de milliers de fichiers et de longs chemins ; FAT32 est lent pour ça et sans liens
  symboliques (des sources en ont : les déplier à l'extraction) : construire sur `/home` en ext4 (journal ext4 au
  plan) ou sur exFAT.
* **perl, ruby, autotools** sont de gros ports, et `./configure` lance des milliers de petits process : la vitesse
  de `fork`/`exec` de Korin comptera.
* Les `.so` tiers : vérifier la place de la TLS, `__dso_handle` et le dérouleur C++ (`armpi-dlopen`).

## 4. Hors de ce plan

* Le Pi : les mêmes paquets pour aarch64 plus tard (même construction, `ARCH=aarch64`) ; les gros ports (WebKit,
  OCCT, Godot) pas dans le Pi (mémoire).
* Construire GCC, rustc ou le runtime .NET dans Korin : non demandé.
