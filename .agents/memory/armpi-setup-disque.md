---
name: armpi-setup-disque
description: "Setup de Korin OS — étape Disque (étendre, créer, supprimer, logiques), écrite avant les logiciels, fatfs agrandit au montage ; pièges d'essai"
metadata:
  node_type: memory
  type: project
  originSessionId: 65543494-9bfa-4f4d-875d-14e44f3daaf8
  modified: 2026-11-03T16:09:46.716Z
---

Demandé le 03/11/2026 après un essai de la carte min sur le Pi (« aucun paquet » à l'étape Logiciels).

- Étape Disque de Setup (src/sys/setup/disque.h) avant le Wi-Fi : étendre (espace libre juste après), créer (FAT32, espace libre), supprimer (pas p1) ; plus de 4 partitions → logiques p5..p12 (blkserv.inc les sert). Écrit en quittant l'étape, puis redémarrage et reprise au Wi-Fi (SD0:/var/setup-page) : l'utilisateur veut les partitions appliquées avant le téléchargement des logiciels.
- Setup n'écrit jamais dans une partition montée : il change la table, fatfs agrandit le FAT32 au montage (fat_agrandir). Les images ont une FAT pour 128 Go (mkdisk --extensible 128). Écrire BPB/FSInfo à chaud échouait : fatfs réécrivait son ancien compte de libres.
- Le pilote SD lit la capacité (CSD, sd_capacite) ; avant, BLK_INFO donnait 0.
- « Aucun paquet » : le Pi n'a pas d'horloge, avant NTP il est en 1970 et refuse le certificat ; Setup attend net_time/net_ntp_sync, affiche l'erreur, bouton Réessayer.
- Essais : partitionnement et extension du disque essayés sur le Pi par l'utilisateur le 03/11, sans problème. Sous QEMU : images de 16 Go creuses. Redémarrage à chaud sous QEMU : le pilote SD répond parfois trop tard (montage SD0 -2) ; relancer à froid. Clavier AZERTY dans QEMU : « key shift-1 » pour taper 1.
- Publié le 3/11 : armpi 2026.11.3, images refaites (lite 9,5 Mo, full 1,23 Go).
- publish.sh lancé depuis WSL dans un worktree s'arrête à `git rev-parse` (chemin Windows du worktree) : le lancer depuis le dossier principal.

Lié : [[armpi-paquets]], [[armpi-pocketterm]] (partition UserFS : ne jamais y toucher soi-même).
