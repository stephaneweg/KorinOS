---
name: armpi-branche-future-main
description: 15/11 — la branche claude/x86-64-pc deviendra la future main ; les gros chantiers (renommage SD0: → SYS:, multi-utilisateur) n'ont pas à ménager les autres branches
metadata:
  type: project
---

Le 15/11/2026, l'utilisateur a dit à propos du renommage `SD0:` → `SYS:` (plus de 800 fichiers touchés) : « pas besoin de te soucier des autres branches, celle-ci deviendra la future main ». Celle-ci, c'est `claude/x86-64-pc`.

**Why:** tout le travail actif se fait sur cette branche. Les conflits avec les autres branches (main, sessions du cloud) ne sont pas un critère.

**How to apply:**
- Pas de fusion préalable ni de « moment calme » à attendre avant un changement massif sur `claude/x86-64-pc`.
- Ne pas proposer non plus de reporter un changement sur `main`, sauf si l'utilisateur le demande.

Voir [[armpi-x86-pc]], [[armpi-volumes-sys-home]], [[armpi-multi-utilisateur]].
