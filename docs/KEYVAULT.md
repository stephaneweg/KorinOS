# Le coffre à secrets (keyvault)

Le coffre garde les secrets des programmes : clés d'API, jetons OAuth, mots de passe de serveurs, clés privées.
Ils sont chiffrés dans `/var/keyvault`. Seul le service `keyvault` sait les lire, et il ne rend un secret qu'au
programme qui l'a rangé. L'étude et les décisions sont dans [etude/COFFRE.md](etude/COFFRE.md).

**État (18/11/2026) : PC seulement.** Vérifié sous QEMU (§8). Le TPM n'est essayé que sous Linux, contre
swtpm. Le Pi n'a pas de coffre : ses programmes gardent leurs anciens fichiers, et les fonctions du coffre y
rendent une erreur.

```
programme ──kv_* / vault_* / secret──▶ keyvault ──▶ /var/keyvault/<uid>.db
                                         │  identité : SYS_PROC_EXE (noyau) + empreinte du fichier
                                         │  clé : mot de passe du compte (authd) + clé de la machine (TPM)
                                         └─ carte /sys/authcard (mot de passe, accord)
```

## 1. Ranger et relire un secret

### En C ou en C++

En C ou en C++ (libposix), avec l'en-tête seul `keyvault.h` : `src/lib/keyvault/keyvault.h` dans le dépôt,
`<keyvault.h>` avec le GCC de Korin. Aucune bibliothèque n'est à lier.

```c
#include <keyvault.h>

kv_set ("anthropic", key, strlen (key));        /* -> 0 */
char k[256];
long n = kv_get ("anthropic", k, sizeof k - 1); /* -> octets ; -2 : pas de tel secret */
if (n >= 0) k[n] = 0;
kv_delete ("anthropic");
kv_list (buf, sizeof buf);                      /* les noms de ce programme, un par ligne */
kv_status ();                                   /* < 0 : pas de coffre (le Pi) */
kv_error (buf, sizeof buf);                     /* le texte de la dernière erreur */
```

### Les autres langages

| Langage | Comment |
|---|---|
| assembleur, BASIC et tout langage qui appelle les kits | les mêmes fonctions dans AppKit : `vault_set`, `vault_get`, `vault_delete`, `vault_list`, `vault_status`, `vault_error` (fin de la table, version 59) |
| scripts | la commande `secret` (§4) |

### Noms, valeurs et erreurs

- **Nom** : lettres, chiffres et `. _ - / @ :`, 128 caractères au plus.
- **Valeur** : 64 Kio au plus, des octets quelconques.
- **Erreurs** :

| Code | Sens |
|---|---|
| `-2` | pas de tel secret |
| `-13` | coffre fermé, ou mot de passe refusé |
| `-1` | programme non reconnu |
| `-125` | refusé à la carte |

- **Coffre fermé** : le coffre ouvre la carte du mot de passe, et l'appel attend la réponse (5 minutes au plus).

**Sans coffre** (`kv_status () < 0`), un programme garde son ancien comportement. Les migrations du §6 le font
toutes.

## 2. L'identité d'un programme

Le programme ne dit jamais qui il est : le coffre le demande au noyau.

### Les deux appels du noyau

`SYS_PROC_EXE` (0x94) rend, pour un pid :
- le chemin de son image, tel que son chargeur l'a déclaré ;
- l'empreinte SHA-256 de cette image (calculée par le noyau à la première demande) ;
- le script qu'il exécute, s'il en a déclaré un.

`SYS_PROC_SET_SCRIPT` (0x95) déclare ce script :
- une seule fois par process ;
- remis à zéro par `exec`, gardé par `fork`.

### Ce que le coffre en fait

1. **Il vérifie le chemin.** Il relit le fichier lui-même, en calcule l'empreinte et la compare à celle de
   l'image. Une image qui ne correspond pas à son fichier est refusée (« relancez-le »). Les programmes de `/sys`
   (droits) sont crus sans relecture.
