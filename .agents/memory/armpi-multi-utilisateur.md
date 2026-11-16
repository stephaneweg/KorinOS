---
name: armpi-multi-utilisateur
description: Demande du 15/11 — plusieurs utilisateurs (/home/<nom>), utilisateur système seul à écrire /bin /apps /sys /etc, élévation pour les mises à jour, /sbin séparé de /bin ; étude docs/etude/MULTI-UTILISATEUR.md, rien de fait
metadata:
  type: project
---

Le 15/11/2026, l'utilisateur a demandé d'étudier le multi-utilisateur :
- des dossiers par utilisateur dans `/home`, comme sous Unix ;
- un utilisateur système, seul autorisé à modifier `/bin`, `/apps`, `/sys` et `/etc` ;
- une élévation de droits pour les mises à jour ;
- un **`/sbin`** séparé de `/bin` pour les commandes système.

L'étude est faite (non commitée au moment de l'écriture) : docs/etude/MULTI-UTILISATEUR.md. Elle propose :
- un uid par process dans le noyau, recopié dans les messages (MSG_UID) ;
- des règles par chemin dans vfs (FAT n'a pas de propriétaire), d'abord en mode journal ;
- authd et sudo pour l'élévation ;
- pkgd comme démon system ;
- `~/.config/korin` pour les réglages personnels ;
- login avec connexion automatique par défaut quand il n'y a qu'un compte.

Cinq questions restent posées à l'utilisateur à la fin de l'étude (§4).

**Why:** c'est une évolution de [[armpi-pas-de-securite-attaquant]]. Le cadre reste la résistance aux bugs et la séparation des données, pas un système durci.

**How to apply:**
- Avec un seul compte en connexion automatique, le comportement doit rester exactement celui d'aujourd'hui.
- Sur le Pi, ne jamais déplacer de données vers SD1:.

Voir [[armpi-volumes-sys-home]], [[armpi-x86-pc]].

**15/11 (suite), proposé par l'utilisateur :**
- `HOME:` est un volume virtuel de vfs qui désigne toujours `SYS:/home/<utilisateur de l'appelant>/` (§2.4 de l'étude).
- Les apps écrivent `HOME:/…`. Le remplacement de `SD0:/home` par `HOME:` est mécanique et peut se faire dès l'étape 1 (alias vers `SD0:/home/` tant qu'il n'y a pas d'uid).
- Remplace l'ancien projet `SD1: → HOME:`. Sur le Pi, la partition exFAT garde `SD1:` ou devient `DATA:` (à choisir).

**15/11 soir** : étape 1 faite avec l'arbre unique (voir [[armpi-arbre-unix]]) : `HOME:` = /home pour tous (table par uid à faire à l'étape 2), réglages personnels dans HOME:/.config/korin. Prochaine étape : uid dans les noyaux.
