# Étude : Widevine dans Jet (Netflix, Disney+, Prime Video, Spotify web…)

Demandée le 18/11/2026 (question de départ : pourquoi LibreWolf peut-il télécharger Widevine et pas Jet ?).
**Mise à jour le 19/11/2026** : l'utilisateur lance le chantier par un **hôte qui exécute du code Linux**, sans
attendre la couche Linux complète (`armpi-linux-compat`, pour Claude Code). Vérifications en ligne faites le 19/11
(sources en fin de document). **Jalon 1 fait le 19/11** : `liblinux` et `linuxhost` chargent un `.so` de Linux dans
un process de Korin PC, essayé sous QEMU (§ 7).

## 1. Le problème

Netflix, Disney+, Prime Video, Deezer et le lecteur web de Spotify exigent **EME + Widevine**, le DRM de Google.
Le module (CDM) est un **binaire fermé** de Google, livré pour Windows, macOS, ChromeOS, Android et Linux
(`libwidevinecdm.so`, **x86-64 seulement** : Google a arrêté le 32 bits, l'ARM64 n'existe que dans les images de
ChromeOS). Korin ne peut ni le recompiler ni le charger tel quel : il est lié à glibc et à l'ABI de Linux. WebKit
n'a pas d'hôte pour lui : Apple utilise FairPlay, WPE et GTK passent par OpenCDM/Thunder.

La voie choisie : un **process de Korin qui charge le module de Linux** et lui sert ce qu'il demande (fonctions de
glibc, appels système de Linux), et que Jet pilote par IPC à travers EME.

## 2. Ce que l'on sait du module (vérifié le 19/11)

### 2.1 Obtention et mise à jour

* Le module de Linux est une archive de Google : `<version>-linux-x64.zip` (`libwidevinecdm.so`, `manifest.json`,
  licence). La série actuelle est la **4.10.x** (4.10.2391.0 dans la documentation de Mozilla).
* **Chrome** le reçoit par son *component updater* (le serveur de mise à jour de Google, composant
  « WidevineCdm ») et le range dans `WidevineCdm/<version>/_platform_specific/linux_x64/`.
* **Firefox** (et LibreWolf) ne l'héberge pas : son serveur de mises à jour (Balrog) donne, par plateforme, l'URL
  **chez Google** (`redirector.gvt1.com/edgedl/widevine-cdm/…-linux-x64.zip`), la taille et l'empreinte SHA-512 ;
  Firefox télécharge chez Google et vérifie. Les blocs de Balrog sont faits à la main par Mozilla à chaque version.
* Ancienne voie des scripts : `dl.google.com/widevine-cdm/versions.txt`, puis `<version>-linux-x64.zip`. Encore
  citée par les guides, mais sans garantie qu'elle suive les versions récentes.
* Seul Google peut distribuer le module : **jamais dans nos paquets**. Le module est téléchargé chez Google, sur la
  machine de l'utilisateur, à sa demande.

### 2.2 Son interface (la « CDM API » de Chromium)

En-tête public `content_decryption_module.h` (dépôt `chromium/cdm`, copié par Firefox dans
`dom/media/gmp/widevine-adapter/`) :

* **Exports C** du module : `InitializeCdmModule_4` (`CDM_MODULE_VERSION` 4), `DeinitializeCdmModule`,
  `CreateCdmInstance (cdm_interface_version, key_system, key_system_size, GetCdmHostFunc, user_data)`,
  `GetCdmVersion`, et `VerifyCdmHost_0` (vérification des binaires de l'hôte ; exporté **aussi** par le module de
  Linux, relevé du 19/11, § 2.5).
* **Interfaces C++** (classes à méthodes virtuelles, ABI Itanium : la même pour g++ de Korin et le clang de
  Google) : `ContentDecryptionModule_10`, `_11` et `_12` ; côté hôte, `Host_10`, `Host_11`, `Host_12`. Firefox et
  Chrome stables parlent `_10`/`_11` ; `_12` (statuts de clés `KeyStatus_2`) est la plus récente.
* **Le module** : `Initialize`, `GetStatusForPolicy`, `SetServerCertificate`, `CreateSessionAndGenerateRequest`,
  `LoadSession`, `UpdateSession`, `CloseSession`, `RemoveSession`, `TimerExpired`, `Decrypt`,
  `InitializeAudioDecoder`/`VideoDecoder`, `DecryptAndDecodeFrame`/`Samples`, `Destroy`.
* **L'hôte** : `Allocate` (tampons), `SetTimer`, `GetCurrentWallTime`, `OnInitialized`, `OnResolvePromise`,
  `OnResolveNewSessionPromise`, `OnRejectPromise`, `OnSessionMessage` (la requête de licence à envoyer),
  `OnSessionKeysChange`, `OnExpirationChange`, `OnSessionClosed`, `SendPlatformChallenge`, `EnableOutputProtection`,
  `QueryOutputProtectionStatus`, `OnDeferredInitializationDone`, `CreateFileIO` (stockage persistant),
  `RequestStorageId`.
* **`Decrypt ()` rend l'échantillon en clair à l'hôte** (mode « déchiffrement seul ») : c'est le mode du niveau L3
  sur ordinateur, et celui qui nous va (le décodage reste à FFmpeg, MediaKit).

### 2.3 Dépendances, vérification de l'hôte, VMP

* Bibliothèques demandées (relevé de Mozilla, version 2023) : `libc.so.6`, `libdl.so.2`, `libpthread.so.0`,
  `libm.so.6`, `libgcc_s.so.1`, `ld-linux-x86-64.so.2`. Le module est fait pour tourner **dans le bac à sable de
  Chrome** (seccomp) : il passe par glibc et par l'hôte, peu d'appels directs. La liste exacte de ses imports se
  mesure avec `linuxhost --check` (§ 7), dès que le module est là.
* Pièges connus : glibc ≥ 2.18 attendue par endroits (horloges par `clock_gettime`), `libgcc_s` chargée
  dynamiquement pour le déroulement des exceptions C++.
