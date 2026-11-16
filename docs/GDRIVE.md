# Google Drive : gdrivefs

*16/11/2026. Fait : les étapes 1 et 2 de l'étude ([etude/GDRIVE.md](etude/GDRIVE.md)) : un volume lu en flux et
écrit, et Fichiers › Volume › Se connecter à Google Drive…. La lecture et l'écriture sont essayées sous Linux et
dans Korin PC sous QEMU (faux serveur), et **validées sur le ZBook contre le vrai Google Drive** (pc 2026.11.21).
Reste à revoir : le dialogue de Fichiers (lent, le Drive a dû être monté à la main).*

Le Drive de l'utilisateur monté dans l'arbre, comme un partage SMB. Dans **Fichiers** : **Volume › Se connecter à
Google Drive…**, un nom (`perso`), puis la page de Google dans Jet ; le Drive devient `/net/netN` et est remonté à
chaque démarrage (`/etc/mounts.txt`). Depuis le shell :

```sh
oauth setup gdrive/perso provider=google        # une fois : le client de Korin (docs/OAUTH.md)
oauth browser gdrive/perso                      # une fois : se connecter à son compte Google
mount /net/net1 gdrive:perso gdrivefs
ls /net/net1
cp /net/net1/Photos/vacances.jpg /tmp/
cp rapport.pdf /net/net1/Documents/             # envoyé à Google à la fermeture
df /net/net1                                    # « Google Drive (adresse) », quota et place libre
```

`gdrive:NOM` prend le compte OAuth `gdrive/NOM` d'oauthd ; gdrivefs ne voit jamais que des jetons d'accès
(`oauth_token`). Rien n'est lu ni demandé au réseau pendant le montage (vfs attend alors le service) : tout se fait à
la première requête.

## Réglages (facultatifs)

`HOME:/gdrive/drives.ini`, une section par nom :

| Clé | Rôle |
|---|---|
| `oauth` | un autre compte d'oauthd que `gdrive/NOM` |
| `root` | l'identifiant d'un dossier à monter seul (défaut `root` : tout le Drive) |
| `cache` | la taille du cache des blocs (défaut `64M`, 8 Mo au moins ; l'avance va jusqu'à la moitié) |
| `ttl` | la durée de vie d'une liste de dossier, en secondes (défaut 60) |
| `readonly` | `1` : en lecture seule (écrire, créer, effacer… : `E_ACCES`) |
| `spool` | le dossier du tampon d'envoi (défaut `HOME:/gdrive/spool`) |
| `api_url` | l'adresse de l'API (défaut `https://www.googleapis.com`) ; en clair seulement vers une adresse privée : un faux serveur d'essai |

## Ce qu'on voit

- Les dossiers et les fichiers de Drive (`files.list`, pages suivies). La corbeille est cachée.
- **Cachés pour l'instant** : les Google Docs, Sheets, Slides, formulaires, et les raccourcis (pas de contenu
  binaire ; les exports viendront).
- **Deux fichiers du même nom** dans un dossier : le deuxième et les suivants prennent ` (2)`, ` (3)`… avant
  l'extension (`rapport (2).pdf`), dans l'ordre de création (stable). Écrire dans `rapport (2).pdf` écrit ce
  fichier-là.
- Un `/` dans un nom devient `∕` (U+2215), et redevient `/` dans un nom créé ou renommé. Un nom cherché sans la
  bonne casse est trouvé s'il est seul.

## La lecture en flux

- Blocs de 1 Mo, demandés par `alt=media` avec `Range` ; un cache (64 Mo par défaut) partagé par les fichiers
  ouverts, par identifiant et version du fichier (`modifiedTime`, `md5Checksum`, pris à l'ouverture).
- **Avance** : des lectures qui se suivent font demander 2, 4, 8, puis 16 blocs d'un coup (chaque requête paie l'attente de Google, quelques centaines de ms) ; un saut loin la remet à 1.
- **Jamais un fichier entier** : la réponse est bornée à la plage demandée ; un serveur qui ignorerait `Range` coupe
  la lecture (E_IO).
- **Une seule connexion** HTTPS gardée ouverte (HTTP/1.1), refaite si le serveur l'a fermée ; une lecture coupée (connexion perdue, réponse plus courte que demandé) est redemandée après 1, 2, 4 s, puis rend une erreur : jamais une fin de fichier.
- Les listes de dossiers sont gardées 60 s (256 au plus) pour lister un dossier et pour la taille d'un fichier ;
  pour **traverser** un dossier vers un chemin plus profond, une liste plus vieille sert encore (relue seulement si
  le nom y manque) : ouvrir un dossier ne recharge que lui, et le `stat` que Fichiers fait sur chaque fichier ne
  coûte rien. Sans réseau, une liste expirée sert encore. Un changement fait par gdrivefs oublie aussitôt la liste
  du dossier touché.
