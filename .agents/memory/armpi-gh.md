---
name: armpi-gh
description: "18/11 : pas de gh (pas de PR) ; au plan : identification GitHub par code d'appareil dans git-credential-korin (oauthd), étude docs/etude/GH.md §5"
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

**18/11, piste plus simple notée (pas maintenant, dit l'utilisateur)** : sans gh, git-credential-korin lancerait lui-même le code d'appareil pour github.com (fournisseur [github] dans providers.ini, `oauth token NOM` à ajouter) ; gh ne servirait plus qu'aux PR, issues, api.

**18/11, DÉCISION** : c'est cette piste que l'utilisateur veut, **au plan** ; pas de `gh`, pas besoin de PR (il ne fait que push, pull, fetch, checkout). Détail : docs/etude/GH.md §5. Il lui faudra créer une OAuth App GitHub (Device Flow) pour le client_id.
