# L'hôte Linux : Claude Code officiel sur Korin PC

Chantier ouvert le 21/11/2026 à la demande de l'utilisateur : **faire tourner la CLI officielle de Claude Code, non
modifiée, avec son abonnement** (pas de clé d'API), sur Korin PC (x86-64), par l'hôte Linux construit pour Widevine
(`src/lib/linux`, `src/sys/linuxhost`, docs/etude/WIDEVINE.md). Il prolonge la décision du 13/11
(`.agents/memory/armpi-linux-compat.md`) et l'étude du binaire (docs/etude/ETUDE-CLAUDE-NATIF.md).

## 1. Décisions de l'utilisateur (21/11)

| Question | Réponse |
|---|---|
| Claude Desktop | **rien pour l'instant** : Anthropic ne le publie que pour macOS et Windows, l'hôte Linux ne peut pas le lancer |
| Par quoi commencer | **jalons 1 à 3** : un exécutable glibc, puis Node.js, puis Claude Code dans le Terminal ; le frontal après |
| Le but | la **CLI officielle par l'abonnement** ; un frontal Korin (jalon 4) pilotera cette même CLI (`--input-format` / `--output-format stream-json`, connectée au compte de la même façon), jamais une clé d'API |

## 2. Ce qu'il y a (21/11)

* `liblinux` (`src/lib/linux`, 2 700 lignes) : charge une **bibliothèque** `.so` ELF64 x86-64 (`ET_DYN`), la reloge,
  lie ses imports à une glibc de compatibilité servie par libposix (environ 45 des fonctions dont Claude Code a
  besoin), traduit les `syscall ()` de glibc ; `capture.c` : les instructions `syscall` du code Linux renvoyées par le
  noyau (SYS_SYSCALL_CAPTURE, trame SIGSYS ; docs/etude/WIDEVINE.md § 3.2) ; `/proc/self/maps` fabriqué ; TLS
  dynamique (`__tls_get_addr`) ; threads, futex.
* `linuxhost` (`src/sys/linuxhost`) : `--check`, `--self-test`, `FICHIER.so FONCTION`.
* Le noyau du PC : la capture des syscall, l'arène haute `0x1000_0000_0000`–`0x7000_0000_0000` (VM_FIXED permis).

## 3. Ce que demande Claude Code 2.1.330

Binaire officiel de ce conteneur (`/opt/claude-code/bin/claude`, 381 Mo), lu sans l'exécuter sous Korin :

* **exécutable** ELF64 `ET_EXEC` non PIE, interpréteur `ld-linux-x86-64.so.2` ; segments `LOAD` de `0x200000` à
  environ `0x16D6_C000` (365 Mo à adresse fixe) ; `PT_TLS` de 0x5d30 octets (0x25f0 initialisés) ; pile demandée :
  12,2 Mo (`GNU_STACK`) ;
* liaison dynamique : `libc.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `libm.so.6` ; **519 fonctions**
  importées (`tools`, à venir : la liste et ce que liblinux sert déjà) : stdio et ses variantes `_chk`, locale,
  maths, pthread, `dlopen`, sockets, `getaddrinfo`, `getifaddrs`, **epoll**, **eventfd**, **inotify**, `pidfd_spawnp`,
  `posix_spawn*`, `fork`, `execve`, termios, `mount`, xattr, `iconv`, `backtrace`…
* c'est **Bun** : JavaScriptCore avec son compilateur à la volée (pages RWX ou W^X par `mprotect`), beaucoup de
  threads, la boucle d'événements sur epoll, des signaux (à vérifier : JSC suspend des threads par signal pour son
  ramasse-miettes sous Linux), le terminal en mode brut (TUI Ink), BoringSSL intégré (TLS par sockets ordinaires),
  `/proc/self/exe` ; la connexion au compte ouvre une page (xdg-open : Jet) et attend le retour sur `localhost`.

## 4. Jalon 1 : un exécutable glibc

`linuxhost run PROGRAMME [arguments]` : un exécutable Linux x86-64 lié à glibc démarre dans le process de l'hôte.
Essais : de petits programmes compilés par la gcc d'Ubuntu (`tools/linux/…`) : `hello`, threads, sockets, epoll,
signaux ; puis la vraie liste d'imports de Claude Code.

### 4.1 La carte mémoire

Un exécutable non PIE se charge **à ses adresses** : Claude Code à `0x200000`, le cas habituel des exécutables
`x86-64` (`0x400000` par défaut de ld). Or les programmes du PC sont liés à `0x400000` (`posix.ld`), l'hôte aussi.

* **L'hôte lié haut**, à `0x40_0000_0000` (comme les programmes du Pi) : `posix.ld` prend une base choisie
  (`POSIX_BASE`), `tools/linux/build.sh` lie `linuxhost` (et plus tard cdmhost s'il faut) à cette base. Le tas de
  `SBRK` (borné à `0x50_0000_0000`), l'arène `0x58…`–`0x68…`, les tampons et l'arène haute ne bougent pas. À vérifier :
  le chargeur ELF du noyau du PC (`core/elf.c`) accepte cette base.
* **Le noyau : une zone basse pour `VM_FIXED`** (`core/mm.c`, `in_arena`) : `[0x10000, 0x40_0000_0000)`, refusée si
  elle recouvre l'image du process (la vérifier contre l'étendue de l'image, que le noyau connaît : `core/image.c`) ;
  `find_gap` n'y cherche jamais (les adresses habituelles ne changent pas).
* le `brk` du programme Linux : une région à part (VM_MAP), après son dernier segment.

### 4.2 Le chargeur d'exécutables (`elf.c`)

`PT_LOAD` projetés à leur adresse (`VM_FIXED`), le fichier lu dedans, puis les droits (`mprotect`) ; `PT_INTERP`
ignoré (l'hôte est l'éditeur de liens) ; les imports (`R_X86_64_JUMP_SLOT`, `GLOB_DAT`, `COPY` : les données de la
libc copiées dans l'exécutable, comme `stdout`, `environ`) liés à la glibc de compatibilité ; la pile du programme
(argc, argv, envp, auxv : `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_ENTRY`, `AT_RANDOM`, `AT_SECURE`,
`AT_EXECFN`, `AT_HWCAP`, `AT_CLKTCK`) ; l'entrée `_start` appelée sur cette pile, qui appelle `__libc_start_main` :
servi par la glibc de compatibilité (constructeurs de l'exécutable, `main`, `exit`).

### 4.3 Le TLS de l'exécutable

Un exécutable a son bloc TLS **juste sous `%fs`** (variante II ; décalages fixés à l'édition des liens : Claude Code
lit `%fs:-0x5d30…`), et l'hôte aussi (libposix, liblinux). Les deux se recouvriraient. Solution retenue : **réserver
dans le TLS de l'hôte, en haut, une zone vide** (un `.tbss` de 64 Ko placé en dernier par `posix.ld`) : les variables de
l'hôte sont plus bas, le bloc de l'exécutable (au plus 64 Ko) se pose dans `[%fs - taille, %fs)`. Chaque thread : le
`.tdata` de l'exécutable recopié là (le fil principal par le chargeur ; les autres par un tremplin du
`pthread_create` de compatibilité). Le TCB à `%fs` est celui de libposix : `%fs:0` (soi-même), `%fs:0x28` (canari),
`%fs:0x30` (garde des pointeurs) sont lus par le code Linux (déjà vrai pour Widevine).

## 5. Jalons suivants

* **2. Node.js officiel** (linux-x64, glibc) : `node -e`, puis un petit serveur ; valide libuv (epoll, eventfd,
  threads, signaux) avant Bun.
* **3. Claude Code** : `claude --version`, `claude -p` (connexion par l'abonnement : la page d'Anthropic dans Jet, le
  retour sur la boucle locale), puis l'interface dans le Terminal de Korin (xterm-256color). Les outils qu'il lance
  (`bash`, `git`, `rg`) : ceux de Korin. Mise à jour automatique coupée.
* **4. Le frontal Korin** : une app UIKit qui pilote la CLI en `stream-json`.
* **5. L'installation** : comme Widevine (cdmupdate), le binaire n'entre jamais dans nos paquets ; un installateur le
  télécharge chez Anthropic, vérifie son empreinte, et le pose dans le dossier de l'utilisateur.

## 6. Risques

* JSC et les signaux (suspension des threads pour le ramasse-miettes) : la capture et la livraison des signaux
  Linux à un thread précis ;
* les `syscall` directs de Bun (au-delà de glibc) : la capture les attrape, il faut les traduire ;
* la taille : 381 Mo d'image, une pile de 12 Mo, un tas de plusieurs centaines de Mo ;
* les conditions d'Anthropic : binaire officiel non modifié, utilisé avec son propre compte.
