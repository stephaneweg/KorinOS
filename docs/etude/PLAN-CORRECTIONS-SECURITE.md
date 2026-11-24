# Plan de correction des failles de sécurité de la version PC

Rédigé le 24/11/2026 à partir de [AUDIT-SECURITE-PC.md](AUDIT-SECURITE-PC.md), sur `main` (df483dc6). **Rien n'est
encore implémenté** : les corrections viennent après le portage de Blender.

**La règle de ce plan, posée par l'utilisateur.** Avant toute implémentation, on vérifie en profondeur, en lisant le
code, qu'aucune correction ne casse le système. C'est fait ici pour chaque étape :

* tous les usages légitimes de ce qui change ont été cherchés dans le dépôt : `src/`, kits, libposix, korinrt, apps,
  `tools/`, patches des ports, scripts d'essai ;
* chaque étape dit ce qui en dépend, avec un verdict.

**Les verdicts :**

* **sans impact** : la liste des appelants est relue et aucun ne change ;
* **adaptation** : à faire *avant* ou *dans le même commit* ;
* **à essayer** : un risque qu'on ne tranche qu'en lançant, avec l'essai précis.

L'analyse a été faite par cinq lectures du code, et les points délicats ont été relus une seconde fois.

**Ce que l'analyse a changé.** Le brouillon des corrections contenait **onze propositions qui auraient cassé le
système** ; la §2 les liste avec leur remplacement. Toute correction ajoutée plus tard à ce plan doit passer par la
même analyse.

---

## 1. Règles de mise en œuvre

1. **Un commit, une étape.** Chaque commit garde `main` démarrable. Après chaque commit :
   * `.\build.ps1 test` ;
   * sous QEMU q35 : ktest, itest, mtest, posixtest, forktest, exectest, sigtest, threadtest, jobtest, le bureau, Jet
     et `dotnet run` ;
   * sous QEMU raspi4b quand du code commun au Pi change ;
   * enfin, sur le ZBook pour le matériel.
2. **Le Pi partage le code C.** vfs, fatfs, ramfs, netd, hidmanager, hwmanager, init2, imgblk, Elegant, menubar,
   panel, notifyd, clipd, clockd, shutdown, soundmenu, quickmenu, UIKit, AppKit, korinrt et libposix sont communs.
   Son noyau asm n'a pas d'uid : `SYS_PROC_UID` et `SYS_PROC_EXE` y rendent `E_NOSYS`.
   * **Tout contrôle d'uid traite `E_NOSYS` comme « uid 0, permis »**, à la manière de `vfs/pids.c:63` et
     `init2.c:493`.
   * Il traite `E_SRCH` (demandeur mort) comme un **refus**.
   * Ne pas recopier `desktop.c:218` (`!= 0`, donc `E_PERM`), qui refuse à tort sur le Pi.
3. **ABI :**
   * nouveaux appels système à la fin de `src/inc/syscalls.inc` (0x97 et suivants), en `E_NOSYS` dans
     `src/kernel/syscall.asm` du Pi, avec leurs noms ajoutés à sysstat, `docs/ABI.md` et l'aide de l'API ;
   * nouveaux opcodes de service à la fin ;
   * tables `.abi` des kits : ajout en fin seulement ;
   * `tools/check_constants.py` doit passer.
4. **Paquets :** chaque changement de fichiers d'un paquet fait monter sa version (`tools/pkg/versions.ini`,
   `mkrepo.py --bump`) au commit qui le change.
5. **Programmes liés statiquement** (bash, git, python, dotnet, rg, make, gcc, rust, Jet, Godot, webd…) : ils gardent
   l'ancienne libposix jusqu'à leur reconstruction.
   * Toute règle du noyau doit leur laisser un **repli qui marche**, même s'il est moins protégé.
   * Ne jamais compter sur un troisième argument nouveau qu'ils ne passent pas.
6. **À ne pas toucher pendant le portage de Blender** :
   * `remotepw` et telnetd : `tools/blender/pctest.py:48` en dépend ;
   * les correctifs python : `tools/blender/patches/python-3.13.patch` doit changer en même temps que
     `tools/python/korin.patch`.
7. **Restrictions par étapes.** Pour une restriction dont l'effet est incertain, on commence par un mode « compter et
   journaliser » (comme `access = log`), on mesure, puis on refuse : c'est le cas de `vm_map_page`, des quotas et
   d'`enforce`.

---

## 2. Propositions du brouillon qui auraient cassé le système

Toutes ont été relues dans le code. Les quatre premières sont celles du brouillon des corrections, qui reprenait le
§17 de l'audit ; leurs remplacements sont dans les étapes.