- `fscache net1` : succès, défauts, Mo gardés, remplacements (`FS_CACHE` ; vider le cache : `FS_CC_DROP`).

## Écrire

| Opération | Ce que fait gdrivefs |
|---|---|
| créer (`O_CREAT`) | `files.create` (nom, dossier, type d'après l'extension) dès l'ouverture : le fichier existe, vide |
| ouvrir pour écrire | le contenu tenu dans un fichier du **tampon d'envoi** (`spool`) : copié de Drive à l'ouverture (par le cache), sauf `O_TRUNC` ; lu et écrit là |
| fermer, `FS_SYNC`, fin du programme | s'il a changé : **envoi « resumable »** (`PATCH /upload/drive/v3/files/ID?uploadType=resumable`, puis `PUT` par morceaux de 8 Mo avec `Content-Range`) ; après une coupure, « où en est l'envoi ? » (`bytes */T`) et reprise au dernier octet reçu (3 fois au plus) ; le tampon effacé |
| `mkdir` | `files.create` d'un dossier ; `E_EXIST` s'il y en a un |
| effacer (`rm`, `rmdir`) | **à la corbeille** de Drive (`trashed: true`, 30 jours) ; un dossier qui a encore quelque chose, Google Docs compris : `E_NOTEMPTY` |
| renommer, déplacer | `PATCH` du nom, et `addParents` / `removeParents` d'un dossier à l'autre ; un fichier du nouveau nom est remplacé (mis à la corbeille) ; sur un dossier : `E_EXIST` |
| dater (`utime`) | `PATCH` de `modifiedTime` |

