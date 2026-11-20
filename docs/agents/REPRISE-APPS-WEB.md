# Reprise : apps web (Spotify, Netflix…), cookies de WebKit, cohérence de fatfs

Passation du 20/11/2026 au soir, d'une session locale (Windows + WSL) à une session dans le cloud. Lire d'abord
[CLOUD.md](CLOUD.md) (mise en place de la machine), [AGENTS.md](../../AGENTS.md) et `.agents/memory/MEMORY.md`,
puis `.agents/memory/armpi-musique.md` et `armpi-widevine.md`. Tout ce qui suit est poussé sur `main`.

## 1. Où on en est

### Les apps web (fait, poussé)

Une **coquille d'app web** sur le moteur de Jet : le site dans sa propre fenêtre, sans barre d'adresse ni onglets,
avec son icône, sa session à part et le module Widevine de Jet. Étude et décisions : `docs/etude/MUSIQUE.md` (début).

| Quoi | Où |
|---|---|
| Le mode app de Jet (`/apps/jet.app/main --app=<nom>`) | `src/extra/onyxports/jet/webapp.cpp` ; `engine.h` / `engine_webkit.cpp` : `engine_set_data_name`, `engine_set_helper_path`, `engine_run_script` ; `main.cpp` : l'aiguillage |
| Le lanceur commun (`execv` de Jet sous le nom de l'app, `korin_exec_name` de libposix) | `src/lib/webapp/launcher.c` (le `main.c` de chaque app l'inclut) ; `src/lib/posix/proc.c` |
| Les apps | `src/apps/{spotify,netflix,disneyplus,tf1plus,primevideo}` : `main.c`, `webapp.txt` (title, url, sites, player, maximize), `app.txt`, `help_fr.md`, `help_en.md` |
| Icônes (génériques, jamais les logos) | `tools/icons/art.py` (`video_screen`, `spotify`, `netflix`…), `tools/icons/rendered/*.rgba` |
| Paquets | `tools/pkg/packages-pc.ini` (`needs = jet`), `tools/pkg/versions.ini` |
| Essai sous QEMU PC | `tools/linux/test_webapp.sh` (`APP=`, `PORT=`, `SESSION=1` quitte et relance, `PROBE=1` / `PROBE_JS` montrent les cookies, `URL=` / `URL2=`) |

État : Spotify publié (spotify 1.1.0 avec jet 1.4.1), **essayé par l'utilisateur sur le ZBook** (joue, carte
réduite). Netflix, Disney+, TF1+, Prime Video 1.0.0 : construits, ouverts sous QEMU (sans compte), **pas publiés**
(seulement sur demande de l'utilisateur ; il avait dit d'attendre que Jet soit plus fluide pour les vidéos).

### Les cookies (corrigé, poussé, a59945f4)

