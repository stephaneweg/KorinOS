---
name: armpi-coffre
description: Coffre à secrets (keyvault) — étude docs/etude/COFFRE.md, référence docs/KEYVAULT.md ; fait sur le PC le 18/11 (service, noyau, AppKit, authd, TPM CRB, migrations), vérifié sous QEMU, TPM contre swtpm ; pas sur le ZBook ; access = enforce pas mis
metadata:
  type: project
---

Le 18/11/2026, l'utilisateur a demandé une étude puis (« tu fais tout jusqu'à la fin ») le coffre à secrets.

Décisions de l'utilisateur (18/11) :
- menaces : vol d'un secret par un autre exécutable, vol du disque, vol de la machine ;
- identité = **chemin** de l'exécutable (pas le paquet) ; scripts et .NET : l'interprète donne le chemin ;
- mot de passe de session par défaut ; pas de clé de secours pour l'instant ; Pi plus tard ; nom keyvault.

Fait (PC) : SYS_PROC_EXE 0x94 / SYS_PROC_SET_SCRIPT 0x95 (noyau C), service src/svc/keyvault (OpenSSL,
/var/keyvault/<uid>.db, KEK = HKDF(Argon2id(mdp) || clé machine)), tpm.c (CRB, sans PCR), /bin/secret,
vault_* d'AppKit (version 59), carte /sys/authcard mode coffre, authd → KV_UNLOCK/KV_PASSWD/KV_SETPW,
espace @system (uid 0), vfs refuse /var/keyvault et /etc/keyvault même en mode log, patches bash/CPython/dotnet.
Migrations : oauthd, Mail, apps Claude/Vibe (.env → env/NOM), puis apps et services par deux agents.

Tranché en faisant : compte sans mot de passe = coffre sous la clé machine seule (comportement d'avant gardé).

**Why:** revient sur [[armpi-pas-de-securite-attaquant]] pour ce domaine, à la demande de l'utilisateur.

**How to apply:** pas essayé sur le ZBook ni avec le vrai TPM ; CPython et dotnet pas reconstruits avec la
déclaration du script (bash oui) ; `access = enforce` reste à décider (étape 5 de [[armpi-multi-utilisateur]]).
Pièges : patchs Python écrits par Write : « \r\n » dans une chaîne non brute devient un vrai retour ;
QEMU n'est que dans WSL (OVMF sans shell) : image d'essai avec EFI/BOOT/BOOTX64.EFI sur KORIN OS, qemu_nvme.py
--boot-disk, sortie relue par mtype -i disk.img@@74448896. Voir [[armpi-oauth]].

**18/11, au plan (demande de l'utilisateur)** : un secret par clé nommé `<Fournisseur>.<Nom>` (Gemini.Cle1,
Claude.Cle1…), `kv_list_match` avec un motif (`Gemini.*`), la liste des clés plus gardée dans les réglages mais
demandée au coffre à l'ouverture de la liste déroulante ; Koton d'abord (aujourd'hui un seul secret `ai-keys`
en JSON), sans migration (installation neuve : `ai-keys` et la migration depuis settings.json retirés), puis PocketQI et les autres. Décision : tout nom en UTF-8 accepté (sauf caractères de contrôle), le premier `.` sépare le fournisseur. Détail : docs/KEYVAULT.md §9.

**Gestionnaire du coffre (21/11, demande de l'utilisateur)** : KV_MGR_LIST/GET/DELETE (13-15) servis à Réglages seul
(/apps/control.app[/main]) ; valeur et effacement après le mot de passe du compte, vérifié sur l'enveloppe, puis 5 min
pour le process (choix de l'utilisateur) ; KVF_SYSTEM = coffre du compte system après l'élévation (5 min ensuite).
Volet src/svc/keyvault/applet/applet.cpp -> sys/applets/vault.so (« Coffre », build_keyvault) : liste, Afficher (30 s),
Copier, Supprimer, Secrets du système. docs/KEYVAULT.md § 4 bis. Vérifié sous QEMU PC ; publié pc 2026.11.77.