* **VMP** (*Verified Media Path*, signature des binaires de l'hôte) : seulement Windows et macOS. **Linux n'a pas
  de VMP** : c'est pourquoi Netflix y plafonne la résolution, et pourquoi un hôte qui n'est pas Chrome peut charger
  le module (Firefox, Kodi, OpenWV le font).
* **Niveau L3** seulement (sans protection matérielle).

### 2.4 Comment d'autres le chargent hors de Chrome

| Projet | Comment |
|---|---|
| **Firefox / LibreWolf** | process `plugin-container` (GMP), adaptateur `widevine-adapter` qui implémente `Host_10/11` ; module téléchargé chez Google par l'URL et l'empreinte de Balrog |
| **Kodi** (`inputstream.adaptive`) | charge `libwidevinecdm.so` dans le process de Kodi (`src/decrypters/widevine`, `lib/cdm`), module installé à part par `inputstreamhelper` (x86-64 depuis Google, ARM depuis une image de ChromeOS) |
| **Asahi Linux** (aarch64) | module tiré d'une image de récupération de ChromeOS ; glibc patchée (celle de ChromeOS diffère), segments ELF réalignés pour des pages de 16 Ko, symboles `__aarch64_ldadd4_acq_rel` manquants recâblés par la GOT ; Netflix avec un user agent de ChromeOS, 720p |
| **WebKitGTK / WPE** | EME par `CDMProxyThunder` (OpenCDM de Thunder/RDK), qui encapsule le module ; pas d'hôte CDM de Chromium dans WebKit |
| **OpenWV** | réimplémentation libre du module (même interface), mais il faut l'identité d'un appareil (`.wvd`) extraite ailleurs : **écarté** (conditions, et rien de légitime à mettre dedans) |

### 2.5 Le module réel, mesuré le 19/11 (4.10.3050.0)

Obtenu comme Firefox : le serveur de Mozilla (Balrog, `aus5.mozilla.org`, plateforme `Linux_x86_64-gcc3`) donne
désormais **le composant de Chrome lui-même**, chez Google (`edgedl.me.gvt1.com/edgedl/release2/chrome_component/…
oimompecagnajdejgnnjijobebaeigek_4.10.3050.0_linux_….crx3`, 21 685 984 octets, SHA-512 vérifiée). L'ancienne
adresse `dl.google.com/widevine-cdm/versions.txt` rend **404** : elle est morte. Le `.crx3` est un en-tête `Cr24`
suivi d'un zip (`_platform_specific/linux_x64/libwidevinecdm.so`, 21,7 Mo ; `manifest.json` ; `LICENSE`).
Copie de développement dans `build/pc/linux/widevine/` (jamais dans le dépôt ni un paquet).

* **Manifeste** : `x-cdm-module-versions` 4, `x-cdm-interface-versions` **10**, `x-cdm-host-versions` **10**
  (donc `ContentDecryptionModule_10` et `Host_10` seulement) ; codecs `vp8, vp09, avc1, av01` ; schémas `cenc` et
  `cbcs` ; pas de licence persistante. Le manifeste cite aussi une plateforme `linux/arm64`
  (`_platform_specific/linux_arm64/`), absente de cette archive : piste pour le Pi, à vérifier.
* **`linuxhost --check`** : 81,6 Mo en mémoire (dont ~60 Mo de `.bss`), `BIND_NOW` ; `NEEDED` libdl, libpthread,
  libm, libgcc_s, libc, ld-linux ; versions de symboles jusqu'à **GLIBC_2.17** seulement (base ancienne : bon
  signe). **177 imports, 108 non servis** par la compatibilité du jalon 1 : threads (`pthread_create`, `_join`,
  `cond_*`, `rwlock_*`, `attr_*`, `sem_*`, `pthread_setname_np`, `getaffinity_np`, `__register_atfork`), fichiers
  (`open64`, `fopen64`, `pread64`, `creat64`, `__xstat64`, `fgets`, `getc`…), locales et caractères larges de
  libstdc++ (`newlocale`, `uselocale`, `strtod_l`, `mbrtowc`, `catopen`…), temps (`localtime_r`, `gmtime_r`,
  `mktime`, `timegm`), maths (`exp`, `log`, `floor`…), système (`getauxval`, `prctl`, `uname`, `getrlimit64`,
  `sched_getaffinity`, `setpriority`, `sched_setscheduler`, `madvise`), `dlsym`/`dlclose`, `_setjmp`/`longjmp`,
  déroulement C++ (`_Unwind_*`), `__libc_stack_end`, `__tls_get_addr`.
* **TLS** : un segment `PT_TLS` de 0xB1 octets (aligné à 64), 2 relocations, accès par `__tls_get_addr`.
* **Appels système directs : 791 instructions `syscall`** dans le code (désassemblage), au milieu de code
  **obscurci** : le numéro est calculé juste avant (`mov %r13,%rax ; syscall`), jamais une constante lisible. C'est
  la protection du module contre l'interception de glibc. **Conséquence : la personnalité Linux du noyau est
  obligatoire**, quelle que soit la décision sur glibc (§ 3.2).
* **Sonde à l'exécution** (19/11, autorisée par l'utilisateur ; `tools/linux/cdmprobe`) : sous Linux (WSL), un
  hôte `Host_10` minimal charge le module, un filtre seccomp piège (`SECCOMP_RET_TRAP`, SIGSYS) les seuls
  `syscall` dont l'instruction est dans le module, le gestionnaire les note et les exécute. Parcours :
  `InitializeCdmModule_4`, `CreateCdmInstance`, `Initialize`, `CreateSessionAndGenerateRequest` (PSSH de
  Widevine), minuteries, `Destroy`. **Le module produit une vraie requête de licence (1702 octets)** : la capture
  des `syscall` ne le dérange pas. Ses appels directs, au total 66 :

  | Appel de Linux | Nombre | Ce qu'il fait |
  |---|---|---|
  | `openat` (257) | 12 | `/proc/self/maps` ×11, `/proc/net/unix` ×1 |
  | `read` (0) | 37 | le contenu de ces fichiers, par 4 Ko |
  | `close` (3) | 12 | |
  | `rt_sigaction` (13), sans nouvelle action | 5 | lit les gestionnaires de SIGILL, SIGTRAP, SIGABRT |

  Lecture : un contrôle d'intégrité (quelles bibliothèques sont chargées et comment ses propres segments sont
  projetés), une recherche de sockets de débogage, et un contrôle des gestionnaires de signaux (anti-débogueur).
  Hors de `InitializeCdmModule_4` et de `GetCdmVersion`, qui n'en font aucun. `UpdateSession` (licence reçue) et
  `Decrypt` n'ont pas encore été parcourus (il faut une licence : étape 3).
* **Conséquences pour Korin** : Korin n'a pas de `/proc`. `liblinux` devra **fabriquer** `/proc/self/maps` au
  format de Linux, cohérent avec ce que le chargeur a projeté (segments du module `r--p`, `r-xp`, `rw-p` avec
  son chemin, pile, autres projections), et un `/proc/net/unix` réduit à sa ligne d'en-tête ; `rt_sigaction` de
  lecture répond avec la `struct sigaction` du noyau de Linux.

## 3. Architecture dans Korin

```
 Jet (WebKit)                                   cdmhost (process de Korin)
 ─────────────────────────────                  ──────────────────────────────────────────
 MediaKeys / MediaKeySession (EME, JS)          liblinux : chargeur ELF de Linux
 CDMFactoryKorin, CDMPrivateKorin   ── IPC ──▶  glibc de compatibilité, appels de Linux traduits
 CDMInstanceKorin, CDMInstanceSession           Host_10/11 en C++ (minuteries, stockage, tampons)
 lecteur MSE de Jet : échantillons CENC ──────▶ libwidevinecdm.so (Google, chargé tel quel)
   ◀── échantillons en clair ── mémoire partagée ── Decrypt ()
 FFmpeg (MediaKit) décode et affiche
```

### 3.1 L'hôte Linux : `liblinux` (fait en partie, `src/lib/linux`)

* **Chargeur ELF** (`elf.c`) : ce que fait `ld-linux-x86-64.so.2` pour une bibliothèque, sans lui : segments
  `PT_LOAD` d'un seul tenant, relocations `RELATIVE`, `64`, `GLOB_DAT`, `JUMP_SLOT`, `IRELATIVE` liées tout de
  suite, imports pris dans la glibc de compatibilité, protections des segments, constructeurs (`DT_INIT`,
  `DT_INIT_ARRAY`) et destructeurs (`__cxa_atexit`, `DT_FINI_ARRAY`). Les versions des symboles sont ignorées.
  **Reste** : TLS du module (`PT_TLS`, `TPOFF64`, `DTPMOD64` ; à loger dans la réserve des modules que libposix
  garde déjà pour `dlopen`, sous le bloc TLS du programme), et `libgcc_s` (déroulement C++ : `_Unwind_*` de
  la libgcc de Korin, avec `dl_iterate_phdr` qui connaît les modules de Linux).
