---
name: armpi-relics
description: "Relics, jeu de plates-formes et d'exploration à la Rick Dangerous (19/11/2026) : C sur gamefb, tuiles dessinées, 12 niveaux générés, banc d'essai sur l'hôte"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-19T16:45:00.000Z
---

Demandé le 19/11 : « un superbe jeu en side scrolling, comme Rick Dangerous, une dizaine de niveaux, pièges, ennemis ».
`src/apps/relics` (C, comme Arkanoid/Bubbles : gamefb.h, gamedraw.h, gamepause.h, gamesound.h ; 640 x 384 mis à
l'échelle). Nom original (pas de reprise de Rick Dangerous). Sur la branche `claude/gifted-maxwell-bhyryb`.

**Why:** l'utilisateur a demandé en cours de route (avec des captures de Rick Dangerous 1 et 2 et un tileset « forest
platformer » comme inspiration, non repris) : des **tuiles** dessinées au trait plutôt qu'un rendu calculé, et un
**personnage plus grand** par rapport au décor (environ une tuile). D'où : cases de 20 unités de jeu grossies de 1,6
(tuiles de 32 points, 20 x 12 visibles), personnages dessinés à 1,8, trait de 2 points.

**How to apply:**
- Niveaux : `tools/relics/mklevels.py` génère `levels.h` (ne pas éditer levels.h) ; légende en tête de levels.h.
- Vérifier (le texte des images : FreeType et les polices Inter de sdcard/fonts, pas des pavés) : `sh tools/relics/run.sh check` (parcours en largeur par la vraie physique : sortie et objets atteignables),
  `shot n out.png "R30 J40"` (image ; `RELICS_AT=tx,ty`), `fuzz` (12 niveaux au hasard). Contraintes : marche de
  2 cases, trou de 3 cases, 4 cases au-dessus du sol pour un saut entier.
- Pas essayé dans Korin : sous QEMU raspi4b dans le cloud (4 cœurs), même la carte d'origine de `carte_linux.py`
  s'arrête à « / non servi » (fatfs ne répond pas) ; à lancer sous QEMU ou sur le Pi chez l'utilisateur.
- Intégré : listes de `tools/office/build.sh` et `tools/pc/apps.sh`, `[app.relics]` des deux packages*.ini, icône dans
  `tools/icons/art.py`. Pas publié (versions.ini : 1.0.0 par défaut).
