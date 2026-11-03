# Le shell (`SD0:/bin/sh`)

`sh` est un sous-ensemble de POSIX, écrit en assembleur et donc dans le selfhost, sans `fork`. Il
sert de shell à telnet, et lance les commandes de `make` (`sh -c`). Sources : `src/bin/sh/`.

```sh
sh                          # interactif, sur un tube (telnet)
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
| Intégrées | `echo [-n]`, `cd`, `pwd`, `exit`, `export`, `unset`, `set [-ex] [--]`, `shift`, `test`, `[`, `read`, `exec`, `true`, `false`, `:`, `break`, `continue`, `help` |

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

**En interactif**, l'entrée de `sh` est celle de telnet : ce qui arrive pendant qu'un programme tourne
est gardé pour la suite, sauf Ctrl+C, qui l'arrête. Le fils n'y lit rien. Un texte incomplet (`if`
sans `fi`) demande une ligne de plus, avec l'invite `... `.

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
(bloc sur plusieurs lignes, erreur de syntaxe, frappe en avance, Ctrl+C). Pas encore sur le Pi.
