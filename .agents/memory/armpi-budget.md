---
name: armpi-budget
description: Budget mensuel : port C++ UIKit de l'app Windows (gestion/BudgetApp), dans main, paquet budget 1.0.1 publié le 8/11 ; les évolutions se font dans les deux apps
metadata:
  node_type: memory
  type: project
  originSessionId: e3bce7f4-2f04-4b2d-803c-3b3abec5bb92
  modified: 2026-11-07T20:59:49.761Z
---

Port en C++ sur UIKit de l'app WinForms de budget de l'utilisateur (`C:\Projects\gestion\BudgetApp`, à ne jamais modifier). `src/apps/budget` (model.h, grid.h, main.cpp), doc `docs/BUDGET.md`. Même `budget.json` que Windows (SD0:/home/budget). Dans main (c7e7ecb4) et poussé ; paquet `budget` 1.0.0 publié le 7/11 (a8d584b3), ajouté seul à l'index par build/pkg-budget/addpkg.py, sans refaire les autres paquets. Le 8/11, l'utilisateur l'a essayé sur Korin : « marche bien » (version 1.0.1). Le 8/11 aussi, sur sa demande, troisième onglet « Soldes et résumé » (Ctrl+3) pour donner la hauteur aux grilles, Korin seulement ; paquet 1.0.2 publié (306c4d45). Puis ligne des sommes sous les deux grilles, barre horizontale, Total général et Moyenne / jour, colonne Solde retirée, dans les deux apps (Windows : GridFooter.cs) ; Korin sur claude/budget-sommes (5f474116), exe Windows copié le 8/11 vers 11 h.

Le 7/11 au soir, l'utilisateur a demandé une case « Utiliser » par ligne du modèle dans **les deux apps** (il permet donc de modifier gestion/BudgetApp quand il le demande lui-même ; sauvegarde avant dans le scratchpad, pas de git là-bas) : `Skip` dans le JSON, totaux du dialogue en direct. Puis ligne automatique « Chèques repas utilisés » (MealUsed) en tête des dépenses courantes, dans les deux apps. Puis dépenses exceptionnelles (Exceptional), écart sans elles dans la tuile, bouton Catégories du mois. Dans main et poussé ; paquet budget 1.0.1 publié le 8/11 (9d0a25bc) par build/pkg-budget/updpkg.py (remplace la seule section [budget], retire l'ancienne archive, signe). L'exe Windows se met à jour en copiant bin/Release/net10.0-windows dans `gestion\Application` (app fermée) ; dernière copie le 8/11 vers minuit.

Demandes de l'utilisateur : 800x600 → deux onglets en capsule (Charges et suivi / Dépenses courantes) ; grilles comme le tableur (ajout/suppression de lignes faciles, colonnes redimensionnables) ; pilotage complet au clavier.

**Why:** il gérait son budget dans Excel puis dans l'app Windows ; il veut la même app sur Korin.

**How to apply:** pièges : `ui_menu_key` ne lit que lettres/chiffres/F-keys (« Ctrl+PgUp » = Ctrl+P) ; `ui_label` avec "" puis `ui_w_show` peut fauter (texte nul) ; essais QEMU : vider etc/preload.txt (Jet bloque vfs), `--kernel build/kernel8_800.img --ecran 800x600`, chiffres en AZERTY par `key shift-N`, pavé numérique muet. Voir [[armpi-mode-poche]], [[armpi-paquets]].

23/11 (branche claude/linux-budget, pas poussée, pas de paquet Korin publié ; versions.ini : budget 1.0.5, [pc] 1.0.8) :
l'utilisateur ne voyait « pas de drop down » pour classer les opérations bancaires. La cellule « Classer dans »
(et « … classer dans » des règles) est devenue une liste déroulante dessinée par grid.h (GridModel::has_choices /
choices / choose, GChoice : groupes Postes fixes, Dépenses courantes, Autres choix) : clic, Alt+Bas, F4, Menu, clic
droit ; la saisie avec complétion reste. ui_popup_local n'avait pas de bug d'affichage visible dans le code (couche 4
d'Elegant, WF_NOFOCUS : pas bloqué par le dialogue modal) : la liste n'était qu'au clic droit / touche Menu, rien
au doigt. Corrigé aussi : complétion d'un libellé accentué (« meddical »). Essayé sous Linux seulement (port
linux-ports/, voir [[armpi-linux-ports]]), pas sous Korin ni QEMU.
