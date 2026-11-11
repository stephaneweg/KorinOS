---
name: korin-kit-appkit
description: AppKit de Korin OS, le kit de base de tout programme — console (puts, print_dec), chaînes, tas (malloc), client de fichiers f_* et macro sys des appels 0x80-0x8E, lancement (f_spawn*, f_exec, f_fork), chargement des kits (f_kit_load, kits_open), mutex et conditions (mtx_*, cnd_*), traduction (lang_*). À charger pour écrire un programme ou un kit en assembleur FASMARM ; en C, il n'est pas lié : utiliser la libc et POSIX.
---

# AppKit : ce qui fait tourner un programme

Kit en assembleur, `SD0:/lib/appkit.so` (53 fonctions ; aussi module d'initramfs : seul kit qui ne passe pas par vfs).
Depuis l'étape 5 des fichiers, le noyau ne sert plus les appels `0x80`–`0x8E` : c'est le client de fichiers d'AppKit
qui parle à vfs et aux services de fichiers. **Presque tous les programmes asm l'importent.**

## En C / C++ : ne pas l'utiliser directement

Il n'y a pas d'`appkit_abi.h` ni de `-lappkit` (`tools/gcc/kits.sh` : libposix le prend déjà, ses noms `puts`, `malloc`
sont ceux de la libc). Écrivez du C normal ; la libc passe par AppKit pour vous :

| Besoin | En C | AppKit dessous |
|---|---|---|
| fichiers, dossiers, tubes | `open`, `read`, `fopen`, `opendir`, `stat`, `pipe` | client de fichiers, puis vfs |
| lancer un programme | `posix_spawn`, `exec*`, `system` (sans shell) | `f_spawn_file`, `f_exec_file` |
| fork | `fork` (marche si AppKit ≥ 50) | `f_fork` |
| ouvrir un kit | `<kit>_kit_open ()`, `armpi_kit_open` (`<armpi/kit.h>`) | `f_kit_load` |
| threads, mutex | `pthread_*`, `std::mutex` | appels du noyau directement |

Voir `SD0:/apps/help.app/api/libc_fr.md` (section « AppKit et les kits vus du C »). Le reste de ce skill est pour
l'**assembleur**.

## Où lire

| Sujet | Sur la carte | Dans le dépôt |
|---|---|---|
| Table ABI (arguments de chaque fonction) | `SD0:/src/kits/appkit/appkit.abi` | `src/kits/appkit/appkit.abi` |
| Mécanisme des kits, exemple asm | `SD0:/apps/help.app/api/kit_fr.md` | `src/apps/help/data/api/kit_fr.md`, `docs/KITS.md` (Chargement, mutex, fork, exec) |
| Macro `sys`, numéros d'appel | `SD0:/src/inc/armpi.inc`, `SD0:/src/inc/syscalls.inc` | `src/inc/armpi.inc` (`macro sys`), `docs/ABI.md` (l. 30-50) |
| Socle des commandes (`t_debut`, `t_fin`...) | `SD0:/src/inc/tool.inc` | `src/inc/tool.inc`, `docs/OUTILS.md` |
| Client de fichiers | — | `src/kits/appkit/fileclient.inc`, `docs/FICHIERS.md` |
| Vrais programmes | — | `src/bin/hello/hello.asm`, `src/bin/touch/touch.asm`, `src/bin/pibuild/pibuild.asm` |

## Ouvrir le kit (assembleur)

```
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
segment readable executable
kit_use '../../kits/appkit/appkit.abi'	; un relais par fonction ; active FICHIERS_APPKIT = 1
start:	bl	kits_open			; x0 = ligne d'arguments à garder avant ; message et fin si un kit manque
	...
segment readable writeable
kit_data				; la table d'import, remplie par kits_open
```

Avec `tool.inc`, `bl t_debut` fait `kits_open` et donne argc en x19, argv en x20. Les autres kits se déclarent par
d'autres `kit_use` après celui d'AppKit. Mêmes noms dans tous les langages (skills `korin-lang-*`).

**Macro `sys`** : dans un programme qui importe AppKit, `sys SYS_OPEN` … `sys SYS_UNLOAD` (`0x80`–`0x8E`),
`SYS_PRINT` et `SYS_FORK` vont à `fichier_sys` d'AppKit ; les autres numéros font `svc 0`. Mêmes registres rendus qu'un
`svc 0`, sauf **x16**. Un `svc 0` direct sur `0x80`–`0x8E` rend `E_NOSYS`.

## L'essentiel

Fonctions AAPCS64 : arguments x0–x7, résultat x0, x19–x28 gardés ; erreur = valeur négative (`tbnz x0,63,.err`).
- Console : `puts (chaîne)`, `print_dec (valeur)`, `fmt_dec (v, tampon) → fin`, `parse_dec (chaîne)`.
- Chaînes : `str_len`, `str_eq (a, b) → 1`, `str_cat (dest, src) → fin`, `str_ends (s, suffixe)`, `mem_move (d, s, n)`,
  `utf8_put (w0 = cp, dest) → octets`, `path_join (dest, dossier, nom)`, `path_parent (chemin)`.
- Tas : `malloc (taille) → bloc | 0` (par `SYS_SBRK` : **jamais rendu**, il n'y a pas de `free`).
- Fichiers : `f_ouvrir (chemin, O_READ|O_WRITE|O_CREATE|O_TRUNC|O_APPEND|O_NONBLOCK) → fd 64..127`, `f_lire`,
  `f_ecrire (fd, tampon, n)`, `f_lire_a`/`f_ecrire_a (…, position)`, `f_position (fd, pos, 0/1/2)`, `f_fermer`,
  `f_stat (chemin, {taille, attributs})` (`ATTR_DIR` 0x10), `f_lire_rep (fd, DIRENT) → 1 | 0 fin` (`DIRENT_NAME` 0,
  `DIRENT_SIZE` 256, `DIRENT_ATTR` 264, `DIRENT_BYTES` 272), `f_mkdir`, `f_unlink`, `f_chdir`, `f_getcwd (t, ≥ 256)`,
  `f_print (tampon, n)` (sortie standard), `f_pipe (&fds[2])`, `f_redir (chemin, O_READ|O_WRITE)`.
- Lancer : `f_spawn (chemin, args) → pid`, `f_spawn_pipe (…, &fd)`, `f_spawn_stdio (…, &fds[3])`,
  `f_spawn_env (chemin, argv, envp, &fds[3])`, attendre par `sys SYS_WAIT` (x0 = pid) ; `f_exec (chemin, argv, envp,
  &fds[3]|0)` (ne revient pas sauf erreur) ; `f_fork` → pid / 0 dans le fils ; `f_start_blob` (argv + env du père).
- Kits : `f_kit_load (nom)` (c'est ce qu'appelle `kits_open` sur `E_NOENT`).
- Synchronisation (mots de **32 bits à 0**, `dw 0`, alignés sur 4) : `mtx_lock`, `mtx_trylock → 0 | E_BUSY`,
  `mtx_unlock`, `cnd_wait (&c, &m)`, `cnd_timedwait (&c, &m, ms) → 0 | E_TIMEDOUT`, `cnd_signal`, `cnd_broadcast`.
- Langue : `lang_load (dictionnaire)`, `lang_tr (texte)`, `lang_puts (texte)`, `lang_get () → "fr"…`.

## Exemple

Affiche un fichier (`showfile chemin`), sur le modèle de `src/bin/touch/touch.asm` et `pwd.asm` :

```
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'			; (demandé par tool.inc)
segment readable executable
kit_use '../../kits/appkit/appkit.abi'
start:	bl	t_debut				; kits_open ; x19 = argc, x20 = argv
	mov	x22,1
	cmp	x19,2
	b.lo	.fin
	ldr	x0,[x20,8]			; argv[1]
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.err
	mov	x21,x0				; fd (x19-x28 gardés par AppKit)
.lit:	mov	x0,x21
	adr	x1,buf
	mov	x2,4096
	bl	f_lire				; octets, 0 en fin
	cmp	x0,0
	b.le	.ferme
	mov	x1,x0
	adr	x0,buf
	bl	t_write				; sortie tamponnée de tool.inc
	b	.lit
.ferme:	mov	x0,x21
	bl	f_fermer
	mov	x22,0
	b	.fin
.err:	ldr	x0,[x20,8]
	bl	t_err_fic			; « showfile: chemin : inaccessible »
.fin:	mov	x0,x22
	bl	t_fin				; vide la sortie, SYS_EXIT
include '../../inc/tool.inc'
segment readable writeable
kit_data
tool_data
buf	rb 4096
```

Construire sous Korin : le dossier dans `SD0:/src/bin/showfile/showfile.asm` (les `include '../../…'` trouvent
`SD0:/src/inc` et `SD0:/src/kits`), puis `pibuild bin SD0:/src/bin/showfile showfile` (lance `/bin/fasmarm`).

## Pièges

- FASMARM : `db` 8 bits, `dh` 16, **`dw` 32**, **`dd` 64** ; un mutex est donc un `dw 0`.
- Garder x0 (ligne d'arguments) **avant** `kits_open` (`hello.asm` : `mov x20,x0`) ; `t_debut` le fait seul.
- Les fonctions d'AppKit ne gardent pas x0–x15 ; la macro `sys` détruit x16 (et pousse x30 sur la pile).
- Descripteurs d'AppKit : 0–2 (standard) et 64–127 ; 3–63 rendent `E_BADF`. Un pointeur invalide donne une faute
  (le process s'arrête) au lieu d'`E_FAULT`.
- `malloc` n'a pas de `free` : allouer une fois, réutiliser.
- Un programme ne peut pas définir une étiquette du nom d'une fonction importée (`puts`, `malloc`, `str_len`...).
- Un kit qui importe AppKit fait `kit_use` dans son code, `kit_data` dans ses variables, `kits_open` au premier besoin ;
  pas d'adresse absolue dans un kit (`adr` seulement, pas de `dd label`).
- Mutex ni récursif ni lié à un propriétaire ; `cnd_wait` peut se réveiller sans signal : attendre dans une boucle.
- `fork` et `exec*` de la libc passent par `f_fork` et `f_exec_file` (`src/lib/posix/proc.c`) : un vrai `fork` en copie
  sur écriture, un vrai exec (même pid).
