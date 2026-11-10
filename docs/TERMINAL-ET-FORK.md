# Terminal ANSI, `fork`, bash, git et ripgrep : étude

Étude du 10/11/2026, avant tout code. But : réunir ce qu'il faut pour faire tourner **Claude Code** sous Korin OS (un
paquet npm sur Node.js, interface Ink en plein terminal, outil Bash, recherche par ripgrep, git conseillé), et plus
largement les programmes Unix interactifs. Node.js lui-même est hors de cette étude : il viendra après, sur ces
fondations.

Quatre prérequis :

1. un **Terminal** qui comprend les séquences ANSI/VT et donne au programme un vrai clavier (mode brut, taille, signaux) ;
2. **`fork`** dans le noyau (et un vrai `exec`) ;
3. **bash** ;
4. **git** et **ripgrep**.

Chaque section part de ce que le code fait aujourd'hui (références `fichier:ligne`), puis propose. Les décisions à prendre
par le propriétaire sont rassemblées à la fin (§7).

---

## 1. Le Terminal et la console aujourd'hui

### 1.1 Ce que fait le Terminal

`src/apps/terminal/terminal.asm` (1 302 lignes) est un **journal de lignes** avec son propre interpréteur de commandes,
pas un émulateur de terminal.

| Point | Aujourd'hui | Où |
|---|---|---|
| Géométrie | fenêtre fixe 760 × 480, 26 lignes, colonnes calculées une fois ; pas de `WF_RESIZABLE`, pas de `WEV_RESIZE` | `terminal.asm:11-16, 48, 51-63, 71-95` |
| Stockage | anneau de 400 lignes de 256 octets ; ni grille de cellules, ni curseur adressable, ni attributs | `:17-18, 333-353, 1283-1286` |
| Interprétation | LF : ligne suivante ; CR : ignoré ; TAB : espaces ; **tout autre octet, ESC compris, est affiché tel quel** | `term_putb :356-403` |
| Dessin | tout effacer puis une couleur par ligne (`ui_text`) ; trois couleurs fixes | `term_draw :468-538, :22-25` |
| Sortie du programme | `SYS_SPAWN_PIPE`, lecture non bloquante d'un tube vfs toutes les 15 ms (stdout et stderr mêlés) | `run_program :770-846`, `poll_child :629-670` |
| Clavier vers le programme | **aucun** : pendant qu'un programme tourne, les touches sont jetées (sauf PgUp, PgDn, Ctrl+C) | `:155-157` ; `help_fr.md` le dit |
| Ctrl+C | `SYS_KILL`, c'est-à-dire `proc_kill` avec −9 : **pas de SIGINT** | `:131-143` ; `src/kernel/syscall.asm:515-525` |

Aucun fichier du système (hors `src/lib`) ne cherche l'octet ESC (0x1B) ou une séquence CSI.

### 1.2 Le chemin des entrées et sorties standard

* AppKit : les descripteurs 0 à 2 sont « un tube vfs donné par le père, sinon la console »
  (`src/kits/appkit/fileclient.inc:16-18`). **Lire la console rend 0, fin de fichier, aussitôt** (`fileclient.inc:1048`).
* `f_spawn_pipe` donne au fils `{0, w, w}` : son entrée est celle du père, donc la console pour le Terminal
  (`fileclient.inc:1708-1710`).
