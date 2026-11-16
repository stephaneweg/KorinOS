---
name: armpi-oauth
description: "OAuth 2.0 du système (16/11) : service oauthd + fonctions oauth_* de NetKit + commande oauth ; choix de l'utilisateur, état des essais"
metadata:
  type: project
---

Le 16/11/2026, en partant de l'étude d'un client Google Drive en flux (docs/etude/GDRIVE.md), l'utilisateur a
décidé d'abord que chaque utilisateur fournirait son client_id, puis (le soir) **comme Mail : un client_id livré** par fournisseur dans /etc/oauth/providers.ini (skel/pc/etc/oauth, [google] à remplir par l'utilisateur, [microsoft] = Onyx Mail), un compte peut donner le sien,
et l'OAuth doit être **un module de NetKit réutilisable partout**. Forme choisie par l'utilisateur parmi trois :
**un service `oauthd` + des fonctions `oauth_*` dans NetKit** (et non OpenSSL dans netkit.so, ni une liboauth.a).

**Why:** NetKit est du C sans libc, chargé par tout programme qui ouvre un socket ; un service garde les jetons hors
des programmes et sert à toutes les langues et aux services (gdrivefs).

Fait (branche claude/x86-64-pc, non commité au 16/11) : `src/svc/oauthd/oauthd.c` (libposix + OpenSSL, service
« oauth », HOME:/oauth : accounts.ini, secrets AES-256-GCM « KORINOA1 », key), `src/svc/oauthd/oauth.c`
(bin/oauth), 13 fonctions ajoutées à la fin de netkit.abi (NetKit passe à 46, `import = appkit` pour f_spawn ;
fmt_dec local renommé nk_fmt_dec à cause du conflit avec appkit_import.h ; places E_NOSYS dans netkit.asm),
`tools/pc/apps.sh oauthd` (PC seulement), faux serveur `tools/nettest/oauthsrv.py`, essai Linux
`src/svc/oauthd/test/run.sh` (49/49 sous ASan, NET=1 : vrai TLS Google + badssl) ; QEMU PC vérifié le 16/11 par un agent (build/oauthtest : image avec netkit.so remplacé dans l initrd, --esp car OVMF d Ubuntu sans shell, résultats lus par mcopy). Doc : docs/OAUTH.md, guide
netkit_fr/en.md, compétence korin-kit-netkit, ligne NetKit de docs/KITS.md.

**How to apply:** suite possible : construire oauthd pour le Pi, passer Mail (mail/oauth.h, jamais essayé) dessus,
volet « Comptes en ligne » de Réglages et dialogue de connexion dans UIKit, gdrivefs (étude GDRIVE.md). Penser à
monter la version du paquet pc à la publication. Voir [[armpi-smb]] (modèle smbfs), [[armpi-x86-pc]].

**Publié le 16/11** (main ec4a6fc1) : pc 2026.11.15, publication partielle depuis l'archive de pc 2026.11.14 (scratchpad pub_oauth.py) : lib/netkit.so, celui de korin/initrd.tar (seule entrée changée, vérifié octet pour octet), lib/netkit.bi, etc/pkg/abi 611, plus sys/svc/oauthd et bin/oauth (choix de l'utilisateur : dans le paquet système, pas de paquet à part). Le paquet gcc (netkit_abi.h, libnetkit.a de /devtools) n'est pas republié : un programme compilé dans Korin ne voit pas encore oauth_*.