* **glibc de compatibilité** (`glibc.c`) : une table nom → adresse. Les fonctions identiques à l'ABI près
  (`memcpy`, `malloc`, `snprintf`…) sont celles de libposix ; les autres sont des enveloppes : `errno` de Linux
  (`__errno_location`), constantes de Linux (horloges, `sysconf`, drapeaux), tables de `ctype` de glibc
  (`__ctype_b_loc`), mutex de glibc (40 octets), variantes `_chk` de `_FORTIFY_SOURCE`, `stdout`/`stderr` comme
  données. **Reste** : ce que `--check` relèvera sur le vrai module (probablement `pthread_create`,
  `pthread_cond_*`, `dlopen`/`dlsym`, `dl_iterate_phdr`, `getauxval`, `open`/`fstat`/`stat` à structures de Linux).
* **Appels de Linux** (`syscall.c`) : numéros de Linux x86-64 → libposix ; résultat en `-errno` de Linux. Servis :
  read, write, close, lseek, mmap, mprotect, munmap, sched_yield, nanosleep, getpid, gettid, get*id, clock_gettime,
  clock_getres, gettimeofday, time, getrandom, exit. Un appel non servi rend `ENOSYS` et se signale une fois.
* **Le TLS de libposix est compatible** là où le code de Linux regarde : `%fs:0` = pointeur de thread, et le canari
  de pile en `%fs:0x28` tombe dans le TCB de 64 octets de libposix (stable : vérifié par le module d'essai, compilé
  avec `-fstack-protector-all`).
* **Instructions `syscall` directes** dans le module : `--check` compte les octets `0f 05` du code. S'il y en a de
  vraies, il faut la **personnalité Linux du noyau** (décision du 13/11 pour Claude Code : le noyau renvoie les
  `syscall` venus du code Linux vers `liblinux`, façon *Syscall User Dispatch*). C'est le seul point qui toucherait
  au noyau ; il est commun avec la couche Linux de Claude Code.

### 3.2 glibc réelle ou compatibilité : la décision

| | glibc de compatibilité (choisie pour le jalon 1) | vraie glibc + `ld-linux` |
|---|---|---|
| Principe | les imports du module servis par libposix | `ld-linux` et `libc.so.6` d'une distribution chargés tels quels |
| Noyau | rien (tant que le module n'a pas de `syscall` direct) | personnalité Linux obligatoire : glibc fait **tous** ses appels par `syscall` (~100 appels à traduire, `clone`, `futex`, `rt_sigaction`…) |
| Travail | proportionnel aux imports du module (quelques dizaines à ~150) | une couche Linux complète (celle de Claude Code) avant la première licence |
| Fidélité | risque de différences fines de comportement | maximale |
| Distribution | rien à livrer de tiers | une glibc à livrer (LGPL, faisable) |

**Décision de l'utilisateur (19/11) : la compatibilité.** Le relevé du module réel (§ 2.5) la confirme : 177
imports, une glibc de base 2.17. Il montre aussi 791 `syscall` directs : la personnalité Linux du noyau est à
faire de toute façon, mais pour les seuls appels du module (ceux de la compatibilité restent des appels de Korin).

**Forme voulue par l'utilisateur (19/11) : une capture générique, sans « Linux » dans le noyau.** Le noyau ne sait
pas quelle personnalité l'hôte imite ; il sait seulement que l'hôte (le code du process qui charge le module :
`liblinux` dans `cdmhost`) veut **capturer** certains `syscall` :

* l'hôte déclare ce qu'il capture : une ou plusieurs **plages d'adresses** de l'instruction `syscall` (le code du
  module), et au besoin une liste de **numéros** ; plus une fonction de reprise et sa pile ;
* un `syscall` qui correspond n'est pas servi comme un appel de Korin : le noyau sauve l'état du thread et le
  **renvoie à l'hôte** (dans le même thread, comme un signal : registres, adresse de l'instruction, numéro) ;
* l'hôte le sert à sa façon (`linux_syscall` pour Linux), met le résultat dans `rax` et reprend le code capturé ;
  les appels de l'hôte lui-même, hors des plages, restent des appels de Korin ;
* rien de propre à Linux : la même capture servira à d'autres personnalités (un autre système, un bac à sable,
  un traceur). Sous Linux, l'équivalent est *Syscall User Dispatch* ou `SECCOMP_RET_TRAP` (ce que fait la sonde).

**Livraison dans le thread appelant** (choix de l'utilisateur, 19/11, contre un thread hôte séparé) : comme le
noyau de Linux, qui sert un `syscall` dans le thread qui l'a fait, l'hôte le sert dans ce thread pendant que le
code capturé est suspendu. Chaque thread du module est servi en parallèle, un appel qui bloque ne bloque que
son thread, et les appels liés à l'appelant (`gettid`, masque, `futex`) sont faits par le bon thread. Une
livraison « vers un thread ou un process hôte qui attend » (traceur, bac à sable surveillé) pourra s'ajouter
comme une option du même appel.

**Fait le 19/11 (étape 2b)** : `SYS_SYSCALL_CAPTURE` (0x96, `src/inc/syscalls.inc`) : `(plages, n, entrée,
retour, numéros)`, au plus `CAPTURE_MAX` (4) plages `{début, fin}`, table facultative de 512 bits des numéros
(sans table : tous, même au-delà de 511) ; `n = 0` retire la capture. Process entier (threads), gardé par
`fork`, effacé par `exec` ; PC seulement (le Pi rend `E_NOSYS`).
* Noyau (`src/kernel/core/signal.c`, `arch/x86_64/sigframe.c`) : à l'entrée d'un `syscall`, si l'adresse de
  l'instruction (`rip - 2`) est dans une plage, rien n'est servi ; le noyau pose la trame d'un signal (celle du
  PC suit déjà l'`ucontext` de Linux x86-64) avec `signo SIGSYS`, `code CAPTURE_CODE`, l'adresse de
  l'instruction (+16) et le numéro (+24) dans le `siginfo`, et saute à l'entrée de l'hôte. Le masque des signaux
  ne change pas. L'hôte reprend par le `SIG_RETURN` existant.
* `liblinux` (`src/lib/linux/capture.c`) : déclare les plages des modules à chaque chargement et
  déchargement (avant les constructeurs) ; l'entrée lit les registres de l'`ucontext`, appelle
  `linux_syscall`, écrit `rax`.
* Essai sous QEMU PC : 4 `syscall` directs du module d'essai (getpid, write, clock_gettime, un numéro
  inconnu 4094) capturés et traduits, 20 contrôles sur 20 ; `ptest` du noyau (signaux, fork, exec) 22/0.

### 3.3 `cdmhost` et le protocole avec Jet

Un process `cdmhost` par process web qui utilise EME (comme le `plugin-container` de Firefox) : un module qui plante
n'emporte pas Jet, et la mémoire du module reste à part. Canal IPC de Korin (0x70-0x7D) pour les messages, mémoire
partagée pour les échantillons.

