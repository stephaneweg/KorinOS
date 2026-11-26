# Étude : un coffre à secrets (keyvault)

État (18/11/2026) : **fait sur le PC** (référence : [../KEYVAULT.md](../KEYVAULT.md)), vérifié sous QEMU ; le TPM
essayé sous Linux contre swtpm seulement, pas encore sur le ZBook. Le Pi plus tard.

Demande de l'utilisateur : recenser les programmes qui gardent un secret en clair (jetons OAuth, clés d'API, mots de
passe…), puis tout ranger chiffré dans un coffre que seul le service du coffre sait lire. Un programme dépose un
secret sous un nom ; le coffre le range sous (identité du programme, nom) ; pour le relire, le programme donne le
nom et le coffre cherche avec son identité. Question ouverte de l'utilisateur : **où garder la clé du coffre** (§4).

Décisions de l'utilisateur (18/11), reportées dans l'étude :
- **menaces couvertes** : le vol d'un secret par un autre exécutable, le vol du disque, le vol de la machine ;
- **identité = chemin** de l'exécutable (marche aussi pour les apps compilées sur la machine) ;
- **scripts et .NET** : l'interprète connaît le chemin du script ou de l'assembly, il le donne ;
- **mot de passe de session par défaut** ;
- **pas de clé de secours** pour l'instant ;
- **le Pi plus tard** ; nom **`keyvault`**.

## 0. Résumé

- **17 programmes** gardent un secret sur la carte ; **15 en clair** (§1). Seuls Mail et oauthd chiffrent, mais
  leur clé est posée à côté du fichier chiffré (`HOME:/mail/key`, `HOME:/oauth/key`).
- Aujourd'hui, **tout programme de l'utilisateur lit tous ses secrets** : `HOME:` est en FAT32/exFAT, les modes
  `0600` ne sont pas gardés, et les règles de vfs sont en `access = log` (rien n'est refusé).
- Le coffre : un service **`keyvault`** (C, libposix + OpenSSL, uid 0), des fonctions `vault_*` dans AppKit, une
  commande `/bin/secret`. Une entrée = (uid, chemin du programme, nom) → valeur, chiffrée en AES-256-GCM (§3).
- **Deux manques du noyau** (§2) : le chemin de l'exécutable n'est pas attesté (c'est AppKit qui lit le fichier et
  le déclare), et les scripts n'ont aujourd'hui que le chemin de leur interprète.
- **La clé** (§4) : une clé maîtresse aléatoire par compte, enveloppée par une clé tirée **à la fois** du mot de
  passe de session (Argon2id) et d'un secret scellé dans le **TPM 2.0** du PC. Le disque seul ne donne rien (il
  manque le TPM), la machine seule non plus (il manque le mot de passe).

### Rapport avec la décision du 29/10 (pas de sécurité contre un attaquant)

Ce chantier est de la sécurité contre un attaquant : il revient sur la décision du 29/10 pour ce domaine, comme le
multi-utilisateur l'a fait pour les fichiers (décision de l'utilisateur, 18/11). Il sert aussi sans attaquant :
`/home` est partagé par ftpd, smbd et webd, copié par les sauvegardes, lu par les agents IA (Vibe, Claude) ;
aujourd'hui les clés d'API partent avec.

## 1. Recensement

« Lecture » = qui peut le lire aujourd'hui. Avec `access = enforce` (étape 5 du multi-utilisateur), seuls
`/etc/shadow.ini`, `/etc/wifi.txt`, `/etc/remote.txt` et `/etc/smbd.ini` sont cachés aux comptes sans droits
(`src/svc/vfs/access.c`).

### 1.1 Système (`/etc`, services)

