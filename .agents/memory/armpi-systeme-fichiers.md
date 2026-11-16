---
name: armpi-systeme-fichiers
description: Étude du 16/11/2026 (docs/etude/SYSTEME-FICHIERS.md) : ext4 maison + indexd pour /home sur le NVMe, étiquettes et couleurs en xattrs ; rien d'implémenté, questions en attente
metadata:
  type: project
---

Le 16/11/2026, l'utilisateur a demandé quel système de fichiers remplacerait exFAT/FAT32 sur le NVMe de 512 Go du
ZBook : performant, fiable, économe en place, avec une indexation (étiquettes et couleurs sur les fichiers,
recherche par étiquette, couleur, extension).

Étude : `docs/etude/SYSTEME-FICHIERS.md`. **Rien n'est implémenté, aucune décision de l'utilisateur encore** (9
questions au §9).

Recommandation :
- **ext4** réimplémenté en C (`src/svc/ext4fs`) d'après la documentation du format, pour `/home` d'abord ; `/` reste
  en FAT32 (le chargeur UEFI lit son propre volume) ;
- `e2fsck -fn` sous WSL sert d'oracle à chaque étape, e2fsprogs porté comme programme ;
- étiquettes et couleur en xattrs `user.korin.tags` et `user.korin.color` (couleur = un nom traduit en jeton du
  thème) ;
- un service `indexd` tient des index en B+ arbre par volume (nom, extension, taille, date, étiquette, couleur),
  nourri par un journal des changements de type USN tenu par le service du volume. L'index est un cache, qu'on peut
  reconstruire.
- Le système maison façon BFS est écarté faute d'oracle et de fsck (alternative si la lisibilité hors de Korin
  ne compte pas).

Mesure utile : sur `build/sdroot` (16 144 fichiers, 354 Mo), des clusters de 128 Ko (exFAT de plus de 32 Go)
perdent 1,8 Go, contre 36 Mo pour des blocs de 4 Ko.

**Why:** préparation de l'installation sur le NVMe et du multi-utilisateur ([[armpi-x86-pc]]).

**How to apply:** avant de toucher au stockage, aux droits par fichier ou à une recherche dans Fichiers, relire
l'étude et ses réponses (§9). Prérequis repérés :
- `nvme` n'a ni TRIM ni op de vidage pour un client (`BLK_FLUSH` à ajouter avant tout journal) ;
- le protocole FS n'a ni xattrs ni uid, ni mode, ni inode.

`/media/sd1` du Pi n'est jamais converti ni annoté.
