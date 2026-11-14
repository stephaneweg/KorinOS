---
name: armpi-projet
description: "ARMPI OS — OS 64 bits Raspberry Pi 4 en assembleur FASMARM ; décisions de l'utilisateur et état"
metadata:
  node_type: memory
  type: project
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-11-04T18:30:35.603Z
---

ARMPI (C:\Projects\ARMPI) : OS type MenuetOS pour Raspberry Pi 4, 100 % assembleur AArch64 FASMARM.
Origine : commencé from scratch le 28/10/2026, écrit par Claude sous la direction de l'utilisateur ; l'historique git couvre donc tout le projet, malgré son seul nom d'auteur.
Décisions de l'utilisateur (28/10/2026) :
- 64 bits uniquement, SMP, préemptif, contexte FP/NEON sauvegardé, pagination par process.
- Exécutables au format **ELF64** (pas de format plat MenuetOS).
- Licence : **réimplémentation propre**, Circle (ref/circle) sert seulement de carte, pas de copie.
- FASMARM doit être **porté nativement** (pas d'émulateur) → fait : tools/x86toa64.py, identique octet pour octet.
- FAT32 + exFAT, audio PWM/HDMI/USB + micro, « FreeType maison » en asm (src/lib/ttf.asm). Son hors noyau (service sound + pilotes en process) validé sur PocketTerm le 29/10/2026 : jack, casque USB, haut-parleur ; capture validée (crête 8696).
- Pour les pilotes GPU, s'inspirer d'Onyx (C:\Projects\Onyx), OS C++ de l'utilisateur sur Circle.

État au 28/10/2026 : tout testé sous QEMU raspi4b sauf xHCI, GENET, PWM/HDMI/I2S, V3D et WiFi
(src/wifi.asm, firmware dans firmware/wifi/, WPA2 par le supplicant « idsup » de la puce) : écrits,
non testés sur vrai Pi. Dépôt git (branche main) depuis le 28/10/2026. Sources (réorganisées le 28/10 à la demande de l'utilisateur) : noyau dans src/kernel/, arch dans src/kernel/arch/{commun,pi4,pi5}/, programmes dans src/apps/
(carte choisie par -d ARCH=pi4|pi5, interface décrite dans docs/ARCHITECTURE.md).

**Why:** ce sont des choix explicites de l'utilisateur, à ne pas remettre en question.
**How to apply:** garder ces contraintes pour toute suite (WiFi, tests sur vrai Pi) ; voir [[armpi-pieges-fasmarm]].

Organisation de la carte (décision du 28/10/2026) : exécutables **sans extension** ; applications en paquets
`/sd/apps/<nom>.app/` (`main` + `app.txt` manifeste aux clés d'Onyx name/category, plus tard capacités pour
l'agent IA, + config interne) ; `/sd/bin/<nom>` (ligne de commande, fasmarm compris), `/sd/sys/<nom>` (Elegant,
session), toute la config dans `/sd/etc`. Manifestes sources : src/apps/manifestes/<nom>.txt.

Panneau de configuration (décision du 28/10/2026) : **une seule fenêtre comme le control d'Onyx**, les applets
intégrés (modules src/apps/applets/*.inc, pas de fenêtres séparées). Applet Dock = celui d'Onyx (catégories
affichées, application principale, ordre, lanceurs) ; catégories **découvertes dynamiquement** à l'ouverture ;
tout volet trop haut **défile** (molette + barre tactile). Thème avec aperçu.

Exception décidée le 28/10/2026 : **WebKit** (porté d'Onyx, sur une couche POSIX minimale) est compilé sous
Windows par une chaîne C/C++ et n'est PAS construit sur le Pi ; c'est la seule exception à l'auto-hébergement.
Feuille de route détaillée : docs/FEUILLE-DE-ROUTE.md (cases à cocher).
- Fichiers hors du noyau (docs/FICHIERS.md) : etapes 0-5 faites le 29/10/2026 (sur QEMU, non commite a l'etape 5) : files/remote/pipe.asm retires, 12-23/26 = E_NOSYS ; le noyau ne lit AUCUN fichier : kits lus par AppKit (f_kit_load) puis kit_register (99, droits ; sinon init2 INIT_OP_KIT_LOAD), options/WiFi donnes par init (boot_file, 100). A faire plus tard : kitreload, stdio du demandeur pour init2.

GitHub (4/11/2026) : dépôt https://github.com/stephaneweg/KorinOS (remote `origin`) ; CI GitHub Actions demandée le 4/11. Ancienne note (29/10) : pas encore de dépôt ; l'utilisateur doit d'abord choisir le **nom de son OS** (« ARMPI » provisoire) — ne rien pousser avant. Le dépôt Onyx, lui, est https://github.com/stephaneweg/onyx (on peut y pousser quand il le demande). Avant publication : licences de firmware/ (start4.elf) et ref/ (fasm, polices DejaVu) à vérifier avec celle d'ARMPI (pas encore choisie).

Formats audio d'AudioKit (29/10/2026, branche worktree-agent-ad43a96da38fec5b8) : décodage WAV, FLAC, MP3, Ogg Vorbis,
Ogg Opus (CELT+SILK+hybride), encodage WAV/MP3/Opus CELT, API par paquets ak_codec_* pour MediaKit, ak_play, play, Musique.
Code commun kit/banc dans src/kits/audiokit/ak_*.inc ; banc du PC tools/audiotest (QEMU virt + semihosting, run.py, refdec).