* `sh` : interactif seulement si son entrée est un tube (telnet) ; il lit les octets bruts, sans écho ni effacement
  (l'édition est laissée au client telnet) ; ses fils reçoivent −1 en entrée, « le fils n'y lit rien »
  (`src/bin/sh/sh.asm:400-417, 463-525` ; `sh_run.inc:971-992` ; `docs/SHELL.md:54-56`). Il exporte `TERM=dumb`.
* telnetd : **mode ligne** (le client fait l'écho et l'édition), négociations ignorées, sous-négociations sautées :
  ni NAWS (taille) ni TTYPE (`src/svc/telnetd/telnetd.asm:1-14, 498-556`).
* **Il n'y a de discipline de ligne nulle part** : pas de périphérique tty, ni `ICANON`, `ECHO`, `ISIG`.

### 1.3 Le côté C (`libposix`)

| Fonction | Aujourd'hui | Où |
|---|---|---|
| `isatty` | vrai pour 0, 1 et 2, **même quand ce sont des tubes** | `src/lib/posix/misc.c:435-454` ; `fd.c:56-73` |
| `tcgetattr`, `tcsetattr`, `tcgetpgrp` | `ENOTTY` | `misc.c:467-469` |
| `tcsetpgrp` | **absente** (échec à l'édition de liens) | — |
| `TIOCGWINSZ` | `$COLUMNS` × `$LINES`, sinon 80 × 25 ; personne ne fixe ces variables | `misc.c:532-543` |
| `poll` sur l'entrée standard | toujours prêt | `poll.c:56-57` |
| `O_NONBLOCK` sur la console | ignoré | `file.c:248-249` |
| Groupes de process | `getpgrp`, `setsid` rendent le pid ; `setpgid` ne fait rien | `proc.c:52-56` |
| `termios.h` | complet pour compiler (valeurs Linux) | `include/sys/termios.h` |

Le noyau sait livrer de vrais signaux (`SYS_SIG_*`, `src/kernel/signal.asm`), mais **personne n'envoie SIGINT ni
SIGWINCH**. Un signal est livré au retour en EL0 : un thread qui attend dans `SVC_CALL` (lecture de tube) n'est pas
interrompu (`signal.asm:21-22`).

### 1.4 Ce qu'il faut à Ink (Claude Code), vim, less, readline

* **Sortie** : déplacements du curseur (`CSI A/B/C/D/G/H`), effacement de ligne et d'écran (`CSI K`, `CSI J`), couleurs
  `SGR` (16, 256 et 24 bits), gras, souligné, inverse, curseur masqué (`CSI ?25l/h`), collage encadré (`CSI ?2004h`),
  écran alternatif (`CSI ?1049h`, pour vim et less), zone de défilement (`CSI r`).
* **Entrée** : le mode brut (`tcsetattr` sans `ICANON` ni `ECHO`), les touches traduites en séquences (flèches
  `ESC [ A`, Échap, Tab, Maj+Tab `ESC [ Z`, Retour arrière `0x7F`, Ctrl+lettre), Ctrl+C en octet 3 quand `ISIG` est coupé.
* **Taille** : `TIOCGWINSZ` juste, et `SIGWINCH` quand la fenêtre change.
* `isatty` vrai **seulement** sur un terminal (sinon les programmes croient parler à un humain dans un tube).

---

## 2. Proposition pour le terminal

### 2.1 Un pseudo-terminal (pty) dans vfs

Comme sous Unix, séparer **l'émulateur** (la fenêtre) de **la discipline de ligne** (le pty), pour que le Terminal,
telnetd et plus tard un serveur SSH partagent la même mécanique.

* **Où** : dans **vfs**, à côté des tubes (`src/svc/vfs/pipes.inc`). Un pty, c'est deux anneaux, un état `termios`,
  une taille et un groupe de premier plan. Les tubes y sont déjà (décision « les tubes vont dans vfs », réponse 8), avec
  leurs bouts possédés par un pid, leurs lectures différées et leurs fins.
* **Deux bouts** : le **maître** (le Terminal, telnetd) et l'**esclave** (l'entrée et les sorties du programme). Le
  programme reçoit l'esclave en 0, 1 et 2, comme un tube aujourd'hui.
* **La discipline, dans vfs** : en mode canonique, l'édition de ligne (effacement, `VERASE`, `VKILL`, `VEOF`) et l'écho ;
  en mode brut, les octets passent, `VMIN`/`VTIME` respectés ; avec `ISIG`, Ctrl+C, Ctrl+\ et Ctrl+Z deviennent
  SIGINT, SIGQUIT, SIGTSTP envoyés au **groupe de premier plan** (`SYS_SIG_SEND`) ; `ICRNL`, `ONLCR`.
* **Nouvelles ops de vfs** : créer un pty, lire et écrire chaque bout, `TCGETS`/`TCSETS`, taille (`TIOCGWINSZ`,
  `TIOCSWINSZ`, qui envoie SIGWINCH au groupe), groupe de premier plan (`TIOCGPGRP`/`TIOCSPGRP`).
* **`V_STDIO` dit le type** de chaque descripteur standard (tube, pty esclave, fichier, console) : `libposix` en tire
  `isatty` exact et branche `tcgetattr`, `tcsetattr`, `tcsetpgrp` et l'`ioctl` sur les ops du pty.

### 2.2 Le Terminal devient un émulateur VT

Réécriture du cœur de `terminal.asm`, l'interface (fenêtre, menus, défilement, aide) restant :

* **Grille de cellules** `lignes × colonnes`, chaque cellule : point de code, couleur de texte, de fond, attributs ;
  curseur ; zone de défilement ; **écran alternatif** ; historique (les lignes qui sortent par le haut).
* **Analyseur** : l'automate de Paul Williams (VT500), restreint au sous-ensemble xterm du §1.4 ; `TERM=xterm-256color`,
  `COLORTERM=truecolor`. Les séquences inconnues sont **avalées**, jamais affichées.
* **Clavier** : codes HID → octets et séquences (§1.4), écrits sur le bout maître.
* **Fenêtre redimensionnable** : `WEV_RESIZE` → nouvelle grille → `TIOCSWINSZ` → SIGWINCH.
* **Dessin** : seules les cellules changées, par plages de même style (`ui_text` par segment de couleur) ; police
  DejaVu Sans Mono déjà là.
* Ses commandes internes peuvent rester, ou céder la place à un shell : au démarrage, le Terminal lance `sh` (puis bash)
  sur un pty, comme tout terminal.
* **telnetd** passe en mode caractère (`WILL ECHO`, `WILL SGA`), lit NAWS pour la taille, et se branche sur un pty.

### 2.3 Groupes de process (minimum)

Pour que Ctrl+C touche le programme au premier plan et pas le shell : un champ `P_PGID` dans le PCB (hérité au
lancement et au `fork`), `setpgid`/`getpgrp`/`getpgid` réels, `kill(-pgid)`. Le contrôle des tâches complet (arrêt
par SIGTSTP, état arrêté dans l'ordonnanceur, `WUNTRACED`) est une étape à part (§6, étape 6) : Claude Code n'en a pas
besoin.

### 2.4 Points de vigilance

* **Signal pendant une lecture** : un `read` qui attend dans vfs n'est pas interrompu par SIGINT. vfs rend déjà
  `E_AGAIN` au bout de `WAIT_MS` et le client redemande (`pipes.inc:9-12`) : `libposix` doit regarder les signaux en
  attente entre deux tentatives et rendre `EINTR`.
* **`poll`** : aujourd'hui une boucle de 1 ms pour les tubes (`POSIX.md:192-195`) ; à garder pour le pty au début, puis
  un vrai réveil.
* **Coût du dessin** : un programme comme Ink redessine souvent ; dessiner seulement les cellules changées est
  indispensable sur le Pi.

---

## 3. `fork` aujourd'hui

`fork`, `_fork` et `vfork` rendent `ENOSYS` (`src/lib/posix/proc.c:654-656`). `execve` est un `posix_spawn`, puis
`waitpid`, puis `_exit` avec le code du fils : le pid change et le process qui fait `exec` attend (`proc.c:658-668`).
Aucune doc n'explique pourquoi `fork` manque, seulement que `posix_spawn` suffisait (`FEUILLE-DE-ROUTE.md:423`).

### 3.1 Ce qui existe et servira

| Mécanisme | Où | Pour `fork` |
|---|---|---|
| Parcours d'un espace d'adressage, pages marquées `PTE_COW` | `img_clone`, `src/kernel/preload.asm:241-312` | le squelette de la copie |
| Défaut de page en copie sur écriture | `vm_fault_locked .cow`, `src/kernel/vmspace.asm:605-645` | copie à la première écriture |
| Recopie de la table des régions | `preload.asm:291-304` | régions du fils |
| Séquence de création | `proc_spawn_elf`, `src/kernel/proc.asm:42-186` | PCB, enregistrement, `P_PARENT`, avis de naissance |
| Avis de naissance aux services | `svc_naissance`, `src/kernel/svc.asm:964-1014` | prévenir vfs et les autres **avant** que le fils tourne |
| Cadre d'exception d'un appel système en haut du bloc du thread | `signal.asm:547` (précédent) | recopier les registres du parent, `x0 = 0` |
| Retour vers EL0 | `exc_return.restore`, `vectors.asm:108` | point de départ du thread fils |
| Poignées IPC avec références | `src/kernel/ipc.asm:104-230` | recopier la table, une référence de plus par objet |
| Table des descripteurs en mémoire du process | `libposix fd.c:30-35`, AppKit `fileclient.inc:42-53` | le fils l'a par la copie de la mémoire |

### 3.2 Ce qui manque

* **Pas de compteur de références par page physique** : `pmm.asm` n'est qu'une bitmap. Le défaut en copie sur écriture
  copie toujours et **ne libère jamais l'ancienne page** (`vmspace.asm:617-645`) : ça marche aujourd'hui parce que toute
  page `PTE_COW` est aussi `PTE_SHARED`, donc à quelqu'un d'autre (le modèle d'une image). Après un `fork`, la page
  appartiendrait au parent et au fils à la fois.
* **`PTE_SHARED` veut dire trois choses** : la page d'un autre (image, kit), une page épinglée (mapcaller,
  `mapcaller.asm:10-16`), de la mémoire de périphérique ou d'écran. `vm_destroy` et `vm_zap_range` les sautent toutes.
* **Les services rangent tout par pid** : bouts de tube (`END_OWNER`, `pipes.inc:5-8, 38-44`), fichiers ouverts de
  fatfs et ramfs (`src/svc/inc/fsowner.inc`), sockets de netd (`SK_OWNER`, `ops.inc:9-24`), tampons par client, fenêtres
  d'Elegant (`W_PID`), tampons partagés (`BF_OWNER`, `BF_GRANT`, `ws.asm:29-42`). Un fils avec un nouveau pid reçoit
  `E_BADF` sur tout ce qu'il a hérité en mémoire.
* **La position dans un fichier est gardée côté client** (`FCD_POS`, AppKit). POSIX veut qu'un parent et son fils
  **partagent** la position d'un fichier ouvert avant le `fork` : `{ cmd1; cmd2; } > f` dans bash en dépend.
* **ASID jamais recyclés** (`vm.asm:106`) : 65 536 espaces d'adressage au plus depuis le démarrage, et un shell en
  consomme deux par commande.

---

## 4. Proposition pour `fork` et `exec`

### 4.1 Le noyau : `SYS_FORK`

1. **Refuser** le `fork` d'un process qui a des droits sur le matériel (`P_DROITS` : IRQ, DMA, registres, écran,
   services enregistrés) : `E_PERM`. Ce n'est utile qu'aux programmes ordinaires.
2. **PCB** : nouveau pid, `vm_create`, recopie de `P_BRK`, `P_BRK_START`, `P_NAME`, `P_KITS`, `P_STDIO`, `P_SIGRET`,
   `P_PGID` ; `P_PARENT` = le parent ; file d'événements, sorties de threads et services remis à zéro.
3. **Mémoire**, en deux temps (décision §7.2) :
   * **d'abord la copie immédiate** de toutes les pages privées (pas de compteur à ajouter, aucun risque sur la
     mémoire partagée). Un bash pèse quelques Mo : acceptable pour commencer ;
   * **ensuite la copie sur écriture**, avec un **compteur de références par page** (un octet ou deux par page de 4 Ko :
     2 à 4 Mo pour 8 Go), un défaut qui rend la page inscriptible quand il en est le seul détenteur, et `vm_destroy`,
     `vm_zap_range` qui décrémentent au lieu de sauter.
   Les pages `PTE_SHARED` (images, kits, tampons, objets partagés) sont recopiées telles quelles, avec une référence de
   plus sur l'objet (`IM_REFS`, `BF_REFS`, `ISH_MAPS`).