Symptôme (l'utilisateur) : on ne restait pas connecté à Spotify après avoir quitté l'app. Cause : `CookieJarDB`
(curl) laissait la requête de `hasCookies` / `hasHttpOnlyCookie` active ; SQLite repoussait le commit de toutes les
écritures, perdues en quittant. Correctif : `tools/webkit/patches/0011-cookies-commit.patch` (appliqué dans les
arbres WebKit de WSL, WebCore reconstruit). Détails : `docs/WEBKIT.md`, « Cookies perdus en quittant ».

**À faire** : la session « Widevine dans Jet » devait reconstruire Jet avec ce patch et publier **jet 1.4.2** (accord
de l'utilisateur). Vérifier dans `packages/pc/index.txt` (`[jet]`, version 1.4.2) ; sinon c'est à refaire (voir § 3),
avec l'accord de l'utilisateur pour publier. Puis lui demander de se reconnecter une fois à Spotify sur le ZBook et
de vérifier qu'il le reste après avoir quitté l'app.

### fatfs : les grosses lectures ne voient pas les écritures d'une poignée ouverte (à faire)

Trouvé en cherchant les cookies, demandé par l'utilisateur dans cette passation. Un process garde un fichier ouvert
et y écrit par petits morceaux (SQLite, `pwrite` de 4 Ko) ; un autre process qui le lit en gros morceaux (`grep -a`
sur le fichier entier) voit **l'ancien contenu**, pendant et même après la mort du premier ; un lecteur par petits
morceaux (SQLite) voit le nouveau. Hypothèse à vérifier : les grosses lectures d'AppKit passent par
`FS_READ_DIRECT` (`fc_rw` dans `src/kits/appkit/appkit.c`), qui lit le disque sans passer par le cache de blocs de
fatfs (`bc_*`, secteurs différés `bc_dirty`, `src/svc/fatfs/fatfs.c` : la version C, celle du PC). Corriger : vider
les secteurs différés du fichier avant une lecture directe, ou servir ces lectures par le cache. Le Pi a sa version
en asm (`fatfs.asm`, cache `cc_*`) : regarder s'il a le même défaut.

Reproduction : `tools/linux/sqltest/` (`sqltest.c` ; `build.sh` le construit avec la chaîne et le `libsqlite3.a` de
Jet ; `run.sh` le lance sous QEMU PC). Résultat du 20/11 :

```
$ grep -a -c holdperm /home/hold.db          (pendant que sqltest … hold garde le fichier ouvert)
0
$ /bin/sqltest /home/hold.db read
  row: holdperm
(30 s plus tard, sqltest sorti sans fermer : même chose, grep 0, SQLite holdperm)
```

C'est fait quand `grep -a` voit les écritures d'un process qui garde le fichier ouvert (et après sa sortie), sous
QEMU PC, et que rien d'autre n'a régressé (démarrage, `run ktest`, essais de fichiers existants).

## 2. Pièges rencontrés

- **Échappements** : un heredoc de Git Bash vers Python transforme `\\n` en vrai retour à la ligne ; écrire les
  fichiers par l'outil d'écriture, ou `chr(92)`. Dans le cloud (bash Linux), moins de risques, mais vérifier.
- **QEMU partagé** : d'autres sessions lancent QEMU ; un port pris (`Failed to find an available port`) : `PORT=`.
- **`/tmp` de WSL** : le FFmpeg de Jet (`/tmp/korin-ffmpeg-jet-x86_64`) disparaît au redémarrage de WSL :
  `ARCH=x86_64 sh tools/webkit/build-media.sh` avant `build-jet.sh`.
- `build-jet.sh` refait `libuikit_c.a` quand `uikit.abi` est plus récent (le mode app appelle `ui_window_place`,
  `ui_window_top`, `ui_maximize`).
- Le mode app passe `WEV_RESIZE` à UIKit (`ui_w_event`) : sans cela ni les bords ni le bouton agrandir ne suivent.
- Jet ignore Ctrl+Q ; une app web le prend (quitter).

## 3. Dans le cloud

- WebKit n'y est pas construit : `sh tools/cloud/setup.sh webkit` (sources), puis la construction de WebKit pour le
  PC (`tools/mesa/build-webkit.sh` avec `ARCH=x86_64`, plusieurs heures, voir `docs/CONSTRUIRE.md` et
  `.agents/memory/armpi-webkit.md`), en appliquant `tools/webkit/patches/*.patch` (dont 0011). Pour le seul bug de
  fatfs, WebKit n'est pas nécessaire : `tools/pc/build.sh`, le sysroot du PC, puis `tools/linux/sqltest` — mais
  `sqltest` se lie au `libsqlite3.a` du sysroot de Jet (`~/korin-webkit-x86_64/sysroot`) : sans lui, le construire
  avec la chaîne `x86_64-onyx-elf` et un SQLite amalgamé, ou essayer avec deux petits programmes C (écrire par
  `pwrite` en gardant ouvert ; lire en un bloc de plus de `FS_DIRECT_MIN`).
- Ne jamais écrire sur `/media/sd1` du Pi ; pas de publication sans demande de l'utilisateur ; commits en français,
  `git commit -- <fichiers>` (d'autres sessions travaillent dans le même dépôt).
