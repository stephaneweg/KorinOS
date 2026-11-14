---
name: armpi-pocketterm
description: "Matériel de test réel d'ARMPI : Waveshare PocketTerm35 (Pi 4), écran, tactile, clavier USB-C, partition perso"
metadata:
  node_type: memory
  type: project
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-10-28T10:44:04.693Z
---

Le vrai matériel de l'utilisateur est un **Waveshare PocketTerm35** (Raspberry Pi 4 dedans), testé depuis le 28/10/2026 :
- écran HDMI 3,5" : ARMPI en **800x600** par défaut sur la carte (`.\build.ps1 sd E:`, choix de l'utilisateur, comme Onyx ; `-Ecran 640x480` sinon) ;
- tactile GT911 à **0x5D** (pas 0x14 malgré l'overlay Waveshare) : le contrôleur BSC du Pi se fige (étirement
  d'horloge) → I2C **en logiciel** sur GPIO 2/3, qui fonctionne (validé le 28/10/2026 dans le noyau, touch.asm) ; depuis le
  28/10/2026 soir : service `SD0:/sys/svc/gt911` lancé par hwmanager (drivers.txt), à valider sur l'appareil ;
  broche INT du GT911 = **GPIO 4** (overlay Waveshare, confirmé le 31/10/2026 avec gpiowatch : 1 au repos, 0 sous le doigt, 97 impulsions/s) ;
- USB en service (usbd) depuis le 28/10/2026 : clavier intégré (DWC2) + clavier et souris Raspberry Pi sur les
  ports USB-A (xHCI ; le clavier RPi contient un hub 05E3:0610) validés ensemble ; branchement à chaud, clé USB (usbstorage, USB0:), souris derrière le hub du clavier RPi validés aussi ; noyau sans pile USB depuis le 29/10/2026 (branche usb-noyau) ;
- clavier = RP2040 1209:0001 sur le port USB-C (DWC2), HID composite TinyUSB (rapport 1 clavier, 2 souris,
  3 multimédia), EP0 de 64 o : décodé par usb_hid.asm (descripteur de rapport), validé ;
- carte SD 128 Go : partition 1 FAT32 4 Go (boot, /sd), partition 2 **exFAT « UserFS » 115 Go = données perso
  (Roms, Mp3...)**, montée en SD1: ; ne JAMAIS la reformater. **Écriture autorisée par l'utilisateur le 28/10/2026**
  (« tu peux la mettre readwrite ») : FatFS (exFAT) l'écrit, après validation sur une image exFAT d'essai (IMG1:).
- Pas de câble série : `/sd/bin/journal.elf` dans demarrage.txt → `/sd/journal.txt` lisible sur E: (le meilleur
  outil) ; aussi `-d TRACE_FS=1`, `/sd/options.txt` « pas_xhci », `keyb=US`. build.ps1 sd réécrit demarrage.txt.

**Why:** tout pilote « vrai Pi » se valide sur cet appareil ; l'utilisateur lit l'écran et rapporte.
**How to apply:** préparer la carte E: avec build.ps1 sd, garder les pilotes risqués désactivables ; voir [[armpi-projet]].