2. **Il reconnaît les interprètes.** Pour un interprète de `/etc/keyvault/interpreters.ini` (bash, python3,
   dotnet), l'identité est le **script** déclaré, pas l'interprète. Les déclarations :
   - bash : `tools/bash/patches/03-coffre-script.patch` ;
   - CPython : `config_run_filename_abspath`, avant `site` et les `.pth` (`tools/python/korin.patch`). Il n'y a pas
     encore d'interpréteur à ce moment-là : seulement l'allocateur brut (`_Py_EncodeLocaleRaw`, `PyMem_RawFree`).
     `Py_EncodeLocale` passe par obmalloc, et `python3 fichier.py` plantait (du 18 au 20/11) ;
   - dotnet : `armpihost.cpp` déclare l'assembly (`tools/dotnet/armpi.patch`).

   Un interprète sans script (bash interactif, `python3 -c`) n'a pas de coffre.
3. **Il partage un dossier d'app.** Une app de `/apps` peut partager ses secrets entre ses programmes avec
   `vault = folder` dans son `app.txt`. L'identité devient alors le dossier `/apps/x.app`.
4. **Il retient l'empreinte hors des dossiers du système.**
   - Les dossiers du système sont `/apps`, `/bin`, `/sbin`, `/sys`, `/lib`, `/devtools`. Ailleurs (une app
     compilée dans `HOME:`), le coffre retient l'empreinte au premier secret.
   - Si le fichier change (nouvelle compilation, ou un autre programme l'a remplacé), la carte demande : « … a
     changé depuis qu'il a rangé ses secrets. Lui laisser ses secrets ? ».

### L'espace partagé @system

L'espace `@system` (`kv_sys_set`, `kv_sys_get`, `kv_sys_delete`, drapeau `KVF_SYSTEM`) est partagé par les
programmes du compte system (uid 0), et refusé aux autres comptes. Un secret écrit par Réglages, élevé, y est lu
par un service : le mot de passe d'accès à distance, les clés Wi-Fi.

## 3. Les clés

### Hiérarchie

```
mot de passe du compte ──Argon2id (64 Mio, 3 passes)──▶ P ─┐
                                                           ├─HKDF-SHA256─▶ KEK ──AES-256-GCM──▶ clé maîtresse du compte
clé de la machine (TPM, sinon /etc/keyvault/machine.key) ─▶ T ─┘                                            │
                                                         chaque secret : AES-256-GCM, données associées uid|identité|nom
```

- **La clé maîtresse** de chaque compte est tirée au hasard une fois. Seule son enveloppe change avec le mot de
  passe. Elle ne vit qu'en mémoire de keyvault, coffre ouvert.
- **Il faut P et T.** Le disque seul n'a pas T (il est dans le TPM). La machine seule n'a pas P (le mot de passe).
- **Le compte system** (les services) n'a que T : ses secrets ne sont protégés que du disque emporté seul.
- **Un compte sans mot de passe** est ouvert par T seul, au premier usage (connexion automatique : comme avant).

### Ouverture et fermeture

| Événement | Ce qui se passe |
|---|---|
| connexion | `/sys/login` fait vérifier le mot de passe par authd (`AUTH_VERIFY`), qui le passe à keyvault (`KV_UNLOCK`) |
| `passwd` | authd demande d'abord au coffre de réenvelopper sa clé (`KV_PASSWD`) ; s'il refuse, rien ne change |
| mot de passe remis par l'uid 0 (`AUTH_SETPW`, sans l'ancien) | coffre ouvert : réenveloppé ; coffre fermé : **vidé** (pas de clé de secours, décision du 18/11) |
| `secret lock` | ferme le coffre du compte |
| première demande, coffre fermé | la carte du mot de passe s'ouvre (`/sys/authcard`, mode coffre) |

## 4. La commande `secret`

