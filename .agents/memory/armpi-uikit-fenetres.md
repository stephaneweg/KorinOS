---
name: armpi-uikit-fenetres
description: "UIKit multi-fenêtres (31/10/2026) — choix de conception et pièges d'essai QEMU ; prérequis du System.Windows.Forms maison"
metadata:
  node_type: memory
  type: project
  originSessionId: 4fdb9b70-0630-4240-84e1-02945b4a9889
  modified: 2026-10-31T11:08:52.864Z
---

Dans main depuis le 31/10/2026 (commit 2914de4, uikit.abi version 182) : un bloc d'état par fenêtre, échangé avec les variables courantes par `ui_window_use`.

Choix à garder : la poignée est l'id d'Elegant ; `ui_w_event` route puis **rend** la fenêtre d'avant ; un dialogue `WF_MODAL` bloque **tout le programme** (comme ShowDialog), pas seulement son parent ; `ui_dropdown`, `ui_combobox`, `ui_popup` restent des menus sans cadre.

**Why:** WinForms ouvre plusieurs Form ; viendront ensuite événements bruts par widget, dessin personnalisé, taille préférée ([[armpi-but-dotnet]]).

**How to apply:** essais QEMU : le démarrage varie de 12 à 17 s (attendre 24 s avant le premier clic). Koton charge sa SoundFont 26 s : un dialogue semble figé tant que `vfs` est occupé, ce n'est pas un blocage. Une image avec des apps récentes refuse un uikit.so plus ancien (table plus courte) : comparer à une image complète de référence, pas au seul kit.
