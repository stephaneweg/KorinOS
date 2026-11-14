---
name: armpi-ports-theme
description: "Tout programme porté doit suivre le thème choisi dans ARMPI (Givre compris), jamais rester sur l'apparence d'origine"
metadata:
  node_type: memory
  type: feedback
  originSessionId: d17beb06-f22c-4b5e-b6fa-50637003b332
  modified: 2026-10-30T16:14:57.175Z
---

Tout portage (Onyx ou autre) doit s'adapter au thème du bureau d'ARMPI : couleurs et style, y compris les thèmes propres à ARMPI comme Givre, qui n'existe pas dans Onyx.

**Why:** Koton, compilé avec l'uikit C++ d'Onyx, s'affichait en style classique sous Givre : le nom est inconnu d'Onyx et `style = plat` retombait sur cde. L'utilisateur l'a repéré sur sa machine et a demandé la règle pour tous les ports à venir (30/10/2026).

**How to apply:** un port passe par l'interface de l'OS ([[armpi-ports-uikit-os]]), qui suit le thème d'elle-même. Tout dessin propre au programme prend ses couleurs dans UIKit, pas dans les mots de `SD0:/etc/theme.txt` : `ui_theme` (style, `UI_STYLE_PLAT` = 2) et `ui_jetons` (jetons TJ_*). Essayer au moins Givre, Onyx Noir et un thème cde ou milk, avec un `theme.txt` tel que l'écrit le panneau de configuration (`theme = <nom>` seul). Voir [[armpi-toolchain-onyx]] et [[armpi-webkit]].
