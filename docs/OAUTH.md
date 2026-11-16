# OAuth 2.0 : les comptes en ligne du système (oauthd)

*16/11/2026. Fait : le service, les fonctions de NetKit, la commande `oauth`. Essayé sous Linux (faux serveur,
vrai TLS) ; dans Korin : voir « Vérifié ».*

Beaucoup de services en ligne refusent les mots de passe et demandent OAuth 2.0 : Google (Drive, Gmail), Microsoft
(Outlook, OneDrive), Dropbox… Un programme de Korin n'a pas à refaire ce protocole : il demande un **jeton d'accès**
à NetKit (`oauth_token`) et le met dans ses requêtes (`Authorization: Bearer …`).

```
programme ──oauth_*──▶ NetKit ──requête (service « oauth »)──▶ oauthd ──HTTPS──▶ serveur d'autorisation
                         │                                       │
                         └── lance /sys/svc/oauthd au besoin     └── HOME:/oauth : accounts.ini, secrets, key
```

## 1. Pourquoi un service

NetKit est du C sans libc ni OpenSSL, et la libc le charge dans tout programme qui ouvre un socket : y mettre
OpenSSL ajouterait plusieurs Mo à chacun. Une bibliothèque statique (le code de Mail sorti dans une `liboauth.a`)
ne servirait qu'au C et au C++ qui lient déjà OpenSSL, et chaque programme lirait lui-même les secrets.

Un service (choix du 16/11) :

- **toutes les langues** l'appellent : assembleur, C, C++, C#, BASIC, les scripts (par la commande `oauth`) ;
- **les jetons ne quittent pas oauthd** : les jetons de rafraîchissement restent chez lui (chiffrés sur la carte) ;
  un programme ne reçoit qu'un jeton d'accès, valable une heure ;
- **un seul endroit** pour les comptes, partagés entre les programmes (Fichiers et gdrivefs avec le même compte
  Google, par exemple), et plus tard un trousseau du système ;
- les **services** (gdrivefs, docs/etude/GDRIVE.md) s'en servent comme les apps.

## 2. Les fonctions (NetKit)

Le **compte** est un nom choisi par le programme : lettres, chiffres, `. _ - / @`, 63 caractères au plus, par
exemple `gdrive/perso` ou `mail/jean@outlook.com`. Résultat négatif : une erreur, dont `oauth_error` donne le texte.

| Fonction | Arguments | Résultat |
|---|---|---|
| `oauth_setup` | compte, configuration (texte, une ligne « clé = valeur » par réglage) | 0 ; le compte est créé ou remplacé |
| `oauth_status` | compte | 2 jeton d'accès valable, 1 connecté, 0 à connecter ; `-2` compte inconnu |
| `oauth_device_begin` | compte, tampon, taille | octets écrits : 5 lignes, le code à taper, l'adresse où le taper, l'adresse complète (code inclus, peut être vide), la durée de validité (s), l'intervalle (s) |
| `oauth_device_poll` | compte | 0 connecté ; `-11` pas encore ; une autre erreur : refusé, expiré |
| `oauth_browser_begin` | compte, port (0 : choisi entre 47800 et 47819), tampon, taille | octets écrits : l'adresse à ouvrir dans un navigateur |
| `oauth_browser_poll` | compte | 0 connecté ; `-11` le navigateur n'est pas encore revenu |
| `oauth_browser_finish` | compte, adresse de retour collée (ou le code seul) | 0 connecté |
| `oauth_token` | compte, tampon, taille | octets écrits : le jeton d'accès (rafraîchi s'il le faut) ; `-13` : à reconnecter |
| `oauth_forget` | compte | 0 ; jetons oubliés, configuration gardée (se déconnecter) |
| `oauth_remove` | compte | 0 ; le compte disparaît |
| `oauth_info` | compte, tampon, taille | octets écrits : la configuration (le secret du client masqué) et les adresses retenues |
| `oauth_list` | tampon, taille | octets écrits : une ligne par compte, « nom, état (0/1/2), fournisseur », séparés par des tabulations |
| `oauth_error` | tampon, taille | octets écrits : le texte de la dernière erreur de ce process |

