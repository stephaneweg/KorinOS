---
name: armpi-vibe-competences
description: "Vibe connaît Korin (13/11/2026) — AGENTS.md + 6 compétences dans tools/vibe/home, paquets python/vibe publiés, ce que Vibe rate sur le Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: bceb4a7a-460a-472e-b8c1-788ccf40c9c1
  modified: 2026-11-13T00:07:45.143Z
---

`tools/vibe/home/` : `AGENTS.md` ($VIBE_HOME = /sd0/.vibe, lu à chaque séance) et les compétences korin, korin-app,
korin-csharp, korin-freebasic, korin-graphics (le « mentor » du dessin et des jeux, demandé par l'utilisateur),
korin-diag. `tools/vibe/home.py <racine>` les pose (build.ps1 l'appelle ; la carte de l'utilisateur : `E:\`).
Publiés le 13/11 dans main (59fb4863) : python 1.0.0, vibe 1.0.0 (+ vibe-data-1/-2), armpi 2026.11.30 (arbre POSIX),
gcc 1.0.10 (premiers relais lib<kit>.a et en-têtes des kits : la 1.0.9 n'en avait pas), bash 1.0.2, git 2.51.1.
Publication partielle depuis les archives publiées (docs/PAQUETS.md), jamais depuis une carte d'essai entière.

**Why:** sur le Pi, Vibe (modèle plus petit) ne charge pas toujours les compétences de lui-même et devine l'API
(jeu d'astéroïdes vide : `ui_vp_line` en pixels au lieu de 1/16) ; l'utilisateur a dû le guider.
**How to apply:** corriger d'abord les compétences et AGENTS.md (directifs, pièges concrets) quand Vibe rate ;
conseiller `/korin-app` en début de séance et une nouvelle séance après une mise à jour des compétences. Pas encore
essayé sur le Pi : fin d'install de vibe par pkg, redémarrage sur 2026.11.30, reprise de séance de l'app corrigée.
Voir [[armpi-python-vibe]], [[armpi-paquets]].