| Programme | Secret | Où, comment | Lecture |
|---|---|---|---|
| telnetd, ftpd, vncd, rdpd | mot de passe d'accès à distance (un seul pour les quatre) | `/etc/remote.txt`, `password = …` en clair ; lu par `src/svc/inc/remotepw.c` et `src/inc/remotepw.inc` ; écrit par Réglages › Accès à distance et Setup | tous (enforce : uid 0) |
| wifisvc (PC), wifi (Pi) | phrases WPA2 | `/etc/wifi.txt` **et `/etc/wifi.bak`**, `key="…"` en clair ; recopiés par `osinstall` | tous (enforce : `wifi.bak` reste ouvert) |
| smbfs (client) | comptes des partages | `/etc/smbfs.ini`, `password = …` en clair, écrit par Fichiers | ~~tous, même en enforce~~ uid 0 seulement en enforce (`secrets[]` de `src/svc/vfs/access.c`) ; Fichiers l'écrit élevé (26/11/2026) |
| smbd (serveur) | comptes du partage | `/etc/smbd.ini` `[users] nom = mot de passe` en clair | tous (enforce : uid 0) |
| webd | clés privées TLS (P-256) | `/etc/web/certs/*.key`, PEM non chiffré | ~~tous, même en enforce~~ uid 0 seulement en enforce (`secrets[]` de `access.c`) ; le volet Serveur web agit élevé (26/11/2026) |
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

## 2. L'identité d'un programme : son chemin

### 2.1 Ce qui manque : un chemin attesté

Aujourd'hui le noyau ne connaît pas le chemin d'un exécutable de façon sûre :
- `struct process.name[32]` est donné par le lanceur ;
- le chargement est fait par **AppKit dans le process lanceur** : `IMAGE_CREATE(chemin)` rend des pages vides,
  AppKit les remplit lui-même puis `IMAGE_SEAL`. Le chemin est déclaré, pas vérifié ; aucun appel ne le rend.

Proposition :
1. `IMAGE_CREATE` garde le chemin résolu (il le fait déjà : `image->path[80]`) mais marque l'image **non attestée**.
2. Nouvelle opération de vfs `V_LOAD_IMAGE(chemin, fenêtre)` : vfs lit le fichier et écrit lui-même dans les pages
   de l'image, puis demande au noyau de la marquer **attestée** (appel réservé aux process à droits). AppKit passe
   par là au lieu de lire et copier (un passage de données en moins : sert aussi la performance).
3. `SPAWN_IMAGE` / `SPAWN_PRELOADED` recopient dans `struct process` le chemin et le drapeau.
4. Nouvel appel `SYS_PROC_EXE(pid, tampon)` → chemin de l'exécutable, chemin du script (§2.2), drapeau « attesté ».
   Le coffre refuse un process dont l'image n'est pas attestée.

### 2.2 Scripts et assemblies .NET : le chemin donné par l'interprète

L'interprète (Python, bash, `dotnet`, la VM BASIC) sait quel script ou quelle assembly il exécute : c'est lui qui
donne ce chemin. Pour qu'un script ne puisse pas se faire passer pour un autre :

1. Nouvel appel `SYS_PROC_SET_SCRIPT(chemin)` : **une seule fois par process**, avant d'exécuter le moindre code
   du script. Un second appel rend `E_PERM` ; `exec` remet à zéro.
2. L'interprète l'appelle au démarrage, après avoir résolu le chemin (`python3 x.py`, `#!/bin/bash`,
   `dotnet app.dll`, apphost .NET).
3. Le coffre ne croit ce chemin que si **l'exécutable attesté** du process est un interprète connu :
   `/etc/keyvault/interpreters.ini` (uid 0), par exemple `/devtools/python/bin/python3`, `/bin/bash`,
   `/devtools/dotnet/dotnet`. L'identité est alors le chemin du script ; sinon celui de l'exécutable.

Limites notées :
- un script qui importe le code d'un autre le fait tourner sous sa propre identité (voulu) ;
- `python3 -c "…"` ou un bash interactif n'ont pas de script : identité = l'interprète, qui n'a accès à rien
  (le coffre refuse l'espace d'un interprète nu) ;
- les interprètes liés statiquement (bash, Python) doivent être reconstruits avec la nouvelle libposix.

### 2.3 Ce que le chemin implique

- **Une app déplacée ou renommée** perd l'accès à ses secrets (elle les redemande à l'utilisateur).
- **Plusieurs programmes d'une même app** (Claude en C et claude-acp en Python, Mail et oauthd) : une app déclare
  dans son `app.txt` `vault = dossier` ; le coffre range alors ses secrets sous le **dossier de l'app**
  (`/apps/claude.app`) et tout exécutable ou script de ce dossier y a accès.
