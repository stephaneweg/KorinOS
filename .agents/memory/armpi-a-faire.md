---
name: armpi-a-faire
description: "Notes de l'utilisateur à reprendre à une date donnée (tâches courtes signalées en passant)"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-14
---

**Mardi 17/11/2026 — Jet, barre d'adresse** (signalé le 14/11) : le bouton « loupe » de la barre d'adresse ne suit pas
le redimensionnement de la fenêtre. En agrandissant, la barre d'adresse s'élargit bien, mais la loupe reste à sa
position d'avant et se retrouve par-dessus la barre. Sa position doit être recalculée au redimensionnement, depuis le
bord droit de la barre. Le source de Jet est dans `src/extra` (hors git, `tools/extra/fetch.sh`), pas dans `src/apps`.
