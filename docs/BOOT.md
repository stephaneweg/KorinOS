# Durée du démarrage

Le journal du noyau (`journal`, qui l'écrit dans `SD0:/journal.txt`) donne l'heure de chaque ligne :

```
[   2.576] init : SD0: pret, verrou du demarrage leve
[   3.799] image SD0:/sys/elegant : chargée en 55 ms
[  19.837] elegant : ecran pris, en service
[  32.187] panel : bureau prêt
```

* **L'heure** est celle du compteur matériel ARM (`cntpct_el0`), en secondes et millisecondes. Sur le Pi, ce compteur
  part avec le firmware : la première ligne du noyau dit donc aussi combien de temps le firmware a pris. Sous QEMU, il
  part de 0.
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
