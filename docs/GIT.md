# git sur Korin

**git 2.51.0**, statique sur newlib + `libposix` ([POSIX.md](POSIX.md)), avec libcurl (HTTP et
HTTPS), OpenSSL et zlib. Du code C porté, construit sur le PC : **hors du selfhost**. Étape 7 de
[TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md) (§5.2) : `fork` et `exec` existent, `run-command` de git
marche donc comme sous Unix (commandes internes relancées, `git-remote-http`, crochets).

| Où | Quoi |
|---|---|
| `tools/git/build.sh` | construction (WSL) : zlib, OpenSSL, libcurl, puis git |
| `tools/git/config.mak` | les réglages de git (copiés dans ses sources) |
| `tools/git/patches/` | `git-0001-is-executable.patch`, `git-0002-volumes.patch`, `git-0003-local-transport.patch` ; `openssl-3.5.5.patch` |
| `tools/git/gitconfig` | la configuration du système (`/etc/gitconfig`) |
| `tools/git/serve.py` | serveur HTTP de dépôts pour les essais sous QEMU |
| `build/git/zlib`, `openssl`, `curl` | les bibliothèques statiques (`include/`, `lib/`) |
| `build/git/sdroot/` | l'arbre de la carte, recopié par `build.ps1` et `tools/ci/carte_linux.py` |
| `~/.cache/armpi-git` (WSL) | sources et arbres de construction, hors de git |
| `build/pc/git/`, `tools/pc/sysroot_git.sh` | le PC x86-64 (`ARCH=x86_64`) : « Sur le PC » ci-dessous |

## Construire

```sh
sh tools/posix/build.sh
sh tools/git/build.sh               # DEPS=<dossier des archives>, REBUILD=1 : tout refaire
```

Les sources sont des **archives locales** (`DEPS`, par défaut `/opt/src/git-deps`), sans
téléchargement : `git_2.51.0.orig.tar.xz`, `zlib_1.3.dfsg+really1.3.1.orig.tar.gz` (zlib 1.3.1),
`openssl_3.5.5.orig.tar.gz`, `curl_8.18.0.orig.tar.gz` (archives `.orig` d'Ubuntu : les sources
amont). Une construction complète prend un peu plus de 2 min sur quatre cœurs (OpenSSL en premier).

