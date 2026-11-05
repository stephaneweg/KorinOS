# Durée du démarrage

Le journal du noyau (`journal`, qui l'écrit dans `SD0:/journal.txt`) donne l'heure de chaque ligne :

```
[   2.576] init : SD0: pret, verrou du demarrage leve
[   3.799] image SD0:/sys/elegant : chargée en 55 ms
[  19.837] elegant : ecran pris, en service
[  32.187] panel : bureau prêt
```

* **L'heure** est celle du compteur matériel ARM (`cntpct_el0`), en secondes et millisecondes. Sur le Pi 4, le firmware
  le remet à zéro juste avant de lancer le noyau (première ligne à 0,003 s) : le temps du firmware (EEPROM, `start4.elf`)
  ne se voit pas ici, il se mesure au chronomètre, de la mise sous tension au bureau, moins l'heure du repère.
* **L'heure d'une ligne est celle de son premier caractère.** Une ligne écrite en deux fois, avant et après une
  attente (`hwmanager : usb 2.0 (...) -> ` puis le pid du pilote), garde l'heure de son début et arrive dans le journal
  après les lignes finies pendant l'attente.
* **Le repère du bureau** : `panel : bureau prêt`, écrit par le panel après avoir dessiné sa première image. Le
  bureau est à l'écran quand ce repère et `elegant : ecran pris` sont tous deux passés. Avec l'ancien dock à la place
  du panel, il n'y a pas de repère.

Pour la durée jusqu'au bureau :

```
journal
grep "bureau\|ecran pris\|Korin OS" SD0:/journal.txt
```

## Lignes d'un seul tenant

Le noyau et les programmes écrivent une ligne en plusieurs morceaux, et quatre cœurs écrivent en même temps. Le journal
assemble donc chaque ligne à part, par écrivain : le thread courant, et pour un process, deux écrivains distincts, ce
qu'il écrit lui-même (`print`) et ce que le noyau écrit pendant ses appels (« image … chargée en N ms » d'un
`spawn`). Huit lignes peuvent être en cours à la fois ; au-delà, la plus ancienne part telle quelle. Une ligne de plus
de 232 octets part en plusieurs lignes. `klog` fait partir les lignes commencées avant de copier le journal.

La liaison série et la console texte du noyau reçoivent toujours chaque caractère tout de suite, sans heure : les
scripts d'essai sous QEMU, qui lisent la liaison série, ne changent pas.

Code : `klog_putc`, `klog_flush` dans `src/kernel/console.asm` ; `klog_user` dans `sys_print`
(`src/kernel/syscall.asm`) ; le repère dans `src/sys/panel/panel.asm`.

## Mesure du 5 novembre 2026 (Pi 4, PocketTerm35)

| Étape | Heure |
|---|---|
| noyau : démarrage terminé | 0,264 s |
| init2 lancé (SD0: prêt) | 0,280 s |
| Elegant chargé | 0,576 s |
| panel chargé | 0,627 s |
| `elegant : ecran pris` | 0,954 s |
| **`panel : bureau prêt`** | **1,369 s** |
| clavier USB (xHCI prêt) | 2,06 s |
| Wi-Fi : connexion lancée, connecté | 6,66 s, 9,53 s |
| DHCP, NTP | 12,71 s, 13,25 s |

Le bureau est prêt 1,4 s après le départ du noyau. Le plus long ensuite est le réseau : le Wi-Fi ne lance la
connexion que 5,5 s après le chargement de son firmware, puis le DHCP prend 3 s.
