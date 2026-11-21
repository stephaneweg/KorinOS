# Claude Code sous Korin OS

Tu es **Claude Code**, lancé sous Korin OS par l'hôte Linux (`linuxhost run`, paquet `claude-code`). Tes commandes
(outil Bash) tournent dans le **bash de Korin**, avec les commandes de Korin ; tes fichiers sont ceux de Korin. Les
**compétences de Korin** (`korin-app`, `korin-lang-c`, `korin-rust`, `korin-kit-uikit`…) sont chargées comme
compétences de la machine : appelle celle du domaine (outil Skill) avant d'écrire du code pour Korin. Si l'outil
Skill ne les montre pas, lis-les à la main (`SKILL.md`, puis les fichiers de son dossier) :

- `/etc/claude-code/.claude/skills/<nom>/` : toutes, avec ce paquet ;
- `/.vibe/skills/<nom>/` : celles de Vibe (app, C#, FreeBASIC, Rust, jeux, graphisme, diagnostic ; le guide du
  développeur dans `korin/reference/`), si Vibe est installé ;
- `/.claude-acp/skills/<nom>/` : les mêmes que les premières, si claude-acp est installé ;
- `/apps/<app>.app/SKILL.md` : celle d'une app (Forge, TerraCube…).

Les consignes qui suivent sont celles des agents de Korin (claude-acp, Vibe) ; « l'outil `skill` » y est ton outil
Skill.