- **Binaire remplacé** : sous `/apps` et `/bin`, seul l'utilisateur système écrit (avec `access = enforce`). Mais une
  app compilée dans `HOME:` peut être écrasée par n'importe quel programme du même compte, qui prendrait son
  identité. Parade proposée : le coffre retient l'empreinte SHA-256 de l'image (calculée par vfs au chargement) à
  la première écriture ; si elle change, une carte (comme celle d'authd) demande « *chemin* a changé (nouvelle
  compilation ?) : lui laisser ses secrets ? ». Pour `/apps` et `/bin`, pas de question : pkgd prévient le coffre
  à chaque mise à jour.

## 3. Le service

### 3.1 Forme

- **`/sys/svc/keyvault`**, service « keyvault », C sur libposix + OpenSSL (comme oauthd), lancé par init2 en uid 0.
- **API dans AppKit** (fin de table) : `vault_set(nom, valeur, taille)`, `vault_get(nom, tampon, taille)`,
  `vault_delete(nom)`, `vault_list(tampon, taille)`. Le programme ne donne **jamais** son identité : le coffre la
  prend du message (`MSG_PID` → `SYS_PROC_UID`, `SYS_PROC_EXE`).
- **Commande `/bin/secret`** (`secret set|get|rm|ls NOM`). Lancée depuis un script, son identité serait
  `/bin/secret` : elle demande donc au coffre l'espace **du process parent** (le script), que le coffre vérifie
  (`SYS_PROC_EXE` du parent, même uid). Ainsi `ANTHROPIC_API_KEY=$(secret get anthropic)` dans un script bash lit
  l'espace de ce script.
- **Module Python** `korin.vault` (sur AppKit par ctypes) pour claude-acp et vibe-acp ; classe .NET
  `Korin.Vault` pour les apps C#.

### 3.2 Rangement

- `/var/keyvault/<uid>.db` : un fichier par compte, **hors de `HOME:`** (pas partagé par ftpd/smbd, pas copié
  avec le dossier personnel), réservé à l'uid 0 dans les règles de vfs.
- Chaque entrée est chiffrée à part en AES-256-GCM avec la clé maîtresse du compte ; les **données associées**
  (AAD) sont `uid | chemin | nom` : une entrée recopiée sous un autre nom ou un autre compte ne se déchiffre pas.
- Écrit à côté puis renommé (comme oauthd), jamais à moitié. Une valeur : 64 Ko au plus.

### 3.3 Espaces

| Espace | Qui lit | Exemples |
|---|---|---|
| programme (chemin) | ce seul exécutable, ou ce seul script | clé d'API de Koton, session Telegram, clé RSA de Budget |
| dossier d'app (`vault = dossier`) | les exécutables et scripts de ce dossier | `/apps/claude.app` : app Claude + claude-acp |
| système (`/var/keyvault/0.db`) | les services de `/sys` | Wi-Fi, accès à distance, smbd, smbfs, clés TLS de webd |

Pas d'espace partagé par tout le compte : ce serait la porte que la première menace ferme. Un programme qui veut le
secret d'un autre passe par lui (oauthd donne des jetons d'accès, pas ses jetons de rafraîchissement).

oauthd devient **un client du coffre** : ses jetons de rafraîchissement dans son espace ; il peut enfin dire quels
programmes (chemins) ont droit à quel compte OAuth.

## 4. La clé du coffre

### 4.1 Trois menaces

| Menace | Ce qui protège |
|---|---|
| un autre exécutable du compte lit les secrets | le service (§3.3), l'identité attestée (§2) et un fichier réservé à l'uid 0 : demande **`access = enforce`** (étape 5 du multi-utilisateur) ; la clé n'y joue aucun rôle |
| le disque emporté seul, ou lu par Windows sur le ZBook | une part de la clé **qui n'est pas sur le disque** : scellée dans le TPM |
| la machine entière emportée | une part de la clé **qui n'est pas dans la machine** : le mot de passe de session |

### 4.2 Hiérarchie

```
mot de passe de session ──Argon2id──▶ P ─┐
                                         ├─HKDF─▶ KEK du compte ──enveloppe──▶ clé maîtresse du compte (256 bits)
secret du compte scellé dans le TPM ─▶ T ─┘                                          │
                                                                                     ▼
                                                  AES-256-GCM de chaque entrée de /var/keyvault/<uid>.db
```

- La **clé maîtresse** de chaque compte est tirée au hasard une fois et ne change jamais ; seule son enveloppe
  change (nouveau mot de passe). Elle ne vit qu'en mémoire de keyvault, une fois le compte ouvert.
