---
name: armpi-securite
description: "Plan du 18/11 contre une page web piégée (PC d'abord) : access=enforce, SMEP/SMAP/UMIP, unveil dans vfs pour WebContent de Jet, ASLR, pledge ; docs/etude/SECURITE.md, rien de fait"
metadata:
  type: project
---
Le 18/11/2026, après la réévaluation de l'écart avec SerenityOS (session « Revue KorinOS vs Serenity » du 17/11),
l'utilisateur a mis au plan, dans cet ordre : 1) `access = enforce` dans vfs ; 2) SMEP, SMAP, UMIP sur le PC ;
3) `unveil` dans vfs (liste de chemins par process, posée par Jet sur son process de rendu) ; 4) ASLR ; 5) `pledge`.
Détail : docs/etude/SECURITE.md. Rien de fait.

**Why:** assouplit [[armpi-pas-de-securite-attaquant]] (29/10) : Korin navigue sur Internet avec un vrai compte
Google ; une page piégée dans Jet est le seul risque réel au quotidien.

**How to apply:** PC d'abord ; le Pi après le noyau commun en C (son noyau asm n'a pas d'uid). Déjà en place :
comptes, coffre ([[armpi-coffre]]), serveurs réseau éteints par défaut, capacité `rights` pour le matériel, NX.
Voir [[armpi-multi-utilisateur]], [[armpi-webkit]].

**24/11, audit de sécurité du PC** : docs/etude/AUDIT-SECURITE-PC.md (lecture seule, rien corrigé). Causes de fond :
avis du noyau forgeables (`sys_svc_call` ne refuse pas `op >= 0xFFF0`), services sans contrôle d'appelant (vfs
PATHMAP/MOUNT, fs.*, blk.*, pcid, usbd, hidmanager, Elegant, oauthd), `/tmp/session.ini` qui décide de l'uid,
failles noyau (SHM_TRUNC, ELF offset+filesz, IMAGE_CREATE ouvert, FMASK sans NT). Plan en 5 vagues au §9 ; `enforce`
n'a de sens qu'après les vagues 1 à 3.

**24/11, seconde passe** (même fichier, §10-§17, plan révisé §17 qui remplace §9) : chaînes C1-C8 (C3 : anneau 0 en
4 appels même en enforce par blkserv op_attach, fenêtre déclarée > tampon -> DMA en RAM) ; Jet : WebContent sans
cloisonnement, cookies.db en clair, /etc/web-probe.js injecté partout, intelgpu CHUNK_FREE sans unbind ; cache
/file-* de libposix empoisonnable entre comptes ; démarrage : partitions visibles de Windows, ESP de Windows en
écriture, CR0.WP jamais posé, console de secours sans mot de passe ; vterm /dev/pts ouvert à tous. L'utilisateur fera
les corrections après le portage de Blender (24/11).