4. **Régions** : la table `P_VMA` recopiée.
5. **Poignées IPC** : la table recopiée **aux mêmes numéros**, une référence de plus par objet (`iobj_ref`).
6. **Signaux** : la table des actions recopiée ; le masque et la pile de secours du thread aussi ; rien en attente.
7. **Un seul thread** : le fils reçoit une copie du thread appelant (registres du cadre d'exception, `TPIDR_EL0`, état
   FP/NEON) et repart de `exc_return.restore` avec `x0 = 0` ; le parent reçoit le pid du fils.
8. **Avis de `fork` aux services** avant que le fils tourne (comme `svc_naissance`, avec un drapeau « fork ») :
   chaque service qui garde des choses par pid les **ouvre aussi au fils** (§4.2).
9. **Recyclage des ASID** (`vm.asm:106`).

### 4.2 Les services : des entrées à plusieurs propriétaires

Plutôt que dupliquer chaque entrée et réécrire les tables d'AppKit dans le fils, une entrée ouverte (bout de tube,
fichier, socket) garde **un compteur et la liste de ses pid propriétaires**. À l'avis de `fork`, le service ajoute le fils
partout où le parent figure. À la fin d'un process (`SVC_OP_FIN`), il se retire ; l'entrée se ferme quand plus personne ne
la tient. Les **numéros restent les mêmes** : la mémoire recopiée du fils est juste, sans réécriture.

