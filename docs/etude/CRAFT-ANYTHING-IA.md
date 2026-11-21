# Craft Anything et l'IA sur Korin : Claude (claude-acp), Mistral Vibe

Étude du 21/11/2026, demandée par l'utilisateur : comment Craft Anything (storytold, révision `827be60` de
`tools/craft/apps.txt`) se sert de Gemini, de Kimi ou de Claude, et comment lui faire prendre **nos** agents : le
Python de Korin avec le SDK anthropic (**claude-acp**, docs/CLAUDE-ACP.md) et **Mistral Vibe** (`vibe-acp`,
docs/PYTHON.md). Rien n'est fait.

## 1. Comment Craft Anything parle à une IA

Tout est dans la caisse `crates/ai` (≈ 15 000 lignes). Deux familles de « backends » (`BackendKind`, `lib.rs`) :

| Famille | Ce que c'est | Sur Korin aujourd'hui |
|---|---|---|
| **API directes** (`Anthropic`, `Openai`, `Openrouter`, `Gemini`, `OpenaiCompatible`) | la boucle d'agent est en Rust, dans l'app (`agent.rs`) : HTTP par `ureq` 3 (`http.rs`), outils de l'app appelés directement | devrait marcher telle quelle avec une clé (réseau et TLS de rustls) ; **pas essayé** |
| **Agents en ligne de commande** (`AgentCli`) | un programme lancé en sous-processus, qui reçoit le **serveur MCP** de Craft Anything (`craft-anything-cli mcp --project … --inbox …`) ; soit un flux JSON propre à chaque CLI (`adapters/*.rs`), soit **ACP** (`cli_agents/acp.rs`) | aucun des agents connus n'existe sur Korin (Node, binaires Linux) |

- **Claude** : en API, `BackendKind::Anthropic` (`anthropic.rs`, `api.anthropic.com`, `claude-opus-5-5` par défaut, clé
  `ANTHROPIC_API_KEY` ou trousseau) ; en agent, Claude Code (`claude`, flux `stream-json`, adaptateur dans
  `adapters.rs`).
