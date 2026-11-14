---
name: armpi-idee-ftpfs
description: "Idée pour plus tard (12/11) : un service de fichiers FTP (ftpfs) monté par vfs comme un volume, jusqu'à une partition système sur un serveur FTP"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T19:50:11.290Z
---

Le 12/11/2026, l'utilisateur a eu l'idée d'un client FTP vu comme un système de fichiers : un service FS « ftpfs », au même protocole que fatfs, monté par vfs (`mount NET0: ftp://hôte/chemin ftpfs`, avec rootpath). On pourrait même envisager une partition système en FTP.

À garder pour plus tard ; ce n'est pas un chantier lancé.

**Points notés :**
- **FTP ne sait pas écrire au milieu d'un fichier** : STOR réécrit tout, APPE ajoute à la fin, REST ne sert qu'à lire à partir d'une position. Il faut donc un cache local, qui renvoie le fichier entier à la fermeture.
- **FTP n'a pas de verrous**, chaque opération ajoute de la latence, et il faut MLSD pour les métadonnées.
- **Pour un volume système**, le réseau doit marcher avant que vfs monte SD0: :
  - netd, le pilote et DHCP doivent être dans l'initramfs ;
  - sur le ZBook, le Wi-Fi charge son firmware depuis un disque, ce qui tourne en rond ;
  - l'Ethernet ne pose pas ce problème.
- **Pour la racine**, des protocoles mieux adaptés existent : 9P, NFS, SMB, ou une image en lecture seule sur HTTP avec une couche locale en écriture, comme l'idée UnionFS.

Voir [[armpi-idee-unionfs]], [[armpi-idee-mount-dossier]].
