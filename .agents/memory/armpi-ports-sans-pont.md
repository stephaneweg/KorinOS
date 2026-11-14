---
name: armpi-ports-sans-pont
description: "Tout port d'app Onyx appelle directement les kits ARMPI : aucun pont, aucune couche onyxcompat, pour aucune application"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 52ea7130-db86-4847-a36b-ee89fc65ff48
  modified: 2026-10-30T18:24:41.495Z
---

Pour toute app Onyx portée, on ne crée aucun pont : on remplace les appels Onyx par des appels directs aux kits ARMPI (`ak_*`, `ui_*`...). Pas de couche `onyxcompat`, pour aucune application (dit par l'utilisateur le 30/10/2026). Koton est converti : modèle dans `src/extra/onyxports/koton` (`ui/view.h`).

**Why:** l'utilisateur ne veut pas de code intermédiaire entre les apps et ses kits, même pour garder les noms d'Onyx et réduire le travail d'un portage.

**How to apply:** en portant, réécrire les sites d'appel. Un contrôle est une poignée `ui_*`, sans classe qui l'enveloppe. Ne pas proposer d'en-tête de compatibilité, de wrapper ni de dossier partagé de noms Onyx ; ce qui manque s'ajoute au kit concerné. Seul reste permis le code propre à l'app (Koton garde son arbre de vues dessinées, que UIKit n'a pas). Complète [[armpi-ports-uikit-os]].