| Commande | Effet |
|---|---|
| `secret set NOM [VALEUR]` | range (sans VALEUR : lue sur l'entrée standard, ou demandée sans écho) |
| `secret get NOM` | écrit la valeur |
| `secret rm NOM` | oublie |
| `secret ls` | les noms |
| `secret whoami` | l'identité vue par le coffre |
| `secret status` | ouvert, fermé, pas encore créé |
| `secret unlock` | ouvre le coffre du compte (mot de passe) |
| `secret lock` | ferme le coffre du compte |

L'identité est celle du **programme qui lance `secret`** : le script, déclaré par son interprète. `secret`
demande au coffre l'espace de son père (`KVF_PARENT`, permis à `/bin/secret` seul, même compte). Exemple dans un
script :

```bash
ANTHROPIC_API_KEY=$(secret get anthropic) claude-acp
```

Un bash interactif n'est pas un script : `secret get` y est refusé.

git range ses identifiants par l'assistant `git-credential-korin` (`credential.helper = korin` dans
`/etc/gitconfig`), qui s'appuie sur `secret`.

## 4 bis. Réglages › Coffre à secrets (le gestionnaire)

Demandé le 21/11 : voir les secrets rangés, comme le Gestionnaire d'identification de Windows. Le volet
(`src/svc/keyvault/applet/applet.cpp` → `/sys/applets/vault.so`, paquet `pc`) :

