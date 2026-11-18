---
name: armpi-coffre
description: Coffre à secrets (keyvault) — étude du 18/11 docs/etude/COFFRE.md : recensement des secrets en clair, service keyvault, identité des apps, clé de la machine (TPM/OTP) + mot de passe ; rien de fait, 6 questions en attente
metadata:
  type: project
---

Le 18/11/2026, l'utilisateur a demandé une étude : recenser les apps qui gardent un secret en clair et les ranger
dans un coffre chiffré, lisible par le seul service du coffre ; une entrée = (identité de l'app, nom). Il ne savait
pas où garder la clé du coffre.

Étude : docs/etude/COFFRE.md (§1 recensement : 15 programmes en clair, Mail et oauthd chiffrent avec la clé à côté).
Propositions : service keyvault (uid 0, /var/vault/<uid>.db), vault_* dans AppKit, /bin/secret ; identité = paquet,
sinon chemin attesté par vfs (V_LOAD_IMAGE, SYS_PROC_EXE : aujourd'hui AppKit charge l'image et déclare le chemin,
rien n'est attesté) ; clé maîtresse par compte enveloppée par la clé de la machine (TPM 2.0 PC, OTP du Pi 4,
fichier /etc/vault à défaut), le mot de passe de session (Argon2id) et une clé de secours.

**Why:** revient sur [[armpi-pas-de-securite-attaquant]] pour ce domaine, comme [[armpi-multi-utilisateur]] ; à
confirmer par la question 1 de l'étude.

**How to apply:** rien à coder avant les réponses au §7. oauthd ([[armpi-oauth]]) deviendrait client du coffre.
