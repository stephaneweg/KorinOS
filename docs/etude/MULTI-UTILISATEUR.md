# Étude : plusieurs utilisateurs et un utilisateur système

État (17/11/2026) : **étapes 1 à 4 faites** (§3) sur le PC : arbre unique à la Unix, `HOME:` par uid, uid dans le
noyau du PC, règles d'accès de vfs, authd, sudo, passwd, `/sbin`, pkgd démon. **Étape 6 faite pour l'essentiel** :
comptes (`useradd`, `userdel`, `usermod`), écran de connexion `/sys/login`, Réglages › Utilisateurs ; reste le
verrou, fermer la session, changer d'utilisateur, `/home` → `/home/<nom>` et les accès distants par comptes.
**Étape 5 à faire** : les règles sont en `access = log` par défaut (rien n'est refusé, les écarts vont au journal).
Le noyau asm du Pi n'a pas encore d'uid. Demande : des dossiers par utilisateur dans `/home`, comme sous
Unix, et un utilisateur système, seul autorisé à modifier `/bin`, `/apps`, `/sys` et `/etc`, quitte à demander une
élévation de droits pour les mises à jour.

## 0. Résumé

- Le noyau reçoit un **uid par process**, hérité au lancement. Il l'écrit dans chaque message de service, à côté du
  pid. Seul l'uid 0 (`system`) peut en changer.
- FAT32 et exFAT n'ont **ni propriétaire ni droits** par fichier. Les droits sont donc des **règles par chemin**,
  appliquées par **vfs** : `/` en lecture seule sauf pour `system`, `/home/<nom>` à son
  utilisateur, `/tmp` à tous.
- **L'élévation** passe par un service `authd` :
  - une boîte de dialogue dessinée par Elegant, pas par l'app ;
  - le mot de passe d'un administrateur ;
  - un ticket limité dans le temps.
- Les mises à jour passent par **pkgd**, qui devient un vrai démon `system`. `pkg`, Logiciels et Package Manager en
  deviennent des clients.
- Les commandes d'administration passent de `/bin` à un nouveau **`/sbin`**, que seul `system` peut exécuter
  (§2.9). `/sys` reste réservé aux services et pilotes qui ont les droits matériel.
- **`HOME:`** est un volume virtuel qui désigne toujours `/home/<utilisateur>/` de l'appelant (§2.4). Les
  apps écrivent `HOME:/documents`, sans connaître le nom de l'utilisateur.
- **Ce qui coûte le plus** : les réglages d'aujourd'hui dans `/etc` qui sont en fait propres à l'utilisateur
  (thème, panel, dock, récents…). Les ~112 fichiers qui écrivent dans `HOME:/<app>` passent à `HOME:/<app>` par un simple remplacement.
- **Ordre** : d'abord préparer sans rien changer au comportement (HOME, réglages). Ensuite l'identité, puis les
  règles de vfs (journal seulement, puis refus), l'élévation, et enfin l'écran de connexion.

### Rapport avec la décision du 29/10 (pas de sécurité contre un attaquant)

Le projet a décidé de ne pas se défendre contre un attaquant. Il vise seulement la **résistance aux bugs**. Cette
étude reste dans ce cadre :

- `/` en lecture seule protège le système d'une app boguée ou d'un `rm -rf` malheureux.
- Les dossiers par utilisateur séparent les données des personnes qui partagent la machine, contre les erreurs et
  la curiosité ordinaire.
- Ce n'est **pas** un système durci :
  - un programme qui parle directement à `fs.root` ou à `blk.*` contourne vfs ;
  - les mots de passe protègent contre la main d'un autre utilisateur, pas contre l'extraction de la carte.

On le dit sans le corriger. Fermer ces portes reste au plan des niveaux d'accès (`FEUILLE-DE-ROUTE.md` : niveaux
d'accès, données privées), à faire seulement si l'utilisateur le demande.

## 1. Ce qui existe

| Sujet | Aujourd'hui |
|---|---|
| Identité d'un process | aucune. `struct process` n'a qu'un booléen `rights` (droits de `/sys` : matériel, services, IRQ, DMA ; `korin/sched.h:41`, `P_DROITS` dans le noyau asm). |
| Droits « matériel » | demandés au lancement (`SPAWN_RIGHTS`), accordés seulement si le lanceur les a (`core/proc.c:369`). vfs marque `ATTR_SYS` pour tout exécutable sous `/sys/` ; un lanceur sans droits délègue à init2 (`INIT_OP_LAUNCH`). |
| Identité de l'appelant pour un service | le noyau écrit le pid à l'octet 44 du message (`MSG_PID`), aussi pour `SVC_FORWARD`. vfs ne s'en sert que pour le dossier courant et les tubes. |
| Contrôles de vfs | aucun, sauf le bit x (index `.exec`). `VFS_MOUNT` est ouvert à tous. fatfs connaît la lecture seule d'un volume entier, pas l'attribut FAT `0x01`. |
| POSIX | `getuid()` rend 0. Un seul compte, « onyx ». `stat` invente `0755`/`0644` et `uid = 0`. `chmod` ne touche que x. |
| HOME | `HOME=/` (libposix), `/` (sh). UIKit : `uk_s_home = "HOME:"`. Chaque app range ses données dans `HOME:/<app>` (documents, music, jet, koton…). |
| `/home` sur les deux machines | Pi : `HOME:` sur la FAT32 système ; `/media/sd1` est la partition exFAT personnelle. PC : `/media/sd1` = dossier `/home` de la partition KORIN, donc `HOME:` et `/media/sd1/` sont la même chose. |
| Réglages | tous dans `/etc` et partagés : `system.ini`, `theme.txt`, `panel.ini`, `dock.ini`, `pointer.ini`, `places.ini`, `fileassoc.ini`, `recent.txt`, `wifi.txt`, `remote.txt`, `smbd.ini`, `shares.ini`, `mounts.txt`… |
| Paquets | `pkg`, `pkgd` (lancé par `etc/session`), Package Manager et setup lient `pkglib.h` et écrivent chacun directement. init2 lance `pkg commit` au démarrage. |
| Mots de passe | `etc/remote.txt` : un seul mot de passe, en clair (telnet, ftp, VNC, RDP). `etc/smbd.ini` : comptes `nom = mot de passe`, en clair. Aucun écran de verrouillage. setup ne crée pas d'utilisateur. |

## 2. Modèle proposé

### 2.1 Comptes

Le fichier `/etc/users.ini` est lisible par tous et modifiable par `system` seulement :

```ini
[system]
uid = 0
home = /home/system
groups = admin

[stephane]
uid = 1000
name = Stéphane
home = /home/stephane
shell = /bin/bash
groups = admin
picture = /home/stephane/.face.png

[invite]
uid = 1001
home = /home/invite
groups =
```

- Les mots de passe sont dans `/etc/shadow.ini`, lisible par `system` seulement. On y garde un hachage salé
  (PBKDF2-SHA256, déjà dans OpenSSL ; une petite fonction C suffit si l'on ne veut pas lier OpenSSL à authd).
  - Un mot de passe vide signifie connexion sans mot de passe.
  - Un compte qui n'est pas dans `shadow.ini` ne peut pas se connecter : c'est le cas de `system`.
- `system` est un compte de service, comme `root`, mais **on ne s'y connecte pas**. On ne l'atteint que par
  élévation.
- Le groupe `admin` dit qui peut s'élever. C'est le seul groupe utile au départ ; l'appartenance aux autres groupes
  ne donne aucun droit sur les fichiers.
- libposix lit `users.ini` pour `getpwnam`, `getpwuid`, `getpwent`, `getgrnam` et `getlogin`. « onyx » et « root »
  restent des alias du compte courant et de l'uid 0, pour les ports.

### 2.2 Identité des process (noyau)

- On ajoute `int uid` à `struct process` dans le noyau C (`core/proc.c`, `sched.h`), et `P_UID` à côté de
  `P_DROITS` dans le noyau asm du Pi (`vm.asm`, `spawn.asm`, `fork.asm`, `exec.asm`). Le noyau du PC passe d'abord,
  le Pi suit avec le même numéro d'appel.
- **Héritage** : spawn, fork et exec gardent l'uid du père.
- Un lanceur d'uid 0 peut demander un autre uid au lancement : nouvelle option de spawn, `SPAWN_UID`, avec l'uid
  dans un registre libre.
- **Changement** :
  - `SYS_SETUID(uid)` n'est permis qu'à un process d'uid 0. Il ne sert qu'à authd et à login.
  - `SYS_GETUID` et `SYS_PROC_UID(pid)` sont ouverts à tous.
- **Messages** : le noyau écrit l'uid de l'appelant dans le message, à côté de `MSG_PID` (`MSG_UID`, 4 octets libres
  de l'en-tête à choisir). Il le recopie aussi pour `SVC_FORWARD`.
  - vfs n'a donc rien à demander au noyau, et la réponse reste juste même si le pid meurt entre-temps.
- **Les droits « matériel » restent séparés de l'uid.**
  - `rights` dit « peut toucher au matériel et enregistrer un service » ; uid dit « à qui sont les fichiers ».
  - Les services lancés par init2 sont `rights = 1, uid = 0`.
  - Elegant et les programmes de session ont les droits s'il le faut, avec l'uid de l'utilisateur connecté.
  - Une app lancée depuis le bureau hérite de l'uid de l'utilisateur.
- **Point à trancher dans le code** : init2 lance aujourd'hui tout `/sys/...` qu'on lui demande. Il doit passer
  l'uid du demandeur, pour qu'un programme de `sys` lancé par un utilisateur garde cet uid. Les services de
  `services.txt` gardent l'uid 0.
- `SYS_KILL` : on le limite au même uid, sauf pour l'uid 0. C'est peu cher, et ça évite qu'une app boguée tue la
  session d'un autre ou un service.

### 2.3 Règles d'accès dans vfs

FAT32 et exFAT ne gardent pas de propriétaire. Les règles dépendent donc **du chemin et du volume**, comme l'index
`.exec` pour le bit x. Une table compilée dans vfs donne les règles par défaut ; `/etc/access.ini` les complète :

| Chemin | `system` (uid 0) | utilisateur propriétaire | autres utilisateurs |
|---|---|---|---|
| `/bin`, `/apps`, `/sys`, `/lib`, `/devtools/gcc`, `/devtools/dotnet`, `/res`, `/fonts` | lecture, écriture | lecture, exécution | lecture, exécution |
| `/sbin` (§2.9) | lecture, écriture, exécution | lecture ; exécution refusée (« commande système : sudo … ») | idem |
| `/etc` | lecture, écriture | lecture (sauf `shadow.ini`, `wifi.txt`, `remote.txt`, `smbd.ini` : rien) | idem |
| `/var` | lecture, écriture | lecture | lecture |
| `/home/<nom>/…` | lecture, écriture | lecture, écriture | rien (option : lecture) |
| `/home/public/…` | lecture, écriture | lecture, écriture | lecture, écriture |
| `/tmp` | tout | tout | tout (pas de bit « sticky » : un fichier n'a pas de propriétaire) |
| volume amovible (`/media/usb0`, `/net/net0`, `/media/img0`) | tout | propriétaire = utilisateur de la session au montage | rien |
| `/media/sd1` sur le Pi (partition personnelle) | tout | propriétaire = premier utilisateur (`mounts.txt : owner=stephane`) | rien |

- **Où** : dans `path.c`, après la résolution et avant de passer au service. On vérifie `V_OPEN` (selon le mode),
  `V_CREATE`, `V_UNLINK`, `V_RENAME` (source et destination), `V_MKDIR`, `V_RMDIR`, `V_CHMOD`, `V_UTIME`,
  `V_TRUNCATE` et `V_MOUNT`/`UNMOUNT` (uid 0, ou volume amovible de la session).
  - Une liste de préfixes, triée une fois au chargement, suffit : le coût est négligeable devant un accès FAT.
- **`stat` et `readdir`** rendent un `st_uid` et un `st_mode` calculés par la même règle : `0755` pour `system`,
  `0700` pour un home. `ls -l`, git, Python et les ports voient alors des droits cohérents ;
  `access(W_OK)` répond juste.
- **`chmod`** ne touche toujours qu'au bit x. Des droits par fichier demanderaient un fichier annexe par volume,
  comme `.exec` : pas prévu.
- **Introduction douce** : un mode `access = log` (dans `system.ini`, ou une option de vfs) journalise les refus
  sans refuser.
  - On fait tourner les apps quelques jours.
  - On corrige celles qui écrivent dans `/` : réglages, caches, `.bash_history` à la racine.
  - Ensuite seulement on passe à `access = enforce`.
- **Services qui servent d'autres personnes** (smbd, ftpd, webd, telnetd) :
  - **telnetd** lance `sh` avec l'uid du compte authentifié. vfs applique alors les règles d'elle-même.
  - **smbd** et **ftpd** tournent en uid 0 et servent plusieurs clients. Deux façons de faire :
    - (a) ils appliquent eux-mêmes les règles par un petit en-tête partagé avec vfs (`src/svc/inc/access.h`) ;
    - (b) un `V_OPEN_AS uid`, réservé à l'uid 0, laisse vfs juger.
    - **Recommandé : (b)**, une seule copie des règles.
  - **webd** sert `/home/<nom>/sites` avec l'uid du propriétaire du site.

### 2.4 Où vont les homes : `HOME:` = `/home/<utilisateur>/`

Décidé avec l'utilisateur le 15/11 : **`HOME:` est un volume virtuel qui désigne toujours le home de celui qui
appelle.**

- Les homes sont dans `/home/<nom>/`, sur les deux machines. On a aussi `/home/public/` et `/home/system/`, le home de l'uid 0.
- `HOME:/documents/lettre.txt` devient :
  - `/home/stephane/documents/lettre.txt` pour stephane ;
  - `/home/invite/documents/lettre.txt` pour invite.
- Une app n'a donc **jamais** besoin de connaître le nom de l'utilisateur. Deux sessions en même temps (bureau et
  telnet sous un autre compte) voient chacune leur home.
- **Dans vfs** :
  - `HOME` est un volume d'alias. La résolution remplace `HOME:/x` par `/home/<nom>/x`.
  - `<nom>` vient de l'uid du message (`MSG_UID` → `users.ini`, avec un cache uid → nom gardé par vfs).
  - C'est le même mécanisme que le `rootpath` de montage déjà prévu (mémoires `armpi-ntfs-dualboot`,
    `armpi-idee-mount-dossier`), avec un chemin qui dépend de l'appelant.
  - Les règles d'accès (§2.3) s'appliquent **après** la résolution, sur le chemin `/home/<nom>/…`. Un seul jeu
    de règles suffit donc.
- **Avant l'étape d'identité** (pas encore d'uid), `HOME:` désigne `HOME:/` tel quel. L'alias peut donc arriver
  **dès l'étape 1**, sans rien changer au comportement.
  - Le passage au multi-utilisateur revient alors à faire pointer l'alias sur `HOME:/<nom>/` et à déplacer le
    contenu.
- **Formes de chemin** :
  - `getcwd` et la boîte Ouvrir montrent la forme `HOME:/…`, plus lisible et indépendante du compte. `realpath`
    rend la forme canonique `/home/<nom>/…`.
  - En POSIX : `HOME=/home/<nom>` (aujourd'hui `/home`). posixtest exige que `HOME`
    commence par `/`.
  - `~` vaut donc `/home/<nom>`, soit `HOME:/`.
- **Conséquence sur le renommage des volumes** (mémoire `armpi-volumes-sys-home`) :
  - `HOME:` n'est plus le nouveau nom de `/media/sd1`.
  - Sur le PC, `/media/sd1` désignait déjà `HOME:` sur la même partition : il devient inutile, `HOME:` le remplace.
  - Sur le Pi, la partition exFAT personnelle est montée sur `/media/sd1` (décidé avec l'arbre unique).
    - C'est un volume de données dont le premier utilisateur est le propriétaire.
    - **On n'y déplace rien.** Si l'utilisateur veut y mettre des choses, c'est lui qui les déplace ; jamais un
      agent ni un script.
- Le contenu actuel de `HOME:` (documents, music, jet, koton…) devient `HOME:/<premier utilisateur>/`.
  - C'est un **renommage** de dossiers sur le même volume, sans copie.
  - Il est fait au premier démarrage d'une version multi-utilisateur, par un programme `system` lancé par init2.
  - On garde un fichier de correspondance pour pouvoir annuler.
- Le Pi a moins de place sur la FAT32 système. Les homes y vivent quand même. Les gros fichiers (ROM, vidéos)
  peuvent aller sur `/media/sd1`, au choix de l'utilisateur.

### 2.5 Réglages : système ou personnels

Il faut couper `/etc` en deux. **Décision du 16/11** (docs/DESIGN-CONNEXION.md §2.6) : les réglages personnels
vont dans **`HOME:/etc/`** (`/home/<utilisateur>/etc`), pas dans `~/.config/korin/` comme l'étape 1 l'avait
fait le 15/11 ; cette ancienne place est encore lue en repli.

| Reste dans `/etc` (machine, écrit par `system`) | Va dans `HOME:/etc/` (personnel) |
|---|---|
| `wifi.txt`, `remote.txt`, `smbd.ini`, `shares.ini`, `mounts.txt`, `printers.ini`, `users.ini`, `shadow.ini`, `pkg/`, `services.txt`, `drivers.txt`, `i2chid.txt`, `clock.cfg` (fuseau), `exec.txt`, `session` (programmes communs) | `theme.txt`, `panel.ini`, `dock.ini`, `pointer.ini` (vitesse, sens), `places.ini`, `fileassoc.ini`, `files.ini`, `recent.txt`, `screenshot.ini`, `alarms.txt`, `startup.txt` (programmes de l'utilisateur), `gamepad.ini` (à voir) |
| `system.ini` : la partie machine (langue par défaut, résolution, nom de la machine) | `system.ini` : la partie personnelle (langue de l'utilisateur, fond d'écran, sons) |

- Lecture : on cherche d'abord dans `HOME:/etc/`, puis dans `HOME:/.config/korin/` (la place du 15/11), puis
  dans `/etc/` comme valeur par défaut. Un nouvel utilisateur hérite ainsi des choix de la machine.
- Écriture : toujours `HOME:/etc/<nom>` ; la première fois, libposix et korinrt y copient d'abord le fichier de
  l'ancienne place ou de `/etc`.
- Un seul point d'accès par bibliothèque : `uk_conf_path` (UIKit), `korin_conf_path` (libposix,
  `armpi/userconf.h`), `conf_path` (korinrt). Pas de `XDG_CONFIG_HOME` : les ports gardent `~/.config/<app>`.
- **Réglages** écrit les volets « machine » (Wi-Fi, Partages, Accès à distance, Imprimantes, Disques, Mises à
  jour) par élévation (§2.6). Les volets personnels s'écrivent sans rien demander.
- Les données d'app suivent la même règle : `HOME:/<app>` devient `~/<app>`. Les dossiers visibles (Documents,
  Musique, Images, Vidéos) restent visibles. Les données internes des apps (jet, koton, pocketqi, cookies)
  pourraient passer sous `~/.local/<app>`. **À trancher** : garder la forme actuelle simplifie la migration.
- Côté code, grâce à `HOME:` (§2.4), il suffit de remplacer `"HOME:/…"` par `"HOME:/…"` dans environ
  112 fichiers (C, C++, asm), plus la table de UIKit (`uk_s_home`).
  - C'est un remplacement **mécanique**, faisable par script, sans nouvelle fonction ni nom d'utilisateur à
    calculer.
  - Les réglages personnels deviennent `HOME:/etc/<fichier>` (d'abord `HOME:/.config/korin/`, changé le 16/11).
  - Il faut construire les deux machines et augmenter la version des paquets touchés.

### 2.6 Élévation

**authd** est un service lancé par init2, avec l'uid 0, sans droits matériel.

1. Un client (pkgd, Réglages, `sudo`) demande `AUTH_ELEVATE(raison, programme)`.
2. authd vérifie que l'utilisateur de l'appelant (son uid, lu dans le message) est dans le groupe `admin`.
3. authd demande à **Elegant** d'afficher une boîte de confirmation système :
   - modale pour toute la session, dessinée par Elegant et non par l'app ;
   - titre « Réglages veut modifier le système » ;
   - champ du mot de passe ; un réglage permet une simple confirmation sans mot de passe.
4. Si c'est accepté, authd rend un **ticket** : une poignée IPC ou un jeton, valable 5 minutes et lié au pid. Le
   service qui fait le travail (pkgd, settingsd) le vérifie auprès d'authd.

Pour le shell, la commande **`sudo commande…`** (nom connu des ports et des habitudes) :

- elle demande le mot de passe au terminal ;
- authd lance alors `commande` avec l'uid 0, l'environnement nettoyé et le terminal hérité ;
- `sudo -s` ouvre un shell `system`.

**Retenir pendant 5 minutes** (18/11, maquette `docs/maquettes/elevation-retenir.html`) : pour un compte avec mot de
passe, la carte d'authcard a une case « Retenir pendant 5 minutes » (décochée à chaque ouverture) et sudo demande
« Retenir pendant 5 minutes ? [o/N] » après le mot de passe. authd garde le délai en mémoire seulement, par uid et par
portée : la session graphique pour la carte, le shell qui lance sudo (son père) pour un terminal. Pendant le délai,
l'élévation est accordée sans carte ni mot de passe. Le délai part de la saisie et ne s'allonge pas. Il est oublié au
bout de 5 minutes, au redémarrage, à chaque ouverture de session (`/sys/login`, `AUTH_FORGET`) et par `sudo -k`.

L'élévation **ne donne jamais les droits matériel** : seulement l'uid 0, donc l'écriture dans `/`.

Il n'y a **pas de bit setuid** : aucun fichier ne peut le porter sur FAT. Tout passe par authd ou par un démon
`system`.

### 2.7 Mises à jour et paquets

pkgd devient le **seul écrivain** de `/bin`, `/apps`, `/sys`, `/lib`, `/etc/pkg` et `/var/pkg`.

- Il tourne en uid 0. init2 le lance avec les services, plus par `etc/session`.
- Il expose un service `pkg` avec les opérations `CHECK`, `UPDATE`, `INSTALL`, `REMOVE` et `PROGRESS`.
- `pkg` (commande), Logiciels et Package Manager deviennent des clients de ce service. `pkglib.h` reste la
  bibliothèque de pkgd, et des clients pour la lecture seule (liste, recherche).
- Ce qui se fait sans demander et ce qui demande une élévation :
  - **Tour quotidien et mises à jour du dépôt signé** : faits par pkgd sans rien demander. La signature du dépôt
    est déjà vérifiée : on fait confiance à la source, pas à l'utilisateur.
  - **Installer ou retirer un paquet** : ticket d'authd. Un utilisateur `admin` voit la confirmation, un utilisateur
    ordinaire voit un refus. Un réglage « les utilisateurs peuvent installer » reste possible.
  - **`pkg commit` au démarrage** ne change pas : init2 le lance en uid 0.
- setup tourne au premier démarrage en uid 0, avant tout compte. Il crée le premier utilisateur (`admin`) : nouvelle
  étape « Votre compte », avant le Wi-Fi.
- Les outils de construction dans l'OS (selfhost, pibuild, `make install` d'un port) passent par `sudo`.

### 2.8 Connexion et session

- init2 lance **login** (uid 0) avant la session :
  - Écran d'Elegant : liste des comptes de `users.ini` avec leur image, champ du mot de passe.
  - Option **connexion automatique** (`system.ini : autologin = stephane`). C'est le **défaut avec un seul compte** :
    l'appareil démarre comme aujourd'hui, sans rien demander.
- Une fois authentifié, login lance `etc/session` puis `HOME:/etc/startup.txt` avec l'uid choisi, et
  `HOME`, `USER`, `LOGNAME`, `SHELL`.
- Les commandes **Fermer la session** (dans le menu d'extinction) et **Verrouiller** (écran de login sur la session
  ouverte) s'ajoutent.
- Le **changement rapide d'utilisateur** (deux sessions ouvertes) vient plus tard. Il demande qu'Elegant tienne
  plusieurs bureaux, ce que les espaces de travail préparent en partie.
- La console texte (PC sans Elegant) et telnet demandent un nom et un mot de passe, comme un `login` Unix.
- Accès à distance :
  - `remote.txt` disparaît au profit des comptes. ftp, telnet et SMB authentifient par `users.ini`/`shadow.ini`.
  - **VNC** garde un mot de passe à part en clair : son défi DES l'exige.
  - SMB et NTLMv2 ont besoin du hachage NT (MD4 de l'UTF-16) : `shadow.ini` garde aussi ce hachage pour les comptes
    qui partagent par SMB.

### 2.9 `/bin` et `/sbin`

On sépare les commandes de tout le monde des commandes d'administration, comme sous Unix (demande du 15/11).

- **Trois dossiers, trois rôles** :
  - **`/bin`** : les commandes de l'utilisateur, lancées avec son uid.
  - **`/sbin`** (nouveau) : les commandes d'administration. Elles demandent l'uid 0,
    mais pas les droits matériel.
  - **`/sys`** (inchangé) : les services, pilotes et programmes de session. Ils reçoivent les **droits
    matériel** (`ATTR_SYS`), sont lancés par init2 et ne sont pas tapés au shell.
- **Exécution** : vfs refuse `V_EXEC` sous `/sbin` à un uid autre que 0. sh et bash affichent alors « commande
  système : sudo <commande> ». On ne s'élève pas tout seul : il faut taper `sudo` ou passer par une app qui demande
  la confirmation.
- **PATH** :
  - utilisateur : `/bin:/devtools/gcc/bin` (comme aujourd'hui) ;
  - `system`, `sudo -s` et `sudo` : `/sbin:/bin:/devtools/gcc/bin`.
- Le bit x de `/sbin` vient de l'index `.exec`, comme pour `/bin` et `/sys` (`exec.c`).
- **Commandes qui passent dans `/sbin`**, d'après `src/bin` au 15/11 :
  - **disques et volumes système** : `mkfs`, `mkinitramfs`, `fscache` ;
  - **noyau et machine** : `cpufreq`, `preload`, `unload` ;
  - **construction de l'OS** : `selfhost`, `pibuild` (ils écrivent dans `/`) ;
  - **outils qui peuvent faire tomber le système** : `crash`, `svcfault`, `memhog`, `dmareach`, `gpuhang` ;
  - **nouvelles commandes** : `useradd`, `userdel`, `usermod`.
- **Restent dans `/bin`**, avec une règle pour l'utilisateur de la session :
  - `mount` et `eject` : un utilisateur monte et démonte les volumes amovibles. Un volume système demande `sudo`,
    et c'est vfs qui juge.
  - `shutdown` et `reboot` : permis à l'utilisateur de la session ouverte, par init2, comme sur un bureau ordinaire.
    Ils sont refusés à une session telnet sans `sudo`.
  - `pkg` : client de pkgd (§2.7). La liste et la recherche marchent sans rien ; installer, retirer et mettre à jour
    passent par le ticket.
  - `wifi`, `soundctl`, `volume`, `journal` : l'état se lit sans rien. Les changements de la machine (réseaux
    enregistrés) passent par le service, qui demande le ticket.
  - `passwd` : change son propre mot de passe par authd.
  - `sudo` : nouvelle commande.
- Les **tests** (`*test`, `*bench`) restent dans `/bin`. Ils n'écrivent que dans `/tmp`, ou dans le home de celui
  qui les lance.
- **Paquets** : le paquet `armpi`/`pc` gagne des fichiers dans `sbin/`. Pour ne rien casser pendant la transition,
  on garde dans `bin/` un renvoi de deux lignes pour chaque commande déplacée (affiche « déplacé dans /sbin »), une
  version de paquets, puis on le retire.
- **Côté construction** : `build.ps1`, `tools/bin/` et `tools/pc/` reçoivent une liste `SBIN` (nom → `sbin/`).
  Le reste ne change pas.

## 3. Plan par étapes

Chaque étape se construit et s'essaie seule. Les trois premières ne changent rien pour l'utilisateur.

| # | Étape | Contenu | Machine |
|---|---|---|---|
| 1 | **Arbre unique, `HOME:` et réglages centralisés** (fait le 15/11) | `/` = volume système, points de montage dans vfs (`/tmp`, `/media/…`, `/net/…`, `/boot`), plus de `VOLUME:` ; alias `HOME:/` (table par uid prévue) ; `SD0:/home/…` → `HOME:/…` dans tout le dépôt (tools/unixtree.py) ; `/devtools` ; `HOME=/home` ; `uk_conf_path()` (lecture `HOME:/etc` puis `/etc` ; `HOME:/.config/korin` jusqu'au 16/11, lu en repli) | PC et Pi |
| 2 | **Identité** (fait le 16/11, PC ; `MSG_UID` remplacé par l’uid gardé par vfs, `SYS_PROC_UID` et l’avis `SVC_OP_SETUID` : l’en-tête des messages n’a pas de place) | `uid` dans les deux noyaux, `MSG_UID`, `SYS_GETUID/SETUID/PROC_UID`, `SPAWN_UID`, kill limité ; libposix lit `users.ini` (un seul compte, uid 1000, rien ne change à l'usage) ; ktest/ptest étendus | PC puis Pi (asm) |
| 3 | **vfs : règles en mode journal** (fait le 16/11 : `HOME:` par uid, `access.c`, `access = log`, propriétaires dans `V_STATX`) | table de règles, `access.ini`, `st_uid`/`st_mode` calculés, `access()` ; journal des refus ; correction des apps qui écrivent dans `/` | commun (vfs en C) |
| 4 | **authd, sudo, pkgd démon, `/sbin`** (fait le 16/11, PC : authd + `SYS_PROC_SETUID`, sudo, passwd, `/etc/shadow.ini` en PBKDF2-SHA-256, `/etc/elevate.ini`, carte `/sys/authcard` et `ui_elevate` de UIKit, pkgd dans services.txt, Logiciels et volets « machine » de Réglages élevés ; reste : le service `pkg` pour les clients, `useradd`) | boîte d'Elegant, tickets, service `pkg`, clients pkg/Logiciels/pkgman, volets « machine » de Réglages ; commandes d'administration déplacées dans `/sbin` (renvois dans `/bin` pendant une version) | commun |
| 5 | **vfs : refus réels** | `access = enforce` ; init2 passe l'uid du demandeur ; `V_OPEN_AS` pour smbd et ftpd | commun |
| 6 | **Comptes et connexion** (en cours le 16/11, PC : useradd/userdel/usermod dans /sbin, sudo passwd nom, HOME d'après le compte, /sys/login d'après DESIGN-CONNEXION (carte A1/A4, connexion automatique), Réglages › Utilisateurs ; reste : verrou, fermer la session, changer d'utilisateur, /home → /home/<nom>, telnet/ftp/smb par comptes) | login, connexion automatique, Réglages › Utilisateurs (créer, image, mot de passe, admin), étape de setup, migration de `/home` vers `/home/<nom>`, telnet/ftp/smb par comptes | commun |
| 7 | **Plus tard** | changement rapide d'utilisateur, quotas, droits par fichier (`.acl` à la `.exec`), niveaux d'accès des services (fermer `fs.*`/`blk.*` aux apps) | — |

**Règle pendant tout le chantier** : un seul compte en connexion automatique doit donner **exactement** le
comportement d'aujourd'hui. On vérifie à chaque étape :

- suite de commandes ;
- posixtest ;
- démarrage QEMU PC et Pi ;
- installation des paquets.

Chaque changement de fichiers d'un paquet augmente sa version.

## 4. Questions pour l'utilisateur

1. ~~Homes sur le Pi~~ : réglé le 15/11. `HOME:` = `/home/<nom>/` sur les deux machines (§2.4), la partition exFAT
   du Pi sur `/media/sd1`.
2. ~~Élévation~~ : réglé le 16/11. L'élévation est permise aux utilisateurs et groupes de `/etc/elevate.ini`
   (étude INSTALLATEUR.md §5.2) et demande **le mot de passe de l'utilisateur** (comme `sudo`) ; celui de
   `system` (root) ne sert qu'en secours, à la console.
3. ~~Entre utilisateurs~~ : réglé le 16/11. Un home est **invisible** aux autres comptes (`0700`) ; `/home/public`
   est ouvert à tous.
4. ~~Données d'app~~ : réglé le 18/11. Les données des apps vont sous **`~/.local/<app>/`** (`~/.local/jet`,
   `~/.local/koton`…) ; le home ne garde que les fichiers de l'utilisateur (et `HOME:/etc` pour les réglages).
   Pas de migration (installation neuve). Fichiers cache `.local` comme les autres noms en point. À faire : un
   chemin commun dans AppKit/UIKit (à côté de `uk_conf_path`/`korin_conf_path`) que toutes les apps utilisent.
5. ~~Installer des paquets~~ : réglé le 16/11. Installer ou retirer est réservé aux comptes d'`elevate.ini`
   (élévation) ; les mises à jour du dépôt signé se font sans rien demander.
