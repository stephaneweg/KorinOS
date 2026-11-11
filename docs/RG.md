# rg sur Korin

**rg**, une réécriture en C de **ripgrep 14.1** sur **PCRE2 10.46**, statique sur newlib + `libposix`
([POSIX.md](POSIX.md)). Étape 8 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md) (§5.3, décision 4 :
« voie B ») : Claude Code cherche avec ripgrep (outils Grep et Glob, index des fichiers des
mentions `@`) ; ce `rg` en tient lieu, sortie identique octet pour octet sur tout ce qu'il emploie.
Du code C construit sur le PC : **hors du selfhost**.

| Où | Quoi |
|---|---|
| `tools/rg/src/` | les sources (≈ 4 200 lignes de C11) : `main.c` (options), `walk.c` (parcours, exclusions, types), `glob.c` (globs, gitignore), `search.c` (recherche, sorties standard, résumés, JSON), `util.c`, `types.c` (table des types de ripgrep) |
| `tools/rg/build.sh` | construction (WSL) : PCRE2 puis rg, pour la carte et pour le PC |
| `tools/rg/difftest.py` | essais différentiels sur le PC contre le vrai ripgrep |
| `tools/rg/cardtest.py` | essais sur la carte, dans QEMU |
| `prebuilt/rg` | le programme de la carte (`SD0:/bin/rg`, copié par `build.ps1` et `tools/ci/carte_linux.py`) |
| `build/rg/host/rg` | le même rg pour le PC (essais) |
| `~/.cache/korin-rg` (WSL) | sources de PCRE2 et objets, hors de git |

Paquet : `[rg]` de `tools/pkg/packages.ini` (`bin/rg`), version 14.1.0 (celle de ripgrep qu'il imite).

## Construire

```sh
sh tools/posix/build.sh
sh tools/rg/build.sh            # PCRE2_TAR=<archive>, REBUILD=1 : tout refaire, HOST_ONLY=1 : le PC seul
python3 -I tools/rg/difftest.py # 545 cas : rg du PC contre /usr/bin/rg (ripgrep 14.1.0)
```

La source de PCRE2 est l'archive **locale** `/opt/src/rg-deps/pcre2_10.46.orig.tar.gz` (celle d'Ubuntu :
l'amont tel quel), sans téléchargement ni retouche. `build.sh` compile la liste de fichiers de son
`CMakeLists.txt` sans `configure` (`config.h.generic`, `pcre2_chartables.c.dist`), avec Unicode et le
**JIT** (sljit, AArch64 sur la carte). Le tout prend une quinzaine de secondes.

## 1. Inventaire : comment Claude Code se sert de ripgrep

Relevé dans le Claude Code installé sur la machine de construction (`/opt/claude-code/bin/claude`,
**version 2.1.322**, un exécutable Bun dont le JavaScript est lisible par `strings`), en lecture seule.
Le code est minifié : les noms de fonctions ci-dessous sont ceux du paquet, ils changent d'une version
à l'autre ; les listes d'options, elles, sont des chaînes littérales sûres.

### Où il trouve `rg`

```js
function kle(){if(cs(i.USE_BUILTIN_RIPGREP)){let{cmd:r}=O0("rg",[]);if(r!=="rg")return{mode:"system",command:r,args:[]}}
 if(wf()){let r={mode:"embedded",command:process.execPath,args:["--no-config"],argv0:"rg"};...return r}
 let{cmd:n}=O0("rg",[]);return{mode:"system",command:n,args:[]}}
function O0(e,n){return{cmd:fN(e)??e,args:n}}        // fN = Bun.which : recherche dans PATH
function wf(){return typeof Bun<"u"&&Bun.isStandaloneExecutable===!0}
```

* L'exécutable natif (Bun autonome) embarque ripgrep : il se relance lui-même avec `argv0 = "rg"`
  (mode « embedded »), sauf `USE_BUILTIN_RIPGREP=0` qui fait préférer un `rg` du `PATH`.
* **Sous Node.js (le paquet npm, ce que Korin lancera), c'est toujours le `rg` du `PATH`** (mode
  « system »). Il n'y a plus de `vendor/ripgrep/<plateforme>/rg` dans cette version. Sans `rg` :
  « ripgrep not found on PATH. Install it ... ». Sur Korin, `PATH=/bin:/gcc/bin` : `SD0:/bin/rg`.
