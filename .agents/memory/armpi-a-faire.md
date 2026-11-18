---
name: armpi-a-faire
description: "Notes de l'utilisateur à reprendre à une date donnée (tâches courtes signalées en passant)"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-18
---

**FAIT le 18/11 — Jet, loupe de la barre d'adresse** (signalé le 14/11, oubliée le 17/11) : la cause était dans UIKit,
pas dans Jet : `ui_w_move` d'une barre d'outils laissait ses boutons (`ui_toolbar_add`, UT_TOOL) à leur place.
Désormais ils bougent du même décalage (`uk_tbar_shift`, uk_tabs.c) et `uk_adapt` ne les place plus lui-même. Les
apps qui placent leurs boutons (mail, irc, letters) le font après la barre : inchangées. Pas encore vérifié à
l'écran (Jet sur le ZBook, cardfile, help).

**PRIORITÉ, signalé le 18/11 — boutons du touchpad du ZBook** : le touchpad marche, pas les boutons autour (3 sous
le touchpad : COL03 ; 3 au-dessus : COL05, PointStick ELAN). À faire dans i2chid, détail : docs/etude/PC-PILOTES.md
§5 point 0.
