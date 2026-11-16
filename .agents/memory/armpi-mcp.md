---
name: armpi-mcp
description: "15/11 : étude d'un serveur MCP korin-mcp (voir l'écran, cliquer, lancer une app, état du système, API des kits) ; claude-acp n'a pas encore de client MCP"
metadata:
  type: project
---

Le 15/11/2026, l'utilisateur a demandé une étude d'un serveur MCP sur Korin, pour claude-acp et Vibe :
`docs/etude/MCP.md`. Rien n'est construit.

- Priorité : la boucle « écrire, lancer, regarder, corriger » : `screen_capture`, `window_list`, `input`,
  `app_run`/`app_status`/`app_stop`. Briques existantes : `WS_OP_SNAPSHOT`, `WS_OP_WIN_LIST`/`WIN_READ`,
  `WS_OP_MENU_GET`/`MENU_COMMAND`, l'anneau de HIDManager par `HID_OP_PILOTE` (comme vncd).
- Première étape : un client MCP dans claude-acp (lire `mcpServers` de `session/new`, outils `mcp__<serveur>__<outil>`,
  images dans `tool_result`). Vibe sait déjà parler MCP (`tools/vibe/mcp_test_server.py`).
- Serveur proposé en Python + petit module C `_korin` ; les opérations partagées (lire une fenêtre, injecter des
  entrées) vont dans un kit, pas dans le serveur.

- L'utilisateur veut aussi s'en servir pour les pilotes, corriger le système, examiner le matériel, auditer (§ 6) :
  un second serveur `korin-mcp-sys` dans /sys (droits DEV_MAP, IO_PORT, ACPI), lecture seule par défaut. Pour les
  pilotes, l'agent tourne **ailleurs** que la machine essayée (Claude Code sur le PC par le réseau, ou QEMU).

- Décisions du 15/11 : la capture marche avec les surfaces du GPU sur le vrai matériel ; l'agent reçoit **toutes les
  fautes** d'une app sans le journal (fiches de faute du noyau, `SYS_PROC_FAULTS`, `korin.proc.fault`) ; UIKit expose
  **tout l'arbre des widgets** (`WS_OP_UI_TREE`/`UI_ACT`) ; **toutes les fonctions des kits et tous les appels
  système** sont exposés (fichiers `.sig`, `kit_call`/`syscall`, outils `defer_loading` + recherche d'outils de l'API).

- Le client MCP est déjà écrit dans la session de la CAO (Forge piloté par MCP, branche claude/cao-occt-367ad4,
  pas encore commité le 15/11) : le reprendre, ne pas en faire un second. Voir [[armpi-cao]].

**Why:** l'agent ne voit pas l'écran, ne peut pas essayer les apps qu'il écrit, et lit mal le matériel.
**How to apply:** avant de coder, relire l'étude et ses questions ouvertes (§ 8). Voir [[armpi-claude-acp]],
[[armpi-agent-ia-korin]].
