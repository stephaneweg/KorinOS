# Durcissement N3 à N5 : comment les corriger (étude technique, 27/11/2026)

Suite de l'audit [AUDIT-SECURITE-PC-2.md](AUDIT-SECURITE-PC-2.md), points N3 (droits du matériel, IOMMU), N4 (ASLR,
SMEP/SMAP/UMIP, W^X) et N5 (cloisonnement de Jet). Faite sur le code de main au 27/11 (pc 2026.11.124) ; les numéros
de ligne sont ceux de ce jour. Rien n'est encore codé : chaque section dit ce qui existe, ce qu'il faut changer,
dans quel ordre, et ce que ça casse. Le Pi (noyau en assembleur, `src/kernel/*.asm`) n'est pas concerné, sauf mention.

## 0. Ordre conseillé

| Étape | Contenu | Taille | Risque |
|---|---|---|---|
| **0** | correctif tout de suite : vfs ne lance plus n'importe quel `/sys/svc/<x>` pour un montage d'un compte (§3.1) ; oauthd et gdrivefs sans droits (§3.1) ; SMEP + UMIP (§1.1) ; JIT de Jet réduit par l'environnement (§2.1) | 1 à 2 jours | faible |
| **1** | SMAP (§1.2) ; ASLR de la pile, de l'arène et des kits (§1.4) ; capacités dans le noyau, d'abord en journal (§3.2) | 1 à 2 semaines | moyen |
| **2** | uid de bac à sable et process web de Jet dedans (§2.2) ; cdmhost lancé par l'interface (§2.3) | 1 à 2 semaines | moyen |
| **3** | W^X, journal puis refus (§1.3) ; fenêtre des tampons à adresse tirée (§1.5) ; matériel accordé par appareil et maître de bus coupé à la mort du pilote (§3.3) | 2 à 3 semaines | moyen |
| **4** | VT-d (§3.4) ; exécutables PIE (§1.6) | 1 mois et plus | élevé |

**Étape 0 faite le 27/11** (§5). Chaque étape se publie seule ; les étapes 1 et 3 commencent en mode « journal » (comme E33 et `access = log`), pour
voir sur le ZBook ce qui serait refusé avant de refuser.

---

## 1. N4 : protections de la mémoire

### 1.1 SMEP et UMIP (étape 0)

**Ce qui existe.** `cpu_init` (`src/kernel/arch/x86_64/cpu.c:148-189`) ne met dans CR4 que OSFXSR, OSXMMEXCPT et
OSXSAVE (`:175-181`). CR0.WP est déjà mis (`:173`), EFER.NXE aussi (`paging.c:211`). Le noyau n'exécute jamais de
code en page utilisateur (pas de vDSO ; les gestionnaires de signaux tournent en anneau 3, `sigframe.c:97-98`).
Aucun programme n'utilise `sgdt`, `sidt`, `sldt`, `smsw` ni `str` en anneau 3 (seules les tables de l'émulateur
i386 de MAME les nomment, interprétées).

**Le piège.** La faute de page (`src/kernel/arch/x86_64/idt.c:87-103`) appelle `mm_fault` pour toute adresse sous
`0x800000000000`, **quel que soit CS**. Une violation SMEP (ou SMAP) arrive sur une page présente avec les bons
droits : `mm_fault` rend 0 (`mm.c:338-341`), le gestionnaire revient, l'instruction refaute : **boucle infinie**.

**À faire.**
1. Détecter : `cpuid(7, 0)` → EBX bit 7 (SMEP), bit 20 (SMAP) ; ECX bit 2 (UMIP) ; vérifier d'abord que la feuille
   max (`cpuid(0)`) est ≥ 7, comme le fait déjà `arch_hw_random` (`cpu.c:224-227`).
2. Dans `cpu_init`, sur chaque cœur : `cr4 |= 1<<20` (SMEP) et `1<<11` (UMIP) si présents. Une option de démarrage
   `nosmep` (`cmdline_has`, `pc_main.c:344`) pour le premier essai sur le ZBook.
3. Faute de page : **avant** `mm_fault`, une faute en mode noyau (`!(r->cs & 3)`) sur une adresse utilisateur dont
   `rip != user_copy_insn` est une erreur du noyau (tous ses accès à la mémoire d'un process passent par
   `arch_copy_user`, `switch.asm:147-155`, vérifié : aucun déréférencement direct) → panique avec le message, sans
   `mm_fault`. Une faute d'exécution (bit I/D, `error & 16`) en mode noyau sous `0x800000000000` → panique « SMEP ».
   Les fautes de `user_copy_insn` gardent le chemin actuel (`mm_fault`, sinon `user_copy_fault`).

**Coût.** Nul à l'exécution. Le Tiger Lake du ZBook a les trois. QEMU : `-cpu max` (ou `host` avec KVM) pour tester.

### 1.2 SMAP (étape 1)