- **Il faut P et T** : le disque seul n'a pas T ; la machine seule n'a pas P. Un autre système démarré sur le ZBook
  peut demander T au TPM, mais il lui manque encore le mot de passe, et Argon2id (64 Mo, ~0,5 s) rend chaque essai
  coûteux. Pour aller plus loin : protéger T dans le TPM par une valeur tirée du mot de passe, et le TPM bloque
  lui-même les essais répétés (anti-martèlement) ; à décider lors de l'étape TPM.

### 4.3 Le mot de passe de session (par défaut)

- `/sys/login` vérifie le mot de passe par authd (`AUTH_VERIFY`) ; authd le passe aussitôt à keyvault
  (`VAULT_UNLOCK`, de service à service), qui calcule P puis ouvre la clé maîtresse. Le mot de passe n'est jamais
  gardé.
- `passwd` (`AUTH_SETPW` avec l'ancien mot de passe) → keyvault réenveloppe.
- **Fermer la session** ou le **verrou** (Super+L) referment le coffre du compte.
- **Compte sans mot de passe ou en connexion automatique** : pas de P. Le coffre du compte reste fermé jusqu'à la
  première demande d'un programme ; une carte demande alors le mot de passe (comme `ui_elevate`). Un compte sans
  mot de passe du tout ne peut pas avoir de coffre : Réglages › Utilisateurs le dit.

### 4.4 Le TPM 2.0 (PC)

- Le ZBook a un TPM 2.0. Il faut un pilote : interface CRB ou TIS en MMIO (table ACPI `TPM2`), et quelques
  commandes (`CreatePrimary`, `Create`, `Load`, `Unseal`, `GetRandom`). Il vit dans keyvault lui-même (accès au
  matériel : il a les droits) ; aucun autre programme ne parle au TPM.
- **Secrets des comptes** : T est scellé **sans lien aux PCR** : une mise à jour du firmware ou de `korin.efi` ne
  ferme rien (c'est le mot de passe qui protège la machine volée).
- **Secrets du système** (uid 0 : Wi-Fi, accès à distance, smbfs, clés TLS) : pas de mot de passe au démarrage,
  les services en ont besoin avant toute connexion. Clé = T_sys seul, scellé aux PCR 7 (Secure Boot) et 4
  (`korin.efi`). Limite à connaître : le noyau, l'initrd et `/sys` ne sont pas mesurés ; un voleur de la machine qui
  modifie un service sur le disque peut lui faire lire ces secrets. Pour les couvrir, `korin.efi` devrait mesurer le
  noyau et l'initrd (PCR 8-9), et le noyau vérifier les signatures de `/sys` : chantier à part, plus tard. Ces
  secrets se ressaisissent (Wi-Fi), au pire.
- **Sans TPM** (QEMU sans swtpm, autre PC) : T est un fichier `/etc/keyvault/machine.key` (uid 0). Le disque volé
  n'est alors protégé que par le mot de passe. QEMU sait émuler un TPM (`swtpm`) pour les essais.

### 4.5 Sans clé de secours (décision du 18/11)

Les secrets sont **perdus**, et le coffre du compte recréé vide, si :
- le TPM est effacé (réglage du BIOS, changement de carte mère) ;
- le mot de passe est oublié et remis par un administrateur (`AUTH_SETPW` sans l'ancien) ;
- `/var/keyvault` est effacé.

On perd des jetons et des clés d'API, pas de données : on se reconnecte. Réglages › Utilisateurs le dit avant de
remettre un mot de passe. Une clé de secours pourra s'ajouter plus tard sans rien changer au format (une enveloppe
de plus).

## 5. Ce que devient chaque programme

| Programme | Après |
|---|---|
| oauthd, Mail | `secrets` + `key` → leur espace ; lecture de l'ancien format puis effacement |
| claude, claude-acp, vibe, vibe-acp | `.env` → espace du dossier de l'app ; claude-acp lit par `korin.vault`, garde `ANTHROPIC_API_KEY` de l'environnement en priorité |
| Koton, PocketQI, IRC, Forge | clés et mots de passe → espace du programme ; le fichier de réglages garde le reste |
| Telegram | `session.dat` entier → espace du programme (≤ 64 Ko) |
| Budget | clé privée RSA importée dans le coffre ; `KeyPath` n'est plus qu'un import |
| Courier | les collections restent au format Postman ; une variable `{{vault:nom}}` remplacée à l'envoi, et un bouton « Garder dans le coffre » |
| wifisvc | `key=` → espace système ; `wifi.txt` garde SSID et priorité ; `wifi.bak` disparaît |
| telnetd, ftpd, vncd, rdpd | `remote.txt` → espace système (vncd a besoin du mot de passe en clair pour DES : le coffre le lui rend) ; plus tard, comptes d'authd |
| smbfs | `password` de `smbfs.ini` → espace système, par partage |
| smbd | `[users]` → empreinte NT dans `shadow.ini` (déjà prévu par le multi-utilisateur) |
| webd | clés TLS → espace système ; les certificats (publics) restent dans `/etc/web/certs` |
| installateur | l'empreinte passe à `osinstall` par un tube, plus par `/tmp/install/plan.ini` |
| git | un assistant `git-credential-korin` sur `/bin/secret` |
| Jet | hors de ce chantier (les cookies sont une base SQLite) ; plus tard : `cookies.db` chiffré par une clé du coffre |

Le Pi garde ses fichiers actuels jusqu'au portage (noyau asm sans uid ni chemin attesté).

## 6. Plan par étapes (PC)

1. **Coffre** : keyvault, `vault_*` dans AppKit, `/bin/secret`, `/var/keyvault`, T en fichier
   (`/etc/keyvault/machine.key`), mot de passe de session (Argon2id, authd → keyvault, `passwd`, carte pour la
   connexion automatique), identité = uid seul en attendant l'étape 2. Passer oauthd et Mail dessus.
2. **Identité** : `V_LOAD_IMAGE` dans vfs, image attestée, `SYS_PROC_EXE`, `SYS_PROC_SET_SCRIPT` ; Python, bash
   et dotnet le font ; `interpreters.ini` ; `vault = dossier` ; empreinte et carte pour les binaires de `HOME:`.
3. **Migrer** les apps (§5), d'abord les clés d'API (Claude, Vibe, Koton, PocketQI), puis le système (Wi-Fi,
   accès à distance, smbfs, webd).
4. **TPM 2.0** : pilote CRB/TIS dans keyvault, T et T_sys scellés ; essais sous QEMU avec swtpm puis sur le ZBook.
5. **`access = enforce`** (étape 5 du multi-utilisateur), avec `/var/keyvault` et `/etc/keyvault` réservés à
   l'uid 0. **Sans elle, la première menace n'est pas couverte** : le fichier reste chiffré, mais un programme du
   compte pourrait remplacer un binaire de `/apps` et prendre son identité.
6. Plus tard : la clé de secours ; le Pi (uid et chemin attesté dans le noyau asm, OTP « device private key » à la
   place du TPM) ; la mesure du noyau et de `/sys` pour les secrets du système.

## 7. Questions restantes

1. Le TPM : protéger T par une valeur tirée du mot de passe (le TPM bloque les essais répétés) ? Plus sûr contre
   la machine volée, mais une mise à jour du mot de passe doit aussi changer l'objet du TPM.
2. Un compte sans mot de passe : pas de coffre du tout (proposé), ou un coffre sous T seul (protégé contre le
   disque volé, pas contre la machine volée) ?

### Tranché en faisant (18/11)

1. **Pas de valeur du mot de passe dans l'objet du TPM** : le TPM ne sert qu'à la clé de la machine, sans lien aux
   PCR (Korin démarre sans Secure Boot, le PCR 4 change à chaque `korin.efi`) ; le mot de passe, par Argon2id,
   protège la machine volée.
2. **Un compte sans mot de passe a un coffre**, sous la clé de la machine seule : avec un seul compte en connexion
   automatique, le comportement doit rester celui d'avant (les apps doivent pouvoir ranger leurs clés). Il est
   protégé du disque emporté seul, pas de la machine emportée.
3. **Espace `@system`** : un secret écrit par un programme du compte system (Réglages élevé) et lu par un autre
   (un service) ; refusé aux autres comptes.
4. **`access = enforce` n'est pas mis par défaut** (décision de l'étape 5 du multi-utilisateur : après un essai en
   mode journal sur le ZBook). En attendant, vfs refuse `/var/keyvault` et `/etc/keyvault` aux autres comptes même
   en mode journal ; sans `enforce`, un programme du compte pourrait encore remplacer un binaire de `/apps` et
   prendre son identité.
