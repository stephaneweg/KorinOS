---
name: armpi-skel-sdroot
description: "Plus de sdroot ni sdroot2 a la racine (3/11/2026) : modele suivi dans skel/sd0 et skel/sd1, sortie dans build/sdroot"
metadata:
  node_type: memory
  type: project
  originSessionId: 6cbe2b78-9032-4b62-950f-92e82490eae8
  modified: 2026-11-03T09:01:46.516Z
---

Le 3/11/2026, l'utilisateur ne veut plus de `sdroot` ni `sdroot2` à la racine du dépôt (« je n'en veux plus »). Le modèle suivi est dans `skel/sd0` et `skel/sd1`, et `build.ps1` refait `build/sdroot` à neuf.

**Why:** il veut une racine propre, et a supprimé ces dossiers lui-même.
**How to apply:** ne jamais recréer `sdroot` à la racine. `C:\Projects\ARMPI\sdcard` est la carte **minimale** : ne pas y mettre une carte complète. Exception à « ne pas pousser sur GitHub » : le dépôt `stephaneweg/KorinOS` (nom et passage en public choisis par l'utilisateur le 3/11, selon la session GitHub ; d'abord prévu privé sous le nom ArmPi), historique neuf, MIT sauf licences imposées. Voir [[armpi-paquets]].
