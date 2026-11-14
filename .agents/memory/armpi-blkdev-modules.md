---
name: armpi-blkdev-modules
description: "Choix du 12/11 pour les périphériques de blocs : un serveur blkdev générique qui charge le pilote matériel comme un module (modèle 2), à faire quand tout sera stable"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T20:26:36.237Z
---

Le 12/11/2026, l'utilisateur a choisi le **modèle 2** pour les périphériques de blocs :
- un serveur générique **`blkdev`** qui porte la couche d'abstraction (protocole `blk.*`, partitions MBR et GPT, bornes, clients, protection en écriture, retrait du support) ;
- hwmanager (ou usbd pour les clés USB) en lance **une instance par périphérique trouvé** ;
- cette instance charge le **module du pilote matériel** (nvme, sdhci, rtsx, usbstorage, imgblk…) : une bibliothèque partagée qui exporte une petite table (`init`, `read`, `write`, `flush`, `present`), figée comme une table de kit (on ajoute, on ne retire pas), chargée par le même mécanisme que les kits (`LIB_OPEN`).

**Pourquoi :** il y aura au moins trois sortes de périphériques de blocs (NVMe, disque USB, carte SD) ; l'abstraction vit une seule fois et se met à jour seule, et un pilote n'est plus qu'un petit module.

**Quand :** « quand tout sera stable », pas pendant le portage.

**D'ici là :**
- On garde un pilote complet par périphérique, avec `blkserv.c` compilé dedans.
- On réunit d'abord les deux copies de `blkserv.c` (`src/svc/common` et `src/svc/sdmmc`) en une seule.
- Cette copie unique prend déjà la forme du modèle 2 : chaque pilote lui donne une structure de fonctions, et c'est elle qui fait la boucle de service.

Même modèle, plus tard, pour le son (snddev : sorties USB, PWM, HDMI, HDA ; entrées ; couche commune sndif) et le réseau (netdev : couche netif) ; les pilotes propres au Pi restent en asm.

Voir [[armpi-x86-pc]], [[armpi-pc-partition-korin]].