**Ce qui existe.** Un seul point d'accès à la mémoire des process : `rep movsb` de `arch_copy_user`
(`switch.asm:150`). L'entrée par `syscall` efface déjà AC (FMASK `0x44700`, `cpu.c:164`). Les entrées
d'interruption et d'exception (`isr.asm:84-132`, `isr_common` ; `:35-82`, `isr_paranoid`) **n'effacent pas AC**, et
un programme peut le mettre (`popf` ; `sigreturn` laisse passer le bit 18, `sigframe.c:141`).

**À faire.**
- `stac` avant `rep movsb`, `clac` avant les deux `ret` (normal et `user_copy_fault`).
- `clac` à côté des deux `cld` (`isr.asm:51`, `:111`) ; par prudence aussi après `swapgs` dans `syscall_entry`
  (`switch.asm:80`).
- `stac`/`clac` font `#UD` sur un processeur sans SMAP : les écrire en octets (`0F 01 CB` / `0F 01 CA`) et les
  remplacer par des `NOP` de 3 octets au démarrage si SMAP manque (le texte du noyau est en lecture seule à son
  adresse, mais la carte physique, `PML4[256]`, l'atteint en écriture ; le faire avant `smp_start`,
  `pc_main.c:443`). Puis `cr4 |= 1<<21`.
- Les écritures du noyau dans les pages d'un process passent par la carte physique (`proc.c:189-193`, `elf.c:66`,
  `image.c:232-238`, `kits.c:147`, `ipc.c:910`, `vm.c:318`) : SMAP ne les touche pas.

### 1.3 W^X (étape 3)

**Ce qui existe.** `sys_vm_map` (`mm.c:578-624`), `sys_vm_protect` (`:660-737`) et `mm_map_shared` (`:1187`)
acceptent `prot` de 0 à 7, donc lecture-écriture-exécution. Le chargeur ELF réunit les droits d'une page partagée
par deux segments en **RWX** (`elf.c:73-74`) ; le chemin des images en cache (`image.c:213`) garde ceux du premier.
Les scripts d'édition de liens alignent déjà les données (posix.ld : `ALIGN(0x10000)` ; bin.ld : page suivante),
donc ce cas ne devrait pas arriver pour les programmes du système.

**Qui demande RWX aujourd'hui** (à adapter avant de refuser) :

| Programme | Où | Adaptation |
|---|---|---|
| JavaScriptCore (Jet, jsc) | une réservation RWX (`ExecutableAllocator.cpp:381-385`, `OSAllocatorPOSIX.cpp:81`) ; `ENABLE_SEPARATED_WX_HEAP` 0 | permission JIT (ci-dessous), ou JIT coupé (§2.1) |
| CoreCLR (.NET) | `DOTNET_EnableWriteXorExecute=0` (`tools/dotnet/armpi.patch`) | remettre son W^X : il mappe le code deux fois (RW et RX) par memfd ; demande que le noyau mappe deux fois un même objet SHM avec deux droits différents (à vérifier dans `ipc.c`) |
| libffi (ctypes de Python) | `FFI_MMAP_EXEC_WRIT=1` (`tools/python/build.sh:93-99`) | trampolines statiques de libffi (`FFI_EXEC_STATIC_TRAMP`, x86-64) |
| PCRE2/sljit (rg) | `tools/rg/build.sh:11-14` | allocateur W^X de sljit (`SLJIT_PROT_EXECUTABLE_ALLOCATOR`) |
| QBasic | `codeAlloc` RWX (`src/apps/basic/main.cpp:147`) | écrire en RW puis `mprotect` RX |
| émulateurs (gcemu, n64emu, n3dsemu, ndsemu) | mmap RWX | permission JIT |
| libposix `dlopen` | page de bord = union des droits (`dlopen.c:763-775`) | séparer les segments (modules liés avec `-z separate-code`) |
| hôte Linux (`src/lib/linux/elf.c:582-584`, `:684`) | réservation RWX puis mprotect | réserver sans droits, puis chaque segment avec les siens |
| LLVM de Mesa (lavapipe) | RW puis RX | rien |

