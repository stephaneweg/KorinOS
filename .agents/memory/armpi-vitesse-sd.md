---
name: armpi-vitesse-sd
description: "Carte SD et cache fatfs (01/11/2026) : sonde du DMA 4 Ko 6465 Ko/s, cache dans main, hello.dll 2,24 s ; UHS 1,8 V derrière /etc/sd_uhs reste à essayer sur le Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: 6cbe2b78-9032-4b62-950f-92e82490eae8
  modified: 2026-11-01T13:44:30.404Z
---

Mesures du Pi le 01/11/2026 : `blkbench` 1 secteur 208 Ko/s, 8 secteurs 1611, 64 secteurs 18408, 256 secteurs 21005 ; `readbench` 4 Ko 453 Ko/s, 256 Ko 15198. Soit environ 2,4 ms par commande de 8 secteurs ou moins, 0,4 ms de surcoût à partir de 64, et 9 ms par lecture de fichier. QEMU ne reproduit pas (pas d'ADMA2, PIO) : la cause se cherche sur le Pi.

Ajouté, non commité au moment de cette note : `BLK_STATS` (7, compteurs préparation, réponse, données, sommeils ; attente active `sd_spin`), `blkbench` et `readbench` en deux passes (dormir, sonder), `BLK_UHS` (8) lancé par init2 si `SD0:/etc/sd_uhs` existe : carte reprise à zéro, ACMD41 S18R, CMD11, expandeur GPIO 4 (1,8 V) et 6 (alimentation), DDR50 sinon SDR25, début de carte relu et comparé, repli en coupant l'alimentation.

Mesure du Pi le 01/11 avec BLK_STATS : la carte n'est pas en cause. Pilote qui sonde : 4 Ko à 6287 Ko/s au lieu de 536. Dormir coûte 2 à 9 ms par commande (1,7 sommeil par commande sur blk.sd0p1), cause exacte non établie. Ajouté : `IRQ_WAKE_BOOST` dans irq_fire_un et sonde par défaut de 6 ms (`SD_SPIN_US`) ; le premier n'a aucun effet observable sous QEMU, qui reste en PIO.

Résultat sur le Pi le 01/11, pilotage à distance (telnet 192.168.0.16, ftp avec XCRC, `reboot` envoyé par une session gardée ouverte 45 s) : `sonde` 4 Ko 6465 Ko/s, 16 Ko 11010, 64 Ko 17579, 256 Ko 19931 ; blkbench 256 secteurs 22321. Sonde 8 ms par défaut, IRQ armée seulement pour dormir (ligne 158 partagée avec wifi : sur le cœur de wifi, dormir reste à 6 ms par commande, sans doute une rafale de réveils de wifi ; le thread 4 de `sd` sur le cœur 3, donc blk.sd0 et SD1, tid 8). WAKE_BOOST dans irq_fire_un et svc_reveil. `hello.dll` 3,28 s stable, chronométré du PC : le disque n'explique pas tout.

Cache de fatfs essayé sur le Pi le 01/11 (branche claude/file-cache-f1060b, 74f4f09, non fusionnée, Pi remis sur main ensuite) : hello.dll de 3,3 s à 2,24 s dès le 3e lancement, CoreLib relue à 300-630 Mo/s sans commande, cachetest PASS sur SD0. `fscache off` sans volume rend -2 : essayer `fscache sd0 off`. Il reste environ 2,1 s hors disque (runtime, JIT). Pas essayé sur SD1.

Essai final de b5317ea sur le Pi (8 Go, 7862 Mo libres) le 01/11 : hello.dll 2,24 s dès le 2e lancement, 3,3 s avec `fscache off`, cachetest PASS sur SD0, `memhog touch 100` tué sans dégât (cache rendu, système intact). Non essayé sur le Pi : pénurie où le cache libère assez pour satisfaire la demande, cache à 256 Mo. Fusionné dans main le 01/11 (b5317ea, avance rapide) ; binaires reconstruits identiques aux essayés (CRC). Pi mis sur ce main le 01/11 (noyau 07617F18, fatfs, fscache, cachetest, memhog) : hello 3,11 s puis 2,25 s, cachetest PASS.

**Why:** l'utilisateur veut l'UHS, la lecture à la demande et le cache fatfs ; ces deux derniers sont dans la session file-cache. Le sous-coût des petites commandes pèse sans doute plus que le bus.
**How to apply:** QEMU n'a validé que la reprise à zéro, pas la tension. Voir [[armpi-chantier-perf]].

Le 04/11 : 1,8 V essayé sur le Pi (DDR50 + ADMA2, blkbench 256 secteurs 41 Mo/s contre 22) ; actif par défaut depuis 795e9d91, coupé par SD0:/etc/no_sd_uhs.
