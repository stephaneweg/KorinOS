# Le shell (`SD0:/bin/sh`)

`sh` est un sous-ensemble de POSIX, écrit en assembleur et donc dans le selfhost, sans `fork`. Il
sert de shell à telnet et au Terminal quand bash n'est pas sur la carte (sur un pseudo-terminal,
[VTERM.md](VTERM.md)), lance les commandes de `make` (`sh -c`) et les scripts sans ligne `#!`. Sources :
`src/bin/sh/`.

```sh
sh                          # interactif, sur un pty (Terminal, telnet) ou un tube
sh -c "commandes" [nom [arguments]]
sh script [arguments]       # options : -e, -x
```

## Ce qu'il comprend

| Domaine | Détail |
|---|---|
| Mots | guillemets `'` et `"`, `\`, découpage aux blancs, motifs `*`, `?`, `[a-z]` sur le dernier composant, triés |
| Variables | `NOM=valeur`, `NOM=valeur commande`, `$NOM`, `${NOM}`, `${NOM:-x}`, `${NOM-x}`, `${NOM:=x}`, `${NOM:+x}`, `${#NOM}`, `$?`, `$#`, `$@`, `$*`, `$$`, `$0` à `$9` |
| Substitutions | `$(commande)`, `` `commande` ``, `$(( + - * / % ( ) ))` |
| Listes | `;`, `&&`, `\|\|`, `!`, fin de ligne, `\` en fin de ligne, commentaires `#` |
| Tubes | `a \| b \| c` (32 commandes au plus) |
| Redirections | `<`, `>`, `>>`, `n>`, `2>&1`, `1>&2`, `/dev/null` (descripteurs 0 à 2, 8 par commande) |
| Composés | `if`, `for`, `while`, `until`, `case`, `{ }`, `( )`, redirigés ou non |
| Intégrées | `echo [-n]`, `cd`, `pwd`, `exit`, `export`, `unset`, `set [-ex] [--]`, `shift`, `test`, `[`, `read`, `exec`, `true`, `false`, `:`, `break`, `continue`, `help`, `jobs`, `fg [%n]`, `bg [%n]` (sur un pty) |

Les programmes sont cherchés dans `PATH`, puis dans `SD0:/apps/<nom>.app`. Les anciennes commandes du
terminal (`ls`, `cat`, `cp`, `mkdir [-p]`, `rm [-f]`, `ps`, `kill`, `mem`) ne servent que si `PATH` n'a
pas de programme de ce nom : les outils Unix prendront leur place sans rien changer ici.

Pas encore : fonctions, `<<` (documents en ligne), arrière-plan (`&` vaut `;`), descripteurs
au-delà de 2, `break n`, `eval`, `.`, `trap`, les motifs dans les dossiers (`*/x.c`).

## Comment il fonctionne

**Pas d'arbre syntaxique** : le texte est analysé en s'exécutant, `x28` est le curseur. Avec `noexec`
supérieur à 0, on analyse sans rien lancer : une branche non prise, la fin d'un bloc après `break`,
le passage de vérification fait avant d'exécuter un texte. Une boucle remet simplement `x28` au début
de son corps. C'est court, et l'arbre aurait demandé un allocateur pour peu de gain.

**Sans `fork`**, tout ce qui demande un autre process relance `sh -c` sur sa tranche de texte, avec
toutes les variables : les membres d'un tube qui ne sont pas de simples programmes, `( )`, `$(...)`, un
composé redirigé. Un simple programme dans un tube est lancé directement. Le sous-shell garde ainsi
l'isolation attendue : `( cd x )` ne change pas le dossier courant.

**Un mot** se développe dans une arène, libérée à la fin de chaque commande (`ar_p`, et `at_p` pour
les pointeurs). Un caractère cité garde devant lui l'octet 1, pour que le découpage et les motifs ne le
voient pas ; il est retiré à la fin.

**Vers les programmes** : `f_spawn_env` d'AppKit (`src/kits/appkit/spawnenv.inc`) reprend le protocole
de libposix ([POSIX.md](POSIX.md)) : `argv` et environnement dans un objet partagé, sans limite de
longueur, et la ligne d'arguments pour les programmes en assembleur (vide au-delà de 1 024 octets).
`f_start_blob` fait le chemin inverse au démarrage. Les redirections passent par `f_redir`
([FICHIERS.md](FICHIERS.md)).

**En interactif sur un pty** (le Terminal, telnet : service vterm, [VTERM.md](VTERM.md)), la discipline
de ligne fait l'écho et l'édition (retour arrière, ^W, ^U) ; `sh` se met dans un groupe à lui
(`SETPGID`), ignore `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN` et `SIGTTOU` (il reprend le premier plan
depuis l'arrière-plan à la fin d'une commande), et pose `TERM=xterm-256color` et
`COLORTERM=truecolor` (si `TERM` manque ou vaut `dumb`). Chaque commande lancée a le terminal en entrée
et un groupe à elle (le premier pid d'un tube : tout le tube), mis au premier plan (`VT_SPGRP`) :
Ctrl+C lui envoie `SIGINT` ; à sa fin, `sh` revient au premier plan. Ctrl+C à l'invite abandonne la
ligne. Les `$(...)` et les sous-shells restent dans le groupe de `sh` (ils reçoivent `SIGINT` aussi).

**Tâches arrêtées** (10/11/2026, docs/POSIX.md « Contrôle des tâches ») : `sh` attend une commande avec
`SYS_WAIT` étendu (`WEX_UNTRACED`). Ctrl+Z (`SIGTSTP` au groupe de la commande) l'arrête : `sh` reprend
le premier plan, note la tâche (son groupe, le dernier pid du tube, son nom ; 8 au plus) et écrit
`[n] Arrêté  nom` ; `$?` vaut 128 + le signal. `jobs` les montre (`Arrêté`, `En cours`, `Fini` : l'état
est relu sans attendre, une tâche finie quitte la table) ; `fg [%n]` met la tâche au premier plan,
lui envoie `SIGCONT` et l'attend ; `bg [%n]` lui envoie `SIGCONT` et la laisse en arrière-plan (si
elle lit le terminal, `SIGTTIN` l'arrête de nouveau). Sans numéro : la plus haute de la table. Un
sous-shell ou un `$(...)` arrêté (dans le groupe de `sh`) : la tâche est le groupe de `sh`. Pas de
`&` (il vaut toujours `;`), ni de message à la fin d'une tâche en arrière-plan (seulement par `jobs`).
`sh` qui finit avec des tâches arrêtées : le noyau leur envoie `SIGHUP` et `SIGCONT` (groupe orphelin).

**En interactif sur un tube** (entrée donnée par un tube de vfs), ce qui arrive pendant qu'un programme
tourne est gardé pour la suite, sauf Ctrl+C (octet 3), qui l'arrête (`SYS_KILL`). Le fils n'y lit rien.
Un texte incomplet (`if` sans `fi`) demande une ligne de plus, avec l'invite `... `.

Les anciennes commandes du Terminal sont dans `sh` : `echo`, `cd`, `pwd`, `exit`, `help` intégrées ;
`ls`, `dir`, `cat`, `cp`, `mkdir`, `rm`, `ps`, `kill`, `mem`, `free`, `clear` (séquence d'effacement de
xterm) servent si `PATH` n'a pas de programme de ce nom.

| Fichier | Rôle |
|---|---|
| `sh.asm` | démarrage, options, interactif, sortie |
| `sh_word.inc` | variables, développement des mots, `$(( ))`, motifs |
| `sh_run.inc` | analyse et exécution, tubes, redirections, lancement |
| `sh_cmds.inc` | commandes intégrées |

## Essais

`/gcc/test/shtest.sh` (`tools/gcc/test`) se vérifie lui-même, ligne par ligne :

```sh
sh /gcc/test/shtest.sh
```

Sous QEMU (30/10/2026) : 45 PASS, 0 FAIL, dont une compilation par `gcc`, des tubes de programmes C,
l'environnement transmis et une ligne de plus de 2 Ko. Le mode interactif a été essayé à part
(bloc sur plusieurs lignes, erreur de syntaxe, frappe en avance, Ctrl+C), et le 10/11/2026 dans le
Terminal : `sleep 30`, Ctrl+Z, `jobs`, `bg`, `jobs`, `fg`, Ctrl+C ; `cat` arrêté au milieu de sa
lecture puis repris par `fg` (il lit la ligne suivante) ; `ttytest sigint` (C) arrêté, repris, puis
Ctrl+C (`EINTR`). Pas encore sur le Pi.

## bash

**GNU bash 5.2.37** (`SD0:/bin/bash`, paquet `bash`, [BASH.md](BASH.md)) est le shell complet : fonctions,
documents en ligne, arrière-plan `&` et `wait`, `trap`, tableaux, readline (édition, historique, complétion). Le
Terminal l'ouvre **s'il est sur la carte**, sinon `sh` (décision 6 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md) :
« `sh`, puis bash »). `sh` reste `/bin/sh` : l'interprète des scripts sans `#!` et de `make` ; telnet ouvre bash lui aussi (sinon `sh`). Le contrôle des
tâches (étape 6) attend que bash soit reconstruit avec `--enable-job-control`.
