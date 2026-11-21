---
name: armpi-competences-apps
description: "21/11 : une app peut apporter sa compétence pour les agents (clé skill = SKILL.md de app.txt) ; Forge, TerraCube, Arabesque, Koton"
metadata:
  type: project
---

Idée de l'utilisateur (21/11/2026), après que Claude sur Korin a dû lire les sources d'Arabesque pour écrire un `.arb` :
les apps de création et multimédia sans MCP apportent **leur compétence dans leur dossier**, et les consignes
générales disent d'aller la chercher.

- Format : `skill = SKILL.md` dans `app.txt`, le fichier à côté (`src/apps/<app>/SKILL.md`, copié par build.ps1,
  tools/office/build.sh, tools/pc/apps.sh, build-koton.sh). Nom `korin-<app>`. Doc : docs/ABI.md, docs/CLAUDE-ACP.md.
- claude-acp et mai-acp : `app_skills` (claude_acp/agent.py) lit les manifestes de `/apps` à chaque séance ;
  celle d'une app l'emporte sur une copie du même nom dans skills/ (les comptes gardent les anciennes copies).
- Vibe : `VIBE_SKILL_PATHS=["/apps"]` posé par libacp (`acp_home`, profil Vibe) ; vérifié contre Vibe 3.1.2.
- Fait : korin-forge et korin-terracube déplacées de tools/vibe/home/skills dans leurs apps ; korin-arabesque
  (.arb, script vérifié par le moteur recipe.h/replay.h) et korin-koton (.kson, script vérifié par model.cpp et
  compile.cpp) écrites. Versions montées (Pi : arabesque, koton ; PC : forge, terracube, arabesque, koton, vibe,
  claude, mai) ; **pas encore publié ni essayé dans l'OS**.

**Why:** l'agent ne doit pas avoir à lire les sources d'une app pour s'en servir ; un paquet installé apporte sa
compétence sans reconstruire l'agent.
**How to apply:** une app dont un agent peut écrire les fichiers (Vector, fmtracker, Slides, Letters...) reçoit sa
compétence de la même façon ; vérifier le script d'exemple contre le vrai moteur de l'app. Voir [[armpi-agent-ia-korin]],
[[armpi-arabesque]], [[armpi-cao]], [[armpi-terracube]].
