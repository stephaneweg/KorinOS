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
faire construire un projet par l'agent. Sur Korin, une bonne part manque : pas de Chromium (Jet peut le remplacer, § 4 bis), pas de rizin. Ce qui reste :
décrire ce qu'on veut, donner des captures ou des fichiers, laisser l'agent construire. Et le projet construit doit
pouvoir se compiler **sur Korin** : Rust (la chaîne `x86_64-unknown-korin` n'est pas encore dans l'OS, elle est
construite dans WSL), ou bien orienter l'agent vers ce que Korin construit chez lui (C par gcc, C#, Python, BASIC) par
les compétences (`.agents/skills`, déjà lues par claude-acp et Vibe).

## 4 bis. Remplacer Chromium par notre WebKit (Jet)

Question de l'utilisateur (21/11). Oui, c'est possible : Craft Anything ne charge pas Chromium, il **lui parle par le
protocole DevTools (CDP)** sur `127.0.0.1` (`crates/engines/src/cdp.rs`, client WebSocket fait main). Il suffit donc
que Jet parle CDP.

**Ce que Craft Anything attend** (`crates/engine/src/tools/web_common.rs`, `Browser::resolve`) :

- un programme lancé avec `--user-data-dir=<dossier> --remote-debugging-port=0 --headless=new …` puis l'adresse de
  départ ; il attend le fichier `<dossier>/DevToolsActivePort` (le port sur la première ligne), puis lit
  `/json/version` et `/json/list` en HTTP, et ouvre le WebSocket d'une page. Les autres drapeaux de Chromium
  (`--no-first-run`, `--disable-sync`…) peuvent être ignorés ;
- ou un navigateur déjà lancé par l'utilisateur, dont on donne l'adresse (`--remote-debugging-port=9222`) ;
- le chemin du programme se règle dans Réglages → Moteurs (`engines.tools.chromium`) : `/apps/jet.app/main` sur Korin,
  **sans toucher au code de Craft Anything** (au plus, ce chemin par défaut sur Korin dans `korin_app.py`).

**Les 70 commandes CDP employées** : `Page` (navigate, reload, captureScreenshot, getFrameTree, getResourceTree,
dialogues, téléchargements, événements de chargement), `Runtime` (evaluate, callFunctionOn, console, exceptions),
`Network` (requêtes, réponses, getResponseBody, cookies, WebSockets), `DOM` (getDocument, querySelector,
getOuterHTML, getBoxModel, focus), `Debugger` (scriptParsed, getScriptSource), `Input` (souris, clavier, insertText),
`Emulation` (taille de l'écran, média, langue, fuseau), `Accessibility` (l'arbre), `Profiler` (couverture du code),
`Target`, `Log`, `Browser.close`.

**Ce que WebKit a déjà** : CDP est né d'une copie du protocole de l'inspecteur de WebKit (2013), et les deux ont gardé
les mêmes domaines pour l'essentiel : `Page`, `Runtime`, `Network` (avec `getResponseBody`), `DOM`, `Debugger`,
`Console` (le `Log` de CDP) ont presque les mêmes messages JSON. Il manque côté WebKit : `Input`, une bonne part
d'`Emulation`, `Accessibility`, la couverture de `Profiler` (WebKit a `ScriptProfiler`), et `Target` à la façon de
Chromium. Playwright a résolu le même problème par des patchs de WebKit (`browser_patches/webkit` : `Input`,
captures, émulation, mode sans écran) : une source d'idées, et de code si la licence va (à vérifier).

**Le plan** :

1. Dans Jet, un **mode sans fenêtre** (`--headless=new`) : WebKit2 peint déjà dans une mémoire partagée, Jet ne la
   présente simplement pas ; et `--user-data-dir` (profil, cookies) à part.
2. Un **serveur CDP dans Jet** (C++, sur le bouclage seulement) : HTTP pour `/json/version` et `/json/list`,
   WebSocket par page, écriture de `DevToolsActivePort`. Chaque message passe à **l'inspecteur de WebKit** de la
   page (sa connexion locale, `InspectorController` / `WebPageInspectorController`) après une traduction légère des
   noms et des champs qui diffèrent ; ce que WebKit n'a pas est fait à côté dans Jet : `Input` (les événements
   envoyés à la page, comme le fait l'UI de Jet), `Page.captureScreenshot` (l'image de la page en PNG),
   `Emulation.setDeviceMetricsOverride` (la taille de la vue), `Target` (les onglets de Jet).
3. Ce qui peut attendre : `Accessibility`, `Profiler` (couverture), `Emulation` du fuseau et de la langue ; Craft
   Anything signale un outil qui manque sans tout arrêter (à vérifier outil par outil).

