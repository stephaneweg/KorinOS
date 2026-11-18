---
name: armpi-multi-utilisateur
description: Demande du 15/11 — plusieurs utilisateurs (/home/<nom>), utilisateur système seul à écrire /bin /apps /sys /etc, élévation pour les mises à jour, /sbin séparé de /bin ; étapes 1 à 4 et 6 faites sur le PC (17/11), règles de vfs encore en access = log (étape 5 à faire)
metadata:
  type: project
---

Le 15/11/2026, l'utilisateur a demandé d'étudier le multi-utilisateur :
- des dossiers par utilisateur dans `/home`, comme sous Unix ;
- un utilisateur système, seul autorisé à modifier `/bin`, `/apps`, `/sys` et `/etc` ;
- une élévation de droits pour les mises à jour ;
- un **`/sbin`** séparé de `/bin` pour les commandes système.

L'étude est faite (non commitée au moment de l'écriture) : docs/etude/MULTI-UTILISATEUR.md. Elle propose :
- un uid par process dans le noyau, recopié dans les messages (MSG_UID) ;
- des règles par chemin dans vfs (FAT n'a pas de propriétaire), d'abord en mode journal ;
- authd et sudo pour l'élévation ;
- pkgd comme démon system ;
- `HOME:/etc` pour les réglages personnels (décision du 16/11 ; d'abord `~/.config/korin`) ;
- login avec connexion automatique par défaut quand il n'y a qu'un compte.

Questions de l'étude (§4) : quatre réglées les 15-16/11 ; **reste la question 4** (données d'app à la racine du home ou sous ~/.local/), au 18/11.

**Why:** c'est une évolution de [[armpi-pas-de-securite-attaquant]]. Le cadre reste la résistance aux bugs et la séparation des données, pas un système durci.

**How to apply:**
- Avec un seul compte en connexion automatique, le comportement doit rester exactement celui d'aujourd'hui.
- Sur le Pi, ne jamais déplacer de données vers SD1:.

Voir [[armpi-volumes-sys-home]], [[armpi-x86-pc]].

**15/11 (suite), proposé par l'utilisateur :**
- `HOME:` est un volume virtuel de vfs qui désigne toujours `SYS:/home/<utilisateur de l'appelant>/` (§2.4 de l'étude).
- Les apps écrivent `HOME:/…`. Le remplacement de `SD0:/home` par `HOME:` est mécanique et peut se faire dès l'étape 1 (alias vers `SD0:/home/` tant qu'il n'y a pas d'uid).
- Remplace l'ancien projet `SD1: → HOME:`. Sur le Pi, la partition exFAT garde `SD1:` ou devient `DATA:` (à choisir).

**15/11 soir** : étape 1 faite avec l'arbre unique (voir [[armpi-arbre-unix]]) : `HOME:` = /home pour tous (table par uid à faire à l'étape 2), réglages personnels dans HOME:/.config/korin. Prochaine étape : uid dans les noyaux.

**16/11, décision de l'utilisateur** : les réglages personnels vont dans **`HOME:/etc`** (`/home/<nom>/etc`, comme le dit docs/DESIGN-CONNEXION.md §2.6), pas `HOME:/.config/korin`. Fait dans uk_conf_path, korin_conf_path, conf_path et alarms.c ; l'ancienne place reste lue en repli (et copiée à la première écriture). L'étape 2 (identité) se fait dans ce dépôt, après ce changement.

**16/11, étapes 2 et 3 faites (PC, QEMU)** : uid dans le noyau du PC (SYS_GETUID/SETUID/PROC_UID 0x90-0x92, SPAWN_UID, kill et signaux par uid ; ktest 56 ok) ; pas de place pour MSG_UID dans l'en-tête : vfs demande l'uid par SYS_PROC_UID et le garde par pid (avis SVC_OP_SETUID). AppKit SYS_SPAWN_AS (0x8F ; chemin 0 = pour le prochain lancement). /etc/users.ini (system 0, user 1000, home /home) ; Elegant reste en uid 0 et lance /etc/session sous le compte (korinrt spawn_session) ; init2 lance startup.txt sous le compte et /sys pour un process sans droits avec l'uid du demandeur ; telnetd et console : shell sous le compte. libposix users.c lit users.ini. Bienvenue n'est plus lancé au démarrage. Étape 3 : HOME: selon l'uid (vfs homes.c), règles par chemin (vfs access.c, access = log par défaut dans system.ini), propriétaires et modes dans V_STATX (bits 32-57) -> st_uid/st_mode. Les ports liés statiquement (bash, git, python) gardent l'ancienne libposix (bash $UID=0) jusqu'à leur reconstruction. Prochaine : étape 4 (authd, sudo, carte d'élévation, pkgd démon, /sbin).

