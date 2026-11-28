# Les tâches planifiées : taskd

Lancer un programme, un script ou une application à une heure, à intervalles, au démarrage, à l'extinction ou quand
quelque chose arrive (le réseau revient, une clé USB est branchée, le secteur est débranché…), comme cron, anacron,
les minuteurs de systemd ou le Planificateur de tâches de Windows. Deux niveaux :

| Niveau | Fichiers | Lancées | Quand |
|---|---|---|---|
| **Utilisateur** | `HOME:/etc/tasks/<id>.task` ; état et sortie dans `HOME:/var/tasks/` | sous le compte de la session | tant que sa session est ouverte |
| **Système** | `/etc/tasks/<id>.task` ; état et sortie dans `/var/tasks/` | en uid 0 | toujours ; les écrire demande l'élévation |

| Pièce | Où | Rôle |
|---|---|---|
| `taskd` | `src/svc/taskd/taskd.c`, `/sys/svc/taskd` | le service, sur le runtime du système (korinrt, pas libposix) ; lancé par init2 |
| le cœur | `src/svc/taskd/taskfile.c`, `.h` | le fichier d'une tâche, ses déclencheurs, le calendrier ; du C sans bibliothèque, commun aux trois |
| l'applet | `src/svc/taskd/applet/applet.cpp`, `/sys/applets/tasks.so` | Réglages › Tâches planifiées (PC ; le Pi ne charge pas encore les modules de Réglages) |
| `tasks` | `src/bin/tasks/tasks.c`, `/bin/tasks` | la ligne de commande (Pi et PC) |

## Le fichier d'une tâche

```
name    = Sauvegarde des notes
enabled = 1
kind    = shell                     program (chemin [arguments]) | app (nom d'une app) | shell (une ligne de bash)
run     = cp -r ~/notes /media/usb0/
when    = daily 03:00               une ou plusieurs lignes : la tâche part à la première qui arrive
catchup = 1                         un passage manqué (machine éteinte) est rattrapé au démarrage
delay   = 0                         secondes entre le déclencheur et le lancement
timeout = 600                       secondes, puis le programme est arrêté (0 : sans limite)
ac_only = 0                         seulement sur secteur
net_only = 0                        seulement avec le réseau
notify  = fail                      never | fail | always : une notification à la fin
```

L'id est le nom du fichier (`a-z`, `0-9`, `-`, `_`). Un `program` qui finit par `.sh` est lancé par bash ;
`HOME:/` et `~/` au début du chemin d'une tâche de l'utilisateur deviennent son dossier. Une tâche de l'utilisateur
démarre dans son dossier.

| Déclencheur (`when`) | Part |
|---|---|
| `every <n> m\|h\|d` | toutes les n minutes, heures, jours depuis le dernier passage (comme anacron : un intervalle passé machine éteinte part au démarrage) |
| `daily HH:MM` | chaque jour |
| `weekly mon,tue,wed,thu,fri,sat,sun HH:MM` | certains jours |
| `monthly <jour> HH:MM` | le jour du mois ; 29 à 31 : le dernier jour d'un mois plus court |
| `once AAAA-MM-JJ HH:MM` | une fois |
| `startup` | système : au démarrage de Korin ; utilisateur : à l'ouverture de sa session |
| `shutdown` | système : à l'extinction ; utilisateur : à la fermeture de sa session (et à l'extinction) |
| `event <sujet> [valeur]` | un sujet du bus (docs/BUS.md ; `préfixe*` permis), à chaque valeur nouvelle qui convient au motif (`*`, `?` ; vide : toute valeur) |
| `removed <sujet>` | le sujet retiré du bus (une clé débranchée, un volume démonté) |
| `power ac`, `power battery` | passage sur secteur, sur batterie (`korin.power.battery`) |
| `battery <n>` | la batterie descend sous n %, sans secteur |

`power`, `battery` et `removed` partent quand la condition **devient** vraie, pas à chaque publication qui la
confirme ; `event` part à chaque valeur nouvelle (une valeur redite ne relance pas). Ce qui est déjà là quand taskd
s'abonne (une clé déjà branchée) ne lance rien.

Les heures sont locales (`SYS_CLOCK_GET`, le fuseau compris). Une échéance passée depuis plus de 10 minutes (machine
éteinte, mise en veille, heure avancée par NTP) n'est rattrapée qu'avec `catchup = 1`, une seule fois ; taskd
attend 45 s après le démarrage avant les échéances d'heure (l'heure n'est sûre qu'après NTP). Une tâche qui tourne
encore n'est pas relancée ; une tâche dont une condition manque (secteur, réseau) saute ce passage.

## L'état et la sortie

