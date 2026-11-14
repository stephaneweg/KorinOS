---
name: armpi-ntfs-dualboot
description: (ABANDONNÉ pour l'utilisateur le 12/11 : partition KORIN dédiée ; à garder comme fonction facultative pour d'autres) "But final sur le PC : Windows et Korin sur la même partition NTFS ; vfs fait de SD0:/korin la racine SD0:/ ; pilote NTFS en lecture et écriture, tout à la fin"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T16:11:44.274Z
---

Le 12/11/2026, l'utilisateur a fixé le but final sur le ZBook : démarrer aussi bien Windows que Korin sur la même partition NTFS du NVMe.
- Korin vit dans un dossier de cette partition, par exemple `\korin`.
- Une option facultative de vfs fait de ce dossier la racine du volume système : `SD0:/korin` vu comme `SD0:/`, une sorte de chroot.
- Le pilote NTFS doit donc **écrire**, pas seulement lire. L'utilisateur l'a confirmé : « il faudra un accès en écriture après ».
- C'est la tâche de la toute fin du portage, quand la version PC fonctionnera.

**Why:** pas de partition à part pour Korin ; le même disque sert aux deux systèmes.

**How to apply:**
- Garder la règle « jamais d'écriture sur le NVMe » tant que l'utilisateur ne lève pas lui-même cette interdiction pour ce chantier. D'ici là, les essais se font sur des images NTFS et des disques externes.
- À prévoir : démarrage rapide et veille prolongée de Windows éteints, et BitLocker (AES-XTS) ou partition non chiffrée.
- Le chargeur sur l'ESP (FAT32), le menu UEFI à côté de Windows.

Montage : un 4e paramètre facultatif rootpath partout où l'on monte (init.txt : vfs SD0 blk.nvme0p1 ntfs /korin ; mounts.txt ; mount VOLUME: source service rootpath) ; omis ou « / » = comme aujourd'hui ; appliqué par vfs, donc pour tout service FS. PRIORITÉ : rootpath + BOOT: + carte double AVANT NTFS, dès que vfs en C est vérifié (12/11). Aussi (12/11) : une même carte SD pour les deux machines, /korinARM (Pi) et /korinPC (PC) sur la partition 1, chaque initrd monte le sien comme SD0:/ ; SD1: commun ; fichiers de démarrage hors de la racine : volume BOOT: toujours monté (rootpath /), clé boot= dans packages.ini pour que pkg y écrive noyaux, modules, config.txt, firmware (remarque de l'utilisateur), un seul fatfs par partition. Détails dans docs/etude/PC-PILOTES.md. Voir [[armpi-idee-mount-dossier]], [[armpi-idee-unionfs]], [[armpi-x86-pc]].

**Mise à jour du 12/11 (soir)** : l'utilisateur a pu créer une partition KORIN dédiée (voir [[armpi-pc-partition-korin]]) et ne fera PAS de double démarrage sur la partition NTFS de Windows. Le pilote NTFS et le démarrage dans un dossier de la partition Windows restent une fonction **facultative** à envisager pour un utilisateur qui ne veut pas de partitions multiples, en toute fin, sans priorité. rootpath garde son utilité : carte double Pi/PC, et HOME: (SD1:) monté sur le dossier /home de la partition KORIN (mounts.txt : SD1 = blk.nvme0p5, fatfs, /home), même service fatfs que SD0:.