* À la première recherche, `rg --version` doit sortir **0** et commencer par **`ripgrep `**
  (`r.stdout.startsWith("ripgrep ")`), sinon ripgrep est noté « en panne ». Notre `--version` dit
  `ripgrep 14.1.0`.
* Chaque appel : `rg [--no-config] [-j 1] <options> <cible>` — la cible (un dossier, un fichier, ou
  `-` pour l'entrée standard) **toujours en dernier**, le plus souvent un chemin absolu, le dossier
  courant étant la racine du projet (les globs ancrés `/src/**` s'y rapportent). `-j 1` : seulement
  pour relancer après `EAGAIN`. Délai : 20 s (`CLAUDE_CODE_GLOB_TIMEOUT_SECONDS`), sortie au plus 20 Mo.
* Codes de sortie : 0 et 1 sont des résultats ; 2 avec un message `rg: regex parse error`,
  `rg: error parsing glob`, `rg: unrecognized file type`, `rg: error parsing flag` (expression
  `/^rg: (?:regex parse error|error parsing glob|unrecognized file type|error parsing flag|compiled
  regex exceeds size limit)/m`) est rapporté au modèle comme motif refusé ; `rg: <cible>: ... (os error N)`
  comme erreur d'accès.

### Les appels

| Qui | Options (chaînes du paquet) | Sortie lue |
|---|---|---|
| **Grep**, mode `files_with_matches` (par défaut) | `--hidden --glob !.git --glob !.svn --glob !.hg --glob !.bzr --glob !.jj --glob !.sl --max-columns 500 [-U --multiline-dotall] [-i] -l --null` puis `-e PAT` si le motif commence par `-` (sinon `PAT`), `[--type T]`, `[--glob G]...` (le champ `glob` coupé aux blancs et aux virgules hors `{}`), `[--iglob G]...` (règles de refus des permissions), `[--glob !**/x/**]...` (versions orphelines), `[--glob !…/tasks/**]` | chemins séparés par NUL |
| Grep, mode `count` | idem avec `-c -H --null` | `chemin\0nombre\n` |
| Grep, mode `content` | idem avec **`--json -n`**, `[-o]`, `[-C N]` ou `[-B N] [-A N]` | **JSON** : `type` (`begin`, `match`, `context`, `end`), `data.path.text`/`bytes`, `data.lines.text`/`bytes`, `line_number`, `submatches[].start`/`match`, `end.binary_offset` (« binary file matches (found "\0" byte around offset N) ») ; lignes de plus de 500 caractères abrégées par Claude Code lui-même |
| **Glob** | `--files --null --glob PAT --sort=modified --no-ignore --hidden` (`--no-ignore` et `--hidden` réglables par `CLAUDE_CODE_GLOB_NO_IGNORE`, `CLAUDE_CODE_GLOB_HIDDEN`), `[--iglob G]... [--glob G]...` | chemins séparés par NUL, du plus ancien au plus récent |
| index des fichiers (`@`), si `git ls-files` échoue | `--files --follow --hidden --glob !.git/ ... --glob !.sl/ [--no-ignore-vcs]` ; réinclusions : `--files --hidden --glob G... --glob !.git ...` | un chemin par ligne |
| fichiers `.md` (commandes, agents) | `--files --hidden --follow --no-ignore --glob *.md` | lignes |
| versions orphelines | `--files --hidden --no-ignore --max-depth 4 --glob <motif>` | lignes |
| compte des fichiers du projet | `--files --hidden --glob !<d>...` (lignes comptées) | |
| modèles dépréciés dans le projet | `--no-config --line-buffered --only-matching --with-filename --null --no-line-number --no-heading --max-count 20 --max-filesize 1M --max-depth 8 -g !node_modules -g !site-packages -g !dist-packages -g !vendor -g !third_party -g !*.lock -e <regex>` | `chemin\0texte` |
| seaux de stockage (s3://...) | `-o -H --no-line-number --no-messages --no-heading --color=never --null --hidden -g !.git -g !node_modules -g *.toml -g *.yaml -g *.yml -g *.json -g *.cfg --max-filesize 4M -e <regex>` | `chemin\0texte` |
| bac à sable (Linux) | `--files --hidden --no-ignore --no-config --line-buffered --max-depth N --iglob ... -g !**/node_modules/**` | lignes |
| sous-répertoire (`/proc/self/fd/N`, macOS `Library`) | cible `-`, entrée standard ; `--glob !/…` | |

Le modèle peut aussi taper `rg` dans l'outil Bash : la liste des options « sûres » de Claude Code pour
`rg` est `-e -f -i -S -F -w -v -c -l --files-without-match -n -o -A -B -C -H -h --heading --no-heading
-q --column -g -t -T --type-list --hidden --no-ignore -u -m -d -a -L --color --json --stats --help
--version --debug`.

Le moteur : les motifs sont ceux du moteur par défaut de ripgrep (Rust `regex`), jamais `-P`.

### Verdict

**Faisable en C** : une vingtaine d'options, trois formats de sortie (lignes, résumés, JSON), le
parcours avec ses fichiers d'exclusion, la détection des binaires. Le JSON n'a rien de subtil (celui
de serde_json) ; ce qui l'est, c'est de reproduire exactement ripgrep là où il a des comportements de
détail (tampons de lecture et binaires, exclusions des dossiers parents) : fait et vérifié ci-dessous.
La voie A (cible Rust) n'a pas été nécessaire.

## 2. Ce que fait ce `rg`

Un seul fil (comme `rg -j1`) ; les noms d'un dossier sont **triés** (comme `--sort path`) : la sortie
ne dépend pas du système de fichiers.

* **Parcours** : itératif, un seul dossier ouvert à la fois (64 fichiers par process) ; les fichiers
  cachés sautés (`--hidden`, `-.`) ; `--max-depth`, `--max-filesize`, `-L` (sans effet : pas de liens
  sur Korin), `--files`, `--sort`/`--sortr` `path|modified|accessed|created`, `--type-list`.
* **Exclusions**, comme le crate `ignore` : `-g`/`--iglob` (dernier qui correspond gagne, `!` exclut,
  un glob positif fait une liste blanche des fichiers) d'abord ; puis `.rgignore` > `.ignore` >
  `.gitignore` > `.git/info/exclude` > exclusions globales (`$XDG_CONFIG_HOME/git/ignore` ou
  `~/.config/git/ignore`) > `--ignore-file` ; le dossier le plus profond gagne ; `.gitignore` ne compte
  que dans un dépôt (un `.git` au-dessus) et pas au-dessus de sa racine ; ceux des dossiers parents de la
  racine aussi ; puis `-t`/`-T` (table de ripgrep 14.1, `--type-add`, `--type-clear`, `all`) ; une
  liste blanche l'emporte sur « caché ». Les fichiers donnés en argument ne sont jamais filtrés.
  Gitignore : commentaires, `\#`, `\!`, `!`, ancrage `/`, `dossier/`, `**/`, `/**`, `/**/`, classes
  `[...]` (non fermée : littérale), `{a,b}`, blancs de fin (sauf `\ `). `--no-ignore`, `-u`/`-uu`/`-uuu`,
  `--no-ignore-vcs`, `-dot`, `-parent`, `-global`, `-exclude`, `-files`, `--no-require-git`,
  `--ignore-file-case-insensitive`.
* **Motifs** : `-e` (plusieurs), `-f`, `-F`, `-i`, `-S`, `-s`, `-w`, `-x`, `-v`, `-U`,
  `--multiline-dotall`, `-m`. PCRE2 en `UTF | UCP | MATCH_INVALID_UTF | MULTILINE` (`\w`, `\d`, `\b`
  Unicode et casse Unicode comme Rust ; octets non UTF-8 cherchés). Recherche ligne par ligne comme
  `grep-searcher` (candidat sur le tampon, confirmation sur la ligne seule), correspondances vides et
  correspondances suivantes comme `find_iter`. Sans `-P`, les **retours arrière** et **regards
  autour** sont refusés avec le message de ripgrep (son moteur ne les a pas) ; `-P` les permet.
* **Sorties** : lignes (`-n`/`-N`, `--column`, `-b`, `-H`/`-I`, `--heading`, `--null`, `-0`, `-o`,
  `--vimgrep`, `--trim`, `-A`/`-B`/`-C` et `--`, `--context-separator`, `--no-context-separator`,
  `--field-match-separator`, `--field-context-separator`, `--path-separator`, `--max-columns`,
  `--max-columns-preview`) ; résumés (`-c`, `--count-matches`, `-l`, `--files-without-match`, `-q`,
  `--include-zero`) ; `--json` (messages `begin`/`match`/`context`/`end`/`summary`, statistiques
  comprises) ; `--stats`. Par défaut, comme ripgrep : numéros de ligne et titres par fichier sur un
  terminal, pas dans un tube ; nom de fichier sauf pour un seul fichier donné. Pas de couleur
  (`--color` accepté).
* **Binaires**, comme ripgrep jusque dans le détail : un fichier trouvé par le parcours est lu par
  tampons de 64 Kio (qui grandissent pour une longue ligne et le restent pour les fichiers suivants) ;
  un NUL dans un tampon arrête la recherche, ce qui précède reste (« WARNING: stopped searching binary
  file after match », `binary_offset` du JSON ; `-c`/`-l` ne disent alors rien) ; un fichier donné
  (au plus 10) est lu d'un bloc : NUL dans les 64 premiers Kio ou dans une ligne trouvée, « binary
  file matches » ; l'entrée standard : NUL changés en fins de ligne. `-a`, `--binary`.
* **Entrée standard** : `-`, ou sans chemin quand elle est un fichier ou un tube.
* **Korin** : chemins à volume (`SD0:/x`) comme racines et dans la sortie ; les parents d'une racine
  s'arrêtent à `SD0:/` ; `/tmp` est `RAM0:/tmp`.
* Codes de sortie 0/1/2, messages d'erreur de ripgrep (`rg: regex parse error:`, `rg: error parsing
  glob '…': …`, `rg: unrecognized file type: …`, `rg: unrecognized flag …`, `rg: <chemin>: … (os error N)`).
* `--line-buffered` vide la sortie à chaque ligne. Acceptées sans effet : `--no-config`, `-j`, `--debug`,
  `--trace`, `--block-buffered`, `--mmap`, `--no-mmap`, `-E`, `--crlf`, `--no-unicode`.

### Défauts de ripgrep reproduits

* Les exclusions des **dossiers parents** d'une racine sont essayées sur « racine absolue + chemin
  relatif au dossier de l'entrée » (et non au dossier de l'exclusion) : `rg --files d` où le
  `.gitignore` du dessus dit `d/x/y.txt` montre `d/x/y.txt`. Et pour plusieurs racines, la base est
  celle de la première racine qui a chargé ce parent (`rg x src docs`).
* Le tampon de lecture garde sa taille d'un fichier à l'autre : un fichier binaire de 70 Kio est sauté
  entièrement après un fichier à longue ligne, lu en partie sinon.

## 3. Compatibilité vérifiée

### Sur le PC : `tools/rg/difftest.py`

Notre rg (`build/rg/host/rg`) et ripgrep 14.1.0 (`/usr/bin/rg`, lancé avec `--sort path`) sur les mêmes
arborescences, mêmes arguments, même dossier courant ; sortie standard et code de sortie comparés
**octet pour octet** (seules les durées de `--json`/`--stats` masquées) :

**545 cas sur 545 identiques** (10/11/2026). Ils couvrent : tous les appels de Claude Code ci-dessus
(Grep dans les trois modes avec `-i`, `--type`, `--glob`, `--iglob`, `-C/-B/-A`, `-o`, `-U`, cible
dossier/sous-dossier/fichier/binaire ; Glob avec dix motifs ; index des fichiers ; `.md` ; modèles ;
seaux ; bac à sable) ; gitignore (négations, ancrage, dossiers seuls, `**`, classes, imbriqués, parents,
dépôt sans `.git`, exclusions globales et `info/exclude`) ; binaires (NUL au début, juste avant,
pile à et après 64 Kio, première ligne de plus de 64 Kio, plus de 10 fichiers donnés, entrée standard) ;
Unicode (casse grecque, `ß`, CJK, emoji, Latin-1 invalide en JSON `bytes`) ; CRLF ; lignes longues et
`--max-columns` ; contextes qui se chevauchent, `-m` avec contexte ; fichiers vides et sans fin de
ligne ; nomenclature UTF-8 et UTF-16 ; erreurs ; une centaine d'expressions courantes du modèle (`\bfoo\b`, `class\s+\w+`, `\p{Greek}`,
`[[:alpha:]]`, `(?x)`, `\{`...) en lignes, `-o`, `--json`, `-c`, `-i`, `-w`.

### Sur la carte : `tools/rg/cardtest.py`

```sh
FASMARM=<fasmarm> python3 -I tools/ci/carte_linux.py
ARMPI_QEMU=/opt/qemu/bin/qemu-system-aarch64 python3 -I tools/rg/cardtest.py --boot
```

Le script copie sur la carte (`SD0:/t/rg`, FAT32) les sources de Korin (`src/` sans `extra/` :
1 700 fichiers, 33 Mo), un dépôt git créé par le git du PC (`.gitignore`, `.ignore`, `.git`) et des
fichiers binaires ; ripgrep 14.1 du PC donne sur la même arborescence les sorties attendues ; un script
bash lancé de `startup.txt` refait chaque appel avec `SD0:/bin/rg`, le chronomètre et compare (`diff`).

**QEMU, 11/11/2026 : 26 appels sur 26 identiques à ripgrep** (`--version` ; Grep `-l`, `-c`, `--json`
avec contexte, `-o`, `-U`, `--glob`, `--type` ; Glob `--sort=modified` ; index des fichiers avec et sans
`--no-ignore-vcs` ; cibles relatives et absolues `SD0:/t/rg/...` ; binaires parcourus et donnés ; motif
invalide, code 2), plus l'entrée standard, le chemin implicite (pas de `./`), un arbre sur `RAM0:/tmp`
(`.ignore` respecté) et le chemin implicite sur `RAM0:`. **JIT** : `PCRE2 10.46 is available (JIT is
available)` — la mémoire exécutable de `mmap` marche.

Durées sous QEMU (SD émulée, lente ; un seul fil) :

| Appel | Durée |
|---|---|
| Grep `-l` sur `src/` (1 700 fichiers, 33 Mo), premier appel (cache froid) | 28,5 s |
| le même genre d'appel ensuite (`-i -l`, `-c`, `--json -n`) | 7,8 à 9,4 s |
| aucune correspondance sur `src/` (tout est lu) | 8,0 s |
| `--files src` (parcours seul) | 2,8 s |
| Glob `**/*.c --sort=modified` sur `src/` | 1,5 s |
| `--json -n -i` sur 248 fichiers (`src/lib`, `kernel`, `svc`) | 1,3 s |
| dépôt git de ≈ 50 fichiers, Grep et `--files` | 0,1 à 0,3 s |
| `-c 'mov\s+x0,\s*x1'` sur `src/` : avec JIT / interpréteur (`RG_NO_JIT=1`) | 7,3 s / 8,6 s |

La lecture domine (≈ 4 Mo/s sur la SD émulée) : le JIT ne gagne que 15 %. Sur le PC, le même `rg`
fait les 541 essais en deux fois moins de temps que ripgrep (ripgrep démarre ses fils à chaque appel).

Taille : `prebuilt/rg` **734 Ko** (statique, PCRE2 avec le JIT compris).

## 4. Limites

* **Expressions** : PCRE2 et non le moteur Rust. Les motifs ordinaires se comportent pareil (vérifié) ;
  différences connues : PCRE2 accepte ce que Rust refuse (`a{` littéral, `\Q…\E`, quantificateurs
  possessifs, `(?|…)`...), et ignore les opérations de classes de Rust (`[a-z&&[^aeiou]]`, `--`, `~~`) ;
  `\u{…}`/`\U…` (Rust) absents ; `-w` par regards autour (`(?<!\w)…(?!\w)`) et non par `(?:^|\W)…`.
  La limite de taille de ripgrep (« compiled regex exceeds size limit ») n'existe pas ; PCRE2 a les
  siennes (profondeur, temps : la ligne est alors tenue pour non trouvée).
* Pas de `--replace`, `--passthru`, `--search-zip`, `--pre`, `--encoding` (seul l'encodage « auto » :
  nomenclature UTF-8 ôtée, UTF-16 avec nomenclature transcodé, comme ripgrep), `--hyperlink-format`,
  couleurs.
* Un seul fil : sur la carte, c'est le disque qui limite.
* Ordre : toujours celui des noms (ripgrep sans `--sort` donne un ordre quelconque, Claude Code ne s'en
  sert pas).
* `--sort=modified` : à date égale, l'ordre des noms (ripgrep : celui du système de fichiers).
* `--stats` : les octets imprimés des résumés comptent 0, comme ripgrep.
* JIT : sljit demande une page `PROT_READ|WRITE|EXEC` (`mmap`) et vide les caches par
  `__builtin___clear_cache` ; s'il échoue, PCRE2 interprète (`RG_NO_JIT=1` force l'interpréteur).