| Service | À changer |
|---|---|
| vfs | bouts de tube et de pty à plusieurs propriétaires ; répertoire courant du fils (déjà recopié à la naissance, `path.inc:310-333`) |
| fatfs, ramfs | fichier ouvert à plusieurs propriétaires ; **la position passe côté service** pour être partagée (AppKit garde un cache) ; tampon d'échange par pid : le fils en demande un nouveau |
| netd | socket à plusieurs propriétaires ; tampon par pid : nouveau pour le fils |
| Elegant | rien : fenêtres et surfaces restent au parent ; le fils ne doit pas s'en servir (documenté) |

### 4.3 AppKit et `libposix` : le côté du fils

* `fork()` dans `libposix` : appelle les fonctions de `pthread_atfork` (aujourd'hui une souche, `pthread.c:550`),
  `SYS_FORK`, puis, dans le fils : remettre à zéro les verrous internes (`__posix_lock`, malloc, `fc_verrou`), la table
  des enfants (`s_children`), le tid du thread principal, les tampons d'échange d'AppKit et de NetKit (`fc_tampons`,
  `nk_buf`), le cache `fc_stdio`.
* `getppid()` lit le vrai parent (aujourd'hui toujours 1, `proc.c:51`) ; `waitpid` connaît les enfants nés d'un `fork`.
* **Les descripteurs au-delà de 2 sont hérités** naturellement : ce que `posix_spawn` ne sait pas faire aujourd'hui
  (`POSIX.md:145-151`), `fork` le donne.

### 4.4 Un vrai `exec`

`execve` remplace aujourd'hui le process par un fils qu'il attend. Avec `fork`, chaque commande d'un shell coûterait
deux process et une copie de bash. Un **`SYS_EXEC`** charge le nouveau programme **dans le même PCB** : même pid, mêmes
poignées (sauf celles marquées `FD_CLOEXEC`, que `libposix` ferme avant), nouvel espace d'adressage depuis l'image
(`img_clone` ou `elf_load`), signaux remis par défaut sauf ceux ignorés. Les services n'ont rien à changer : le pid ne
bouge pas. Le chemin des images préchargées (`preload.asm`) sert tel quel.

### 4.5 Risques

* **Parent à plusieurs threads** : seul l'appelant est copié ; un verrou tenu par un autre thread reste fermé dans le fils.
  `pthread_atfork` et la remise à zéro des verrous internes le couvrent pour `libposix` et AppKit, pas pour le code des
  programmes (comme partout ailleurs).
* **Pages épinglées par mapcaller pendant une requête** : avec la copie immédiate, aucune ambiguïté ; avec la copie sur
  écriture, il faudra distinguer « épinglée » et « partagée » (un bit logiciel de plus).
* **Limites** : 128 requêtes en cours dans tout le système (`SR_MAX`), 64 enfants suivis par `libposix`, 64 fichiers par
  process dans AppKit. Un shell qui lance beaucoup de tubes les approchera.

---

## 5. bash, git, ripgrep

### 5.1 bash

GNU bash 5.2, chaîne `aarch64-onyx-elf` (GCC 14.2, newlib 4.4, `libposix`), configuré comme make
(`--host=aarch64-onyx-elf`, `tools/gcc/make.sh:22-25`), **mais avec `fork` disponible**.

| Point | Réponse |
|---|---|
| `fork`, sous-shells, tubes, `$(...)`, substitution de process | `fork` (§4) ; substitution de process : `/dev/fd` à vérifier (sinon `--disable-process-substitution`) |
| Édition de ligne (readline) | pty et `termios` (§2) |
| Contrôle des tâches | d'abord `--disable-job-control` ; ensuite §6, étape 6 |
| Bit d'exécution : tout fichier est `0644` (`stat.c:51`), bash cherche les commandes exécutables | `libposix` : `S_IXUSR`/`S_IXGRP`/`S_IXOTH` sur les fichiers réguliers des chemins d'exécutables, ou sur tout fichier dont les 4 premiers octets sont `\x7fELF` ou `#!` (décision §7.5) |
| Scripts `#!` lancés par d'autres programmes | `V_EXEC` (vfs) apprend à lire la ligne `#!` et lancer l'interprète |
| `PATH` : `:` sépare les entrées, et `SD0:/bin` en contient un | chemins virtuels de `SD0:/etc/paths.txt` (`/bin`, `/gcc/bin`), déjà ainsi dans l'environnement par défaut (`env.c:33-36`) |
| `alarm`, `setitimer` (TMOUT, `read -t`) : sans effet (`time.c:330-337`) | à faire : minuterie du noyau qui envoie SIGALRM |
| `SA_RESTART` sans effet | bash relance ses appels sur `EINTR` ; à surveiller |
| Utilisateur : `pw_shell` vaut `SD:/bin/cmd` (`misc.c:352-432`) | `/bin/bash` |
| here-docs | fichiers temporaires dans `/tmp` (`RAM0:/tmp`) : déjà là |

Essais : la suite de tests de bash (`make check`), en partie, sous QEMU ; puis les scripts de construction du dépôt
lancés par bash sur le Pi.

### 5.2 git

C, `Makefile` avec un `config.mak` :

* `NO_ICONV` (iconv est une souche, `misc.c:691-699`), `NO_GETTEXT`, `NO_TCLTK`, `NO_PERL`, `NO_PYTHON`,
  `NO_UNIX_SOCKETS`, `NO_MMAP` d'abord (pas de `MAP_SHARED` en écriture) ;
* **HTTPS** : libcurl 8.16 (OpenSSL 3.5.9, nghttp2, zlib) existe déjà et vérifie les certificats
  (`OPENSSL.md:133-160`) : `git-remote-https` est possible ;
* `run-command` : avec `fork`, tel quel ; sans, le chemin par lancement de la version Windows ;
* **le système de fichiers** impose ses réglages : `core.filemode=false` (pas de bit d'exécution),
  `core.symlinks=false` (pas de liens), `core.ignorecase=true` (FAT32 et exFAT ignorent la casse),
  `core.checkStat=minimal`, `core.trustctime=false` (une seule date, à 2 s près sur FAT) ; à poser dans le
  `gitconfig` du système ;
* fichiers verrou : `rename` sur un fichier existant marche (`FICHIERS.md:145-151`) mais n'est pas atomique en cas de
  coupure ;
* limites : chemins d'environ 254 octets, 64 fichiers ouverts (`core.packedGitLimit`).

git vient **après bash** : ses crochets, alias et l'éditeur passent par un shell.

### 5.3 ripgrep

Rien en Rust dans le dépôt : ni compilateur, ni cible. Deux voies :

| Voie | Travail | Pour | Contre |
|---|---|---|---|
| **A. Une cible Rust pour Korin** | `-Zbuild-std` avec une cible `aarch64-unknown-korin` : la caisse `libc` et la `std` (famille `unix`) branchées sur newlib + `libposix`, à la manière des cibles newlib existantes (ESP-IDF, Horizon) ; édition de liens par `aarch64-onyx-elf-gcc` | ripgrep tel quel, et tout l'écosystème Rust ensuite | un port de la `std` : du même ordre que CoreCLR |
| **B. Un `rg` de remplacement en C** | sur PCRE2 (portable, sans dépendance) : les options dont Claude Code se sert, `.gitignore`, `--files`, `--glob` | rapide, dans la chaîne actuelle | comportement à rapprocher de ripgrep option par option ; à refaire si Claude Code change |

À vérifier avant de choisir : la liste exacte des options de ripgrep que Claude Code utilise, et s'il accepte un `rg`
du système à la place du sien.

---

## 6. Ordre des étapes

Chaque étape se valide avant la suivante, sous QEMU puis sur le PocketTerm.

| # | Étape | Dépend de | Essai d'acceptation |
|---|---|---|---|
| 1 | **Terminal VT** : grille, analyseur, couleurs, écran alternatif, clavier en séquences, fenêtre redimensionnable | — | un programme qui écrit toutes les séquences du §1.4 ; capture comparée à xterm ; aucun ESC affiché |
| 2 | **pty dans vfs** + `termios`, `isatty` exact, `TIOCGWINSZ`/SIGWINCH, `ISIG` vers le groupe de premier plan, `EINTR` ; groupes de process minimaux ; Terminal et telnetd sur pty | 1 | un programme en mode brut lit chaque touche ; Ctrl+C donne SIGINT ; redimensionner donne SIGWINCH ; `isatty` faux dans un tube |
| 3 | **`fork`** par copie immédiate ; avis de `fork` ; entrées à plusieurs propriétaires et position partagée dans vfs, fatfs, ramfs, netd ; `fork()` de `libposix` | — | groupe `fork` de `posixtest` : mémoire séparée, descripteurs hérités (y compris > 2), position partagée, `waitpid`, `getppid` ; 10 000 `fork` sans fuite de pages ni d'ASID |
| 4 | **`exec` réel** | 3 | même pid avant et après, `FD_CLOEXEC` respecté, signaux remis |
| 5 | **bash** (sans contrôle des tâches) ; bit d'exécution ; `#!` dans `V_EXEC` ; SIGALRM | 2, 3, 4 | bash interactif dans le Terminal, readline, tubes, `$(...)`, here-docs ; une partie de `make check` |
| 6 | Contrôle des tâches : arrêt (SIGTSTP, SIGCONT, état arrêté), `WUNTRACED`, `tcsetpgrp` | 2, 5 | Ctrl+Z, `fg`, `bg`, `jobs` |
| 7 | **git** | 5 | `clone`, `commit`, `log`, `diff`, `push` en HTTPS sur un dépôt de test |
| 8 | **ripgrep** (voie A ou B) | 3 (voie A : la `std` attend `fork` ou `posix_spawn`) | les recherches de Claude Code sur ce dépôt donnent les mêmes résultats que sur Linux |
| 9 | **Copie sur écriture** avec compteurs de pages | 3 | même essai que 3, mémoire mesurée ; un `fork` de bash en temps constant |

Les étapes 1-2 et 3-4 sont indépendantes : elles peuvent avancer en parallèle.

---

## 7. Décisions à prendre

1. **pty dans vfs ou service à part** (`ptyd`) ? Proposé : **vfs**, où sont déjà les tubes, leurs propriétaires et
   leurs attentes.
2. **`fork` : copie immédiate d'abord, copie sur écriture ensuite**, ou la copie sur écriture tout de suite ? Proposé :
   **d'abord la copie immédiate** (moins de risque sur la mémoire partagée et épinglée), mesurer, puis l'étape 9.
3. **Position des fichiers côté service** (partagée après `fork`, comme POSIX) ? Proposé : **oui**, AppKit garde un cache
   pour les lectures séquentielles.
4. **ripgrep : voie A (Rust) ou B (`rg` en C sur PCRE2)** ? Proposé : vérifier d'abord ce que Claude Code demande à
   ripgrep ; si la liste est courte, B pour avancer, A plus tard pour l'écosystème Rust.
5. **Bit d'exécution** : sur les fichiers des répertoires d'exécutables, ou sur ceux qui commencent par `\x7fELF` ou `#!`
   (lecture de 4 octets à chaque `stat`) ? Proposé : **les deux règles**, la seconde seulement hors des répertoires connus.
6. **Les commandes internes du Terminal** : les garder, ou lancer un shell sur un pty à l'ouverture ? Proposé : un shell
   (`sh`, puis bash), les commandes internes passant dans `sh`.
