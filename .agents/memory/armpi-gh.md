---
name: armpi-gh
description: "18/11 : pas de gh (pas de PR) ; identification GitHub par code d'appareil dans git-credential-korin (oauthd) faite le 21/11, Linux seulement ; docs/GIT.md « GitHub »"
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

**18/11, précision** : sans client_id, l'assistant ouvre Jet sur la création de l'OAuth App GitHub, enregistre le client_id donné, puis fait le login (GH.md §5, point 5).

**21/11, FAIT (branche claude/keen-lamport-62iz6y, essayé sous Linux seulement)** :
- oauthd : `provider = github` (adresses connues) ; le champ `error` lu quel que soit le statut (GitHub rend
  authorization_pending, slow_down avec `interval`, access_denied en HTTP 200) ; jeton **durable** (ni expires_in ni
  refresh_token : `lasting`, access_until = 1<<62), gardé comme un secret (ligne `access` du coffre ; ligne à 3
  champs `access\tcompte\tjeton` dans HOME:/oauth/secrets), oublié par forget ou un changement de client.
  Harnais 71/71 (faux serveur /gh/device, /gh/token dans tools/nettest/oauthsrv.py).
- tools/git/git-credential-korin : coffre d'abord ; sinon pour https://github.com le compte oauthd « github »
  (oauth status/token), sinon connexion : question Jet [O/n] (si /apps/jet.app/main), `oauth setup github
  provider=github scope='repo workflow'` (marche si providers.ini a un client_id), sinon fait créer l'OAuth App
  (valeurs affichées, Jet sur settings/applications/new, Client ID lu sur /dev/tty, mis dans le compte), puis
  `oauth login github >&2`. Nom rendu x-access-token. erase : oauth forget si c'est le jeton d'oauthd.
  GIT_TERMINAL_PROMPT=0 : pas de connexion. KORIN_CREDENTIAL_TTY pour les essais. Banc tools/git/test/credential.sh
  (vrai git, oauth et secret simulés) 29/29.
- skel/pc/etc/oauth/providers.ini : [github] scope = repo workflow, pas de client_id livré (l'utilisateur peut en
  mettre un, il n'est pas secret).
- Le vrai github.com n'est pas joignable depuis le cloud (le proxy de la session l'intercepte : réponse 415).
**Reste** : construire oauthd + bin/oauth pour le PC (tools/pc/apps.sh oauthd), essai QEMU (faux serveur /gh/) puis
vrai push depuis le ZBook ; publier (oauthd, providers.ini et git-credential-korin dans le paquet pc : version à
monter ; partielle : reprendre providers.ini rempli de l'archive publiée, jamais de skel/). Le Pi : pas d'oauthd.

**21/11, suite** : vérifié sous QEMU PC contre le faux GitHub (oauthsrv.py /gh/, compte github avec device_url et
token_url vers 10.0.2.2:8090) : fill, approve, fill sans réseau, oauthd tué puis relancé (jeton durable au coffre),
reject -> forget. Essai : scratchpad prep.py (copie de build/pc/sysroot sans devtools, telnetd, remote.txt) puis
`tools/python/pctest.py run` (image build/pc/pytest/disk.img). Publié dans pc 2026.11.77 avec les volets Comptes en
ligne et Coffre ([[armpi-oauth]], [[armpi-coffre]]). Reste : un vrai push vers github.com depuis le ZBook (créer
l'OAuth App « Korin » avec Device Flow, le Client ID est demandé au premier push ou par Réglages › Comptes en ligne).

**21/11, client livré** : l'utilisateur a créé l'OAuth App GitHub « Korin » (Device Flow coché, ni « wildcard
matching » ni « expire user access token » : jetons durables, le chemin vérifié ; des jetons de 8 h demanderaient que
git-credential-korin demande toujours à oauthd plutôt qu'au coffre). Client ID Ov23liuX6RMNkkcSZ4Fh, pas secret en
code d'appareil : en clair dans skel/pc/etc/oauth/providers.ini [github], publié dans pc 2026.11.78 (providers.ini
publié repris de l'archive, section [github] remplacée). Plus de création d'app ni de question : seulement le code.

**21/11, validé en réel par l'utilisateur** : connexion à GitHub par le code d'appareil avec le client « Korin » livré
(pc 2026.11.78), « bien plus simple que devoir renseigner un client_id ». Essai suivant de l'utilisateur : fetch puis
push d'un petit fichier par Claude Code dans Korin (résultat à noter).
