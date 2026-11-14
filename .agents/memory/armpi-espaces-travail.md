---
name: armpi-espaces-travail
description: "Applet Dock d'Onyx et espaces de travail (31/10/2026) : choix de conception, reste à faire, pièges d'essai ; dans main"
metadata:
  node_type: memory
  type: project
  originSessionId: 96a5758e-f6c4-4321-bec6-3c8beb0581cc
  modified: 2026-10-31T21:03:00.673Z
---

Port de `dockconf` fait sous QEMU le 31/10/2026 dans main depuis le 31/10/2026 (commit 64e8869).

Choix à garder : une fenêtre d'un autre espace = bit `MINI_HORS_BUREAU` de `W_MINI` (composition, clics et focus sautent tout W_MINI non nul) ; `WS_OP_RAISE` va sur l'espace de la fenêtre ; le dock fixe le nombre d'espaces au démarrage ; lecture de dock.ini commune dans `src/inc/dockconf.inc` ; l'applet écrit les clés d'Onyx (`drawer`, `launcher`, `hidden`, `desk`), `tiroir`/`lanceur` restent lues ; taille et corbeille (propres à ARMPI) dans l'en-tête du volet ; Control a une seule taille, le volet d'Onyx 700 x 470 si l'écran le permet.

**Why:** l'utilisateur a demandé d'ajouter au dock ce qui manquait plutôt que de retirer de l'applet ; ARMPI n'avait aucun espace de travail. Les catégories jadis décochées (absentes du fichier) reviennent en tiroirs : seule `hidden` masque.

**How to apply:** reste le menu de fenêtre « Déplacer vers… / Sur tous les bureaux » (ARMPI n'a pas de menu de barre de titre). Essais : `type` de qemu_drive est en disposition US, sur AZERTY `a` donne `q` ; 800x600 = noyau `-d ECRAN_W=800 -d ECRAN_H=600` + `--kernel` + `--ecran`. Voir [[armpi-uikit-fenetres]].
