# Audit de sécurité de Korin OS sur PC (x86-64), n° 2

Date : 26/11/2026. Branche `main`. Audit indépendant fait sur le code source, les fichiers de configuration
(`skel/pc/etc`, `skel/sd0/etc`) et les scripts de construction seulement, sans lire les études ni les audits
précédents, sans rien construire ni lancer. Les chemins de Raspberry Pi (assembleur aarch64) sont hors du champ.

## 1. Résumé

Le noyau est écrit avec soin : copies utilisateur/noyau bornées et rattrapées, retour par `iretq`, trame de signal
assainie, pids à génération, avis du noyau infalsifiables, identité de l'appelant sans course (`SVC_REQ_INFO`),
tampons partagés accordés par pid. Les services exposés au réseau (netd, supplicant WPA2) ont reçu un vrai travail
de durcissement, et la chaîne des paquets (index signé ECDSA, SHA-256 par paquet, numéro de série contre le retour en
arrière, noms d'archive filtrés) est solide.

La faiblesse est dans les **frontières entre comptes et entre « compte » et « système »** :

* les règles d'accès de vfs sont **en mode « journal » par défaut** : en dehors d'une courte liste obligatoire, un
  programme d'un compte peut lire et écrire presque tout ; cette liste oublie `/etc/mounts.txt`, ce qui donne
  **l'uid 0 à n'importe quel programme d'un compte** après un redémarrage qu'il peut lui-même demander ;
* un compte administrateur **sans mot de passe** peut se donner un mot de passe sans rien prouver puis passer par
  `sudo` : la règle « un compte sans mot de passe ne s'élève pas » se contourne ;
* les **droits du matériel** (ports d'E/S, DMA, registres, disque brut) sont donnés à tous les services de
  `/sys/svc` (dont ceux qui parlent au réseau) et un compte peut lancer lui-même des programmes à droits (oauthd,
  pkgd, Elegant…) sous son uid ; il n'y a pas d'IOMMU : toute faille dans l'un d'eux vaut le noyau ;
* **aucune défense en profondeur** contre l'exploitation mémoire : pas d'ASLR (exécutables `ET_EXEC`, adresses
  fixes partout), ni SMEP, ni SMAP, ni W^X, alors que le navigateur fait tourner un JIT JavaScript et WebAssembly
  dans des process web non cloisonnés.

Niveau général : **moyen**. Bon contre un attaquant du réseau local dans la configuration par défaut (aucun service
en écoute), faible contre un programme malveillant ou compromis qui tourne déjà sous un compte (application, page
web exploitant WebKit, agent d'IA détourné par injection de consignes), qui peut devenir uid 0, puis noyau.

Les problèmes les plus graves :

| N° | Gravité | Problème |
|---|---|---|
| N1 | critique | Règles de vfs en mode « log » par défaut ; `/etc/mounts.txt` (et d'autres fichiers lus par l'uid 0) modifiable par tout compte : uid 0 au redémarrage |
| N2 | haute | Compte administrateur sans mot de passe : `AUTH_PASSWD` sans preuve, puis `sudo` en lisant le mot de passe sur stdin |
| N3 | haute | Droits du matériel pour tous les services, lançables par un compte (oauthd, pkgd, Elegant…) ; pas d'IOMMU |
| N4 | haute | Ni ASLR, ni SMEP/SMAP/UMIP, ni W^X ; kits et pile à adresses fixes |
| N5 | haute | Jet : process web non cloisonnés, sous l'uid du compte, avec JIT (FTL, Wasm) |
| N6 | moyenne | Élévation d'une app : toute l'app passe en uid 0, et la carte n'affiche que le texte fourni par l'app (y compris « mise à jour du système ») |
| N7 | moyenne | Coffre : l'identité « script d'un interprète » se contourne par `BASH_ENV`, `PYTHONPATH`, `python -i`… |
| N9 | moyenne | FTP (secret commun) : tout l'arbre en uid 0, en clair ; RDP : empreinte du mot de passe équivalente au mot de passe |

Comptes par gravité : **1 critique, 4 hautes, 6 moyennes, 8 basses** (19 constats).

## 2. Constats

### N1 — Règles d'accès en mode « journal » par défaut ; un compte réécrit `/etc/mounts.txt` et devient uid 0

* **Gravité** : critique
* **Composant** : vfs (règles d'accès), init2, authd
* **Fichiers** :
  * `src/svc/vfs/homes.c:19` et `:118` (`mode_acces = ACCESS_LOG` : défaut), `:127-128` (seul `access = enforce`
    l'active ; aucun script ni l'installateur ne l'écrit : aucune occurrence de `access=enforce` hors de ce lecteur) ;
  * `skel/pc/etc/system.ini` (`#access=log`, commenté) ;
  * `src/svc/vfs/access.c:305-361` (`acces_verifier` : en mode log, un refus de `permis()` est seulement noté, sauf
    pour les listes `toujours` et `marques`) ; `:32-58` (liste `marques`, obligatoire en écriture) : elle n'a pas
    `/etc/mounts.txt`, `/etc/bash.bashrc`, `/etc/claude-code`, `/etc/oauth`, `/etc/wifi.txt`, `/devtools`, `/res`,
    `/var/log`, `/boot` ;
  * `src/svc/vfs/vfs.c:708`, `:843` (mounts.txt lu au démarrage par vfs, uid 0), `:377-470` (`monter` : n'importe
    quel point de montage, rootpath libre ; les restrictions de `monter_permis`, `:500-524`, ne valent que pour
    `VFS_MOUNT` d'un compte) ;
  * `src/svc/init2/init2.c:785-786` (un process du compte de la session peut demander `INIT_OP_REBOOT`) ;
    `:83-93` (`lancer_ligne` : une ligne de `/etc/services.txt` lancée en uid 0, quel que soit son chemin) ;
  * `src/svc/authd/authd.c:200-233`, `:312-345` (authd lit `/etc/users.ini`, `/etc/elevate.ini`,
    `/etc/shadow.ini` par vfs, donc par l'arbre monté).
* **Description** : en mode `log` (le défaut), `permis()` refuse mais `acces_verifier` laisse faire, sauf pour le
  coffre, shadow.ini, le repère de la carte, `/tmp/<uid>` et la liste `marques`. Tout le reste de `/etc`, `/var`,
  `/res`, `/devtools`, les dossiers des autres comptes, est lisible et inscriptible par tout uid ≥ 1000. Or
  `/etc/mounts.txt` est lu par vfs (uid 0, droits) à chaque démarrage et accepte un montage n'importe où avec un
  dossier racine (« `POINT = source, service, rootpath` »).
* **Scénario d'attaque** : un programme du compte (une app piégée, un agent d'IA en mode « tout accepter » détourné
  par un fichier, une page qui exploite WebKit, voir N5) écrit dans `/etc/mounts.txt` une ligne
  `/etc = blk.boot, ext4fs, home/user/.x` (la même source que `/` : le service est partagé), après avoir préparé
  dans `~/.x` un `users.ini`, un `elevate.ini` et un `shadow.ini` dont il connaît le mot de passe (ou un
  `services.txt` qui lance son programme). Il demande le redémarrage (`INIT_OP_REBOOT`, permis au compte de la
  session). Au démarrage, `/etc` est son dossier : authd vérifie le mot de passe choisi par l'attaquant, `echo pw |
  /bin/sudo …` (voir N2 pour la lecture sur stdin) donne l'uid 0 ; ou init2 lance son programme en uid 0. Persistant.
  Autres effets directs du mode log, sans redémarrage :
  * lecture des fichiers de la liste `secrets` (`/etc/wifi.txt`, `/etc/remote.txt`, `/etc/smbd.ini`,
    `/etc/smbfs.ini`, `/etc/web/certs`, `/var/log/auth.log`) quand ils existent, et des dossiers des autres comptes ;
  * écriture de `/etc/bash.bashrc` (lu par tout bash interactif, donc par `sudo -s` : code en uid 0) ;
  * écriture de `/devtools/python`, `/devtools/dotnet`, `/devtools/gcc` (un `os.py` piégé s'exécute chez les autres
    comptes et chez l'uid 0 ; le coffre tient `/devtools/` pour un dossier du système, `keyvault.c:887-894`) ;
  * écriture de `/res/agents/skills` (consignes des agents de tous les comptes ; `src/lib/acp/home.c:10-11` dit
    « seul l'uid 0 écrit dans /res »), de `/res/widevine/libwidevinecdm.so` (le test `st_uid == 0` de
    `engine_webkit.cpp:619` porte sur un propriétaire déduit du chemin, toujours 0 sous `/res`), de
    `/etc/claude-code/{CLAUDE.md,mcp.json}` (serveur MCP lancé par Claude Code chez les autres comptes) ;
  * écriture ou troncature de `/var/log/auth.log` (le journal d'audit) ;
  * écriture de `/boot/korin/…` (noyau, initrd) pendant qu'un `pkg` élevé tient `/boot` monté : `esp_permis`
    (`access.c:293-302`) l'autorise à tout uid, et `/boot` n'est dans aucune liste.
* **Correction proposée** : passer le défaut à `ACCESS_ENFORCE` (`homes.c:19`, `:118`) et l'écrire dans
  `system.ini` à l'installation ; en attendant, ajouter à `marques` au moins `/etc/mounts.txt`, `/etc/bash.bashrc`,
  `/etc/claude-code`, `/etc/oauth`, `/etc/wifi.txt`, `/etc/fileassoc.ini`, `/res`, `/devtools`, `/var/log`,
  `/boot`, et plus simplement **tout `/etc` sauf une liste blanche** ; faire refuser par `monter` (lignes de
  mounts.txt) un point sous `/etc`, `/sys`, `/bin`, `/sbin`, `/lib`, `/apps`, `/devtools`, `/res`, `/var`, `/boot`.
* **Confiance** : élevée (chaîne lue de bout en bout ; non essayée).

### N2 — Un compte administrateur sans mot de passe se donne un mot de passe sans preuve, puis s'élève

* **Gravité** : haute (dans les configurations concernées)
* **Composant** : authd, sudo
* **Fichiers** : `src/svc/authd/authd.c:312-318` (`verifier` : pas de ligne dans shadow.ini, ou valeur vide →
  vrai pour tout mot de passe), `:889-905` (`AUTH_PASSWD` : l'ancien vérifié par `verifier_compte`, donc accepté
  vide), `:853-887` (`AUTH_SUDO` refuse un compte sans mot de passe, mais le compte vient de s'en donner un) ;
  `src/bin/sudo/sudo.c:173-185` (le mot de passe lu par `read_line` sur l'entrée standard, un tube suffit) ;
  `src/svc/keyvault/keyvault.c:1232-1241` (le coffre d'un compte sans mot de passe s'ouvre avec « ») ;
  `skel/pc/etc/users.ini` + `skel/pc/etc/elevate.ini` (le compte `user`, groupe `admin`, dans `elevate.ini`, sans
  shadow.ini : configuration des images de développement et de QEMU).
* **Description** : la décision « un compte qui gère le système a un mot de passe » est appliquée au moment de
  l'élévation, mais `AUTH_PASSWD` permet à n'importe quel programme du compte de fixer ce mot de passe quand il n'y
  en a pas encore. `vrai_sudo` vérifie que l'appelant est le vrai `/bin/sudo`, pas qu'un humain tape au terminal.
* **Scénario d'attaque** : sur une machine dont l'administrateur n'a pas (ou plus : Réglages permet d'en retirer un,
  « nouveau vide : plus de mot de passe », `authd.c:20-21`) de mot de passe, un programme du compte envoie
  `AUTH_PASSWD("", "x")` puis lance `printf 'x\nN\n' | /bin/sudo /bin/sh -c …` : uid 0, sans interaction.
  `osinstall` impose un mot de passe à l'administrateur (`osinstall.c:1358-1362`), ce qui protège une installation
  neuve, mais pas les images faites depuis `skel/pc/etc` ni un compte qui a retiré son mot de passe.
* **Correction proposée** : pour un compte de `elevate.ini` sans mot de passe, ne fixer le premier mot de passe que
  par la carte d'authd (couche sûre, saisie humaine) ou par l'uid 0 ; interdire à `AUTH_PASSWD` de rendre vide le
  mot de passe d'un compte qui peut s'élever.
* **Confiance** : élevée.

### N3 — Droits du matériel trop larges, lançables par un compte ; pas d'IOMMU

* **Gravité** : haute (conception ; exploitation subordonnée à une faille mémoire dans un de ces programmes)
* **Composant** : noyau (`rights`), vfs (`ATTR_SYS`), init2, services
* **Fichiers** : `src/svc/vfs/path.c:467-479` (tout `/sys/svc/*` plus `elegant`, `pkgd`, `notifyd`, `clipd`,
  `printd` sont lancés avec les droits) ; `src/svc/init2/init2.c:503-511` (un compte peut faire lancer par init2 tout
  `/sys/…` hors `/sys/svc/`, et `/sys/svc/oauthd`, sous son uid **avec les droits**, avec ses arguments) ;
  `src/kits/appkit/appkit.c:1322` (`P[2] = ATTR_SYS`) ; `src/svc/vfs/vfs.c:408-409` (gdrivefs, service à droits,
  lancé sous l'uid du compte qui monte) ; noyau : `src/kernel/core/hw.c:30-42` (`DEV_MAP`), `:207-218`
  (`IO_PORT` : tout port, donc la configuration PCI 0xCF8/0xCFC), `src/kernel/core/mm.c:1048` (`DMA_ALLOC`) ;
  `src/svc/common/blkserv.c:735` et `caller.h` (`caller_admin` = droits ou uid 0 : accès brut au disque) ;
  aucune occurrence de DMAR/VT-d dans `src/kernel` ni `src/svc`.
* **Description** : les « droits » valent le noyau (registres de tout périphérique, ports d'E/S, mémoire DMA,
  lecture brute du disque donc de shadow.ini et des coffres, noms de service du système). Ils sont donnés à tous les
  services, y compris ceux qui analysent des données hostiles : netd (TCP/IP), iwl (trames 802.11), usbd
  (descripteurs USB), ftpd/telnetd/vncd/rdpd, smbfs, gdrivefs et oauthd (HTTP et JSON de serveurs distants). En
  plus, un compte lance lui-même oauthd, pkgd, Elegant, notifyd, clipd, printd sous son uid, avec ses arguments et
  la configuration de son dossier (`HOME:/oauth/accounts.ini` pour oauthd). Sans IOMMU, un pilote à droits programme
  n'importe quel périphérique pour écrire n'importe où, et un périphérique Thunderbolt ou PCIe malveillant fait de
  même sans aucun logiciel.
* **Scénario d'attaque** : une faille mémoire dans oauthd (réponse HTTPS ou fichier `accounts.ini` du compte),
  déclenchée par un programme du compte, donne à ce programme les droits : `IO_PORT` sur 0xCF8 pour reprogrammer un
  contrôleur, `DEV_MAP` de ses BAR, DMA dans la mémoire du noyau : élévation au noyau. Une faille dans netd ou iwl,
  déclenchée depuis le réseau, donne directement le noyau.
* **Correction proposée** : découper `rights` en capacités (IRQ, MMIO d'une liste de BAR donnée par pcid, ports
  d'une liste, DMA, disque brut, SVC_REGISTER) et ne donner à chaque service que les siennes (oauthd, gdrivefs,
  ftpd, smbfs, webd n'ont besoin d'aucun accès au matériel) ; refuser à un compte le lancement d'un programme à
  droits (oauthd, gdrivefs sous l'uid du compte mais **sans** droits) ; activer VT-d (table DMAR) pour cantonner le
  DMA de chaque pilote à ses tampons ; garder la liste des ports permis par `IO_PORT` (i8042, EC, registres ACPI).
* **Confiance** : élevée sur le mécanisme ; l'exploitation suppose une faille dans un de ces programmes.

### N4 — Pas d'ASLR, ni SMEP/SMAP/UMIP, ni W^X

* **Gravité** : haute (défense en profondeur absente)
* **Composant** : noyau x86-64, chargeur ELF, kits
* **Fichiers** : `src/kernel/core/elf.c:35-36` et `src/kernel/core/image.c:166-169` (seuls les `ET_EXEC` sont
  chargés : aucune relocation) ; `src/kernel/include/korin/mm.h:14-35` (pile, tas, arène, tampons, DMA à adresses
  fixes) ; `src/kernel/core/kits.c:5`, `:22-23` (« chargé une fois pour tout le système à une place fixe ») ;
  `src/kernel/arch/x86_64/cpu.c:175-181` (CR4 : seulement OSFXSR, OSXMMEXCPT, OSXSAVE ; ni SMEP (bit 20), ni SMAP
  (21), ni UMIP (11)) ; `src/kernel/core/mm.c:582`, `:665` (`VM_MAP` et `VM_PROTECT` acceptent `prot = 7`,
  lecture-écriture-exécution) ; `src/kernel/core/elf.c:74` (page commune à deux segments : W et X réunis) ;
  `src/kernel/arch/x86_64/paging.c:169-175` (pages du noyau globales, pas de séparation des tables, pas de KPTI).
* **Description** : toutes les adresses d'un process sont connues d'avance (programme, kits partagés, pile à
  `0x7FFFFFFF0000`), le noyau peut exécuter ou lire une page utilisateur sans faute, et une page peut être à la fois
  inscriptible et exécutable.
* **Scénario d'attaque** : une simple écriture hors bornes dans un service ou une app (voir N3, N5) suffit à une
  chaîne ROP sur les kits, dont les gadgets sont aux mêmes adresses partout ; dans le noyau, un pointeur de fonction
  écrasé peut viser du code utilisateur (pas de SMEP), un pointeur de donnée une structure forgée en espace
  utilisateur (pas de SMAP).
* **Correction proposée** : activer SMEP et UMIP tout de suite (aucune exécution noyau en espace utilisateur n'est
  voulue) ; SMAP avec `stac`/`clac` autour d'`arch_copy_user` ; un décalage aléatoire de la pile et de l'arène par
  process ; à terme des exécutables PIE et des kits à adresse tirée au démarrage ; W^X par défaut, une permission
  explicite pour le JIT (double mappage RW/RX comme sur macOS).
* **Confiance** : élevée.

### N5 — Jet : process web sous l'uid du compte, sans cloisonnement, avec JIT

* **Gravité** : haute
* **Composant** : navigateur Jet, WebKit
* **Fichiers** : `src/extra/onyxports/jet/engine_webkit.cpp:1100-1104` (process web et réseau : le même programme,
  lancé comme un fils ordinaire, donc avec l'uid et l'accès aux fichiers du compte) ; `tools/webkit/build-jsc.sh:33-36`
  (modes `ftl` et `ftlwasm` « Korin PC » : JIT DFG, FTL et Wasm BBQ/OMG) ; N4 (pas d'ASLR, RWX permis).
* **Description** : un process par onglet sépare les sites entre eux mais pas de l'utilisateur : le moteur de rendu
  d'une page lit `~/.ssh`, les cookies des autres sites (`HOME/jet/webkit`), et, par N1, peut devenir uid 0. Le JIT,
  sans ASLR ni W^X, rend une faille de JavaScriptCore bien plus simple à exploiter. La révision de WebKit (copie
  d'Onyx, `src/extra/webkit`, absente du dépôt) n'a pas pu être comparée aux correctifs publiés.
* **Scénario d'attaque** : une page malveillante exploite une faille connue de JSC dans le process web, écrit
  `/etc/mounts.txt` (N1) et demande le redémarrage : compromission persistante depuis une simple visite.
* **Correction proposée** : faire tourner les process web sous un uid à part (un uid « jet-web » par compte) avec un
  vfs qui ne lui ouvre que ce que le process réseau lui passe ; à défaut, désactiver le FTL et le JIT Wasm sur PC ;
  suivre les versions de sécurité de WebKit.
* **Confiance** : élevée sur le modèle de process ; moyenne sur l'âge de WebKit (non vérifiable ici).

### N6 — L'élévation d'une app passe toute l'app en uid 0 ; la carte n'affiche que le texte de l'app

* **Gravité** : moyenne
* **Composant** : authd, authcard, apps (Jet)
* **Fichiers** : `src/svc/authd/authd.c:568-643` (`elever` : la carte reçoit « jeton|raison|compte|1 », la raison
  venant de l'app ; `PROC_SETUID(pid, 0)` sur l'app elle-même, `:663`) ; `:907` (le mode 2 ou 3, « mise à jour du
  système », est choisi par l'app) ; `src/sys/authcard/authcard.c:94`, `:114` (affiche la raison telle quelle, ni
  le programme ni son chemin) ; `src/extra/onyxports/jet/widevine.h:52-85` (Jet s'élève, fait `fork`/`execl`, puis
  `setuid` pour revenir : tout le navigateur, ses fils et ses autres threads sont en uid 0 pendant ce temps).
* **Description** : rien ne dit à l'utilisateur quel programme demande ; une app peut écrire « Logiciels veut
  installer une mise à jour du système » et obtenir la carte « mise à jour du système ». L'accord donne l'uid 0 au
  process demandeur entier, pas à une opération.
* **Scénario d'attaque** : une app quelconque demande `AUTH_ELEVATE` mode 2 avec un texte rassurant au moment où
  l'utilisateur attend une mise à jour ; l'utilisateur tape son mot de passe ; l'app est uid 0.
* **Correction proposée** : afficher sur la carte le chemin vérifié de l'exécutable (`SYS_PROC_EXE` + empreinte,
  comme `vrai_sudo`) et réserver les modes 2/3 à pkg/pkgman vérifiés ; élever un programme d'aide nommé (comme
  `cdmupdate`) plutôt que l'app : authd lance l'aide en uid 0 après accord.
* **Confiance** : élevée.

### N7 — Coffre à secrets : l'identité « script d'un interprète » se contourne

* **Gravité** : moyenne (isolation entre programmes d'un même compte)
* **Composant** : keyvault, bash, python, dotnet
* **Fichiers** : `src/svc/keyvault/keyvault.c:932-973` (`identify` : pour un interprète de
  `interpreters.ini`, l'identité est le script déclaré par `SYS_PROC_SET_SCRIPT`) ; `:1405-1414` (`KVF_PARENT` : le
  père de `/bin/secret`) ; `skel/pc/etc/keyvault/interpreters.ini` (`/bin/bash`, `/bin/sh`, python3, dotnet) ;
  `tools/bash/patches/03-coffre-script.patch`, `tools/python/korin.patch` (le script déclaré avant tout code).
* **Description** : un programme du compte lance `BASH_ENV=/tmp/a.sh bash ~/bin/script-victime.sh`, ou
  `PYTHONPATH=/tmp/x python3 ~/outil.py` (un module de `/tmp/x` passe avant la bibliothèque standard), ou
  `python3 -i ~/outil.py` (invite interactive après le script), ou `DOTNET_STARTUP_HOOKS=…` : son code tourne dans
  un interprète qui a l'identité du script victime ; `secret get <nom>` (ou l'API) rend alors les secrets de ce
  script. Le fichier du script n'est pas changé : son empreinte retenue ne déclenche pas la carte.
* **Scénario d'attaque** : un programme malveillant du compte lit la clé d'API qu'un script de l'utilisateur
  (`ANTHROPIC_API_KEY=$(secret get anthropic)`) range dans le coffre.
* **Correction proposée** : quand un interprète déclare un script, l'interprète doit ignorer les variables qui
  injectent du code (`BASH_ENV`, `ENV`, `PYTHON*`, `-i`, `DOTNET_STARTUP_HOOKS`) — mode « isolé » forcé, comme
  `python -I` — ou le noyau doit marquer `PROC_SET_SCRIPT` comme non fiable dès qu'une telle variable est présente ;
  documenter que le coffre ne protège pas contre un autre programme du même compte.
* **Confiance** : moyenne à élevée (comportement standard de bash et de CPython ; non essayé sur Korin).

### N8 — Journal d'audit falsifiable ; journal du noyau lisible par tous

* **Gravité** : moyenne
* **Composant** : noyau (`SYS_PRINT`, `SYS_KLOG`), auditd
* **Fichiers** : `src/kernel/core/syscall.c:28-42` (`SYS_PRINT` : tout process écrit dans le journal du noyau) ;
  `src/kernel/core/sys.c:241-288` (`KLOG`, `KLOG_FROM` : lisibles par tous) ; `src/svc/auditd/auditd.c:31-32`
  (lignes gardées par leur seul préfixe « authd : », « vfs : acces refuse »…).
* **Description** : n'importe quel process peut écrire `\nauthd : sudo par alice (pid 4242) : accorde\n` ; auditd
  la recopie dans `/var/log/auth.log` comme une vraie ligne d'authd. En mode log (N1), `auth.log` est même
  inscriptible. À l'inverse, tout compte lit le journal du noyau : chemins des fichiers des autres comptes (lignes de
  refus de vfs), programmes lancés, téléchargements de Jet.
* **Correction proposée** : préfixer chaque ligne du journal du noyau du pid et de l'uid de l'émetteur (ou un
  canal d'audit à part que seuls authd, vfs, init2 écrivent) et faire vérifier par auditd que l'émetteur est le bon
  service ; réserver `KLOG` à l'uid 0 ou filtrer les lignes des autres comptes.
* **Confiance** : élevée.

### N9 — Accès à distance : FTP en uid 0 sur tout l'arbre, protocoles en clair, empreinte équivalente au mot de passe

* **Gravité** : moyenne (ces services ne sont pas lancés par défaut ; haute dès qu'on les active)
* **Composant** : ftpd, telnetd, vncd, rdpd, authd
* **Fichiers** : `src/svc/ftpd/ftpd.c:44-46` (« vide : tout l'arbre (le secret commun) »), `:195-218`
  (`resolve` : en mode comptes, confinement par simple préfixe) ; ftpd est un service de `/sys/svc`, donc uid 0 et
  droits : vfs le voit en uid 0 et ne lui applique aucune règle ; `src/svc/rdpd/rdpd.c:12-26` (protocole sans
  chiffrement ; preuve HMAC avec pour clé la valeur PBKDF2 stockée) ; `src/svc/authd/authd.c:928-1018`
  (`AUTH_SALT`, `AUTH_PROOF` : la clé est l'empreinte de shadow.ini).
* **Description et scénarios** :
  * FTP avec le mot de passe commun : lecture de `/etc/keyvault/machine.key`, `/var/keyvault/*.db`,
    `/etc/shadow.ini`, écriture de `/sys/svc/*` : le mot de passe d'accès à distance vaut l'uid 0 et les droits.
    En mode comptes, un compte dont le dossier est `/home` (celui de `skel/pc/etc/users.ini`) voit les dossiers de
    tous les autres, sans les règles de vfs.
  * Telnet, FTP, VNC (défi DES) et le protocole de rdpd passent en clair : un voisin du Wi-Fi lit l'écran, les
    frappes, les fichiers, et peut détourner une session déjà ouverte (aucune intégrité).
  * rdpd : qui a l'empreinte PBKDF2 d'un compte (sauvegarde, disque volé) s'y connecte sans le mot de passe ; un
    échange capturé (défi + preuve, sel public par `AUTH_SALT`) permet une attaque par dictionnaire hors ligne.
* **Correction proposée** : faire servir les fichiers de ftpd (et smbd) sous l'uid du compte authentifié (un
  process par session après `SPAWN_AS`, comme telnetd lance son shell), jamais en uid 0 ; supprimer le mode « secret
  commun = tout l'arbre » ; FTPS/TLS pour ftpd, TLS pour rdpd et vncd ; pour rdpd, un protocole à preuve sans
  équivalence d'empreinte (SRP, ou OPAQUE).
* **Confiance** : élevée.

### N10 — Chaîne de démarrage sans vérification, disque en clair, connexion automatique

* **Gravité** : moyenne (accès physique, ou Windows compromis sur le même disque)
* **Composant** : chargeur UEFI, osinstall, login
* **Fichiers** : `src/kernel/arch/pc/boot/bootx64.asm` et `ab.inc` (le noyau et l'initrd sont lus et lancés sans
  signature ni empreinte ; pas de Secure Boot) ; `src/svc/vfs/access.c:290-302` (l'ESP est partagée avec Windows) ;
  `src/bin/osinstall/osinstall.c:1349`, `:1686-1687` (`autologin` vaut `yes` par défaut : la session de
  l'administrateur s'ouvre sans mot de passe) ; aucun chiffrement de `/` ni de `/home` (seul le coffre est chiffré).
* **Scénario d'attaque** : un portable volé démarre directement dans la session de l'utilisateur (ses fichiers, ses
  sessions web) ; un démarrage sur clé ou la lecture du disque donne tout sauf le coffre ; un logiciel malveillant de
  Windows (même ESP) remplace `\korin\a\kernel.elf` et persiste sous Korin. Le menu A/B permet aussi de démarrer une
  version de secours plus ancienne.
* **Correction proposée** : `autologin = no` par défaut pour un compte administrateur ; signer noyau et initrd et
  les vérifier dans le chargeur (clé intégrée, ou Secure Boot avec shim) ; chiffrement du volume, la clé scellée
  dans le TPM (le coffre sait déjà le faire, `tpm.c`) et déverrouillée par le mot de passe.
* **Confiance** : élevée.

### N11 — Pas d'IOMMU : attaques DMA par Thunderbolt/PCIe et par les micrologiciels

* **Gravité** : moyenne
* **Composant** : noyau, pcid, pilotes
* **Fichiers** : aucune gestion de DMAR/VT-d (`grep` sans résultat dans `src/kernel`, `src/svc`) ;
  `src/kernel/core/mm.c:1048-1077` (`DMA_ALLOC` rend des adresses physiques que le périphérique utilise sans
  limite) ; micrologiciel Intel Wi-Fi chargé dans la carte (`firmware/pc/iwlwifi`).
* **Description** : tout périphérique maître du bus (station d'accueil Thunderbolt piégée, carte Wi-Fi dont le
  micrologiciel serait compromis depuis l'air) lit et écrit toute la mémoire, noyau compris, mots de passe et clés
  du coffre déverrouillé compris. Voir aussi N3.
* **Correction proposée** : activer VT-d, une table de traduction par périphérique, n'y mettre que les tampons DMA
  du pilote ; désactiver le maître du bus des ponts Thunderbolt tant qu'aucun pilote ne les réclame.
* **Confiance** : élevée.

### N12 — PBKDF2-SHA256 à 20 000 tours pour les mots de passe

* **Gravité** : basse
* **Fichiers** : `src/svc/authd/authd.c:84` (`ITERATIONS 20000`), `:370-389`.
* **Description** : très en dessous des recommandations actuelles (centaines de milliers de tours, ou Argon2id,
  que le coffre utilise déjà) ; shadow.ini et un échange rdpd capturé (N9) se cassent vite sur GPU.
* **Correction proposée** : Argon2id (déjà dans le coffre) ou ≥ 600 000 tours ; relever l'empreinte à la connexion.
* **Confiance** : élevée.

### N13 — Écriture non atomique de shadow.ini ; quelques chemins « ouverts en cas d'échec »

* **Gravité** : basse
* **Fichiers** : `src/svc/authd/authd.c:416-420` (`O_TRUNC` puis écriture), `:85` (`FILE_MAX 8192` : au-delà,
  les lignes suivantes sont perdues à la réécriture), `:312-318` (pas de ligne = pas de mot de passe) ;
  `src/sys/login/login.c:605-606` (sans fenêtre, la session du premier compte s'ouvre sans mot de passe).
* **Description** : une coupure pendant `passwd` laisse un shadow.ini vide ou tronqué : tous les comptes
  concernés se connectent alors sans mot de passe (écran de connexion, et `AUTH_VERIFY` pour telnetd/ftpd). Si
  Elegant refuse la fenêtre de login (ressources épuisées), login ouvre le premier compte.
* **Correction proposée** : écrire dans `shadow.ini.new` puis renommer ; distinguer « pas de ligne » de « fichier
  illisible/tronqué » (en-tête attendu) ; dans login, refuser d'ouvrir une session quand la fenêtre manque et qu'un
  mot de passe existe.
* **Confiance** : élevée.

### N14 — Épuisement de la mémoire physique par n'importe quel process

* **Gravité** : basse (déni de service local)
* **Fichiers** : `src/kernel/core/image.c:266-339` (`IMAGE_CREATE` : jusqu'à 4 Go par fenêtre, pages allouées
  d'avance ; une image scellée reste dans la table, 128 au plus, même sans process) ; `src/kernel/core/mm.c:910-939`
  (`BUF_CREATE` : 64 Mo contigus, 128 tampons, sans quota) ; `src/kernel/core/ipc.c:889-931` (SHM).
* **Correction proposée** : quotas par uid (pages épinglées), libérer une image passagère scellée sans process.
* **Confiance** : élevée.

### N15 — L'uid des process, vu par vfs, dépend d'avis qui peuvent se perdre

* **Gravité** : basse
* **Fichiers** : `src/svc/vfs/pids.c:57-69` (uid lu une fois puis gardé) ; `src/kernel/core/svc.c:703-723`
  (`svc_notify_setuid` : l'avis est perdu si `kzalloc` échoue).
* **Description** : si l'avis d'un `PROC_SETUID(pid, 1000)` est perdu (mémoire du noyau épuisée, voir N14), vfs
  garde l'ancien uid (0 pour un process d'abord lancé en uid 0 puis rabaissé) : règles contournées pour ce process.
* **Correction proposée** : faire porter l'uid par le noyau dans chaque requête (`SVC_REQ_INFO` le donne déjà) et
  l'y lire au lieu d'un cache.
* **Confiance** : moyenne (exploitation difficile).

### N16 — Agents d'IA : permissions décidées par les serveurs MCP eux-mêmes

* **Gravité** : basse
* **Fichiers** : `tools/claude/claude_acp/mcp.py:165` (`readOnlyHint` du serveur pris tel quel) ;
  `tools/claude/claude_acp/agent.py:561` (un outil « en lecture seule » ne demande jamais l'accord), `:580-587`
  (`read_file`, `grep`, `glob` sans accord, sur tout ce que le compte lit) ; `:31-35` (mode « tout accepter ») ;
  N1 (consignes de `/res/agents` modifiables en mode log).
* **Description** : un serveur MCP tiers (celui d'une app) peut marquer « lecture seule » un outil qui agit ; un
  document piégé peut faire lire `~/.ssh` à l'agent (le contenu part chez le fournisseur du modèle).
* **Correction proposée** : ne croire `readOnlyHint` que pour une liste de serveurs du système (korin-mcp) ; exiger
  l'accord pour lire hors du dossier de travail.
* **Confiance** : élevée.

### N17 — Programmes « de confiance » d'Elegant lançables par tous

* **Gravité** : basse
* **Fichiers** : `src/sys/elegant/remote.c:23-26` (`/bin/screenshot`, `/bin/korin-mcp`, `/bin/overlaytest` dans
  la liste qui lit l'écran entier et les fenêtres des autres) ; `src/svc/hidmanager/hidmanager.c:1019` (seuls
  ABONNER et PILOTE sont réservés : la disposition du clavier de toute la machine se change sans droit).
* **Description** : la règle « une app ne lit que ses fenêtres » (G4) se contourne en lançant `/bin/screenshot`
  ou `korin-mcp` et en lisant leur sortie ; un process peut changer la disposition du clavier pendant qu'un autre
  compte tape son mot de passe à l'écran de connexion (gêne, verrouillage par essais faux).
* **Correction proposée** : demander l'accord de l'utilisateur (ou la session sûre) pour ces outils ; réserver
  `HID_OP_KEYMAP` à login, Réglages et au compte de la session.
* **Confiance** : élevée.

### N18 — Petites lectures hors bornes dans netd

* **Gravité** : basse
* **Fichiers** : `src/svc/netd/stack.c:741-770` (`tcp_parse_mss` : `p[1]`, `p[2]`, `p[3]` lus sans vérifier
  `p + l <= end` ni `l >= 3/4`) ; `:1545-1600` (`dns_resolve` : la réponse est lue à `q + 256` alors que la requête
  d'un nom long dépasse 256 octets : la comparaison de la question porte en partie sur des octets que la réponse
  vient d'écrire, et le renvoi après un délai part abîmé).
* **Description** : quelques octets lus au-delà des options TCP ; affaiblissement de la vérification de la question
  DNS pour les noms de plus de 240 caractères. Pas de corruption trouvée.
* **Correction proposée** : vérifier `p + 2 <= end`, `l <= end - p` et la longueur par option ; tampon de réponse
  séparé dans `dns_resolve`.
* **Confiance** : élevée (impact faible).

### N19 — Téléchargements de pkg : redirection vers http, taille non bornée, index sans date limite

* **Gravité** : basse
* **Fichiers** : `src/lib/pkg/pkglib.h:946-952` (une redirection https → http est suivie) ; `:732-745`
  (`FileSink` écrit tout ce qui arrive : `want` n'arrête pas la réception) ; `:1160-1175` (l'ancien index signé du
  même numéro est accepté indéfiniment).
* **Description** : un intermédiaire ne peut pas faire installer un paquet (empreinte et signature vérifiées), mais
  il peut remplir le disque (`/tmp/0` ou `/var/pkg/dl`) ou geler les mises à jour en rejouant le dernier index.
* **Correction proposée** : refuser la descente vers http, couper à `p.size` octets, ajouter une date d'expiration
  signée dans l'index.
* **Confiance** : élevée.

## 3. Points forts observés

* **Entrée/sortie du noyau** : `syscall` reconstruit une trame d'interruption et revient toujours par `iretq`
  (`switch.asm`) : pas de piège `sysret` avec RIP non canonique ; `arch_signal_return` (`sigframe.c:107-161`) remet
  CS/SS, ne garde que les drapeaux permis, refuse un RIP/RSP hors de l'espace utilisateur, assainit MXCSR et
  l'en-tête XSAVE. `FMASK` coupe IF, TF, DF, NT, AC à l'entrée.
* **Copies utilisateur** : `user_check` borne à `0x0000800000000000` avec test de débordement, amène les pages
  paresseuses, puis `arch_copy_user` rattrape une faute ; `copy_string_from_user` ne lit jamais au-delà de la page.
* **Mémoire** : NX sur les pages utilisateur et noyau, texte du noyau en lecture seule, CR0.WP ; `vm_map_page`
  refuse de remplacer une page présente ; ordre des verrous et invalidations de TLB documentés et respectés (fautes
  de copie sur écriture, vues de `SVC_MAP_CALLER` épinglées et rendues après l'invalidation).
* **IPC** : avis du noyau (op ≥ 0xFFF0) infalsifiables, pid de l'appelant posé par le noyau, `SVC_FORWARD` garde
  le pid d'origine, `SVC_REQ_INFO` donne uid et père sans course ; un compte ne peut jamais prendre un nom de service
  déjà pris une fois par le système (`sys_seen`) ; tampons partagés accordés par pid, « tous » refusé ; SHM nommée
  par compte ; sceaux (`SEAL_WRITE`) tenus aussi par `VM_PROTECT`.
* **Process** : pids à génération, `KILL`/signaux/`WAIT`/`PROC_EXE` limités au même compte ; `fork` et `exec`
  interdits à un process à droits ; `SPAWN_UID`, `SETUID`, `PROC_SETUID` réservés à l'uid 0.
* **Aléa** : ChaCha20 amorcé par RDSEED/RDRAND et la gigue du compteur avant le premier process, clé renouvelée à
  chaque appel (effacement en avant).
* **Services de fichiers et disques** : les ops de chemin d'un service de fichiers ne sont acceptées que transmises
  par vfs ou d'un appelant à droits/uid 0 (`fs_path_ok`) ; l'accès brut aux disques et l'USB sont réservés aux
  pilotes ; un compte ne monte qu'une source amovible ou réseau sous `/media` ou `/net`, sans `..` ; renommer un
  ancêtre d'un chemin protégé est refusé ; noms courts 8.3, points et espaces finaux, casse de FAT pris en compte.
* **authd** : comparaison en temps constant, blocage progressif par compte et par origine, `vrai_sudo` (chemin et
  empreinte du binaire), carte d'élévation en uid 0 sur une couche sûre avec un repère secret de la machine, jeton
  aléatoire et pid de la carte vérifiés, « retenir » limité au shell père ou à l'app.
* **Coffre** : Argon2id + clé de la machine scellée dans le TPM, AES-256-GCM par secret avec données associées
  (compte, identité, nom), identité par empreinte du fichier et carte quand elle change.
* **Paquets** : index signé ECDSA P-256, carte de destination vérifiée, numéro de série contre le retour en arrière,
  taille et SHA-256 de chaque archive vérifiés avant toute écriture, noms d'archive filtrés (`..`, `:`, `\`, points
  finaux, `var/`, `etc/shadow`…), TLS 1.2+ avec vérification du nom d'hôte, téléchargement de l'uid 0 dans
  `/tmp/0` que vfs lui réserve dans tous les modes, `fetch =` en HTTPS avec empreinte signée.
* **Réseau** : aucun service en écoute par défaut sur le PC (`skel/pc/etc/services.txt`) ; ISN TCP aléatoire ;
  DNS à identifiant et port aléatoires, réponse acceptée seulement du serveur, port 53, même question ; ARP qui
  n'apprend que des réponses attendues et fige la passerelle ; adresses martiennes filtrées ; pas de fragments ;
  ports < 1024 réservés à l'uid 0 ; DHCP qui n'accepte l'ACK que du serveur de l'offre.
* **WPA2** : compteur de rejeu, nonce vérifié, protections contre KRACK (message 3 et clé de groupe rejoués sans
  réinstallation), EAPOL en clair refusé après l'installation des clés.
* **Jet** : fichiers de débogage de `/etc` pris seulement s'ils sont au système, téléchargement non demandé par
  l'utilisateur soumis à confirmation, nom de fichier téléchargé assaini, un process web par onglet.
* **Serveur web (webd)** et **smbd** : chemins décodés puis refusés s'ils contiennent `..`, `\`, `:`, un octet nul ou
  un composant caché ; smbd sans invité, signature active.
* **Moniteur du noyau** retiré des constructions normales (`#ifdef KORIN_MONITOR`, `pc_main.c:427-433`).

## 4. Ce qui n'a pas pu être vérifié

* **Rien n'a été construit ni lancé** : les chaînes de N1, N2 et N7 sont déduites du code, pas essayées sous QEMU.
* **WebKit** (copie d'Onyx dans `src/extra/webkit`, absente du dépôt) : ni sa révision ni ses correctifs de
  sécurité ; le pont JavaScript de Jet (`webapp.cpp`) et `cdmhost` (module Widevine propriétaire, traduction de ses
  appels Linux) n'ont été que parcourus.
* **Code en assembleur** encore utilisé sur PC (`netd.asm`, `vfs.asm`, `ramfs.asm`, `fatfs.asm` et leurs `.inc`) :
  seules les parties en C ont été lues ; le chargeur UEFI (`bootx64.asm`, `menu.inc`, `ab.inc`, `firmware.inc`) l'a
  été en surface (lecture de `boot.cfg`, A/B).
* **Bibliothèques tierces** : OpenSSL, libsmb2 (moteur de smbd), Mesa (intelgpu), ACPICA (acpid), CPython, bash,
  BusyBox : versions et correctifs non vérifiés.
* **Pilotes** : iwl (hors du supplicant), xhci, usbhid/usbstorage, nvme, intelgpu, hda : survol seulement ; pas de
  recherche systématique de dépassements dans l'analyse des descripteurs et des trames.
* **Hôte Linux** (`src/sys/linuxhost`, `src/lib/linux`) et le binaire de Claude Code qu'il lance : non audités.
* **Libposix** (`src/lib/posix`) : non auditée (allocateur, `realpath`, analyse des arguments de crt0).
* **Comportement réel de l'installation** : quel système de fichiers porte `/` sur le ZBook installé (ext4 ou
  FAT32), quelles lignes `osinstall` et Réglages ajoutent ensuite à `system.ini` ; le constat N1 suppose qu'aucune
  n'ajoute `access = enforce` (aucune occurrence trouvée dans le code).
* **Micrologiciel et matériel** : verrous de l'UEFI, TPM, Thunderbolt (niveau de sécurité du contrôleur) : hors de
  portée d'une lecture de code.

---

## Suivi (27/11/2026)

* **N1, en partie** : vfs protège désormais en écriture, quel que soit le mode, `/etc/mounts.txt`,
  `/etc/bash.bashrc`, `/etc/claude-code`, `/res` (compétences des agents, module Widevine, polices) et `/var/log`
  (`src/svc/vfs/access.c`, liste `marques`). Reste le passage en « enforce » par défaut (étapes E73 et E74 du plan) ;
  `/devtools` n'est pas protégé : pip, lancé sous un compte, y installe.
* **N2, corrigé** : le premier mot de passe d'un compte qui peut administrer est confirmé à la carte d'élévation
  (couche sûre, devant la machine) avant qu'authd l'écrive (`src/svc/authd/authd.c`, `premier_mdp`) ; `passwd` et
  Réglages › Comptes attendent la carte. Essayé sous QEMU.
