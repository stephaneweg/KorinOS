# Les commandes Unix (`SD0:/bin`)

Réécrites en assembleur d'après les outils d'Onyx (`user/BinUtils`), dans le selfhost. Chacune est
un petit programme de `src/bin/<nom>/`, sur le socle `src/inc/tool.inc` (l'équivalent de `tool.h`
d'Onyx). `sh` ([SHELL.md](SHELL.md)) les trouve par `PATH` (`/bin`). Elles se lancent aussi d'un
programme C, avec des arguments sans limite de longueur.

| Commande | Options |
|---|---|
| `cat` | fichiers, `-` : entrée standard |
| `ls` | `-a`, `-l`, trié |
| `cp` | `-r`, source(s) destination |
| `mv` | sans copie sur un volume, un fichier recopié d'un volume à l'autre |
| `rm` | `-f`, `-r` |
| `mkdir` | `-p` |
| `rmdir`, `touch` (`-c`), `basename` (suffixe), `dirname`, `pwd`, `sleep`, `echo` (`-n`), `true`, `false`, `test` | |
| `head`, `tail` | `-n N`, `-N` |
| `wc` | `-l`, `-w`, `-c` |
| `tee` | `-a` |
| `tr` | `-d`, plages `a-z`, `\n`, `\t` |
| `cut` | `-d C -f 1,3-5`, `-c 2-4` |
| `sort` | `-r`, `-n`, `-u` (tri fusion, stable) |
| `uniq` | `-c`, `-d`, `-u` |
| `grep` | `-E`, `-i`, `-v`, `-n`, `-c`, `-l`, `-q`, `-e` |
| `sed` | `-n`, `-E`, `-e` ; adresses (numéro, `$`, `/motif/`, plages) ; `s` (`g`, `p`, `&`, `\1`), `d`, `p`, `q` |
| `find` | `-name`, `-type f\|d`, `-maxdepth` |
| `arc` | archives, sur ArchiveKit : `arc l\|t archive` (lister, vérifier), `arc x archive [dossier]`, `arc c archive [-0..-9] fichiers` (format d'après `.zip`, `.tar`, `.tgz`, `.tar.gz`, `.gz`), `arc f` (formats) |

`echo`, `test`, `true`, `false` et `pwd` existent aussi dans `sh` : ces programmes servent à `make`,
qui lance directement une commande simple sans passer par le shell. `test` et la commande intégrée
partagent une seule source (`src/inc/testcmd.inc`).

**Expressions régulières** (`src/inc/regex.inc`) : le motif est compilé en une petite machine
(caractère, ensemble, choix, saut, capture), exécutée avec retour arrière. Syntaxe de base
(`\(`, `\)`, `\|`, `\+`, `\?`) ou étendue (`-E`), `.`, `[a-z]`, `[^x]`, `*`, `^`, `$`. Pas encore :
`{n,m}`, renvois `\1` dans le motif (ils marchent dans le remplacement de `sed`).

Pas encore : `awk`, `diff`, `xargs`, `nl`, `ed`, `hexdump`, `date`, `uname`, `df`, `mount`, `ps` et
`kill` en programmes (ils restent dans `sh`), les commandes réseau, et `sort -k`.

**Essai** : `/gcc/test/tooltest.sh` (`tools/gcc/test`) se vérifie lui-même. Sous QEMU (30/10/2026) :
71 PASS, 0 FAIL. Pas encore sur le Pi.
