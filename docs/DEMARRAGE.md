# Ce qui démarre avec Korin

Korin lance ses programmes en trois temps (docs/MICRO-NOYAU.md) :

| Qui | Quoi | Fichier | Compte |
|---|---|---|---|
| init (initramfs du Pi, initrd du PC) | les pilotes de disque, `vfs` | `init.txt`, `initramfs.txt` | système |
| init2 (`src/svc/init2/init2.c`) | `busd`, `authd`, `hwmanager`, `hidmanager`, Elegant, puis les **services** | `/etc/services.txt` | uid 0 |
| Elegant (`src/sys/elegant/desktop.c`, `pocket.c`) | les **programmes de la session** du bureau | `/etc/session`, `/etc/session.poche` (mode poche) | la session |
| init2, une fois la session ouverte | les programmes de l'utilisateur | `HOME:/etc/startup.txt`, sinon `/etc/startup.txt` | la session |

Chaque fichier a une ligne par programme : `chemin [arguments]` ; `#` commente.

## Ne plus lancer un programme : les listes `.off`

Le volet **Démarrage** de Réglages (`src/apps/control/applets/startup.cpp`, `control startup`) coupe un programme
sans toucher au fichier de démarrage : il écrit la ligne, telle qu'elle est dans ce fichier, dans une liste.

| Liste | Pour | Lue par | Écrite |
|---|---|---|---|
| `/etc/services.off` | les lignes de `/etc/services.txt` | init2 | avec l'élévation (authd) |
| `session.off` (`HOME:/etc/`, sinon `/etc/` : `conf_path`) | les lignes de `/etc/session`, `/etc/session.poche`, `startup.txt` | Elegant, init2 | par l'utilisateur, sans élévation |

Pourquoi des listes plutôt que des `#` : `services.txt` et `session` sont des fichiers `config =` du paquet du
système ; un fichier modifié n'est plus remplacé par les mises à jour (elles posent un `.new` à côté). Les listes ne
sont dans aucun paquet : les mises à jour continuent d'apporter les nouveaux services.

Les volets plus anciens (**Accès distant**, **Serveur web**) coupent encore un service par un `#` dans
`services.txt` ; le volet Démarrage montre ces lignes coupées et retire le `#` quand on les rallume. Accès distant lit
aussi `services.off` (un service qui y est n'est pas coché) et l'en retire quand on le coche.

La comparaison se fait sur la ligne entière, blancs autour ignorés (`list_load`, `list_has` de korinrt ;
`off_has`, `off_set` de Réglages) : deux lignes du même programme avec des arguments différents
(`/sys/svc/imgblk img0 …`, `img1 …`) se coupent l'une sans l'autre.

## Arrêter et démarrer tout de suite

init2 relance un service surveillé qui s'arrête sur une erreur ; tuer le process ne suffit donc pas. Deux
opérations du service `init2` (appelant en uid 0 : Réglages élevé) :

| Op | Données | Effet |
|---|---|---|
| `INIT_OP_SVC_STOP` (4) | la ligne de `services.txt` | le service de cette ligne n'est plus surveillé, puis il est arrêté -> 0, `E_NOENT` (pas surveillé) |
| `INIT_OP_SVC_START` (5) | la ligne | lancé et surveillé -> pid, `E_EXIST` (tourne déjà) ; seulement `/sys/…` |

Un service que init2 ne surveille pas (lancé par `hwmanager`, ou abandonné après trois relances) est arrêté par
Réglages lui-même. Les programmes de la session appartiennent à l'utilisateur : Réglages les arrête et les lance
directement (un programme de `/sys` passe par `INIT_OP_LAUNCH`, avec l'uid de l'appelant).

## Ce qui ne se coupe pas

Le volet ne propose pas d'interrupteur pour ce qu'init et init2 lancent eux-mêmes (`vfs`, `busd`, `authd`,
`hwmanager`, `hidmanager`, Elegant), ni pour `vterm`, `ps2kbd`, `menubar`, `panel` et, en mode poche, `home` et
`switcher` ; il demande confirmation pour `netd`, `sound`, `pkgd`, `v3d`, `notifyd` et `clipd`. Ces listes sont
dans `st_info` de `startup.cpp`, avec le nom et la description de chaque programme connu.
