# vterm : les pseudo-terminaux

Étape 2 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md) (§2.1, décision du 10/11/2026 : un service à part, pas dans
vfs). Service `src/svc/vterm` (`SD0:/sys/svc/vterm`), lancé par `init2` depuis `SD0:/etc/services.txt` et relancé s'il
tombe (les pty ouverts sont alors perdus). Protocole : `src/inc/vterm.inc`.

## Un pty

| Partie | Contenu |
|---|---|
| bout maître | le Terminal, telnetd : il écrit ce que tape l'utilisateur, lit ce qu'affiche le programme |
| bout esclave | entrée, sortie et erreur standard du programme (`sh`, puis ses fils) |
| anneau d'entrée | 4 Ko, maître → esclave, après la discipline de ligne |
| anneau de sortie | 4 Ko, esclave → maître, après `OPOST` |
| `termios` | `struct termios` de Linux (`c_cc` à 32 cases) ; par défaut `ICRNL IUTF8`, `OPOST ONLCR`, `CS8 CREAD`, `ISIG ICANON ECHO ECHOE ECHOK ECHOCTL ECHOKE IEXTEN`, `^C ^\ DEL ^U ^D`, `VMIN` 1, `^Z ^R ^W ^V` |
| taille | lignes, colonnes, pixels ; 24 × 80 à la création |
| groupe de premier plan | 0 (aucun) jusqu'au premier `VT_SPGRP` |
| session | celle dont c'est le terminal de contrôle (`VT_SCTTY`) ; 0 : aucune |

16 pty au plus. Une poignée de bout vaut `VT_TAG` (0x40000000) + 1 + 2 × pty + côté (0 maître, 1 esclave) : le bit
`VT_TAG` la distingue d'un bout de tube de vfs (1 à 128) partout où passe un descripteur standard.

## Propriétaires

Comme les bouts de tube de vfs (`src/svc/vfs/pipes.inc`) : un bout appartient à un ou plusieurs pid, `E_BADF` pour les
autres. `vterm` est inscrit aux avis de naissance, de `fork` et de fin (`SVF_NAISSANCES | SVF_FINS`) :

* **naissance** (0xFFFE) : le fils devient propriétaire des bouts de vterm que le père lui donne en entrée, sortie ou
  erreur (0 : ceux du père, que vterm garde par pid) ;
* **fork** (0xFFFC) : le fils tient tous les bouts du père, aux mêmes poignées ;
* **fin** (0xFFFF) : le pid quitte ses bouts. Plus de maître : l'esclave lit la fin et le groupe de premier plan reçoit
  `SIGHUP`. Plus d'esclave : le maître lit la fin. Plus personne : le pty est libéré.

## Ops (`src/inc/vterm.inc`)

