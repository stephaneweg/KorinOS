# Korin OS sur PC x86-64 : étude

Étude du 11/11/2026, avant tout travail. Le propriétaire veut savoir si Korin peut tourner, plus tard, sur **son PC
actuel**, avec au moins **le son, l'USB, le réseau Wi-Fi et la 3D**. Cible unique : ce PC-là, pas « le PC » en général.

Décisions déjà prises par le propriétaire :

* les **kits gardent leur interface** (les tables `.abi`) ; ~~leur logique est réécrite en assembleur x86-64~~ :
  remplacé le 11/11 par la décision ci-dessous (C ou C++ commun) ;
* la partie assembleur du noyau et les pilotes sont réécrits ;
* (11/11) Korin reste un **micronoyau** sur PC comme sur le Pi ;
* (11/11) dans la branche `claude/x86-64-pc`, le code qui peut servir **aux deux machines est réécrit en C**, pour la
  portabilité ; seul ce qui dépend du processeur ou de la carte reste en assembleur (FASM pour x86, FASMARM pour ARM) ;
* (11/11, plus tard) **le plus de code commun possible entre le Pi et le PC** : les kits, les services, les pilotes
  et les apps en asm sont réécrits **en C ou en C++**, un à un, pour les deux systèmes ; seul ce qui dépend
  directement de la machine reste propre à chacune ;
* (11/11) premiers démarrages du ZBook sur **carte SD** (lecteur Realtek PCIe `10EC:5261`), plus tard sur le SSD.

## 0. État de la branche `claude/x86-64-pc`

Jalon 1 fait le 11/11/2026, **essayé sous QEMU q35 + OVMF seulement** :

| Fichier | Langage | Rôle |
|---|---|---|
| `src/kernel/arch/pc/boot/bootx64.asm` | FASM | chargeur UEFI : GOP, RSDP, lecture de `\korin\kernel.elf`, tables de pages du noyau, carte mémoire, `ExitBootServices` |
| `src/kernel/arch/pc/boot/bootinfo.inc`, `arch/pc/bootinfo.h` | | bloc de démarrage commun au chargeur et au noyau |
| `src/kernel/arch/x86_64/entry.asm`, `isr.asm` | FASM | entrée du noyau, `gdt_load`, 256 vecteurs |
| `src/kernel/arch/x86_64/gdt.c`, `idt.c`, `paging.c`, `cpu.h` | C | GDT + TSS (piles IST), IDT et écran d'exception, tables de pages (toute la mémoire à `0xFFFF800000000000`, framebuffer en écriture combinée, noyau à `0xFFFFFFFF80000000`) |
| `src/kernel/arch/pc/acpi.c`, `apic.c`, `ps2.c`, `pc_main.c` | C | MADT, FADT, 8259 masqués, APIC local (xAPIC et x2APIC), IOAPIC, minuterie à 1000 Hz étalonnée sur la minuterie ACPI ou le PIT, clavier PS/2 AZERTY, démarrage |
| `src/kernel/include/korin/arch.h` | C | **interface entre le noyau portable et une architecture** |
| `src/kernel/core/console.c`, `lib.c`, `pmm.c`, `monitor.c` | C | **portables** : console sur framebuffer (police Spleen du Pi), `kprintf`, pages physiques par bitmap, moniteur (`help`, `info`, `mem`, `uptime`, `clear`, `reboot`) |
| `tools/pc/build.sh` | sh (WSL) | construction : FASM de Windows par l'interop, gcc et ld de WSL |
| `tools/pc/qemu_pc.py` | Python | `run` (fenêtre) ou `test` (sans écran : capture, touches, journal du port 0xE9) |
| `tools/pc/sd.ps1` | PowerShell | copie sur une carte SD ou une clé FAT32 amovible, sans formater |

Jalon 2 fait le 11/11/2026, **essayé sous QEMU seulement** : process en mode utilisateur et ordonnanceur.

| Fichier | Langage | Rôle |
|---|---|---|
| `src/kernel/core/kheap.c` | C portable | tas du noyau (classes de 32 à 2048 octets, gros blocs en pages contiguës) |
| `src/kernel/core/sched.c` | C portable | threads, process, tourniquet préemptif (quantum de 10 ms), sommeil, libération par le thread inactif |
| `src/kernel/core/elf.c`, `initrd.c` | C portable | chargement ELF64 dans un espace d'adresses ; archive ustar `\korin\initrd.tar` |
| `src/kernel/core/syscall.c` | C portable | `SYS_PRINT`, `SYS_EXIT`, `SYS_THREAD_EXIT`, `SYS_GETPID`, `SYS_THREAD_SELF`, `SYS_SLEEP`, `SYS_YIELD`, `SYS_TIME_MS` ; les autres rendent `E_NOSYS` |
| `src/kernel/include/korin/vm.h`, `sched.h`… | C | interfaces portables (espaces d'adresses, threads) |
| `src/kernel/arch/x86_64/switch.asm` | FASM | bascule des threads, passage en mode utilisateur (`iretq`), entrée `syscall`/`sysretq` |
| `src/kernel/arch/x86_64/cpu.c`, `vm.c` | C | GS du cœur, MSR de `syscall`, pile des threads ; PML4 par process, moitié haute partagée, NX |
| `src/inc/x86/sys.inc` | FASM | macro `sys` des programmes x86 (`mov eax, n` + `syscall`) |
| `src/kernel/arch/pc/initrd/*.asm` | FASM | programmes d'essai : `hello`, `spin` (préemption), `crash` (faute isolée) |

Les numéros des appels système viennent de `src/inc/syscalls.inc` : `tools/pc/build.sh` en tire
`build/pc/gen/korin/syscall_nr.h`, le `.inc` reste la seule source. Le chargeur lit aussi `\korin\initrd.tar`.
Le moniteur est un thread du noyau, avec `ps`, `ls` et `run NOM`.

Vérifié sous QEMU : `hello` démarre seul et dort une seconde entre ses lignes ; `spin` calcule sans céder la main et le
moniteur répond quand même (préemption) ; `crash` provoque une faute de page qui n'arrête que lui ; plusieurs process
tournent ensemble et sont libérés à leur fin. À vérifier : sous QEMU (TCG), l'horloge du noyau semble prendre du
retard sur l'heure réelle quand un process calcule (étalonnage de la minuterie de l'APIC émulée ?).

Jalon 3 fait le 11/11/2026, **essayé sous QEMU seulement** : plusieurs cœurs (SMP).

* Tous les cœurs logiques **actifs** de la MADT démarrent, autant qu'il y en a (256 au plus) : rien n'est fixé pour
  une machine. Sur le ZBook (i7-11800H, 8 cœurs, hyperthreading), ce sont 16 cœurs logiques. Essayé sous QEMU avec 1,
  4 et 16 cœurs.
* `src/kernel/arch/pc/smp_trampoline.asm` (FASM, binaire plat mis en objet par `objcopy`) : copié dans 4 pages libres
  sous 1 Mo prises dans la carte de l'UEFI ; passe du mode réel au mode long avec des tables provisoires, puis
  `ap_entry` → `ap_main`. `src/kernel/arch/pc/smp.c` : INIT-SIPI-SIPI cœur par cœur (xAPIC ou x2APIC).
* Par cœur : structure `struct cpu` (GS), GDT et TSS avec leurs piles IST, APIC local et minuterie à 1000 Hz, thread
  inactif. L'IDT est commune.
* Verrous tournants (`korin/spinlock.h`, atomiques de GCC, portables) : ordonnanceur, tas, pages physiques, console,
  sortie de débogage.
* Ordonnanceur : une file commune ; le verrou est pris avant une bascule et rendu par le thread qui reprend, pour
  qu'aucun autre cœur ne reprenne un thread dont la pile n'est pas encore sauvée (`on_cpu`). Un cœur inactif ne
  repasse par l'ordonnanceur que s'il y a des threads prêts.
* État FPU/SSE de chaque thread sauvé à chaque bascule (`fxsave64`), CR0 et CR4 réglés sur chaque cœur.
* Heure : le TSC s'il est invariant (le cas sur le ZBook ; pas sous QEMU sans KVM), sinon la minuterie du cœur 0.

