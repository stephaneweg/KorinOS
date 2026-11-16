---
name: korin-sys-packages
description: Installer, mettre à jour et retirer des logiciels sur Korin OS avec pkg (paquets .opk, dépôt, modes de mise à jour, paquets système appliqués au redémarrage). À charger pour toute installation ou mise à jour sur la machine ; pour construire et publier des paquets depuis le dépôt, voir korin-dev-packages.
---

# Paquets sur la machine

Référence : `docs/PAQUETS.md`. L'app **Logiciels** (`pkgman`) fait la même chose en fenêtres. Le service `pkgd`
cherche les mises à jour en arrière-plan.

| Commande | Effet |
|---|---|
| `pkg list [filtre]` / `pkg list -a` | les paquets installés / tout le dépôt |
| `pkg info NOM` | ce que sait le dépôt du paquet (version, résumé…) |
| `pkg add NOM…` ou `pkg add x.opk` | installer depuis le dépôt ou depuis un fichier |
| `pkg delete [-p] NOM…` | retirer (`-p` : les réglages changés aussi, jamais `home/`) |
| `pkg update NOM` / `pkg update -a` | mettre à jour un paquet / tous |
| `pkg upgrade` | un tour de pkgd (index, paquets en mode `auto`) |
| `pkg check` | relire l'index du dépôt |
| `pkg mode NOM manual|auto|never` | comment pkgd met à jour ce paquet (`manual` par défaut) |
| `pkg commit` | appliquer les paquets étagés (init2 le fait seul au démarrage) |
| `-r DÉPÔT` | utiliser un autre dépôt |

Les réglages de pkg sont dans `/etc/pkg/pkg.ini`.

## Pièges

- On ne met pas à jour une app ouverte : demandez à l'utilisateur de la fermer.
- Les paquets à `restart = 1` (`armpi` : noyau, kits, services, `bin`, `etc`… ; `pi-firmware`) sont étagés dans
  `/var/pkg/stage/`, puis **appliqués au redémarrage** par init2 (`/var/pkg/commit.log`). L'ancien noyau reste en
  `kernel8.img.old`.
- Une app ajoutée ou retirée relance `panel` et `menubar`.
- **Demandez toujours l'accord avant `add`, `delete`, `update` ou `upgrade`** : nommez les paquets, et la taille quand
  `pkg info` la donne.
- Il faut le réseau, et une horloge juste : HTTPS.
