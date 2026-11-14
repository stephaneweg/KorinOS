---
name: armpi-pc-partition-korin
description: "Sur le ZBook, Korin s'installe sur une partition FAT32 « KORIN OS » de 10 Go (disque 0, entrée GPT 4 = blk.nvme0p4 pour Korin, « partition 5 » et D: pour Windows), créée par l'utilisateur le 12/11"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T19:39:14.248Z
---

Le 12/11/2026, l'utilisateur a créé sur le NVMe du ZBook une partition **KORIN OS** de 10 Go en FAT32 : disque 0, type Basic data. Korin la voit en **entrée GPT 4 (`blk.nvme0p4`, secteurs 977346560 à 998316031)** ; Windows l'appelle « partition 5 ». Elle est entre C: (p3, NTFS) et la récupération (p5), et vue en D: sous Windows. Elle remplace l'idée d'installer Korin dans `\korin` de la partition EFI, trop petite (512 Mo partagés avec Windows) et risquée pour le démarrage de Windows.

Ce jour-là aussi :
- Korin a démarré pour la première fois sur le vrai ZBook, lancé depuis la partition EFI par F9 > Boot From EFI File. Il y a eu 16 cœurs, le GOP en 1920x1080 et le clavier PS/2, mais seulement 900 Mo de mémoire, corrigé ensuite.
- BitLocker est décrypté par l'utilisateur, et Secure Boot désactivé.
- Le carte SD n'est pas vue par l'UEFI HP : le lecteur Realtek PCIe n'a pas de pilote dans le firmware.

**Why:** une partition à part, assez grande pour tout le système, sans toucher aux fichiers de démarrage de Windows.

**How to apply:**
- Korin lit et écrit cette partition par son pilote NVMe, avec écriture permise **seulement** sur KORIN, et tout le reste du disque en lecture seule.
- Le chargeur et le menu peuvent aller sur KORIN même, puisque l'UEFI démarre depuis n'importe quelle partition FAT32.
- Ne jamais écrire ailleurs sur le NVMe.
- Le double démarrage sur NTFS est abandonné pour l'utilisateur (fonction facultative, à la fin). Voir [[armpi-ntfs-dualboot]], [[armpi-x86-pc]].

**13/11 : jalon validé sur le vrai ZBook** : menu de Korin (Korin et Windows démarrent), 32 472 Mo, 16 cœurs, ktest/ptest/itest/mtest/btest/wtest sans échec, pcid 27 fonctions, nvme lit la GPT (règles : p4 servie en blk.korin, tout en lecture seule par -r, garde 0 erreur), vfs monte SD0: sur blk.korin et ls lit /korin. Lecture NVMe ≈ 2 000 Mo/s par 1 Mo. Lancé encore par F9 > Boot From EFI File (pas d'entrée firmware : `korin.ps1 default` à lancer par l'utilisateur s'il veut). Son Intel = DSP (classe 04/01, BAR4 1 Mo) ; I²C 00:15.0 sans BAR (touchpad ?).
Lecteur SD Realtek (rtsx) pas essayé sur le vrai ZBook le 13/11 (la carte était dans le Pi) : l'utilisateur dit de le considérer comme marchant, essai réel plus tard.
