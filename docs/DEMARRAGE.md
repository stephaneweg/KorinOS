# Ce qui démarre avec Korin

Korin lance ses programmes en trois temps (docs/MICRO-NOYAU.md) :

| Qui | Quoi | Fichier | Compte |
|---|---|---|---|
| init (initramfs du Pi, initrd du PC) | les pilotes de disque, `vfs` | `init.txt`, `initramfs.txt` | système |
| init2 (`src/svc/init2/init2.c`) | `busd`, `authd`, `hwmanager`, `hidmanager`, Elegant, puis les **services** | `/etc/services.txt` | uid 0 |
| Elegant (`src/sys/elegant/desktop.c`, `pocket.c`) | les **programmes de la session** du bureau | `/etc/session`, `/etc/session.poche` (mode poche) | la session |
| init2, une fois la session ouverte | les programmes de l'utilisateur | `HOME:/etc/startup.txt`, sinon `/etc/startup.txt` | la session |

Chaque fichier a une ligne par programme : `chemin [arguments]` ; `#` commente.

Ce qui doit partir à une heure, à intervalles, à l'extinction ou à un événement (une sauvegarde, un nettoyage, un
script à l'ouverture de la session) est une **tâche planifiée** (docs/TACHES.md, Réglages › Tâches planifiées) :
taskd, lancé par init2 après les services, les lance au démarrage, à l'ouverture et à la fermeture de la session, à
l'extinction, et les attend avant d'éteindre.

## Le type de démarrage : Automatique, Manuel, Désactivé

Le volet **Démarrage** de Réglages (`src/apps/control/applets/startup.cpp`, `control startup`), à la manière de la
console Services de Windows, donne à chaque ligne un type, sans toucher au fichier de démarrage : il écrit la ligne,
telle qu'elle est dans ce fichier, dans une liste.

| Type | Services (`/etc/services.txt`) | Session (`/etc/session`, `session.poche`, `startup.txt`) |
|---|---|---|
| Automatique | dans aucune liste : lancé au démarrage | lancé à l'ouverture de la session |
| Manuel | `/etc/services.manual` : lancé quand un service qui en a besoin démarre (`/etc/services.deps`), ou par Démarrer | `session.manual` : seulement par Démarrer |
| Désactivé | `/etc/services.off` (ou la ligne en commentaire) : jamais lancé, même comme dépendance ; init2 refuse `INIT_OP_SVC_START` | `session.off` |

Les listes des services s'écrivent avec l'élévation (authd) ; celles de la session sont personnelles
(`HOME:/etc/`, sinon `/etc/` : `conf_path`), lues par Elegant et init2 (startup.txt).

Pourquoi des listes plutôt que des `#` : `services.txt` et `session` sont des fichiers `config =` du paquet du
système ; un fichier modifié n'est plus remplacé par les mises à jour (elles posent un `.new` à côté). Les listes ne
sont dans aucun paquet : les mises à jour continuent d'apporter les nouveaux services.

`/etc/services.deps` (skel/sd0/etc, skel/pc/etc ; livré avec le système, hors `config =`) : « `service = ce qu'il lui
faut` », des chemins. init2 lance les dépendances d'un service avant lui (au démarrage et pour
`INIT_OP_SVC_START`), sauf celles qui tournent déjà ou sont désactivées (« dependance desactivee » au journal) ; le
volet en tire « Nécessite » et « Requis par », et demande confirmation avant de désactiver un service dont un
automatique a besoin ou d'arrêter un service dont d'autres se servent.

Les volets plus anciens (**Accès distant**, **Serveur web**) coupent encore un service par un `#` dans
`services.txt` ; le volet Démarrage montre ces lignes désactivées et retire le `#` quand on choisit un autre type.
Accès distant lit aussi `services.off` et `services.manual` (un service qui y est n'est pas coché) et l'en retire
quand on le coche.

La comparaison se fait sur la ligne entière, blancs autour ignorés (`list_load`, `list_has` de korinrt ;
`off_has`, `off_set` de Réglages) : deux lignes du même programme avec des arguments différents
(`/sys/svc/imgblk img0 …`, `img1 …`) ont chacune leur type.

## Démarrer, arrêter, redémarrer tout de suite

init2 relance un service surveillé qui s'arrête sur une erreur ; tuer le process ne suffit donc pas. Deux
opérations du service `init2` (appelant en uid 0 : Réglages élevé) :

| Op | Données | Effet |
|---|---|---|
| `INIT_OP_SVC_STOP` (4) | la ligne de `services.txt` | le service de cette ligne n'est plus surveillé, puis il est arrêté -> 0, `E_NOENT` (pas surveillé) |
| `INIT_OP_SVC_START` (5) | la ligne | ses dépendances, puis lui, lancés et surveillés -> pid, `E_EXIST` (tourne déjà), `E_PERM` (désactivé, ou hors de `/sys`) |

Redémarrer = arrêter puis démarrer. Un service que init2 ne surveille pas (lancé par `hwmanager`, ou abandonné après
trois relances) est arrêté par Réglages lui-même. Les programmes de la session appartiennent à l'utilisateur :
Réglages les arrête et les lance directement (un programme de `/sys` passe par `INIT_OP_LAUNCH`, avec l'uid de
l'appelant).

## Ce qui reste automatique

Le volet ne propose aucune action pour ce qu'init et init2 lancent eux-mêmes (`vfs`, `busd`, `authd`,
`hwmanager`, `hidmanager`, Elegant) ; `vterm`, `ps2kbd`, `menubar`, `panel` et, en mode poche, `home` et
`switcher` restent automatiques et ne s'arrêtent pas (les programmes de la session peuvent être redémarrés). Il
demande confirmation pour `netd`, `sound`, `pkgd`, `v3d`, `notifyd` et `clipd`. Ces listes sont dans `st_info` de
`startup.cpp`, avec le nom et la description de chaque programme connu.