| Op | Arguments | Résultat |
|---|---|---|
| 1 `VT_OPEN` | — | a maître, b esclave (tous deux à l'appelant), c numéro du pty |
| 2 `VT_READ` | a bout, b octets (≤ 464), c bit 0 : sans attente | octets (données), 0 fin, `E_AGAIN`, `E_INTR` |
| 3 `VT_WRITE` | a bout, données | octets acceptés (0 : plein, redemander), `VT_E_IO` (l'autre côté est parti) |
| 4 `VT_CLOSE` | a bout | l'appelant quitte le bout |
| 5 `VT_TCGETS` | a bout | données : `termios` (48 o : `iflag oflag cflag lflag cc[32]`) |
| 6 `VT_TCSETS` | a bout, b 0 / 1 / 2 (`TCSANOW`, `TCSADRAIN`, `TCSAFLUSH`), données | 0 |
| 7 `VT_GWINSZ` | a bout | a lignes, b colonnes, c, d pixels |
| 8 `VT_SWINSZ` | a bout, b lignes, c colonnes, d pixels x \| y << 16 | 0 ; `SIGWINCH` au groupe si la taille change |
| 9 `VT_GPGRP` / 10 `VT_SPGRP` | a bout (b groupe) | a groupe de premier plan |
| 11 `VT_INFO` | a bout | a 1 maître / 2 esclave, b numéro, c octets à lire (`FIONREAD`, `poll`), d 1 : fin vue |
| 12 `VT_FLUSH` | a bout, b 0 entrée / 1 sortie / 2 les deux | 0 |
| 13 `VT_SLAVE` | b numéro du pty | a bout esclave, l'appelant en devient propriétaire (`open ("/dev/pts/N")`) |
| 14 `VT_SCTTY` | a bout | 0 ; `TIOCSCTTY` : le pty devient le terminal de contrôle de la session de l'appelant, qui doit la mener (`E_PERM` sinon, ou si le pty est à une autre session) ; son groupe passe au premier plan |
| 15 `VT_NOTTY` | a bout | 0 ; `TIOCNOTTY` : le meneur de la session rend le terminal (`SIGHUP` puis `SIGCONT` au groupe de premier plan, plus de groupe ni de session) ; un autre membre : rien ; `E_NOTTY` si ce n'est pas le terminal de sa session |
| 16 `VT_GSID` | a bout | a session du terminal (0 : aucune ; `tcgetsid`) |

Une lecture qui ne peut rien rendre attend dans vterm (réponse différée) : `E_AGAIN` au bout d'une seconde, le client
redemande ; en mode brut avec `VTIME`, l'échéance est `VTIME` et la lecture rend ce qui est là (0 compris).

## La discipline de ligne

* **Entrée** (écrite par le maître) : `ISTRIP`, `IGNCR`, `ICRNL`, `INLCR`. Avec `ISIG`, `VINTR`, `VQUIT`, `VSUSP`
  envoient `SIGINT`, `SIGQUIT`, `SIGTSTP` au groupe de premier plan (`SIG_SEND` à `-groupe`, docs/ABI.md), vident
  l'entrée (sauf `NOFLSH`), font l'écho `^C`, et les lectures de l'esclave en attente reçoivent `E_INTR`.
* **Canonique** (`ICANON`) : une ligne de 255 octets en édition ; `VERASE` (et `^H`) efface un caractère UTF-8,
  `VWERASE` un mot, `VKILL` la ligne, `VREPRINT` la réécrit, `VLNEXT` prend l'octet suivant tel quel ; la ligne passe
  au lecteur à `LF` ou `VEOL` ; `VEOF` la donne sans fin de ligne (sur une ligne vide : `read` rend 0). Un `read` rend
  une ligne au plus.
* **Brut** : les octets passent ; `VMIN` octets attendus (au plus ce qui est demandé), `VTIME` en dixièmes de seconde ;
  `VMIN` 0 et `VTIME` 0 : ce qui est là, tout de suite.
* **Écho** : `ECHO`, `ECHOE` (BS espace BS), `ECHOK`, `ECHOKE`, `ECHONL`, `ECHOCTL` (`^X`), par `OPOST`.
* **Sortie** (écrite par l'esclave) : `OPOST`, `ONLCR` (LF → CR LF), `OCRNL`.

## Les entrées / sorties standard

Un descripteur standard est un tube de vfs, un bout de vterm, un fichier ou la console. **C'est vfs qui porte le
type** : le bout de vterm donné au lancement (paramètres de `SPAWN_IMAGE`, `f_spawn_stdio`, ou hérité : 0) est gardé
tel quel dans la fiche du fils et rendu tel quel par `V_STDIO` ; le bit `VT_TAG` dit que c'est un pty et la poignée
nomme le bout. vfs ne le compte pas : vterm tient ses propriétaires (avis de naissance). Le noyau n'a rien à changer
(`P_STDIO` marque l'entrée comme « donnée par le père »).

* **AppKit** (`fc_std`, `f_print`) envoie lectures et écritures de ces descripteurs à vterm (`fc_vt_std`) ;
  `f_spawn_stdio` accepte une poignée de vterm dans `fds[]`.
* **libposix** (`tty.c`) en fait des descripteurs `POSIX_FD_TTY` : `isatty`, `tcgetattr`, `tcsetattr`, `tcgetpgrp`,
  `tcsetpgrp`, `ioctl` (`TIOCGWINSZ`, `TIOCSWINSZ`, `TIOCGPGRP`, `TIOCSPGRP`, `FIONREAD`...), `posix_openpt`,
  `/dev/ptmx`, `/dev/pts/N`, `/dev/tty` ([POSIX.md](POSIX.md)).

## Contrôle des tâches (10/11/2026)

Côté esclave, quand le pty a un groupe de premier plan, un process d'un autre groupe (en arrière-plan) de la session
du pty — ou de n'importe quelle session si le pty n'en a pas — :

* qui **lit** reçoit `SIGTTIN` (à son groupe) ;
* qui **écrit** avec `TOSTOP`, ou fait `VT_TCSETS`, `VT_SPGRP`, `VT_FLUSH`, reçoit `SIGTTOU`.

vterm demande au noyau (`SIG_SEND` avec `SIGF_TTY_MASK`, docs/ABI.md) : s'il a envoyé le signal, une lecture rend
`E_AGAIN`, les autres requêtes `E_INTR` (le process s'arrête au retour de l'appel ; repris, AppKit ou libposix refait
la lecture, libposix aussi les autres) ; si le process ignore ou masque `SIGTTIN`, ou si son groupe est orphelin,
`VT_E_IO` ; s'il ignore ou masque `SIGTTOU`, l'opération se fait. Ctrl+Z (`VSUSP`) envoie `SIGTSTP` au groupe de
premier plan, qui s'arrête (sans gestionnaire) ; les lectures en attente reçoivent `E_AGAIN` (et non `E_INTR` comme
pour Ctrl+C) : un programme en assembleur (AppKit) redemande une fois repris. Un groupe orphelin ne s'arrête pas (le
noyau jette le signal).

## Qui s'en sert

* **Le Terminal** : au démarrage, un pty et `SD0:/bin/sh` sur l'esclave ; les touches (`key_encode`) vont au maître, la
  sortie du maître à l'analyseur VT ; redimensionner : `VT_SWINSZ` ; réponses `DSR` / `DA` écrites sur le maître ; `sh`
  fini, le Terminal se ferme.
* **telnetd** : un pty par session, mode caractère (`WILL ECHO`, `WILL SGA`), `NAWS` → `VT_SWINSZ`.
* **sh** : interactif sur un pty ; groupe à lui, chaque commande dans son groupe au premier plan ; Ctrl+Z, `jobs`, `fg`,
  `bg` ([SHELL.md](SHELL.md)).

## Essais

`ttytest` (C, `src/lib/posix/test/ttytest.c`, `prebuilt/ttytest`) : 34 vérifications sur un pty à lui
(`posix_openpt`) et sur son entrée standard (QEMU, 10/11/2026 : 34 / 34). `ttytest raw` montre chaque touche en mode
brut, `ttytest size` la taille et chaque `SIGWINCH`, `ttytest sigint` attend Ctrl+C dans un `read`. `jobtest`
(`src/lib/posix/test/jobtest.c`) : le contrôle des tâches sur un pty à lui (Ctrl+Z, `SIGTTIN`, `TOSTOP`, sessions ;
QEMU, 10/11/2026 : 40 / 40).

## Limites

* Sessions simplifiées : un pty sans session (personne n'a fait `TIOCSCTTY` : le Terminal lance `sh` sans `setsid`)
  est le terminal de tout le monde ; le pty ne quitte pas la session quand son meneur finit (seulement par
  `TIOCNOTTY`) ; `open` d'un pty ne le donne jamais comme terminal de contrôle (toujours `O_NOCTTY`) ; `VT_SPGRP` ne
  vérifie pas que le groupe est de la session. Une lecture déjà en attente dans vterm n'est pas revue si le groupe de
  premier plan change (au plus une seconde).
* `IXON` / `IXOFF` (^S, ^Q) ne sont pas traités ; `PARMRK`, `BRKINT` non plus.
* Le groupe de premier plan est un nombre : un groupe disparu reste nommé jusqu'au prochain `VT_SPGRP`. Un signal de
  groupe ne touche jamais un process qui a les droits de `SD0:/sys`.