| Message (Jet → hôte) | Réponse / événement (hôte → Jet) |
|---|---|
| `INIT key_system, distinctive_id, persistent_state` | `INITIALIZED ok` |
| `SET_SERVER_CERT promise, cert` | `RESOLVE promise` / `REJECT promise, exception, code, message` |
| `CREATE_SESSION promise, session_type, init_data_type, init_data` | `NEW_SESSION promise, session_id` |
| `UPDATE_SESSION promise, session_id, response` | `SESSION_MESSAGE session_id, type, message` (requête de licence ; Jet l'envoie au serveur du site) |
| `LOAD/CLOSE/REMOVE_SESSION promise, session_id` | `KEYS_CHANGE session_id, has_new_usable_key, [key_id, statut]` |
| `DECRYPT id, tampon partagé, key_id, iv, sous-échantillons, schéma cenc/cbcs, motif` | `DECRYPTED id, statut, taille` (le clair dans le même tampon partagé) — **audio seulement** |
| `INIT_VIDEO_DECODER codec, profil, taille, extradata` | `VIDEO_DECODER_READY statut` |
| `DECODE_VIDEO id, tampon partagé (Annex B pour H.264), key_id, iv, sous-échantillons, horodatage` | `VIDEO_FRAME id, statut, I420 (plans, pas) dans une mémoire partagée` ; `NEED_MORE_DATA` |
| `GET_STATUS_FOR_POLICY promise, hdcp` | `EXPIRATION_CHANGE`, `SESSION_CLOSED` |

**Constat de l'étape 3 : la vidéo ne sort jamais compressée en clair.** Le module L3 refuse `Decrypt` sur un
échantillon vidéo chiffré (`kNoKey`) ; il déchiffre et décode lui-même (`DecryptAndDecodeFrame`, codecs du
manifeste : H.264, VP9, AV1) et rend des images I420. Pour la vidéo, Jet reçoit donc des **images décodées** de
`cdmhost` (comme Chrome), pas des échantillons pour FFmpeg ; l'audio, lui, revient en clair et passe par FFmpeg
(MediaKit). Comme Chromium, le lecteur passe le H.264 en **Annex B** (codes de début, SPS/PPS avant chaque image
clé, sous-échantillons ajustés : `cenc_to_annexb`), sinon le module ne reconnaît pas les images.

Côté hôte : minuteries (`SetTimer` → boucle du `cdmhost`), heure (`GetCurrentWallTime`), stockage (`CreateFileIO`
→ `HOME:/.jet/widevine/<origine>/`), `RequestStorageId` (un identifiant aléatoire gardé par origine),
`QueryOutputProtectionStatus` (« pas de lien externe » : rien de mieux à dire en L3).

### 3.4 EME dans WebKit pour Korin

* `ENABLE_ENCRYPTED_MEDIA` dans la construction de Jet (`tools/webkit`) ; WebKit fournit `MediaKeys`,
  `MediaKeySession`, `navigator.requestMediaKeySystemAccess` au-dessus de `CDMFactory`, `CDMPrivate`,
  `CDMInstance`, `CDMInstanceSession`. On écrit les nôtres (`CDMFactoryKorin`…), comme `CDMProxyThunder` pour
  GStreamer.
* Le lecteur MSE de Jet (MediaKit/FFmpeg) lit les boîtes `pssh`, `tenc`, `senc` des segments MP4 (CENC), envoie
  l'événement `encrypted` au JavaScript, puis passe chaque échantillon chiffré à `cdmhost` avant FFmpeg.
* **ClearKey d'abord** (`org.w3.clearkey`, déchiffrement AES-CTR/CBC par OpenSSL, sans module) : valide toute la
  chaîne EME/MSE/CENC de Jet sur des flux d'essai publics avant de brancher Widevine.
* Identification : pour ces sites, Jet se présente comme Chrome ou Firefox **sous Linux** (Netflix et Spotify
  refusent les navigateurs inconnus) ; `navigator.platform` cohérent.

### 3.5 La case des réglages

« Lire les contenus protégés (Widevine, module de Google) », décochée par défaut. Cochée : Jet demande la version
courante (voie choisie au § 6), télécharge l'archive **chez Google**, vérifie l'empreinte, la range dans
`HOME:/.jet/widevine/<version>/`, et vérifie de nouveau à chaque démarrage (au plus une fois par jour). Un Korin
distribué à d'autres ne l'active jamais d'office.

## 4. Ce qu'on peut en attendre

* **Spotify, Deezer** (audio) : sans limite gênante.
* **Netflix, Disney+** : **720p** au mieux sous Linux (L3, sans VMP) ; **Prime Video** : souvent en définition
  standard sous Linux.
* **Fragile** : Google peut retirer une version (les serveurs de licences refusent les modules trop anciens : il
  faut suivre les mises à jour), les services peuvent durcir leurs contrôles (navigateur, plateforme), et le
  module peut se mettre à demander ce que la compatibilité ne sert pas encore. Peut casser sans recours.
* **Droit** : zone grise tolérable pour un usage personnel (module téléchargé chez Google par l'utilisateur, pas
  modifié, pas redistribué) ; pas de contournement du DRM : on l'exécute tel quel.
* **Le Pi** : pas de module pour Linux ARM64 hors de ChromeOS ; possible plus tard par la voie d'Asahi (glibc et
  ELF de ChromeOS), hors du plan.

## 5. Étapes chiffrées

| # | Étape | Fini quand | Estimation |
|---|---|---|---|
| 1 | **liblinux + linuxhost** (chargeur, glibc de compatibilité, appels de Linux) | un `.so` de Linux appelé dans Korin | **fait le 19/11** |
| 2a | Module réel : téléchargement de développement et `linuxhost --check` | la liste de ce qui manque | **fait le 19/11** (§ 2.5) |
| 2b | Capture générique des `syscall` dans le noyau du PC (§ 3.2) ; `liblinux` s'en sert | un `syscall` direct d'un module d'essai traduit | **fait le 19/11** (QEMU) |
| 2c | Les 108 imports, TLS du module (`__tls_get_addr`), `_Unwind_*`, threads ; `/proc/self/maps` et `/proc/net/unix` fabriqués, `rt_sigaction` | dans Korin, la sonde va jusqu'à la requête de licence, comme sous Linux | **fait le 19/11** (QEMU, § 8) |
| 3 | `cdmhost` : `Host_10/11` en C++, essai hors de Jet sur un serveur de licences de test public | une licence de démonstration obtenue, un échantillon déchiffré | **fait le 19/11** (Linux et QEMU, § 9) ; le serveur IPC de `cdmhost` reste, avec l'étape 4 |
| 4 | EME ClearKey dans Jet (WebKit, MSE chiffré, CENC) | un flux ClearKey de démonstration joue | **fait le 20/11** (QEMU, § 11) ; `cdmhost` et son client aussi (§ 10) |
| 5 | EME Widevine dans Jet par `cdmhost` (audio, vidéo) | un flux Widevine joue dans Jet ; puis le lecteur web de Spotify | **fait le 20/11** (QEMU, flux de Shaka, § 12) ; Spotify à essayer |
| 6 | Les sites : Jet se présente comme Chrome sous Linux, un vrai lecteur (Shaka) joue | Netflix et Spotify chargent ; Shaka joue | **fait le 20/11** (QEMU, § 13) ; lecture avec un compte : à essayer par l'utilisateur |
| 7 | Case des réglages, téléchargement chez Google, mise à jour | de la case à la lecture sans manipulation | **fait le 20/11** (QEMU, § 14) |

