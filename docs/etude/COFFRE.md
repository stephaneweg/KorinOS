# Étude : un coffre à secrets (keyvault)

État (18/11/2026) : **étude seulement, rien n'est fait.** Demande de l'utilisateur : recenser les programmes qui
gardent un secret en clair (jetons OAuth, clés d'API, mots de passe…), puis tout ranger chiffré dans un coffre que
seul le service du coffre sait lire. Un programme dépose un secret sous un nom ; le coffre le range sous
(identité du programme, nom) ; pour le relire, le programme donne le nom et le coffre cherche avec son identité.
Question ouverte de l'utilisateur : **où garder la clé du coffre lui-même** (§4).

## 0. Résumé

- **17 programmes** gardent un secret sur la carte ; **15 en clair** (§1). Seuls Mail et oauthd chiffrent, mais
  leur clé est posée à côté du fichier chiffré (`HOME:/mail/key`, `HOME:/oauth/key`).
- Aujourd'hui, **tout programme de l'utilisateur lit tous ses secrets** : `HOME:` est en FAT32/exFAT, les modes
  `0600` ne sont pas gardés, et les règles de vfs sont en `access = log` (rien n'est refusé).
- Le coffre proposé : un service **`keyvault`** (C, libposix + OpenSSL, `uid 0`), des fonctions `vault_*` dans
  AppKit, une commande `/bin/secret`. Une entrée = (uid, identité de l'app, nom) → valeur, chiffrée en
  AES-256-GCM (§3).
- **Deux manques du noyau** à combler pour que « l'identité de l'app » vaille quelque chose (§2) : le chemin de
  l'exécutable n'est pas attesté (c'est AppKit qui lit le fichier et le déclare), et les scripts (Python, bash,
  .NET) ont tous l'identité de leur interprète.
- **La clé** (§4) : une clé maîtresse aléatoire par utilisateur, enveloppée par une **clé de la machine**
  (TPM 2.0 sur le PC, OTP « device private key » sur le Pi 4, à défaut un fichier de `/etc` lisible par l'uid 0
  seul) et, au choix du compte, par le **mot de passe de session** (comme le trousseau de macOS ou GNOME
  Keyring), plus une **clé de secours** imprimable.

### Rapport avec la décision du 29/10 (pas de sécurité contre un attaquant)

Ce chantier est surtout de la sécurité contre un attaquant (carte volée, app malveillante qui lit les clés d'API) :
il revient sur la décision du 29/10 pour ce domaine, comme le multi-utilisateur l'a fait pour les fichiers. Il a
aussi un intérêt contre les erreurs, sans attaquant :
- `/home` est partagé par ftpd, smbd et webd, copié par les sauvegardes et envoyé tel quel dans un rapport ou une
  capture : aujourd'hui, les clés d'API partent avec ;
- un agent IA qui lit `HOME:` (Vibe, Claude) voit les `.env` et `settings.json` qui contiennent des clés ;
- un secret recopié d'un compte vers un autre par `osinstall` ou par un copier-coller de dossier.

## 1. Recensement

« Lecture » = qui peut le lire aujourd'hui. Avec `access = enforce` (étape 5 du multi-utilisateur), seuls
`/etc/shadow.ini`, `/etc/wifi.txt`, `/etc/remote.txt` et `/etc/smbd.ini` sont cachés aux comptes sans droits
(`src/svc/vfs/access.c`).

### 1.1 Système (`/etc`, services)

| Programme | Secret | Où, comment | Lecture |
|---|---|---|---|
| telnetd, ftpd, vncd, rdpd | mot de passe d'accès à distance (un seul pour les quatre) | `/etc/remote.txt`, `password = …` en clair ; lu par `src/svc/inc/remotepw.c` et `src/inc/remotepw.inc` ; écrit par Réglages › Accès à distance et Setup | tous (enforce : uid 0) |
| wifisvc (PC), wifi (Pi) | phrases WPA2 | `/etc/wifi.txt` **et `/etc/wifi.bak`**, `key="…"` en clair ; recopiés par `osinstall` | tous (enforce : `wifi.bak` reste ouvert) |
| smbfs (client) | comptes des partages | `/etc/smbfs.ini`, `password = …` en clair, écrit par Fichiers | **tous, même en enforce** |
| smbd (serveur) | comptes du partage | `/etc/smbd.ini` `[users] nom = mot de passe` en clair | tous (enforce : uid 0) |
| webd | clés privées TLS (P-256) | `/etc/web/certs/*.key`, PEM non chiffré | **tous, même en enforce** |
| oauthd | `client_secret` des fournisseurs | `/etc/oauth/providers.ini` en clair (Google : valeurs remplies à la construction) | tous ; un `client_secret` d'application de bureau ne protège rien (voir `docs/OAUTH.md`) |
| installateur | empreinte PBKDF2 des mots de passe | `/tmp/install/plan.ini` (`hash =`, `root_hash =`), recopié dans `shadow.ini` | tous, pendant l'installation |
| authd | mots de passe des comptes | `/etc/shadow.ini`, **empreinte** `pbkdf2-sha256` (pas en clair) | enforce : uid 0 — **correct** |

### 1.2 Applications (`HOME:`)

| App | Secret | Fichier | Remarque |
|---|---|---|---|
| Mail | mots de passe IMAP/SMTP, jetons Microsoft | `HOME:/mail/secrets` (AES-256-GCM) | **chiffré**, clé dans `HOME:/mail/key` |
| oauthd | jetons de rafraîchissement | `HOME:/oauth/secrets` (AES-256-GCM) | **chiffré**, clé dans `HOME:/oauth/key` ; `client_secret` d'un compte en clair dans `accounts.ini` ; tout programme peut demander le jeton de tout compte |
| Claude (app) et claude-acp | clé d'API Anthropic | `HOME:/.claude-acp/.env` | en clair, écrit par l'app (C), lu par claude-acp (Python) |
| Vibe et vibe-acp | clés Mistral, Moonshot, compatibles OpenAI | `HOME:/.vibe/.env` | en clair, écrit par `vibe_account.py` |
| Koton | clés Gemini, Groq, Mistral, Claude… | `HOME:/koton/settings.json` | en clair (l'infobulle le dit) |
| PocketQI | clés Mistral, Groq, URL perso | `HOME:/pocketqi/config.ini` | en clair (mode 0600 sans effet sur FAT) |
| Courier | jetons Bearer, Basic, clés d'API des collections ; cookies | `HOME:/courier/collections/*.json`, `cookies.json` | en clair ; format Postman, à garder lisible par Postman |
| Jet | cookies de session, stockage des sites | `HOME:/jet/webkit/cookies.db` (SQLite)… | en clair ; pas de mots de passe enregistrés |
| Telegram | clés d'autorisation MTProto (2048 bits par DC) | `HOME:/telegram/session.dat` (hex) | en clair : qui a le fichier a la session |
| IRC | mot de passe du serveur | `HOME:/irc/config.ini` | en clair |
| Forge | jeton du serveur MCP | `HOME:/forge/mcp.txt` | en clair ; jeton tiré de l'heure et d'une adresse, `/dev/urandom` seulement s'il existe |
| Budget | clé privée RSA (Enable Banking), session bancaire | `HOME:/budget/banque.json` + `.pem` non chiffré | en clair ; donne l'accès aux comptes en banque |
| git (si l'utilisateur active `credential-store`) | jetons | `~/.git-credentials` | en clair ; pas d'assistant de Korin |

Hors carte (rien à faire) : `../korin-secrets.ini` et la clé de signature des paquets restent sur le PC de
développement ; le client Windows d'accès à distance garde le mot de passe par DPAPI.

### 1.3 Ce qui existe déjà

- **Crypto** : OpenSSL 3.5.9 (AES-GCM, HKDF, PBKDF2, **Argon2id**, RSA, EC) pour tout programme C/C++ sur libposix ;
  SHA-256/HMAC/PBKDF2 sans OpenSSL dans `src/svc/authd/pwhash.h`.
- **Hasard** : `SYS_GETRANDOM` (ChaCha20 ; RNG200 sur le Pi, `rdrand` sur le PC), vu par `getentropy`,
  `/dev/urandom` et `RAND_bytes`.
- **Qui appelle** : le noyau écrit le pid de l'appelant dans chaque message (`MSG_PID`) ; `SYS_PROC_UID(pid)`
  donne son uid ; `SYS_PROC_RIGHTS(pid)` dit s'il est de `/sys`. Un service ne peut pas être usurpé :
  `SVC_REGISTER` demande les droits.

## 2. L'identité d'une app

La demande : « l'identifiant de l'app, par exemple son chemin exécutable ». Aujourd'hui le noyau ne le connaît pas
de façon sûre :
- `struct process.name[32]` est donné par le lanceur ;
- le chargement est fait par **AppKit dans le process lanceur** : `IMAGE_CREATE(chemin)` rend des pages vides,
  AppKit les remplit lui-même puis `IMAGE_SEAL`. Le chemin est déclaré, pas vérifié ; aucun appel ne le rend.

### 2.1 Proposition : un chemin attesté par vfs

1. `IMAGE_CREATE` garde le chemin résolu (il le fait déjà : `image->path[80]`) mais marque l'image **non attestée**.
2. Nouvelle opération de vfs `V_LOAD_IMAGE(chemin, fenêtre)` : vfs lit le fichier et écrit lui-même dans les pages
   de l'image (comme `SVC_MAP_CALLER`), puis demande au noyau de la marquer **attestée** (appel réservé aux
   process à droits). AppKit passe par là au lieu de lire et copier.
3. `SPAWN_IMAGE` / `SPAWN_PRELOADED` recopient dans `struct process` le chemin et le drapeau.
4. Nouvel appel `SYS_PROC_EXE(pid, tampon)` → chemin + drapeau « attesté ». Le coffre refuse une image non
   attestée (ou la range sous l'uid seul, §3.3).

Coût : un aller-retour de vfs en moins (vfs écrit directement dans l'image au lieu de passer par le lanceur) ; ce
changement sert aussi la performance. Le Pi suit avec le même numéro.

### 2.2 Le chemin ou le paquet ?

Le chemin seul casse dans trois cas :
- **une app déplacée ou renommée** perd ses secrets ;
- **deux programmes d'une même app** : Claude (`/apps/claude.app/claude`, C) écrit la clé, claude-acp (Python) la
  lit ; Vibe pareil ; Mail et oauthd ;
- **les interprètes** : Python, bash et `dotnet` ont tous le chemin de l'interprète. Courier, le Studio, toute app
  C# et claude-acp seraient la même « app ».

Proposition : l'identité est le **nom du paquet** quand l'exécutable appartient à un paquet installé (pkgd tient
déjà la liste des fichiers de chaque paquet, et les paquets du dépôt sont signés) ; le chemin attesté sinon (un
programme compilé à la main dans `HOME:`). Un paquet peut déclarer dans son `app.txt` un groupe de coffre
(`vault = claude`) que partagent plusieurs paquets du même auteur.

Pour les interprètes, deux voies :
- **un lanceur natif** par app (`apphost`, comme .NET le fait déjà sous Windows) : `/apps/courier.app/courier`
  est un petit binaire du paquet qui charge `dotnet` dans son process ; l'identité est celle du lanceur ;
- sinon le coffre range les secrets d'un script sous **l'uid seul** (pas de séparation entre scripts d'un même
  compte) ; c'est la limite de macOS et de GNOME Keyring aussi.

## 3. Le service

### 3.1 Forme

- **`/sys/svc/keyvault`**, service « vault », C sur libposix + OpenSSL (comme oauthd), lancé par init2 en uid 0.
- **API dans AppKit** (fin de table) : `vault_set(nom, valeur, taille)`, `vault_get(nom, tampon, taille)`,
  `vault_delete(nom)`, `vault_list(tampon, taille)`. Le programme ne donne **jamais** son identité : le coffre la
  prend du message (`MSG_PID` → uid, `SYS_PROC_EXE`, paquet).
- **Commande `/bin/secret`** (`secret set|get|rm|ls NOM`) pour bash, Python et les outils qui veulent une variable
  d'environnement (`ANTHROPIC_API_KEY=$(secret get anthropic)`). Son identité est celle de… `/bin/secret` : pour
  ne pas ouvrir une porte à tout script, `secret` ne voit que l'espace « partagé » du compte (§3.3), et le coffre
  demande une confirmation à l'utilisateur la première fois qu'un programme lit un secret d'un autre espace.
- **Module Python** `korin.vault` (sur AppKit par ctypes ou cffi) pour claude-acp et vibe-acp.

### 3.2 Rangement

- `/var/vault/<uid>.db` : un fichier par compte, **hors de `HOME:`** (pas partagé par ftpd/smbd, pas copié avec le
  dossier personnel), réservé à l'uid 0 dans les règles de vfs (efficace avec `enforce`).
- Chaque entrée est chiffrée à part en AES-256-GCM avec la clé maîtresse du compte ; les **données associées**
  (AAD) sont `uid | identité | nom` : une entrée recopiée sous un autre nom ou un autre compte ne se déchiffre pas.
- Écrit à côté puis renommé (comme oauthd), jamais à moitié.
- Taille d'une valeur : 64 Ko au plus (une clé privée PEM, une session Telegram tiennent).

### 3.3 Espaces et droits

| Espace | Qui lit | Exemples |
|---|---|---|
| app (`paquet` ou chemin attesté) | les exécutables de ce paquet, ou ce chemin | clé d'API de Koton, session Telegram, clé RSA de Budget |
| groupe (`vault = …` dans `app.txt`) | les paquets qui le déclarent | `claude` : app Claude + claude-acp |
| partagé du compte | tout programme du compte, après un premier accord | jetons git, clés d'API utilisées par plusieurs outils |
| système (uid 0) | les services de `/sys` | Wi-Fi, accès à distance, smbd, smbfs, clés TLS de webd |

oauthd devient **un client du coffre** : il garde ses jetons de rafraîchissement dans son espace, et reste le seul
à les donner (jetons d'accès) ; il peut enfin dire quels programmes ont droit à quel compte OAuth.

## 4. La clé du coffre

C'est la vraie question : un coffre dont la clé est posée à côté (ce que font Mail et oauthd aujourd'hui) ne
protège qu'un fichier copié seul. Trois menaces, trois réponses :

| Menace | Ce qui protège |
|---|---|
| une app du compte lit les secrets d'une autre | le service (§3.3) et un fichier réservé à l'uid 0 — **la clé n'y joue aucun rôle** |
| la carte ou le disque emporté seul, ou lu par Windows sur le ZBook | une clé qui **n'est pas sur le disque** : TPM, OTP du Pi |
| la machine entière emportée, ou démarrée sur un autre système | une clé **dérivée du mot de passe** de l'utilisateur |

### 4.1 Hiérarchie proposée

```
mot de passe de session ──Argon2id──▶ KEK_mdp ─┐
clé de la machine (TPM / OTP / fichier) ───────┼──enveloppent──▶ clé maîtresse du compte (256 bits, aléatoire)
clé de secours (24 caractères imprimés) ───────┘                          │
                                                                          ▼
                                                       AES-256-GCM de chaque entrée de /var/vault/<uid>.db
```

- La **clé maîtresse** de chaque compte est tirée au hasard une fois et ne change jamais ; elle est rangée
  plusieurs fois, **enveloppée** par chacune des clés ci-dessous (en-tête de `<uid>.db`). Changer de mot de passe
  ou de machine ne rechiffre que l'enveloppe, pas les entrées.
- La clé maîtresse ne vit qu'en mémoire du coffre, une fois déverrouillée.

### 4.2 La clé de la machine

| Machine | Source | Ce qu'elle apporte | Travail |
|---|---|---|---|
| PC (ZBook) | **TPM 2.0** : une clé scellée dans le TPM, rendue au coffre seulement | le disque lu ailleurs (ou par Windows) ne donne rien ; liée aux PCR 7 (Secure Boot) et 4 (`korin.efi`), elle ne sort que pour Korin | un pilote TPM (interface CRB/TIS en MMIO, table ACPI `TPM2`) et quelques commandes TPM2 (`CreatePrimary`, `Create`, `Load`, `Unseal`) ; liée aux PCR, une mise à jour de `korin.efi` demande de resceller |
| Pi 4 | **OTP « device private key »** : 256 bits gravés une fois dans la puce (propriétés de la boîte aux lettres `0x00030081` lire, `0x00038081` écrire) | la carte SD emportée seule ne donne rien | un appel du noyau réservé au coffre ; **écriture irréversible**, à faire une fois, avec accord ; le Pi n'ayant pas de démarrage sécurisé, une autre carte dans ce Pi la lit |
| les deux, à défaut | fichier `/etc/vault/machine.key`, réservé à l'uid 0 | une app ne la lit pas (avec `enforce`) ; le disque emporté, si | aucun : c'est l'étape 1 |

Le noyau garde la clé de la machine : l'appel qui la rend (`SYS_MACHINE_KEY`) n'est permis qu'au process à droits
dont le chemin attesté est `/sys/svc/keyvault`. Et le coffre ne garde que la clé maîtresse déchiffrée, jamais la clé
de la machine.

### 4.3 Le mot de passe de session

- Au choix du compte (Réglages › Utilisateurs) : **« déverrouiller à la connexion »**. La clé maîtresse n'est alors
  enveloppée que par le mot de passe et la clé de secours (pas par la clé de la machine seule).
- `/sys/login` et authd vérifient déjà le mot de passe (`AUTH_VERIFY`) : authd le passe au coffre (`VAULT_UNLOCK`,
  service à service) qui dérive la KEK par **Argon2id** (OpenSSL 3.5) et ouvre la clé maîtresse ; le mot de passe
  n'est jamais gardé.
- `passwd` (`AUTH_SETPW` avec l'ancien mot de passe) → le coffre réenveloppe. Un administrateur qui **remet** un mot
  de passe sans l'ancien ne peut pas : le coffre de ce compte reste fermé jusqu'à la clé de secours.
- Le **verrou** (Super+L, à faire) peut refermer le coffre.
- Connexion automatique sans mot de passe : seul le mode « clé de la machine » est possible (c'est le cas du Pi
  aujourd'hui).
- Les services (uid 0) n'ont pas de mot de passe : leur coffre est toujours sous la clé de la machine.

### 4.4 La clé de secours

24 caractères (120 bits en base32) montrés une fois à la création du coffre, à noter ou imprimer (PrinterKit). Elle
sert quand le TPM est effacé, la carte du Pi changée, ou le mot de passe remis par un administrateur. Sans elle,
dans ces cas, **les secrets sont perdus** (on se reconnecte aux services ; aucune donnée personnelle n'est
perdue, seulement des jetons et des clés).

## 5. Ce que devient chaque programme

| Programme | Après |
|---|---|
| oauthd, Mail | `secrets` + `key` → espace de l'app dans le coffre ; lecture de l'ancien format puis effacement |
| claude, claude-acp, vibe, vibe-acp | `.env` → groupe `claude` / `vibe` ; claude-acp lit par `korin.vault`, garde `ANTHROPIC_API_KEY` de l'environnement en priorité |
| Koton, PocketQI, IRC, Forge | clés et mots de passe → espace de l'app ; le fichier de réglages garde le reste |
| Telegram | `session.dat` entier → espace de l'app (≤ 64 Ko) |
| Budget | clé privée RSA importée dans le coffre ; `KeyPath` n'est plus qu'un import |
| Courier | les collections restent au format Postman ; une variable `{{vault:nom}}` remplacée à l'envoi, et un bouton « Garder dans le coffre » |
| wifisvc, wifi (Pi) | `key=` → espace système ; `wifi.txt` garde SSID et priorité ; `wifi.bak` disparaît |
| telnetd, ftpd, vncd, rdpd | `remote.txt` → espace système (vncd a besoin du mot de passe en clair pour DES : le coffre le lui rend) ; plus tard, comptes d'authd |
| smbfs | `password` de `smbfs.ini` → espace système, par `[partage]` |
| smbd | `[users]` → empreinte NT dans `shadow.ini` (déjà prévu par le multi-utilisateur) |
| webd | clés TLS → espace système ; les certificats (publics) restent dans `/etc/web/certs` |
| installateur | l'empreinte passe à `osinstall` par un tube, plus par `/tmp/install/plan.ini` |
| git | un assistant `git-credential-korin` sur `/bin/secret` |
| Jet | hors de ce chantier (les cookies sont une base SQLite) ; plus tard : `cookies.db` chiffré par une clé du coffre |

## 6. Plan par étapes

1. **Coffre minimal** : keyvault, `vault_*` dans AppKit, `/bin/secret`, clé de la machine en fichier
   (`/etc/vault/machine.key`), identité = uid seul. Passer oauthd et Mail dessus. Ajouter `/var/vault` et
   `/etc/vault` aux règles de vfs.
2. **Identité des apps** : `V_LOAD_IMAGE` dans vfs, image attestée, `SYS_PROC_EXE` (PC puis Pi) ; paquets et
   groupes (`vault =` dans `app.txt`) ; lanceurs natifs pour les apps .NET et Python.
3. **Migrer** les apps (§5), d'abord les clés d'API (Claude, Vibe, Koton, PocketQI), puis le système (Wi-Fi,
   accès à distance, smbfs, webd).
4. **Mot de passe de session** (Argon2id, authd → coffre, `passwd`, verrou) et **clé de secours**.
5. **Clé de la machine matérielle** : TPM 2.0 sur le PC, OTP sur le Pi.
6. **`access = enforce`** (étape 5 du multi-utilisateur) : sans elle, tout ce qui précède reste lisible sur le
   disque ; le chiffrement tient, mais la séparation entre apps ne passe que par le service.

## 7. Questions pour l'utilisateur

1. **Menace visée** : contre les apps seulement (étapes 1 à 3 suffisent), contre le disque emporté (+ étape 5),
   ou aussi contre la machine emportée (+ étape 4) ? C'est un retour sur la décision du 29/10 pour ce domaine.
2. **Identité** : le paquet quand il y en a un (proposé), ou le chemin seul comme dans la demande ?
3. **Scripts et .NET** : lanceur natif par app (séparation réelle) ou uid seul pour tout ce qui passe par un
   interprète ?
4. **Mot de passe de session** : mode proposé par défaut pour un compte qui en a un, ou laissé en option ?
5. **OTP du Pi** : l'écriture est définitive sur la carte mère ; d'accord pour la faire (avec la clé de secours), ou
   garder le fichier sur le Pi ?
6. **Nom** : `keyvault` (service « vault », `vault_*`, `/bin/secret`) ?
