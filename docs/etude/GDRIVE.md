# Étude : Google Drive comme volume de Korin (`gdrivefs`)

*16/11/2026. État : OAuth fait (oauthd, [OAUTH.md](../OAUTH.md)), connexion Google réelle validée ; étape 1 faite le
16/11 (gdrivefs en lecture seule, [GDRIVE.md](../GDRIVE.md)), pas encore essayée sur le vrai Drive.*

## 1. Le but

Monter le Google Drive de l'utilisateur dans l'arbre, comme un partage SMB :

```sh
mount /net/net1 gdrive:perso gdrivefs
ls /net/net1/Photos
cat /net/net1/Musique/air.mp3 | play -        # lu en flux, jamais copié en entier
```

« En flux » veut dire : rien n'est synchronisé sur la carte. Une lecture va chercher les octets demandés (et un peu
plus loin) sur le réseau ; seuls des blocs récents restent en mémoire. C'est le modèle de rclone `mount`, pas
celui de l'application Google Drive pour ordinateur (qui synchronise).

Toutes les apps en profitent sans changement : Fichiers, les lecteurs de média, Jet, l'éditeur, le shell.

## 2. Ce qui existe déjà

| Brique | Où | Rôle ici |
|---|---|---|
| **smbfs** | `src/svc/smbfs/smbfs.c` ([SMB.md](../SMB.md)) | le modèle : un service de fichiers en C sur libposix (protocole de `src/inc/fs.inc`), lancé par vfs au montage, monté sur `/net/netN`, remonté au démarrage par `/etc/mounts.txt`, comptes dans `/etc/smbfs.ini`, connexion au premier accès |
| **OpenSSL** | `tools/openssl/build.sh` ([OPENSSL.md](../OPENSSL.md)) | TLS 1.3, certificats vérifiés par `/etc/ca-bundle` |
| **libcurl** | `libcurl.a` construite par `tools/git/build.sh` (curl 8.18, HTTP/1.1) | les requêtes HTTPS, `Range`, envoi par morceaux |
| **Fichiers** | Volume › Se connecter à un serveur… | le dialogue, le genre « Serveur », l'éjection, la ligne de `mounts.txt` |
| **vfs** | `src/svc/vfs` | `netN = source, service` monte sur `/net/netN` |

Il manque : un analyseur JSON (cJSON, un seul fichier C, MIT : à ajouter dans `tools/`), et l'OAuth.

## 3. L'API Drive v3 en face du protocole de fichiers

Drive ne connaît pas de chemins : chaque fichier a un **identifiant** et une liste de **parents**. Le service
tient donc une table chemin → identifiant, remplie au fil des `readdir` et des `stat`.

