---
name: armpi-claude-acp
description: "14/11 : claude-acp, agent de code Python (SDK anthropic) sur la clé d'API, en ACP ; façade agent --claude ; jiter en Python pur ; installé à la main sur D:"
metadata:
  type: project
---

Le 14/11/2026, l'utilisateur a choisi un **client maison sur la clé d'API** : il dit que son abonnement Max lui donne
100 $ de crédit d'API. Il veut s'en servir sur Korin pour créer, modifier et construire des apps, et aussi le noyau.
Il a choisi **Python + SDK anthropic**, avec une façade « façon Claude Code ».

Ce qui est fait (docs/CLAUDE-ACP.md, commité sur claude/x86-64-pc le 15/11, pas encore dans main) :

- `tools/claude/claude_acp` : un agent ACP sur stdio, fait pour que les façades libacp de Vibe servent telles quelles.
- Les façades : `agent --claude` (profil dans tools/agent/agent.c) et l'app Claude (src/apps/claude, le main.c de Vibe avec KORIN_CLAUDE ; dialogue de la clé ; icône claude). claude.app est posée sur D: le 15/11, construite pour le PC seulement. Pour le Pi, tools/office/build.sh échoue, pour vibe aussi : les en-têtes des kits de build/gcc sont périmés (TJ_SUCCES au lieu de TJ_SUCCESS), il faut les refaire.
- L'utilisateur a demandé si Claude comprend les compétences faites pour Vibe : oui, home.py copie les 6 compétences de Vibe et les 30 de .agents/skills.
- Opus 5.5 par défaut, fallbacks et compaction côté serveur, sessions dans /sd0/.claude-acp.
- `tools/claude/jiter` : jiter en Python pur. jiter ≥ 0.13 ne se construit pas pour Korin (codspeed veut libc ≥ 0.2.186), et le PC n'a pas de chaîne Rust dans ce WSL.
- Essai fakeapi.py + acptest.py : tout passe dans WSL.
- Paquet [claude] dans packages-pc.ini (needs vibe). Pi : packages.ini de cette branche n'a pas [vibe] (main l'a).

Installé sur D: le 14/11 vers 23 h. Copie à la main de build/claude-pc (DEST=… build.sh) et de bin/agent par robocopy, **pas par korin.ps1 install** : cette branche n'a pas build/pc/sysroot ni build/pc/python, car le Python du PC a été construit ailleurs. Rien n'a été essayé sous QEMU ni sur le ZBook.

**Why:** il a 100 $ de crédit d'API à utiliser ; l'abonnement reste pour le vrai Claude Code (couche Linux, [[armpi-linux-compat]]).

**How to apply:**

- Un prochain `korin.ps1 install` depuis un arbre sans claude remet l'ancien bin/agent (sans --claude). Il ne retire pas les fichiers de claude, qui ne sont pas dans installed.txt.
- Copier vers Windows par un chemin court : les noms d'anthropic dépassent 260 caractères sous le scratchpad.
- Voir [[armpi-agent-ia-korin]], [[armpi-vibe-facade]], [[armpi-installer-sans-qemu]].
