---
name: armpi-gh
description: "Au plan le 18/11 : gh (GitHub CLI) pour s'identifier et pousser depuis Korin ; gh est en Go, voie choisie : gh auth en C sur oauthd + coffre, puis commandes courantes en C, le vrai gh par la couche Linux plus tard"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026) : ajouter au plan le port de `gh` ; git est déjà là, `gh` faciliterait
l'identification pour pousser depuis Korin. Il préfère un port en C/C++ à un port de Go.

Analyse (docs/etude/GH.md) : `gh` est en Go (~250 000 lignes, Go 1.24+), aucun port C/C++ n'existe ; gccgo de GCC 14
(Go 1.18) ne le compile pas ; porter Go = nouvel OS dans le runtime. Proposé : 1) `gh auth` en C sur ce qui existe
(oauthd a le flux par code d'appareil, le coffre, git-credential-korin lit déjà `git/github.com/<compte>`) ;
2) sous-ensemble des commandes en C (api, repo, pr, issue) sur libcurl + une lib JSON ; 3) le vrai `gh` binaire Linux
par [[armpi-linux-compat]] quand elle existera. Trois questions en attente (client_id, étape 2, le Pi).

**How to apply:** relire l'étude et les réponses avant de commencer. Voir [[armpi-oauth]], [[armpi-coffre]].
