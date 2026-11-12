# Les commandes de /bin en C

Décision du 12/11/2026 : tout le code commun au Pi et au PC est réécrit en C (voir
[etude/ETUDE-X86.md](etude/ETUDE-X86.md)). Après les kits viennent les commandes de `SD0:/bin`
(`src/bin/<nom>/<nom>.asm`, 116 programmes). Seul **fasmarm** (l'assembleur, 144 000 lignes générées)
reste en assembleur.

Choix de l'utilisateur : une commande en C **ne prend pas newlib ni libposix**. Elle repose sur un petit
runtime maison, `src/lib/korinrt`, au-dessus d'AppKit, comme les programmes d'essai des kits
(`src/kits/appkit/test/aktest.c`) : binaires aussi petits que l'asm, et qui tournent sur PC dès qu'AppKit
y tourne. Le même fichier C donne la commande du Pi et celle du PC.

## 1. Le runtime : `src/lib/korinrt`

| Fichier | Contenu | Équivalent asm |
|---|---|---|
| `korinrt.h` | tout ce qu'une commande inclut : types, constantes (`O_*`, `ATTR_DIR`, `E_*`, `V_*`…), prototypes d'AppKit et du runtime | `armpi.inc`, `fs.inc` |
| `crt0.c` | `_start` : AppKit ouvert, `argv` et environnement, `main`, `exit` | `t_debut`, `t_fin`, `kits_open` |
| `sys.c` | `sys_*` (appels de fichiers par `file_sys`), `ksys`, `path_type`, `path_join2`, `path_base`, `is_opt`, `vfs_call`, `vfs_msg`, `vfs_rename`, `vfs_utime`, `copy_file` | macro `sys`, `t_type`, `t_join`, `t_base`, `t_est_opt`, `t_vfs`, `t_msg`, `t_rename`, `t_utime`, `t_copier` |
| `io.c` | sortie tamponnée (`out_*`), erreurs (`err_*`), lecture tamponnée (`in_open`, `rd_*`) | `t_putc`, `t_puts`, `t_write`, `t_putn`, `t_dire`, `t_err*`, `t_ouvrir`, `t_getc`, `t_lire_ligne` |
| `format.c` | `printf`, `eprintf`, `snprintf`, `vsnprintf`, `vformat` | (nouveau) |
| `string.c` | `mem*`, `str*`, `is*`, `to*`, `strtol`, `strtoul`, `atoi`, `u64_to_str` | `str_len`, `str_eq`… d'AppKit |
| `heap.c` | `malloc`, `calloc`, `realloc`, `free` | `malloc` d'AppKit (jamais rendu) |
| `sort.c` | `qsort`, `sort_ptrs` (tri fusion stable) | `fusion` de `sort.asm` |
| `time.c` | `civil_from_secs`, `secs_from_civil` | `civil` de `date.asm`, `dostime.inc` |
| `regex.c` | `re_compile`, `re_search` | `src/inc/regex.inc` (grep, sed, ed) |
| `bin.ld` | script de l'éditeur de liens : en-têtes et code dans un segment lecture + exécution à `KRT_BASE`, variables dans le suivant | `app_header` |

### 1.1 Démarrage (`crt0.c`)

Le noyau entre dans `_start` avec la ligne d'arguments en `x0` (AArch64) ou `rdi` (x86-64), 0 s'il n'y en a
pas. `_start` :

1. ouvre AppKit (`SYS_LIB_OPEN "appkit"`, version minimale = nombre de fonctions de `appkit.abi`, comme
   `kits_open`) ; absent ou trop ancien : « `kit absent ou trop ancien : SD0:/lib/appkit` » sur la console
   du noyau et code 127 ;
2. prend argv : l'objet du père (`f_start_blob` : `+4` argc, `+8` nombre de variables, `+16` les chaînes à
   la suite, argv puis l'environnement) — c'est le cas de tout ce que lance bash ; sinon la ligne du noyau,
   coupée exactement comme `t_ligne_coupee` : aux espaces (pas aux tabulations), un mot qui **commence**
   par `"` va jusqu'au `"` suivant avec `\"` et `\\` (un `"` au milieu d'un mot est une lettre), et
   **`argv[0]` vaut « commande »** (la ligne ne porte pas le nom du programme : les messages d'erreur
   disent alors « `commande: …` », comme l'asm) ;
3. appelle `main(argc, argv)`, puis `exit(code)` : sortie standard vidée, `SYS_EXIT`.

`krt_argc`, `krt_argv`, `krt_envc`, `krt_env` (les chaînes « `NOM=valeur` » à la suite), `krt_line` (la ligne
brute du noyau, toujours gardée : `hello` l'affiche) et `getenv(nom)` sont à la disposition des commandes.
`exit` vide la sortie, `_exit` non.

### 1.2 Appels système (`sys.c`)

Comme la macro `sys` d'un programme en assembleur qui importe AppKit : les numéros `0x80`–`0x8E`
(fichiers et lancement), `SYS_PRINT` et `SYS_FORK` vont à **`file_sys`** d'AppKit, avec les descripteurs 0,
1 et 2 (entrée, sortie, erreur standard) ; les autres vont au noyau (`korin_sys0`…`korin_sys6` de
`src/kits/common/korin_kit.h`).

* `ksys(nr, a0…a5)` : n'importe quel appel, aiguillé comme la macro.
* `sys_open`, `sys_read`, `sys_write`, `sys_seek`, `sys_close`, `sys_stat` (`st[0]` taille, `st[1]`
  attributs), `sys_readdir` (`struct dirent` : nom de 256 o, taille, attributs ; 1, puis 0 en fin),
  `sys_mkdir`, `sys_unlink`, `sys_chdir`, `sys_getcwd`, `sys_print`.
* `sys_sleep(ms)`, `sys_clock(0 UTC | 1 décalage | 2 local)`, `sys_time_ms`, `sys_getpid`,
  `sys_vm_map(taille)` (région à zéro, peuplée à la demande).
* **`file_sys` sur AArch64 prend le numéro dans `x8`** (comme un `svc 0` ; son relais le donne à la
  fonction en 7e argument) : l'appeler comme une fonction C à 7 arguments met le numéro dans `x6` et tous
  les appels de fichiers échouent. `sys.c` l'appelle par une ligne d'asm (`bl file_sys`, `x8` chargé) ;
  sur x86-64, c'est bien le 7e argument. Passer par `ksys` ou les `sys_*`.
* **Ne pas appeler `f_read(0, …)` ou `f_write(1, …)` directement** : les fonctions `f_*` d'AppKit ne
  servent pas les descripteurs 0–2 ; `file_sys` (donc `sys_read`, `sys_write`) le fait.

Les fonctions d'AppKit s'appellent par leur nom (`parse_dec`, `fmt_dec`, `str_eq`, `lang_tr`,
`f_spawn_env`, `mtx_lock`…), par les relais générés par `tools/kits/abi2import.py` ; **`puts` et `malloc`
d'AppKit s'appellent `ak_puts` et `ak_malloc`** (le runtime a les siens).

### 1.3 Sortie et erreurs (`io.c`)

* `out_putc`, `out_puts`, `out_write`, `out_nl`, `out_putn` (décimal non signé), `out_putw(v, largeur)`
  (aligné à droite), `out_say` (traduit par `lang_tr`), `out_flush`, `printf`. Tampon de 4 096 octets
  (`T_OUT`), écrit par `SYS_WRITE` sur 1 quand il est plein (au caractère suivant, comme `t_putc`) et à la
  fin.
* `err_puts`, `err_say` (traduit), `err_msg(texte)` → « `argv[0]: texte traduit` », `err_file(chemin)` →
  « `argv[0]: chemin : inaccessible` » (traduit : `src/bin/cat/lang/en.txt`), `eprintf`. Une erreur **vide
  d'abord la sortie standard**, puis écrit sur 2 sans tampon, en plusieurs écritures comme l'asm.
* `ak_puts`, `print_dec` (AppKit) : sans tampon, par `f_print` (ce que faisaient `puts` et `print_dec` dans
  l'asm).

### 1.4 Lecture (`io.c`)

* `in_open(chemin)` : 0 ou « `-` » donnent l'entrée standard (0), sinon `sys_open(O_READ)` ; `in_close`
  ne ferme pas 0.
* `struct reader` (descripteur, tampon de 4 096 o) ; `krt_in` est le lecteur commun (`t_fd`, `t_lu` de
  l'asm). `rd_init(r, fd)`, `rd_getc(r)` (octet, −1 à la fin ; **une fin n'est pas retenue** : l'appel
  suivant relit, comme `t_getc`), `rd_line(r, &len)` : la ligne sans sa fin, dans `krt_line_buf`
  (65 536 o) ; au-delà de 65 534 octets le reste de la ligne est perdu ; une dernière ligne sans fin de
  ligne compte ; 0 à la fin. Les octets nuls restent dans la ligne (`len` les compte, `out_puts` s'y
  arrête, comme l'asm).

### 1.5 Le reste

* Tas (`heap.c`) : classes de 32 o à 1 Mo prises au tas (`SYS_SBRK`) et **réutilisées** après `free` ;
  au-delà, une région à soi rendue par `free`. Un seul thread.
* Tri : `sort_ptrs(v, n, cmp(a, b, ctx), ctx)` sur un tableau de pointeurs (tri fusion stable, même ordre
  que `sort.asm`), `qsort`.
* Dates : `civil_from_secs(s, &c)` (année, mois 1–12, jour, heure, minute, seconde, jour de la semaine,
  jour de l'année ; même calcul que l'asm), `secs_from_civil`.
* Expressions régulières : traduction pas à pas de `regex.inc` (même programme, mêmes sauts, même ordre
  d'essai, donc mêmes correspondances et mêmes captures). Base : `. [abc] [^a-z] * ^ $ \( \) \| \+ \?` ;
  `RE_E` (étendue) : `( ) | + ?` ; `\t`, `\n` ; pas de `{n,m}` ni de `\1` ; `RE_I` sans la casse ASCII ; en
  base, `^` n'est une ancre qu'en tête de branche et `$` qu'en fin de branche. `re_compile(motif, drapeaux)`
  rend un programme (0 : mémoire, ou plus de 1 024 instructions — l'asm débordait), `re_search(re, ligne,
  départ, caps)` : 1 si trouvé ; `caps[0]`, `caps[1]` le tout, `caps[2n]`, `caps[2n+1]` le groupe n
  (pointeurs dans la ligne, 0 si absent). Vérifiée par `grep` (69 essais identiques à l'asm, motifs
  qui font tomber l'asm compris : `a**` épuise la pile dans les deux) ; `sed` et `ed` s'en serviront.
* Langue : `lang_tr` d'AppKit, comme l'asm ; les textes traduits d'une commande restent dans
  `src/bin/<nom>/lang/<xx>.txt` (`tools/lang/merge.py` les assemble dans `SD0:/res/lang`).

## 2. Construire

```
wsl sh tools/bin/build_c.sh            toutes les commandes qui ont un .c
wsl sh tools/bin/build_c.sh cat wc     seulement celles-ci
```

`tools/bin/build_c.sh` (dans WSL : Git Bash abîme les chemins) :

1. tire `korin_sys_nr.h` de `src/inc/syscalls.inc` et les relais d'AppKit de `appkit.abi`
   (`abi2import.py --min N --rename puts=ak_puts,malloc=ak_malloc --sections` : un relais par section) dans
   `build/bin/gen` ;
2. compile le runtime en `build/bin/<arch>/obj/libkorinrt.a` (AArch64 : `aarch64-onyx-elf-gcc` de
   `/opt/toolchains` ; x86-64 : `gcc` de WSL) ;
3. lie chaque `src/bin/<nom>/*.c` en **`build/bin/aarch64/<nom>`** (lié à `0x4000000000`) et
   **`build/bin/x86_64/<nom>`** (lié à `0x400000`) : statique, `-O2`, `--gc-sections` (seules les fonctions
   qui servent restent), symboles retirés, `src/lib/korinrt/bin.ld` (en-têtes dans le premier segment, comme
   FASMARM). Refuse un binaire qui aurait des relocations (les chargeurs n'en font pas). Des pointeurs dans
   les données initialisées sont permis : l'adresse est fixe.

Options : `-ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fwrapv -fno-strict-aliasing`
(calculs en boucle comme l'asm), `-ffixed-x18` sur AArch64, `-mno-red-zone` sur x86-64. Les registres
flottants sont permis (le noyau les sauve ; AppKit ne les touche pas).

Sur la carte : `build.ps1` copie `build\bin\aarch64\*` dans `SD0:/bin` **à la place** des binaires asm
(l'asm reste assemblé : le selfhost le construit encore). Sur PC : `tools/pc/build.sh` lance
`tools/bin/build_c.sh` et met `build/bin/x86_64/*` dans l'initrd (un nom déjà pris, `hello`, garde le
programme d'essai du PC). Au moniteur du PC : `urun NOM arguments` (la suite de la ligne est la ligne
d'arguments du process).

Paquets : les binaires de `SD0:/bin` sont dans le paquet `armpi` ; une publication demande d'augmenter sa
version (`tools/pkg/versions.ini`, `mkrepo.py --bump`).

## 3. Vérifier : comparaison asm / C

Chaque commande portée a **`src/bin/<nom>/test.txt`** : une ligne de bash par essai (« `#` » : commentaire ;
« `@lang en` » : la suite en anglais, `SD0:/etc/system.ini` réécrit ; « `@lang fr` » pour revenir). Les
essais tournent dans `SD0:/bt` (fichiers d'essai écrits par `tools/bin/mkfixtures.py` : textes, nombres,
champs, lignes sans fin, octets hauts, longues lignes, un arbre, un nom avec espace) ; `RAM0:/tmp/w` est un
dossier de travail vide au départ ; l'entrée standard est un fichier vide (une commande qui lit l'entrée ne
bloque pas). La sortie et l'erreur standard sont mêlées dans un fichier, suivies de « `[code N]` » ; un
essai qui finit par une faute montre le message de bash avec un pid : le mettre dans un sous-shell dont
l'erreur va dans un fichier (`( cut -d ) 2> RAM0:/tmp/w/segv.txt; /bin/echo "faute : $?"`).

```
sh tools/bin/compare.sh cards          (Git Bash) build/card_bin_a et build/card_bin_c, copies de build/sdroot
wsl sh tools/bin/build_c.sh
sh tools/bin/compare.sh                tous les test.txt ; ou : sh tools/bin/compare.sh cat wc
sh tools/bin/compare.sh clean          cartes et images effacées
```

`compare.sh` met les binaires C dans `card_bin_c/bin` (la carte A garde l'asm), écrit `SD0:/bt` (fichiers,
`run.sh`, `boot.sh`) et une ligne « `SD0:/bin/bash SD0:/bt/boot.sh` » dans `SD0:/etc/startup.txt` des deux,
refait les images (`tools/mkdisk.py build/sd_bin_X.img --fat32 build/card_bin_X --exfat build/exfat_root`)
et démarre chacune dans QEMU (emplacement `RUN_PI_SLOT`, 3 par défaut, ports de `tools/kits/run_pi.sh` :
n'en prenez pas un qu'un autre agent utilise). Au démarrage, init2 lance `boot.sh`, qui fait tourner
`run.sh` puis écrit la sortie sur la console du noyau entre `@@BT_DEBUT` et `@@BT_FIN` ; `compare.sh` la
prend dans le journal de l'UART (sans les lignes du noyau « `[pid N nom] terminé` »). `build/bin/cmp_a.txt` et
`cmp_c.txt` doivent être **identiques** (`build/bin/cmp_diff.txt` vide) ; journaux complets :
`build/bin/cmp_uart_a.log`, `cmp_uart_c.log`. Environ 10 minutes pour les deux cartes. Pour une expérience
à la main par telnet : `sh tools/bin/pi_run.sh build/sd_bin_c.img SORTIE "commande"...`.

Pièges vus sous QEMU (12/11) :

* telnet : la première session tarde parfois plus de 20 s (bash au démarrage) et les sessions suivantes
  restent alors muettes ; deux sessions qui lançaient `run.sh` ensemble s'abîmaient leurs fichiers. D'où
  le lancement par `startup.txt` et la sortie par l'UART. (FTP : le mode passif ne traverse pas le réseau
  de QEMU.) `pi_run.sh` fait tout dans la première session qui répond ;
* **bash et ses fils ne partagent pas la position d'un fichier ouvert par `>`** : dans
  `{ /bin/echo a; echo b; } > f`, le `b` de bash écrase le `a` du fils (à corriger dans libposix ou vfs).
  Sortie en ajout (`: > f` puis `>> f`) ;
* un essai ne doit jamais abîmer les fichiers de `SD0:/bt` (`cp a.txt a.txt` vide `a.txt` : la
  destination est tronquée avant la lecture, comme l'asm) : travailler sur une copie dans `RAM0:/tmp/w`.

Écrire un `test.txt` : les cas normaux, chaque option, les options inconnues, les arguments manquants
(usage, code), les fichiers absents (message, code), plusieurs fichiers, l'entrée standard (`< fichier`,
`cat fichier |`, « `-` »), la sortie dans un tube (`| hexdump` pour voir les octets exacts), un essai par
`sh -c '…'` (le shell en asm : autre chemin de lancement), un essai en anglais pour les messages traduits.
Les commandes internes de bash (`echo`, `true`, `false`, `pwd`, `test`, `kill`…) s'appellent par leur chemin
(`/bin/echo`). Rien qui dépende de l'heure exacte ou d'un pid (`date +%Y`, `date | wc -c`).

Sur PC (pas encore de vfs : seules les commandes sans fichiers ont un sens) : au moniteur, `urun echo a b`,
sous QEMU (`python -I tools/pc/qemu_pc.py test --keys …`, clavier AZERTY : « a » se tape `q`, « - » `6`,
« : » `dot`, « / » `shift-dot`, « . » `shift-comma`, un chiffre `shift-N`) ; sortie dans
`build/pc/debug.log`. La construction du PC et `qemu_pc` sont partagées : les entourer de
`until mkdir build/pc.lock 2>/dev/null; do sleep 15; done` … `rmdir build/pc.lock`.

## 4. Règles pour porter une commande

1. Lire tout le `.asm` (et ses `.inc` : `tool.inc` surtout) avant d'écrire. Le C est une **traduction**,
   pas une réécriture : mêmes options (y compris celles qui sont ignorées, ou les lettres inconnues qui
   valent une autre option), mêmes messages (mêmes textes : `lang/<xx>.txt` les traduit), mêmes codes de
   sortie, mêmes tailles de tampons quand elles se voient (lignes de 65 534 octets, chemins de 300…), mêmes
   bizarreries. Une bizarrerie reproduite est notée en commentaire (« comme l'asm »), en tête du fichier si
   elle se voit de l'extérieur.
2. `src/bin/<nom>/<nom>.c` à côté du `.asm` (gardé jusqu'à ce que le selfhost construise le C) ; un seul
   `main` ; d'autres `.c` ou `.h` dans le même dossier si besoin (tous sont compilés ensemble).
3. Le tampon de sortie : `out_*` partout où l'asm utilisait `t_putc`/`t_puts` ; `sys_write(1, …)` là où l'asm
   écrivait directement (`cat`, `tee`) ; `ak_puts`/`print_dec` là où il appelait `puts`/`print_dec`.
4. Ce qui sert à plusieurs commandes va dans `src/lib/korinrt` (avec son équivalent asm noté dans le
   commentaire), pas dans la commande. Les fonctions de `tool.inc` y sont déjà ; `regex.inc` aussi.
5. Pas de newlib, de libposix ni d'en-tête du système (`<stdio.h>`…) : seulement `korinrt.h` (`<stddef.h>`,
   `<stdint.h>` et `<stdarg.h>` du compilateur sont permis).
6. Les autres kits : `src/bin/<nom>/bin.conf` avec « `import = netkit,buskit` ». `build_c.sh` génère leurs
   relais (`abi2import.py --sections`) et `<kit>_import.h` (prototypes en `long`, d'après les commentaires du
   `.abi` : `#include "netkit_import.h"`), et la liste `krt_kits` que `_start` ouvre comme `kits_open`
   (version minimale = nombre de fonctions du `.abi` ; un kit pas encore en mémoire est lu par `f_kit_load` ;
   absent : « `kit absent ou trop ancien : SD0:/lib/<kit>` », code 127). Construction vérifiée (avec
   `netkit`) ; pas encore lancé par une vraie commande.
7. Vérifier : `test.txt`, `compare.sh <nom>` sans différence, puis sur PC si la commande n'a pas besoin de
   fichiers. Mettre à jour le tableau ci-dessous.
8. Taille : `build_c.sh` l'affiche ; comparer à `build/sdroot/bin/<nom>` (l'asm).

## 5. État des commandes

Légende : **asm** pas encore porté ; **C Pi** sortie identique à l'asm sur le Pi (QEMU raspi4b, essais de
`test.txt`) ; **C PC** essayé au moniteur du PC (QEMU q35) avec la même sortie que sur le Pi. Tailles en
octets (asm / C AArch64 / C x86-64).

| Commande | État | Essais Pi | Tailles asm / C Pi / C PC | Remarques |
|---|---|---|---|---|
| `ansitest` | C Pi, C PC | 8 | 6904 / 6888 / 6624 | octets comparés par hexdump ; PC : `-n` identique au Pi (8132 o) |
| `arc` | C Pi | 40 | 5680 / 7224 / 6304 | ArchiveKit (`bin.conf`) ; `f`, `l`, `t`, `x`, `c` en zip, tar, tgz, gz |
| `basename` | C Pi, C PC | 15 | 3840 / 3160 / 2816 |  |
| `bench` | C Pi | 2 | 2127 / 3560 / 3072 | mesures du serveur graphique masquées ; PC : pas de serveur graphique |
| `blkbench` | C Pi | 3 | 4392 / 4824 / 4432 | chiffres masqués ; laisse l'attente active de blk.sd0 (comme l'asm) |
| `bus` | C Pi, C PC | 29 | 3007 / 5896 / 5152 | BusKit (`bin.conf`) ; ligne d'arguments brute ; `watch` : seulement l'usage ; PC : busd absent |
| `cachetest` | C Pi | 7 | 2719 / 4248 / 4384 | RAM0:, IMG0:, SD0:/cachetest.bin effacé |
| `cat` | C Pi, C PC | 18 | 3928 / 3464 / 3008 | PC : sans vfs, seulement l'erreur « inaccessible » |
| `chmod` | C Pi | 37 | 4368 / 4232 / 3840 | état du fichier demandé avant de lire le mode |
| `cliptest` | C Pi | 3 | 6526 / 4760 / 3968 | UIKit (`bin.conf`) ; clipd ; `krt_call_r4` (ui.c) |
| `codectest` | C Pi | 33 | 5648 / 7496 / 8032 | AudioKit (`bin.conf`) ; ligne brute ; « ouverture … ms » retirée ; -p (jfk.mp3, 2 s) chiffres retirés ; RSB par ak_dmadd, log2d faux comme l'asm ; PC : usage, erreur -2 |
| `cowtest` | C Pi | 6 | 8432 / 10472 / 10272 | PC : 29 PASS 4 FAIL (exec sans vfs, comptes de copies) |
| `cp` | C Pi | 21 | 4352 / 4888 / 4480 |  |
| `cpuwatch` | C Pi, C PC | 4 | 1879 / 3192 / 2720 | nombres masqués (charges, pids, durées) |
| `crash` | C Pi | 2 | 1864 / 3192 / 2720 | Pi : faute dès SYS_PRINT (file_sys), comme l'asm ; PC (« crashc ») : -14 puis faute |
| `cut` | C Pi, C PC | 28 | 4464 / 5520 / 5224 | PC : usage |
| `date` | C Pi, C PC | 19 | 4568 / 5160 / 5248 | PC : heure du CMOS |
| `df` | C Pi | 23 | 5008 / 6056 / 5824 | « df ram0 » (sans « : ») : le volume courant ; virgule même en anglais |
| `diff` | C Pi | 49 | 5792 / 7224 / 7104 | dont « trop de lignes » (6 000 × 6 000) |
| `directtest` | C Pi | 11 | 2728 / 4184 / 3744 |  |
| `dirname` | C Pi, C PC | 14 | 3816 / 3176 / 2784 |  |
| `dltest` | C Pi | 3 | 2770 / 4040 / 3360 | serveur de l'hôte 10.0.2.2:8000 ; durée filtrée |
| `echo` | C Pi, C PC | 14 | 3816 / 3240 / 2848 |  |
| `ed` | C Pi | 100 | 14624 / 17952 / 19680 | commandes par l'entrée standard (`printf … \| ed`) |
| `eject` | C Pi | 25 | 6680 / 6296 / 5808 | fausses clés (ramfs « blk.usbN ») ; fichiers ouverts (FS_SYNC > 0) pas essayé |
| `exectest` | C Pi | 9 | 6136 / 8248 / 8096 | crt0 appelle f_exec_resume avant f_start_blob (sinon descripteurs hérités perdus après exec) |
| `exfattest` | C Pi | 1 | 4648 / 6184 / 7488 | SD1: = image d'essai test_exfat.img sur les cartes d'essai (mounts.txt changé) |
| `false` | C Pi, C PC | 4 | 3728 / 2584 / 2240 |  |
| `find` | C Pi | 38 | 4424 / 4840 / 4240 | comparée dans les sorties brutes (`compare.sh` coupait la section) |
| `fontchk` | C Pi | 5 | 2064 / 3640 / 3072 | sans écart : la ligne « premier ecart » (adresses) pas essayée |
| `forktest` | C Pi | 3 | 6112 / 7880 / 7424 | PC (20 fork) : 16 PASS 7 FAIL (tubes, fichiers sans vfs) |
| `fputest` | C Pi, C PC | 2 | 2344 / 3880 / 3360 | PC : xmm8/9/15 gardés sous préemption |
| `fscache` | C Pi | 22 | 2175 / 3896 / 3392 | ligne brute du noyau (krt_line) ; compteurs sans chiffres (tr -d) |
| `fstest2` | C Pi | 1 | 7863 / 10392 / 12512 | SD1: = image d'essai ; numéros FS_BUFFER et FS_OPEN masqués |
| `fstest` | C Pi | 4 | 3424 / 4856 / 4960 | fichier de SD0:/docs effacé par test.txt |
| `ftp` | C Pi | 27 | 11176 / 14400 / 13256 | serveur FTP de l'hôte (2121) ; durées et port du 227 filtrés ; l'asm tombe en faute sur get/put d'un fichier local inaccessible (t_err_fic et x20), le C dit « inaccessible » |
| `grep` | C Pi, C PC | 69 | 80248 / 9232 / 9696 | regex.c vérifié par grep (motifs de base, étendus, -i, groupes, classes) ; PC : usage |
| `head` | C Pi | 20 | 4000 / 3984 / 3464 |  |
| `hello` | C Pi | 2 | 1826 / 3112 / 2624 | PC : l'initrd garde le hello d'essai du PC |
| `hexdump` | C Pi, C PC | 43 | 4504 / 4872 / 4512 | PC : entrée vide, usage |
| `httpd` | C Pi | 6 | 3090 / 4840 / 4032 | client par l'hôte (proxy vers hostfwd 8109) ; boucle locale de netd peu fiable |
| `httpget` | C (pas essayé) | 0 | 3288 / 4920 / 4064 | demande toujours example.com d'abord : pas essayé |
| `journal` | C Pi | 2 | 1908 / 3128 / 2688 | 60 s d'attente ; SD0:/journal.txt de la carte d'essai |
| `kittest` | C Pi | 2 | 2840 / 4328 / 3680 | PC : f_kit_load -1 (pas d'init2) |
| `ls` | C Pi | 69 | 80936 / 9184 / 9160 | -C, --color, LS_COLORS ; pty (telnet) essayé : mêmes sorties |
| `maptest` | C Pi | 2 | 3152 / 4968 / 4256 | PC : mapsvc absent (-2) |
| `mediatest` | C Pi | 18 | 20964 / 6536 / 5696 | MediaKit (`bin.conf`) ; vp9, av1, vp8/vp9 + opus, .mka ; temps masqués ; PC : mediakit absent (127) |
| `memhog` | C Pi | 10 | 2295 / 4120 / 3712 | touch jamais lancé ; sbrk refusé d'avance |
| `memtest` | C Pi | 6 | 12296 / 17016 / 16816 | PC : 28 PASS 3 FAIL (fils tués -2, pas de préchargement) |
| `mkdir` | C Pi | 15 | 4000 / 3688 / 3168 |  |
| `mkfs` | C Pi | 42 | 9680 / 12376 / 12080 | IMG0: agrandi à 40 Mo sur les cartes d'essai, vrais formatages ; jamais SD0:/SD1: |
| `mkinitramfs` | C Pi | 21 | 3976 / 5944 / 5632 | listes d'essai, .mod et config.txt d'essai retirés ; liste par défaut jamais lancée |
| `mount` | C Pi | 35 | 6768 / 8280 / 7504 | volumes ramfs ; volutil.c dans korinrt (mount, eject, mkfs) |
| `multiwin` | C Pi | 2 | 9184 / 8312 / 6944 | lancé puis arrêté (kill) ; 9 captures identiques dans les fenêtres (clics, Ctrl+K, saisie, agrandir) ; Message, Ouvrir, fermeture pas cliqués |
| `mv` | C Pi | 13 | 4024 / 4488 / 4168 |  |
| `netstat` | C Pi | 15 | 7080 / 6856 / 6304 | pids remplacés, lignes triées |
| `nl` | C Pi, C PC | 17 | 4152 / 5080 / 4544 | PC : usage |
| `notify` | C Pi, C PC | 15 | 2288 / 4072 / 3936 | `korinrt/notify.c` (notify.inc) ; PC : notifyd absent, usage |
| `nslookup` | C Pi | 11 | 5744 / 4968 / 4200 | une requête DNS (nom.invalid) |
| `pibuild` | C Pi, C PC | 6 | 77907 / 3768 / 3440 | usage et type inconnu seulement (make s'arrête) |
| `ping` | C Pi | 13 | 78955 / 4456 / 3712 | durées filtrées ; sans argument (example.com) pas essayé |
| `pipetest` | C Pi | 6 | 5352 / 7128 / 6336 |  |
| `play` | C Pi, C PC | 27 | 4993 / 6904 / 5952 | AudioKit ; ligne brute ; positions retirées (temps réel) ; PC : usage, erreur -2 |
| `preload` | C Pi, C PC | 11 | 2759 / 5048 / 4800 | `korinrt/notify.c` (preload_notify.inc) ; PC : « aucune image » |
| `presenttest` | C Pi | 3 | 7376 / 4904 / 4128 | nombres masqués ; capture identique |
| `proctest` | C Pi | 3 | 2479 / 4120 / 3552 |  |
| `pwd` | C Pi, C PC | 5 | 3768 / 2968 / 2560 | PC : sans vfs, ligne vide |
| `ramtest` | C Pi | 8 | 3967 / 5992 / 5856 | SD0:/gros.bin effacé |
| `readbench` | C Pi | 3 | 2447 / 4056 / 3488 | chiffres masqués |
| `reboot` | C PC | 1 | 1823 / 3064 / 2592 | aucun essai sans danger sur le Pi (tout lancement redémarre) ; PC : init2 absent, code 1 |
| `rm` | C Pi | 14 | 4160 / 3992 / 3584 |  |
| `rmdir` | C Pi | 7 | 3792 / 3176 / 2720 |  |
| `rngtest` | C Pi, C PC | 3 | 2999 / 4344 / 4576 | octets tirés masqués |
| `screenshot` | C Pi | 9 | 2311 / 3880 / 3392 | sur RAM0: ; pixels comparés octet pour octet asm / C sur la même carte |
| `sed` | C Pi | 88 | 81544 / 10280 / 10520 | `sed` asm : `.bss` dans le fichier |
| `selfhost` | C Pi, C PC | 5 | 6394 / 10136 / 8896 | jamais sans argument ; un assemblage sur RAM0: ; durées masquées ; PC : lancement impossible |
| `sh` | C Pi, C PC | 130 | 22920 / 34824 / 34256 | sh.c, sh_word.c, sh_run.c, sh_cmds.c ; sh -c, script, interactif sur un tube ; pty (jobs, fg, bg, Ctrl+Z) pas essayé ; PC : sh -c (help, echo, if, for) |
| `sigtest` | C Pi | 2 | 3910 / 5944 / 5344 | PC : bits de signaux corrigés le 12/11 (1 << signal, comme le Pi) |
| `sleep` | C Pi, C PC | 5 | 3760 / 2776 / 2368 |  |
| `sontest` | C Pi | 9 | 9152 / 7800 / 7424 | AudioKit ; `sndif.c` (sndif.inc) ; trames et cadences masquées ; faux micro et faux pilote (DMA -1 sous QEMU) ; PC : attend sound |
| `sort` | C Pi, C PC | 20 | 4528 / 5752 / 5376 | PC : entrée vide |
| `soundctl` | C Pi, C PC | 33 | 5072 / 6424 / 5664 | AudioKit ; ligne brute ; `proc_find` (sound.c) ; réglages remis ; PC : pas de service sound |
| `surfacetest` | C Pi | 7 | 7488 / 6168 / 5376 | nombres masqués ; capture identique hors de la surface animée |
| `svcfault` | C Pi | 3 | 1791 / 3384 / 3008 | PC : blk.sd0 absent |
| `svctest` | C Pi | 2 | 2383 / 3912 / 3328 | PC : hwtest absent |
| `synthtest` | C Pi, C PC | 9 | 12616 / 15368 / 14368 | AudioKit ; test.sf2 par .incbin ; poignées (adresses du tas) masquées ; PC : PASS |
| `sysstat` | C Pi, C PC | 16 | 6424 / 9064 / 8448 | compteurs et pids masqués (N) |
| `tail` | C Pi | 19 | 4104 / 4128 / 3656 |  |
| `tcpbench` | C Pi | 6 | 3495 / 4216 / 3520 | écoute et accept seulement (port 5001 non redirigé) |
| `tee` | C Pi | 10 | 4008 / 3528 / 3104 |  |
| `test` | C Pi, C PC | 59 | 4520 / 3480 / 3040 | essais par `/bin/test` ; PC : codes 0/1 (`-lt`, `=`, `!`, `-f` sans vfs) |
| `threadtest` | C Pi, C PC | 2 | 7296 / 9016 / 8224 | PC : TPIDR par %fs (FSGSBASE éteint) ; priorité haute 126 % (FAIL) |
| `touch` | C Pi | 13 | 3888 / 3688 / 3272 |  |
| `tr` | C Pi, C PC | 22 | 4144 / 3960 / 3744 | PC : usage |
| `true` | C Pi, C PC | 4 | 3728 / 2584 / 2240 |  |
| `ultest` | C Pi | 3 | 2691 / 3832 / 3616 | puits de l'hôte 10.0.2.2:9000 ; durée filtrée |
| `uname` | C Pi, C PC | 18 | 5440 / 6784 / 6280 | PC : x86_64, valeurs absentes « ? » sans vfs |
| `uniq` | C Pi | 17 | 4008 / 4760 / 4352 |  |
| `unload` | C Pi, C PC | 10 | 1919 / 3768 / 3328 | PC : usage, erreur -2 |
| `unzip` | C Pi | 55 | 84264 / 12400 / 11720 | `src/bin/zip/zipcli.h` ; dates des listes masquées (dossiers à l'heure courante) |
| `usbtest` | C Pi | 8 | 4216 / 5752 / 6816 | sans clé (10 s) ; fausses clés ramfs USB0:/USB1:, gros.bin écrit par bash ; durées masquées |
| `volume` | C Pi, C PC | 40 | 7672 / 7800 / 7072 | AudioKit ; sortie USB de QEMU présente ; réglages remis ; PC : pas de service sound |
| `wc` | C Pi, C PC | 19 | 4176 / 4888 / 4608 | PC : entrée standard vide (0 0 0) |
| `wget` | C Pi | 47 | 7632 / 8248 / 7744 | serveur HTTP de l'hôte (8000) |
| `whois` | C Pi | 16 | 6240 / 6080 / 5128 | serveur de l'hôte (4343) ; chemin IANA pas essayé |
| `wifi` | C Pi | 23 | 4640 / 6968 / 6240 | QEMU sans Wi-Fi : erreurs et usage |
| `writebench` | C Pi | 5 | 2471 / 3896 / 3360 | RAM0: et SD0:/writebench.bin effacé |
| `writetest` | C Pi | 3 | 2223 / 3576 / 3232 | SD0:/writetest.bin effacé par test.txt |
| `wstest` | C Pi | 6 | 2879 / 4744 / 4192 | Elegant tient l'écran : serveur refusé (-1), client -22 ; couple serveur/client pas essayé |
| `zip` | C Pi | 56 | 84208 / 12344 / 11480 | `src/bin/zip/zipcli.h` ; dates masquées |

Vérifié le 12/11/2026 : **442 essais, sorties identiques** asm / C sur le Pi (QEMU raspi4b, `sh tools/bin/compare.sh`), plus une session telnet (pty) sur la carte C (tubes, `sort | uniq -c`, `cp -r` et `rm -r` sur `RAM0:`, script bash, `hello` préchargé). PC (QEMU q35, `urun`) : 19 commandes en mode ligne (argv[0] « commande »), mêmes sorties que sur le Pi. `grep` asm pèse 80 Ko : son `.bss` est écrit dans le fichier.

Lot son et médias (12/11/2026) : **168 essais, sorties et codes identiques** asm / C sur le Pi (`BIN_TAG=sound`, emplacement 5) ; synthtest aussi sur PC (PASS).

Lot interface (12/11/2026) : **51 essais, sorties et codes identiques** asm / C sur le Pi (`BIN_TAG=ui`), plus des captures du bureau identiques dans les fenêtres des programmes (fond d'écran tiré au hasard, masqué).

Lot programmes d'essai (12/11/2026) : **113 essais, sorties et codes identiques** asm / C sur le Pi (`BIN_TAG=tests`), exectest après la correction de crt0. exfattest et fstest2 écrivent en dur sur SD1: : sur les cartes d'essai, SD1: est l'image test_exfat.img (`etc/mounts.txt` changé après `compare.sh cards`).

Lot système (12/11/2026) : **229 essais, sorties et codes identiques** asm / C sur le Pi (`BIN_TAG=sys`) ; tout sauf journal aussi sur PC.

Lot réseau (12/11/2026) : **170 essais, sorties et codes identiques** asm / C sur le Pi (`BIN_TAG=net`, emplacement 6), avec les serveurs d'essai de l'hôte : `python -I tools/nettest/binsrv.py` avant `compare.sh`. La boucle locale de netd (127.0.0.1) est peu fiable, avec l'asm comme avec le C : à reprendre dans netd.

Lot fichiers et volumes (12/11/2026) : **274 essais, sorties et codes identiques** asm / C sur le Pi (`BIN_TAG=files`). `pkg` n'est pas en asm (C++ sur libposix, OpenSSL, `src/lib/pkg`) : hors de ce chantier.

Lot texte et archives (12/11/2026) : **496 essais, sorties et codes identiques** asm / C sur le Pi (`BIN_TAG=text`) ; `hexdump` et `test` aussi sur PC.

Toutes les commandes de `src/bin` sont en C, sauf `fasmarm` (l'assembleur), `pkg` (C++ sur libposix) et les programmes propres au matériel du Pi (`gpuinfo`, `gputest`, `gpuhang`, `dmareach`), qui restent en asm (décision du 12/11).

## 6. Les lots suivants (proposition)

Indépendants entre eux (chaque lot touche ses dossiers `src/bin/<nom>` ; ce qui devient commun va dans
`src/lib/korinrt` : prévenir les autres lots avant d'y changer une signature). Les nombres sont des lignes
d'asm.

| Lot | Commandes | Dépend de | Remarques |
|---|---|---|---|
| 1. Texte | `sed` (493), `ed` (1 738), `diff` (480), `hexdump` (208), `find` (184), `test` (24 + `testcmd.inc`) | `regex.c` | `test` est aussi interne à bash : essais par `/bin/test` |
| 2. Fichiers et volumes | `ls` (842, `vterm.inc` : couleurs et colonnes), `chmod` (174), `df` (330), `mount` (245), `eject` (182), `mkfs` (862, ne jamais viser `SD1:` ni un vrai disque), `fscache` (128), `mkinitramfs` (531), `pkg` | `volutil.inc`, `blk.inc` | `ls` sert à l'alias de bash : à essayer aussi en pty |
| 3. Process et système | `sh` (1 229 + 3 `.inc` : le plus gros), `reboot`, `journal`, `notify`, `preload`, `unload`, `sysstat`, `cpuwatch`, `bus` (BusKit), `selfhost`, `pibuild` | `notify.inc`, `preload_notify.inc` | `sh` lance des commandes en mode ligne : à garder pour la fin du lot |
| 4. Réseau (NetKit) | `ping`, `nslookup`, `netstat`, `httpd`, `httpget`, `wget`, `whois`, `ftp`, `wifi`, `tcpbench`, `ultest`, `dltest` | `bin.conf` « `import = netkit` », `nettool.inc`, `netd.inc` à mettre dans le runtime | serveurs locaux de `tools/nettest` sous QEMU |
| 5. Archives | `zip` (778), `unzip` (759), `arc` (320) | ArchiveKit (C) | sorties octet pour octet (`hexdump`) |
| 6. Programmes d'essai du noyau et des fichiers | `forktest`, `exectest`, `pipetest`, `threadtest`, `sigtest`, `cowtest`, `memtest`, `maptest`, `memhog`, `proctest`, `svctest`, `svcfault`, `crash`, `kittest`, `fstest`, `fstest2`, `vfstest`, `ramtest`, `exfattest`, `cachetest`, `directtest`, `readbench`, `writebench`, `writetest`, `blkbench`, `dmareach`, `bench`, `rngtest`, `fputest` | — | souvent des essais d'AppKit et du noyau : leur sortie fait foi, y compris sur PC |
| 7. Son, image, GPU, interface | `play`, `soundctl`, `volume`, `codectest`, `sontest`, `synthtest`, `mediatest`, `gputest`, `gpuinfo`, `gpuhang`, `screenshot`, `cliptest`, `presenttest`, `surfacetest`, `multiwin`, `wstest`, `fontchk`, `ansitest`, `usbtest` | AudioKit, UIKit (ports en cours), MediaKit, GpuKit (reste en asm, propre au Pi) | après les ports d'AudioKit et d'UIKit ; `gpu*` restent propres au Pi |

`fasmarm` reste en asm.