Un tampon trop petit rend `-34`. Avec une taille de 0, une fonction qui écrit rend seulement la taille nécessaire.
Ces fonctions suivent la convention d'appel ordinaire (contrairement à `net_*` et `wifi_*`, elles ne gardent pas
tous les registres) et elles **attendent le réseau** (jusqu'à 20 s) : dans une app graphique, un thread.

### La configuration

| Clé | Rôle |
|---|---|
| `provider` | `google`, `microsoft` ou `custom` (défaut) : les adresses des deux premiers sont connues |
| `client_id` | l'identifiant de l'application ; sans lui, celui que Korin livre pour ce fournisseur (ci-dessous) |
| `client_secret` | si le fournisseur en donne un (Google, « application de bureau » : il en faut un, qui ne protège rien) |
| `scope` | les permissions, séparées par des espaces (Microsoft : ajouter `offline_access`) |
| `tenant` | Microsoft : `common` (défaut), `consumers`, `organizations` ou l'identifiant d'une organisation |
| `auth_url`, `token_url`, `device_url` | les adresses, pour `custom` (ou pour remplacer celles d'un fournisseur connu) |
| `auth_params` | paramètres de plus pour l'adresse d'accord ; Google : `access_type=offline&prompt=consent` par défaut, sans quoi il ne rend pas de jeton de rafraîchissement |

Changer `client_id` ou l'adresse des jetons oublie les jetons du compte.

### Les fournisseurs livrés avec Korin

Comme Mail, qui livre son identifiant chez Microsoft (« Onyx Mail ») : l'utilisateur n'a **qu'à se connecter avec
son compte**. `/etc/oauth/providers.ini` (paquet `pc`, `skel/pc/etc/oauth/providers.ini`) a une section par
fournisseur ; ses clés valent pour tout compte de ce fournisseur qui ne les donne pas (décision du 16/11) :

```ini
[google]
client_id = @GOOGLE_CLIENT_ID@                 ; l'application « Korin » de la console Google Cloud
client_secret = @GOOGLE_CLIENT_SECRET@
scope = https://www.googleapis.com/auth/drive

[microsoft]
client_id = 85ccaf6e-81ff-4a62-9194-930fc36429ad   ; « Onyx Mail », le même que Mail
tenant = common
```

`oauth setup gdrive/perso provider=google` suffit donc. Le compte ne recopie pas ces valeurs (`accounts.ini` n'a que
ce que le compte donne) : une mise à jour du système qui change l'identifiant vaut pour tous les comptes, et
oauthd relit le fichier quand il change. Un compte peut toujours donner son propre `client_id` (il l'emporte) ;
`oauth info` marque « (Korin) » ce qui vient du système. Une section peut aussi décrire un fournisseur inconnu
d'oauthd (`auth_url`, `token_url`, `device_url`).

**Les valeurs ne sont pas dans le dépôt** (demande de l'utilisateur, 16/11) : `skel/pc/etc/oauth/providers.ini` n'a que
des marques `@NOM@`. Les vraies valeurs sont dans un fichier local, hors du dépôt, à côté de la clé des paquets :
`../korin-secrets.ini` (ou le chemin de `KORIN_SECRETS`), une ligne `NOM = valeur` par marque
(`GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET`). `tools/pc/sysroot.sh` remplit la copie de `build/pc/sysroot` par
`tools/secrets.py` (qui refuse de réécrire une source de `skel/` et n'affiche jamais une valeur) ; sans le fichier,
la marque reste, oauthd la prend pour une valeur absente, et `oauth setup … provider=google` sans `client_id`
refuse. Une machine de construction sans ce fichier (un agent du cloud) construit donc un système sans client
Google. Les paquets publiés, eux, contiennent les valeurs : elles doivent être sur la machine de l'utilisateur.

Pour une application installée, Google (« application de bureau ») et Microsoft (client public) ne tiennent pas
l'identifiant ni le secret du client pour secrets : ils sont livrés en clair, comme ceux de rclone. Côté Google,
l'application « Korin » non vérifiée est limitée à **100 utilisateurs**, qui voient « Google n'a pas validé cette
application » à la connexion ; au-delà, la permission `drive` (« restreinte ») demande la vérification de Google et
un audit de sécurité annuel.

### Les deux façons de se connecter

**Code d'appareil** (RFC 8628) : le programme affiche un code et une adresse (« sur votre téléphone, ouvrez
microsoft.com/link et tapez ABCD-EFGH »), puis appelle `oauth_device_poll` toutes les secondes ; oauthd ne
questionne pas le serveur plus vite que l'intervalle qu'il a donné. Rien à ouvrir dans Korin. **Google limite ce
flux** à quelques permissions (pas `drive`, seulement `drive.file` et `drive.appdata`) ; Microsoft l'accepte pour
Outlook et OneDrive.

**Navigateur** (code d'autorisation avec PKCE S256, RFC 7636 ; retour local, RFC 8252) : `oauth_browser_begin`
rend l'adresse d'accord et oauthd écoute sur `http://127.0.0.1:<port>/`. Le programme ouvre l'adresse dans Jet ;
après l'accord, le navigateur revient sur ce port, oauthd prend le code, l'échange, et répond une page « Connexion
réussie ». Le programme appelle `oauth_browser_poll` toutes les secondes. Sans navigateur dans Korin (le Pi sans
Jet), l'adresse s'ouvre sur un autre appareil : le retour vers `127.0.0.1` y échoue, mais la barre d'adresse montre
`…/?code=…&state=…` ; on la colle dans Korin, `oauth_browser_finish`. Le `state` est vérifié dans les deux cas.

Le port écoute sur toutes les interfaces (netd ne sait pas se limiter à 127.0.0.1) : une requête venue d'ailleurs
sans le bon `state` est refusée.

## 3. La commande `oauth`

Pour les scripts, les agents et les essais :

```sh
oauth setup gdrive/perso provider=google   # le client de Korin (providers.ini)
oauth browser gdrive/perso          # affiche l'adresse, attend le retour du navigateur
oauth status gdrive/perso
curl -H "Authorization: Bearer $(oauth token gdrive/perso)" \
     "https://www.googleapis.com/drive/v3/about?fields=user"
oauth list
oauth login mail/outlook            # code d'appareil
oauth finish gdrive/perso 'http://127.0.0.1:47800/?code=…&state=…'
oauth forget gdrive/perso ; oauth remove gdrive/perso
```

Sans `clé=valeur` après le nom, `oauth setup` lit la configuration sur l'entrée standard (`oauth setup x < conf.ini`).

## 4. Exemple en C

```c
#include <netkit_abi.h>
#include <stdio.h>
#include <unistd.h>

static int connect_account (const char *acc)
{
	char out[1024];
	if (oauth_status ((long) acc) > 0)
		return 0;
	long r = oauth_device_begin ((long) acc, (long) out, sizeof out);
	if (r < 0)
		return -1;
	printf ("Code et adresse :\n%s", out);
	while ((r = oauth_device_poll ((long) acc)) == -11)
		sleep (1);
	return r < 0 ? -1 : 0;
}

int main (void)
{
	char tok[4096], err[256];
	if (!netkit_kit_open ())
		return 1;
	oauth_setup ((long) "demo/ms", (long) "provider = microsoft\nclient_id = 0000-…\n"
		"scope = https://graph.microsoft.com/Files.Read offline_access\n");
	if (connect_account ("demo/ms") || oauth_token ((long) "demo/ms", (long) tok, sizeof tok) < 0)
	{
		oauth_error ((long) err, sizeof err);
		fprintf (stderr, "%s\n", err);
		return 1;
	}
	/* tok : « Authorization: Bearer <tok> » dans les requêtes HTTPS (OpenSSL, curl) */
	return 0;
}
```

Un programme appelle `oauth_token` **avant chaque série de requêtes** plutôt que de garder le jeton : oauthd le
garde en mémoire et ne le rafraîchit qu'une minute avant son expiration. Une réponse 401 du service : rappeler
`oauth_token` une fois ; `-13` : l'utilisateur doit se reconnecter.

## 5. Les fichiers

Dans le dossier personnel (`HOME:/oauth`, aujourd'hui `/home/oauth`), comme Mail (`HOME:/mail`) : avec les comptes
d'utilisateurs ([etude/MULTI-UTILISATEUR.md](etude/MULTI-UTILISATEUR.md)), chacun aura les siens.

| Fichier | Contenu |
|---|---|
| `accounts.ini` | une section par compte, la configuration ; **aucun jeton** ; écrit par oauthd seul |
| `secrets` | les jetons de rafraîchissement : « KORINOA1 », 12 octets d'IV, 16 de marque, puis le texte (« nom, tabulation, jeton » par ligne) chiffré en AES-256-GCM (EVP d'OpenSSL) |
| `key` | la clé, 32 octets aléatoires, faite au premier jeton |

Chaque fichier est écrit à côté puis renommé : jamais à moitié écrit. Les jetons d'accès ne sont jamais écrits.

**Ce que protège le chiffrement** : un fichier `secrets` copié seul, ou ouvert par erreur, ne se lit pas. La clé est
sur la même carte : qui a la carte entière a les jetons (comme pour Mail). Un jeton de rafraîchissement donne
l'accès au compte en ligne (tout le Drive avec la permission `drive`) jusqu'à ce qu'on le révoque chez le
fournisseur ; `oauth forget` ne fait que l'oublier ici. Korin n'a pas encore de comptes ni de droits : tout
programme peut demander le jeton de tout compte. Plus tard : un trousseau, une clé liée au mot de passe de session,
et une liste des programmes permis par compte.

## 6. Le service

`src/svc/oauthd/oauthd.c` : C sur libposix et OpenSSL (comme smbfs), service « oauth », `/sys/svc/oauthd`.

- **Lancé par NetKit** au premier appel d'une fonction `oauth_*` (`f_spawn`, puis `SVC_OPEN` avec 4 s d'attente,
  comme PrinterKit avec printd), puis il reste. S'il s'arrête, l'appel suivant le relance.
- **Un seul thread** : une requête à la fois ; une requête HTTPS (20 s au plus) fait attendre les autres programmes.
  Les jetons d'accès en mémoire évitent presque toujours le réseau.
- **Protocole** : le nom du compte dans les données du message ; `c` et `d` désignent un tampon du programme, que
  oauthd lit ou écrit directement (`SVC_MAP_CALLER`, 64 Ko au plus) ; le résultat dans `op`. Les numéros (1 à 13)
  sont dans `oauthd.c` et `src/kits/netkit/netkit.c`.
- **HTTPS** : HTTP/1.0 (réponse lue jusqu'à la fermeture, morceaux décodés au besoin), TLS 1.2 au moins, certificat
  et nom vérifiés par `/etc/ca-bundle`. **En clair** : seulement vers `localhost` et les adresses privées (127/8,
  10/8, 172.16/12, 192.168/16), pour un faux serveur d'essai.
- **Réponses** : les champs JSON du premier niveau (`access_token`, `refresh_token`, `expires_in`, `error`,
  `error_description`, `device_code`, `user_code`, `verification_uri` ou `verification_url` de Google…). Un
  nouveau `refresh_token` (Microsoft en rend un à chaque fois) remplace l'ancien ; `invalid_grant` au
  rafraîchissement oublie le jeton (révoqué, expiré, mot de passe changé).
- 64 comptes au plus ; la dernière erreur de chaque process (32 process), oubliée à sa fin.

NetKit en assembleur (`netkit.asm`, encore assemblé par `build.ps1`) a les mêmes places dans sa table, qui
rendent `-38` : c'est le NetKit en C qui va sur les cartes.

## 7. Construire et essayer

```sh
wsl sh tools/kits/build_c.sh netkit        # NetKit (Pi et PC)
wsl sh tools/pc/apps.sh oauthd             # PC : sys/svc/oauthd et bin/oauth (OpenSSL de tools/pc/thirdparty.sh)
wsl sh src/svc/oauthd/test/run.sh          # Linux, AddressSanitizer, faux serveur ; NET=1 : vrai TLS aussi
python -I tools/nettest/oauthsrv.py 8090   # le faux serveur seul (QEMU : http://10.0.2.2:8090)
```

`src/svc/oauthd/test/linux.c` compile oauthd sous Linux (les appels de Korin remplacés : `SVC_MAP_CALLER` rend
l'adresse elle-même) et appelle ses opérations directement.

## 8. Vérifié

- **Linux (WSL, AddressSanitizer et UBSan, 16/11)** : 56 vérifications sur 56 contre `oauthsrv.py` (dont, le soir,
  le fournisseur livré : compte réduit à `provider`, client et portée pris de `providers.ini`, marqués « (Korin) »
  par `info`, non recopiés dans `accounts.ini`, connexion par code d'appareil, client propre au compte prioritaire) : configuration
  (nom invalide, client absent), code d'appareil (attente, intervalle respecté, accord à la 3e demande), jeton,
  tampon trop petit, rafraîchissement (réponse en morceaux, nouveau jeton de rafraîchissement enregistré),
  relecture des fichiers (secrets déchiffrés, aucun jeton en clair dans `secrets` ni `accounts.ini`), navigateur
  (PKCE vérifié par le serveur, requête parasite `favicon.ico`, retour sur le port local, page rendue), adresse
  collée, mauvais `state` refusé, accord refusé, liste, révocation (`invalid_grant` : jeton oublié), oubli, retrait,
  `http://` public refusé, client changé (jetons oubliés). **Vrai TLS** (`NET=1`) : Google répond `invalid_client`
  (certificat vérifié), `expired.badssl.com` refusé (certificat expiré).
- **Korin PC sous QEMU** (WSL, TCG, 4 cœurs, NVMe, usb-net, 16/11 ; trois démarrages, script bash lancé par
  `startup.txt`, `oauthsrv.py` sur `10.0.2.2:8090`) : oauthd lancé par NetKit au premier appel (`f_spawn`, par
  init2) ; `setup`, `status`, `login` (accord à la 3e demande, ~3,5 s), `token` (jeton gardé en mémoire), `list`,
  `info` ; erreurs en texte (compte inconnu, nom invalide) ; fichiers de `/home/oauth` (aucun jeton en clair ;
  `secrets` déchiffré sur le PC hôte : bon format) ; **vrai HTTPS** vers Google (`invalid_client`, certificat vérifié
  par `/etc/ca-bundle`) ; comptes et jetons gardés après un redémarrage ; oauthd tué : relancé par NetKit au
  prochain appel, qui rafraîchit depuis `secrets` ; révocation (`invalid_grant`) ; navigateur et PKCE (port 8765,
  retour suivi par `wget` de Korin, page « Connexion réussie ») ; `forget`, `remove`.
- **Pas essayé** : un vrai compte Google ou Microsoft (il faut un `client_id` et une personne), `oauth finish` dans
  Korin (essayé sous Linux seulement), le ZBook, le Pi (oauthd n'est construit que pour le PC), Jet comme
  navigateur de retour.
- Remarque : un oauthd arrêté n'est pas relancé par init2 (lancé par `INIT_OP_LAUNCH`) ; NetKit le relance au
  prochain appel.

## 9. Plus tard

- Construire oauthd pour le Pi (OpenSSL AArch64 de `tools/openssl/build.sh`).
- Passer Mail dessus : son `mail/oauth.h` (Outlook, code d'appareil, jamais essayé) appellerait `oauth_*`, et ses
  jetons Microsoft quitteraient `HOME:/mail/secrets`.
- Un volet de Réglages « Comptes en ligne » (liste, connexion, oubli), et un dialogue d'UIKit pour se connecter
  (code d'appareil ou Jet), que les apps appelleraient d'une ligne.
- Plusieurs requêtes à la fois (un thread par requête HTTPS).
- Révoquer chez le fournisseur à `oauth_forget` (Google : `oauth2.googleapis.com/revoke`).
