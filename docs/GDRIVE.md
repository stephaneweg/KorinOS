# Google Drive : gdrivefs

*16/11/2026. Fait : étape 1 de l'étude ([etude/GDRIVE.md](etude/GDRIVE.md)), un volume en lecture seule, lu en
flux. Essayé sous Linux (faux serveur) et dans Korin PC sous QEMU (faux serveur) ; pas encore contre le vrai Drive.*

Le Drive de l'utilisateur monté dans l'arbre, comme un partage SMB :

```sh
oauth setup gdrive/perso provider=google        # une fois : le client de Korin (docs/OAUTH.md)
oauth browser gdrive/perso                      # une fois : se connecter à son compte Google
mount /net/net1 gdrive:perso gdrivefs
ls /net/net1
cp /net/net1/Photos/vacances.jpg /tmp/
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
| `cache` | la taille du cache des blocs (défaut `32M`, 8 Mo au moins) |
| `ttl` | la durée de vie d'une liste de dossier, en secondes (défaut 60) |
| `api_url` | l'adresse de l'API (défaut `https://www.googleapis.com`) ; en clair seulement vers une adresse privée : un faux serveur d'essai |

## Ce qu'on voit

- Les dossiers et les fichiers de Drive (`files.list`, pages suivies). La corbeille est cachée.
- **Cachés pour l'instant** : les Google Docs, Sheets, Slides, formulaires, et les raccourcis (pas de contenu
  binaire ; les exports viendront).
- **Deux fichiers du même nom** dans un dossier : le deuxième et les suivants prennent ` (2)`, ` (3)`… avant
  l'extension (`rapport (2).pdf`), dans l'ordre de création (stable).
- Un `/` dans un nom devient `∕` (U+2215). Un nom cherché sans la bonne casse est trouvé s'il est seul.
- **Lecture seule** : écrire, créer, effacer, renommer rendent « permission refusée » (`E_ACCES`).

## La lecture en flux

- Blocs de 1 Mo, demandés par `alt=media` avec `Range` ; un cache (32 Mo par défaut) partagé par les fichiers
  ouverts, par identifiant et version du fichier (`modifiedTime`, `md5Checksum`, pris à l'ouverture).
- **Avance** : des lectures qui se suivent font demander 2, puis 4 blocs d'un coup ; un saut loin la remet à 1.
- **Jamais un fichier entier** : la réponse est bornée à la plage demandée ; un serveur qui ignorerait `Range` coupe
  la lecture (E_IO).
- **Une seule connexion** HTTPS gardée ouverte (HTTP/1.1), refaite une fois si le serveur l'a fermée.
- Les listes de dossiers sont gardées 60 s (256 au plus) ; sans réseau, une liste expirée sert encore.
- `fscache net1` : succès, défauts, Mo gardés, remplacements (`FS_CACHE` ; vider le cache : `FS_CC_DROP`).

## Erreurs

| Réponse de Google | Effet |
|---|---|
| 401 | nouveau jeton demandé à oauthd, un nouvel essai ; puis `E_ACCES` et, au journal, « oauth browser gdrive/NOM » |
| 429, 403 `rateLimitExceeded` / `userRateLimitExceeded`, 5xx | nouvel essai après 1, 2, 4 s ; puis `E_AGAIN` (quota) ou `E_IO` |
| 404 | `E_NOENT` |
| autre 403 | `E_ACCES` |

## Construire et essayer

```sh
wsl sh tools/pc/apps.sh gdrivefs               # sys/svc/gdrivefs (OpenSSL, -lnetkit)
wsl sh src/svc/gdrivefs/test/run.sh            # Linux, AddressSanitizer, faux serveur
python -I tools/nettest/gdrivesrv.py 8091      # le faux Drive seul (QEMU : http://10.0.2.2:8091)
```

`src/svc/gdrivefs/gdrivefs.c` : la boucle, la table des fichiers ouverts, les propriétaires, les tampons partagés et
`FS_READ_DIRECT` viennent de smbfs ; les outils HTTPS et JSON d'oauthd (avec la connexion gardée et les tableaux
JSON).

## Vérifié

- **Linux (16/11, ASan et UBSan)** : 92 vérifications sur 92 contre `gdrivesrv.py` : listes en plusieurs pages,
  Docs, raccourci, formulaire et corbeille cachés, doublons dans l'ordre de création, `/` remplacé, `stat` sans
  requête de plus, petits fichiers, fichier vide (aucune requête), **100 Ko au milieu d'un fichier de 5 Mo : un seul
  bloc demandé (1 Mo), relu depuis le cache sans octet de plus**, 40 lectures au hasard identiques octet pour octet,
  lecture suivie (3 requêtes : 1 + 2 + 3 blocs, SHA-256 égal, une connexion), propriétaires et fork, écritures
  refusées, `FS_INFO`, 401 puis nouvel essai, 429 et 403 de quota, connexion coupée puis refaite, liste expirée
  rechargée.
- **Korin PC sous QEMU (16/11, faux serveur, compte d'essai d'oauthd)** : `mount /net/net1 gdrive:test gdrivefs`,
  `ls` / `ls -l` (doublons, noms cachés, tailles), `cat`, `hexdump -s` au milieu, `cp` du fichier de 5 Mo vers
  `/tmp` (empreinte `git hash-object` égale), `fscache`, `df` (« Google Drive (adresse) »), écritures, `mkdir`, `rm`,
  `mv` refusés. Le faux serveur : 17 requêtes sur **une connexion**, le fichier de 5 Mo **servi une seule fois**.
- **Pas essayé** : le vrai Google Drive, le ZBook, le Pi (gdrivefs n'est construit que pour le PC), le démontage
  (pas de commande `umount` dans `/bin` : Fichiers le fait par `VFS_UNMOUNT`).

## Limites et suite

- **Un jeton refusé que oauthd croit valable** (révoqué chez Google) : oauthd ne sait pas encore forcer un
  rafraîchissement, le nouvel essai après 401 rend le même jeton ; l'utilisateur doit se reconnecter ou attendre
  l'expiration (1 h). À faire : une fonction de NetKit qui demande un nouveau jeton.
- **Un seul thread** : une avance de 4 Mo ou une attente de quota (7 s au plus) fait attendre les autres requêtes.
- `df` et `mount` montrent le type `?` (ils ne nomment que les genres 1 à 3 ; smbfs aussi).
- Un fichier ouvert dont le contenu change ailleurs garde la version vue à l'ouverture.
- **Suite** (étude, section 9) : Fichiers › Se connecter à Google Drive… (dialogue, `oauth_browser_*`, Jet) ;
  l'écriture (envoi « resumable » à la fermeture) ; les exports des Google Docs ; les raccourcis ; `changes.list` ;
  plusieurs requêtes à la fois ; le Pi.