**À vérifier d'abord** (les sources de WebKit sont hors de git, `src/extra/webkit`, pas dans le cloud) : que
l'inspecteur est construit dans le WebKit de Jet (`ENABLE_INSPECTOR` ; le transport `REMOTE_INSPECTOR` est éteint
pour `jsc` dans `build-jsc.sh`, et n'est pas nécessaire : le serveur de Jet parlerait à l'inspecteur en local).

**Taille** : une à deux semaines (le serveur et la traduction de `Page`, `Runtime`, `Network`, `DOM` d'abord, puis
`Input`, captures, émulation). Le gain dépasse Craft Anything : Jet deviendrait pilotable par tout outil CDP
(Puppeteer, les essais de pages de Korin, un serveur MCP de navigation pour claude-acp et Vibe).

## 4 ter. Un serveur MCP dans Jet, en plus de CDP

Question de l'utilisateur (21/11) : oui, et les deux partagent presque tout. CDP est le protocole **bas** (pour les
outils qui le parlent : Craft Anything, Puppeteer) ; MCP est le protocole des **agents** (claude-acp, Vibe, Claude
Code depuis le PC) : quelques outils de haut niveau, faits sur la même couche de Jet (la page, son inspecteur, les
événements d'entrée, les captures).

**Comme Forge (7311) et TerraCube (7312)** : transport HTTP « Streamable » sur le bouclage, jeton `Authorization:
Bearer` gardé dans le coffre à secrets (sinon `HOME:/jet/mcp.txt`), **port 7313**, `jet --mcp 7313` ou le menu
*Fichier > Piloter par un agent (MCP)…*. Le transport (`mcp.cpp` de Forge, déjà copié dans TerraCube) passe enfin dans
une bibliothèque commune (`src/lib/mcp`), comme la mémoire de TerraCube le demandait. claude-acp et Vibe le prennent
par leur `config.toml` (`transport = "streamable-http"`, `url = "http://127.0.0.1:7313/mcp"`), et la compétence de
Jet (clé `skill` de son `app.txt`, comme les autres apps) leur dit quand s'en servir.

**Les outils** (sur le modèle de Playwright MCP, qui a fait ses preuves avec les agents) :

| Outil | Ce qu'il fait |
|---|---|
| `tabs`, `tab_open`, `tab_select`, `tab_close` | les onglets de Jet |
| `navigate`, `back`, `forward`, `reload`, `wait_for` (texte, sélecteur, chargement) | se déplacer |
| `snapshot` | la page en texte structuré (arbre des éléments visibles, rôle, nom, valeur), chaque élément avec une **référence** ; c'est ce que l'agent lit, bien moins cher qu'une image |
| `click`, `hover`, `type`, `fill_form`, `select_option`, `press_key`, `drag`, `upload_file` | agir sur un élément par sa référence |
| `screenshot` (page, élément) | une image quand le texte ne suffit pas |
| `console`, `network` (requêtes, statut, en-têtes sans secrets) | ce que la page a dit et chargé |
| `evaluate` | du JavaScript dans la page (**avec accord**, comme les commandes de claude-acp) |
| `pdf`, `save_page`, `downloads` | garder une page, récupérer un fichier |

`snapshot` demande l'arbre d'accessibilité, ou à défaut un parcours du DOM (rôles ARIA, textes, champs) ; c'est
aussi ce qui manquait au domaine `Accessibility` de CDP (§ 4 bis) : fait une fois pour les deux.

**Prudence** (un agent dans un navigateur voit ce que l'utilisateur voit) :

- par défaut, un **profil à part** (ni les cookies ni les mots de passe de l'utilisateur) ; le profil de l'utilisateur
  seulement si on le demande ;
- une **bande visible** dans la fenêtre de Jet tant qu'un agent la pilote, et un bouton pour l'arrêter ;
- jamais de saisie dans un champ de mot de passe ni d'envoi de paiement par l'agent ; `evaluate` et `upload_file` avec
  accord ;
- écoute sur le bouclage seulement, avec jeton (le réseau, plus tard, avec jeton aussi, comme TerraCube).

**Taille** : une semaine de plus que CDP, la couche étant commune (le transport existe ; `snapshot` est le plus gros
morceau). Ordre proposé : la couche de Jet et le mode sans fenêtre, puis **MCP d'abord** (claude-acp et Vibe s'en
servent tout de suite, pour lire et essayer des pages), puis CDP pour Craft Anything.

## 5. Estimation

Les retouches 1 à 4 : une demi-journée (tables, un adaptateur, la détection, le CLI dans l'app). L'essai sous QEMU avec
les fausses API : une demi-journée. Le plus incertain : ce que les outils d'inspection de Craft Anything exigent avant
de laisser l'agent travailler (le parcours « reconstruire » peut refuser de partir sans eux).