| Opération Korin (`fs.inc`) | Requête Drive |
|---|---|
| `FS_READDIR` | `GET /drive/v3/files?q='<id>' in parents and trashed=false&fields=nextPageToken,files(id,name,mimeType,size,modifiedTime)&pageSize=1000` (pages suivies) |
| `FS_STAT` | la table (remplie par le `readdir` du dossier parent) ; sinon `files.list` sur le parent avec `name='…'` |
| `FS_OPEN` (lecture) | rien sur le réseau : identifiant et taille sont déjà connus |
| `FS_READ`, `FS_READ_DIRECT`, `FS_SEEK` | `GET /drive/v3/files/<id>?alt=media` avec `Range: bytes=a-b`, par blocs (section 4) |
| `FS_OPEN` (écriture) + `FS_WRITE` + `FS_CLOSE` | écrit dans un fichier tampon de `/tmp`, puis l'envoie à la fermeture : envoi « resumable » (`POST /upload/drive/v3/files?uploadType=resumable`, puis des `PUT` de 8 Mo avec `Content-Range`) ; `PATCH` à la place du `POST` pour remplacer un fichier qui existe |
| `FS_MKDIR` | `POST /drive/v3/files`, `mimeType = application/vnd.google-apps.folder` |
| `FS_UNLINK` | `PATCH … {trashed: true}` (la corbeille de Drive, pas d'effacement définitif) |
| `FS_RENAME` | `PATCH /drive/v3/files/<id>?addParents=…&removeParents=…` avec `{name}` |
| `FS_UTIME` | `PATCH … {modifiedTime}` |
| `FS_INFO` | `GET /drive/v3/about?fields=storageQuota,user` (taille, place libre, étiquette = l'adresse du compte) |

Drive n'écrit jamais au milieu d'un fichier : un fichier ouvert en écriture est d'abord téléchargé en entier dans
le tampon s'il n'est pas tronqué (`O_TRUNC`), puis renvoyé en entier. C'est acceptable pour des documents, pas
pour une base de données. Le dire dans la doc.

## 4. Le flux : un cache de blocs

Chaque `FS_READ` ne peut pas devenir une requête HTTPS : un lecteur MP3 lit par 4 Ko, et un aller-retour vers
Google coûte de 30 à 100 ms.

- **Blocs de 1 Mo**, alignés. Une lecture qui tombe dans un bloc absent le télécharge (`Range` d'1 Mo).
- **Lecture anticipée** : deux lectures qui se suivent font demander les blocs suivants, jusqu'à 4 Mo d'avance.
  Un `FS_SEEK` loin annule l'avance.
- **Cache LRU** en mémoire : 32 Mo par défaut (`cache = 32M` dans `drives.ini`), partagé par tous les fichiers ouverts.
  Pas de cache sur la carte au départ ; un cache disque (`/tmp` ou `HOME:/.cache/gdrive`) viendra plus tard s'il
  le faut.
- **Validité** : un bloc est lié à l'identifiant **et** au `modifiedTime` (ou `md5Checksum`) vu à l'ouverture. Un
  fichier changé ailleurs n'est pas mélangé.
- **Une connexion gardée ouverte** (keep-alive de curl, un seul `CURL*` réutilisé) : sans elle, chaque bloc coûte
  une poignée de main TLS.

Débit attendu : celui du réseau, moins un aller-retour par Mo. Une vidéo de 5 Mbit/s demande 0,6 Mo/s, ce qui
passe largement si le reste de la machine suit (à mesurer : le JIT de Jet et le Wi-Fi du Pi).

### Un seul thread

smbfs sert une requête à la fois. Pour Drive, c'est un vrai problème : pendant qu'un bloc d'1 Mo arrive (une demi-
seconde), Fichiers qui liste un dossier du même volume attend. Deux choix :

1. **D'abord** : un seul thread, comme smbfs, et les métadonnées en cache (section 5) pour que la plupart des
   `stat` et `readdir` ne touchent pas le réseau. Suffisant pour une première version.
2. **Ensuite** : l'interface « multi » de curl (`curl_multi_*`) dans la boucle du service : plusieurs transferts
   en cours, une réponse à `SYS_SVC_REPLY` quand le sien finit. Il faut vérifier que la boucle du service peut
   attendre à la fois un message (`SYS_SVC_NEXT`) et des sockets (`poll`) : sinon un thread pour le réseau et une
   file entre les deux.

## 5. Les métadonnées

- **Cache des dossiers** : la liste d'un dossier reste valable 60 s (réglable). Les `stat` qui suivent un
  `readdir` (ce que fait toujours Fichiers) sont servis par la table.
- **Changements faits ailleurs** : `changes.getStartPageToken` au montage, puis `changes.list` au plus une fois par
  30 s à la première requête qui suit ; les entrées touchées sont invalidées. Bien moins de requêtes que de
  recharger les dossiers.
- Ce qu'on modifie soi-même met la table à jour tout de suite.

## 6. Ce qui ne ressemble pas à un système de fichiers

| Cas | Choix |
|---|---|
| Deux fichiers du même nom dans un dossier | le second et les suivants prennent un suffixe : `rapport (2).pdf`, dans l'ordre de création ; stable tant que rien ne change |
| Un fichier avec plusieurs parents (ancien Drive) | visible dans chaque dossier ; effacer le retire seulement de ce parent (`removeParents`) |
| **Google Docs, Sheets, Slides, Drawings** (pas de contenu binaire) | montrés comme des exports, en lecture seule : `Rapport.gdoc` → `Rapport.docx`, Sheets → `.xlsx`, Slides → `.pptx`, Drawings → `.png` ; `files.export` (10 Mo au plus) ; taille inconnue avant l'export : `stat` rend 0, puis la vraie taille une fois l'export en cache. Réglable : `export = docx` ou `odt`, `pdf` |
| Raccourcis (`application/vnd.google-apps.shortcut`) | suivis vers la cible (pas de liens symboliques dans Korin) |
| Formulaires, sites, cartes | cachés |
| Caractères interdits par Korin (`/` dans un nom) | remplacé par `∕` (U+2215), et l'inverse à l'écriture |
| « Partagés avec moi », Drives partagés | plus tard : un dossier virtuel `.shared/` et `drives/<nom>/` (`corpora=allDrives`) |
| Corbeille | invisible ; `rm` met à la corbeille, jamais d'effacement définitif depuis Korin |

## 7. L'authentification : OAuth 2.0

C'est la partie difficile, pour des raisons de règles plus que de code.

### 7.1 L'identifiant de l'application

Il faut un projet Google Cloud, l'API Drive activée et un **client OAuth**. Le `client_id` (et le
« secret », qui n'en est pas un pour une app installée) va dans le paquet. Tant que l'app n'est pas vérifiée par
Google, l'écran d'accord affiche « Google n'a pas validé cette application » et le projet est limité à 100
utilisateurs de test. Pour un usage personnel, ça suffit. Une publication large demanderait la vérification de
Google, et la permission `drive` (tout le Drive) est « restreinte » : audit de sécurité payant. Hors de portée.

**Décision (16/11, revue le même jour)** : **comme Mail, un identifiant livré**. L'utilisateur n'a qu'à se
connecter avec son compte Google ; l'application « Korin » est créée une fois par l'éditeur de Korin, et son
`client_id` et son `client_secret` sont livrés dans `/etc/oauth/providers.ini` (paquet `pc`), que lit oauthd
([OAUTH.md](../OAUTH.md), « Les fournisseurs livrés avec Korin »). Un utilisateur peut toujours donner le sien
(`oauth setup … client_id=…`). Limite tant que l'application n'est pas vérifiée : 100 utilisateurs et l'écran
« application non validée ». (Première décision du matin : chaque utilisateur crée le sien ; abandonnée parce que
trop lourde pour l'utilisateur.)

Ce que l'éditeur fait une fois, sur la console Google Cloud (logo : `assets/korin-oauth-120.png`) :

1. créer un projet et y activer l'**API Google Drive** ;
2. écran d'accord : type « Externe », son adresse comme utilisateur de test, la permission
   `https://www.googleapis.com/auth/drive` ;
3. **passer l'application « En production »** sans demander la vérification. En mode « Test », Google fait expirer
   le jeton de rafraîchissement **au bout de 7 jours** : il faudrait se reconnecter chaque semaine. En production non
   vérifiée, le jeton dure, au prix de l'écran « application non validée » à la première connexion (à confirmer au
   moment du port, c'est ce que décrit la documentation de rclone) ;
4. créer un identifiant OAuth de type **« Application de bureau »** et mettre le `client_id` et le
   `client_secret` dans la section `[google]` de `skel/pc/etc/oauth/providers.ini`. Pour une application de
   bureau, Google demande le secret, mais celui-ci ne protège rien.

### 7.2 Le flux pour se connecter

| Flux | Permissions possibles | Ce qu'il faut dans Korin |
|---|---|---|
| **Appareil** (« Sur votre téléphone, allez sur google.com/device et tapez ABCD-EFGH ») | **pas `drive`** : seulement `drive.file` (les fichiers créés ou ouverts par l'app) et `drive.appdata`, d'après la liste de Google | rien de plus : deux `POST` et une attente |
| **Navigateur avec redirection locale** (`http://127.0.0.1:<port>`) | `drive`, `drive.readonly` | un navigateur dans Korin (Jet, ou une fenêtre WebKit) et un petit serveur HTTP local d'un coup ; ou un navigateur **sur un autre appareil** et un code collé à la main |

Le flux appareil est le plus simple mais ne donne pas ce qu'on veut (voir tout son Drive). Donc :

1. **Version 1** : le flux par navigateur, fait par **oauthd** et les fonctions `oauth_*` de NetKit
   ([OAUTH.md](../OAUTH.md), 16/11). `gdrivefs` ne lance rien lui-même (c'est un service). C'est Fichiers
   (Volume › Se connecter à Google Drive…) qui appelle `oauth_setup` (compte `gdrive/<nom>`, `provider = google`),
   puis `oauth_browser_begin` (PKCE, retour sur `http://127.0.0.1:<port>/` écouté par oauthd), ouvre l'adresse
   dans Jet, attend `oauth_browser_poll`, et monte le volume.
   Sans navigateur dans Korin (le Pi sans Jet) : l'URL affichée, ouverte sur un autre appareil ; la page de
   redirection vers `127.0.0.1` échoue là-bas mais montre le `code=` dans la barre d'adresse, que l'on colle dans
   le dialogue (`oauth_browser_finish`). Pas élégant, mais rclone fait pareil.
2. Le **jeton d'accès** (valable 1 h) n'est jamais écrit : `gdrivefs` appelle `oauth_token` au premier accès, puis
   quand une requête rend 401 ; oauthd le garde en mémoire et le rafraîchit.

Le flux appareil reste utile pour un mode « dossier de l'app » (`drive.file`) : un dossier « Korin » dans le Drive,
pour des sauvegardes. À garder pour plus tard.

### 7.3 Où ranger le jeton

**Fait par oauthd (16/11, [OAUTH.md](../OAUTH.md))** : sur la demande de l'utilisateur, le code OAuth commun est
devenu un **service du système** et non une copie dans chaque programme. Le compte Google (`client_id`,
`client_secret`, `scope`) est dans `HOME:/oauth/accounts.ini`, les jetons de rafraîchissement chiffrés en
AES-256-GCM dans `HOME:/oauth/secrets` (clé `HOME:/oauth/key`) ; gdrivefs ne les voit jamais, il demande un jeton
d'accès à `oauth_token`. Le même compte sert à tout programme (Fichiers, gdrivefs, un script par `oauth token`).

Il reste à gdrivefs ses propres réglages, sans aucun secret, dans le dossier personnel (`HOME:/gdrive/`, aujourd'hui
`/home/gdrive`) : quand les comptes arriveront ([MULTI-UTILISATEUR.md](MULTI-UTILISATEUR.md)), chaque utilisateur
aura son Drive sans rien changer.

```ini
; HOME:/gdrive/drives.ini (écrit par Fichiers)
[perso]
oauth = gdrive/perso   ; le compte d'oauthd
root = root            ; ou l'identifiant d'un dossier pour monter seulement lui
cache = 32M
export = docx
```

Comme chez Mail, la clé est sur la même carte que les secrets : le chiffrement empêche de lire un jeton en ouvrant
le fichier ou en le copiant seul, pas de le prendre avec la carte entière. Le jeton donne tout le Drive : le dire
dans le dialogue. Un trousseau du système (et une clé liée au mot de passe de session) viendra avec les comptes.

`mounts.txt` : `net1 = gdrive:perso, gdrivefs`. La source tient dans les 23 caractères de la table de vfs.
gdrivefs lit `HOME:/gdrive/` et demande son jeton au premier accès, jamais pendant le montage (vfs attend alors le
service), comme smbfs avec `smbfs.ini`.

## 8. Les limites de Google

- **Quotas de l'API** : environ 12 000 requêtes par minute et par projet, bien plus qu'il n'en faut. Un 403
  `rateLimitExceeded` ou un 429 : nouvel essai après un délai qui double (1, 2, 4… s).
- **Téléchargements** : certains fichiers très partagés sont bloqués par Google (`downloadQuotaExceeded`) : E_IO.
- **Fichiers soupçonnés de virus** : `acknowledgeAbuse=true` est nécessaire, on ne le passe pas.
- **Hors ligne** : le volume reste monté ; chaque requête rend E_IO tant que le réseau manque. Les blocs en cache
  restent lisibles.

## 9. Le découpage

| Étape | Contenu | Taille estimée |
|---|---|---|
| 0 | cJSON dans `tools/`, `libcurl.a` construite aussi pour l'x86-64 et l'AArch64 (aujourd'hui dans `tools/git/build.sh` : la sortir dans `tools/curl/build.sh`) | petit |
| 1 | `src/svc/gdrivefs` en lecture seule : jeton depuis l'ini (obtenu à la main avec un script sur le PC, `tools/gdrive/auth.py`), `readdir`, `stat`, lecture par blocs avec cache, `FS_INFO` | ~ smbfs (1000 lignes) |
| 2 | Fichiers › Se connecter à Google Drive… : premier écran pour `client_id` et `client_secret` (avec le lien vers la page d'Aide qui explique la console Google), puis flux par navigateur et PKCE, code collé en secours, par les fonctions `oauth_*` de NetKit (faites : [OAUTH.md](../OAUTH.md)) | petit |
| 3 | Écriture : tampon dans `/tmp`, envoi « resumable », `mkdir`, `rename`, corbeille | moyen |
| 4 | Exports des Google Docs, raccourcis, doublons de noms | petit |
| 5 | `changes.list`, interface multi de curl (plusieurs requêtes à la fois) | moyen |
| Plus tard | cache sur disque, Drives partagés, « Partagés avec moi », mode `drive.file` par le flux appareil | |

## 10. Essais

- **Sous Linux d'abord** : le cœur (table des chemins, cache de blocs, JSON) compilé dans WSL contre un faux
  serveur Drive en Python (`tools/gdrive/fakedrive.py` : `files.list`, `alt=media` avec `Range`, envoi
  « resumable », erreurs 401/403/429 à la demande). Pas de vrai compte Google dans les essais automatiques.
- **QEMU** (PC) : montage sur le faux serveur par `10.0.2.2`, puis sur le vrai Drive de l'utilisateur à la main.
- **Mesures** : débit d'un fichier de 100 Mo (`cp` vers `/tmp`), lecture d'un MP3 et d'une vidéo dans le lecteur,
  temps d'affichage d'un dossier de 1000 fichiers dans Fichiers.

## 11. Questions ouvertes

1. ~~Un `client_id` livré dans le paquet ?~~ Non : celui de l'utilisateur (16/11, section 7.1).
2. La boucle des services peut-elle attendre des sockets en plus des messages (section 4) ? À lire dans
   `src/svc/svclib` et la boucle de smbfs.
3. Faut-il un mécanisme commun aux volumes réseau (cache de blocs, cache de dossiers) dans un petit `libnetfs`,
   pour que smbfs en profite aussi (SMB.md le demande déjà : « un cache des listes de dossier ») ? Et plus tard
   d'autres services (OneDrive, Dropbox, WebDAV) : leurs API sont proches de celle de Drive.
