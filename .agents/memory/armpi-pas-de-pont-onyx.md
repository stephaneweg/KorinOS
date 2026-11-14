---
name: armpi-pas-de-pont-onyx
description: La règle « pas de pont » vise les portages d'applications Onyx, pas les ports tiers comme FreeBASIC
metadata:
  type: feedback
---

« Pas de pont » s'applique au portage des applications Onyx : elles appellent directement l'UIKit d'ARMPI, sans couche d'émulation.

**Why:** précisé par l'utilisateur le 31/10/2026 à propos de FreeBASIC, pour lequel une couche d'adaptation est acceptable.

**How to apply:** pour un port tiers, une couche d'adaptation vers une vue UIKit est permise, par exemple gfxlib2 de FreeBASIC via un pilote de type fbdev : un tampon graphique affiché dans une vue UIKit. Voir [[armpi-ports-theme]].