**À faire.**
1. Noyau : refuser `W|X` ensemble dans `sys_vm_map`, `sys_vm_protect`, `mm_map_shared` **sauf** pour un process qui a
   la permission JIT ; le passage RW → RX par `mprotect` reste permis (c'est W^X, pas « jamais de code nouveau »).
   Le chargeur ELF refuse un ELF dont deux segments W et X partagent une page.
2. La permission JIT : un attribut d'image donné par vfs comme `ATTR_SYS` (`path.c:483-490`), lu d'une liste
   protégée (`/etc/jit.txt` dans les `marques[]` de `access.c`, ou `jit = 1` dans l'`app.txt` d'une app de `/apps`,
   déjà protégé) ; AppKit le passe au noyau dans `P[2]` (`appkit.c:1322`) avec les autres bits de lancement. Pas
   d'appel système pour se la donner : un process exploité l'appellerait.
3. D'abord en journal : une ligne par process qui demande RWX (comme `rights_used`, `proc.c:103-112`), une semaine
   sur le ZBook, puis refus.

**Ce que W^X apporte vraiment.** Il empêche d'écrire puis d'exécuter du code injecté ; une chaîne ROP qui appelle
`mprotect` le contourne. Il ne vaut qu'avec l'ASLR (§1.4) : les deux ensemble.

### 1.4 ASLR de la pile, de l'arène et des kits (étape 1)

**La source d'aléa existe** : ChaCha20 du noyau (`src/kernel/core/sys.c:96-204`), amorcé par RDSEED/RDRAND et la
gigue du TSC (`random_init`, appelé `pc_main.c:442`, avant le premier process). Il manque une fonction interne :
`uint64_t random_u64(void)` dans `sys.c`, sous `rnd_lock`, comme `sys_getrandom` sans la copie.

**Pile.** `USER_STACK_TOP = 0x7FFFFFFF0000` (`include/korin/mm.h:34`), 64 Mo paresseux (`proc.c:172-198`). Tirer un
décalage de 0 à 1 Go, aligné sur 16 octets, à chaque `spawn` (pas à `fork` : l'enfant garde la disposition du
parent, comme Linux). Une seule dépendance dans les programmes : `ARMPI_USER_STACK_TOP` (`src/lib/posix/armpi.h:174`),
lu par `pthread.c:63-65` pour les bornes de la pile du fil principal → la lire du noyau (un champ de plus dans les
informations de process, ou le registre qui porte déjà l'adresse des arguments au démarrage).

**Arène (mmap, dlopen).** `find_gap` (`mm.c:569-576`) prend le premier trou dans `[VM_ARENA_BASE, VM_ARENA_END)`
(`0x5800000000`-`0x6800000000`, 64 Go) puis l'arène haute. Un décalage tiré **par process** (pas par appel, pour ne
pas fragmenter) au début de chaque arène. Seuls `memtest.c:51,204` et `posixtest.c:134` supposent l'adresse : des
essais.

**Kits.** Chargés une fois à `KIT_BASE + n × 64 Mo` (`kits.c:22-24`, `:132-156`), mais :
- leur code est sans relocation (`-fPIC`, `.rel*` et `.got` refusés par `tools/kits/build_c.sh:110-114`) ;
- aucun programme n'écrit l'adresse d'un kit en dur : tous passent par `LIB_OPEN`, qui rend l'adresse de la table
  (`armpi.c:67-90`, stubs de `abi2c.py:120-136`, macro `kit_use` de `kit.inc`).

Donc `map_into` (`kits.c:138`) peut prendre `base = KIT_BASE + tirage_par_process(i)` : les pages physiques restent
partagées, seule l'adresse virtuelle change par process. Exceptions à vérifier : glkit et mediakit portent des
relocations et se lient eux-mêmes à leur segment de variables (`glkit_x86_64.ld:1-4`) ; il faut qu'ils calculent
leur base à l'exécution (s'ils la prennent de `LIB_OPEN`, c'est bon).

**Ce qui reste fixe à cette étape** : le programme lui-même (`0x400000`), le tas qui le suit, la fenêtre des tampons
(§1.5), la fenêtre DMA (`0x6A00000000`), `WS_FB_VA`, `MAPC_BASE`, les fenêtres d'images. Les trois derniers ne sont
écrits en dur nulle part dans les programmes (rendus par les appels système) : on peut les tirer aussi, sans rien
changer ailleurs.

### 1.5 La fenêtre des tampons partagés (étape 3)

`BUF_CREATE` ne rend que le numéro (`mm.c:942`) ; l'adresse `0x6800000000 + n × 64 Mo` est recalculée dans
**49 fichiers** : UIKit (`uk_surface.c:28`), AppKit (`appkit.c:70, 1361, 1696, 2117, 2205`, `fileclient.inc`,
`exec.inc`), NetKit, GpuKit (asm), svclib, fssvc, fatfs, ext4fs, smbfs, gdrivefs, init, intelgpu, Elegant
(`surface.c:274`, `uikit_ops.c:383`, `core.c:1555`), setup, des commandes (`mkfs`, `gpt`, `osinstall`…), Mesa
(`korin_i915.c`, `armpi_drm.c`, `korin_lvp_wsi.c`). `korin_i915.c:186` suppose en plus que deux tampons consécutifs
sont contigus en mémoire virtuelle.

**À faire.** Tirer la base de toute la fenêtre par process (un seul décalage : la contiguïté des numéros
consécutifs reste vraie), la rendre par un appel système (`SYS_VM_INFO` ou un champ de `BUF_INFO`), et remplacer les
49 calculs par une fonction `buf_addr(id)` (svclib, uikit, appkit, posix) qui lit la base une fois. Travail
mécanique mais large (kits en assembleur compris) : à faire en une fois avec `tools/rename_ids.py` pour les
constantes, puis publier tous les paquets touchés.

### 1.6 Exécutables PIE (étape 4)

Le noyau ne charge que `ET_EXEC` (`elf.c:35-36`, `image.c:168`) ; libposix est compilé `-fno-pic -fno-pie`
(`tools/posix/build.sh:55`), les commandes `-static -no-pie`. Il faut : accepter `ET_DYN` avec un biais tiré, faire
les relocations `R_X86_64_RELATIVE` (dans le noyau, ou un `rcrt0` de libposix comme `_dl_relocate_static_pie` de la
glibc), recompiler libposix en `-fPIE`, puis **reconstruire tous les ports** (Jet, gcc, Python, .NET, Qt…). Le cache
d'images partage toujours les pages de texte (identiques quel que soit le biais si on lie avec `-z text`). C'est le
plus long ; l'essentiel du gain vient déjà des kits (où sont les gadgets communs à tous) et de la pile.

### 1.7 KPTI : pas nécessaire sur le ZBook

Le Tiger Lake n'est pas touché par Meltdown (`IA32_ARCH_CAPABILITIES.RDCL_NO`). À noter pour un autre PC : il
faudrait changer de CR3 dans `syscall_entry`, `isr_common`, `isr_paranoid` et `user_iret`, et laisser visibles
`cpus[]`, GDT/TSS, IDT et piles du noyau. À ne faire que si un PC visé le demande (lire ce MSR au démarrage et
l'écrire dans le journal suffit pour le savoir).

---

## 2. N5 : Jet

### 2.1 Réduire le JIT sans rien reconstruire (étape 0)

Jet pour le PC est construit avec JIT, DFG, FTL, Wasm BBQ et OMG (`tools/mesa/build-webkit.sh:15-28` ; config :
`ENABLE_FTL_JIT 1`, `ENABLE_WEBASSEMBLY_BBQJIT 1`, `OMGJIT 1`). JSC lit **chaque option** par
`getenv("JSC_<nom>")` même sur Onyx (`Options.cpp:1137-1147`), et `ProcessLauncherOnyx` passe `environ` aux process
web (`ProcessLauncherOnyx.cpp:155`). Donc Jet peut, dans `main()` avant de créer le premier contexte :

```c
setenv("JSC_useFTLJIT", "false", 0);
setenv("JSC_useOMGJIT", "false", 0);   /* garder BBQ : le Wasm reste rapide */
```

Le plus gros du code JIT (et des failles récentes de JSC) est dans DFG/FTL et OMG. Un réglage de Jet
(« JavaScript : complet / réduit / sans JIT ») avec « réduit » par défaut ; « sans JIT » = `JSC_useJIT=false` (plus
lent sur YouTube, Claude.ai, les jeux). À mesurer sur le ZBook avant de choisir le défaut.

### 2.2 Un uid de bac à sable pour les process web (étape 2)

**Ce qui existe.** Le même programme (`/apps/jet.app/main`) joue l'interface, le process web et le process réseau ;
`ProcessLauncherOnyx` le relance par `posix_spawn` avec `--onyx-webkit-process=web|network`, un identifiant et un
descripteur de socket (`ProcessLauncherOnyx.cpp:28-36, 146-155`). Tous ont l'uid du compte. Un uid ne change que par
l'uid 0 (`SETUID`, `proc.c:421-431` ; `SPAWN_AS`, `:534-545`).

Ce dont le process web a besoin, et qui marche déjà sans l'uid du compte :
- dessin : tampon créé par l'interface et accordé **par pid** (`BUF_GRANT`, `mm.c:945-975` ; patch 0017) ;
- IPC : canaux du noyau (`socketpair`), mémoire partagée memfd passée par `SCM_RIGHTS` (`ipc.c:186-240`) : aucune
  vérification d'uid ;
- polices `/res/fonts`, ICU dans le binaire, `/etc/web-*` en lecture ;
- son (AudioKit → sound : seuls les pilotes et le micro sont filtrés, `sound.c:1308-1318`), GPU (GlKit → intelgpu) ;
- presse-papiers, fichiers envoyés (`<input type=file>`, collage, dépôt) : passent par l'interface (`pagefiles.h`) ;
- réseau, cookies, stockage : dans le process réseau.

**À faire.**
1. **Noyau : un uid dérivé.** Pour tout uid `u` d'un compte, `bac(u) = u | 0x40000000`. Règle : tout process peut
   lancer un enfant sous `bac(son uid)` (nouveau bit de `SPAWN_IMAGE`/`SPAWN_PRELOADED`, à côté de `SPAWN_UID`),
   sans être uid 0 ; un process sous un uid de bac ne peut **ni** en sortir **ni** lancer sous un autre uid. `kill`
   et les signaux (`proc.c:348-363`, `signal.c:249`) permettent en plus à `u` de viser `bac(u)` (le lanceur de
   WebKit tue ses process par `SIGKILL`, ligne 194).
2. **vfs : un bac est toujours refusé, même en mode `log`.** Dans `permis()` (`access.c:134-167`), avant tout :
   un uid de bac lit `/res`, `/lib`, `/apps/jet.app`, `/etc` (sauf `secrets[]`), écrit seulement `/tmp/<son uid>`,
   et rien d'autre (ni `/home`, ni `/tmp`, `/media`, `/net`, `/home/public`, ouverts à tous aujourd'hui). Le faire
   dans `acces_verifier` (`:316-372`) à côté de `toujours[]`, qui est déjà refusé en mode `log`. Ce n'est pas
   bloqué par E73/E74.
3. **netd : pas de socket pour un bac** (le process web n'en ouvre pas ; un process web exploité, si). netd connaît
   l'uid de l'appelant comme pour les ports < 1024 (`ops.c:209-211`).
4. **WebKit :** `ProcessLauncherOnyx` lance `--onyx-webkit-process=web` avec le bit du bac ; le process réseau reste
   sous l'uid du compte (il lit les cookies). Avant `WebProcessMain`, le process web vérifie qu'il est bien dans un
   bac (`getuid()`), sinon il s'arrête : une erreur de lancement ne doit pas donner un process web à l'uid du
   compte.
5. Les apps web (netflix, spotify… → `/apps/jet.app/main --app=`, `launcher.c:130-136`) en profitent sans rien
   changer. Même chose pour les vues de Fichiers, PDF, Aide, WebStudio et Vibe qui lancent Jet.

**À vérifier en le faisant** : les chemins `file://` (WebKit les lit en principe dans le process réseau) ; la
mémoire partagée nommée (`shm_open`, réservée à l'uid créateur, `ipc.c:968-981`) si WebKit en ouvre une créée par
l'interface ; les fichiers `/etc/web-gpu-*` (lecture : permis).

**Plus tard** : un vrai « unveil » (une liste de chemins par process, `path.c:11-12` l'évoque) généraliserait le bac
à d'autres programmes qui lisent des données hostiles (visionneuses, Koton et ses greffons, cdmhost).

### 2.3 cdmhost (Widevine) (étape 2)

cdmhost est lancé **par le process web** (`CDMWidevineOnyx.cpp:286`, `fork`+`execv`) avec `--storage
HOME/.jet/widevine/storage` : sous le bac, il ne pourrait plus écrire ses licences. Le faire lancer par l'interface :
le process web demande par l'IPC de WebKit, l'interface lance cdmhost (uid du compte, ou mieux son propre bac avec
le seul dossier `storage` permis) et rend au process web les deux bouts de canal par `SCM_RIGHTS` (les canaux du
noyau passent, `ipc.c:10-12`). Patch dans `CDMWidevineOnyx.cpp` et `webkit2_armpi.c`.

Au passage : l'interface de Jet devient brièvement uid 0 pour lancer cdmupdate (`widevine.h:52-86`, `ui_elevate`
puis `setuid(was)`). Mieux : un petit lanceur à part que l'élévation vise (la carte d'authd dit alors exactement quoi).

### 2.4 Suivre WebKit

La révision de WebKit vient d'Onyx (`tools/extra/fetch.sh:12-13`, `ONYX_COMMIT`) puis du `fetch.sh` d'Onyx ; elle
est écrite dans `src/extra/webkit/ONYX-REVISION.txt`, absent du dépôt. Le SDK de Jet dit 1.4.8 dans `VERSION`
(paquet jet à 1.4.16). À faire : copier la révision dans `tools/pkg/versions.ini` ou un fichier du dépôt à chaque
SDK, et la comparer aux bulletins de sécurité de WebKitGTK/WPE (ils donnent les révisions corrigées).

---

## 3. N3 : droits du matériel

### 3.1 Tout de suite (étape 0)

**Un compte fait lancer n'importe quel service à droits par vfs.** Un process d'uid > 0 peut demander un montage
(`VFS_MOUNT`, `vfs.c:1118-1138`) : `monter_permis` (`:500-523`) vérifie le point (`/media/x`, `/net/x`) et la source
(clé, image, réseau), mais `monter_ligne` (`:532-549`) accepte comme service **tout nom `[a-z0-9]+`** de `/sys/svc`.
vfs le lance en uid 0 avec les droits (`vfs.c:404-416`), avec les arguments `<point> <source>`. Correction : une
liste des services de systèmes de fichiers (`fatfs`, `ext4fs`, `ramfs`, `smbfs`, `gdrivefs`) dans `monter_ligne`, et
seulement `fatfs`, `ext4fs`, `smbfs`, `gdrivefs` pour un uid > 0.

**oauthd et gdrivefs : les droits sous l'uid du compte.** init2 laisse un compte lancer `/sys/svc/oauthd`
(`init2.c:503-506`) ; vfs lance gdrivefs sous l'uid de qui monte (`vfs.c:408-409`) ; tous deux avec les droits (ils
sont dans `/sys/svc`, donc `ATTR_SYS`, `path.c:467-479`). Ils n'utilisent que `SVC_REGISTER` (nom du système) et
`SVC_MAP_CALLER`. En attendant les capacités : les lancer **sans** droits (un bit « sans droits » dans la demande à
AppKit) ; il faut alors que `SVC_REGISTER` sans droits leur garde leur nom (aujourd'hui : nom du compte,
`svc.c:139-162`, ce qui convient pour oauthd, déjà par compte `oauth.<uid>` depuis le 26/11) et que smbfs, gdrivefs,
oauthd, fsowner ne testent plus `caller_rights` pour reconnaître vfs mais son pid (vfs est enregistré « vfs »).

notifyd, clipd et printd sont dans `/etc/session` sous l'uid de la session, avec les droits (`path.c:467`) ; ils
n'utilisent que `SVC_REGISTER`, `SVC_MAP_CALLER` (clipd) et `PROC_EXE` (notifyd) : même traitement.

### 3.2 Des capacités au lieu d'un seul « droits » (étape 1)

**Ce qui existe.** `int rights` dans le process (`sched.h:41`), vérifié par un seul `rights_used(p, op, nom)`
(`proc.c:103-112`) avec **déjà** 26 numéros d'opérations (`RU_*`, `sched.h:180-183`). Les droits ne se perdent
jamais (pas d'appel pour les rendre), passent à un enfant seulement si le parent les a (`proc.c:565, 817`) ; `fork`
et `exec` sont refusés à un process à droits (`proc.c:606-608, 667`).

**Le découpage**, d'après ce que chaque service utilise vraiment (relevé du 27/11) :

| Capacité | Opérations (`RU_*`) | Qui |
|---|---|---|
| `CAP_MMIO` | `DEV_MAP` | pcid (ECAM), pilotes PCI (leurs BAR), acpid (AML, ECAM), i2chid, keyvault (TPM) |
| `CAP_PORT` | `IO_PORT` | ps2kbd (0x60/0x64), acpid (EC, PM, SystemIO) |
| `CAP_DMA` | `DMA_ALLOC`, `BUF_PHYS`, `SVC_CALLER_PHYS` | nvme, sdhci, rtsx, usbd, hda, intelgpu, iwl ; mapsvc (`SVC_CALLER_PHYS`) |
| `CAP_IRQ` | `IRQ_BIND`, `IRQ_MSI` | les mêmes, ps2kbd, acpid (SCI), i2chid |
| `CAP_ACPI` | `ACPI_TABLE` | pcid (MCFG), acpid, ps2kbd (APIC), i2chid, keyvault |
| `CAP_SVC` | `SVC_REGISTER` (nom du système), `SVC_SIGNAL`, `KIT_REGISTER` | tous les services |
| `CAP_MAPCALLER` | `SVC_MAP_CALLER` | vfs, fatfs, ext4fs, smbfs, gdrivefs, oauthd, clipd, keyvault, mapsvc, blkserv |
| `CAP_BOOT` | `MOD_LAUNCH`, `MOD_READ`, `BOOT_FILE`, `BOOT_READY` | init, vfs, hwmanager, nvme |
| `CAP_SYS` | `CLOCK_SET`, `POWER`, `PRIO_MAX` | init2, netd (NTP), acpid |
| `CAP_WS` | `WS_REGISTER`, `WS_SIGNAL`, `CLIP` | Elegant ; intelgpu et iwl (`WS_SIGNAL`) |
| `CAP_IMAGE` | `IMAGE_PRELOAD`, `IMAGE_FORGET`, épingler | pkgd, vfs |
| `CAP_PROCINFO` | `PROC_EXE`, `PROC_STATS` d'un autre uid | keyvault, notifyd |
| `CAP_SPAWN` | `SPAWN_RIGHTS` : donner à un enfant **une partie** de ses capacités | init, init2, vfs, hwmanager |

**Noyau.** `uint32_t caps` à la place de `int rights` ; `rights_used(p, op)` devient
`(p->caps & cap_de[op]) != 0`, avec le même journal. Au lancement : `params[2]` porte un masque ; l'enfant reçoit
`masque & caps du parent` (jamais plus). Le bit 0 actuel garde son sens le temps de la transition (« toutes les
capacités du parent »). Un appel `CAPS_DROP(masque)` pour en rendre. `PROC_RIGHTS` reste (`caps != 0`) et
`PROC_CAPS` rend le masque.

**Qui décide.** Aujourd'hui vfs (`ATTR_SYS` pour `/sys/svc/*` et la liste `sys_rights[]`, `path.c:467-490`), AppKit
(`P[2]`, `appkit.c:1322`), init2 (courtier des process sans droits, `init2.c:466-522`). Demain : une table
`/etc/caps.txt` (protégée : dans `marques[]`), `programme = capacités`, que vfs lit et rend dans la réponse de
`V_EXEC` à la place de `ATTR_SYS` ; un programme de `/sys/svc` absent de la table n'a **aucune** capacité (fini les
droits pour tout `/sys/svc/*`).

**Les services qui reconnaissent un appelant à ses droits** (`caller_rights`/`caller_admin`, `caller.h`) passent à
la capacité dont ils ont besoin : pcid (CLAIM… : `CAP_MMIO`), usbd (`CAP_DMA` ou mieux le pid lié, E49), hwmanager,
hidmanager, netd (`IF_REGISTER`), sound (`DRV_*`), blkserv (accès brut au disque : une capacité `CAP_RAWDISK` à part,
donnée à vfs, fatfs, ext4fs, osinstall/setup ; aujourd'hui **tout** process à droits lit le disque brut, donc
`shadow.ini` et les coffres), fsowner, smbfs, gdrivefs, oauthd, intelgpu, i2chid. Liste complète dans le relevé
(`pcid.c:474`, `usbd.c:346`, `hwmanager.c:445`, `hidmanager.c:1019`, `netd.c:396`, `sound.c:1311`,
`blkserv.c:735`, `fsowner.c:43,73`, `smbfs.c:555`, `gdrivefs.c:2531`, `oauthd.c:1901`, `serve.c:433`,
`i2chid.c:694`, `init2_x86_64.c:68`).

### 3.3 Le matériel accordé appareil par appareil (étape 3)

**Ce qui existe.** `DEV_MAP` (`hw.c:30-42`) mappe toute adresse qui n'est pas de la RAM ; `IO_PORT`
(`hw.c:207-218`, `ps2.c:303-343`) permet tout port sauf ceux du noyau (PIC, PIT, CMOS, 0xCF8-0xCFF, COM1, 0xE9) ;
`IRQ_BIND` prend toute GSI de l'IOAPIC ; seuls les vecteurs MSI sont liés à leur pid (`hw.c:61-62`). La propriété
d'un appareil n'existe que dans pcid (`op_claim`, `pcid.c:391-409`) ; le noyau ne la connaît pas.

**À faire.**
- **Des accords dans le noyau.** pcid (qui garde un `CAP_MMIO` complet) déclare au noyau, à chaque CLAIM :
  `DEV_GRANT(pid, bdf, plages des BAR)`. Le noyau garde par process une petite liste de plages MMIO, de ports et
  de GSI permis ; `DEV_MAP`, `IO_PORT`, `IRQ_BIND` vérifient la liste au lieu de la seule capacité. Les accès qui ne
  sont pas des BAR (acpid : AML et ECAM ; i2chid : pads GPIO du PCH ; keyvault : TPM CRB) : acpid garde un accès
  complet (c'est l'interpréteur ACPI, au même niveau que le noyau), keyvault reçoit la plage du TPM lue par le noyau
  dans la table TPM2, i2chid reçoit ses pads d'acpid.
- **Le maître de bus coupé à la mort du pilote, avant de libérer sa mémoire.** Aujourd'hui `vm_destroy` libère les
  pages DMA (`proc.c:259-265`) et pcid ne coupe le maître de bus qu'ensuite, quand `SVC_OP_END` lui parvient
  (`pcid.c:467-470`) : un appareil peut encore écrire dans des pages déjà rendues et réutilisées. Le noyau, qui
  connaît maintenant le `bdf` accordé, efface le bit Bus Master (registre de commande, par l'ECAM de MCFG) dans
  `hw_proc_exit`, avant `vm_destroy`.
- `BUF_INFO` ne rend l'adresse physique d'un tampon qu'à son propriétaire ou à qui il est accordé (aujourd'hui : à
  tout process à droits, `mm.c:1036`).

### 3.4 VT-d (étape 4)

**Pourquoi.** Sans IOMMU, un pilote (ou un périphérique Thunderbolt/PCIe malveillant, sans aucun logiciel) écrit
partout en mémoire par DMA. Tout le reste de N3 ne protège que contre un pilote qui se tient bien.

**Ce qui existe.** Le noyau lit les tables ACPI (`arch/pc/acpi.c:281-304` : MADT, FADT ; HPET et MCFG seulement
vus) ; pas de DMAR. Les adresses MSI sont au format de compatibilité (`msi.c:111`) et pcid n'accepte que
`0xFEExxxxx` (`pcid.c:451-456`).

**À faire, dans l'ordre.**
1. **Lire la DMAR** dans `acpi_init` : les unités DRHD (adresse des registres, périmètre : quels appareils), les
   RMRR (mémoire qu'un appareil doit garder : mémoire volée de l'iGPU, USB d'héritage), le bit
   `DMA_CTRL_PLATFORM_OPT_IN`. Refuser `DEV_MAP` sur les registres des DRHD.
2. **Un pilote VT-d minimal dans le noyau**, en mode « hérité » (table racine + tables de contexte, pas le mode
   scalable) : un domaine par process pilote, tables de second niveau au format des tables de pages x86-64.
   Invalidation par registres d'abord (plus simple que la file).
3. **Adresses DMA = adresses physiques (identité)** dans chaque domaine : les pilotes ne changent pas (ils
   continuent à donner des adresses physiques aux appareils), le noyau ne met dans le domaine que les pages
   permises :
   - `DMA_ALLOC` (`mm.c:1048-1078`) ajoute ses pages au domaine du pilote ;
   - blkserv `op_attach` (tampon du client) et `SVC_MAP_CALLER`/`SVC_CALLER_PHYS` (pages de l'appelant) les
     ajoutent le temps de l'échange, et les retirent ensuite (avec invalidation de l'IOTLB avant de rendre les
     pages) ;
   - intelgpu (GTT) : idem pour les tampons des clients (`BUF_INFO`, `serve.c:285`).
4. **L'appareil attaché au domaine** au moment du `DEV_GRANT` de pcid (§3.3), détaché à la mort du pilote :
   maître de bus coupé → contexte effacé → IOTLB invalidée → pages libérées.
5. **L'iGPU d'abord en passe-droit** (son DRHD dédié) : la GTT et la mémoire volée compliquent tout ; protéger d'abord
   NVMe, xHCI, Wi-Fi, HDA et surtout **Thunderbolt** (le vrai risque sans logiciel).
6. **Plus tard** : le remappage des interruptions (change le format des adresses MSI et le filtre de pcid).

**Essai.** QEMU q35 a `-device intel-iommu` (avec `caching-mode=on`) : tout se teste sans le ZBook, appareils NVMe
et xHCI de QEMU compris. Sur le ZBook, une option de démarrage `noiommu` pour revenir en arrière.

---

## 4. Ce qu'on gagne, étape par étape

| Après | Une faille dans… | …donne aujourd'hui | …donne après |
|---|---|---|---|
| 0 | un compte qui monte une clé | un service quelconque en uid 0 à droits | rien |
| 0 | oauthd (réponse HTTPS) | droits = noyau | le compte (déjà le sien) |
| 1 | le noyau (pointeur écrasé) | exécuter/lire du code utilisateur | faute, panique |
| 1 | une app (débordement) | ROP sur des kits aux adresses connues | il faut d'abord une fuite d'adresse |
| 2 | JavaScriptCore (page web) | les fichiers du compte, ses cookies, `~/.ssh` | `/tmp/<bac>` seulement, pas de réseau |
| 3 | un pilote (netd, iwl, usbd) | tout le matériel, tout le disque | son appareil et sa mémoire DMA |
| 4 | un pilote ou un appareil Thunderbolt | DMA partout | ses pages seulement |

Ce qui reste hors de cette étude : N1, N2 (corrigés le 26/11), N6 et la suite de l'audit.

---

## 5. Fait : l'étape 0 (27/11/2026)

* **Montages d'un compte** (`src/svc/vfs/vfs.c`, `monter_ligne`) : le service doit être `fatfs`, `ext4fs`, `smbfs`,
  `gdrivefs` (ou `ramfs`, pour le système seulement). Sous QEMU, `mount /media/x blk.usb0 telnetd` d'un compte :
  « invalide » ; `fatfs` est essayé comme avant.
* **Droits de service seulement** : `rights` du noyau devient un masque des opérations `RU_*`
  (`include/korin/sched.h` : `RIGHTS_ALL`, `RIGHTS_SVC`, `rights_full`) ; `SPAWN_SVC` (4, avec le bit 0 des options
  de `SPAWN_IMAGE`/`SPAWN_PRELOADED`) réduit les droits du fils à `SVC_REGISTER` et `SVC_MAP_CALLER`. vfs donne
  `ATTR_SVC` (0x1000) avec `ATTR_SYS` à `/sys/svc/oauthd` et `/sys/svc/gdrivefs` (`path.c`, PC seulement) ; AppKit le
  traduit en `SPAWN_SVC` (`appkit.c`, `f_spawn_file`). `PROC_RIGHTS` rend 2 pour eux (1 : droits complets, ce que
  testent `caller_rights` et les autres services) ; `PROC_FLAGS` (0x100), `PROC_EXE` (`PEXF_RIGHTS`), les images
  « système » et l'épinglage demandent les droits complets. Sous QEMU : oauthd lancé par init2 pour un compte
  (uid 1000) répond, le journal ne montre que `SVC_REGISTER`.
* **SMEP et UMIP** (`src/kernel/arch/x86_64/cpu.c`, `cpu_protect`) : sur chaque cœur, le cœur 0 après `paging_init` ;
  options de démarrage `nosmep`, `noumip` pour revenir en arrière. La faute de page (`idt.c`) s'arrête sur une faute
  de protection du noyau hors `arch_copy_user` (avant : boucle sans fin). ktest 59/59 et le bureau sous QEMU
  (`-cpu max` : « cpu : SMEP oui, UMIP oui »).
* **Jet 1.4.17** (`src/extra/onyxports/jet/jit.h`) : Réglages › « JavaScript : complet / réduit / sans JIT »,
  `javascript=` dans `HOME:/jet/settings.ini`, « réduit » par défaut (`JSC_useFTLJIT=false`, `JSC_useOMGJIT=false` ;
  « sans JIT » : `JSC_useJIT=false`), appliqué dans chaque process avant WebKit.

Reste de N3 pour l'étape 1 : notifyd, clipd et printd (droits complets sous l'uid de la session, `path.c`
`sys_rights[]`) n'utilisent que `SVC_REGISTER`, `SVC_MAP_CALLER` et `PROC_EXE` : à passer en droits de service, une
fois vérifié qu'aucun service ne les reconnaît à leurs droits complets.