| Proposition | Ce qui cassait | Preuve | Remplacement |
|---|---|---|---|
| Avis traité seulement si `SVC_NEXT` rend `REQ_SIGNAL-1` | les signaux des pilotes arrivent avec `REQ_SIGNAL` (`svc.c:279`) ; sur le Pi, un avis n'a pas de numéro distinct (`svc.asm:580-590`) : interruptions et fins de process perdues | `svc.c:272-296` | un avis a un pid d'appelant **nul** (`msg+44 == 0`), sur le PC comme sur le Pi (E17) |
| `WS_SIGNAL` réservé à `rights` | UIKit l'appelle à chaque dommage de fenêtre (`uk_core.c:442,453`) | relu | bits {0, `WS_SIG_DAMAGE`} pour un process sans droits (E05) |
| `IMAGE_CREATE`, `SEAL`, `FORGET` réservés à `rights` | tout lancement par AppKit passe par `fc_image_load` (`appkit.c:1343-1348`) ; le repli est plafonné à 64 Mo, donc Jet, Godot et designcraft ne se lancent plus | relu | images rangées **par uid** (E07) |
| `fork` refusé pendant un chargement d'image | échecs au hasard dans les programmes à plusieurs fils (`subprocess`, MSBuild, `make -j`) | — | `vm_fork` ne recopie pas la plage des fenêtres (E06) |
| Empreinte de l'image au scellement | 0,3 à 0,6 s de plus à chaque lancement à froid de Jet ou de Godot | `sha256.c` | inutile après E06 et E08 |
| `vm_map_page` refuse une page présente | `elf.c:73` remappe volontairement une page commune à deux segments | — | `vm_map_page_replace` ; mode journal d'abord (E11) |
| NX sur la carte physique | le trampoline des cœurs secondaires ne pose pas NXE (`smp_trampoline.asm:47-50`), d'où une triple faute au démarrage | relu | NXE passé au trampoline, dans le même commit (E09) |
| `IO_PORT` sans 0xB2 | la batterie du ZBook passe par un SMI sur 0xB2 (`BATTERIE-PC.md:10-11`) | relu | 0xB2 réservé à acpid (E42) |
| `DEV_MAP` limité aux BAR | pcid (ECAM), acpid (régions AML), i2chid (SBREG, BAR0 qu'il assigne), keyvault (TPM) | — | `SYS_DEV_GRANT` donné par pcid, et des listes par programme (E42) |
| `rights` retiré aux programmes de session | `SVC_REGISTER` exige `rights` (`svc.c:117`) : notifyd, clipd, agenda, clockd, stickies, quickmenu, soundmenu, wifimenu, printd ne s'inscrivent plus | relu | `SVC_REGISTER` permis pour les noms non réservés, avec plafond par uid, **puis** retrait (E34) |
| `WF_TOP`, `WF_GRAB`, `WF_CURTAIN`, `WF_BACK` réservés à l'uid 0 | chaque menu déroulant, infobulle et dialogue d'UIKit (`uk_frame.c:548`), et les jeux, EmuKit, SDL plein écran, Météo | relu | règles de forme et de premier plan (E36) |
| `VFS_BOOT_BEGIN` réservé à l'uid 0 tout de suite | pkglib retombe sur la racine si l'appel échoue (`pkglib.h:189`) et écrit le noyau sous `/` | relu | pkglib abandonne sur échec, **puis** restriction (E25) |
| `MOUNT` permis à tous « sous `/media` » | monter la partition système sur `/media/x` contourne toutes les règles | `vfs.c:389-394` | source amovible ou réseau, pas déjà servie (E26) |
| `forbidden()` étendu à `sys/`, `etc/pkg`, `etc/elevate.ini` | le paquet `pc` livre `sys/` et `etc/` entiers (`packages-pc.ini:509`), et `commit()` bloquerait les mises à jour du système | relu | exception pour les paquets `required` et système, dans `install_file` **et** `commit` (E51) |
| Filtre « source 127/8 » dans netd | la boucle locale passe par `ip_input` (`stack.c:293-295`) : localhost casse | — | drapeau « venu de lo », puis filtre (E67) |
| ARP strict « réponses seulement » | `ip_output` jette la trame si la MAC est inconnue (`stack.c:300-304`) : premier SYN-ACK perdu | relu | garder les demandes ARP qui nous visent (E67) |
| NTP qui ne recule plus | Windows écrit la CMOS en heure locale, et Korin la lit comme UTC : recul d'une à deux heures à chaque démarrage du ZBook | `bg.c:518-521` | lier requête et réponse, plancher à la date de construction (E66) |
| Pids sur 64 bits | `MSG_PID` sur 32 bits dans tous les messages, `pid_t` int, groupes par pid négatif | `svc.h:3`, `korinrt.h:443` | générations modulo 32767 (E02) |
| Vider le slot d'abord dans `proc_finished` | `wait` rend `E_SRCH`, ou revient avant que les poignées soient fermées | `proc.c:242-262` | état P_DYING, `wait` attend la fin du démontage (E13) |
| `setpgid` vers un groupe existant seulement | `echo a \| cat` : le meneur a déjà fini, d'où SIGTTIN | `sh.c:399` | groupe permis si c'est un fils, vivant ou dans `exits[]` (E14) |
| `HID_OP_ABONNER` réservé à Elegant | la console de secours s'abonne aussi (`console.c:351`) | relu | réservé à `rights` (E19) |
| Filtre `CFG_WRITE` sur MSI, MSI-X et BAR | pcilib, iwl, hda et rtsx écrivent MSI, MSI-X et PM ; i2chid écrit BAR0 | `pcilib.c:149-173`, relu | adresse MSI dans `0xFEE00000-0xFEEFFFFF`, BAR seulement si elle vaut 0 (E41) |
| `WIN_CLOSE` réservé à menubar, panel et rdpd | home et switcher en mode poche | `home.asm:1539` | ajouter home et switcher (E36) |
| Telnet et FTP par comptes | `armpi_remote.py`, environ 30 scripts d'essai (dont celui de Blender), `ConnectDialog.cs`, l'envoi FTP du Pi | — | les outils d'abord, et un mode « secret commun » pour le Pi et les images d'essai (E70) |
| Attributs GPT « caché » par défaut | `korin.ps1` a besoin de la lettre D: ; journaux lus depuis Windows | `korin.ps1:91,111` | option d'osinstall seulement (E75) |
| Empreinte de Widevine compilée | Google met le module à jour, et l'ancien est révoqué | — | signature CRX3, ou empreinte dans un dossier uid 0 (E60) |
| `KV_UNLOCK` réservé à authd | `secret unlock` (`secret.c:72`) | — | authd identifié par son image, plus la carte (E23) |
| Retirer `web-probe.js` | les outils de mesure de Jet (`test_webapp.sh`, `test_eme_jet.sh`, `JET-*.md`) | — | sonde lue seulement si elle appartient à l'uid 0, ou `--probe` donné par l'UI (E58) |
| Plafond PNG à 16384 par côté | panoramas | — | côté ≤ 65535 et pixels ≤ 2²⁸ (E04) |
| Tout `exec` par des pids d'uid 0 tue les services (`sudo kill`) | « un process à `rights` ne reçoit de signal que d'un process à `rights` » casse pkg (relance du panneau) tant que le panneau a `rights` | `pkglib.h:916-921` | après E34 |
| Terminal exigé pour `AUTH_SUDO` sans mot de passe | l'installateur fait `popen("/bin/sudo …")` sans terminal | `installer/main.cpp:256` | l'installateur appelle `ui_elevate` d'abord (E38) |

---

## 3. Les étapes

**Format des étapes.** Chaque étape porte :

* les **constats** qu'elle corrige ;
* le **changement** ;
* la **non-régression** : les appelants relus et le verdict ;
* les **essais** ;
* les effets sur le **Pi, l'ABI et les paquets**.

Les phases suivent l'ordre des dépendances ; à l'intérieur d'une phase, l'ordre est conseillé.

### Phase 0 : préparation

**E00. Outils d'essai et état de référence.**

* **`sectest`** (`src/bin/sectest`, paquet de tests à part, hors image publiée) : un cas par constat. Chacun doit
  être refusé ; avant correction, il est marqué « attendu ouvert ». Les cas sont listés aux §16 et §9 de l'audit.
* **Références** :
  * relever la liste des essais qui passent aujourd'hui (QEMU q35, raspi4b, ZBook) ;
  * relever le CR0 de chaque cœur au démarrage (préalable à E08) ;
  * mesurer les pics de ressources de Blender, `dotnet build`, Jet sur un site lourd, `gcc -j16` (préalable à E45).
* **Journal du mode log sur le ZBook** : une semaine d'usage normal en relevant toutes les lignes
  « vfs : acces refuse (journal seulement) ». Préalable à E72-E74.

### Phase 1 : corrections locales, sans changement d'usage

**E01. Bornes des chargeurs et adresses canoniques.** Constats N3, N6, N29.

* **Changement :**
  * `offset > size || filesz > size - offset` et la même chose pour `phoff`, dans `elf.c:38,45`, `image.c:159,168` et
    `kits.c:78`, en gardant la garde `filesz != 0` (un `.bss` peut avoir son offset en fin de fichier) ;
  * `vaddr + memsz` sans débordement dans `kits.c` ;
  * `user_canonical()` pour `e_entry` (`elf.c:89`, `image.c:241`) ;
  * le gestionnaire de signal seulement si `> 1` (0 = `SIG_DFL`, 1 = `SIG_IGN`) ;
  * le restaurateur seulement s'il n'est pas nul ;
  * `tls` après la résolution de -1 (EmuKit, `thread.c:44`).
* **Non-régression :** sans impact. Aucun binaire légitime n'a ces valeurs.
* **Essais :** démarrage complet, mtest, posixtest, Jet, Godot, dotnet, bash, EmuKit.
* **Pi et paquets :** PC seulement ; paquet `pc`.

**E02. Entrée du noyau et pids.** Constats N5, N23, N21.

* **Changement :**
  * `MSR_FMASK = 0x44700` (`cpu.c:164`) ;
  * reprise sur une #GP à l'adresse d'`iretq`, sur le modèle de `msr_fault_fixup` (`idt.c:73`) ;
  * `sig >= NSIG` refusé (`signal.c:197`) ;
  * `slot_of[s]` vidé quand `proc_create` échoue (`proc.c:111-115`) ;
  * `generation[s] = (generation[s] + 1) % 32767`.
* **Non-régression :** sans impact.
  * Aucun code du noyau n'utilise NT, et `sigreturn` filtre déjà les drapeaux (`sigframe.c:141`).
  * libposix a `SIGRTMAX 63`.
  * La pagination de PROCLIST_FROM (`mcp/tools.c:96`, `cpuwatch.c:37`, `sysstat.c:68`) garde `pid & 0xFFFF` = slot.
* **Essais :** `pushfq` avec NT puis `syscall` (sectest) ; sigtest.

**E03. Mémoire partagée et canaux, corrections internes.** Constats N1, N8, N20c.

* **Changement :**
  * `SHM_TRUNC` suit la règle de `SHM_SET_SIZE` (`E_BUSY` si l'objet est mappé, `SEAL_SHRINK` respecté,
    `ipc.c:914`) ;
  * `BUF_FREE` teste et efface le bit sous `buf_lock` (`mm.c:922`) ;
  * `put_locked` et `free_msg` deviennent itératifs, avec une liste de travail (`ipc.c:111-148`).
* **Non-régression :** sans impact.
  * Le seul utilisateur de `SHM_TRUNC` est `shm_open(O_TRUNC)` de libposix (`posix/ipc.c:386`). WebKit, posixtest et
    Rust ne l'appellent pas avec `O_TRUNC`, et le cache `/file-*` ne s'en sert pas.
  * Un second `BUF_FREE` rend `E_INVAL` comme aujourd'hui.
* **Essais :** posixtest (memfd), Jet, sectest (`SHM_TRUNC`, double `BUF_FREE`).

**E04. Bornes dans l'espace utilisateur.** Constats G2, D3, A7 (`find`, `ecrire_mdp`), F5 (ramfs).

* **Changement :**
  * Elegant : `off > total || n > total - off` (`uikit_ops.c:31`), et la même chose dans `snapshot.c:136`, sur le
    modèle de `menu.c:57`. Vérifier aussi `uikit_ops.inc`.
  * UIKit `uk_image.c` :
    * IHDR de 13 octets ;
    * `bits` valides selon `color` ;
    * côté ≤ 65535 et `w*h` ≤ 2²⁸ ;
    * multiplication contrôlée ;
    * index de palette < taille de PLTE ;
    * BMP : `hdr` et palette bornés.
  * sudo : `find()` borné (`sudo.c:116`).
  * authd : `who` ≤ 31 caractères, validé comme `uf_name_ok` (`authd.c:330,348`).
  * ramfs : `pos + len` contrôlé (`ramfs.c:380-399`), et une poignée ne survit plus à un nœud réutilisé
    (`:273,310`).
* **Non-régression :** sans impact.
  * Les 407 PNG et BMP du dépôt font au plus 1800 px.
  * Blender, Forge et Vector ont leurs propres décodeurs.
* **Essais :** Viewer avec un panorama de 20 000 px ; les PNG piégés de sectest ; posixtest sur `/tmp`.
* **Pi et paquets :** UIKit et ramfs sont communs, donc paquets `pc` et `armpi`.

### Phase 2 : noyau

**E05. Avis réservés.** Constats I1, N17.

* **Changement :**
  * `sys_svc_call` et `sys_svc_forward` rendent `E_INVAL` pour un op ≥ 0xFFF0 lu en 64 bits non signés, après
    `copy_from_user` (`svc.c:185`) et sur `in` dans `forward` (`svc.c:506`). `ws_call` passe par `sys_svc_call`
    (`ws.c:66`), donc il est couvert.
  * `SVC_SIGNAL` est réservé à `rights`.
  * `WS_SIGNAL` : un process sans droits n'a que les bits {0, `WS_SIG_DAMAGE`}.
* **Non-régression :**
  * **Ops réservées : sans impact.** Aucun client n'envoie d'op ≥ 0xFF00 ; seuls le noyau et `WS_OP_EXIT` en
    produisent. Le seul `SVC_FORWARD` est celui de vfs (`path.c:439`), qui garde l'op du client.
  * **`SVC_SIGNAL` : sans impact pour l'usage normal.** Ses appelants (netd, `netif.c:91` dans iwl et usbnet, init2,
    ktest) ont tous `rights`. Seule exception, `nvme stop|flush` en ligne de commande, qui passera par sudo après E33.
  * **`WS_SIGNAL` : sans impact.** UIKit envoie `WS_SIG_DAMAGE` ; intelgpu et iwl sondent avec 0.
* **Adaptation :** l'aide de l'API, qui présente `SVC_SIGNAL` comme ouvert aux apps (`help/data/api/service_fr.md:26`,
  `ipc_fr.md:140` et leurs versions anglaises).
* **Pi (facultatif) :** le même refus dans `svc.asm` (`sys_svc_call:346`, et non `svc_call_k`), `sys_svc_forward` et
  le chemin `ws_call` de `ws.asm`, pour les services asm du Pi.

**E06. Fenêtres d'image et zones.** Constats N2, N24 (zones).

* **Changement :**
  * `vm_fork` ne recopie pas la plage des fenêtres d'image (`vm.c:243-259`) ;
  * `WIN_BASE` passe à une zone libre, par exemple 0x7000000000 (`image.c:29`) ;
  * `dma_next` et `dev_next` sont bornés et rendent `E_NOMEM` (`mm.c:966,986`).
* **Non-régression :** sans impact.
  * L'enfant n'a jamais besoin de la fenêtre.
  * Les seuls process qui mappent `WS_FB_VA` et `MAPC_BASE` ont `rights`, et ceux-là ne peuvent pas faire `fork`
    (`proc.c:538`).
  * AppKit utilise l'adresse rendue.
  * mtest attend `win >= 0x6C00000000` (`mtest.c:124`), ce qui reste vrai.
  * Ne pas bouger `WS_FB_VA` : `wtest.c:126` en dépend.
* **À surveiller :** intelgpu sur une longue durée (`ppgtt.c:52`), puisqu'il n'y a pas de `DMA_FREE`.

**E07. Images rangées par uid.** Constat N4 ; chaînes C5 et C6.

