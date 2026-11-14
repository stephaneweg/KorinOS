---
name: armpi-busd
description: "Bus d'événements busd + BusKit + outil bus, korin.theme, USB et volumes (4/11) : tout dans main ; USB/volumes sur la carte, pas encore essayé sur le Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: 7400838a-2aca-45ee-b2e7-008de25ae810
  modified: 2026-11-04T18:20:17.872Z
---

Demandé le 04/11/2026 : « un bus pour les évènements à la manière de dbus/com/xpc, on l'appelle busd ».
busd dans main (2126992e, poussé) : `src/svc/busd` (service « bus », lancé en premier par init2, SURV_MAX 24),
`src/kits/buskit`, `src/bin/bus`, `src/inc/bus.inc`, docs/BUS.md. Noyau : `SVF_POST` (8) = droit à WS_POST ;
signaux = WEV_BUS (13) dans la file d'ui_event ; BUS_RETAIN = état courant, à la PPS.

Thème (même jour) : 03ba6a82 poussé dans origin/main le 4/11 ; main local = fusion avec 795e9d91 de l'autre session (non poussé par moi).
Panneau publie korin.theme ; UIKit s'abonne dans ui_init (appel direct au service), uk_retheme redessine,
WEV_THEME (14) au programme ; voronoy (seeds refaits), menubar, panel, agenda, stickies, calculatrice le traitent ;
plus de relance de la session sauf sans busd. Déployé sur le Pi le 4/11 par FTP (CRC vérifiés, originaux dans
scratchpad/pi_backup), Pi redémarré : busd tourne, 6 abonnés. Pi en armpi 2026.11.11 + ces fichiers.
Essai de l'utilisateur sur le Pi (4/11) : Givre → autre thème par le panneau, Arabesque ouverte, tout a changé
instantanément partout. Validé.

USB et volumes (4/11) : dans origin/main (16e71df6) ; copié sur la carte du Pi (E:) : vfs.mod, sys/svc/vfs, usbd, files.app (originaux dans scratchpad/sd_backup_usb). vfs au démarrage = vfs.mod de l'initramfs.
Le 4/11 au soir, les fichiers du thème sur le Pi étaient revenus à 2026.11.11 (init2 sans busd : « busd ne répond pas ») ; renvoyés par deploy_pi.py, Pi redémarré, `bus list` montre volumes et USB. Avant d'accuser le code : comparer les CRC du Pi (XCRC) à pi_backup et au build.
usbd publie korin.usb.device.<n>, vfs korin.storage.volume.<vol> (thread des montages attend busd puis publie
le démarrage), via src/svc/inc/buspub.inc ; Fichiers abonné, plus de sondage. Essayé sous QEMU : clé ajoutée à
chaud par hmp drive_add/device_add, retirée par device_del. Suite : manettes, réseau, son.

Réseau (4/11, dans origin/main fc6bb0e4, déployé sur le Pi : korin.net.wifi = connected 1 Wegener_Wifi, originaux dans scratchpad/pi_backup_net) : docs/POLLING.md (inventaire des
sondes) ; netd publie korin.net.state (background.inc), pilote wifi korin.net.wifi (pub_thread, signal relu
toutes les 5 s) ; menubar, wifimenu, applet Wi-Fi abonnés à korin.net.* ; QEMU : wifi simulé par « wifi test »
dans startup.txt, réseaux Maison/Telephone/Cafe ouvert. Restent IRC, pkgd (C++), NetKit wait_dhcp, serveurs.

Son, heure, Disques, usbstorage (8/11) : dans origin/main (6dc25154), publiés en armpi 2026.11.26, clock 1.0.6, disks 1.0.5 (e482c3fd).
korin.audio.state (sound, state_update), korin.clock.offset (netd après NTP/fuseau, Horloge) ; menubar lit son et clock.cfg
au lancement puis au bus, heure redessinée à la minute ; Mixer, Horloge, clockd, Disques (C++ : tools/office/build.sh via WSL,
liens build/posix et build/gcc vers le dossier principal), usbstorage. QEMU OK. Principe de l'utilisateur : le bus évite de
relire l'état dans un fichier (moins d'accès disque).

**Why:** l'utilisateur veut la pub/sub d'abord ; il a proposé que les programmes du bureau s'abonnent au lieu
d'être tués. Un paquet armpi a été commencé puis mis de côté au profit du déploiement direct.

**How to apply:** le dossier principal porte du travail non commité d'une autre session (noyau, sd, Elegant) :
construire et déployer depuis un worktree propre. Pi : telnet/FTP 192.168.0.16, utilisateur ftp armpi, mot de
passe = nom de l'OS en minuscule. XCRC rend « 250 <hex> ». Pièges : SVC_CALL rend le résultat du service
(négatif possible) ; noms réservés FASMARM `subs`, `word` ; macro `jet` de UIKit définie après uikit_theme.inc.
Voir [[armpi-pieges-fasmarm]], [[armpi-paquets]].
