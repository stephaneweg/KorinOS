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

- Idée du 19/11 (étude § 6.6) : le MCP parle à **tous les kits, services et Elegant**, pour que l'agent **montre**
  lui-même (« affiche-moi ceci ») : fenêtres à la volée décrites par l'agent (contrôles UIKit, événements renvoyés à
  l'agent), vues prêtes (tableau, image, graphique, formulaire), sans lancer ni créer d'app ; « garde-la » en fait
  une vraie app (korin-app).

**Why:** l'agent ne voit pas l'écran, ne peut pas essayer les apps qu'il écrit, et lit mal le matériel.
**How to apply:** avant de coder, relire l'étude et ses questions ouvertes (§ 8). Voir [[armpi-claude-acp]],
[[armpi-agent-ia-korin]].

**22/11 : korin-mcp v1 fait** (demande de l'utilisateur : instrumenter l'OS pour Claude Code, claude-acp, Vibe par des
outils structurés ; docs/MCP-KORIN.md). **C sur libposix** (pas Python : appels système directs, aucune dépendance,
binaire de 350 Ko), `src/sys/mcp` → `/bin/korin-mcp`, `sh tools/mcp/build.sh` (constantes et rangs de NetKit tirés
des .inc/.abi ; `--host` : essais JSON et PNG). 17 outils : processes, process, cpu, kernel_log, list_dir, read_file,
find_files, volumes, packages, services, gpu, network, time, windows, screenshot (PNG maison, png.c), send_signal,
launch. Garde-fous : read_file refuse les secrets ; launch refuse /sys, /sbin, rm/mkfs/gpt/sudo…, /media/sd1 ;
send_signal refuse /sys sans allow_system.
- **Enregistrement** : `/bin/claude` ajoute `--mcp-config=/etc/claude-code/mcp.json` (forme avec « = » : la forme à
  espace avale l'invite). **Pas `managed-mcp.json`** : vérifié avec Claude Code 2.1.330 (unshare -m, faux
  /etc/claude-code), il prend le contrôle EXCLUSIF des serveurs MCP (ceux de claude mcp add, KorinStudio, Forge,
  TerraCube disparaîtraient) ; `managedMcpServers` de managed-settings.json n'est pas lu du fichier. Deux
  --mcp-config se cumulent (vérifié). Vibe : libacp passe korin-mcp dans mcpServers s'il est installé (KORIN_MCP=0 :
  sans) ; pas essayé (vibe pas construit dans le cloud).
- Claude Code 2.1.330 sonde `server/discover` (protocole 2026-07-28) avant `initialize` 2025-11-25 : répondre -32601.
- Piège du cloud : la CLI `claude` du conteneur répond vraiment (le proxy authentifie) même avec `env -i` et un HOME
  vide : n'y lancer que `--version` et `mcp …` ; `claude config list` (pas une sous-commande) devient une invite.
- Paquet : bin/korin-mcp dans **claude-code** (2.1.330.9) ; vibe 1.0.10 (libacp) ; rien de publié.
- **Essayé le 22/11 sous QEMU PC** (`python3 -I tools/mcp/pctest.py`, bureau Elegant) : RÉUSSI, 39 vérifications ;
  Claude Code 2.1.330 sous linuxhost : `claude mcp list` → « korin-direct: /bin/korin-mcp - ✔ Connected ». Pièges du
  banc : pas de « | » dans les commandes (séparateur de ONLY) ; Claude Code avec `< /dev/null` (sinon il mange ce que
  le banc tape). Pas essayé : une vraie séance qui appelle les outils (compte : sur le ZBook), Vibe, le Pi.