**pc 2026.11.17 (16/11, main dbe28753)** : client Google « Korin » (projet korin-514900, application de bureau, non vérifiée, 100 utilisateurs) dans skel/pc/etc/oauth/providers.ini, vérifié auprès de Google (faux code : invalid_grant ; page d'accord acceptée avec retour 127.0.0.1). pc 2026.11.16 d'une autre session était parti de l'archive 14 et avait perdu NetKit 46 et oauthd : 17 = archive 16 + ces fichiers. **Connexion réelle validée par l'utilisateur sur le ZBook (16/11)** : oauth browser gdrive/perso -> connecté, oauth token rend un jeton ya29.… ; pas de curl dans Korin PC pour appeler l'API.

**gdrivefs étape 1 (16/11)** : src/svc/gdrivefs (lecture seule, en flux, calqué sur smbfs + outils d'oauthd), docs/GDRIVE.md, faux serveur tools/nettest/gdrivesrv.py, essai Linux 92/92, QEMU PC vérifié par un agent (build/gdrivetest ; sysroot de 753 Mo : devtools et gcc laissés hors de l'image de 400 Mo). Pas encore contre le vrai Drive. Manque : forcer un rafraîchissement d'oauthd après un 401 (nouvelle fonction de NetKit), umount en commande.

**Secrets hors du dépôt (16/11, demande de l'utilisateur)** : skel/pc/etc/oauth/providers.ini n'a que @GOOGLE_CLIENT_ID@ et @GOOGLE_CLIENT_SECRET@ ; les valeurs sont dans C:\Projects\korin-secrets.ini (hors dépôt, comme onyx_pkg-key.pem.txt ; KORIN_SECRETS pour un autre chemin), remplies dans build/pc/sysroot par tools/secrets.py appelé par sysroot.sh. Ne jamais afficher ni commiter ces valeurs ; une publication partielle doit reprendre etc/oauth/providers.ini de l'archive publiée ou de build/pc/sysroot (remplis), jamais de skel/. Le premier secret était déjà dans l'historique poussé (46fd0595) et dans les paquets pc 2026.11.17-19.

**gdrivefs étape 2 (16/11, d1f5fefe ; publié dans pc 2026.11.21, KorinOS-pub eb54363c)** : écriture (tampon d'envoi HOME:/gdrive/spool, envoi « resumable » à la fermeture par morceaux de 8 Mo avec reprise, créer dès l'ouverture, mkdir, rm à la corbeille, mv, utime ; `readonly = 1` dans drives.ini) et Fichiers › Volume › Se connecter à Google Drive… (menu seulement si /sys/svc/gdrivefs existe ; oauth_setup/token/browser_begin + Jet + browser_poll par ui_dialog_timer ; ligne `netN = gdrive:NOM, gdrivefs` dans /etc/mounts.txt). Linux 133/133, QEMU PC : écriture vérifiée (build/gdrivetest/gdrivetest.sh ; sortie lue par `mcopy -i disk.img@@74448896`, partition 4). Le dialogue de Fichiers n'a pas été lancé. Piège : le faux serveur doit traduire l'alias « root » dans addParents.

**Google Drive validé (16/11, pc 2026.11.21)** : l'utilisateur a monté son vrai Drive à la main sur le ZBook (`mount /net/netN gdrive:perso gdrivefs`) et copié un fichier de plusieurs Go à plusieurs Mo/s. À revoir plus tard : Fichiers › Se connecter à Google Drive… a été lent et n'a pas monté le Drive ; l'utilisateur lancera une session Claude depuis Korin pour diagnostiquer en réel (journal, oauth status, /etc/mounts.txt). Piste : oauth_token et opendir appelés dans la boucle de la fenêtre (elle se fige jusqu'à 20 s) ; faire la connexion dans un thread.

**Montage gdrivefs bloqué (16/11 soir, corrigé 8fa00e34, publié dans pc 2026.11.28 : vfs seul remplacé dans initrd.tar, gdrivefs, oauthd)** : depuis l'étape 6 du multi-utilisateur, libposix lisait /etc/users.ini (par vfs) au démarrage de tout programme sans HOME ; un service lancé par vfs (gdrivefs, smbfs) attendait vfs, qui attendait son inscription (SVC_OPEN 3 s) -> « pas de tel volume », service orphelin (-17 au montage suivant, volume vide). Correction : env.c saute la lecture en uid 0 sans environnement ; vfs tue un service pas inscrit à temps. Essayé sous QEMU (build/gdrivetest). À publier : vfs dans initrd.tar (ne remplacer que vfs dans l'archive publiée : le dossier de travail porte des changements d'autres sessions, init2.c), gdrivefs, oauthd, et les autres services libposix à relier. Piège de construction : un rm -rf de build/pc/initrd incomplet sur /mnt/c a renommé tous les services en .svc (initrd sans init : le noyau part au moniteur) ; effacer le dossier et refaire build.sh.

**pc 2026.11.30 (16/11 soir)** : netd (PC) anneaux de 1 Mo et fenêtre décalée de 5 (4af9cc68 ; la fenêtre de 256 Ko plafonnait gdrivefs à ~4 Mo/s), remplacé seul dans initrd.tar ; Fichiers a1fe6602 (connexion à Google Drive en arrière-plan) construit depuis le source commité (la session du dialogue de copie avait des changements non commités dans main.c : copie temporaire en src/apps/zzfilespub, effacée), et 5 lignes ajoutées au res/lang/en.txt publié (pas de refusion). La copie en arrière-plan (non modale) est demandée à la session du dialogue de progression.