| Bibliothèque | Comment |
|---|---|
| zlib 1.3.1 | ses `.c` dans `libz.a` (comme `tools/webkit/ports/common.sh`) |
| OpenSSL 3.5.5 | `tools/openssl/build.sh` avec l'archive locale (`OPENSSL_TAR`, `OPENSSL_VER`, `OPENSSL_PATCH`) : même cible `armpi-aarch64`, mêmes options. `armpi.patch` est fait pour 3.5.9 ; `openssl-3.5.5.patch` est le même, une ligne de contexte change (`rand_unix.c` : `__wasi__` seul en 3.5.5). Le reste du système (pkg, curl de WebKit) garde 3.5.9 |
| libcurl 8.18.0 | `configure --host=aarch64-onyx-elf`, `libcurl.a` seule : HTTP/1.1 (pas de nghttp2 ici ; libcurl 8.16 de WebKit l'a), IPv4, résolveur dans un thread, OpenSSL, zlib ; ni brotli, zstd, psl, idn, ldap, ni les protocoles dont git ne se sert pas ; pas de `socketpair` (comme `tools/webkit/ports/curl`) ; certificats `/etc/ca-bundle` |
| git | `make` avec `config.mak` ; `uname_S=Korin` (rien de `config.mak.uname` de l'hôte) |

`config.mak` : `NO_ICONV`, `NO_GETTEXT`, `NO_TCLTK`, `NO_PERL`, `NO_PYTHON`, `NO_UNIX_SOCKETS`,
`NO_EXPAT` (donc pas de `git-http-push`, le push « WebDAV » ; le push HTTP intelligent marche),
`NO_IPV6`, `NO_MMAP` (git lit les paquets par `pread` dans des fenêtres), `NO_NSEC`, `NO_OPENSSL`
(git lui-même : SHA-1 avec détection de collision, SHA-256 interne ; OpenSSL ne sert qu'à libcurl),
`NO_REGEX` (celle de git), `NO_TRUSTABLE_FILEMODE`, `NO_SYMLINK_HEAD`, `FREAD_READS_DIRECTORIES`,
`SKIP_DASHED_BUILT_INS`, `DEFAULT_PAGER=cat`, `SHELL_PATH=/bin/sh`. Chemins fixes (pas de
`RUNTIME_PREFIX`).

### Les retouches de git

* **`git-0001-is-executable`** : `is_executable` (`run-command.c`) demande aussi `access (X_OK)` si
  `stat` ne donne pas de bit `x`. Sans cela git ne trouvait pas `/bin/git` dans `PATH` pour relancer
  ses commandes internes (`git maintenance`, `stash`, `repack`...) : tout fichier est `0644` sur FAT.
  Avec l'index des exécutables de vfs (§5.4 de TERMINAL-ET-FORK.md), `stat` donnera le bit : la
  retouche ne servira plus, sans gêner.
* **`git-0002-volumes`** : `compat/korin.h`, inclus par `git-compat-util.h` sous `__armpi__` : un
  chemin qui commence par un volume (« `/a` », « `/tmp` » : lettres, chiffres, `:`) est
  **absolu**, comme « `C:/a` » sous Windows (`has_dos_drive_prefix`, `skip_dos_drive_prefix`,
  `offset_1st_component`). Sans cela `git init` écrivait `core.worktree = /gt/r//gt/r`, et
  `git clone /a` prendrait « `SD0` » pour un hôte ssh. « `hote:/chemin` » (syntaxe scp) reste
  distant si l'hôte ne finit pas par un chiffre ; « `x://` » est une URL.
* **`git-0003-local-transport`** (`connect.c`) : un transport local (`git clone /a`, `push`
  vers un dépôt de la carte) lançait `git-upload-pack '<chemin>'` par `sh -c`. Il n'y a pas de
  commandes à tiret sur la carte (`SKIP_DASHED_BUILT_INS` : trois copies de 3,4 Mo évitées) et le
  `sh` de la carte est minimal : git lance donc lui-même `git upload-pack <chemin>`
  (`receive-pack`, `upload-archive` de même).

Côté `libposix` ([POSIX.md](POSIX.md)), git a demandé `flockfile` / `funlockfile` et `getgrnam`,
`initgroups` (`stdiolock.c`), et un `mkdtemp` qui crée le répertoire (`tempname.c` : la quarantaine
de `receive-pack`).

## Sur la carte

| Chemin | Quoi |
|---|---|
| `/bin/git` | le programme : toutes les commandes internes (3,4 Mo) |
| `/bin/git-core/` | `gitexecdir` : `git-remote-http` et `git-remote-https` (le même programme, 5,7 Mo chacun : pas de liens sur FAT), les scripts (`git-submodule`, `git-mergetool`... : il leur faut un `sh` POSIX, bash à l'étape 5) |
| `/etc/gitconfig` | la configuration du système |
| `/etc/git-core/templates/` | le modèle de `git init` (sans les exemples de crochets) |

En tout ≈ 15 Mo. git est vu comme `/bin/git`, ses aides comme `/bin/git-core/...` et sa
configuration comme `/etc/gitconfig` : les chemins virtuels de `/etc/paths.txt` (`/bin`, `/etc`)
existent déjà, rien n'y est ajouté. `git-http-fetch` (5,7 Mo) n'est pas copié : `git-remote-http`
fait aussi le HTTP « simple ».

Sur la carte par : `build.ps1` et `tools/ci/carte_linux.py` (copie de `build/git/sdroot` s'il
existe) ; le paquet **`git`** de `tools/pkg/packages.ini` (avant `[armpi]`, qui prend `bin/` et
`etc/` ; `etc/gitconfig` y est un fichier de configuration : pas écrasé s'il a changé).

## `/etc/gitconfig`

FAT32 et exFAT n'ont ni bit d'exécution, ni liens, ignorent la casse et n'ont qu'une date à 2 s
près ([FICHIERS.md](FICHIERS.md)) :

| Clé | Valeur | Pourquoi |
|---|---|---|
| `core.filemode` | `false` | pas de bit `x` (git init l'écrit aussi : `NO_TRUSTABLE_FILEMODE`) |
| `core.symlinks` | `false` | pas de liens (`symlink` → `ENOSYS`) |
| `core.ignorecase` | `true` | FAT32 et exFAT (git init le vérifie aussi) |
| `core.checkStat` | `minimal` | ni inode, ni uid, ni ctime fiables |
| `core.trustctime` | `false` | une seule date |
| `core.pager` | `cat` | pas de pageur (`less` n'est pas sur la carte) |
| `core.packedGitLimit`, `packedGitWindowSize` | 64m, 4m | 64 fichiers ouverts par process ; mémoire |
| `core.preloadIndex`, `fsmonitor`, `untrackedCache` | `false` | ni threads de `lstat` utiles, ni démon |
| `gc.cruftPacks` | `false` | le nom temporaire du `.mtimes` d'un paquet « cruft » passe 63 octets (RamFS) |
| `init.defaultBranch` | `main` | |
| `http.sslCAInfo` | `/etc/ca-bundle` | les autorités de Mozilla, comme curl et Jet |
| `safe.directory` | `*` | une seule identité (uid 0) |

L'éditeur n'est pas réglé : `git commit` sans `-m` cherche `vi`. Avec bash (étape 5),
`core.editor` pourra désigner un éditeur de la console.

## Ce qui marche (QEMU, 10/11/2026)

Essais par un script du `sh` de la carte lancé de `startup.txt`, sur chaque volume :

| Essai | / (FAT32) | /media/sd1 (exFAT) | /tmp (RamFS) |
|---|---|---|---|
| `config --global` (`HOME` = `/` : `//.gitconfig`) ; `clone` local d'un volume à l'autre (`/` → `/media/sd1`, `clone --bare` vers `/tmp`), `push` vers un dépôt de la carte | oui | oui | oui |
| `--version`, `init`, `config`, `add`, `commit`, `log`, `status`, `diff`, `branch`, `switch`, `checkout -b`, `merge` (avance rapide et vraie fusion « ort »), `tag` (léger et annoté), `stash` / `pop`, `rm`, `mv`, `.gitignore`, `reset --hard`, `gc`, `count-objects`, `fsck`, `log --graph` | oui | oui | oui |
| `clone` HTTP simple (fichiers), `pull`, `fetch` | oui | oui | oui |
| `clone` HTTP intelligent, `push` (branche, nouvelle branche, tag annoté) | oui | oui | oui |
| dépôt de 400 fichiers (20 dossiers, 5 versions, 2 133 objets) : `clone`, `status`, `diff --stat`, `checkout`, `add -A`, `commit`, `push`, `gc`, `fsck` | oui | oui | non : RamFS n'a que 256 nœuds |
| `git ls-remote https://...` : refus de connexion, nom inconnu, serveur qui ne parle pas TLS | erreur propre (`fatal: unable to access...`, code 128) | | |

Durées sous QEMU (SD émulée, lente) : une quarantaine de commandes en ≈ 50 s sur /, ≈ 55 s sur
/media/sd1, ≈ 20 s sur /tmp ; `clone` du dépôt de 400 fichiers ≈ 65 s (/), ≈ 80 s (/media/sd1) ; `git
status` sur ses 400 fichiers ≈ 3 à 4 s. HTTP : `tools/git/serve.py` sur le PC, vu de QEMU comme
`http://10.0.2.2:<port>/` (réseau « user », sans redirection). HTTPS vers l'extérieur n'a pas pu
être essayé ici (pas d'accès) : la poignée de main TLS part bien (un serveur HTTP répond
`wrong version number`).

## Sur le PC (Korin x86-64)

Le même git (mêmes sources, mêmes retouches, même `config.mak`, même `gitconfig`), construit pour
Korin PC par la chaîne `x86_64-onyx-elf` sur la libposix du PC ; rien ne change pour le Pi.

```sh
ARCH=x86_64 sh tools/posix/build.sh
ARCH=x86_64 sh tools/git/build.sh      # build/pc/git (zlib, curl, sdroot/), ~/.cache/korin-git-x86_64
ARCH=x86_64 sh tools/rg/build.sh       # build/pc/rg/rg (docs/RG.md)
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_git.sh [arbre]
```

* `tools/git/build.sh` : `ARCH=x86_64` choisit la chaîne, `build/pc/posix/lib/posix.specs`, pas de
  `-mcpu=cortex-a72`, `uname_M=x86_64`, et **l'OpenSSL du PC** (`build/pc/thirdparty/openssl`, 3.5.9,
  cible `korin-x86_64` d'`armpi.patch` : celui des apps et de Jet ; `ARCH=x86_64 sh tools/openssl/build.sh`
  est lancé s'il manque) ; zlib et libcurl 8.18.0 sont refaits comme pour le Pi. `config.mak` reçoit la
  chaîne, les options du processeur et le dossier d'OpenSSL du script (`@TC@`, `@CPU@`, `@SSL@`) : pour le
  Pi, le fichier produit est le même qu'avant. Les archives sont aussi cherchées dans
  `~/.cache/korin-src/git-deps`. Environ 4 min sur six travaux.
* `tools/pc/sysroot_git.sh` pose dans l'arbre du PC `bin/git`, `bin/git-core/`, `etc/gitconfig`,
  `etc/git-core/templates/`, `etc/ca-bundle` (s'il manque : `http.sslCAInfo`, les autorités de Mozilla de
  `skel/sd0/etc`) et `bin/rg` ; à relancer après chaque `sysroot.sh` (qui refait l'arbre). Il refuse un
  programme qui n'est pas x86-64.
* Taille : `bin/git` 4,1 Mo, `git-remote-http` et `git-remote-https` 7,2 Mo chacun, en tout **≈ 18,5 Mo**
  avec `rg` (0,85 Mo).

**Vérifié (QEMU q35 + OVMF, 4 cœurs, TCG, NVMe, `mkkorindisk.py --layout zbook --korin-mb 400`, copie de
`build/pc/sysroot` sans `gcc/` ni Jet, bureau, script bash lancé par `startup.txt`, `usb-net` en réseau
« user », 13/11)** : **80 essais sur 80** (avec le protocole 0 : ci-dessous) dans /tmp et /media/sd1 (FAT32 sur le PC : `/home` de KORIN OS) :
`--version`, `config --global`, `init`, `add -A`, `.gitignore`, `status`, `commit`, `log`, `diff`,
`branch`, `switch`, fusion « ort », `log --graph --all`, `tag -a`, `show --stat`, `stash` / `pop`,
`checkout --`, `mv`, `rm`, `gc`, `fsck`, `count-objects` (≈ 20 s par volume) ; `clone` local /media/sd1 →
/tmp ; HTTP de `tools/git/serve.py` (lancé dans WSL, vu comme `http://10.0.2.2:<port>`) : `clone`
intelligent (2 s), `clone` simple (`/dumb/`), `push` d'une branche existante et d'une nouvelle, `fetch`,
`pull` ; **HTTPS** : `git clone --depth 1 https://github.com/octocat/Hello-World.git` en 3 s (certificat
vérifié par `etc/ca-bundle`) ; un serveur HTTP joint en `https://` : `wrong version number`, code 128.

**Protocole 2 sur le PC (corrigé le 14/11)** : un `clone` (ou `fetch`) HTTP en protocole 2 s'arrêtait sur le
PC (5 fois sur 5) : `git clone` fork `index-pack` pendant que son thread démultiplexeur (`start_async`) attend
un verrou de libposix ; le noyau du PC prenait comme clé d'un futex l'adresse physique du mot, que la copie
sur écriture d'après le fork change : le thread n'était plus réveillé. Clé (espace d'adresses, adresse
virtuelle) depuis le 14/11, comme le Pi ; le contournement `protocol.version = 0` est retiré de
`tools/pc/sysroot_git.sh` (docs/etude/PC-PILOTES.md, « Réveils perdus »).

## GitHub

*21/11/2026, [etude/GH.md](etude/GH.md) §5. Pas de `gh` (Go) : l'identification est dans l'assistant de git.*

Au premier `git push` (ou `pull`, `fetch`, `clone` d'un dépôt privé) vers `https://github.com`, sans jeton au
coffre, `git-credential-korin` connecte le compte OAuth « `github` » d'oauthd ([OAUTH.md](OAUTH.md)) par le
**code d'appareil**, à la façon du Git Credential Manager :

```
$ git push
GitHub : ouvrir ses pages dans Jet, sur cet écran ? [O/n]
GitHub : connexion de Korin à votre compte.
Sur un téléphone ou un ordinateur, ouvrez :
  https://github.com/login/device
et tapez le code :  WDJB-MJHT
En attente (900 s au plus)...
github : connecté.
```

1. **Le client** : une « OAuth App » de GitHub avec « Enable Device Flow » coché. Son Client ID n'est pas secret
   dans ce flux (pas de `client_secret`). Korin livre le sien depuis `pc` 2026.11.78 (l'OAuth App « Korin », 21/11 ;
   `[github]` de `/etc/oauth/providers.ini`, jetons qui n'expirent pas) : rien à créer, seulement le code à taper.
   Sans Client ID livré (une section `[github]` sans `client_id`), l'assistant fait créer une fois celle de
   l'utilisateur : il affiche les valeurs à mettre
   (nom « Korin », `http://127.0.0.1` pour la page d'accueil et le retour, Device Flow coché), ouvre
   `https://github.com/settings/applications/new` dans Jet si on l'a voulu, demande le Client ID sur le terminal,
   puis `oauth setup github provider=github client_id=… scope='repo workflow'` (dans le compte de l'utilisateur).
2. **La connexion** : `oauth login github`, messages sur la sortie d'erreur (git lit la sortie de l'assistant),
   question sur `/dev/tty`. Portée `repo` (dépôts privés compris) et `workflow` (pousser `.github/workflows`).
3. **Le jeton** est rendu à git avec le nom `x-access-token` (ou le nom de l'adresse, `https://alice@github.com/…` ;
   GitHub ne regarde que le jeton) ; git le fait garder au coffre (`store`) comme un mot de passe. Le jeton d'une
   OAuth App **ne périme pas** : oauthd le garde aussi, chiffré (coffre, ou `HOME:/oauth/secrets`), et le rend
   sans réseau (`oauth token github`) : sans coffre, rien n'est redemandé.
4. **Refusé par GitHub** (révoqué, application retirée) : git appelle `erase`, qui l'enlève du coffre et, si c'est
   le jeton d'oauthd, fait `oauth forget github` : le `push` suivant reconnecte (sans redemander le Client ID).
5. `GIT_TERMINAL_PROMPT=0` (scripts) : seulement un jeton déjà là, aucune connexion.

À la main : `oauth status github`, `oauth token github`, `oauth forget github` (se déconnecter ; le jeton reste
valable chez GitHub jusqu'à sa révocation dans *Settings › Applications*), `oauth remove github` (oublier aussi le
Client ID).

Essai sous Linux : `bash tools/git/test/credential.sh` (le vrai git, `oauth` et `secret` simulés ; 29
vérifications) ; oauthd : `sh src/svc/oauthd/test/run.sh` (fournisseur `github` contre `tools/nettest/oauthsrv.py`,
`/gh/…`).

**Vérifié dans Korin PC sous QEMU (21/11)**, contre le faux GitHub de `oauthsrv.py` (`device_url`, `token_url` du
compte vers `http://10.0.2.2:8090/gh/…`) : `git credential fill` lance la connexion (code affiché, accordé en 5 s,
`slow_down` respecté) et rend `x-access-token` et le jeton ; `approve` le garde au coffre ; le `fill` suivant le
rend sans réseau ; oauthd tué puis relancé par NetKit rend le même jeton (durable, au coffre) ; `reject` fait
`oauth forget github` ; `GIT_TERMINAL_PROMPT=0` ne connecte pas. **Pas encore** contre le vrai GitHub (le réseau du
cloud l'intercepte) ; **connexion réelle validée par l'utilisateur le 21/11** (client « Korin », pc 2026.11.78). Le Pi : oauthd n'y est pas encore construit, l'assistant y
fait comme avant.

## Limites

* **Pas de `mmap` en écriture partagée** : `NO_MMAP`, git lit par `pread`. Le `mmap` en lecture de
  libposix suffirait peut-être (à mesurer sur le Pi).
* **Renommer n'est pas atomique** en cas de coupure (FAT : [FICHIERS.md](FICHIERS.md)) : un
  `index.lock` ou un `*.lock` de référence peut rester après une coupure de courant ; git dit
  alors de l'effacer.
* **Chemins** ≈ 254 octets (« `/` » compris) ; noms de 63 octets sur /tmp (RamFS, 256 nœuds en
  tout : un dépôt de quelques centaines de fichiers n'y tient pas).
* **64 fichiers ouverts** par process : `core.packedGitLimit` modeste ; un dépôt avec beaucoup de
  paquets se `git gc` (un seul paquet).
* Les scripts de `git-core` (`git submodule`, `mergetool`, `filter-branch`, `request-pull`...) et
  les crochets en shell attendent un `sh` POSIX : bash (étape 5). Les alias `!commande` passent par
  `/bin/sh` (le `sh` de la carte : commandes simples).
* Pas de pageur, pas d'éditeur par défaut, pas de traductions (`NO_GETTEXT`), pas de `git gui`,
  `gitk`, `git send-email`, `git svn`, `git p4` (Tcl, Perl, Python), ni de `git daemon` sur un
  socket Unix ; `credential-cache` (socket Unix) non plus (`credential-store` : pas essayé).
* **Mots de passe et jetons** (https) : `/etc/gitconfig` règle `credential.helper = korin`, l'assistant
  `/bin/git-credential-korin` (`tools/git/git-credential-korin`, un script bash) : `get`, `store`, `erase` sur la
  commande `secret`, donc dans le coffre à secrets (docs/KEYVAULT.md), dans l'espace du script seul :
  `git/<hôte>/<compte>` (le mot de passe), `git/<hôte>/@default` (le dernier compte de l'hôte). Rien en clair,
  au contraire de `credential-store` (`~/.git-credentials`). Sans coffre (le Pi) : rien n'est gardé, git redemande.
  GitHub : connexion par code d'appareil, ci-dessous.
* HTTP/1.1 seulement (libcurl sans nghttp2).
* La barre de progression ne s'actualise pas (pas de SIGALRM : `setitimer` sans effet, étape 5).
* `relative_path` de git compare la première lettre des volumes : un chemin relatif d'un volume à
  l'autre (`/` → `/media/sd1`) peut être faux à l'affichage.

## À faire

* Essayer HTTPS vers GitHub sur le vrai Pi (certificats, vitesse de la poignée de main) ;
* mesurer sur le Pi : `clone` d'un vrai dépôt (des milliers de fichiers), `status`, `log -p` ;
* `core.editor` et les crochets quand bash sera là ; retirer `git-0001` quand `stat` donnera le bit
  `x` ;
* SIGALRM (étape 5) : la progression ; `less` comme pageur s'il arrive.