* **Changement :**
  * `im->uid` = uid du créateur ;
  * `image_get(path, size, need_sys, uid)` n'accepte une image que si `im->sys || im->uid == uid`, où `uid` est celui
    du **futur** process (`spawn_uid`, `proc.c:747`, pour `SPAWN_PRELOADED` d'Elegant et de login) ;
  * le scellement n'invalide que la même classe (`image.c:337-345`) ;
  * `FORGET` : `rights` et uid 0 oublient tout, les autres seulement leurs images.
* **Non-régression :** sans impact.
  * Le préchargement système passe par `IMAGE_PRELOAD`, qui exige `rights` (`image.c:415`, `init2.c:717`), et reste
    partagé.
  * `/bin/unload` (`appkit.c:1515`) n'oublie plus que les images de son uid. C'est voulu, à documenter.
* **Coût :** la mémoire double seulement avec plusieurs comptes actifs.
* **Essais :** lancer la même app sous deux comptes ; sectest (faux `/bin/sudo`).

**E08. CR0 sur tous les cœurs.** Constat B6.

* **Préalable :** le relevé de CR0 de E00. Le trampoline part de 0x60000010 et n'ajoute que PG et PE
  (`smp_trampoline.asm:52`). CD et NW seraient donc à 1 sur les cœurs secondaires, ce qui coupe les caches.
* **Changement :** `cr0 = (cr0 & ~(CD|NW|EM)) | WP | MP | NE` dans `cpu_init` (`cpu.c:167-170`).
* **Non-régression :** sans impact fonctionnel.
  * Toutes les écritures voulues du noyau dans des pages utilisateur passent par la carte physique :
    `setup_stack` (`proc.c:145`), `elf.c:65,79`, `kits.c:90,146`, `image.c:86,223,290`.
  * `copy_to_user` casse d'abord la copie sur écriture (`mm.c:383-405`), et une course est rattrapée par `mm_fault`
    (`idt.c:80-87`).
* **Essais :** QEMU `-smp 4`, puis le ZBook, puis ktest complet. Comparer les performances des cœurs secondaires
  avant et après, si CD était bien à 1.

**E09. NX et texte du noyau en lecture seule.** Constat N9.

* **Changement, dans un seul commit :**
  * l'EFER du cœur 0 est passé par `tramp_params.pad` (`smp.c:22`), et le trampoline fait `or eax, LME | [nxe]` ;
  * NX sur les entrées de 1 Go et de 2 Mo de la carte physique (`paging.c:82,88`) ;
  * `kernel.ld` exporte `__text_end` et `__rodata_end` : `.text` en lecture et exécution, `.rodata` en lecture et NX,
    `.data` et `.bss` en écriture et NX (`paging.c:104`) ;
  * `SPEC_CTRL` posé une fois, seulement si CPUID.7:EDX[26] est présent ; eIBRS si `ARCH_CAPABILITIES[1]`.
* **Non-régression :**
  * Personne n'exécute de code depuis la carte physique : pas de services runtime de l'UEFI, AML dans acpid, et le
    trampoline est **copié** depuis `.rodata`.
  * Rien ne modifie le texte du noyau à l'exécution (`isr_table` est dans `.data`).
  * Un MSR absent sous QEMU est rattrapé.
  * **Piège connu :** la pile `kmalloc` du cœur secondaire, avant `cpu_init`, impose NXE dans le trampoline.
* **Plus tard :** le réveil S3 aura besoin d'une page basse exécutable.
* **Essais :** QEMU `-smp 4`, le ZBook, ktest.

**E10. NMI, #MC et piles du noyau.** Constats faibles du noyau.

* **Changement :**
  * NMI (vecteur 2) et #MC (vecteur 18) lisent le MSR `GS_BASE` au lieu de deviner par CS ;
  * #MC sur l'IST 3 (`gdt_init`, et `ap_main` : `kmalloc(3*IST_SIZE)`) ;
  * une zone de piles du noyau en pages de 4 Ko, avec pages de garde, dont l'entrée PML4 est créée **dans
    `paging_init`, avant `smp_start`**.
* **Non-régression :**
  * Le seul utilisateur des NMI est `nmi_stop` à l'arrêt (`power.c:155`).
  * Aucun code ne convertit une adresse de pile en adresse physique.
* **Essais :** la pile épuisée de sectest (N20c) doit donner une #DF lisible.

**E11. `vm_map_page` ne remplace plus en silence.** Constat N24.

* **Changement :**
  * commit 1 : chaque remplacement qui aurait été refusé est écrit au journal ;
  * commit 2, après les essais : refus effectif, et `vm_map_page_replace` pour `elf.c:73`.
* **Essais entre les deux :** ktest, itest, mtest, posixtest, forktest, exectest, Jet, Godot, linuxhost. Le journal
  doit rester vide hors `elf.c:73`.

**E12. Copies utilisateur sûres.** Constat N7.

* **Changement :**
  * une table d'exceptions : `copy_*_user` rend `E_FAULT` au lieu de tuer le process ;
  * futex : la comparaison **reste sous `futex_lock`**, avec une lecture rattrapée par la table et refaite après une
    faute ;
  * la trame de signal est construite hors de `sig_lock` ;
  * les écritures directes après `user_check` (`ipc.c:491`, `svc.c:258`) passent par la copie sûre.
* **Non-régression :**
  * Les appelants gèrent déjà un code d'erreur de `copy_*`.
  * **Piège :** sortir la lecture du futex du verrou perdrait des réveils. C'est pourquoi la comparaison reste sous
    le verrou.
* **Essais :** sigtest, threadtest, .NET (beaucoup de futex), Jet, et la course de sectest (`munmap` pendant le
  futex).

**E13. Vie des process.** Constats N18, N19, N22.

* **Changement :**
  * état `P_DYING` : `proc_get` le refuse, `sys_wait` l'accepte ;
  * le slot est vidé au début de `proc_finished`, mais les `wait` ne sont réveillés qu'après le démontage ;
  * `p->vm = 0` ;
  * références sur les process et les threads (KILL, SIG_SEND, THREAD_KILL, VM_STATS, alarmes,
    `signal.c:64,184,214,432,452,506`, `ws.c:102`, `mm.c:748`) ;
  * référence sur `mm` et `vm` pendant `SVC_MAP_CALLER` (`svc.c:389-409`), attendue par EXIT et EXEC ;
  * `ws_proc_exit` sous `sched_lock`.
* **Non-régression :**
  * **Adaptation :** vider le slot d'abord ferait rendre `E_SRCH` à `wait`. C'est le cas de sh (`sh_run.c:994,1002`),
    libposix `waitpid` (`proc.c:861`) et Elegant (`desktop.c:146`). D'où `P_DYING`.
  * Si `wait` revenait trop tôt, les poignées vfs du fils seraient encore ouvertes (fstest, pipelines). D'où le réveil
    après le démontage.
* **Essais :** forktest, exectest, proctest, threadtest sous `-smp 8` ; ktest en boucle ; un pipeline de bash.

**E14. Groupes de process.** Constats N28, N27 (envoi à un groupe).

* **Changement :**
  * `SETPGID` accepte `pgid == pid`, ou un groupe qui existe dans la session, ou un **fils de l'appelant, vivant ou
    dans `exits[]`** ;
  * l'envoi d'un signal à un groupe parcourt tout le groupe (`signal.c:203-208`).
* **Non-régression :** **adaptation.** sh fait `SETPGID(pid2, pid1)` (`sh.c:399`) alors que pid1 peut avoir fini
  (`echo a | cat`), et bash, lié statiquement, fait de même.
* **Essais :** `echo a | cat` et `ls | head` dans vterm avec sh et bash ; Ctrl+Z et Ctrl+C ; jobtest.

**E15. Canaux bornés.** Constats N20a et N20b.

* **Changement :**
  * un second plafond par bout, à côté de `RING` (environ 4 096 messages et 1 024 poignées en file) ;
  * un plafond de poignées en transit par process (environ 4 096) ;
  * une profondeur d'imbrication d'au plus 4 ;
  * pas de ramasse-miettes pour l'instant.
* **Non-régression :**
  * Ne **pas** facturer les messages dans `bytes` : un flux d'écritures d'un octet bloquerait.
  * Les messages vides avec poignées (`SCM_RIGHTS`, `posix/ipc.c:184-206`) restent permis.
  * WebKit et Mesa transmettent quelques dizaines de poignées par message.
  * **Pas de refus des bouts qui contiennent des bouts** : WebKit transmet des connexions qui ont déjà des messages en
    file, et libposix des sockets.
* **Essais :** Jet (WebProcess et NetworkProcess), Python `multiprocessing` et `reduction.sendfds`, posixtest.

**E16. `SYS_SVC_REQ_INFO` (0x97).** Constat N16 ; prérequis de E17, E38 et E43.

* **Changement :** `(num) → {pid de l'appelant, pid du transmetteur, uid de l'appelant, pid de son père}`, pour une
  requête prise par le service qui appelle. Cet appel sert à :
  * `caller_uid()`, sans course sur la réutilisation des pids ;
  * la portée de « retenir », calculée par authd (A2) ;
  * savoir si une requête a été transmise par un autre service (N16).
* **Pi :** `E_NOSYS`, plus l'entrée dans `syscalls.inc`, sysstat, ABI.md et l'aide.

### Phase 3 : identité des appelants dans les services

**E17. Socle commun.** Constats I1 (défense en profondeur), I2.

* **Changement :**
  * `src/svc/common/caller.h`, avec sa variante libposix pour oauthd, keyvault et printd :
    * `svc_from_kernel(m)` : `M32(m,44) == 0` ;
    * `caller_uid(pid)` : `E_NOSYS` donne 0, `E_SRCH` et toute autre erreur donnent -1, c'est-à-dire un refus ;
    * `caller_rights(pid)` ;
  * chaque service ne traite `END`, `BIRTH`, `FORK`, `EXEC`, `SETUID` et `SIGNAL` que si `svc_from_kernel`. Un commit
    par service ou par petit groupe.
* **Services concernés :** acpid, authd, blkserv, busd, ext4fs, fatfs, gdrivefs, hidmanager, hwmanager, init2,
  intelgpu, iwl/wifisvc, keyvault, netd, oauthd, pcid, ramfs, smbfs, sound, usbd, vfs, vterm, `fsowner.c`, Elegant
  (`elegant.c:121`).
* **Non-régression :** sans impact sur le PC comme sur le Pi.
  * Les avis ont le pid 0 : `kzalloc` (`svc.c:532,572,604,628`) et `memset` pour les signaux (`svc.c:275`) sur le PC ;
    `str wzr` et `stp xzr` (`svc.asm:476,546,976,1061`) sur le Pi.
  * Tout appel réel a un pid ≥ 1. Un thread du noyau du Pi vaut 0x7FFFFFFF.
* **Essais :** PC et QEMU raspi4b (fin de process, clé USB, réseau, son).

**E18. init2, corrections sûres.** Constats M2 (repli en uid 0), D9, D20.

* **Changement :**
  * `LAUNCH` : `caller_uid < 0` (hors `E_NOSYS`) rend `E_SRCH` (`init2.c:500-502`) ;
  * `SVC_STOP` et `SVC_START` : `uid == 0 || E_NOSYS` (`init2.c:617-621`) ;
  * process épargnés à l'arrêt : `kept(nom) && PROC_RIGHTS(pid) == 1`, et de même dans `wait_gone`
    (`init2_x86_64.c:37-48,136`).
* **Non-régression :** sans impact.
  * Le seul appelant de `SVC_*` est Réglages élevé (`startup.cpp:835-850`).
  * Toutes les entrées de `keep[]` sont des services qui ont `rights`.

**E19. Contrôles par `rights`, service par service.**

Constats G1/P5, P6, P7, P8, P2 (prise), P11 (pilotes), D14 (i2chid), `IG_OP_STOP`, J7, F6 (disposition du clavier).

| Service | Changement | Appelants relus | Verdict |
|---|---|---|---|
| hwmanager | `HW_OP_PCI`, `HW_OP_USB`, `RETRAIT` réservés à `rights` | pcid, usbd (C et asm), stest, usbclasstest | sans impact |
| netd | `IF_REGISTER` réservé à `rights` | iwl, usbnet (netif) ; genet, wifi et usbnet sur le Pi | sans impact |
| hidmanager | `ABONNER` et `PILOTE` réservés à `rights` ; boucle bornée (≤ `ANNEAU_N` événements par passe, `tail` ramené) ; `KEYMAP` limité à `[A-Za-z0-9_-]` | ABONNER : Elegant, **console** (`console.c:351`), essais. PILOTE : usbhid, ps2kbd, i2chid, vncd, rdpd, gt911. KEYMAP : Réglages, installateur, setup (FR, US, BE, BE-COMMA). Les émulateurs reçoivent `WEV_PAD` d'Elegant | sans impact ; `KEYMAP`, `PADMAP` et `POINTER` restent ouverts |
| usbd | `CONTROL`, `BULK*`, `INTR_OPEN`, `ISO_OPEN`, `ADD_CONTROLLER` réservés à `rights` ; `DESCRIPTORS` ouvert | usbhid, usbstorage, usbnet, usbaudio, usbdtest ; aucune app ni aucun kit | sans impact |
| pcid | `CLAIM`, `RELEASE`, `CFG_WRITE`, `RESCAN`, `ANNOUNCE` réservés à `rights` (sans filtre de contenu, qui vient en E41) | les pilotes ; `ANNOUNCE` par init2 ; `RESCAN` n'a aucun appelant | sans impact |
| sound | `DRV_*` réservés à `rights` ou uid 0 | hda et usbaudio (sndif) ; sontest passera par sudo | sans impact |
| i2chid | `STOP` réservé à `rights` ou uid 0, **et** `stop` déplacé après `sysfront_rights()` (`i2chid.c:932-948`) | init2 ; la façade | adaptation dans le même commit |
| intelgpu | `STOP` réservé à `rights` ou uid 0, et `stop` et `status` passent par `front()` (`intelgpu.c:695`) ; `CHUNK_FREE` délie le morceau dans toutes les VM du client, puis vide le TLB du GPU | init2 (`init2_x86_64.c:194`) ; le winsys Mesa n'appelle jamais `CHUNK_FREE` | adaptation (façade) ; `CHUNK_FREE` sans impact |

* **Essais :** `build.ps1 test` ; QEMU q35 (clavier, souris, clé, réseau, son) ; le ZBook (touchpad i2c, Wi-Fi,
  GPU, son).

**E20. blkserv.** Constats P1, B14 ; chaîne C3.

* **Changement :**
  * `ATTACH`, `READ`, `WRITE`, `READ_D`, `PLAN`, `RESCAN`, `DMA`, `UHS` réservés à `rights || uid == 0` ;
  * `info[2] == pid` (propriétaire du tampon) ;
  * fenêtre = `min(m->b, info[1])` ;
  * `INFO` et `STATS` restent ouverts.
* **Non-régression :** sans impact. Appelants relus :
  * fatfs et ext4fs (lancés par vfs avec `rights`) ;
  * mkfs, gpt et osinstall (uid 0 par sudo ou init2 : **pas** `rights`, d'où « ou uid 0 ») ;
  * setup (`rights`) ;
  * blktest et usbclasstest (services).

  Chacun crée son tampon puis l'accorde : il en est le propriétaire, et la taille déclarée égale la taille créée.
  Seul blkbench (outil de mesure) demandera sudo.
* **Essais :** nvme sur le ZBook ; `gpt disktest` ; `installkey.py --plan` ; mkfs sous sudo ; imgblk sur le Pi.
* **Paquets :** `pc` (initrd : nvme, usbstorage, sdhci, rtsx, imgblk) ; `armpi` (imgblk).

**E21. fsowner.** Constat F4.

* **Changement :** `pid_vfs = getppid()` au démarrage, accepté seulement si `PROC_RIGHTS(ppid) == 1` et que son nom
  est « vfs ». En secours, l'ancien test de nom **plus** `rights`.
* **Non-régression :** à essayer. vfs lance chaque service de fichiers (`vfs.c:404-410`).
* **Essais :** PC et Pi, `ppid == vfs` pour chaque sorte de montage :
  * la racine, `/tmp` ;
  * une clé ;
  * SMB, gdrivefs ;
  * un volume partagé ;
  * vfs relancé.

**E22. `BUF_INFO` sans adresse physique pour les process sans droits.** Constat : fuite de `BUF_INFO`.

* **Non-régression :** sans impact.
  * Les lecteurs de l'adresse physique (blkserv, intelgpu) ont `rights`.
  * Le propriétaire et l'accord restent lisibles (intelgpu, `serve.c:282`).
* **Adaptation :** ktest (`ktest.c:188`) doit tourner avec droits, ou ne plus tester `bi[0]`.

**E23. keyvault.** Constats D7, D8, D17, A6 (premier volet).

* **Changement sans impact :**
  * un coffre n'est créé que si `stat` rend `ENOENT`, toute autre erreur donne `E_IO` (`keyvault.c:253-273,646-656`) ;
  * plafonds d'environ 4 Mio et quelques milliers d'entrées par coffre ;
  * plus de `unlink` avant `rename` (`:293`), puisque fatfs remplace le fichier d'arrivée (`fatfs.c:1161-1200`) ;
  * en-tête borné (`mkib`, `t`, `lanes`) ;
  * `OPENSSL_cleanse` dans `expire()` ;
  * un process qui n'est pas un interprète mais qui a `PEXF_SCRIPT` est refusé (aucun non-interprète n'appelle 0x95
    aujourd'hui).
* **Changement avec adaptation :** `KV_UNLOCK` réservé à authd, reconnu par son image (`/sys/svc/authd` et
  `PEXF_RIGHTS`), ou à la carte (`KV_CARD`). `secret unlock` (`secret.c:72-77`) passe par la carte, dans le même
  commit. Compteur d'échecs sans `SLEEP`.
* **Non-régression :**
  * Appelants : Budget, Courier, oauthd, Jet, remotepw, smbfs, wifisvc, sous les plafonds.
  * `KV_UNLOCK` : authd (`authd.c:710`) et `secret`.
* **À essayer :** un coffre posé sur ext4 (`ext4w_rename`).

### Phase 4 : vfs (encore en mode `log`)

**E24. Règles de vfs, sans changement d'usage.** Constats F1, F2 (nom de service), F3, F6 (homes).

* **Changement :**
  * vfs répond `E_EXIST` **avant** la règle (`path.c:722`) ;
  * le nom de service est limité à `[a-z0-9]` ;
  * `PATHMAP` est réservé à l'uid 0 ;
  * la comparaison sans casse s'applique à `access.c` (`sous()`) **et** à `homes.c` (`home_proprio`, `home_externe`)
    ensemble ;
  * homes : un uid sans compte n'a plus de home ; `/home/system` est réservé à l'uid 0 ;
  * le cas `home = /home` est réservé aux images de développement.
* **Non-régression :**
  * **`PATHMAP` : sans impact.** Le seul appelant est init2 (`init2.c:416`) ; `paths.txt` ne contient que des
    commentaires.
  * **Noms de service : sans impact.** Ce sont fatfs, exfat, ext4fs, ramfs, smbfs, gdrivefs.
  * **Casse : faible risque.** En ext4, la règle est plus stricte.
  * **`E_EXIST` d'abord :** sinon un `mkdir` korinrt ou asm d'un dossier protégé existant verrait `E_ACCES`.
* **Essais :** sous QEMU q35, en uid 1000, `cat /ETC/SHADOW~1.INI` et `/etc/shadow.ini.` ; noms courts de fatfs.

**E25. Volume de démarrage.** Constat B3 (deuxième volet).

* **Changement :**
  * commit 1 : pkglib **abandonne** l'installation quand `VFS_BOOT_BEGIN` échoue, au lieu de retomber sur `""`
    (`pkglib.h:189`), avec la version de `pkgman` montée ;
  * commit 2 : `VFS_BOOT_BEGIN` réservé à l'uid 0, en traitant `E_NOSYS` comme permis.
* **Non-régression :** **adaptation obligatoire dans cet ordre.** Sans le commit 1, un `pkg check` non élevé (Logiciels
  `J_CHECK`, `pkgui.cpp:456`) écrirait le noyau sous `/`. Les appelants sont pkglib et mkinitramfs (`/sbin`, le Pi).

**E26. Montage.** Constat F2 ; chaîne C4.

* **Changement :**
  * pour un uid ≠ 0, `MOUNT` exige un point sous `/media/` ou `/net/`, une source `blk.usb*`, `//…`, `smb:` ou
    `gdrive:` (et `blk.img*`), et une source qu'aucun autre volume ne sert déjà ;
  * `UNMOUNT` d'un uid ≠ 0 : volumes amovibles ou réseau seulement, jamais `root`, `/tmp`, `/boot` ni un `/home`
    monté.
* **Non-régression :** adaptation de la règle (voir la §2).
  * **Utilisateurs gardés :**
    * Disques (`disks/main.cpp:229,246`) ;
    * Fichiers : SMB, Google Drive, éjecter (`files/main.c:1774,2141,2154,2369`) ;
    * `mount`, `umount`, `eject`.
  * **Déjà en uid 0 :** osinstall, mkfs, usbstorage, init2.
  * `gpt` (test) passera par sudo.
* **Essais :** clé USB, SMB, Drive, éjection ; tentative sectest de monter `blk.nvme0p2` sur `/media/x`.

**E27. Fichiers temporaires et écriture.** Constats D11, F5 (`O_WRITE`).

* **Changement :**
  * **`O_EXCL` (bit 64, libre) :** appliqué d'abord dans chaque service (fatfs, exfat, ext4fs, ramfs, smbfs, gdrivefs),
    puis libposix l'envoie en gardant son `stat` de secours ;
  * **`O_WRITE`** vérifié dans ramfs et ext4fs ;
  * **`/tmp/<uid>` :**
    * créé par login ou init2 à l'ouverture de la session ;
    * `TMPDIR=/tmp/<uid>` quand `getuid() != 0` (`env.c:37`) ;
    * règle vfs : `/tmp/<uid>` pour ce compte seulement ;
  * **`tempname.c`** ne retombe plus sur `rand()`.
* **Non-régression :**
  * `O_EXCL` : un ancien service ignore le bit, et libposix garde son repli. Sans impact.
  * `O_WRITE` : **à essayer**, il peut révéler des écritures sur un fd ouvert en lecture (posixtest, ttytest, jobtest,
    les essais ext4 de gpt).
  * `/tmp/<uid>` : les ports liés statiquement gardent `/tmp`. Ce n'est pas une casse, seulement une protection plus
    tardive.

**E28. `V_OPEN_AS` (op 58, réservée à l'uid 0).** Prérequis de S2. Sans impact tant qu'aucun service ne s'en sert.

### Phase 5 : session, élévation, bureau

**E29. L'uid de session sort de `/tmp`.** Constat A1 ; chaîne vers telnet et console.

* **Changement, en un seul commit** (korinrt est lié statiquement à Elegant, init2 et la console) :
  * **init2 :**
    * ajoute `INIT_OP_SESSION_SET = 7` et `INIT_OP_SESSION_GET = 8` en fin (`armpi.inc`, `init2.h`) ;
    * `SET` n'est accepté que de `/sys/login` en uid 0 (`SYS_PROC_EXE`) ;
    * `GET` rend {uid, pid d'Elegant, génération}, et uid 0 sur le Pi.
  * **Elegant :** garde l'uid que login lui rend (il connaît `login_pid`). Il ne doit **jamais** appeler init2 de façon
    synchrone pendant `req_session_end`, qui lance `session_start` dans le traitement même (`desktop.c:222`) : risque
    d'interblocage.
  * **korinrt** (`user_session_uid`, `spawn_session_next`) demande à init2, et refuse l'uid 0.
  * **`/tmp/login.halt` et `/tmp/login.manual`** passent en mémoire dans Elegant et init2.
  * **Marque non autoritaire :** une marque `/tmp/session.open`, écrite par init2, reste pour shutdown (`:147`) et
    quickmenu.
* **Non-régression :** adaptation lourde. Écrivains et lecteurs relus :
  * login `:197` ;
  * Elegant `:120,122,134,161,289` ;
  * init2 `:92,579,594,770` ;
  * console `:109` ;
  * telnetd `:168` ;
  * shutdown ;
  * quickmenu `:421`.
* **Essais QEMU q35 :** connexion automatique, fermer la session, changer de compte, telnet, console, extinction
  depuis l'écran de connexion.

**E30. Réglages lus pour le bon compte.** Constat A4.

* **Changement :** `conf_path_uid(uid, nom)` dans korinrt (le home lu dans `users.ini`), utilisé par :
  * init2 : `session.off`, `session.manual`, `startup.txt` (`init2.c:774-777`) ;
  * Elegant : `desktop.c:234-236`, `pocket.c:94`, qui ont le même défaut.
* **Essais :** deux comptes, chacun avec son `startup.txt`.

**E31. Console de secours.** Constat B4.

* **Changement :**
  * dans le mode secours (`plat_shell_lost`, `init2_x86_64.c:263`), mot de passe demandé si le compte en a un
    (`AUTH_HASPW`, puis `AUTH_VERIFY`) ;
  * jamais d'uid 0 ;
  * si authd est mort, init2 le relance avant.
* **Non-régression :** à essayer. Les images d'essai (`sysroot.sh:12`, `testimage.py:77`, `gcctest/run.sh`) ont
  `shell=console` sans mot de passe, et restent ouvertes puisque leur compte n'a pas de mot de passe.

**E32. Contrôles « session ou uid 0 ».** Constats M1, P12 (ops du Wi-Fi), D14 (acpid), P11 (`REC_OPEN`).

* **Prérequis :** E29. Avant, l'uid de session vient d'un fichier que tous peuvent écrire, et un contrôle qui le lit
  ne protège rien.
* **Changement :** init2 `REBOOT` ; wifisvc `JOIN`, `LEAVE`, `FORGET`, `PRIORITY` et `COUNTRY` ; acpid `PROFILE` et
  `RELOAD` ; sound `REC_OPEN`.
* **Non-régression :** sans impact. Appelants relus :
  * shutdown, `reboot`, Réglages, pkgman, acpid et `pkg commit` ;
  * NetKit (Réglages, wifimenu, quickmenu, setup, installateur, `/bin/wifi`) ;
  * menubar, Réglages et `battery` ;
  * l'Enregistreur et audtest.

  Tous tournent sous la session ou en uid 0. Les shells telnet et console ont l'uid de session : c'est voulu.
* **Essais :** extinction depuis l'écran de connexion ; Réglages ; `/bin/wifi` ; l'Enregistreur.

**E33. `LAUNCH` limité pour un uid > 0.** Constat M2 ; chaîne C1.

* **Changement :** une liste de programmes lançables :
  * panel, menubar, voronoy, dock, stickies, notifyd, clipd, clockd, agenda ;
  * home, quick, switcher, quickmenu, soundmenu, wifimenu, shutdown, printd ;
  * `svc/oauthd`.

  L'uid 0 garde tout `/sys`.
* **Non-régression :** adaptation, **avant E34**. Appelants relus :
  * panel (`panel.c:623`) ;
  * menubar (`:903,984,1433`) ;
  * quickmenu (`:66,437`) ;
  * Réglages (`theme.cpp:690`, `panel.cpp:216`) ;
  * Notes (stickies) ;
  * NetKit (oauthd) ;
  * PrinterKit (printd).
* **Effet annoncé :** les façades de développement de `/sys/svc` (`i2chid`, `iwl`, `intelgpu`, `acpidump`, svctest,
  kittest, `nvme stop`) demandent désormais sudo.

**E34. Programmes de session sans `rights`.** Constats D2, N15 (en partie), N27 (signaux vers les process à droits).

* **Changement :**
  * commit 1 : `SVC_REGISTER` permis sans `rights` pour un nom non réservé, avec un plafond par uid. Noms réservés :
    vfs, fatfs, `blk*`, netd, sound, `hid*`, init2, auth, keyvault, pci, usb, hw, wifi… Il faut aussi agrandir
    `SVC_MAX`, qui vaut 64 pour tout le système.
  * commit 2 : `SPAWN_UID` avec un uid ≠ 0 donne un fils **sans** `rights` (`proc.c:468-477`). Ni nouveau bit, ni
    changement dans AppKit ou korinrt.
  * commit 3 : un process à `rights` ne reçoit de signal que d'un process à `rights`.
* **Non-régression :**
  * **Inscription :** notifyd, clipd, agenda, clockd, stickies, quickmenu, soundmenu, wifimenu, printd, home, quick
    et switcher s'inscrivent : d'où le commit 1 d'abord.
  * **Aucun autre appel réservé :** voronoy, menubar, panel, login et authcard n'en font pas. Seul Elegant garde
    `DEV_MAP`, DMA, IRQ et `WS_REGISTER`.
  * **Le commit 3 attend le commit 2 :** pkg élevé tue panel et menubar (`pkglib.h:916-921`).
  * keyvault vérifiera par empreinte les programmes de session, qui pourront aussi faire `fork` et `exec`.
* **Effet annoncé :** `sudo kill <service>` ne marche plus.
* **Essais :** connexion ; menubar, son, Wi-Fi, notifications, presse-papiers, agenda ; relance du panneau après
  `sudo pkg install`.

**E35. Bus, notifications, presse-papiers.** Constats D15, D16, N12, G5 (presse-papiers).

* **busd :** préfixes réservés à l'uid 0 : `korin.power.`, `korin.net.`, `korin.audio.`, `korin.storage.`,
  `korin.usb.`.
  * Ne **pas** réserver `korin.clock.offset` (l'app Horloge), `korin.shell.*` (menubar, Réglages), `korin.notify.*`,
    `korin.media.*`, `korin.files.changed` ni `korin.clock.alarms`.
  * Plafond d'au moins 32 abonnements par pid (menubar en a environ 10). Sujets libérés à la mort du publieur.
* **notifyd :** `app` accepté de l'uid 0, ou si l'exe est `/apps/<app>.app/main` ; sinon le nom du process. Seuls
  `notify -a` et `notifier_app` le donnent aujourd'hui.
* **DROP_GET :** réservé au pid cible mémorisé dans `drag_drop`.
* **Presse-papiers du noyau** (`ws.c:208,228`) : réservé à `ws_pid`. Son seul utilisateur, `wtest.c:121`, est à
  adapter.
* **clipd :**
  * `CLIP_INFO`, `CURSOR`, `DELETE` et `CLEAR` réservés au widget `/sys/clipboard` ;
  * filtre par uid ;
  * `CLIP_GET` « premier plan » plus tard, avec une op d'Elegant. **À essayer :** SDL `HasClipboardText` et Jet
    `clip_serial` interrogent en arrière-plan.

**E36. Elegant : opérations sensibles et couches.** Constats G3, G4, G5.

* **Adaptation préalable :** Avalonia (`ArmpiWindow.cs:285-305`) passe de `WIN_LIST` à `WIN_POS`, avec la version du
  paquet dotnet et Avalonia montée.
* **Opérations réservées à une liste de programmes** (`SYS_PROC_EXE`) :
  * `SNAPSHOT`, `WIN_THUMB`, `WIN_READ`, `WIN_LIST` : panel, dock, home, switcher, screenshot (bin et app), taskman,
    korin-mcp, rdpd, vncd ;
  * `WIN_CLOSE` : rdpd, home, switcher ;
  * `MENU_COMMAND` : menubar.
* **Couches :**
  * menu, infobulle ou liste (TOP, GRAB, NOFOCUS) : taille bornée, appelant au premier plan, GRAB lâché au clic
    dehors ;
  * plein écran en couche 3 : pour l'app au premier plan ;
  * `WF_CURTAIN` : pour le panel ;
  * `WF_BACK` opaque : pour voronoy (Météo garde un `WF_BACK` non opaque) ;
  * `WS_OP_WIN_TOP` (`screen.c:236`) suit la même règle. Ses appelants : Jamendo, Média, Jet, Blender.
* **Non-régression :** à essayer. Menus de toutes les familles d'apps (UIKit, WinForms, Avalonia, SDL), jeux, EmuKit,
  terracube, l'installateur, screenshot, le mode poche, la vue d'ensemble.

**E37. vterm.** Constats D1, D4, D5.

* **Changement :**
  * `VT_SLAVE` accepté si l'appelant a l'uid du créateur, **ou** tient déjà un bout de ce pty, **ou** est en uid 0 ;
  * `VT_SPGRP` : groupe de la session du pty si `p->sid` est posé ; sinon un groupe de l'uid de l'appelant, ou un
    appelant en uid 0 ;
  * `PTY_MAX` passe à 64, avec 32 par uid et 2 places réservées à l'uid 0.
* **Non-régression :**
  * Le seul appelant de `VT_SLAVE` est `openpty` dans le même process (`tty.c:348`, `pty.c:61`). Console et telnetd
    passent l'esclave par héritage.
  * `exec 3</dev/pts/0` dans un shell telnet reste possible grâce aux détenteurs.
* **Essais :** jobtest ; une tâche de fond de bash sous telnet ; onglets du Terminal ; Claude Code ; linuxhost.

**E38. Élévation.** Constats A2, A3, A7.

* **Adaptation préalable :** l'installateur appelle `ui_elevate` avant ses `popen("/bin/sudo …")`
  (`installer/main.cpp:256,738,1214,1513`), et `/tmp/install` passe sous `/tmp/<uid>`.
* **Changement dans authd :**
  * `AUTH_SUDO` n'est accepté que de `/bin/sudo`, vérifié **par empreinte** et pas seulement par chemin (sinon N4
    rouvre la porte) ;
  * sans mot de passe, il exige un terminal ;
  * la portée de « retenir » est calculée par authd avec `SVC_REQ_INFO` (E16) ;
  * la portée 0 est liée au pid élevé ;
  * compteur d'échecs par uid, sans `SLEEP` dans la boucle ;
  * un fil par carte ;
  * Argon2id : les deux formats sont lus, et l'empreinte est réécrite en Argon2id au prochain `AUTH_PASSWD` ou
    `AUTH_SETPW`.
* **Non-régression :**
  * `AUTH_SUDO` n'a qu'un appelant, sudo (`sudo.c:147-170`) ; `ui_elevate` utilise `AUTH_ELEVATE`.
  * Le coffre reçoit le mot de passe en clair : sans impact.
* **Essais :**
  * connexion avec un `shadow.ini` ancien ;
  * sudo dans le Terminal ;
  * l'installateur sur la clé live ;
  * deux cartes ouvertes en même temps ;
  * vérifier si osinstall écrit des empreintes (`osinstall.c:258-261`).

**E39. Carte d'élévation et écran de connexion protégés.** Constat A5.

* **Changement :**
  * un drapeau WF ajouté en fin, accepté seulement de `/sys/authcard` et `/sys/login` en uid 0 : couche au-dessus de
    tout, avec un repère que seul Elegant dessine ;
  * authd affiche le chemin vérifié du demandeur.
* **Pi et UIKit :** le Pi n'en a pas. Une fonction UIKit éventuelle va à la fin de `uikit.abi`.

**E40. login, soundmenu, sudo.** Constats D10, D13, A7 (sudo).

* **login :** une erreur de `AUTH_HASPW` vaut « mot de passe demandé ». authd distingue « `shadow.ini` illisible »
  (erreur) de « pas d'empreinte » (`authd.c:273-290`, `719`).
  * Effet : sans authd, plus de connexion sans mot de passe.
  * **Essai :** arrêter authd puis fermer la session.
* **soundmenu :** ne lance rien tant que la session n'est pas ouverte (même marque que `at_login` de quickmenu).
* **sudo :**
  * guillemets seulement autour d'un argument qui contient espace, guillemet ou `\` : sans impact pour
    `osinstall /tmp/install/plan.ini` ;
  * `HOME` de system par `posix_spawn`.

### Phase 6 : noyau, compléments

**E41. Filtre de `CFG_WRITE` dans pcid.** Constat P2 (écriture en RAM par MSI).

* **Changement :**
  * MSI et MSI-X : adresse dans `0xFEE00000-0xFEEFFFFF` seulement (celles que rend `SYS_IRQ_MSI`) ; le contrôle reste
    permis ;
  * BAR : écriture seulement si elle vaut 0 ;
  * ponts (type 1) : refusés.
* **Non-régression :** adaptation (voir la §2) : pcilib (`pcilib.c:149-173`), iwl (`iwl.c:368-395`), hda
  (`plat_x86_64.c:70-92`), rtsx (PM, lien), i2chid (BAR0 à 0, `i2chid.c:476-490`).
* **Essais sur le ZBook :** i2chid, iwl, nvme, xHCI, hda.

**E42. `DEV_MAP` et `IO_PORT`.** Constat N15 ; constat faible sur `IO_PORT`.

* **Changement :**
  * nouvel appel `SYS_DEV_GRANT` (0x98), réservé à pcid : il dit au noyau les BAR de la fonction prise par chaque pid.
    pcid relit une BAR après un `CFG_WRITE` permis par E41.
  * Le noyau limite ensuite `DEV_MAP` à ces BAR, plus des listes par programme :
    * pcid : l'ECAM ;
    * keyvault : le TPM CRB ;
    * i2chid : SBREG ;
    * acpid : toute adresse qui n'est pas de la RAM.
  * `IO_PORT` : 0x92 refusé ; 0xB2 réservé à acpid, identifié par son image.
* **Non-régression :**
  * Utilisateurs relus : intelgpu (BAR0), nvme, hda, usbd, sdhci, rtsx et iwl sont dans la règle ; pcid, acpid,
    i2chid et keyvault sont dans les listes.
  * Le noyau du Pi rend `E_NOSYS` à `DEV_GRANT`, et pcid l'ignore.
* **Essais sur le ZBook :** la commande `battery` doit lire `_STA` (SMI) ; touchpad, TPM (coffre), GPU, son, Wi-Fi.

**E43. Informations sur les autres process.** Constats N25, N26 ; fuites du noyau.

* **Changement :**
  * `WAIT` : appelant père, ou même uid, ou uid 0 (uid ajouté dans `exits[]`) ;
  * `PROC_EXE` : même uid ou uid 0 (seul keyvault l'utilise, en uid 0) ;
  * `IMAGE_LIST` : les images `im->sys` restent visibles ;
  * statistiques d'un autre uid réduites : temps arrondi, threads, pages, mais pas de compteurs par appel système ;
  * `MEMBARRIER` ne vise que les cœurs qui exécutent ce `vm` ;
  * `KLOG` : sans vidage de registres pour un uid ≠ 0.
  * **`PRINT` préfixé par le pid, au format `[  123.456|pid]`, en deux commits :** sysfront passe d'abord sa fenêtre
    de 16 à 32 octets (`sysfront.c:138`), avec la version des paquets iwl, i2chid et intelgpu montée ; puis le noyau.
* **Non-régression :**
  * **`PROC_INFO` reste public :** PROCLIST rend la même chose, et menubar, notifyd, sh `alive()`, `waitpid`, netstat
    et mcp en dépendent.
  * Les statistiques réduites gardent le Gestionnaire des tâches, sysstat, cpuwatch et korin-mcp utiles.
  * `WAIT` : sh attend sudo (uid 0) en tant que père, et Elegant attend login.
* **Essais :** Gestionnaire des tâches, `ps`, `sysstat`, korin-mcp ; une app Avalonia qui alloue beaucoup (GC et
  membarrier).

**E44. Aléa.** Constat N14 ; prérequis de l'ASLR.

* **Changement :**
  * commit 1 : graine EFI_RNG_PROTOCOL dans `bootx64`, passée par `bootinfo` ; RDSEED et RDRAND vérifiés ; jitter
    haché ; graine sauvée sur disque en plus ;
  * commit 2 : `getrandom` bloque tant que l'amorçage n'est pas fait (en principe jamais, car l'amorçage précède le
    premier process).
* **Non-régression :**
  * authd tire un sel et un jeton au démarrage, avant la session : l'amorçage doit précéder le premier process.
  * QEMU `-cpu max` a RDRAND. **À vérifier :** le `-cpu` de `tools/ci/boot_test.py`.

**E45. Quotas par uid.** Constat N13.

* **Changement :**
  * commit 1 : compter et journaliser (process, threads, pages, poignées, SHM, BUF, canaux, services) ;
  * mesures (E00) ;
  * commit 2 : refuser.

  Ordres de grandeur pour l'uid de session : environ 512 process, environ 8 192 threads, plusieurs Go de SHM, pas de
  plafond de pages mais une réserve de quelques dizaines de Mo pour l'uid 0.
* **Non-régression :** à essayer. Les BUF ne sont créés que par des services d'uid 0, et le TBB de Blender, le GC de
  .NET et WebKit sont gros en threads.

**E46. Noms SHM et cache `/file-*`.** Constats N10, N11, X1.

* **Changement :**
  * **commit 1 :**
    * le propriétaire et le mode passent par des **bits de `flags`** de `SHM_OPEN` (bit « mode valide », mode dans les
      bits 16 à 27), **jamais par un troisième argument** ;
    * par défaut, propriétaire seul, sans exception pour l'uid 0 ;
    * drapeau « scellable » pour un objet nommé ;
    * propriétaire dans le `pad` de `HANDLE_INFO`, avec un bit « valide ».
  * **commit 2 :** compteur de projections partagées inscriptibles par objet. `VM_PROTECT` refuse W sur une région
    `VMK_SHARED` **sans copie sur écriture** d'un objet scellé, et `SEAL_WRITE` est refusé s'il existe une projection
    inscriptible.
  * **commit 3 (libposix) :**
    * `/file-*` est rempli, démappé, puis scellé (WRITE, SHRINK, GROW, SEAL) ;
    * le lecteur vérifie le propriétaire et les sceaux ;
    * sur le Pi (`ipc.asm` ignore les bits inconnus), `kinfo` est mis à zéro et un échec de scellement est toléré.
* **Non-régression :**
  * **Vieux binaires liés statiquement :** ils se dégradent sans casser. `SHM_OPEN(CREAT|EXCL)` rend `E_EXIST`, puis
    `E_PERM`, puis `MAP_FAILED`, et ils retombent sur une copie privée (`posix/ipc.c:476`). Le prix est la perte du
    partage de pages entre comptes.
  * **Projections privées en copie sur écriture :** elles restent inscriptibles, ce qui garde les relocations de .NET
    et le cache de libposix (`K_VM_COW`).
  * **IPCSemaphore de WebKit :** pas concerné, il n'utilise que SHRINK, GROW et SEAL.
* **Essais :** posixtest (memfd, sceaux), Jet, `dotnet run`, linuxhost (node), deux comptes qui lancent dotnet.
* **Plus tard :** objets créés par vfs (volume, inode), un chantier séparé.

**E47. Surfaces partagées.** Constat J6.

* **Changement :**
  1. Liste de pids accordés par tampon (8 à 64 places, la plus ancienne remplacée), `-1` encore accepté.
  2. intelgpu accorde sa page d'état à chaque client à l'ouverture (`serve.c:257`, `engine.c:726`).
  3. Le code de liaison de WebKit accorde au pid du WebProcess (`onyx_surface_grant`, x86 seulement) et le refait
     après un changement de process. Jet reconstruit, version montée.
  4. Après la publication de Jet : `-1` refusé, ou réservé à `rights`.
* **Non-régression :** adaptation dans cet ordre. Sinon un ancien Jet affiche des pages blanches (`onyx_surface_map`
  rend NULL).

**E48. Services par uid.** Constats O1, D6.

* **oauthd :**
  * il s'inscrit sous `oauth.<uid>` ; NetKit (C **et** asm) ouvre `oauth.<getuid>` (`oauth.0` sur le Pi) ;
  * gdrivefs (uid 0) reçoit de vfs l'uid du compte qui a monté, ouvre `oauth.<uid>` et lance oauthd en `SPAWN_AS` ;
  * oauthd accepte son uid, ou un appelant `rights` ou d'uid 0 ;
  * le tout dans un seul commit. Une fonction `oauth_*_for` éventuelle va à la fin de `netkit.abi`.
* **printd :**
  * service `print.<uid>` (C et `printerkit.asm`) ;
  * spool `/var/spool/print/<uid>` ;
  * `output` refusé hors du HOME de l'appelant.
* **Non-régression :** à essayer.
  * oauthd range ses jetons dans le coffre de **son** uid : une instance unique en uid 0 perdrait les coffres.
  * Appelants relus : NetKit, Fichiers, l'applet, korin-mcp, gdrivefs.
* **Essais :** Fichiers, Google Drive et `gdrive:` ; un second compte ; une impression en PDF.

**E49. usbd lié au périphérique.** Constat P6, deuxième volet.

* **Changement :** hwmanager rend le pid lancé dans la réponse de `HW_OP_USB` (`M64(msg,8)`), et usbd réserve ce
  périphérique à ce pid.
* **Non-régression :** à essayer.
  * usbd n'a qu'un fil, donc pas de course.
  * La liaison se fait par périphérique, pas par interface (pilotes composites).
  * Le Pi ignore la réponse.
* **Essais :** périphériques composites ; clé retirée puis remise ; pilote relancé ; second xHCI.

### Phase 7 : paquets et démarrage

**E50. Outils de publication.** Préalable de K2.

* **Changement :**
  * `serial` dans `[repo]` **après `board`** ;
  * les indices de `mkrepo.py:405` décalés ;
  * `serial` retiré de la comparaison (`:409`) ;
  * `pub.py:467-472` l'incrémente ;
  * `checkrepo.py` vérifie qu'il augmente, et reprend la règle de `forbidden` étendue (exception des paquets requis),
    recopiée dans `mkrepo.py:71`.
* **Non-régression :** sans impact. Les anciens `pkg` ignorent les clés inconnues (`pkglib.h:1035`).

**E51. pkglib** (paquet `pkgman`, mis à jour avant le système). Constats K1, K2, K3, K4, L4.

* **Clé :**
  * clé publique compilée, et une liste de clés ;
  * `key =` de `pkg.ini` ignoré (`pkglib.h:983`) ;
  * `skel/sd0/etc/pkg/onyx.pub` est bien suivi par git. `PAQUETS.md:191` dit le contraire et est à corriger.
* **Numéro de série :** gardé **par URL de dépôt** ; absent compte comme 0 ; pas d'expiration pour `--root` ni pour
  les dépôts locaux.
* **`forbidden()` étendu**, avec l'exception des paquets `required` et système (`pc`, `armpi`, `pkgman`), **dans
  `install_file` et dans `commit`** (`:1439,1449`).
* **Téléchargement :**
  * dans `RP(PKG_DL2)` (`/var/pkg/dl`), purgé au démarrage de pkgd ;
  * la place sur la partition système est vérifiée (jusqu'à 90 Mo par archive ; attention au Pi).
* **Noms** validés `[a-z0-9._+-]`, ce qui couvre tous les noms des deux index.
* **`proc_kill`** par pid et par uid (repli par nom sur le Pi) (`:916-945,1348`, `pkg/main.cpp:411`,
  `pkgd/main.cpp:103`).
* **Non-régression :** adaptation (voir la §2 pour `forbidden`).
* **Essais :** mise à jour du paquet `pc` (étagée, `restart = 1`) ; installation et retrait d'une app ; dépôt local
  puis officiel.

**E52. osinstall.** Constats B8, B10 (mot de passe).

* **Changement :**
  * installation par `pkg --root` (`main.cpp:465`) à la place de `key_install` (`osinstall.c:719`), ce qui suppose la
    clé compilée de E51 : la clé de la racine cible n'existe pas encore ;
  * l'installateur impose un mot de passe à un compte admin.
* **Essais :** `installkey.py run` sous QEMU.

**E53. Démarrage, petites corrections.** Constats B2 (`local.tar`), B9, B13, B15, B16.

* **Changement :**
  * `local.tar` : seulement `*.txt` (`initrd.c`) ; ses seuls écrivains sont osinstall et `installkey.py`, qui n'y
    mettent que `init.txt` et `nvme.txt` ;
  * moniteur : `run` limité **dans les replis seulement** (initrd sans `init`, `/` absent). L'option explicite
    `monitor` reste entière : `tools/pc/build.sh:25-33` s'en sert pour ktest ;
  * recherche de Windows : disque de démarrage **ou** disque non amovible ;
  * MOR ;
  * trampoline effacé, à recréer quand S3 arrivera.
* **Non-régression :** sans impact, sauf pour le menu démarré depuis une clé, qui garde le Windows interne grâce au
  « non amovible ».

**E54. Écriture limitée sur l'ESP.** Constat B3.

* **Changement :** filtre de chemins pour `/boot` dans vfs ou fatfs (`\korin`, `\EFI\Korin`, et `\EFI\BOOT` sur une
  ESP neuve), et non dans `policy.c`, qui travaille par blocs.
* Corriger le commentaire de `nvme/policy.c:18-21`.
* **Essais :** mise à jour du noyau ; installation à côté de Windows (QEMU avec une ESP de Windows factice).

**E55. Mise à jour A/B.** Constat B5.

* **Changement :**
  * **paires** noyau + initrd, A et B : une entrée « version précédente » avec un initrd neuf ne démarrerait pas, à
    cause des numéros d'appels qui diffèrent ;
  * `.new`, puis synchronisation, puis renommage dans le volume ;
  * SHA-256 refait au `commit` ;
  * compteur d'essais remis à zéro par init2 ;
  * entrée de menu « précédent ».
* **Non-régression :**
  * Place sur l'ESP : environ 3,1 Mo par version, à vérifier avant d'écrire.
  * Les raccourcis chiffrés du menu se décalent (`menu.inc:27-31`).
  * Le chemin du Pi (`kernel8.img`) ne change pas.
* **Essais :** disque `mkkorindisk.py`, coupure simulée pendant le `commit`, puis le ZBook.

**E56. Journal d'audit.** Constat B12. Un service uid 0 écrit `/var/log/auth.log` en ajout seulement : élévations,
connexions, refus de vfs, commits de paquets, `boot.cfg`. PC seulement.

**E57. Clé de signature.** Constat K3. D'abord une sous-clé pour le cloud (`pub.py:130-136`, `publish.sh:49-61`),
ensuite seulement une clé principale chiffrée, puis une rotation, rendue possible par la liste de clés de E51.

### Phase 8 : Jet et code étranger

**E58. Jet, première étape.** Constats J4, J8, J10, J11.

* **Changement :**
  * `pipe2(…, O_CLOEXEC)` dans `engine_spawn_self` (`engine_webkit.cpp:799`). Sans impact : libposix respecte
    `FD_CLOEXEC`, et `adddup2` l'efface sur la copie.
  * La sonde (`web-probe.js`), la console et `web-trace-net` ne sont lues que si le fichier appartient à l'uid 0, ou
    par un argument `--probe` donné par l'UI. Les scripts sont adaptés dans le même commit : `test_webapp.sh`,
    `test_eme_jet.sh`, `docs/WEBKIT.md`, `JET-*.md`. `user.css` est gardé.
  * `JET_CDMHOST` et `JET_WIDEVINE_CDM` sont retirés (aucun utilisateur).
  * Permission EME demandée une fois par origine ; les apps web l'accordent d'office.
  * Dialogue pour un téléchargement que l'utilisateur n'a pas lancé ; les apps web refusent.
* **Essais :** Gmail, YouTube, Netflix, Spotify, une app web, les scripts de mesure.

**E59. Rôle WebContent dans le coffre.** Constat J12. Le rôle web déclare « webkit:web » (`PROC_SET_SCRIPT`), et
keyvault refuse ce rôle. Le keyvault d'E23 le fait déjà ; il ne reste que Jet, à reconstruire.

**E60. cdmhost et Widevine.** Constat J5.

* **Changement :**
  * cdmhost lancé par l'UI, avec les tubes passés comme poignées : c'est le prérequis de `pledge` (pas de `SPAWN`)
    et de `unveil` (pas de `.jet`) ;
  * le module est vérifié par sa signature CRX3 de Google au chargement, ou par une empreinte gardée dans un dossier
    réservé à l'uid 0.
* **Non-régression :** à essayer. Une empreinte compilée casserait à chaque mise à jour de Google.

**E61. linuxhost.** Constat L1 (premier volet). Un avertissement quand Claude Code tourne en mode `bypassPermissions`.
Un `unveil` serait gênant : Claude Code travaille sur `HOME` et `/media`.

**E62. Identité des scripts.** Constat A6, deuxième volet. **À faire après le portage de Blender.**

* **Changement :**
  * vfs dit le script au noyau lors du `V_EXEC` d'un fichier `ATTR_SCRIPT` ;
  * les interprètes ne déclarent pas de script si `BASH_ENV`, `ENV`, `PYTHON*` ou `DOTNET_STARTUP_HOOKS` sont
    présents, et keyvault les refuse alors.
* **Non-régression :**
  * keyvault ne peut pas lire l'environnement d'un autre process, d'où le contrôle dans l'interprète.
  * Aucun usage légitime de ces variables dans le système.
* **Correctifs à reconstruire :**
  * `tools/bash/patches/03-coffre-script.patch` ;
  * `tools/python/korin.patch`, **et** `tools/blender/patches/python-3.13.patch`, ensemble ;
  * `tools/dotnet/armpi.patch`.

### Phase 9 : parseurs, pilotes, Wi-Fi

**E63. Parseurs.** Constats F5, P9, D12, D18, usbstorage INQUIRY, noms d'ArchiveKit.

* **Changement :**
  * ext4 : `bpg` et `ipg` ≤ `8*bs` ; `descsz` borné (64 à 1024, puissance de 2) ;
  * FAT et exFAT : parcours d'une chaîne borné au nombre de clusters ;
  * noms venus des disques et de smbfs : refus de `/`, NUL, `.` et `..` exacts ; les autres caractères de contrôle
    sont **remplacés** par un caractère de substitution, pas masqués. `\` et `:` sont légitimes en ext4 et Samba ;
  * usbhid : bornes sur le modèle de `hidrd.c` ;
  * gdrivefs : `*.googleapis.com` seulement ;
  * usbstorage : INQUIRY filtrée avant la console.
* **Non-régression :** sans impact pour tout ce que fait mkfs (invariant du format). `bigalloc` n'est pas géré de toute
  façon.
* **Essais :** `fs_compare.sh` sur le Pi ; clés FAT, exFAT et ext4 ; partage Samba.

**E64. nvme.** Constat P10.

* **Changement :** Abort sur un délai dépassé, puis l'emplacement et les pages du client mis **en quarantaine**
  jusqu'à l'achèvement ; remise à zéro seulement sur CFS.
* **Non-régression :** à essayer **sur le ZBook seulement**. Une remise à zéro sur un simple ralentissement (APST)
  avec `/` monté serait dangereuse, et QEMU ne simule pas ces délais.

**E65. Wi-Fi.** Constats P3, P4, P12 (SSID), P13 ; faibles du Wi-Fi.

* **Changement :**
  * un M3 retransmis reçoit **toujours son M4**, sans réinstaller la clé ;
  * le compteur de rejeu n'est inscrit qu'après un MIC vérifié ;
  * une fois `keys_on`, l'EAPOL en clair est refusé, mais l'échange de clé de groupe et le renouvellement de la PTK,
    protégés, restent acceptés ;
  * nonces tirés de RDRAND ou de `getrandom` ;
  * SSID **échappé** dans `wifi.txt`, pas refusé ;
  * `mic_ok` en temps constant ;
  * en-tête avec `pad` corrigé ;
  * PMF « capable ».
* **On garde :** le groupe TKIP (box anciennes en mode mixte, `assoc.c:417`).
* **Non-régression :** à essayer. Jeter un M3 retransmis ferait déconnecter le point d'accès, et passer à PMF
  « requis » serait une casse.
* **Essais :** box, partage depuis un téléphone, Wi-Fi public.

### Phase 10 : réseau

**E66. Aléa dans netd.** Constats R2, R3, R5, R7 (ISN, ports).

* **Changement :**
  * **DNS :** identifiant et port source tirés de `getrandom`, avec contrôle de collision ; source vérifiée
    (`net_dns:53`) ; question comparée **sans la casse** (DNS 0x20) ; bit TC et classe IN.
  * **DHCP :** `xid` aléatoire ; ACK seulement du serveur de l'offre (option 54) ; options bornées.
  * **ISN :** SipHash.
  * **NTP :** horodatage d'origine aléatoire, source et port vérifiés, plancher à la date de construction. **Le recul
    reste permis** (Windows écrit la CMOS en heure locale), ou bien il faut deux serveurs d'accord au-delà de 26 h.
* **Non-régression :** sans impact. QEMU slirp répond depuis 10.0.2.3.
* **Essais :** QEMU PC et raspi4b ; le ZBook après un démarrage de Windows (heure) ; le Pi par `armpi_remote`.

**E67. Boucle locale, martiens, ARP.** Constats R9, R4.

* **Changement :**
  * commit 1 : drapeau « venu de lo » passé à `ip_input` ;
  * commit 2 : filtre des adresses martiennes sur les interfaces physiques, et pas d'écho ICMP vers la diffusion ;
  * ARP :
    * plus d'apprentissage depuis les paquets IP (`stack.c:249`) ;
    * on garde les demandes ARP qui nous visent et les réponses à nos demandes ;
    * l'ARP gratuit ne vaut que pour une entrée connue ;
    * l'entrée de la passerelle reste figée pendant le bail.
* **Non-régression :** adaptation (voir la §2).
* **Essais :** localhost (Jet, webd, OAuth) ; telnet, FTP et SMB depuis un nouveau client du réseau local.

**E68. Liaison à une adresse et `getpeername`.** Constats R1, O2.

* **Changement :**
  * opérations **nouvelles** `BIND_ADDR` et `PEERNAME`, en fin. Ne pas lire un champ libre de `op_bind` : les anciens
    clients du Pi y laissent n'importe quoi ;
  * libposix (`socket.c:182,813`) ;
  * `net_getpeername` à la fin de `netkit.abi` ;
  * le conflit reste jugé par port seul (webd `--dev` cherche son port en essayant de lier) ;
  * les retours OAuth passent sur 127.0.0.1.
* **Effet annoncé :** `webd --dev` devient local seulement, donc plus d'aperçu de WebStudio depuis un téléphone.

**E69. Ports, SYN, RST.** Constats R6, R7, R8.

* **Changement :**
  * ports < 1024 réservés à l'uid 0 (`E_NOSYS`, donc le Pi, permis) ;
  * SYN cookies, ou au minimum un quota par écouteur et une réserve pour les sockets internes, avec les tampons
    alloués à l'acceptation ;
  * RFC 5961 pour les RST.
* **Non-régression :** sans impact sur le PC. webd (80, 443), telnetd, ftpd et smbd sont en uid 0 ; WebStudio utilise
  8080 et plus.
* **Essais :** connexion vers un port fermé ; `curl` en boucle sous QEMU et sur le Pi.

**E70. Serveurs d'accès à distance.** Constats S1 à S11. **À faire après le portage de Blender.**

* **Commit 1, les outils d'abord :**
  * `tools/armpi_remote.py` lit `ARMPI_REMOTE_USER` (absent : pas d'invite de compte) ;
  * `tools/remote/ConnectDialog.cs` ;
  * la trentaine de scripts qui écrivent `password = essai` dans `/etc/remote.txt`, dont les `pctest.py` de Python et
    de Blender, `test_webapp.sh`, `testimage.py`, `compare.sh`.
* **Ensuite :**
  * authentification par comptes (authd) avec **repli « secret commun »** pour le Pi et les images d'essai ;
  * plus d'import automatique de `password =` (S5) ;
  * compteur d'échecs commun dans `remotepw.c` (S3) ;
  * ftpd sous `V_OPEN_AS` et enfermé dans `HOME:`, ou en uid 0 quand l'uid est inconnu (le Pi, envoi FTP de
    `kernel8.img`) (S2) ;
  * RDP n'est plus coupé avant authentification (S6) ;
  * `PORT` limité à l'adresse du client, `PASV` aléatoire avec pair vérifié (S7) ;
  * bornes de ftpd (S10) ;
  * vncd `SetEncodings` borné ; webd refuse les caractères < 0x20 (S11) ;
  * mot de passe VNC distinct (S4) ;
  * `/tmp/webd.pid` déplacé vers `/var/run` (S9) ;
  * smbd exige la signature (S8), et smbfs seulement avec un compte : l'invité des NAS ne peut pas signer.
* **Essais :** `armpi_remote` vers le Pi et vers QEMU PC ; l'Explorateur de Windows et le Finder vers smbd ; un NAS
  en invité.

**E71. Clients en clair** (constat O3) : documentation seulement, sur wget en HTTP et sur le client FTP.

### Phase 11 : `access = enforce`

**E72. Ce qu'`enforce` casserait aujourd'hui.** Le relevé, à corriger avant :

* **Réglages :**
  * `mouse.cpp` écrit `/etc/pointer.ini` **à chaque mouvement** (`:78`) : enregistrer sur Appliquer, élevé ;
  * `padconf.cpp`, `sound.cpp`, `dotnet.cpp`, `keyboard.cpp`, `display.cpp` : `ap_elevate` ;
  * `remote.cpp` lit `remote.txt` hors élévation.
* **Applet webd :** `services.txt`, `sites.ini`, les certificats ; webd lancé par init2 `SVC_START`, pas par `spawn`.
* **Applet smbd :** `smbd.ini` lu élevé ; smbd lancé par init2.
* **Horloge :** `/etc/clock.cfg`, élevé ou par un service (menubar et morpho le lisent).
* **Fichiers :** `/etc/smbfs.ini` (élevé, ou le coffre) et `/etc/mounts.txt`.
* **Données d'apps à migrer vers HOME** : Notes (`/Notes`, `/apps/notes.app`), Calendrier (`/apps/calendar.app`,
  lu par agenda), Archiveur (`recent.txt`), Koton (`/res/soundfonts`). L'ancienne place reste lisible.
* **webstudio :** `webd site add`, élevé.
* **Logiciels :** `J_CHECK` passe par pkgd, ou élevé (`/var/pkg/index.txt`).
* **printd et PrinterKit :** voir E48.
* **L'installateur :** voir E38.
* **Les noms fixes de `/tmp` lus par l'uid 0 :** voir E29, E38, E51 et E70.
* **`ui_elevate`** rend 0 sans authd (`uk_core.c:165-166`) : en `enforce`, il doit rendre une erreur claire.
* **À relire :** les apps portées d'Onyx qui écrivent dans leur dossier d'app (koton `drums.json`, vector `ext/`,
  cardfile).
* **Documentation :** `docs/etude/COFFRE.md:52` est périmé, puisque `access.c:23` protège déjà le coffre.

**E73. Une semaine en mode log sur le ZBook** après E72 : plus aucune ligne « acces refuse » en usage normal.

**E74. `enforce` par défaut.** `skel/pc/etc/system.ini`, `homes.c:19`, et osinstall écrit `access=enforce`. Les
chaînes C1, C2, C4 et la persistance par `/etc` sont alors fermées.

### Phase 12 : durcissement (SECURITE.md)

**E75.** Dans cet ordre, chacun avec son analyse de non-régression avant d'écrire le code.

* **SMEP, SMAP, UMIP.** Prérequis : E08 et E12. `stac` et `clac` autour de `copy_*`, de `futex_wait` et de
  l'écriture des arguments par la carte physique.
* **`unveil` et `pledge` de WebContent** (nouveaux appels en fin de table, `E_NOSYS` sur le Pi). La liste du §11 de
  l'audit, **complétée** par :
  * les dossiers des apps web `HOME/<app>/webkit` ;
  * `/etc/web-*` en lecture (lu à chaque page) ;
  * le cache des shaders de Mesa (`MESA_SHADER_CACHE_DIR` dans un dossier permis) ;
  * `mediakeys`.

  Prérequis : E24 à E27 et E60. Essais : pièce jointe Gmail (`<input type=file>`, glisser-déposer), Netflix,
  YouTube, apps web.
* **ASLR**, après E44.
* **JIT W^X** (J2) : risque fort pour la vitesse. Dans JSC, le tas W^X séparé n'est prévu que pour ARM64, et
  `mprotect` à chaque écriture coûte un shootdown TLB. Mesurer JetStream, Speedometer et YouTube avant et après
  (`docs/agents/JET-MESURES.md`). Sinon, une option « JIT désactivé ».
* **Cookies chiffrés** par une clé du coffre donnée au seul process réseau (J3). Les droits 0700 n'ont pas d'effet sur
  FAT.
* **VT-d** (N15, B11), avec la confirmation d'un périphérique PCIe branché à chaud.
* **Secure Boot :**
  * empreintes signées vérifiées par le chargeur (B2), à faire **avec** Secure Boot seulement, sinon l'attaquant
    remplace aussi `korin.efi` ;
  * PCR du TPM (B7) : migration (desceller, re-sceller), un repli par mot de passe, et un nouveau scellement à chaque
    `commit` du chargeur ;
  * B1 : attributs GPT en option d'osinstall (« isoler de Windows »).
* **ASLR des kits et programmes PIE**, à étudier avec la chaîne GCC de Korin.

---

## 4. Décisions à prendre par l'utilisateur

Chaque point change un usage qu'on ne peut pas garder tel quel :

1. **Accès à distance :** comptes, avec repli « secret commun » pour le Pi et les essais (E70), ou garder le secret
   commun partout.
2. **`webd --dev` local seulement** (E68) : plus d'aperçu de WebStudio depuis un téléphone. Sinon, une option.
3. **Façades de `/sys/svc` et `sudo kill` d'un service** (E33, E34) : sudo devient nécessaire.
4. **B1** (partitions cachées de Windows) : option d'osinstall seulement, ou aussi par défaut à l'installation.
5. **JIT W^X** (J2) : accepter une baisse de vitesse mesurée, ou une option « sans JIT » pour les sites sensibles.
6. **Partage de pages entre comptes** (E07, E46) : la mémoire double seulement avec plusieurs comptes actifs. À
   accepter.
7. **Une semaine de mode log sur le ZBook** (E73) avant `enforce`.

---

## 5. Correspondance des constats et des étapes

Chaque constat de l'audit est rattaché à au moins une étape. « — » signale un constat sans correction : point
correct, ou information.

| Constat | Étape(s) | Constat | Étape(s) | Constat | Étape(s) |
|---|---|---|---|---|---|
| I1 | E05, E17 | I2 | E17, E19-E23 | N1 | E03 |
| N2 | E06 | N3 | E01 | N4 | E07 |
| N5 | E02 | N6 | E01 | N7 | E12 |
| N8 | E03 | N9 | E09 | N10 | E46 |
| N11 | E46 | N12 | E35 | N13 | E45 |
| N14 | E44 | N15 | E34, E42, E75 | N16 | E16 |
| N17 | E05 | N18 | E13 | N19 | E13 |
| N20 | E03, E15 | N21 | E02 | N22 | E13 |
| N23 | E02 | N24 | E06, E11 | N25 | E43 |
| N26 | E43 | N27 | E14, E34 | N28 | E14 |
| N29 | E01 | fuites du noyau | E22, E43 | W+X | E75 |
| NMI, #MC, piles | E10 | F1 | E24 | F2 | E24, E26 |
| F3 | E24 | F4 | E21 | F5 | E04, E27, E63 |
| F6 | E19, E24 | A1 | E29 | A2 | E38 |
| A3 | E38 | A4 | E30 | A5 | E39 |
| A6 | E23, E59, E62 | A7 | E04, E38, E40 | M1 | E32 |
| M2 | E18, E33 | P1 | E20 | P2 | E19, E41 |
| P3 | E65 | P4 | E65 | P5 | E19 |
| P6 | E19, E49 | P7 | E19 | P8 | E19 |
| P9 | E63 | P10 | E64 | P11 | E19, E32 |
| P12 | E32, E65 | P13 | E65 | faibles des pilotes | E19, E63, E65 |
| R1 | E68 | R2 | E66 | R3 | E66 |
| R4 | E67 | R5 | E66 | R6 | E69 |
| R7 | E66, E69 | R8 | E69 | R9 | E67 |
| S1 à S11 | E70 | O1 | E48 | O2 | E68 |
| O3 | E71 | G1 | E19 | G2 | E04 |
| G3 | E36, E39 | G4 | E36 | G5 | E35, E36 |
| K1 | E51 | K2 | E50, E51 | K3 | E51, E57 |
| K4 | E51, E63 | J1 | E75 | J2 | E75 |
| J3 | E75 | J4 | E58 | J5 | E60 |
| J6 | E47 | J7 | E19 | J8 | E58 |
| J9 | — (correct) | J10 | E58 | J11 | E58 |
| J12 | E23, E59 | X1 | E46 | X2 | — (information) |
| L1 | E61, E75 | L3 | E62, E74 | L4 | E51 |
| B1 | E75 | B2 | E53, E75 | B3 | E25, E54 |
| B4 | E31 | B5 | E55 | B6 | E08 |
| B7 | E75 | B8 | E52 | B9 | E53 |
| B10 | E52, E74 | B11 | E41, E75 | B12 | E56 |
| B13 | E53 | B14 | E20 | B15 | E53 |
| B16 | E53 | D1 | E37 | D2 | E34 |
| D3 | E04 | D4 | E37 | D5 | E37 |
| D6 | E48 | D7 | E23 | D8 | E23 |
| D9 | E18 | D10 | E40 | D11 | E27 |
| D12 | E63 | D13 | E40 | D14 | E19, E32 |
| D15 | E35 | D16 | E35 | D17 | E23 |
| D18 | E63 | D19 | — (correct) | D20 | E18 |

**Chaînes d'attaque :**

| Chaîne | Fermée par |
|---|---|
| C1 | E33 et E74 |
| C2 | E38 et E74 |
| C3 | E20 |
| C4 | E24 et E26 |
| C5 | E06 |
| C6 | E07, E19, E39 |
| C7 | E01, E03, E06, E13, E19 |
| C8 | E58 puis E75 |

---

## 6. Ordre d'ensemble et taille

| Phase | Étapes | Taille | Protège aussi des bugs |
|---|---|---|---|
| 0 | E00 | un jour, plus une semaine de mode log | — |
| 1 | E01-E04 | quelques heures | oui |
| 2 | E05-E16 | quatre à six jours ; E12-E13 sont les plus délicates | oui |
| 3 | E17-E23 | deux à trois jours | oui pour la plupart |
| 4 | E24-E28 | deux jours | oui |
| 5 | E29-E40 | cinq à sept jours ; E29 et E36 sont les plus lourdes | en partie |
| 6 | E41-E49 | cinq à sept jours ; E47 demande une reconstruction de WebKit | en partie |
| 7 | E50-E57 | trois à quatre jours | E55 oui |
| 8 | E58-E62 | deux à quatre jours, plus les reconstructions de Jet, bash, python et dotnet | non |
| 9 | E63-E65 | deux jours, plus les essais Wi-Fi et nvme sur le ZBook | oui |
| 10 | E66-E71 | trois à quatre jours | en partie |
| 11 | E72-E74 | deux jours, plus une semaine de mode log | oui |
| 12 | E75 | chantiers (semaines) | en partie |

Les **phases 1 et 2** ferment à elles seules toutes les élévations vers le noyau par la mémoire. Les phases 3 à 5
ferment les élévations par les services. `enforce` (phase 11) n'a de sens qu'après elles.