Pas encore fait : invalidation du TLB entre cœurs (inutile tant qu'un process ne retire pas de pages), réveil d'un
cœur inactif par IPI (aujourd'hui : à la milliseconde suivante), répartition qui tient compte de l'hyperthreading
(deux cœurs logiques d'un même cœur physique), état AVX (`xsave`), interruptions des périphériques réparties sur
plusieurs cœurs (tout va au cœur 0).

Jalon 4 fait le 11/11/2026, **essayé sous QEMU seulement** : les appels système du Pi, premier bloc. Tout est en C
portable dans `src/kernel/core`, avec les mêmes numéros, arguments et résultats que `docs/ABI.md`.

| Domaine | Fichier | Fait |
|---|---|---|
| Mémoire | `mm.c` | `SBRK` (peuplé, borné à `0x50_0000_0000`), `VM_MAP` / `UNMAP` / `PROTECT` / `ADVISE` / `QUERY` / `STATS` / `PAGEINFO` (régions paresseuses dans l'arène `0x58`–`0x68_0000_0000`, recollées, 4096 au plus), `.bss` et pile de 256 Ko paresseux, fautes de page (aussi quand le noyau lit la mémoire du process), `BUF_CREATE` / `GRANT` / `MAP` / `FREE` / `INFO` (`0x68_0000_0000` + n × 64 Mo), `DMA_ALLOC`, invalidation des TLB des autres cœurs par IPI avant de rendre une page |
| Process | `proc.c` | pids à génération (génération × 65536 + emplacement + 1), `SPAWN_IMAGE` (droits, ligne d'arguments), `WAIT` (et sa forme étendue `WEX_NOHANG`), `KILL`, `PROCLIST`, `PROCLIST_FROM`, `PROC_INFO`, `PROC_FLAGS`, `PROC_RIGHTS`, `GETPPID` |
| Threads | `thread.c`, `sched.c` | `THREAD_CREATE` / `_EX` (TLS = base de FS), `JOIN` (16 dernières fins gardées), `SET_PRIORITY` (priorités du Pi : avance 0, 4, 16 + âge), `SET_NAME`, `KILL` (différé au retour en mode utilisateur), `LIST`, `FUTEX_WAIT` / `WAKE` (clé : adresse physique) ; attentes avec échéance et réveil (`E_INTR` pour un thread tué) |
| Services | `svc.c` | `SVC_REGISTER` / `OPEN` / `CALL` / `NEXT` / `REPLY` / `SIGNAL` / `FORWARD`, avis `SVF_ENDS` et `SVF_BIRTHS`, `E_PIPE` quand un service s'arrête, requêtes abandonnées quand l'appelant meurt, 30 s au plus par appel |
| Matériel | `hw.c` | `DEV_MAP` (registres seulement, jamais la mémoire ; non cachés), `IRQ_BIND` / `ACK` / `WAIT` / `UNBIND` (ligne = GSI de l'IOAPIC, vecteur `0x40` + ligne, masquée à la levée, `IRQ_SHARED`) |
| Système | `sys.c`, `syscall.c` | `CLOCK_GET` / `SET` (RTC lue comme UTC), `SYSINFO`, `GETRANDOM` (ChaCha20 remêlé par RDRAND, le TSC et les interruptions), `KLOG` (64 Ko, heure par ligne), `MEMBARRIER`, `PRINT` |

Essai : `src/kernel/arch/pc/initrd/ktest.c`, premier programme en C (sans bibliothèque) : **37 essais, tous passés sous
QEMU avec 1, 4 et 16 cœurs** (`run ktest` au moniteur).

Écarts avec le Pi : `VM_PROTECT` à `PROT_NONE` rend les pages (x86 n'a pas de page présente sans accès : elles
reviennent à zéro) ; une seule zone de mémoire (pas de zone basse sous 1 Go) ; `DMA_CACHED` sans effet (DMA cohérent).

Jalon 5 fait le 11/11/2026, **essayé sous QEMU seulement** : signaux, contrôle des tâches, `fork` et `exec`.

| Domaine | Fichier | Fait |
|---|---|---|
| Signaux | `core/signal.c`, `arch/x86_64/sigframe.c` | `SIG_ACTION` / `SEND` (pid, groupe -pid, thread, `SIGF_TTY`) / `RETURN` / `MASK` (`SET`, `BLOCK`, `UNBLOCK`, `PENDING`, `SUSPEND`, `WAIT`) / `ALTSTACK` ; livrés au retour en mode utilisateur ; défauts du Pi (ignorés, arrêt, fin à -signal) ; fautes vers `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGTRAP` avec `si_addr` ; `SLEEP` interrompu (`E_INTR`), `SVC_CALL`, `JOIN`, `WAIT` non ; `SIGCHLD` au père |
| Contrôle des tâches | `core/signal.c`, `core/proc.c` | arrêt (`SIGSTOP`, et `TSTP` / `TTIN` / `TTOU` sans gestionnaire) de tout le process, reprise par `SIGCONT`, `WAIT` étendu (`WEX_UNTRACED`, `WEX_CONTINUED`), groupes et sessions (`SETPGID`, `setsid`, `GETPGID`, `getsid`) |
| Minuterie | `core/signal.c` | `SETITIMER` (`ITIMER_REAL`, `SIGALRM`, réarmée), thread du noyau « alarmes » |
| `fork` | `core/proc.c`, `core/mm.c`, `arch/x86_64/vm.c` | copie du process, un seul thread, `rax` = 0 dans le fils ; pages privées en **copie sur écriture** (compteur de détenteurs par page dans `pmm`, bit `PTE_COW`, la dernière écriture rend la page sur place) ; régions, tas, tampons, actions des signaux recopiés ; avis `SVC_OP_FORK` ; `E_PERM` pour un process qui a les droits (comme le Pi) |
| `exec` | `core/proc.c` | même PCB, même pid, autres threads tués, espace neuf construit avant le point de non-retour ; gestionnaires remis par défaut ; `PROC_EXEC` ; **seulement depuis une image en mémoire** (`EXP_IMAGE`), sinon `E_NOENT` (les images préchargées ne sont pas faites) |

La trame des signaux suit celle de Linux x86-64 (le Pi suit Linux arm64) : adresse de retour, siginfo (128 o),
ucontext (832 o, `gregs` dans l'ordre de Linux, état FPU à +320) ; documentée en tête de `sigframe.c`. Les appels
système construisent maintenant la même trame complète qu'une interruption et reviennent par `iretq`.

Essais : `ktest` (37) et `ptest` (22, `urun ptest` : lancé sans droits pour pouvoir faire `fork`), tous passés sous QEMU
avec 1, 4 et 16 cœurs.

Limites connues : un process trouvé par son pid peut être libéré pendant qu'on lui envoie un signal (il faudrait un
compteur de références) ; la livraison d'un arrêt à un thread qui tourne sur un autre cœur attend sa prochaine
interruption (pas d'IPI) ; pas de `SVF_MEMORY` (la mémoire manque : refus immédiat).

Jalon 6 fait le 11/11/2026, **essayé sous QEMU seulement** : IPC entre process (`core/ipc.c`, `0x70`–`0x7D`).

* Poignées 1 à 1023 par process : `HANDLE_CLOSE`, `INFO`, `WAIT` (attente dans le noyau, valeurs de `poll`),
  `LIST` (marques), `INHERIT` (données au prochain fils lancé) ; recopiées par `fork` (mêmes numéros), fermées à la fin
  du process.
* Canaux `STREAM`, `DGRAM`, `SEQPACKET` : `CHAN_CREATE`, `SEND` (iovecs rassemblées, poignées portées, paquet entier ou
  `EMSGSIZE`, flot par morceaux, anneau de 64 Ko par bout), `RECV` (paquet coupé : `IPC_TRUNC` ; flot : plusieurs
  messages sans passer un message qui porte des poignées ; `IPC_PEEK`), `SHUTDOWN` ; le bout receveur ne passe pas par
  son propre canal (`E_INVAL`).
* Mémoire partagée : `SHM_CREATE`, `SHM_OPEN` (noms globaux, `CREAT`, `EXCL`, `TRUNC`, `WEAK`), `SHM_UNLINK`, `SHM_CTL`
  (taille, sceaux, épingle), `SHM_MAP` : région `VMK_SHARED` de l'arène, pages de l'objet communes à tous les process,
  créées à la première touche ; `VM_COW` : copie privée à la première écriture. `VM_UNMAP`, `VM_QUERY`, `fork` y
  marchent.
* Un seul verrou pour tout l'IPC (le Pi en a un par canal) : plus simple, à affiner si la contention se voit.

Essais : `itest` (29, `urun itest`). Avec `ktest` et `ptest`, **88 essais passés ensemble sous QEMU avec 1 et 16
cœurs**.

Jalon 7 fait le 11/11/2026, **essayé sous QEMU seulement** : kits et images préchargées.

* **Kits** (`core/kits.c`) : `KIT_REGISTER` (droits de `SD0:/sys`), `LIB_OPEN` (version minimale ; un kit absent de la
  mémoire est cherché dans l'initrd sous `<nom>.so`, comme le module `appkit.so` du Pi). Même format et mêmes adresses
  que le Pi : ELF64 lié à 0, table « AKIT », version, décalages ; une place fixe de 64 Mo à `0x50_0000_0000` + n, 16
  kits ; code partagé, variables recopiées dans chaque process (`fork` : copie sur écriture). Un kit x86 s'écrit en
  FASM (`format ELF64 executable 3 at 0`, accès relatifs à RIP) : exemple `initrd/testkit.so.asm`.
* **Images** (`core/image.c`) : `IMAGE_CREATE` (fenêtre à `0x6C_0000_0000` + n × 4 Go, 4 chargements par process),
  `IMAGE_SEAL` (en-têtes vérifiés, image en place, l'ancienne du même chemin n'est plus prise), `IMAGE_LIST`,
  `IMAGE_PRELOAD`, `IMAGE_FORGET`, `SPAWN_PRELOADED` (chemin aux majuscules près et taille ; droits seulement depuis une
  image construite avec droits). Pages entières du fichier mappées telles quelles (code partagé, données en copie sur
  écriture), les autres recopiées, `.bss` paresseux. Épinglée : gardée ; passagère : rendue avec son dernier process.
  `EXEC` prend maintenant l'image du chemin quand `EXP_IMAGE` vaut 0.

Essais : `mtest` (18, `urun mtest`). Les quatre programmes, **106 essais, passent ensemble sous QEMU avec 1 et 16
cœurs**.

Jalon 8 fait le 11/11/2026, **essayé sous QEMU seulement** : démarrage et derniers appels de service.

* **Démarrage** (`core/boot.c`) : l'initrd tient lieu d'initramfs. `MOD_LANCER` (un module, avec droits),
  `MOD_LIRE`, `BOOT_FILE` (la liste attendue : `SD0:/etc/options.txt` ; init le donne), `DEMARRAGE_PRET` (le noyau
  applique les options : `no_irq`, `no_preload`). Le noyau lance `init` s'il est dans l'initrd (sinon `hello`).
* `SVC_MAP_CALLER` / `UNMAP_CALLER` / `CALLER_PHYS` : la mémoire de l'appelant vue par le service à
  `0x6E_0000_0000` + n × 1 Mo (16 vues par service), pages amenées et épinglées (une référence de plus), rendues par
  `SVC_REPLY`, `SVC_FORWARD` et la fin du service.
* `PROC_STATS` (compteurs d'appels du process), `SVF_MEMORY` (sous 64 Mo libres, `SVC_SIG_LOWMEM` aux services qui le
  demandent, une fois par seconde au plus ; pas encore d'attente de 2 s avant de refuser une page).

Essais : `btest` (10, `run btest`). Les cinq programmes, **116 essais, passent ensemble sous QEMU avec 1 et 16
cœurs** (`qemu_pc.py test --after 8` pour laisser le temps aux essais).

Jalon 9 fait le 11/11/2026, **essayé sous QEMU seulement** : canal du serveur graphique (`core/ws.c`).

* `WS_REGISTER` (le service « ws », prévenu des fins de process), `WS_CALL`, `WS_NEXT`, `WS_REPLY`, `WS_SIGNAL` (par
  les services nommés) ; `WS_POST` (serveur, ou service `SVF_POST`) et `EV_WAIT` (file de 64 événements de 32 octets
  par process, 0 sur un signal) ; `WS_DISPLAY` (le framebuffer GOP chez le serveur à `0x6D_0000_0000`, en écriture
  combinée ; la console du noyau se tait et reprend quand le serveur disparaît) ; `WS_MODE` (le mode GOP est fixé par
  l'UEFI : `E_IO`, la résolution reste, comme un refus du firmware du Pi) ; `SCREEN_INFO`, `CLIP_SET`, `CLIP_GET`.

Essais : `wtest` (12). Les six programmes, **128 essais, passent ensemble sous QEMU avec 1 et 16 cœurs** (`--after 25`).

### Parité des appels système

**Le noyau PC sert maintenant tous les appels système du noyau du Pi** (`0x00`–`0x7D` de `syscalls.inc`), avec les
mêmes numéros, arguments et résultats, sauf `FW_CALL` (firmware du VideoCore), qui n'a pas de sens sur PC et rend
`E_NOSYS`.

Écarts et manques connus, à reprendre quand un service ou une app en aura besoin :

* `VM_PROTECT` à `PROT_NONE` rend les pages (x86 n'a pas de page présente sans accès) ;
* une seule zone de mémoire (pas de zone basse sous 1 Go), `DMA_CACHED` sans effet (DMA cohérent) ;
* `SVF_MEMORY` : le signal est envoyé, mais une allocation qui échoue ne patiente pas 2 s comme sur le Pi ;
* un process trouvé par son pid peut être libéré pendant qu'on lui envoie un signal (compteur de références à
  ajouter) ; un arrêt n'interrompt pas tout de suite un thread qui tourne sur un autre cœur (pas d'IPI) ;
* un seul verrou pour tout l'IPC ; kits mappés hors du verrou de la mémoire du process ;
* `WS_MODE` refuse tout changement (le mode GOP est fixé par l'UEFI ; un pilote du GPU le permettra) ;
* l'horloge CMOS est lue comme du temps universel (Windows la règle à l'heure locale) ;
* sauvegarde AVX (`xsave`) pas faite : seul l'état SSE est sauvé.

Prochaine étape (décision du 11/11) : les services, les pilotes, les kits et les apps, réécrits en C ou en C++ communs
au Pi et au PC, puis un vrai démarrage sur le ZBook.

### Portage des kits en C (commencé le 11/11/2026)

Décisions du propriétaire (11/11) : les kits sont réécrits **en C** (pas en C++), communs au Pi et au PC ; chaque kit
est **vérifié d'abord sur le Pi** (il remplace le `.so` en asm, les apps et services existants prouvent qu'il se
comporte pareil), puis le même C est compilé pour x86-64.

Infrastructure :

| Fichier | Rôle |
|---|---|
| `src/kits/common/korin_kit.h` | appels système en ligne (`svc 0` sur AArch64, `syscall` sur x86-64), message des services, mémoire et chaînes |
| `src/kits/common/kitrt.c` | `memcpy`, `memset`, `memmove`, `memcmp`, `strlen` (sans bibliothèque) |
| `src/kits/common/kit.ld` | ELF64 lié à 0, en-têtes et code dans un segment lecture + exécution, variables sur leurs pages |
| `tools/kits/abi2table.py` | la table d'exportation depuis le `.abi` (« AKIT », nombre de fonctions, décalages de 32 bits), en assembleur GNU commun aux deux architectures |
| `tools/kits/build_c.sh` | construit `build/kits/aarch64/<kit>.so` et `build/kits/x86_64/<kit>.so` dans WSL ; refuse un kit qui aurait des relocations ou une GOT |
| `build.ps1` | copie `build/kits/aarch64/*.so` sur la carte à la place des kits en asm ; assemble le module `appkit.mod` de l'initramfs avec `-d APPKIT_C=1` quand `build/kits/aarch64/appkit.so` existe (`src/svc/modules/appkit.asm` prend alors l'AppKit en C) |
| `tools/pc/build.sh` | met `build/kits/x86_64/*.so` dans l'initrd (`LIB_OPEN` les y trouve) |

`src/kits/<kit>/kit.conf` (facultatif) : `keep = net_,wifi_` pour des fonctions qui gardent tous les registres sauf
`x0` (sur AArch64, la table pointe sur un relais généré qui sauve `x1`–`x15` et `x18`, comme la macro `keep_regs` de
l'asm), `cflags_aarch64 = -mgeneral-regs-only` pour un kit qui ne doit pas toucher aux registres flottants.
`keep_all = *` (ou des préfixes) : relais AArch64 qui garde `x1`–`x18` et les drapeaux NZCV (`mrs`/`msr nzcv`) ;
`keep_x0 = f_exec_resume` : garde aussi `x0` ; `x8_arg = file_sys` : le relais passe `x8` (numéro d'appel) en
7e argument (`x6`) ; sur x86-64, c'est le 7e argument System V. Ces relais supportent une fonction qui revient deux
fois (`fork` : le fils a une copie de la pile) ou jamais (`exec`). AppKit les utilise pour toutes ses fonctions :
`file_sys` remplace un `svc 0` dans la macro `sys` de tout programme en assembleur, et des appelants en
assembleur comptent sur des registres que l'asm ne touchait pas (`ftpd` garde `x10` autour de `fmt_dec`,
`threadtest` garde `x2` autour de `mtx_unlock`).

**Kit qui en appelle un autre** : `import = appkit` dans `kit.conf` ; `tools/kits/abi2import.py` génère des relais (AArch64 et x86-64) et `<kit>_import.h`. La table des adresses est dans le `.bss` du kit (une par process), remplie au premier appel par `LIB_OPEN` : aucune relocation. Essayé avec ArchiveKit.

**Noms en anglais** (12/11) : en portant un kit, ses fonctions exportées et les constantes publiques de son domaine passent en anglais dans tout le dépôt (`tools/rename_ids.py CARTE [--apply]` : hors chaînes, les commentaires seulement pour les noms composés, les .md seulement dans le code). Fait avec AppKit : `f_open`, `f_read`, `f_write`, `f_seek`, `f_close`, `f_readdir`, `f_pread`, `f_pwrite`, `f_exec_resume`, `file_sys`, `FILES_APPKIT` ; protocole des fichiers (`V_OPEN`, `FS_OPEN`, `FS_READ`, `FS_WRITE`, `FS_CLOSE`, `FS_READDIR`, `FS_BUFFER`, `FS_READ_BUF`, `FS_WRITE_BUF`, `FS_READ_DIRECT`, `FS_SEEK`, `FS_POS_CUR`, `FS_BUFFER_SIZE`, `FS_DATA_MAX`, `VFS_MOUNT`, `VFS_UNMOUNT`, `MSG_SIZE`, `PIPE_SHORT`, `ATTR_FILE`) ; noyau (`SYS_MOD_LAUNCH`, `SYS_MOD_READ`, `SYS_BOOT_READY`, `EXP_NAME`, `EXP_MARKS`, `EXP_NMARKS`, `SVC_OP_END`, `SVC_OP_BIRTH`, `SVF_ENDS`, `SVF_BIRTHS`, `SVC_PID_KERNEL`). Vérifié : tous les binaires en asm identiques avant et après, sauf trois programmes d'essai qui impriment ces noms (`fstest2`, `vfstest`, `sysstat`) ; `aktest -f`, `artest -f`, `exectest`, `forktest`, `pipetest` identiques avec l'asm et le C sur le Pi, `aktest` et `artest` sur PC.

**Vérification par comparaison** : chaque kit a un programme d'essai sans bibliothèque (`src/kits/<kit>/test/`, construit pour les deux architectures) qui appelle toutes ses fonctions et imprime des résultats indépendants des adresses et de l'heure ; sa sortie doit être identique sur le Pi avec l'asm, sur le Pi avec le C et sur PC. `tools/kits/run_pi.sh CARTE SORTIE commandes...` lance une carte dans QEMU raspi4b et rend la sortie par telnet (`RUN_PI_MODULES=a.mod,b.mod...` : d'autres modules de démarrage, par exemple un `appkit.mod` en asm pour comparer) ; `tools/pc/build.sh` met les programmes d'essai dans l'initrd du PC.

Les chargeurs de kits des deux noyaux ne font pas de relocations : un kit en C ne doit pas ranger de pointeur dans
ses données (tableau de chaînes, table de fonctions) ; `build_c.sh` le détecte. UIKit s'en passe (tables de
décalages, répartition par switch) ; quand un kit en aura besoin (AudioKit ?), il faudra appliquer les relocations relatives (`R_AARCH64_RELATIVE`, `R_X86_64_RELATIVE`) au chargement,
dans les deux noyaux.

| Kit | Asm | C | Pi | PC |
|---|---|---|---|---|
| BusKit | 281 lignes | `src/kits/buskit/buskit.c` | QEMU raspi4b : `bus pub`, `get`, `list`, `del` par telnet, services du démarrage (clockd, barre, notifications) | `kbtest` : 14 essais (faux busd) |
| NetKit | 967 lignes | `src/kits/netkit/netkit.c` | QEMU raspi4b : telnet lui-même, `ping`, `netstat`, `nslookup`, `wifi` (sans puce), `wget` de 100 000 octets (contenu vérifié) | `nktest` : 12 essais (faux netd, zones du tampon) |
| GpioKit | 1 594 lignes | `src/kits/gpiokit/gpiokit.c` | QEMU raspi4b : un programme BASIC (`basic`, simulateur : balayage I2C, BME280, sorties, pull-up, pull-down) donne la même sortie qu'avec l'asm (la mesure du BME280 dérive avec l'heure : valeur valide vérifiée) | `gktest` : 21 essais |
| DocumentKit | 2 702 lignes | `src/kits/documentkit/documentkit.c` | `dktest` (4 286 328 appels, 1 781 lignes) : sortie identique avec l'asm et avec le C | `dktest` : même sortie que le Pi |
| AppKit | 3 986 lignes (`appkit.asm` et ses `.inc`, plus les fonctions de `src/inc/lib.inc`) | `src/kits/appkit/appkit.c` (relais `keep_all` sur AArch64 ; aussi dans le module `appkit.mod` de l'initramfs) | `aktest -f` (367 lignes : chaînes, nombres, UTF-8, chemins, tas, verrous et conditions avec 8 threads, fork, `file_sys` avec contrôle des registres, langue et dictionnaire, fichiers et répertoires de `RAM0:`, tubes, spawn, `f_redir`, argv et environnement, scripts, kits, fork avec fichiers, exec) : identique avec l'asm et avec le C. Système par telnet (bash, `ls \| grep`, redirections, `cd`/`pwd`, `mkdir`/`rm` sur `RAM0:`, `ping`, `nslookup`, `exectest` 17/17, `forktest` 23/23, `pipetest` 31/31, `posixtest exec fork proc ipc` 73/73, `posixtest file io` 85 + 2 sautés) : mêmes sorties qu'avec l'asm, aux temps près ; bureau affiché | `aktest` (partie sans fichiers, 120 lignes) : même sortie que le Pi |
| ArchiveKit | 6 695 lignes | `src/kits/archivekit/*.c` (importe AppKit) | `artest` et `artest -f` (4 082 lignes : sommes, deflate à tous les niveaux et enveloppes, données abîmées, archives sur `RAM0:`) : identique avec l'asm et avec le C, octets compressés compris | `artest` (partie en mémoire, 2 022 lignes) : même sortie que le Pi |
| UIKit | 27 791 lignes (`uikit.asm`, `ttf.asm` et 25 `uikit_*.inc`) | `src/kits/uikit/*.c` (18 400 lignes, 27 fichiers, `uk.h`, `uk_consts.h` ; importe AppKit : les appels de fichiers passent par `file_sys` comme l'asm ; relais `keep_all`, `ret` pour les fonctions qui rendent plusieurs registres ; flottant seulement dans `uk_ttf.c`, `fp_files`) | `uitest -f` (2 031 lignes : thèmes, dessin, texte TTF, images, tous les widgets avec événements simulés, polices et icônes de `SD0:`, associations, vraie fenêtre, surfaces) : identique avec l'asm et avec le C ; captures du bureau (barre de menus, panel, widgets du bureau, menu) et des apps lancées (Calculatrice, Fichiers, Réglages, Éditeur, Budget en C++, uidemo, Courier en .NET), thèmes Givre et Onyx Noir : identiques au pixel près, sauf l'horloge | `uitest` (partie en mémoire, 1 915 lignes) : même sortie que le Pi sauf trois sommes de texte, où le PC donne le résultat IEEE exact (le même que l'hôte x86-64 et qemu-aarch64 10.2 avec les mêmes objets) et le Pi sous QEMU 11.1 un autre, avec l'asm comme avec le C |
| AudioKit | 31 492 lignes (`audiokit.asm` et 24 `ak_*.inc` : WAV, FLAC, MP3 et son encodeur, Ogg, Vorbis, Opus (SILK, CELT) et son encodeur, MIDI, FM, SoundFont, flux, lecteur) | `src/kits/audiokit/*.c` (20 200 lignes, 22 fichiers, `ak.h`, `ak_fp.h` : FMA, fmax/fmin, conversions saturées d'AArch64, FMA logicielle exacte sur x86-64 ; importe AppKit ; convention normale, aucun relais : aucun appelant asm ne compte sur d'autres registres ; `-ffp-contract=off`). Chaque codec d'abord comparé au bit près à l'asm sur un banc Linux (`src/kits/audiokit/test/cmp` : l'asm inchangé chargé sous `qemu-aarch64`, asm contre C puis x86-64 contre AArch64 : ~150 000 sorties, fichiers fabriqués et abîmés) | `audtest -f` (739 lignes : décodage de tous les formats en mémoire et par fichiers, coupés, abîmés, recherches, étiquettes, API par paquets S16/F32, encodage WAV/MP3/Opus et relecture, FM, synthé MIDI en banque FM et SoundFont, outils, flux et capture du service sound, lecteur, FM en direct) : identique avec l'asm et avec le C sous le QEMU de Linux (WSL) ; sous le QEMU de Windows, la FMA de QEMU dépend de l'état (`audtest` le détecte et ne compare alors que le S16) : identique aussi. `synthtest` PASS identique, `codectest` sur les 8 fichiers d'essai, `play`, `sontest 22050`, `soundctl`, `volume` ; mediaplayer, music, piano, fmtracker, media, tetris, snake, mixer, recorder démarrés ensemble et vivants après 20 s, mêmes clients du son qu'avec l'asm | `audtest` (partie en mémoire, 605 lignes) : même sortie que le Pi (QEMU de Linux), asm et C |
| PrinterKit | 2 245 lignes (`printerkit.asm`) | `src/kits/printerkit/printerkit.c` (importe AppKit, UIKit et ArchiveKit ; relais `keep_all` : l'applet Imprimantes de Réglages est en asm ; ouvre les kits comme `kits_open`, fin 127 si l'un manque) | `pktest -L -o RAM0:/pk.txt -f` (lancé en `SD0:/sys/pktest` par init2 : il joue lui-même printd, sommes de chaque message reçu ; vrais travaux sur `RAM0:`, fichier des pages décrit enregistrement par enregistrement, images moyennées et compressées) et `pktest -r` (vrai printd, imprimante PDF, PDF relu et sommé) : 326 lignes identiques avec l'asm et avec le C ; captures : Paint (Ctrl+P : dialogue d'impression, enregistrement du PDF), Réglages > Imprimantes | `pktest` (sans service et faux service, 146 lignes) : même sortie que le Pi |
| EmuKit | 1 736 lignes (`emukit.asm`) | `src/kits/emukit/emukit.c` (importe AppKit, UIKit et AudioKit ; relais `keep_all` : les émulateurs en asm appellent `emu_frame_size` et `emu_pad_axes` depuis leur image ; horloge : `CNTPCT_EL0` sur le Pi, millisecondes du noyau sur PC) | `emtest -f` (petites fonctions, `emu_thread`, `emu_run --thumb` sur un faux émulateur : ROM, sauvegarde, boutons, son, vignette sommée, `EMD_BUFS`, `emu_rom_by_path`, erreurs) : identique avec l'asm et avec le C ; captures : NES sans ROM (dialogue d'ouverture) et avec une ROM d'essai faite pour l'occasion | `emtest` (partie en mémoire, 16 lignes) : même sortie que le Pi |

L'asm de BusKit reste dans le dépôt tant que le C n'est pas construit par la chaîne de l'OS (auto-hébergement) ; la
carte prend le C. Ordre selon les imports (12/11) : sans import, DocumentKit et AppKit ; important AppKit, ArchiveKit, AudioKit et UIKit ; important UIKit, PrinterKit et EmuKit. **GpuKit n'est pas porté** (décision du 12/11) : il reste en asm sur le Pi (V3D), et le PC aura sa propre version, même `.abi`, une fois écrits les pilotes de ses GPU : l'Intel d'abord, idéalement la NVIDIA aussi (décision du 12/11). Les plus gros : UIKit (27 571) et AudioKit
(31 577). MediaKit et GlKit sont déjà en C (Mesa, code porté).

Pièges : le chargeur de kits du PC refusait un segment de variables vide dans le fichier (`.bss` seul, décalage
au-delà de la fin) : corrigé dans `kits.c`, `elf.c` et `image.c`. Telnet sur la carte d'essai de QEMU :
`password = essai` dans `SD0:/etc/remote.txt`, `ARMPI_REMOTE_PW=essai`, et attendre la fin du démarrage (environ 80 s).

Le dossier `src/kernel/core` est le début du noyau commun : le Pi pourra l'adopter morceau par morceau, en fournissant
`korin/arch.h` depuis son code ARM64. Le moniteur et le clavier dans le noyau sont **provisoires** : dans le micronoyau,
le clavier ira dans un service dès qu'il y aura des process.

Construire, essayer, copier sur la carte :

```
wsl sh tools/pc/build.sh
python -I tools/pc/qemu_pc.py run
powershell -File tools\pc\sd.ps1 E:
```

Sur la carte : `EFI\BOOT\BOOTX64.EFI` et `korin\kernel.elf`. Le chargeur lit le noyau par l'UEFI, avant d'en sortir :
le noyau n'a donc pas besoin de pilote du lecteur de cartes pour démarrer. Pour lire d'autres fichiers, le chargeur
devra charger un disque en mémoire (ramdisk) de la même façon, en attendant un pilote du lecteur Realtek (ou de l'USB).
Le HP doit permettre de démarrer sur la carte SD (menu F9) ; sinon, la même copie marche sur une clé USB.

Les décisions qui restent à prendre sont rassemblées à la fin (§11).

---

## 1. La machine

Relevé fait sous Windows (WMI) le 11/11/2026.

| Élément | Matériel | Identifiant PCI |
|---|---|---|
| Machine | HP ZBook Fury 17.3 G8 (carte mère HP `886D`), 32 Go | |
| Processeur | Intel Core i7-11800H (Tiger Lake-H, 8 cœurs, 16 fils) | |
| Micrologiciel | **UEFI** | |
| GPU intégré | Intel UHD Graphics (Tiger Lake, **Gen12 Xe-LP**, 32 EU) | `8086:9A60` |
| GPU séparé | NVIDIA T1200 Laptop (Turing TU117) | `10DE:1FBC` |
| Son | contrôleur HDA / Smart Sound d'Intel + codec **Realtek ALC285** ; HDA de la NVIDIA (HDMI) | `8086:43C8`, codec `10EC:0285` |
| USB | xHCI du PCH (USB 3.2) et xHCI Thunderbolt 4 du processeur (USB 3.1) | `8086:43ED`, `8086:9A17` |
| Wi-Fi | **Intel Wi-Fi 6 AX201** 160 MHz (CNVi) | `8086:43F0` |
| Ethernet | Intel I219-V (famille e1000e) | `8086:15FA` |
| Disque | NVMe SK hynix PC711 512 Go (**porte Windows**) | |

Le clavier interne est très probablement en PS/2 (contrôleur i8042 émulé par l'EC) et le pavé tactile en **I2C-HID**
(bus I2C LPSS d'Intel) ; à confirmer par les tables ACPI.

---

## 2. Ce qui existe aujourd'hui

Comptes de lignes du dépôt (sources `.asm`/`.inc` d'un côté, `.c`/`.cpp`/`.h`/`.cs` de l'autre) :

| Zone | Assembleur ARM64 | C / C++ / C# |
|---|---:|---:|
| `src/kernel` | 21 752 | 0 |
| `src/svc` | 54 079 | 0 |
| `src/sys` | 27 268 | 3 888 |
| `src/kits` | 83 634 | 386 |
| `src/bin` | 185 130 | 279 |
| `src/apps` | 99 305 | 225 232 |
| `src/lib` | 0 | 32 288 |
| `src/inc` | 12 823 | 813 |
| **Total** | **≈ 484 000** | **≈ 263 000** |

Plus grands kits : AudioKit 31 577 lignes (surtout les codecs MP3, Vorbis, Opus, FLAC), UIKit 27 571, ArchiveKit
6 695, GpuKit 3 942, AppKit 3 922. Plus grands services : `usbd` 6 145, `netd` 6 131, `fatfs` 5 572, `wifi` 4 957,
`vfs` 3 760.

Ce qui aide :

* `src/kernel/arch/{common,pi4,pi5}` sépare déjà ce qui dépend de la **carte** (`docs/ARCHITECTURE.md`, « Interface
  d'une carte »). Mais tout le reste du noyau suppose AArch64 : il faut une séparation de plus, celle du
  **processeur**.
* Les pilotes sont déjà **hors du noyau**, dans des services (`usbd`, `sound`, `wifi`, `v3d`…) : leur interface avec
  le reste du système (`src/inc/usb.inc`, `sound.inc`, `netd.inc`…) ne change pas.
* Le montage Mesa du Pi (`tools/mesa/libdrm`, faux libdrm qui sert de winsys) est le modèle de la 3D sur PC (§7.4).
* `tools/x86toa64.py` a déjà traduit FASM de x86 vers ARM64, à l'octet près : un traducteur de source à source a déjà
  marché une fois dans ce projet (§9).

---

## 3. Forme du portage

### 3.1 Une deuxième architecture, pas une troisième carte

Proposition :

| Dossier | Contenu |
|---|---|
| `src/kernel/arch/aarch64/` | ce qui dépend d'ARM64 et qui est aujourd'hui mêlé au code générique (vecteurs, MMU, changement de contexte, GIC, appels système) |
| `src/kernel/arch/x86_64/` | la même chose pour x86-64 |
| `src/kernel/arch/pc/` | la « carte » PC : UEFI, ACPI, PCIe, APIC, console GOP |

`build.ps1 -d ARCH=pc` choisit l'assembleur (**FASM** pour x86-64, FASMARM pour ARM64) et les dossiers.

### 3.2 L'assembleur

**FASM 1.73** sait produire de l'ELF64 x86-64 et du PE64 (pour l'application UEFI, §4.1). C'est l'outil d'origine
de FASMARM : mêmes macros, mêmes `struc`, mêmes directives. **Attention** : dans FASM d'origine, `dw` = 16 bits et
`dd` = 32 bits (`dq` = 64), alors que dans FASMARM `dw` = 32 bits et `dd` = 64 bits. Les sources x86 doivent suivre la
convention de FASM, et les en-têtes partagés entre les deux (structures publiques, `syscalls.inc`) ne doivent pas
employer `dw`/`dd` sans macro de taille commune (`d32`, `d64`…).

Auto-hébergement : FASM est lui-même écrit en assembleur x86 ; il tournerait **nativement** sous Korin x86, avec la
même couche d'appels système que celle du port de FASMARM.

### 3.3 La chaîne C

Une cible `x86_64-korin-elf` pour GCC 14.2, sur le modèle d'`aarch64-onyx-elf` (WSL), avec newlib et `libposix`. À
vérifier : les morceaux de `libposix` en assembleur ou en asm en ligne (souches d'appels système, `setjmp`, signaux),
qui sont à réécrire. Tout le C, le C++, le C# (le runtime .NET a son JIT x64), FreeBASIC, Mesa, SDL, Skia, Avalonia,
OpenSSL, WebKit se recompilent.

---

## 4. Le noyau x86-64

Le noyau compte 21 752 lignes ; tout ce qui suit est à écrire, le reste (ordonnanceur, process, IPC, `SVC_FORWARD`,
signaux, mmap) se transpose en gardant la même logique.

### 4.1 Démarrage

* Une petite **application UEFI** en FASM (PE64), `BOOTX64.EFI` sur une clé USB FAT32 : elle lit le **framebuffer
  GOP**, la carte mémoire, l'adresse des tables ACPI (RSDP), charge le noyau ELF et les fichiers de démarrage, appelle
  `ExitBootServices` et saute dans le noyau **déjà en mode long** (pas de code 16 bits pour le cœur 0).
* Le framebuffer GOP remplace `fb_init` (mailbox du VideoCore). Sa résolution est celle que l'UEFI a choisie
  (l'écran interne en natif).

### 4.2 Processeur

| Fonction | ARM64 aujourd'hui | x86-64 |
|---|---|---|
| Vecteurs d'exception | table `VBAR_EL1` | **IDT** (256 entrées), **GDT**, **TSS** (piles IST pour les fautes doubles et les NMI) |
| Tables de pages | 4 niveaux, granule 4 Ko, `TTBR0/1` | 4 niveaux (PML4), pages de 4 Ko, 2 Mo, 1 Go, `CR3`, bits NX, PCID si utile |
| Appels système | `svc #0` | `syscall`/`sysret` (MSR `LSTAR`, `STAR`, `FMASK`), `swapgs` |
| Contexte FP | NEON (`q0`–`q31`) | **XSAVE**/`XRSTOR` (SSE, AVX2 ; AVX-512 existe sur ce processeur) |
| Interruptions | GIC-400 | **LAPIC** + **IOAPIC**, et surtout **MSI/MSI-X** pour les périphériques PCIe |
| Minuterie | timer générique ARM | minuterie du LAPIC en mode TSC-deadline, TSC invariant pour l'heure, HPET en secours |
| SMP | spin table, PSCI | `INIT`-`SIPI`-`SIPI` vers les cœurs de la MADT, avec un **trampoline 16 bits** sous 1 Mo |
| Atomiques et verrous | `ldaxr`/`stlxr`, `dmb` | `lock cmpxchg`, `xadd`, `pause` ; le modèle mémoire de x86 est plus fort : la plupart des barrières disparaissent |
| Invalidation du TLB sur les autres cœurs | `tlbi …is` (diffusé par le matériel) | **IPI** de « shootdown » (logiciel) |
| Caches et DMA | entretien explicite (`dc civac`) | DMA **cohérent** : l'entretien des caches disparaît des pilotes |

### 4.3 Carte PC

* **ACPI, tables statiques** : MADT (cœurs, APIC), MCFG (adresse ECAM du PCIe), FADT, HPET, DMAR (à ignorer au
  début : laisser l'IOMMU éteint). Suffisant pour démarrer.
* **ACPI, AML** : il faut un interpréteur pour l'extinction (`_S5`), la batterie, le capot, les boutons, les
  périphériques I2C (pavé tactile) et parfois le routage des interruptions. **ACPICA** (C, double licence BSD/GPL) se
  compile en service ; l'écrire en asm n'a pas d'intérêt.
* **PCIe** : énumération par ECAM, BAR, MSI-X ; un service `pcid` qui remplace la découverte par adresses fixes du Pi.
* Console série : ce PC n'en a pas. Débogage par l'écran GOP, par QEMU (`-serial`) et plus tard par le réseau.

---

## 5. L'ABI des kits et des appels système

### 5.1 Convention d'appel

Proposition : **System V AMD64**, celle de GCC, pour que le C appelle les kits sans adaptateur.

| | ARM64 (aujourd'hui) | x86-64 |
|---|---|---|
| Arguments entiers | `x0`–`x7` | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`, puis **la pile** |
| Arguments flottants | `v0`–`v7` | `xmm0`–`xmm7` |
| Retour | `x0` (`x1`) | `rax` (`rdx`) |
| Registres sauvegardés par l'appelé | `x19`–`x28`, `x29`, `x30` | `rbx`, `rbp`, `r12`–`r15` |
| Alignement de la pile | 16 octets | 16 octets **avant le `call`** |
| Appel système | `x8` = numéro, `x0`… | `rax` = numéro, `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` (`rcx` et `r11` détruits par `syscall`) |

Conséquences :

* une fonction de kit à 7 ou 8 arguments les reçoit par la pile sur x86 ; la macro d'entrée des kits s'en charge une
  fois pour toutes ;
* 16 registres au lieu de 31 : la logique des kits ne se traduit pas ligne à ligne, elle se réécrit (variables sur la
  pile, boucles repensées) ;
* les numéros d'appels système (`src/inc/syscalls.inc`) restent les mêmes ; seule la macro `sys` change.

### 5.2 Le `.abi` comme source unique

Les deux architectures partagent les tables `.abi` (même ordre, même numérotation, « on ajoute à la fin » pour les
deux à la fois). Les en-têtes C, les `.bi` de FreeBASIC, les déclarations C# et les souches de chaque architecture
devraient en être **générés**, pour que les deux versions ne puissent pas diverger.

### 5.3 Structures publiques

Les deux architectures sont en 64 bits little-endian : les structures publiques (événements, blocs de fenêtre,
tampons audio…) gardent les **mêmes champs aux mêmes décalages**. Il reste à vérifier les alignements des champs de
8 octets et des `double`, qui suivent les mêmes règles dans les deux ABI.

### 5.4 Tests communs

Une suite de tests en **C**, par kit, qui appelle l'API et compare les résultats (images rendues par UIKit, sorties des
codecs d'AudioKit, archives d'ArchiveKit…), sans rien de propre à ARM. Recompilée pour les deux architectures, elle
prouve que la version x86 se comporte comme l'originale. C'est la condition pour réécrire 84 000 lignes de kits sans
dériver.

---

## 6. QEMU d'abord

QEMU `-M q35` avec le micrologiciel **OVMF** (UEFI) émule, sur ce même PC, presque tout ce qu'il faut pour la première
moitié du travail, sans redémarrer :

| Besoin | Émulé par QEMU | Sur le ZBook |
|---|---|---|
| UEFI, GOP, ACPI | OVMF | UEFI d'HP |
| USB | `qemu-xhci` | xHCI Intel |
| Son | `intel-hda` + `hda-duplex` | HDA + ALC285 |
| Réseau filaire | `e1000e` (82574) | I219-V (même famille, autre PHY), **sans câble** chez le propriétaire |
| Réseau USB | `usb-net` (ECM, RNDIS) | partage de connexion d'un téléphone |
| Stockage | `usb-storage`, NVMe, AHCI | clé USB |
| Clavier | PS/2 | i8042 (EC) |
| 3D | aucun GPU Intel | Gen12 |
| Wi-Fi | aucun | AX201 |

Avec KVM (WSL2 ou Linux), QEMU x86 tourne à vitesse native : bien plus rapide que l'émulation `raspi4b`.

---

## 7. Les quatre objectifs

### 7.1 USB : facile

* Deux contrôleurs **xHCI** Intel. Korin en a déjà un pilote (`src/svc/usbd/xhci.inc`, pour le VL805 du Pi 4 et le RP1
  du Pi 5) : même standard, même logique, à réécrire en x86 avec le PCIe et les MSI-X.
* Le xHCI Thunderbolt (`9A17`) sert les ports USB-C ; il se programme comme un xHCI ordinaire. Le multiplexage des
  ports USB-C est fait par le micrologiciel (PMC) ; le Thunderbolt lui-même est hors sujet.
* Les classes USB déjà écrites (`usbkeyboard`, `usbmouse`, `usbstorage`, `usbaudio`, `usbnet`, `usbgamepad`) passent
  par `usb.inc` et se réécrivent sans changer d'interface.
* Le DMA est cohérent sur x86 : tout l'entretien des caches du pilote du Pi disparaît.

### 7.2 Son : moyen

* Contrôleur **HDA** d'Intel, en mode HDA classique (pas le DSP Smart Sound) : CORB/RIRB pour parler au codec, flux
  BDL en DMA. La spécification Intel HDA est publique.
* Codec **Realtek ALC285** : parcours du graphe de widgets (spécification HDA), puis les réglages propres à HP
  (GPIO de l'ampli des haut-parleurs, DEL de sourdine, coefficients du codec). Les correctifs de Linux
  (`patch_realtek.c`, GPL) servent de **carte**, sans copie, comme Circle pour le Pi.
* Obtenus ainsi : **haut-parleurs, prise casque, micro de la prise casque**. Le casque USB passe par `usbaudio`.
* Hors de portée au début : les **micros numériques intégrés** (DMIC), reliés au DSP, qui demandent son firmware
  (SOF). Le son HDMI (HDA de la NVIDIA ou de l'Intel) est pour plus tard.
* Le service `sound` et AudioKit ne changent pas d'interface ; seul un pilote `hdaudio` s'ajoute, à côté de
  `pwmaudio` et `hdmiaudio`.

### 7.3 Réseau : le Wi-Fi dès le premier démarrage

Le propriétaire **n'a pas de câble Ethernet** : sur le ZBook, le seul réseau est le Wi-Fi. Le pilote de l'AX201 fait
donc partie de la **première version** qui démarre sur la machine, pas de la fin. Or `netd`, telnet, FTP, le dépôt de
paquets et le débogage à distance dépendent tous du réseau.

**Ethernet I219-V : seulement sous QEMU.** Le pilote e1000e (descripteurs en anneaux, documentation Intel publique)
sert à mettre au point `netd` sous QEMU (`-device e1000e`). Le PHY de l'I219 passe par MDIO et diffère de l'82574 de
QEMU ; la version du ZBook n'est pas prioritaire.

**Filet de secours : le partage de connexion USB d'un téléphone.** `usbnet` ne gère aujourd'hui que **CDC-ECM**. Les
téléphones Android partagent leur connexion en **RNDIS** (anciens) ou **CDC-NCM** (récents) ; l'iPhone utilise un
protocole d'Apple, hors sujet. Ajouter NCM, et RNDIS si besoin, à `usbnet` donne un réseau sur le ZBook **sans
dépendre du Wi-Fi**, pour mettre au point le Wi-Fi lui-même à distance. Ce sont de petits ajouts à un pilote existant,
mis au point sous QEMU (`usb-net` sait aussi faire RNDIS).

**Mise au point sans QEMU.** QEMU n'émule aucune puce Wi-Fi Intel (et le passage du vrai AX201 à une machine virtuelle
demande Linux et VFIO, pas Windows). Le pilote se met au point **sur le ZBook** : traces à l'écran, journal écrit sur
la clé USB, puis par le réseau du téléphone (ci-dessus) dès qu'il marche.

**Commencer par un réseau ouvert.** Un point d'accès sans mot de passe (partage du téléphone en mode ouvert, le temps
des essais) sépare les deux problèmes : d'abord le firmware, le balayage et l'association, ensuite seulement le WPA2.

**Wi-Fi AX201 : le plus dur des quatre objectifs.**

Sur le Pi, la puce Broadcom est « FullMAC » : son firmware fait l'association **et** le WPA2 (supplicant `idsup` de la
puce). Korin n'a donc **ni pile 802.11 ni supplicant**. Avec l'AX201, le système doit fournir :

| Morceau | Contenu |
|---|---|
| Transport PCIe | chargement du firmware (`iwlwifi-QuZ-a0-hr-b0-*.ucode`, binaire redistribuable, ≈ 1,5 Mo), anneaux de commandes et de trames (TFD), « context info », interruptions MSI-X |
| Commandes du firmware | jeu de commandes « MVM » : contexte PHY, MAC, station, balayage, clés ; le firmware fait l'adaptation du débit et le chiffrement matériel |
| Pile 802.11 (MLME) | balayage, authentification, association, gestion des trames, itinérance plus tard |
| Supplicant WPA2-PSK | PBKDF2-SHA1 pour la clé, poignée de main en quatre temps (EAPOL), dérivation PTK/GTK, pose des clés dans le firmware ; **WPA3-SAE** plus tard si la box l'exige |
| Règlementation | domaine de régulation, canaux permis |

Références : **`iwx(4)` et `net80211` d'OpenBSD**, sous licence **ISC**, qui gèrent l'AX200/AX201 et contiennent une
pile 802.11 et un supplicant WPA2 complets. Le pilote `iwlwifi` de Linux (GPL) ne sert que de carte.

La crypto du supplicant (SHA-1, HMAC, AES key wrap) peut venir d'OpenSSL, déjà porté, si le supplicant est un service
en C ; ou être écrite en asm x86 (AES-NI, SHA-NI existent sur ce processeur). Comme le Wi-Fi vient tôt, le supplicant
en C impose d'avoir la chaîne `x86_64-korin-elf` et `libposix` **avant** le premier démarrage sur le ZBook (§11) ; en
asm, il n'attend que le noyau. Le reste de la crypto est petit : SHA-1, HMAC-SHA1, PBKDF2 et AES key wrap tiennent en
quelques centaines de lignes.

Note : `armpi-wifi-coupures` (mémoire du projet) soupçonnait déjà l'AX201 de ce PC dans les pertes d'accusés vues
depuis le Pi ; sans rapport direct, mais c'est la même puce.

### 7.4 3D : moyenne à dure, sur le GPU Intel

**Ce qu'on vise** : le GPU **Intel Gen12** intégré. La NVIDIA T1200 est laissée de côté : en mode hybride, l'écran
interne est branché sur l'Intel, et la NVIDIA ne servirait qu'à calculer pour lui (plus tard, si jamais).

**Côté Mesa : rien à écrire.** Mesa a déjà **iris** (OpenGL ES et OpenGL) et **anv** (Vulkan) pour Tiger Lake. C'est du
C, recompilé pour `x86_64-korin-elf`.

**Côté système : le même montage que sur le Pi.** Sur le Pi, un faux libdrm (`tools/mesa/libdrm`) imite les ioctls de
v3d et parle au service `v3d`. Sur PC, le faux libdrm imite l'interface **`i915`** (ou `xe`) et parle à un service
`i915` :

| ioctl | Rôle |
|---|---|
| `GETPARAM`, `QUERY` | identité de la puce, topologie des EU, capacités |
| `GEM_CREATE`, `GEM_MMAP_OFFSET`, `GEM_SET_DOMAIN`, `GEM_WAIT`, `GEM_CLOSE` | tampons dans la mémoire du système |
| `GEM_CONTEXT_CREATE`, `VM_CREATE` | contextes et espaces d'adresses du GPU (PPGTT) |
| `GEM_EXECBUFFER2` | soumission des lots de commandes |
| `SYNCOBJ_*` | synchronisation |

Le service doit : prendre la main sur le GPU (« forcewake »), monter la GGTT et les PPGTT, démarrer le moteur de rendu
et soumettre par **execlists** (le mode par défaut de Tiger Lake ; le firmware GuC n'est pas nécessaire), gérer les
interruptions de fin de lot. Intel publie la documentation de Tiger Lake (PRM, volumes du moteur de commandes, de la
mémoire et des registres).

**L'affichage reste au GOP.** On ne touche **pas** au moteur d'affichage de l'Intel (modes, ports eDP/HDMI, horloges,
firmware DMC) : c'est la plus grosse part d'`i915` et on peut s'en passer. L'UEFI a déjà allumé l'écran interne en
résolution native ; son framebuffer est dans la mémoire « volée » (stolen) du GPU, que le GPU peut viser par la GGTT.
Le rendu finit donc dans le framebuffer GOP, par une copie du GPU (blitter) ou directement. Prix : pas de changement
de résolution, pas de deuxième écran, pas de synchronisation verticale fiable au début.

Rétroéclairage : laissé allumé par l'UEFI ; son réglage passe par ACPI ou le registre PWM du GPU (plus tard).

Puissance : 32 EU Gen12, largement au-dessus du V3D du Pi 4 : Avalonia, Jet avec WebGL, SDL et les émulateurs en
profitent.

---

## 8. Ce qu'il faut en plus

| Besoin | Matériel | Difficulté | Remarque |
|---|---|---|---|
| Clavier interne | i8042 (PS/2) | facile | QEMU l'émule |
| Pavé tactile | **I2C-HID** sur l'I2C LPSS d'Intel | moyenne | trouvé par ACPI (`_HID` `PNP0C50`) ; le format HID est déjà lu pour l'USB |
| Extinction, batterie, capot, boutons | ACPI (AML) | moyenne | ACPICA (§4.3) |
| Stockage | clé USB (`usbstorage`) | facile | voir §10 pour le NVMe |
| Heure | horloge CMOS (RTC), puis NTP | facile | Korin est en UTC, la RTC du PC sous Windows est en heure locale |
| Économie d'énergie | P-states, C-states | plus tard | le processeur reste à sa fréquence de base au début |

---

## 9. Les programmes en assembleur hors des kits

Les commandes de `/bin` (185 000 lignes), les services (54 000), les programmes système (27 000) et les apps en asm
(99 000) sont aussi de l'ARM64. La règle des kits (même interface, logique réécrite) ne les couvre pas. Trois voies,
qui peuvent se combiner :

1. **Réécriture en asm x86**, programme par programme, en commençant par ce qui démarre le système (`init`, `vfs`,
   `fatfs`, `ramfs`, `netd`, Elegant, `bash` est déjà en C).
2. **Passage en C** pour ce qui doit exister sur les deux architectures sans doubler l'effort.
3. **Traducteur de source à source ARM64 → x86-64**, dans l'esprit de `tools/x86toa64.py` (qui a fait le chemin
   inverse) : la plupart des appels passent par des macros (`sys`, appels de kits), ce qui aide ; le manque de
   registres oblige à loger `x16`–`x28` en mémoire, le code est plus lent mais juste. Utile pour les centaines de
   petites commandes et les jeux, pas pour le noyau ni les kits.

Une règle peu coûteuse dès aujourd'hui : écrire les **nouveaux** services et outils en C plutôt qu'en asm ARM, quand
rien ne l'empêche, et ne plus mêler du code propre à AArch64 au code générique du noyau.

---

## 10. Sécurité de la machine

* **Ne jamais écrire sur le NVMe** : il porte Windows et les données du propriétaire. Korin démarre et vit sur une
  **clé USB**. Le pilote NVMe, s'il vient un jour, est d'abord en lecture seule. C'est la même règle que pour `SD1:`
  sur le Pi.
* **BitLocker** : si le chiffrement de Windows est actif, désactiver le Secure Boot ou démarrer autre chose peut faire
  demander la **clé de récupération** au démarrage suivant de Windows. Le propriétaire doit vérifier qu'il l'a avant
  de toucher au BIOS.
* Réglages du BIOS, à faire par le propriétaire : démarrage sur clé USB, **Secure Boot désactivé** (ou signature de
  `BOOTX64.EFI` plus tard), graphisme en mode **hybride** (écran interne sur l'Intel).
* Ne pas toucher au micrologiciel de l'EC ni aux variables UEFI autres que celles du démarrage.

---

## 11. Jalons proposés

| Jalon | Contenu | Où |
|---|---|---|
| 0 | séparation `arch/aarch64` dans le noyau actuel, sans changer de comportement ; règles de taille communes (`d32`, `d64`) | Pi, QEMU `raspi4b` |
| 1 | `BOOTX64.EFI`, noyau x86-64 qui affiche sur le GOP, IDT, pagination, LAPIC, clavier PS/2 | QEMU `q35` + OVMF |
| 2 | process ELF x86-64, appels système, SMP, ramfs, `vfs`, `fatfs` sur clé USB (xHCI + `usbstorage`) | QEMU |
| 3 | chaîne `x86_64-korin-elf`, newlib, `libposix`, `bash` ; ACPICA en service | QEMU |
| 4 | `netd` sur e1000e ; `usbnet` avec NCM et RNDIS | QEMU |
| 5 | premier démarrage sur le ZBook, **en mode texte** : clé USB, écran GOP, clavier, partage de connexion du téléphone | ZBook |
| 6 | **AX201** : firmware, commandes MVM, balayage et association sur un réseau ouvert, puis pile 802.11 et supplicant WPA2 | ZBook |
| 7 | kits réécrits (AppKit, UIKit d'abord) avec la suite de tests commune ; Elegant, bureau ; HDA + ALC285 | QEMU puis ZBook |
| 8 | service `i915` + faux libdrm, Mesa iris, puis Avalonia et Jet sur le GPU | ZBook |
| 9 | pavé tactile I2C-HID, batterie, extinction, puis le reste des programmes (§9) | ZBook |

Le Wi-Fi passe avant l'interface graphique : sans lui, le ZBook n'a pas de réseau, et tout le reste se met au point
plus vite avec telnet, FTP et le dépôt de paquets. Le jalon 5 n'a besoin d'aucun kit graphique.

Ordre de grandeur relatif, du plus petit au plus gros : USB < réseau USB < son < noyau x86 < 3D < Wi-Fi < kits <
programmes asm hors kits.

---

## 12. Décisions à prendre

1. **Convention d'appel des kits sur x86** : System V AMD64 (proposée) ou une convention propre.
2. **Génération** des en-têtes et souches depuis les `.abi` : oui ou non, et avant le portage.
3. **Programmes asm hors kits** (§9) : réécriture, passage en C, traducteur, ou un mélange (lequel pour quoi).
4. **Nouveaux programmes en C** dès maintenant quand c'est possible : oui ou non.
5. **Supplicant WPA2** : service en C avec OpenSSL (la chaîne C doit alors précéder le premier démarrage sur le
   ZBook), ou asm x86 (il n'attend que le noyau).
6. **Réseau de secours** : ajouter NCM et RNDIS à `usbnet` pour le partage de connexion d'un téléphone, oui ou non ;
   et quel téléphone (Android ou iPhone) est disponible.
7. **AudioKit** : réécrire les codecs (≈ 30 000 lignes) en asm x86, ou les compiler depuis des bibliothèques C sous
   la même interface.
8. **Nom de la carte** dans `build.ps1` (`pc`, `zbook`…) et place de l'étude dans `docs/FEUILLE-DE-ROUTE.md`.

GpioKit : sur PC, `gp_now_us` est l'heure en millisecondes × 1000 (pas encore d'horloge à la microseconde) : le PWM simulé et l'heure des fronts y sont plus grossiers.