taskd écrit `<racine>/var/tasks/<id>.state` (`seen`, `last`, `code`, `runs`, `duration`, `running`, `next` :
secondes UTC, ms) à chaque début, fin et relecture, et met la sortie et les erreurs du dernier passage dans
`<id>.log` (une ligne d'en-tête : l'heure et la commande). Codes à part : -1001 arrêtée (trop longue), -1002 pas
lancée, -1000 fin vue sans code. Le journal du noyau a une ligne par début et par fin (`taskd : user sauvegarde :
lancee (pid 41)`).

## Le service

Sans sondage : taskd dort sur le bus (`bus_wait`) jusqu'à la prochaine échéance, une heure au plus, 30 s sans busd,
une demi-seconde tant qu'une tâche tourne (sa fin par `SYS_WAIT` étendu, sans attendre ; sa durée maximale).

| Sujet | Qui | Effet |
|---|---|---|
| `korin.tasks.changed` | l'applet, `tasks`, après un changement de fichier | taskd relit tout (ce qui tourne et le calendrier suivent) |
| `korin.tasks.run` | « Exécuter maintenant » : `system <id>` ou `user <id>` | la tâche lancée tout de suite, même désactivée |
| `korin.tasks.login` | `taskd phase login` (init2, à l'ouverture de la session) ; gardé | les tâches de l'utilisateur relues, ses `startup` lancées (une fois par ouverture : `/tmp/taskd.login`) |
| `korin.tasks.state` | taskd | `<system\|user> <id> run\|end <code>` à chaque début et fin (pas gardé) |
| `korin.clock.offset` | netd, l'Horloge | les échéances recalculées |

init2 (`src/svc/init2/init2.c`) :

* lance `/sys/svc/taskd` (surveillé) après les services de `services.txt`, s'il est là ; pas par `services.txt`,
  qu'une mise à jour ne change plus quand l'utilisateur l'a modifié ;
* à l'ouverture de la session, après `startup.txt` : `taskd phase login` ;
* avant de fermer la session (`INIT_OP_LOGOUT`) : `taskd phase logout`, qui lance les tâches `shutdown` de
  l'utilisateur et les attend ; avant d'éteindre ou de redémarrer (`INIT_OP_REBOOT`), `taskd phase shutdown` :
  celles de l'utilisateur, puis celles du système. Chacune 30 s au plus (sa durée maximale si elle est plus
  courte), 60 s en tout ; init2 sert les demandes pendant ce temps.

Les tâches `startup` du système partent une fois par démarrage (`/tmp/taskd.boot`) : un taskd relancé après une
panne ne les relance pas.

## L'applet

Réglages › **Tâches planifiées** : « Mes tâches » ou « Tâches du système » en tête, la liste (tâche, quand, dernier
passage, prochain, rafraîchis chaque seconde depuis les `.state`), et Nouvelle tâche, Modifier, Exécuter maintenant,
Activer / Désactiver, Sortie (le `.log`), Supprimer. Le dialogue d'une tâche n'a que des listes et des champs
simples : quoi lancer (une application de la liste, un programme ou un script avec Choisir…, une ligne de bash),
quand (à intervalles, chaque jour, certains jours avec une case par jour, chaque mois, une fois, au démarrage, à
l'extinction, à un événement choisi parmi des phrases : « Le réseau est connecté », « Un volume est monté »,
« La batterie descend sous un seuil »… ou un sujet du bus), les options, et une phrase qui dit ce qui va se
passer (« Lancera « sauvegarde.sh » du lundi au vendredi à 18:30, sur secteur seulement. »). Il ne montre que le
premier `when` d'un fichier qui en a plusieurs (« (+1) ») et garde les autres.

## La commande

```
tasks                                   les tâches : les miennes, puis celles du système
tasks show <id>                         son fichier et son état
tasks run <id>                          la lancer tout de suite (par taskd)
tasks log <id>                          la sortie de son dernier passage
tasks enable|disable <id>
tasks add <id> "<quand>" <commande...>  une tâche nouvelle (une ligne de bash)
tasks remove <id>
sudo tasks -s ...                       les tâches du système
```

```
tasks add sauvegarde "daily 03:00" cp -r ~/notes /media/usb0/
tasks add photos "event korin.storage.volume.* blk.usb*" ~/bin/importer-photos.sh
```

## Limites

* Le Pi n'a pas encore l'applet (Réglages n'y charge pas de module : control/README.md) : la commande `tasks`.
* Arrêter une tâche trop longue arrête son process, pas ceux qu'il a lancés (un script : ses commandes en cours).
* Tout programme peut publier sur le bus (docs/BUS.md, « Les droits ») : `korin.tasks.run` et les sujets des
  déclencheurs peuvent faire partir une tâche plus tôt, jamais en faire lancer une autre que celle écrite par son
  propriétaire.
* Les tâches de l'utilisateur ne tournent que pendant sa session (pas de « que l'utilisateur soit connecté ou non »).