- **la liste** de tous les secrets du compte, de tous les programmes : programme (l'identité : chemin, script,
  dossier d'app ou `@system`), nom, taille ; jamais les valeurs ;
- **Afficher…**, **Copier** : la valeur, après le **mot de passe du compte** ; la fenêtre de la valeur se ferme
  seule après 30 s ;
- **Supprimer** (même règle), **Actualiser** ;
- **Secrets du système** : le coffre du compte system (services, `@system` : accès à distance, Wi-Fi, partages…),
  après la carte d'élévation (`ui_elevate`).

Trois opérations du coffre, servies à **Réglages seul** (`/apps/control.app/main` ou `/apps/control.app`, identité
vérifiée comme au §2 ; un volet de `/sys/applets` agit donc au nom de Réglages) :

| Opération | Données | Rend |
|---|---|---|
| `KV_MGR_LIST` (13) | — | une ligne par secret : `identité\tnom\ttaille` |
| `KV_MGR_GET` (14) | identité, nom, mot de passe (ou vide) | la valeur |
| `KV_MGR_DELETE` (15) | identité, nom, mot de passe (ou vide) | 0 |

- `KVF_SYSTEM` : le coffre du compte system. Permis à un appelant d'uid 0 (Réglages élevé) ; le process garde
  ensuite ce droit **5 minutes**, revenu à son uid.
- Valeur et effacement dans le coffre du compte : sans mot de passe, `E_ACCES` (« mot de passe demandé ») ; le
  mot de passe est vérifié sur l'enveloppe du coffre (Argon2id, comme à l'ouverture ; refus : 1 s d'attente), puis
  vaut **5 minutes** pour ce process (choix de l'utilisateur, comme sudo). Un compte sans mot de passe n'en demande
  pas (la machine seule protège son coffre).
- Coffre fermé : la carte du mot de passe, comme pour toute demande.
- Client : `kv_mgr_list`, `kv_mgr_get`, `kv_mgr_delete` de `src/lib/keyvault/keyvault.h`.

## 5. Fichiers

| Fichier | Contenu |
|---|---|
| `/var/keyvault/<uid>.db` | le coffre d'un compte (voir ci-dessous) |
| `/etc/keyvault/machine.tpm` | la clé de la machine scellée dans le TPM (`KORINTPM1`, parties privée et publique) |
| `/etc/keyvault/machine.key` | la clé de la machine sans TPM (32 octets) ; reprise dans le TPM puis effacée au premier démarrage avec un TPM |
| `/etc/keyvault/interpreters.ini` | les interprètes dont le script déclaré est cru |

Le format de `/var/keyvault/<uid>.db` :
1. `KORINKV1`, la version, les réglages d'Argon2id et le sel ;
2. la clé maîtresse enveloppée (IV, marque, 32 octets) ;
3. les enregistrements, chacun : sorte, identité, nom, IV, marque, valeur chiffrée.

Le fichier est écrit à côté puis renommé : jamais à moitié écrit.

vfs refuse `/var/keyvault` et `/etc/keyvault` à tout autre que l'uid 0, **même en mode `access = log`**
(`src/svc/vfs/access.c`). Les autres fichiers à secrets sont protégés en mode `enforce` seulement : `shadow.ini`,
`wifi.txt`, `wifi.bak`, `remote.txt`, `smbd.ini`, `smbfs.ini`, `/etc/web/certs`.

## 6. Ce qui y est rangé

| Programme | Secret au coffre | Avant |
|---|---|---|
| oauthd | `oauth` : jetons de rafraîchissement et `client_secret` des comptes | `HOME:/oauth/secrets` + `key` |
| Mail | `mail` : mots de passe IMAP/SMTP, jetons | `HOME:/mail/secrets` + `key` |
| Claude, Vibe (apps) | `env/NOM_API_KEY` : les lignes `*_API_KEY=` et `*_AUTH_TOKEN=` du `.env` de l'agent, rendues à l'agent dans son environnement à son lancement | `.env` en clair |
| Koton, PocketQI, IRC, Forge, Telegram, Budget, Courier (`{{vault:nom}}`) | voir leurs sources | fichiers de `HOME:` en clair |
| telnetd, ftpd, vncd, rdpd (`@system`) | le mot de passe d'accès à distance | `/etc/remote.txt` |
| wifisvc (`@system`) | les phrases WPA2 | `/etc/wifi.txt`, `wifi.bak` |
| smbfs, smbd, webd (`@system`) | mots de passe des partages, comptes du serveur, clés TLS | `/etc/smbfs.ini`, `smbd.ini`, `/etc/web/certs/*.key` |

Chaque programme fait passer une fois l'ancienne valeur au coffre, puis l'efface de son fichier. Jet (cookies
SQLite) n'est pas concerné.

## 7. Le TPM 2.0

`src/svc/keyvault/tpm.c`. Le TPM est trouvé par la table ACPI `TPM2`, et seule l'interface **CRB** (méthode 7)
est prise : c'est celle des fTPM d'Intel (PTT du ZBook) et d'AMD. Sans elle, la clé de la machine reste en
fichier.

Fonctionnement :
- **La clé primaire** de stockage (ECC P-256) est refaite à chaque démarrage sous la hiérarchie du propriétaire.
- **L'objet scellé** (keyedhash) contient la clé de la machine. `Load` puis `Unseal` la rendent.

Pas de lien aux PCR :
- **PCR 7** : Korin démarre sans Secure Boot, et le PCR 7 est le même pour tout système démarré ainsi.
- **PCR 4** : il change à chaque `korin.efi`.

Le TPM protège donc du disque emporté seul ; la machine emportée est protégée par le mot de passe.

**Un TPM effacé** (BIOS, carte mère) perd les coffres.

## 8. Construire et essayer

```bash
wsl sh tools/pc/apps.sh keyvault               # sys/svc/keyvault et bin/secret (OpenSSL)
wsl sh src/svc/keyvault/test/run.sh            # le pilote TPM sous Linux contre swtpm (apt install swtpm)
```

Vérifié sous QEMU (image d'essai NVMe, `tools/pc/qemu_nvme.py --boot-disk`, script lancé par `startup.txt`) :

| Domaine | Vérifié |
|---|---|
| rangement | coffre créé au premier secret ; `set`, `get`, `ls`, `rm` ; aucun secret en clair dans le fichier |
| séparation | deux scripts ne voient pas leurs secrets ; noms invalides refusés |
| mot de passe | coffre ouvert à la connexion par mot de passe ; `passwd` réenveloppe (l'ancien refusé, le nouveau accepté, secrets gardés) |
| cartes | coffre fermé : carte du mot de passe ; programme de `HOME:` recompilé : carte d'accord, accord retenu |
| ensemble (tout reconstruit, migrations comprises) | démarrage et services sans erreur ; `secret` ; valeur de plus de 64 Kio refusée ; `git-credential-korin` (`store`, `get`, `erase`) ; `/var/keyvault` et `machine.key` refusés à l'uid 1000 |

Contre swtpm : scellement, descellement, reprise de `machine.key`, fichier abîmé refusé.

Pas encore essayé :
- le ZBook ;
- le vrai TPM (CRB) ;
- bash, CPython et dotnet reconstruits avec la déclaration du script, sauf bash ;
- les migrations des apps et des services en marche (sous QEMU, telnetd ne lit `remote.txt` qu'à une connexion :
  la reprise du mot de passe n'a pas pu se voir).

## 9. Au plan : un secret par clé, liste filtrée (demande du 18/11/2026)

Aujourd'hui Koton range **toutes** ses clés d'IA dans un seul secret, `ai-keys` (un tableau JSON : fournisseur,
nom, clé, dernière choisie) ; la liste des clés est donc dans ce secret (et dans `settings.json` sans coffre).
Ce qui est voulu :

1. **Une clé = un secret, nommé `<Fournisseur>.<Nom>`** : la clé « Clé1 » de Gemini devient `Gemini.Cle1`, la
   « Clé2 » `Gemini.Cle2`, la « Clé1 » de Claude `Claude.Cle1`. Le nom affiché est celui que l'utilisateur a tapé.
2. **Lister avec un filtre** : `kv_list_match (motif, tampon, taille)` rend les noms de ce programme qui suivent
   le motif (`Gemini.*`, `*` et `?` comme dans un shell), un par ligne ; AppKit `vault_list_match` (à la fin de la
   table) ; `secret ls MOTIF`. Le filtre se fait dans le coffre (les autres noms ne sortent pas). `kv_list` reste.
3. **La liste n'est plus gardée** dans les réglages de l'app : à l'ouverture de la liste déroulante des clés,
   l'app demande `Gemini.*` (ou `Claude.*`, `Mistral.*`…) et affiche ce qui revient. Les réglages ne gardent que
   le nom de la clé choisie par fournisseur (pas la clé).
4. **Les noms (décision du 18/11)** : **tout nom de clé en UTF-8** est accepté (accents, espaces, ponctuation,
   `.`, `*`, `?` compris), jusqu'à 128 octets. Seuls les caractères de contrôle (octet nul, saut de ligne…) restent
   refusés : ils ne se tapent pas dans un champ et `kv_list` rend un nom par ligne. Le **premier** `.` sépare le
   fournisseur (ASCII, sans `.`) du nom : `Gemini.Clé 1.bis` = fournisseur `Gemini`, nom `Clé 1.bis`. Dans un motif,
   `\*`, `\?` et `\\` désignent les caractères eux-mêmes (`kv_list_match` ; un nom qui en contient reste listé par
   `Gemini.*`). Le coffre vérifie que le nom est de l'UTF-8 valide. Les noms des autres programmes gardent les mêmes
   règles (élargies pour tous).
5. **Koton** : **pas de migration** (décision du 18/11 : installation neuve, rien dans les réglages de Koton) ;
   `ai-keys` et la migration depuis `settings.json` (`loadVault`, `legacyKeys`) sont retirés. Ajouter, renommer
   (= nouveau secret puis effacer l'ancien), supprimer une clé.
6. **Les autres apps** qui ont plusieurs clés ou comptes suivent la même règle (PocketQI d'abord ; Mail, IRC,
   Telegram si elles en ont plusieurs). Le petit code commun (lister un fournisseur, nommer, renommer) va dans
   AppKit ou UIKit (une liste déroulante « clés du coffre »), pas dans chaque app.
7. **Sans coffre** (le Pi) : comme aujourd'hui, la liste dans les réglages de l'app.

