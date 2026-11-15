---
name: armpi-smb
description: "Partage SMB (15/11) : serveur smbd et client smbfs sur libsmb2 (choix de l'utilisateur), correctifs de libsmb2, essais Windows et QEMU"
metadata:
  type: project
---

Le 14/11/2026, l'utilisateur a demandé un client et un serveur SMB pour partager des fichiers avec un réseau Windows ;
choix : **serveur minimal en C** et **libsmb2 pour le client**. libsmb2 6.3 ayant aussi un moteur de serveur (NTLMv2,
SPNEGO, signature, SMB 3.1.1), smbd est écrit en C au-dessus de lui (pas de protocole refait à la main).

Fait (branche claude/x86-64-pc, docs/SMB.md) :
- `tools/libsmb2/build.sh` (étiquette libsmb2-6.3.2, ARCH=x86_64 / Pi / NATIVE=1) + `tools/libsmb2/korin.patch` :
  QUERY_INFO brut en passthrough, `smb2_set_server_status` (code NT d'erreur), contexte détruit sans rappel,
  et **bug de libsmb2** : `iov_offset` local à `smb2_read_data` -> signature fausse quand un élément d'une requête
  composée arrive en plusieurs segments (Korin envoie un writev morceau par morceau) ; gardé dans le contexte.
  Correctif à proposer en amont à libsmb2.
- `src/sys/smbd` (main.c, fs.c), `/etc/smbd.ini` ; `src/svc/smbfs` (service de fichiers sur libposix, `mount NET0:
  //hôte/partage smbfs`, `/etc/smbfs.ini`) ; construits par `tools/pc/apps.sh smbd smbfs` ; posix.conf : sautés par
  tools/svc/build_c.sh et tools/sys/build_c.sh.

Pièges : ftruncate de libposix relit et réécrit tout le fichier (smbd remet l'agrandissement demandé par Windows à la
fermeture) ; vfs limite la source d'un montage à 23 caractères ; pas de lecture de /etc pendant le montage (vfs attend).

Essais : smbd vérifié sous Linux et depuis Windows 11 (`net use … /TCPPORT:4445`), puis smbd **dans Korin sous QEMU de
WSL** (pas de QEMU Windows sur ce PC ; OVMF d'Ubuntu sans shell : démarrer par `--esp` + disque NVMe de
mkkorindisk.py, arbre copié du worktree cao-occt). Partage de test de l'utilisateur : `\\192.168.0.9\Korin` (son autre
PC Windows, partagé « Everyone ») : Windows y demande un vrai compte (accès anonyme refusé) ; à tester avec ses
identifiants dans SD0:/etc/smbfs.ini.

**15/11, interfaces (demandes de l'utilisateur)** : Fichiers › Volume › « Se connecter à un serveur… » (Ctrl+K) monte
en NETn:, garde TOUJOURS le compte (SD0:/etc/smbfs.ini) et ajoute la ligne à mounts.txt (remonté au démarrage
jusqu'au démontage, qui garde le compte). Volet de Réglages « Partages » (sys/applets/shares.so, avec smbd) : par
partage le dossier, le TYPE (smb ; ftp à venir, pour ftpd/« ftpfs ») et le nom pour SMB, dans SD0:/etc/shares.ini
commun à tous les types (src/sys/smbd/shares.h) ; compte d'accès dans smbd.ini ; smbd relit à chaud. Vérifié sous
QEMU par captures (pilote du moniteur QEMU : scratchpad drive.py, souris PS/2 relative, pas de molette).

**How to apply:** suite : srvsvc (liste des partages), WS-Discovery, CHANGE_NOTIFY, volet de Réglages, paquets PC,
Pi. Voir [[armpi-x86-pc]], [[armpi-idee-ftpfs]], [[armpi-idee-web]].