**16/11, réponses de l'utilisateur** : élévation = mot de passe de l'utilisateur (comme sudo ; celui de system en secours à la console), comptes et groupes de /etc/elevate.ini ; homes invisibles (0700), /home/public ouvert ; installer/retirer un paquet réservé à elevate.ini, mises à jour du dépôt signé sans rien demander.

**16/11, étape 4 faite (PC, QEMU)** : SYS_PROC_SETUID 0x93 (uid 0) ; authd (src/svc/authd, service « auth », services.txt) : AUTH_CAN/SUDO/CHECK/PASSWD/ELEVATE/CARD/WAIT, /etc/elevate.ini ([elevate] users, groups ; sans fichier : groupe admin), /etc/shadow.ini (nom = pbkdf2-sha256$20000$sel$empreinte, SHA-256 maison) ; sudo et passwd (src/bin, écho coupé par VT_TCSETS) ; carte /sys/authcard (UIKit, lancée par authd en uid 0) ; ui_elevate (UIKit, fin de table, version 216 ; asm du Pi : rend 0) ; le noyau abandonne un appel de service au bout de 30 s : authd rend E_AGAIN au bout de 20 s, ui_elevate redemande par AUTH_WAIT. /sbin (tools/sbin.txt, sysroot.sh ; vfs : exécution par l'uid 0 seulement). pkgd dans services.txt (uid 0). Élévation dans pkgui (Logiciels, pkgman), volets Accès à distance, Préchargement, Partage de fichiers (control.h ap_elevate). Tout est en « access = log » : rien n'est encore refusé. Prochaine : étape 5 (enforce, V_OPEN_AS pour smbd/ftpd) après un essai en mode journal sur le ZBook ; étape 6 (login, Réglages › Utilisateurs, /home/<nom>).

**16/11, étape 6 commencée (PC, QEMU)** : /sbin/useradd, userdel, usermod (src/bin/useradd/usersfile.h édite users.ini) ; authd AUTH_SETPW, AUTH_VERIFY, AUTH_HASPW (ouvert à tous) ; libposix : HOME absent ou « /home » -> home du compte ; /sys/login (src/sys/login) dessine la carte du design (WF_ALPHA pour l'ombre), écrit /tmp/session.ini ; Elegant lance voronoy puis login, la session à la fin de login (session_poll) ; init2 lance startup.txt quand /tmp/session.ini existe ; spawn_session lit /tmp/session.ini d'abord ; Réglages › Utilisateurs (applets/users.cpp). Glyphes tb-eye, tb-keyboard, tb-access, tb-network, tb-person (rendus par le Python de Windows : numpy manque dans WSL). Reste : verrou (Super+L), fermer la session, changer d'utilisateur, /home -> /home/<nom>, accès distants par comptes ; étape 5 (enforce) après l'essai en mode journal sur le ZBook.

**17/11, état** : étapes 1 à 4 faites ; étape 6 faite pour l'essentiel (comptes, /sys/login, Réglages › Utilisateurs ; authd lancé par init2 lui-même, a88060b3 ; paquet pc 2026.11.32 publié). Reste de l'étape 6 : verrou (Super+L), fermer la session, changer d'utilisateur, /home → /home/<nom>, telnet/ftp/smb par comptes. **Étape 5 pas faite** : `access` vaut `log` par défaut (src/svc/vfs/homes.c, skel/pc/etc/system.ini) ; rien n'est refusé, les écarts vont au journal. Pi : uid pas porté dans le noyau asm.

**18/11, retenir 5 minutes** (68b304f5) : case de la carte d'authcard et question de sudo ; authd garde (uid, portée) en mémoire : portée 0 = session graphique, sinon le pid du shell (SYS_GETPPID dit par sudo) ; AUTH_FORGET (11) appelé par login à chaque session et par `sudo -k`. Essayé sous QEMU (console et bureau, build/pc/elev5-*). Piège d'essai : vfs refuse d'exécuter depuis /home (mettre un programme d'essai dans /bin). Pas publié.
