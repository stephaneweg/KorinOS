# Étude : `gh` (GitHub CLI) sur Korin

Demandée le 18/11/2026. Rien n'est fait.

**Décision de l'utilisateur (18/11/2026) : pas de `gh`.** Il ne fait que `push`, `pull`, `fetch`, `checkout` (pas de
PR) : au plan, **l'identification GitHub dans `git-credential-korin`** par le code d'appareil d'`oauthd` (§5). Les
§1 à §4 restent pour mémoire. But : s'identifier auprès de GitHub depuis Korin pour **pousser avec git**
sans recopier un jeton à la main, puis, si possible, les commandes courantes de `gh` (dépôts, PR, issues, `gh api`).

## 1. Ce qu'est `gh`

`gh` (github.com/cli/cli, licence MIT) est écrit en **Go** : environ 250 000 lignes avec ses dépendances, Go 1.24 ou
plus récent (génériques, itérateurs). Il n'existe **aucun port en C ou C++**, ni de réécriture complète dans ces
langages.

## 2. Les voies

| Voie | Travail | Jugement |
|---|---|---|
| **A. Porter Go** (`GOOS=korin`) | le runtime de Go fait ses appels système lui-même, sans libc : nouvelle cible dans le compilateur, le runtime, `syscall`, `os`, `net` (comme un nouvel OS dans Go) | très gros ; à faire seulement si on veut Go pour lui-même |
| **B. gccgo** (GCC 14, déjà porté) | libgo passe par la libc (donc libposix), mais GCC 14 ne connaît que Go 1.18 : `gh` ne se compile pas | impossible |
| **C. Le binaire Linux officiel par la couche Linux** (`armpi-linux-compat`, prévue pour Claude Code) | rien de propre à `gh` : un binaire Go statique ne demande que des appels système Linux (futex, clone, epoll, mmap) | gratuit **une fois la couche faite** ; `gh` complet, mis à jour par GitHub |
| **D. Un `gh` en C** (sous-ensemble) | sur ce que Korin a déjà : `oauthd` (le flux par code d'appareil existe : `oauth_device_begin`/`_poll`), le coffre à secrets, `git-credential-korin`, libcurl, OpenSSL | petit pour l'identification, moyen pour les commandes |

## 3. Recommandation : D d'abord, C ensuite

**Étape 1 : `gh auth` en C** (~600 lignes). C'est le besoin réel (pousser depuis Korin).
* Fournisseur `[github]` dans `/etc/oauth/providers.ini` : `device_url = https://github.com/login/device/code`,
  `token_url = https://github.com/login/oauth/access_token`, portée `repo read:org gist workflow`. Le `client_id` :
  une OAuth App de l'utilisateur avec « Device flow » coché, comme Google (à décider : le sien, ou un livré).
* `gh auth login` : `oauth_device_begin` → affiche le code et `https://github.com/login/device` (et l'ouvre dans
  Jet si l'écran est là) → `oauth_device_poll` → `GET /user` pour le nom du compte.
* Le jeton va au coffre sous `git/github.com/<compte>` et `git/github.com/@default` : `git-credential-korin` le rend
  déjà à git, sans rien changer à git. `gh auth setup-git` règle seulement `credential.helper` (déjà fait par
  `/etc/gitconfig`).
* `gh auth status`, `gh auth logout`, `gh auth token` (pour les scripts).
* Les jetons d'une OAuth App de GitHub n'expirent pas (pas de rafraîchissement à faire).

**Étape 2 : les commandes courantes en C** (~3 000 à 5 000 lignes, sur libcurl et une petite bibliothèque JSON à
choisir : aucune n'est encore dans le dépôt pour le C de Korin).
* `gh api` (REST et GraphQL, `--jq` non : pas de jq) ; `gh repo clone/create/view` ; `gh pr create/list/view/
  checkout/merge` ; `gh issue list/view/create` ; `gh run list/view` si utile.
* Même interface et mêmes options que `gh` pour ce qui est fait, pour que les compétences et les habitudes servent.

**Plus tard : le vrai `gh`** par la couche Linux (voie C), quand elle fera tourner Claude Code. Notre `gh` en C
pourra alors rester (sans couche Linux, sur le Pi aussi) ou céder la place.

## 4. Questions

1. `client_id` : une OAuth App de l'utilisateur (comme Google), ou une livrée avec Korin ?
2. L'étape 2 vaut-elle la peine, ou attendre la couche Linux pour le `gh` complet ?
3. Le Pi aussi (C commun : oui, mais pas de coffre sur le Pi : le jeton n'y serait pas gardé) ?

## 5. Au plan : l'identification GitHub dans `git-credential-korin`

Au premier `git push` (ou `pull`, `fetch` d'un dépôt privé) vers `github.com`, sans jeton dans le coffre, l'assistant
lance lui-même le flux par code d'appareil, à la façon du Git Credential Manager :

```
$ git push
Ouvrez https://github.com/login/device et tapez ABCD-1234
Connecté à GitHub.
```

1. **Fournisseur `[github]`** dans `/etc/oauth/providers.ini` (`skel/pc/etc/oauth`) : `device_url =
   https://github.com/login/device/code`, `token_url = https://github.com/login/oauth/access_token`, portée `repo`
   (et `workflow` pour pousser des fichiers `.github/workflows`). `client_id` : une OAuth App de l'utilisateur avec
   « Enable Device Flow » coché (il n'est pas secret dans ce flux). Vérifier qu'oauthd lit la réponse de GitHub
   (`application/x-www-form-urlencoded` par défaut : demander `Accept: application/json`).
2. **`oauth token NOM`** dans `src/svc/oauthd/oauth.c` : écrit le jeton (`oauth_token`) sur la sortie, pour les
   scripts.
3. **`tools/git/git-credential-korin`**, `get` pour `host=github.com` sans secret au coffre : `oauth setup github
   provider=github` si le compte manque, `oauth login github` (messages sur la sortie d'erreur ou `/dev/tty` : la
   sortie est lue par git), puis rend `username=x-access-token` (ou le nom donné par `GET /user`) et
   `password=<jeton>`. `store` le garde ensuite au coffre comme aujourd'hui. `erase` (jeton refusé) fait aussi
   `oauth forget github`.
4. Les jetons d'une OAuth App de GitHub n'expirent pas : rien à rafraîchir.
5. Doc : `docs/GIT.md` (et l'aide de git dans Korin), puis le paquet git (version montée).

Le Pi n'a pas de coffre : le jeton y serait redemandé (sauf à le garder par `oauthd`, qui le chiffre déjà dans
`HOME:/oauth` : l'assistant peut lire `oauth token github` d'abord, ce qui marche aussi sans coffre).
