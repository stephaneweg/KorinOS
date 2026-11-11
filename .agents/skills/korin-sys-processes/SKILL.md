---
name: korin-sys-processes
description: Lancer et surveiller des programmes sous Korin OS — lancer une app, lister et arrêter des process, tâches de fond, appels système par process, images préchargées, services d'init2, programmes du démarrage et de la session, journal du noyau, redémarrage. À charger pour lancer, arrêter ou diagnostiquer un programme ou un service.
---

# Process, services et démarrage

Référence : `docs/MICRO-NOYAU.md`, `docs/SHELL.md`, `docs/BOOT.md`, `docs/OUTILS.md`.

## Lancer

- **Une app :** son nom. Le shell cherche dans `PATH`, puis `SD0:/apps/<nom>.app/main`. On peut lui passer des
  arguments : `notes SD1:/a.txt`.
- **En tâche de fond :** `programme &` dans bash, puis `jobs`, `fg` et `bg`.
- **Les programmes de `SD0:/sys` :** lancés du shell, ils passent par init2. Leur sortie ne va pas dans le terminal,
  mais dans le journal.
- **Lancer un programme sans attendre (en C) :** `fork` + `exec` (voir `korin-posix`).

## Lister et arrêter

| Commande | Effet |
|---|---|
| `sh -c ps` | les process (commande intégrée du petit `sh`, il n'y a pas de `/bin/ps`) |
| `kill PID` | dans bash, la commande intégrée de bash (`kill -TERM`, `kill -9`) |
| `sysstat` | pour chaque process : pid, appels système par seconde, total |
| `sysstat <pid|nom>` | un process et ses 8 appels les plus fréquents |
| `preload` | les images de programmes gardées en mémoire |
| `preload <chemin>…` / `unload <chemin>…` | épingler ou retirer une image (démarrage plus rapide des outils lourds) |

L'app **Gestionnaire de tâches** (`taskman`) fait la même chose en fenêtres.

## Services et démarrage

| Fichier | Contenu |
|---|---|
| `SD0:/etc/services.txt` | les services, lancés par init2 avant tout le reste et **relancés s'ils tombent** : vterm, netd, sound, v3d, telnetd, ftpd… (`chemin [arguments]`, `#` commente) |
| `SD0:/etc/drivers.txt` | les pilotes que hwmanager lance selon le matériel |
| `SD0:/etc/startup.txt` | les programmes lancés au démarrage |
| `SD0:/etc/session` | la session du bureau, lancée par Elegant : fond, barre de menus, panneau, notifyd, clipd, agenda, pkgd, printd… |
| `SD0:/etc/session.poche` | la même chose en mode poche |
| `SD0:/etc/preload.txt` | les programmes préchargés au démarrage |

Pour désactiver un service ou un programme de session, commentez sa ligne avec `#`. Cela prend effet au
redémarrage. **Demandez l'accord de l'utilisateur et gardez une copie du fichier** : un service réseau ou vterm
commenté, et l'on perd telnet ou le Terminal.

## Journal et diagnostic

- `journal` copie le journal du noyau dans `SD0:/journal.txt`. Lisez-le avec `tail -50 SD0:/journal.txt` ou
  `rg motif SD0:/journal.txt`.
- La sortie des programmes de `SD0:/sys` et des services y arrive aussi.

## Redémarrer, éteindre

- `reboot` redémarre ; init2 laisse d'abord finir les écritures.
- `reboot tryboot` démarre une seule fois sur le noyau d'essai.
- **Il n'y a pas de commande d'extinction.** L'utilisateur éteint par le dialogue de fin de session : le bouton
  d'alimentation, ou `SD0:/sys/shutdown`, qui ouvre la fenêtre Redémarrer / Éteindre / Annuler.
- **Toujours demander avant de redémarrer**, et prévenir que la séance de l'agent s'arrête aussi.
