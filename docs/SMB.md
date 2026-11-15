# Partage de fichiers SMB (réseau Windows)

Korin partage des dossiers vers Windows, macOS et Linux (**smbd**, le serveur) et monte les partages des autres
machines comme un volume (**smbfs**, le client). Les deux reposent sur [libsmb2](https://github.com/sahlberg/libsmb2)
(LGPL 2.1), qui fait le protocole : SMB 2.0.2 à 3.1.1, NTLMv2 dans SPNEGO, signature, chiffrement de SMB3.
Pas de Kerberos, pas de NetBIOS ni de SMB1.

## 1. Construire

Dans WSL, depuis la racine du dépôt :

```sh
ARCH=x86_64 sh tools/libsmb2/build.sh     # libsmb2 pour le PC -> build/pc/thirdparty/libsmb2
sh tools/pc/apps.sh smbd smbfs            # sys/smbd, bin/smbd, sys/svc/smbfs
NATIVE=1 sh tools/libsmb2/build.sh        # libsmb2 pour Linux (essais de smbd dans WSL, ASan)
```

`tools/libsmb2/build.sh` prend l'étiquette `libsmb2-6.3.2` (sources dans `~/.cache/korin-libsmb2`) et applique
`tools/libsmb2/korin.patch`, trois retouches du moteur de serveur :

* en *passthrough*, la réponse QUERY_INFO est toujours faite des octets donnés par le serveur (smbd encode
  lui-même toutes les classes d'infos, MS-FSCC) ;
* `smb2_set_server_status` : le code NT d'une réponse d'erreur (sans lui, toute erreur d'un gestionnaire
  devenait STATUS_NOT_IMPLEMENTED ; mettre soi-même une réponse en file libérait la requête que libsmb2
  relit ensuite) ;
* un contexte détruit (client parti) ne rappelle plus les gestionnaires avec une requête nulle.

## 2. Le serveur : smbd

### Réglages › Partage de fichiers

Le volet de Réglages (`sys/applets/shares.so`, livré avec smbd, source `src/sys/smbd/applet`) :

* le serveur : en marche ou arrêté, **Démarrer**, **Arrêter**, **Démarrer avec Korin** (la ligne `/sys/smbd`
  de `/etc/services.txt`) ;
* **Dossiers partagés** : Ajouter, Modifier (double-clic), Retirer. Un partage : le **dossier**, le **type** (SMB,
  le réseau Windows ; FTP plus tard), pour SMB le **nom** sous lequel Windows le voit (`\\korin\Nom`), et
  **lecture seule** ;
* **Compte d'accès** : le nom et le mot de passe que Windows demande (`[users]` de `smbd.ini` ; laissé vide, le mot
  de passe enregistré est gardé).

Les partages vont dans `/etc/shares.ini`, commun à tous les types (`src/sys/smbd/shares.h`) ; le futur
serveur FTP y prendra ceux de type `ftp` :

```ini
[Korin]
path = /media/sd1/home
type = smb          ; smb | ftp
readonly = no
```

smbd relit `shares.ini` et `smbd.ini` quand ils changent (au plus une fois par seconde, à la connexion et à chaque
requête) : un partage ajouté se voit tout de suite, un partage retiré n'est plus servi, même aux connexions
ouvertes.

`src/sys/smbd` (`main.c` : configuration, comptes, partages, arbres ; `fs.c` : les fichiers). Le même source
se compile sous Linux pour les essais.

Configuration : `/etc/smbd.ini` (`smbd -c FICHIER` pour une autre) :

```ini
[smbd]
port = 445
name = KORIN            ; nom du serveur
workgroup = WORKGROUP
label = Korin           ; étiquette des volumes vue par Windows
verbose = 0             ; 1 : chaque requête au journal (aussi smbd -v)

[users]
stephane = motdepasse   ; nom = mot de passe (Windows le demande à la connexion)

[shares]
Korin = /media/sd1/home       ; nom = dossier [, readonly]
Systeme = /etc, readonly
```

Lancement : une ligne `/sys/smbd` dans `/etc/services.txt` (init2, journal), ou `smbd` dans le Terminal.
Depuis Windows : `\\korin\Korin` (ou `\\192.168.x.y\Korin`) dans l'Explorateur, ou
`net use K: \\192.168.x.y\Korin /user:stephane`.

Ce qui est fait : liste des dossiers (classes 1, 2, 3, 12, 37, 38, jokers `*` `?` et ceux de DOS), lecture,
écriture, création, troncature, renommage et déplacement (remplacement si demandé), effacement (fichier,
dossier vide ; DispositionInformation et sa forme Ex), dates (BasicInformation), infos de fichier et de
volume (taille par `statvfs`), descripteur de sécurité (Tout le monde : contrôle total, ou lecture), partages
en lecture seule, requêtes composées (CREATE + … + CLOSE, identifiant 0xFF…FF), IPC$ (sans tubes nommés).

Pas fait : la liste des partages en tapant seulement `\\korin` (RPC srvsvc sur IPC$), la découverte dans
« Réseau » (WS-Discovery, multicast : netd ne l'a pas), les notifications de changement (CHANGE_NOTIFY rend
NOT_SUPPORTED : l'Explorateur ne voit un changement fait ailleurs qu'en rafraîchissant), les flux de données
nommés (`fichier:Zone.Identifier`), les verrous d'octets (accordés), les oplocks et baux.

## 3. Le client : smbfs

### Dans Fichiers

**Volume › Se connecter à un serveur…** (Ctrl+K) : l'adresse (`smb://192.168.0.9/Partage`, `\\192.168.0.9\Partage`,
avec un dossier au bout si l'on veut ; la liste propose les serveurs déjà connus), l'utilisateur (`nom` ou
`DOMAINE\nom`), le mot de passe. Fichiers :

* enregistre le compte dans `/etc/smbfs.ini` (section `[hôte/partage]`) ; laissés vides, l'utilisateur et le
  mot de passe déjà enregistrés pour ce partage servent ;
* monte le partage sur le premier `/net/netn` libre et l'ouvre ; un échec (adresse, compte) le démonte et redemande ;
* ajoute `NETn = //hôte/partage, smbfs` à `/etc/mounts.txt` : vfs le remonte à **chaque démarrage** (smbfs ne
  se connecte qu'au premier accès : le réseau n'a pas à être prêt).

Le volume apparaît à gauche (genre « Serveur »), avec le bouton d'éjection. **Le démonter** (bouton, ou Volume ›
Démonter) retire sa ligne de `mounts.txt` : il ne revient plus au démarrage, mais **le compte reste** dans
`smbfs.ini` pour la prochaine connexion. Une adresse trop longue pour la table de vfs (23 caractères) passe par
une section nommée (`smb:net0`, avec `host`, `share`, `dir`).

