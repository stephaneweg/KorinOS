---
name: armpi-mode-poche
description: "Mode poche (5/11/2026) : maquettes du designer UX validées par l'utilisateur, accueil variante A ; décisions restantes"
metadata:
  node_type: memory
  type: project
  originSessionId: 0268d609-ebc4-4e47-a5a3-79fdd81ea29e
  modified: 2026-11-05T21:20:37.029Z
---

Maquettes : https://claude.ai/artifact/1qG21UencFn3UUzGWxPx6y (source dans le scratchpad de la session du 5/11 ; base : docs/DESIGN-ETUDES-ONYX.md, étude 4). Mode poche = un mode d'Elegant, pas un port de PocketUI ; 800x600 ; apps en plein écran sous la barre, fenêtres fixes en plein écran à contrôles adaptatifs, commutateur à vignettes, volet réglages rapides + notifications, densité tactile dans uk_mt_tab ; choisi dans Réglages > Écran, bureau par défaut.

L'utilisateur trouve ce mode « vraiment pas mal » et a choisi l'**accueil variante A** (tableau du jour : heure, recherche, cartes Aujourd'hui agenda/météo PocketQI/musique, Récentes, « Toutes les apps » -> lanceur à rail en second niveau, bande Ouvertes).

**Why:** décision explicite du 5/11, contre la recommandation B du designer.

Le 5/11 il a accepté les autres recommandations (barre 36, contrôles tactiles 36, boutons plutôt que gestes de bord, notifications dans le volet rapide, vignettes dans le commutateur, ‒ = accueil, relance de session au changement de mode, 800x600), SAUF les fenêtres de taille fixe : **plein écran, contrôles adaptatifs** (pas de carte centrée).

**État (6/11/2026)** : dans main (343cd6db), poussé et publié le 6/11 (paquets armpi 2026.11.21, f6b1d62a), contrat et détails dans docs/MODE-POCHE.md et docs/DESIGN-ELEGANT.md section 18 ; essai d'intégration QEMU 800x600 réussi (noyau 800x600 à part : build.ps1 fait 1024x768 pour QEMU). Restent : Terminal et Réglages ne suivent pas le redimensionnement, « +N » dans Ouvertes au-delà de 4, météo de PocketQI (weather.txt), Media Player sur korin.media.*, Piano/Cibles/Bienvenue/Mixeur adaptatifs, relance de session à chaud, Pi.

**How to apply:** Fenêtres fixes en plein écran : UIKit doit pouvoir replacer/étirer les contrôles (ancres ou grille), puis chaque app concernée (Calculatrice…) est revue. La maquette 5 de la page montre encore la carte centrée. Voir [[armpi-onyx-manques]], [[armpi-design-givre-calme]], [[armpi-panel]].