**24/11, plan de correction** : docs/etude/PLAN-CORRECTIONS-SECURITE.md (E00-E75 en 12 phases, chaque étape avec son
analyse de non-régression faite sur le code, règle de l'utilisateur : « avant toute implémentation, vérifier en
profondeur qu'il n'y aura pas de régression »). §2 : 29 propositions du brouillon qui auraient cassé le système (avis
= pid 0 et non REQ_SIGNAL-1, WS_SIGNAL, IMAGE_CREATE par uid, NXE au trampoline, 0xB2 batterie, DEV_MAP, rights de
session et SVC_REGISTER, WF_TOP/GRAB d'UIKit, VFS_BOOT_BEGIN/pkglib, forbidden et paquet pc, NTP qui recule…).
Règle Pi : E_NOSYS = uid 0 permis. Rien d'implémenté ; corrections après Blender ; E62 et E70 pas pendant Blender.

**25/11, branche `security-fix`** (session cloud, depuis main 1068ab7) : faites et poussées E01-E26 (sauf E10
pages de garde, E11 refus, E09 SPEC_CTRL), E29 réduite (marques de /tmp à l'uid 0 même en log), E30-E32, E35,
E37, E40-E41, E43-E44, E46 (1er volet), E63, E66, E67, E69 ; shadow.ini, users.ini, elevate.ini protégés même en
mode log. Essais : ktest, bureau complet et essais en uid 1000 (sectest.sh), réseau usb-net, identiques à main.
**Décisions de l'utilisateur (25/11)** : E70 : comptes + repli secret commun ; E33/E34 : NON (les programmes de
session gardent `rights`, pas de sudo pour les façades) ; E68 : webd --dev local par défaut, option réseau ;
JIT W^X : plus tard.

**25/11 soir, suite de `security-fix`** : faites aussi E68, E70 (telnetd/ftpd par comptes), E48 (oauthd par uid),
E38 (AUTH_SUDO du seul vrai /bin/sudo par empreinte, portée calculée par authd, compteur par compte et origine
plafonné à 5 min ; compte sans mot de passe : sudo passe par la carte), F4 vraiment (fs.* : ops par chemin de vfs,
uid 0 ou droits seulement : `fs_path_ok`), E27 (/tmp/<uid> à ce compte dans tous les modes, TMPDIR de libposix,
O_WRITE dans ramfs/ext4fs), K1 (pkglib télécharge dans /tmp/0/pkg), G4/G5 (Elegant : captures, liste, fermeture,
MENU_COMMAND à une liste de programmes par SYS_PROC_EXE), N10 (sceaux SHM). Une relecture par un agent a trouvé
et fait corriger : montage par « /media/../etc », renommage d'un parent d'un chemin protégé (« mv /etc /e »).
Listes de vfs valables dans tous les modes : /sys, /bin, /sbin, /lib, /etc/pkg, /var/pkg, /etc/init*, services*,
users.ini, elevate.ini, shadow.ini, coffre, web-probe.js, ca-bundle, /apps/screenshot.app, /apps/taskman.app.
**Reste** (voir le plan) : A5/G3 couches et carte imitable (E36 couches, E39), E10, E11 refus, E09, E42, E45, D8
(KV_UNLOCK), E49-E62, E64-E65, R6/R7, S4/S6/S8, E71-E75 ; /apps et /devtools inscriptibles jusqu'à enforce
(qbasic et qbstudio écrivent /apps sans élévation : à adapter avant E72) ; /etc/system.ini écrit par Réglages sans
élévation (E72). Harnais d'essai : un seul QEMU à la fois (la console série est partagée).

**25/11 nuit** : aussi faits A5/E39 (couche sûre d'Elegant pour authcard, login et ce que login lance en uid 0 ;
repère secret /etc/secure-mark montré par la carte et login), G3 (WF_GRAB d'une app seulement au premier plan),
identité des programmes de confiance d'Elegant par chemin ET empreinte comparée au fichier (une image se crée à
n'importe quel chemin : AppKit crée l'image dans le process qui lance, donc PEXF_SYSIMG ne sert pas), E09
(SPEC_CTRL si eIBRS), E10 (piles noyau gardées, PML4[384]), B10, B16, D8, L4 (forbidden canonique, sans casse),
K2 (serial de l'index : mkrepo, pub.py, pkglib ; rien publié), P9 (usbhid), R6/R7, INQUIRY. La clé de publication
est dans la variable ONYX_PKG_KEY (dit par l'utilisateur le 25/11) : ne publier qu'à sa demande.

**25/11, nouvelles décisions de l'utilisateur** (docs/etude/DECISIONS-SECURITE-25-11.md, docs/etude/MISES-A-JOUR-AB.md) :
E33 ACCEPTÉ (revient sur le non) : programmes de session sans droits de /sys, mais d'abord un tableau
programme / opération / voie, montré à l'utilisateur avant de changer le code ; apps de qbasic/qbstudio dans
HOME:/apps, puis /apps à l'uid 0 ; Réglages sans élévation pour réseau, Wi-Fi, clavier, énergie, par les services
(caller_session_or_admin), system.ini protégé ensuite ; mises à jour A/B avec version de secours confirmée par root
seulement (remplace E55) ; publication de l'index autorisée avec ONYX_PKG_KEY (effacer la clé temporaire après).

**25/11, décisions (suite)** (docs/etude/DECISIONS-SECURITE-25-11.md, fin) : date/heure/fuseau, affichage, disposition
par défaut : par élévation ; disposition du compte chargée à son choix sur l'écran de connexion ; même nom /apps et
HOME:/apps : les deux, « (u) » devant celle de l'utilisateur ; sudo toujours avec mot de passe, un compte admin doit en
avoir un (osinstall, Réglages, authd) ; oauthd par compte (oauth.<uid>) ; moniteur du noyau seulement dans les builds
de test ; / et /home en ext4 à l'installation ; préchargement seulement /apps par élévation, images partagées créées
par le démarrage seul ; K3 pas maintenant ; E34 ACCEPTÉ (tuer/signaler hors de ses process : élévation).

**26/11, faits sur `security-fix`** : A/B sur le PC (docs/etude/MISES-A-JOUR-AB.md §6 : chargeur ab.inc, pkg commit dans
\korin\a|b avec SHA-256, `pkg bootok` lancé par init2 à l'ouverture de session (rend 3 essais, copie /var/pkg/ab.txt),
question de secours : `pkg` au terminal, Logiciels par `ui_elevate_system` (uikit.abi version 223, carte d'authd mode
« system-ask », `pkg --ab-keep`)) ; / et /home en ext4 (osinstall refuse fat32/exfat hors réinstallation,
l'installateur sans choix, plan-test.ini en ext4 avec un mot de passe). Essais QEMU : abcommit.sh, abseq.sh,
itest.sh (scratchpad). Restent : E33 (attend le feu vert sur E33-CARTOGRAPHIE.md), préchargement (avec E33), la
publication des paquets PC dans main (paquets seulement, après tout le reste, demandé par l'utilisateur).
**26/11, suite** : E33 fait (journal « droits : » du noyau, vfs `sys_droits` : /sys/svc + elegant, pkgd, notifyd, clipd,
printd ; noms de service par compte, svc.c ; init2 refuse /sys/svc pour un compte ; voronoy relancé sous le compte) ;
préchargement : images épinglées par uid 0 + droits seulement et /apps seulement, plus d'image partagée (IMG_FORUID
d'AppKit pour SPAWN_AS). Publié dans main (d2cee2a, paquets seulement, depuis un worktree d'origin/main avec
`pub.py --no-push --base HEAD`, puis push) : pc 2026.11.113, pkgman 1.0.25 (sait se passer d'ui_elevate_system),
archiver, calendar, notes, pocketqi, qbasic, qbstudio, webd. Ne jamais publier dock.ini ni oauth/providers.ini d'une
construction du cloud (gardés de l'archive). L'utilisateur teste sur le ZBook.

**26/11 nuit, ÉCRAN GRIS DU ZBOOK (pc 113)** : cause = L4 interdisait « etc/keyvault » (le paquet pc livre
etc/keyvault/interpreters.ini) -> pc laissé étagé, ab.txt jamais écrit -> les programmes de la 113 avec le noyau et
l'initrd (kits) de la 112 -> « kit absent ou trop ancien : /lib/uikit » pour login, panel, menubar. Corrigé (322b476,
pkgman 1.0.26 publié, main 0197f5c). Leçon : un commit partiel ne doit jamais mélanger programmes neufs et vieil initrd.
La clé de réparation (osinstall « mode = repair », écrite dans le worktree /tmp/claude-0/base, pas dans le dépôt) a d'abord
ÉCRASÉ /etc avec les fichiers par défaut du paquet (users.ini, mounts.txt sans BOOT ni /home...) : la v2 garde /etc
(unzip -x etc/*). Sur un PC installé, /etc/mounts.txt doit avoir « BOOT = blk.boot, fatfs, /, ondemand » et
« /home = blk.home, ext4fs » (sinon pkg écrit le noyau sur / et pas sur l'ESP). Compte de l'utilisateur : trolly.
**26/11 matin, suite du ZBook** : 2e cause de l'écran gris = le firmware lance \EFI\BOOT\BOOTX64.EFI (vieux chargeur,
jamais mis à jour, ignore ab.txt) -> pkg met aussi à jour cette copie et les chemins d'avant A/B (758281c, pkgman 1.0.27).
Touchpad : pcid (E41) refusait d'écrire une BAR 64 bits « vide » (relue 0x4) -> ab376ef, pc 114. Google Drive : init2
refusait /sys/svc/oauthd pour un compte -> exception (pc 115, main 31f7b75). Ces trois régressions venaient de
correctifs de sécurité non essayés sur le matériel réel : essayer sur le ZBook avant de publier un pc.
**26/11 après-midi, E70 fini (non publié)** : RDP par compte (ONYXRDP3, authd AUTH_SALT 12 / AUTH_PROOF 13, compte de
la session ou admin ; Korin Remote : champ Compte, non compilé dans le cloud), RDP S6 (le nouveau client s'authentifie
à côté : pend_* de rdpd.c), VNC S4 (secret « remote-vnc » du coffre, Réglages ; refus en mode comptes sans lui), compteur
d'échecs pour VNC/RDP (rp_note), SMB S8 (smbd et smbfs exigent la signature ; smbfs « unsigned = 1 » pour l'invité).
Essais : tools/remote/rdp_account_test.py, vnc_auth_test.py ; SMB par un client libsmb2 natif (scratchpad). Écran de
connexion : bouton d'extinction (07847e9) ; pkg sans sudo (c6974e4). À publier après un essai ZBook : pc, apps
(Réglages : control), smbd/smbfs. Point ouvert : sous QEMU (TCG), environ 1 premier démarrage sur 3, un appel de
service vers vfs dépasse 30 s (keyvault : renommage de /var/keyvault/0.db, errno 116 = ETIMEDOUT ; smbd sans
partages) : surcharge de fatfs au démarrage supposée, pas un interblocage prouvé. /bin/bash n'existe pas sur le PC
(scripts d'essai : /bin/sh).