### À la main

`src/svc/smbfs` : un service de fichiers comme ramfs et fatfs (protocole de `src/inc/fs.inc`), lancé par vfs au
montage :

```sh
mount /net/net0 //192.168.0.9/Korin smbfs      # //hôte/partage[/dossier], hôte[:port]
mount /net/net0 smb:bureau smbfs               # une entrée [bureau] de smbfs.ini
ls /net/net0/
```

Les comptes : `/etc/smbfs.ini`, une section par hôte (`[192.168.0.9]`), par `hôte/partage`, par nom
(`smb:nom`), ou `[default]` :

```ini
[192.168.0.9]
user = stephane
password = ...
domain =                ; facultatif

[bureau]
host = 192.168.0.9
share = Korin
user = stephane
password = ...
```

La connexion se fait à la première requête d'un programme, pas pendant le montage (vfs attend alors le
service, et lire `smbfs.ini` passe par vfs). Pour la même raison, pas de rootpath au montage : le dossier se met
dans la source (24 caractères au plus, la limite de vfs ; sinon `smb:nom`). La connexion est refaite si le
serveur l'a coupée ; les fichiers ouverts avant sont alors perdus (E_IO). Une requête à la fois (un thread).
`df` montre le volume (FS_INFO : taille du partage, genre 4, étiquette `//hôte/partage`).

Windows 11 (24H2 et plus) refuse les invités : un partage « Tout le monde » demande quand même un compte de
cette machine Windows (`user`/`password`).

## 4. Vérifié

* **smbd sous Linux** (WSL, AddressSanitizer, 15/11) avec les exemples de libsmb2 : liste, lecture, écriture,
  stat, partage en lecture seule (ACCESS_DENIED), partage inconnu (BAD_NETWORK_NAME), mauvais mot de passe
  (LOGON_FAILURE).