- **Gemini** : en API, `BackendKind::Gemini` (le point d'entrée compatible OpenAI de Google AI Studio) ; en agent,
  Gemini CLI en ACP (`gemini --experimental-acp`).
- **Kimi** : seulement en API, par le fournisseur « Moonshot » de `providers.rs` (compatible OpenAI,
  `api.moonshot.ai/v1`, `kimi-k3.1-pro`, `MOONSHOT_API_KEY`). « Mistral » y est aussi (`api.mistral.ai`), mais c'est
  l'API brute, pas Vibe.
- La détection (`detect.rs`) : clés dans l'environnement, serveurs locaux, agents trouvés dans le `PATH`
  (`cli_agents::KNOWN`), hôtes ssh ; un balayage d'une seconde et demie.

**Le client ACP est déjà là** (`acp.rs`, 1 700 lignes) : `initialize`, `session/new` (avec le serveur MCP de l'app dans
`mcpServers`) ou `session/load`, un `session/prompt` par message, `session/update` en événements,
`session/request_permission` en demande d'accord dans l'interface, `session/set_config_option`, `authenticate` pour
les agents qui le demandent. Un agent ACP se déclare par trois tables : `KNOWN` (id, programme, nom affiché),
`ACP_AGENTS` (les arguments du mode ACP), `ACP_MODEL_FLAGS` (le drapeau du modèle), et au besoin un `AgentAdapter`.

## 2. Ce que nos agents savent déjà faire

| | claude-acp (`/bin/claude-acp`, paquet `claude`) | Vibe (`/bin/vibe-acp`, paquet `vibe`, Vibe 3.1.2) |
|---|---|---|
| Protocole | ACP sur stdio, toujours (pas d'argument) | ACP sur stdio, toujours |
| Méthodes | `initialize`, `authenticate`, `session/new`, `list`, `load`, `set_mode`, `set_config_option`, `prompt`, `cancel` ; `session/request_permission` vers le client | les mêmes (SDK ACP de Mistral) |
| Serveurs MCP de la session | oui : `acp_mcp` de `session/new` (`mcp.py`, client stdio ; Forge s'en sert) | oui : `mcp_servers` de `session/new` (`vibe/acp/agent.py` ; à revérifier dans 3.1.2, vu dans 3.1.4) |
| Clé, compte | clé d'API dans `~/.local/claude-acp` (dialogue de l'app Claude, `/account` de `agent --claude`) | compte Mistral (navigateur) ou clé dans `$VIBE_HOME/.env` |
| Modèle | Opus 5.5 par défaut, `session/set_config_option` | `session/set_config_option` |

Les deux suivent donc exactement le chemin ACP de Craft Anything ; il n'y a **rien à écrire côté agents**, et la clé
reste là où l'utilisateur l'a déjà mise (pas de second endroit dans Craft Anything).

## 3. Ce qu'il faut faire

Par `tools/craft/korin_app.py` (la règle des Craft : on ne touche pas au dépôt de storytold), sous
`#[cfg(target_os = "korin")]` pour ne rien changer ailleurs :

1. **Déclarer les deux agents** dans `cli_agents/mod.rs` (`KNOWN`) et `cli_agents/acp.rs` (`ACP_AGENTS`, arguments
   vides) :
   `("claude-acp", "claude-acp", "Claude (Korin)")`, `("vibe", "vibe-acp", "Mistral Vibe")`. Un petit
   `AgentAdapter` pour chacun (`adapters/korin.rs`, sur le modèle de `hermes.rs`) : `requires_acp() -> true` (pas de
   mode « un message par lancement »), pas de drapeau de modèle (il passe par `set_config_option`), pas de contrôle de
   `--help` (nos commandes n'en ont pas de pareil).
2. **La détection** : `detect.rs` les propose quand `/bin/claude-acp` ou `/bin/vibe-acp` existent ; sur Korin, ne pas
   proposer les agents qui ne peuvent pas y tourner (Claude Code, Codex, Gemini CLI… : Node ou binaires Linux), pour
   ne pas offrir ce qui échouera.
3. **Le serveur MCP** : `craft-anything-cli` doit être livré **à côté** de `main` (`which::self_cli_path` le cherche dans
   le dossier de l'exécutable) : le construire aussi (`build.sh craft-anything craft-anything-cli`) et le mettre dans
   `craft-anything.app/` ; le paquet `craft-anything` en a besoin (`needs = craft, claude` ou `vibe` : plutôt
   aucun des deux, l'app les propose s'ils sont là).
4. **Le chemin des programmes** : `program_of` cherche dans le `PATH` ; `/bin` y est. Rien à faire, sauf si une app
   lancée du bureau n'a pas de `PATH` (à vérifier sous QEMU : sinon `/bin/<nom>` en dur sur Korin).
5. **Les accords** : compatibles. `acp.rs` choisit l'option par son genre (`allow_once`, `reject_once`), pas par son
   identifiant, et claude-acp envoie `allow` / `always` / `reject` avec ces genres.
6. **Essai** : `tools/claude/fakeapi.py` (une fausse API de Claude) et `tools/vibe/fakellm.py` existent : Craft
   Anything sous QEMU, une construction de bout en bout sans dépenser de crédit, puis une vraie avec la clé.

À part, sans rien changer : les **API directes** de Craft Anything (Claude, Kimi par Moonshot, Mistral) sont à essayer
telles quelles ; si `ureq` ou rustls bloque sur Korin, c'est un patch de plus dans `tools/craft/patches`.

## 4. Ce qui limitera l'usage

Craft Anything « étudie l'original avec de vrais outils d'inspection » (Chromium sans tête, rizin, captures) avant de
faire construire un projet par l'agent. Sur Korin, une bonne part manque : pas de Chromium, pas de rizin. Ce qui reste :
décrire ce qu'on veut, donner des captures ou des fichiers, laisser l'agent construire. Et le projet construit doit
pouvoir se compiler **sur Korin** : Rust (la chaîne `x86_64-unknown-korin` n'est pas encore dans l'OS, elle est
construite dans WSL), ou bien orienter l'agent vers ce que Korin construit chez lui (C par gcc, C#, Python, BASIC) par
les compétences (`.agents/skills`, déjà lues par claude-acp et Vibe).

## 5. Estimation

Les retouches 1 à 4 : une demi-journée (tables, un adaptateur, la détection, le CLI dans l'app). L'essai sous QEMU avec
les fausses API : une demi-journée. Le plus incertain : ce que les outils d'inspection de Craft Anything exigent avant
de laisser l'agent travailler (le parcours « reconstruire » peut refuser de partir sans eux).
