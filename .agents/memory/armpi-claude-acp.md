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

**15/11, essai réel sur le ZBook** : l'utilisateur dit que ça marche très bien, et que Claude lui a fait une app bien meilleure que Mistral. Au début, Claude ne trouvait pas comment faire malgré les compétences : le PC n'a pas /sd0/src. L'utilisateur a dû l'orienter vers les sources GitHub des apps (il a lu les jeux).

Corrigé ensuite, mais **pas encore reporté sur D:** : home.py pose 6 apps dans korin-app/examples, et AGENTS.md envoie au dépôt GitHub (web_fetch, ou python3 + urllib par bash). D: n'était pas joignable : c'est maintenant le partage SMB de Korin `\\192.168.0.9\korin`, et il était déconnecté.

**Paquet PC claude 1.0.0 publié le 15/11** (main 422b514, packages/pc/pkgs/claude-1.0.0.opk, 7,9 Mo, needs vibe), publication partielle : arbre build/pc/claude-pkgroot (DEST=… ARCH=x86_64 build.sh + apps/claude.app), lanceur scratchpad pub_claude.py (mkrepo avec ini réduit à [claude], needs remis après plan, --keep-unbuilt, abi publiée gardée, versions.ini en copie). Pièges : la clé est **C:\Projects\onyx_pkg-key.pem.txt** (l'utilisateur a dit .pem, absent) ; un lancement qui échoue à la signature laisse index.txt écrit, et le suivant garde alors l'ancienne index.sig (« index gardé ») : retirer index.sig avant de relancer. Le vibe publié n'a pas encore l'agent avec --claude : seule l'app Claude marche par le paquet.

Installé sur D: le 14/11 vers 23 h. Copie à la main de build/claude-pc (DEST=… build.sh) et de bin/agent par robocopy, **pas par korin.ps1 install** : cette branche n'a pas build/pc/sysroot ni build/pc/python, car le Python du PC a été construit ailleurs. Rien n'a été essayé sous QEMU ni sur le ZBook.

**Why:** il a 100 $ de crédit d'API à utiliser ; l'abonnement reste pour le vrai Claude Code (couche Linux, [[armpi-linux-compat]]).

**How to apply:**

- Un prochain `korin.ps1 install` depuis un arbre sans claude remet l'ancien bin/agent (sans --claude). Il ne retire pas les fichiers de claude, qui ne sont pas dans installed.txt.
- Copier vers Windows par un chemin court : les noms d'anthropic dépassent 260 caractères sous le scratchpad.
- Voir [[armpi-agent-ia-korin]], [[armpi-vibe-facade]], [[armpi-installer-sans-qemu]].
