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
* Lesquels de ces appels sortent vraiment, et avec quels numéros, se mesure en exécutant le module avec un piège
  sur les `syscall` venus de sa plage d'adresses (seccomp `SECCOMP_RET_TRAP` sur Linux, ou la personnalité de
  Korin elle-même). Pas encore fait : à décider avec l'utilisateur (§ 6).

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
Forme prévue (décision du 13/11, `armpi-linux-compat`) : un drapeau de personnalité par process ; une instruction
`syscall` dont l'adresse est dans une plage déclarée Linux (le module) n'est pas servie comme un appel de Korin
mais renvoyée à `liblinux` (comme `SECCOMP_RET_TRAP` ou *Syscall User Dispatch* de Linux), qui la traduit par
`linux_syscall` et reprend le module avec le résultat dans `rax`.

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
| `DECRYPT id, tampon partagé, key_id, iv, sous-échantillons, schéma cenc/cbcs, motif` | `DECRYPTED id, statut, taille` (le clair dans le même tampon partagé) |
| `GET_STATUS_FOR_POLICY promise, hdcp` | `EXPIRATION_CHANGE`, `SESSION_CLOSED` |

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
| 2b | Personnalité Linux du noyau du PC (renvoi des `syscall` de la plage du module à `liblinux`) | un `syscall` direct d'un module d'essai traduit | 1 à 2 jours |
| 2c | Les 108 imports, TLS du module (`__tls_get_addr`), `_Unwind_*`, threads | `InitializeCdmModule_4` et `CreateCdmInstance` passent | 3 à 5 jours |
| 3 | `cdmhost` : `Host_10/11` en C++, essai hors de Jet sur un serveur de licences de test public | une licence de démonstration obtenue, un échantillon déchiffré | 2 à 3 jours |
| 4 | EME ClearKey dans Jet (WebKit, MSE chiffré, CENC) | un flux ClearKey de démonstration joue | 3 à 5 jours |
| 5 | EME Widevine dans Jet par `cdmhost` (audio) | le lecteur web de Spotify joue un titre | 2 à 3 jours |
| 6 | Vidéo, user agent, codecs | Netflix et Disney+ jouent (720p) | 2 à 4 jours |
| 7 | Case des réglages, téléchargement chez Google, mise à jour | de la case à la lecture sans manipulation | 1 à 2 jours |

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

En attente : **exécuter le module avec un piège sur ses `syscall` directs** pour savoir lesquels il fait (sonde
native sous Linux avec seccomp, ou directement la personnalité de Korin, étape 2b). L'essai sous Linux a été
refusé par le contrôle des permissions de la session : à trancher par l'utilisateur.

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
