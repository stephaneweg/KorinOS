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
  claude, mai).
- **Publié le 21/11** par pub.py (arbre réduit build/pub-skills, effacé ensuite ; Vibe depuis build/pc/sysroot avec
  --only, car son paquet découpé a besoin de tout site-packages pour vibe-data-1/2) : PC forge 1.3.7, terracube
  1.4.2, arabesque 1.3.7, koton 1.0.11, claude 1.0.8 (avec korin-game et korin-widget, jamais publiées avant),
  vibe 1.0.7 (Release) ; Pi arabesque 1.3.4. Les anciennes copies retirées des archives par `pub.py --drop` (ajouté).
  **Pas publiés** : mai (l'utilisateur : l'ignorer) ; koton du Pi (publié 1.0.2 du 4/11, a besoin de llm, absent du
  dépôt du Pi : à reconstruire et publier avec llm). Pas essayé dans l'OS.

**Why:** l'agent ne doit pas avoir à lire les sources d'une app pour s'en servir ; un paquet installé apporte sa
compétence sans reconstruire l'agent.
**How to apply:** une app dont un agent peut écrire les fichiers (Vector, fmtracker, Slides, Letters...) reçoit sa
compétence de la même façon ; vérifier le script d'exemple contre le vrai moteur de l'app. Voir [[armpi-agent-ia-korin]],
[[armpi-arabesque]], [[armpi-cao]], [[armpi-terracube]].
