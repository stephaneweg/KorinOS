---
name: armpi-wifi-coupures
description: "Debit Wi-Fi irregulier du Pi (31/10/2026) : coupures PC vers appareils Wi-Fi, hors de netd et du pilote ; sondes de fin de rafale ajoutees ; cause exacte non tranchee"
metadata:
  node_type: memory
  type: project
  originSessionId: 74caf4e5-1f38-4e1e-9ffb-ee266b51c1eb
  modified: 2026-10-31T20:01:47.024Z
---

Etabli le 31/10/2026 (fusionne dans main, 83bbb3c) : les RTO en envoi viennent d'accuses du PC perdus dans des coupures de 100 a 200 ms du sens PC vers Pi, pas de donnees perdues. Le Pi joint tout le monde sans perte sauf le PC ; les deux PC (meme modele, Intel AX201, Windows) perdent vers tout ce qui est en Wi-Fi, pas vers la box. Le meme chemin fonctionne depuis le Pi. Reste a trancher : relais de la box ou couple AX201/Windows ; test attendu : `ping -c 60 192.168.0.16` depuis le Mac en Wi-Fi.

**Why:** evite de rechercher dans le pilote : `roam_off`, `ampdu_rx` 0, `txbf_bfe_cap` 0 et l'arret des lectures periodiques de la puce n'ont rien change. Une television ou une imprimante en veille repond mal aux pings : seule la cible Pi est probante.
**How to apply:** les mesures de debit depuis ces PC sont bornees par ces coupures ; comparer toujours en alternance A/B, car elles varient de 2 a 55 % selon le moment. Voir [[armpi-chantier-perf]], docs/RESEAU.md « Performances du Wi-Fi ».

Taille d'ecran ecartee le 31/10/2026 : 10 tours, envoi / reception en Mo/s : 1920x1080 8,7 / 5,8 ; 640x480 6,3 / 3,8 ; 800x600 7,0 / 3,4 puis 5,1 / 5,2. Deux series a la meme taille different autant que deux tailles ; un grand ecran ne freine pas. Pas d'alternance A/B, une serie par taille grande ou petite. Aller-retour a 2,0 ou 3,0 ms selon les moments, a toutes les tailles, cause inconnue. Vus une fois chacun : tour d'envoi a 0,00 Mo/s sans RTO, reseau vivant juste avant et apres, sur un tcpbench tout juste lance ; FTP `425 Pas de connexion de donnees` apres un delai sur le port de donnees.

Reseau de la maison : .1 box, .2 second PC, .3/.5 sans doute les smartphones, .6 sans doute decodeur TV, .7 sans doute le Mac, .8 Switch, .9 PC, .13 television Sony, .14 imprimante HP, .16 Pi.
