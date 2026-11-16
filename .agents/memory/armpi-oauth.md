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
