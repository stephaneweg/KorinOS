---
name: armpi-pc-carte-sd-insertion
description: "Lecteur SD du PC (rtsx) : désactivé le 13/11 ; à reprendre lancé seulement quand une carte est présente ou insérée, arrêté avec son fs au retrait"
metadata:
  type: project
---

Le 13/11/2026, sur le vrai ZBook, `rtsx` (Realtek RTS5261, 0000:57:00.0) échoue à l'initialisation : « premier tampon de commandes : -123 (BIPR 8030ff00) : le DMA de la puce ne marche pas ? » ; hwmanager le relance 3 fois puis l'abandonne. Carte absente (elle était dans le Pi).

**Décision de l'utilisateur :** rtsx **désactivé pour l'instant** (ligne `pci:10ec:5261` en commentaire dans skel/sd0/etc/drivers.txt). Quand on le reprendra :
- il ne doit être **lancé que quand une carte est présente, ou dès qu'on en insère une** ;
- il doit **s'arrêter, avec son service de fichiers (fatfs) et le volume, quand on retire la carte**.

Donc : détection de présence sans le pilote complet (hwmanager ou petit veilleur), lancement à l'insertion, démontage + arrêt au retrait. Même idée valable pour sdhci et les clés USB.

Voir [[armpi-x86-pc]], [[armpi-blkdev-modules]].