Un jour = une session de travail soutenue. Total : **quatre à six semaines** ; l'inconnue principale reste ce
que les `syscall` obscurcis du module demandent (§ 2.5).

## 6. Décisions

Prises par l'utilisateur le 19/11 :

1. **glibc de compatibilité** (§ 3.2), pas la vraie glibc.
2. **Le module vient du canal de Google** : le serveur de mise à jour de composants de Chrome (composant
   `oimompecagnajdejgnnjijobebaeigek`, `.crx3`), avec en secours l'archive posée à la main. (Constat du 19/11 : la
   voie de Firefox mène au même `.crx3` de Google.)
3. **Téléchargement de développement** accepté : fait (§ 2.5).
4. **ClearKey d'abord dans Jet** (§ 3.4).

5. **Sonde à l'exécution sous Linux** autorisée : faite (§ 2.5).
6. **Capture générique dans le noyau** (§ 3.2) : le noyau transmet à l'hôte les `syscall` qu'il demande à
   capturer, sans savoir quelle personnalité il imite.

## 7. Jalon 1 (19/11/2026) : ce qui est fait

* `src/lib/linux` : `linux.h`, `elf.c` (chargeur, rapport `linux_check`), `glibc.c` (une centaine de fonctions et
  données), `syscall.c` (22 appels de Linux).
* `src/sys/linuxhost` : `linuxhost --check FICHIER.so [-v]`, `linuxhost FICHIER.so FONCTION [ARG…] [--stubs]
  [--stats]`, `linuxhost --self-test` ; `test/lxtest.c`, le module d'essai, **construit pour Linux** par la gcc
  d'Ubuntu (glibc 2.43, PIC, `-fstack-protector-all`, `_FORTIFY_SOURCE`) et embarqué dans `linuxhost`.