* **smbd depuis Windows 11** (le client SMB de Windows, `net use \\127.0.0.1\Korin /TCPPORT:4445`, SMB 3.1.1
  signé) : liste, lecture, écriture, dossier créé, renommé, déplacé, copie de 20 Mo (empreinte identique),
  copie récursive, effacements.
* **Korin PC sous QEMU** (QEMU de WSL sans KVM, usb-net, 15/11) :
  * smbd lancé par init2 ; depuis Windows (`/TCPPORT:4446` redirigé) : liste, lecture, écriture, dossier,
    renommage, déplacement, partage en lecture seule refusé à l'écriture, copies de 64 Ko à 1 Mo identiques ;
  * smbfs monté sur le smbd de WSL (`mount /net/net0 //10.0.2.2:4445/Korin smbfs`, SMB 3.1.1 signé) : `ls`, `cat`,
    écriture, `mkdir`, `mv`, `cp` vers /, `rm`, `rmdir`, `df`.
  * Débit sous cet émulateur : environ 100 Ko/s en écriture et 400 Ko/s en lecture. Une copie de 10 Mo depuis
    Windows échoue : ses écritures en avance dépassent son délai de 60 s. À mesurer sur le vrai PC.
* **Interfaces dans Korin sous QEMU** (bureau, captures, 15/11) :
  * Fichiers › Se connecter à un serveur… : dialogue, montage en /net/net0 (genre « Serveur », bouton d'éjection),
    compte écrit dans smbfs.ini et ligne dans mounts.txt ; /net/net0 remonté par vfs au démarrage suivant ;
    l'éjection retire la ligne et garde le compte ; reconnexion avec l'adresse seule (compte enregistré) ;
  * Réglages › Partages : état du serveur, liste de shares.ini, compte ; un partage ajouté dans le volet est ouvert
    tout de suite depuis Windows, sans relancer smbd.
  * smbd sous Linux : shares.ini relu à chaud (ajout, retrait, type ftp ignoré).
* **Pas essayé** : le ZBook, le Pi, le partage Windows de l'utilisateur (il faut un compte de ce PC dans
  smbfs.ini).

Le bug de signature trouvé là (requête composée reçue en plusieurs segments : `iov_offset` local à
`smb2_read_data`) touche aussi les clients Windows sur un vrai réseau ; il est corrigé par `korin.patch` et
reproduit sous Linux par un relais qui découpe le flux en morceaux de 7 octets.

## 5. Pièges côté Windows

* **Un seul compte par serveur** : si Windows connaît déjà cette adresse sous un autre nom (un lecteur mappé, une
  entrée du Gestionnaire d'identification, d'une ancienne machine à la même adresse), il refuse le compte de Korin
  avec une « erreur non spécifiée » 0x80004005 (en réalité l'erreur 1219). `net use` montre les lecteurs,
  `cmdkey /list` les comptes gardés ; les retirer (`net use X: /delete`, `cmdkey /delete:ADRESSE`), puis se reconnecter.
* **La racine d'un volume** (`/`) : fatfs n'a pas d'entrée de dossier pour elle (`stat` échoue) ; smbd la prend
  pour un dossier quand `opendir` l'ouvre (15/11).

## 6. Paquets (PC)

Publiés le 15/11 sur main (`packages/pc/`) : **`smbd` 1.0.0** (`sys/smbd`, `bin/smbd`, `sys/applets/shares.so` : le
serveur et le volet Partages) et **`pc` 2026.11.5** (Fichiers avec Se connecter à un serveur, et le client
`sys/svc/smbfs`, qui reste au système). `tools/pkg/packages-pc.ini`, section `[smbd]` avant `[pc]`. **smbd 1.0.1**
(15/11, main 0e92b89) : la racine d'un volume comme partage.

## 7. Plus tard

* srvsvc (liste des partages), WS-Discovery, CHANGE_NOTIFY (avec une veille des dossiers dans vfs).
* Un volet de Réglages (partages, comptes), comme celui de webd.
* smbfs : plusieurs requêtes en parallèle (l'API asynchrone de libsmb2), un cache des listes de dossier.
* Le Pi : `tools/libsmb2/build.sh` sans ARCH, et la construction de smbd et smbfs pour AArch64.
