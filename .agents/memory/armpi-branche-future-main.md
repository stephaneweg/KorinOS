---
name: armpi-branche-future-main
description: 18/11 — claude/x86-64-pc a remplacé main (sans fusion) ; on travaille désormais sur main, qui porte aussi packages/ (GitHub Pages)
metadata:
  type: project
---

Le 15/11/2026, l'utilisateur avait annoncé que `claude/x86-64-pc` deviendrait la future `main`. **C'est fait le 18/11** :
`main` = l'ancienne `claude/x86-64-pc` (21ba38a4), poussée en force, **sans fusion** (« pas de merge, mais un
remplacement complet »). Juste avant, le dossier `packages/` de l'ancienne `main` (le dépôt de paquets servi par GitHub
Pages : https://stephaneweg.github.io/KorinOS/packages/pc) a été repris tel quel dans la branche. L'ancienne `main`
(755c04d2) n'a plus de branche. Le worktree KorinOS-pub est supprimé : on publie depuis le checkout principal
(`tools/pkg/publish.sh`, `ARCH=x86_64`), sur `main`.

**How to apply:** commiter et pousser sur `main` ; ne plus recréer `claude/x86-64-pc`. Les paquets publiés (packages/)
vivent dans la même branche que le code. Voir [[armpi-x86-pc]], [[armpi-build-world-pc]].