* Construction : `wsl sh tools/linux/build.sh` (sortie `build/pc/linux`). Essai :
  `python3 -I tools/linux/test_pc.py` dans WSL après `. ~/korin-cloud/env.sh` (copie de `build/pc/esp`, `linuxhost`
  ajouté à l'initrd, `run linuxhost --self-test --stats` au moniteur du noyau, copie effacée).
* **Résultat sous QEMU PC** : 31 imports servis sur 31, 16 contrôles réussis sur 16 (constructeur, relocations,
  malloc et snprintf, `syscall (SYS_getpid)` et `SYS_write`, `EBADF` et `ENOSYS` de Linux, horloges et
  `nanosleep`, `ctype` de glibc, mutex et `pthread_once`, `getrandom`, destructeur) ; appels de Linux traduits :
  write, close, nanosleep, getpid ×2, clock_gettime ×3, getrandom.
* Pas encore : sur le ZBook, TLS du module, `syscall` directs (noyau), threads du module.

## 8. Étape 2c (19/11/2026) : le vrai module dans Korin

**Résultat sous QEMU PC** : le module de Google 4.10.3050.0, chargé par `liblinux` dans un process de Korin,
passe `InitializeCdmModule_4`, `CreateCdmInstance`, `Initialize` (`OnInitialized 1`) et
`CreateSessionAndGenerateRequest` : **il produit une requête de licence de 1702 octets**, comme sous Linux.
57 `syscall` directs capturés par le noyau et traduits ; appels de Linux servis (directs et par la glibc de
compatibilité) : read ×26, openat ×13, close ×13, mmap ×8, mprotect ×63, madvise ×82, rt_sigaction ×5, prctl ×7,
gettid ×15, getrandom ×8, getpid, uname, stat, clock_gettime. `ktest` 59/0 et `ptest` 22/0 sur le même noyau.

Ce qu'il a fallu :
* **glibc de compatibilité complète pour ce module** (177 imports, 0 absent) : `glibc_thread.c` (mutex,
  conditions avec horloge, verrous lecteurs/écrivains, sémaphores, attributs et création de threads,
  affinité : les objets de glibc portent une « boîte » de libposix dans leur premier mot, faite à la première
  utilisation), `glibc_misc.c` (fichiers, `struct tm` de glibc, locale « C » seule, caractères larges, libm,
  `getauxval`, `prctl`, `uname`, `dlsym`, `_setjmp`/`longjmp`, `_Unwind_*` de libgcc).
* **TLS du module** (`elf.c`) : modèle « general dynamic », `DTPMOD64`/`DTPOFF64`, `__tls_get_addr` avec un bloc
  par thread et par module fait à la première demande.
* **Exceptions C++** : le `.eh_frame` du module (par `PT_GNU_EH_FRAME`) déclaré au dérouleur de libgcc
  (`__register_frame`).
* **`/proc` fabriqué** (`syscall.c`) : `/proc/self/maps` au format de Linux (`linux_maps` d'`elf.c` : le
  programme hôte sous le nom `/usr/lib/x86_64-linux-gnu/libc.so.6`, puisque la glibc de compatibilité y vit, les
  segments du module avec son chemin, la pile), `/proc/net/unix` réduit à son en-tête ; `rt_sigaction` gardé
  sans être installé ; `open`/`openat` aux drapeaux de Linux, `stat` à la `struct stat` de Linux, `pread64`,
  `uname` (« Linux 6.8.0-korin »), `getrlimit`, `prctl`, affinité, **`futex`** (sur ceux de Korin).
* **Noyau : l'arène haute** (`src/kernel/core/mm.c`, `docs/ABI.md`) : PartitionAlloc, l'allocateur de Chromium
  dans le module, réserve au départ **32 Go alignés sur 32 Go** ; l'arène de `VM_MAP` n'a que 64 Go. Sur le PC,
  ce qui ne tient pas dans l'arène va dans une arène haute `0x1000_0000_0000`–`0x7000_0000_0000` ; les adresses
  habituelles ne changent pas (une demande qui échouait réussit, rien d'autre ne bouge).
* **La sonde dans Korin** : `tools/linux/cdmprobe/cdmprobe.cpp` se construit pour Linux (`run.sh`) et pour Korin
  (`WIDEVINE_SO=… sh tools/linux/build.sh`, module embarqué, essai `python3 -I tools/linux/test_pc.py --probe`).

Reste pour l'étape 3 : `UpdateSession` avec une vraie licence et `Decrypt` (ce qu'ils demandent n'a pas encore
été parcouru), le stockage (`CreateFileIO`), les threads du module en vrai, et le ZBook.

## 9. Étape 3 (19/11/2026) : une vraie licence, et le déchiffrement, dans Korin

**L'hôte** (`src/sys/cdmhost`, commun à Linux et Korin) :
* `host.h`, `host.cpp` : `CdmHost`, l'implémentation de `cdm::Host_10` : tampons, minuteries et tâches servies
  par `pump ()` dans le thread du module (comme Chromium), heure, stockage (`CreateFileIO` : un fichier par nom
  dans un dossier, écritures atomiques), identifiant de stockage (32 octets aléatoires gardés), protection de
  sortie (« aucun lien »), défi de plateforme (réponse vide) ; les événements du module vont à un `CdmClient`
  (l'essai aujourd'hui, le serveur IPC de `cdmhost` pour Jet ensuite). `CdmBuffer`, `CdmDecryptedBlock`,
  `CdmVideoFrame`.
* `cenc.h`, `cenc.cpp` : lecture minimale des MP4 chiffrés (pssh de Widevine, tenc, schm, frma, avcC, esds ;
  fragments : tfhd, tfdt, trun, senc) et passage du H.264 en Annex B. Dans Jet, ce travail sera celui du lecteur
  MSE.
* `cdmtest.cpp` : l'essai de bout en bout ; HTTPS par OpenSSL (comme `oauthd`).

**Le flux d'essai** : la démonstration publique « Angel One » du lecteur Shaka (Google), H.264 144p et AAC,
`cenc`, avec un début en clair (« clear lead » : les fragments sans `senc`) ; la licence vient de son serveur de
test (`cwip-shaka-proxy.appspot.com/no_auth`) ; seul le premier Mo de chaque fichier est téléchargé.

**Résultats** (identiques sous Linux et dans Korin) :
* requête de licence de 1714 octets, réponse HTTP 200 de 1228 octets, `UpdateSession` : **5 clés utilisables**
  (dont celles de la vidéo et de l'audio) ;
* vidéo : `Decrypt` refusé sur les 100 échantillons chiffrés (attendu) ; `DecryptAndDecodeFrame` : **300 images
  I420 192×144, 0 erreur** ;
* audio : **188 échantillons chiffrés sur 188 rendus en clair** ; sous Linux, les 564 trames AAC se décodent par
  libavcodec sans erreur ;
* **empreintes identiques** des sorties entre Linux et Korin (vidéo `fabf757bb7928937`, audio
  `85bba6b1d46b3472`) : dans Korin, les mêmes octets déchiffrés et les mêmes images. (Cette empreinte vidéo
  laissait de côté les images rendues à la fin, par des entrées vides ; leur nombre dépend du décodeur du
  module, d'une exécution à l'autre. Depuis le 20/11, elle couvre les 300 images : `967a04bdc5ec8b46`, stable.)
* dans Korin : 1058 `syscall` directs du module capturés et traduits ; réseau par `netd` (carte USB de QEMU,
  DHCP, DNS, NTP), TLS par OpenSSL avec le paquet de certificats `/etc/ca-bundle`.

**Comment le refaire** : sous Linux, `sh tools/linux/cdmtest.sh …/libwidevinecdm.so` ; dans Korin (WSL, QEMU),
`sh tools/linux/build.sh` puis `WIDEVINE_SO=…/libwidevinecdm.so sh tools/linux/test_cdm_pc.sh` (noyau d'essai,
arbre minimal avec `/widevine`, disque d'essai de 160 Mo effacé à la fin ; `cdmtest` lancé par `init2` comme un
service, sortie copiée dans le journal du noyau).

Reste : le serveur IPC de `cdmhost` et le client de Jet (étapes 4 et 5), l'essai sur le ZBook, les licences
persistantes (le manifeste n'en a pas), les flux `cbcs` (motif) et VP9/AV1.

## 10. `cdmhost` et son client (20/11/2026)

* `src/sys/cdmhost/cdmhost.cpp` : le process de l'hôte, lancé par le client avec deux sockets (`--control FD
  --media FD --module CHEMIN [--storage DOSSIER]`) ; un seul thread : le module est appelé et ses rappels servis
  (`CdmHost::pump`) entre deux messages ; la fin du contrôle l'arrête. Le module se charge par `module.cpp`
  (`liblinux` dans Korin, `dlopen` sous Linux).
* `proto.h`, `proto.cpp` : le protocole (§ 3.3) : messages tramés (longueur, type, champs), le contrôle pour les
  sessions et les événements, le média pour les requêtes synchrones des fils de décodage (`CDM_DECRYPT` pour
  l'audio, `CDM_INIT_VIDEO`, `CDM_DECODE_VIDEO` qui rend une image I420). Les échantillons et les images vont dans
  les messages ; la mémoire partagée viendra si le débit le demande.
* `cdmclient.h`, `cdmclient.cpp` : le client (pour Jet) : `socketpair`, `fork` et `exec` de cdmhost (Korin garde
  les descripteurs sans `FD_CLOEXEC` à l'`exec`), les requêtes, les événements ; le média sous un verrou.
* `ipctest.cpp` (`sh tools/linux/ipctest.sh MODULE`) : le scénario de `cdmtest` par cdmhost. **Sous Linux :
  licence, 188 échantillons audio sur 188, 300 images, mêmes empreintes que `cdmtest`** (audio
  `85bba6b1d46b3472`, vidéo `967a04bdc5ec8b46`). Dans Korin : `ESSAI=ipctest sh tools/linux/test_cdm_pc.sh`.

## 11. Étape 4 (19-20/11/2026) : EME Clear Key dans Jet

**La bibliothèque média de Jet (`av`, d'Onyx : `src/extra/onyx-libs/av`, pas suivie)** reçoit le chiffrement
commun par un correctif suivi, `tools/webkit/av-patches/0001-cenc.patch`, que `tools/webkit/glue.sh` applique une
fois (noté dans `src/extra/onyx-libs/.korin-patches`), comme `fetch.sh` pour WebKit :
* `av_mp4.c` : une entrée `encv`/`enca` est relue comme son format d'origine (`sinf`/`frma`), la piste garde
  `encrypted`, son KID et son schéma (`tenc`, `schm`) ; les boîtes `pssh` du `moov` sont gardées entières
  (`av_demux_init_data` : les données d'initialisation « cenc » d'EME) ; le `senc` de chaque fragment donne à chaque
  paquet son `struct av_crypt` (IV, sous-échantillons, motif). Un fragment sans `senc` est en clair (le début en
  clair des flux).
* `av_store.c` : le chiffrement suit chaque trame dans le magasin de MSE (copié à la sortie, libéré au retrait) ;
  une piste chiffrée compte dans les plages tamponnées.
* `av_player.c` : `av_player_set_decryptor` : le déchiffreur de l'hôte est appelé par les fils audio et vidéo
  avant le décodage ; tant qu'il rend `AV_AGAIN` (pas encore de clé), le fil attend sur ce paquet.
* `av_crypt.c` (nouveau) : AES-128 maison (la bibliothèque n'a pas d'autre dépendance), `av_decrypt` : `cenc`
  (CTR, un flux de clé continu sur les parties chiffrées des sous-échantillons), `cens`, `cbcs` (CBC à motif, IV
  repris à chaque sous-échantillon), `cbc1`.
* **Essai natif** (`sh tools/linux/avcenc_test.sh`) : vecteur de FIPS-197 ; sur le flux Clear Key public « Angel
  One » de Shaka (clé publique), 2438 paquets audio et 1300 vidéo chiffrés déchiffrés par `av`, puis décodés par
  libavcodec : **2814 trames AAC et 1500 images H.264, 0 erreur**.
* Garde : `av_packet` et `av_track` ont des champs de plus ; `build-jet.sh` refuse de lier si `av.h` est plus
  récent que `libWebCore.a` (WebKit d'abord), et refait `libonyxav.a` quand les sources d'`av` ont changé.

**WebKit** (correctif `tools/webkit/patches/0008-eme-clearkey.patch`, essayé à part, puis rangé le 20/11 : la
prochaine construction de WebKit le prend, et reconstruit tout WebCore, l'option touchant `cmakeconfig.h`) :
* `OptionsOnyx.cmake` : `ENABLE_ENCRYPTED_MEDIA` avec le moteur ;
* `CDMOnyx.h`, `CDMOnyx.cpp` (nouveaux) : la fabrique du CDM Clear Key de WebKit (`CDMFactoryClearKey`,
  générique) enregistrée pour Onyx, et `CDMProxyOnyx`, le côté déchiffreur d'une instance : les fils de décodage
  lui demandent la clé d'un KID (`copyKey`) et lui disent qu'ils attendent (`waitingforkey`) ;
* `MediaPlayerPrivateOnyx` : `cdmInstanceAttached`/`Detached`, `attemptToDecryptWithInstance`, `waitingForKey`,
  `encryptedInitializationData` (l'événement `encrypted` de l'élément), et le déchiffreur passé à `av` :
  `av_decrypt` avec la clé de la session ;
* `MediaSourcePrivateOnyx` : chaque `MediaSampleOnyx` garde son `av_crypt` à travers les `TrackBuffer` de WebCore
  jusqu'à la file du lecteur ; un segment d'initialisation chiffré signale ses `pssh` au lecteur.
* Construction à part (copies `webkit-eme`, `webkit-gl-eme`, construction `webcore-eme` ; `tools/mesa/build-webkit.sh`
  prend `WK_BASE`, `WK_GL`, `WK_BUILD` ; `build-jet.sh` prend `JET_OUT`), pour ne pas toucher au Jet des autres
  sessions.
* **Jet** (`src/extra/onyxports/jet/engine_webkit.cpp`) : WebKit2 demande au navigateur la permission d'un système
  de clés (`decidePolicyForMediaKeySystemPermissionRequest`) ; sans réponse, `requestMediaKeySystemAccess`
  échoue (`NotSupportedError`). Jet accepte Clear Key, et pour l'instant tout système de clés (la case des
  réglages en décidera pour Widevine, étape 7).
* Essai dans Jet : `tools/webkit/tests/eme-clearkey.html` (MSE + EME Clear Key, la licence JSON faite par la page)
  et `tools/linux/test_eme_jet.sh` (disque d'essai avec ce Jet, page servie par WSL sur `10.0.2.2:8642`, Jet lancé
  par telnet ; la page envoie chaque ligne de son journal au serveur de l'essai, d'où le verdict).
* **Résultat sous QEMU PC (20/11)** : `requestMediaKeySystemAccess` → `setMediaKeys` → `encrypted` (cenc,
  52 octets) → requête de licence → clé `usable` → MSE (60 s tamponnées) → lecture au-delà de 8 s (le début du
  chiffré), **249 images décodées : « EME-CLEARKEY : RÉUSSI »**. Capture : la vidéo s'affiche dans Jet.
* Dans Korin aussi, `cdmhost` par son client (`ESSAI=ipctest sh tools/linux/test_cdm_pc.sh`) : licence, audio et
  vidéo, **mêmes empreintes que sous Linux**.
* Reste : la console des pages ne va pas dans le journal du noyau malgré `/etc/web-console` (à voir) ; essai sur
  le ZBook.

## 12. Étape 5 (20/11/2026) : EME Widevine dans Jet

**av** (correctif `tools/webkit/av-patches/0002-external-decoder.patch`) : `av_decoder_external` (un décodeur donné
par l'hôte : ses appels et son contexte) et `av_player_set_decoder_factory` (la fabrique des décodeurs des pistes
chiffrées, appelée par les fils du lecteur au début d'une piste) ; le déchiffreur peut rendre `AV_OK` en laissant
`p->crypt` : le paquet va encore chiffré au décodeur de l'hôte. Le client de `cdmhost` (`cdmclient.cpp`,
`proto.cpp`, `cenc.cpp` de `src/sys/cdmhost`) est compilé dans `libonyxav.a` (`av.mk`) ; `glue.sh` met son
dossier dans la colle (`user/Libs/cdmhost`), d'où WebCore prend ses en-têtes.

**WebKit** (correctif `tools/webkit/patches/0009-eme-widevine.patch`, après 0008) :
* `CDMWidevineOnyx.h/.cpp` (nouveaux) : `com.widevine.alpha`, proposé seulement quand le module
  (`$HOME/.jet/widevine/libwidevinecdm.so`, ou `JET_WIDEVINE_CDM`) et l'hôte (`/apps/jet.app/libexec/cdmhost`, ou
  `JET_CDMHOST`) sont là. L'instance lance `cdmhost` (`CdmHostClient::spawn`, stockage
  `$HOME/.jet/widevine/storage`) ; un fil lit ses événements et les passe au fil principal (promesses, nouvelle
  session, message de licence, statuts de clés). Sessions temporaires seulement (le module Linux ne garde pas de
  licence), pas d'identifiant distinctif, robustesses `""`, `SW_SECURE_CRYPTO`, `SW_SECURE_DECODE` (L3).
* Les clés restent dans l'hôte : le magasin de clés de WebKit n'en garde que les KID et les statuts (le lecteur
  attend une clé `usable`). L'audio est déchiffré par l'hôte (`Decrypt`), la vidéo **décodée** par le module
  (`DecryptAndDecodeFrame`, H.264 passé en Annex B par `cenc_to_annexb`, images I420), par un décodeur externe d'av.
* `MediaPlayerPrivateOnyx` : la fabrique de décodeurs donne aux pistes vidéo chiffrées un décodeur qui choisit au
  premier paquet : celui de l'hôte (Widevine) ou celui de la bibliothèque (Clear Key, paquets déjà déchiffrés) ;
  un paquet resté chiffré le fait passer à l'hôte (le début en clair a pu partir avant les `MediaKeys`).
* `CDMInstance::ImplementationType::Onyx` (sous `PLATFORM(ONYX)`) : l'instance Widevine.

**Jet** : `build-jet.sh` met `cdmhost` dans `jet.app/libexec/` (PC seulement). Sous Korin, seul
`/apps/<nom>.app/main` est exécutable par règle : `/apps/jet.app/libexec` est ajouté à `skel/sd0/etc/exec.txt`
(comme `/apps/koton.app/plugins`), sinon `access (X_OK)` et `exec` le refusent.

**Essai** : `JET=… KS=widevine WIDEVINE_SO=… sh tools/linux/test_eme_jet.sh` (page
`tools/webkit/tests/eme-widevine.html`, début du flux Widevine « Angel One » de Shaka, licence du serveur de test de
Shaka par le réseau de QEMU ; noyau d'essai avec la capture et l'arène haute dans `build/pc/linux/kbuild`). **Sous
QEMU PC (20/11)** : `requestMediaKeySystemAccess` → `cdmhost` lancé (module 4.10.3050.0) → requête de licence de
1714 octets → licence HTTP 200 → **5 clés utilisables** → lecture jusqu'au bout des 60 s tamponnées, **1496 images
décodées par le module : « EME-WIDEVINE : RÉUSSI »**, la vidéo déchiffrée s'affiche. Beaucoup d'images sautées
sous QEMU (TCG, 192×144) : à mesurer sur le ZBook.

Reste : Spotify web et Netflix (étape 6 : se présenter comme Chrome sous Linux), la case des réglages (étape 7),
l'essai sur le ZBook ; les images passent par la socket (pas encore de mémoire partagée).

## 13. Étape 6 (20/11/2026) : les sites

* **User agent** (`src/extra/onyxports/jet/engine_webkit.cpp`) : sur les sites de contenus protégés (Netflix,
  Spotify, Disney+, Prime Video, Deezer, Max, Hulu, Canal+, TF1, France TV, Bitmovin, démo de Shaka), et seulement
  quand le module est installé, Jet se présente comme **Chrome sous Linux** (Safari recevrait FairPlay, que ce
  moteur n'a pas) ; ailleurs rien ne change (Safari macOS sur le PC).
* **`cdmhost` lancé au premier besoin** (une session, un certificat de serveur), plus à la création des
  `MediaKeys` : `shaka.Player.probeSupport` crée un `MediaKeys` par configuration essayée, d'où 14 hôtes et une
  machine écrasée au premier essai. Les demandes attendent que le module soit initialisé (`withHost`).
* **WebM chiffré pas encore lu** : `av` lit le chiffrement du MP4 (CENC), pas celui du WebM (Matroska
  `ContentEncryption`, octet de signal par bloc) ; Shaka choisissait le VP9 en WebM et le lecteur décodait des
  paquets chiffrés comme en clair. Le CDM Widevine retire donc les capacités WebM des configurations
  (`getSupportedConfiguration`) : les sites choisissent le MP4 (celui de Netflix, Disney+, Spotify). À faire un
  jour dans `av_mkv.c` (YouTube s'en sert pour ses films).
* **Essais sous QEMU PC** (`test_eme_jet.sh`, critère : 20 s de lecture et au moins 100 images) :
  * `KS=widevine` (flux local) : licence, 5 clés, **400 images à 20 s** ;
  * `KS=shaka` (`tools/webkit/tests/eme-shaka.html` : le lecteur Shaka 4.11.7 de jsDelivr, le flux DASH Widevine
    depuis Internet, MediaCapabilities avec système de clés) : **409 images à 20 s, 1396 au bout : « EME-SHAKA :
    RÉUSSI »** ;
  * `SITES=…` : netflix.com et open.spotify.com s'ouvrent normalement (page d'accueil, lecteur web avec ses
    titres), sans message de navigateur non pris en charge.
* **Pas essayé** : lire avec un compte Netflix ou Spotify (l'utilisateur seul s'identifie) ; la vitesse sur le
  ZBook (sous QEMU, beaucoup d'images sautées).

## 14. Étape 7 (20/11/2026) : la case des réglages

* **`cdmupdate`** (`src/sys/cdmhost/cdmupdate.cpp`, dans `jet.app/libexec`, construit par `build-jet.sh` avec OpenSSL
  et zlib du sysroot de Jet) : la voie de Chrome, le serveur de composants de Google
  (`update.googleapis.com/service/update2/json`, protocole Omaha 3.1, composant `oimompecagnajdejgnnjijobebaeigek`,
  Chrome 142 sous Linux x86-64). Sa réponse donne la version courante, ses adresses chez Google (`dl.google.com`,
  `edgedl.me.gvt1.com`… : les https d'abord), la taille et le SHA-256 de l'archive `.crx3`. `cdmupdate` la
  télécharge, vérifie taille et empreinte, saute l'en-tête `Cr24`, lit le zip (zlib) et en tire
  `_platform_specific/linux_x64/libwidevinecdm.so` (CRC vérifié, un ELF) et `manifest.json`. Installation dans
  `HOME/.jet/widevine` (fichiers neufs renommés : jamais un module à moitié écrit), avec `version.txt` et
  `checked.txt` (la date de la demande). La version installée n'est pas retéléchargée ; `--check` dit seulement s'il y
  en a une plus récente. Le client HTTP de `net.cpp` fait maintenant aussi GET, HTTP simple et redirections.
* **Jet** (`main.cpp`) : **Réglages > Lire les contenus protégés (Widevine)…**, montré seulement par un Jet qui sait
  s'en servir (`engine_protected_content` : WebKit avec EME, `cdmhost` et `cdmupdate` dans `jet.app/libexec`).
  Décochée : un dialogue explique (module propriétaire de Google, téléchargé chez lui, environ 22 Mo, mises à jour
  quotidiennes, Jet vu comme Chrome sous Linux sur ces sites) ; OK lance `cdmupdate` en arrière-plan, la boucle de Jet
  suit sa fin, la barre d'état et un dialogue disent la version installée ; la case est cochée. Cochée : un dialogue,
  puis le module et ses données sont effacés. Au démarrage, au plus une demande par jour (`checked.txt`), muette si
  rien ne change. Le user agent de Chrome et le CDM regardent la présence du module à chaque fois : pas besoin de
  relancer Jet. Aide de Jet (`help_fr.md`, `help_en.md`) : « Contenus protégés ».
* **Le module 4.10.3112.0** (la version que Google donnait le 20/11) importe 11 fonctions de plus que la 4.10.3050.0 :
  `__fxstat64`, `fcntl`, `_exit`, `getpagesize`, `strerror`, `rint`, `sigfillset`, `pthread_sigmask`,
  `pthread_getname_np`, `pthread_getschedparam`, `__udivti3` ; liblinux les sert (`glibc_misc.c`). Une mise à jour
  de Google peut donc demander d'en ajouter d'autres : `cdmhost` le dit (`linux_load : import absent : …`) et la page
  reçoit `NotSupportedError` ; `linuxhost --check` en donne la liste.
* **Essais sous QEMU PC** :
  * sous Linux d'abord : `cdmupdate` installe la 4.10.3112.0 (21 965 296 octets), puis répond « à jour » ;
  * `KS=update` (`test_eme_jet.sh`) : dans Korin, `cdmupdate` télécharge et installe le module chez Google en 8,7 s,
    puis la page joue avec lui : **406 images à 20 s, « EME-WIDEVINE : RÉUSSI »** ;
  * la case, par clics (`CLICKS=…` : la tablette USB de QEMU) : le menu Réglages la montre décochée, le dialogue
    s'ouvre, OK télécharge et installe (« Widevine : module 4.10.3112.0 installé »), la page rechargée joue, la case
    est cochée ; sans module, la page reçoit `NotSupportedError` (Widevine n'est pas proposé).
* Reste : essai sur le ZBook ; un compte Netflix ou Spotify (l'utilisateur) ; le WebM chiffré (§ 13) ; le Jet
  ordinaire n'a EME qu'après une construction de WebKit avec les correctifs 0008 et 0009 (puis la version du paquet
  `jet` et celle de `pc`, pour `etc/exec.txt`).

## Sources (19/11/2026)

* En-tête de l'interface CDM, copie de Firefox : https://searchfox.org/mozilla-central/source/dom/media/gmp/widevine-adapter/content_decryption_module.h
* Mises à jour de Widevine chez Mozilla (Balrog) : https://moz-releng-docs.readthedocs.io/en/latest/how-to/release/addons/widevine.html
* Dépendances du module de Linux (`libgcc_s`) : https://bugzilla.mozilla.org/show_bug.cgi?id=1843683
* Horloges et glibc ≥ 2.18 dans le bac à sable : https://bugzilla.mozilla.org/show_bug.cgi?id=1304220
* OpenWV : https://github.com/tchebb/openwv
* Netflix sur Asahi Linux : https://www.da.vidbuchanan.co.uk/blog/netflix-on-asahi.html
* Kodi `inputstream.adaptive` : https://github.com/xbmc/inputstream.adaptive
* EME de WebKit par Thunder (Igalia) : https://blogs.igalia.com/xrcalvar/2020/09/ et https://bugs.webkit.org/show_bug.cgi?id=213550
* Archive de Linux et `versions.txt` (guides de la communauté) : https://gist.github.com/ruario/3c873d43eb20553d5014bd4d29fe37f1
