---
name: armpi-coffre
description: Coffre à secrets (keyvault) — étude du 18/11 docs/etude/COFFRE.md, décisions de l'utilisateur prises (chemin, interprètes, mot de passe par défaut + TPM, pas de clé de secours, PC seulement) ; rien de codé, 2 questions mineures
metadata:
  type: project
---

Le 18/11/2026, l'utilisateur a demandé une étude : recenser les apps qui gardent un secret en clair et les ranger
dans un coffre chiffré lisible par le seul service ; une entrée = (identité de l'app, nom). Étude :
docs/etude/COFFRE.md (§1 : 15 programmes en clair ; Mail et oauthd chiffrent avec la clé à côté).

Décisions de l'utilisateur (18/11) :
- menaces à couvrir : vol d'un secret par un autre exécutable, vol du disque, vol de la machine ;
- identité = **chemin** de l'exécutable (pas le paquet : marche pour les apps compilées sur la machine) ;
- scripts et .NET : l'interprète donne le chemin du script/assembly (proposé : SYS_PROC_SET_SCRIPT une fois,
  cru seulement pour les interprètes de /etc/keyvault/interpreters.ini) ;
- mot de passe de session par défaut ; pas de clé de secours pour l'instant ; Pi plus tard ; nom keyvault.

Conception retenue dans l'étude : KEK = HKDF(Argon2id(mot de passe), secret scellé dans le TPM 2.0) ; secrets du
système sous TPM + PCR 7/4 ; chemin attesté par vfs (V_LOAD_IMAGE, SYS_PROC_EXE) ; /var/keyvault/<uid>.db ;
access = enforce indispensable pour la menace « autre exécutable ».

**Why:** revient sur [[armpi-pas-de-securite-attaquant]] pour ce domaine, à la demande de l'utilisateur, comme
[[armpi-multi-utilisateur]].

**How to apply:** plan §6 de l'étude (étape 1 : service, AppKit, /bin/secret, mot de passe ; oauthd et Mail
d'abord). Voir [[armpi-oauth]].