- Le fichier est envoyé **en entier** à sa fermeture, même pour un octet changé (l'API de Drive ne sait pas changer
  une partie d'un fichier). La fermeture attend la fin de l'envoi ; son erreur est rendue par `close`.
- Deux programmes qui écrivent le même fichier : chacun son tampon, le dernier fermé gagne.
- Le tampon d'envoi est sur le disque (`HOME:/gdrive/spool`) : un gros fichier écrit y prend sa taille le temps de
  l'envoi.

## Erreurs

| Réponse de Google | Effet |
|---|---|
| 401 | nouveau jeton demandé à oauthd, un nouvel essai ; puis `E_ACCES` et, au journal, « oauth browser gdrive/NOM » |
| 429, 403 `rateLimitExceeded` / `userRateLimitExceeded`, 5xx | nouvel essai après 1, 2, 4 s ; puis `E_AGAIN` (quota) ou `E_IO` |
| 403 `storageQuotaExceeded` | `E_NOSPC` |
| 404, 410 | `E_NOENT` (une session d'envoi expirée aussi) |
| 400 | `E_INVAL` |
| autre 403 | `E_ACCES` |

## Construire et essayer

```sh
wsl sh tools/pc/apps.sh gdrivefs files         # sys/svc/gdrivefs (OpenSSL, -lnetkit) et Fichiers
wsl sh src/svc/gdrivefs/test/run.sh            # Linux, AddressSanitizer, faux serveur
python -I tools/nettest/gdrivesrv.py 8091      # le faux Drive seul (QEMU : http://10.0.2.2:8091)
```

`src/svc/gdrivefs/gdrivefs.c` : la boucle, la table des fichiers ouverts, les propriétaires, les tampons partagés et
`FS_READ_DIRECT` viennent de smbfs ; les outils HTTPS et JSON d'oauthd (avec la connexion gardée et les tableaux
JSON). Le faux serveur sait aussi créer, `PATCH`, ouvrir des sessions d'envoi, et couper un envoi au milieu
(`/ctl?cut=N`).

Fichiers (`src/apps/files/main.c`, section « Google Drive ») : le menu n'apparaît que si `/sys/svc/gdrivefs` existe ;
le dialogue demande un nom, crée le compte `gdrive/NOM` (`oauth_setup`, fournisseur `google`, le client livré par
Korin), vérifie un compte déjà connecté par `oauth_token`, sinon ouvre Jet sur l'adresse d'`oauth_browser_begin` et
attend `oauth_browser_poll` (une fois par seconde, minuterie du dialogue) ; puis `VFS_MOUNT`, un premier `opendir`,
et la ligne `netN = gdrive:NOM, gdrivefs` dans `/etc/mounts.txt` (retirée au démontage).

## Vérifié

- **Linux (16/11, ASan et UBSan)** : 133 vérifications sur 133 contre `gdrivesrv.py` : listes en plusieurs pages,
  Docs, raccourci, formulaire et corbeille cachés, doublons dans l'ordre de création, `/` remplacé, `stat` sans
  requête de plus, petits fichiers, fichier vide (aucune requête), **100 Ko au milieu d'un fichier de 5 Mo : un seul
  bloc demandé (1 Mo), relu depuis le cache sans octet de plus**, 40 lectures au hasard identiques octet pour octet,
  lecture suivie (3 requêtes : 1 + 2 + 3 blocs, SHA-256 égal, une connexion), propriétaires et fork, `FS_INFO`, 401
  puis nouvel essai, 429 et 403 de quota, connexion coupée puis refaite, liste expirée rechargée. **L'écriture** :
  `readonly`, créer (visible dès l'ouverture, rien d'envoyé avant la fermeture, relu avant l'envoi), modifier le
  début d'un fichier, `O_APPEND`, ouvert sans rien changer (pas d'envoi), `O_TRUNC` seul, **20 Mo en trois morceaux
  avec deux coupures au milieu du premier : MD5 égal, reprise au milieu (pas tout renvoyé)**, `FS_SYNC`, fin d'un
  process qui écrivait, `mkdir` (et `E_EXIST`), déplacer (aussi vers la racine, `root`), renommer, remplacer (l'ancien à la corbeille), dater,
  `E_NOTEMPTY`, effacer, `∕` redevenu `/`, tampon d'envoi vide à la fin.
- **Korin PC sous QEMU (16/11, faux serveur, compte d'essai d'oauthd)** : la lecture (version 1) : `mount`, `ls`,
  `cat`, `hexdump -s` au milieu, `cp` du fichier de 5 Mo (empreinte égale), `fscache`, `df`. Le faux serveur : 17
  requêtes sur **une connexion**, le fichier de 5 Mo **servi une seule fois**. L'écriture (version 2) : `echo >`
  puis `cat`, `echo >>` (ajouté à la fin), `mkdir` (deux fois : la seconde refusée), **`cp` de 5 Mo vers le Drive
  (8 s, un seul `PUT`), relu depuis le Drive : `git hash-object` égal**, `mv` dans un dossier et d'un dossier à
  l'autre, `rm`, `rmdir` d'un dossier vide (d'un plein : refusé), `touch`, tampon d'envoi vide ensuite, `mount`
  (« Google Drive (essai@example.com) », 15 Go, 10 libres) ; 68 requêtes sur une connexion. Le `mv` vers la
  racine a montré une faute du faux serveur (`addParents=root` pris tel quel), corrigée et ajoutée au harnais.
- **ZBook (16/11)** : `oauth browser gdrive/perso` connecté au vrai compte Google, jeton obtenu. Avec pc 2026.11.21,
  **gdrivefs validé par l'utilisateur contre le vrai Drive** : monté à la main (`mount /net/netN gdrive:perso
  gdrivefs`), le Drive accessible, la copie d'un fichier de plusieurs Go à plusieurs Mo/s.
- **À revoir** : Fichiers › Se connecter à Google Drive… a été lent à répondre sur le ZBook et le Drive a dû être
  monté à la main ; l'étape en cause n'est pas encore connue (diagnostic prévu depuis Korin : journal, `oauth status`,
  `/etc/mounts.txt`). Pistes : `oauth_token` et `opendir` appelés dans la boucle de la fenêtre, qui se fige.
- **Pas essayé** : le Pi (gdrivefs n'est construit que pour le PC ; Fichiers n'y montre pas le menu).

## Limites et suite

- **Un jeton refusé que oauthd croit valable** (révoqué chez Google) : oauthd ne sait pas encore forcer un
  rafraîchissement, le nouvel essai après 401 rend le même jeton ; l'utilisateur doit se reconnecter ou attendre
  l'expiration (1 h). À faire : une fonction de NetKit qui demande un nouveau jeton.
- **Un seul thread** : une avance de 16 Mo, un envoi, une attente de quota (7 s au plus) font attendre les autres
  requêtes du volume.
- `df` et `mount` montrent le type `?` (ils ne nomment que les genres 1 à 3 ; smbfs aussi).
- Un fichier ouvert dont le contenu change ailleurs garde la version vue à l'ouverture.
- **Suite** (étude, section 9) : les exports des Google Docs ; les raccourcis ; `changes.list` ; plusieurs requêtes à
  la fois ; le Pi.
