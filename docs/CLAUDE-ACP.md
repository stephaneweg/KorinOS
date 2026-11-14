# claude-acp : Claude sur Korin, par la clé d'API

`claude-acp` est un agent de code qui tourne **sur Korin** et parle à l'API de Claude par le SDK Python officiel
(`anthropic`). Il lit, écrit et modifie des fichiers, lance des commandes par bash, cherche dans le code, charge les
compétences de Korin, et peut ainsi créer, modifier et construire des apps et le système lui-même. Il demande
l'accord de l'utilisateur avant chaque modification et chaque commande (mode `ask`).

C'est le pendant de Vibe ([PYTHON-PORT.md](PYTHON-PORT.md)) avec Claude : un process Python qui parle **ACP**
(Agent Client Protocol, JSON-RPC sur stdio) ; les façades de Korin sur `libacp` (`src/lib/acp`) s'en servent sans
changement. Il est payé à l'usage, par la clé d'API (console.anthropic.com), pas par l'abonnement : l'abonnement
reste pour le vrai Claude Code, par la couche Linux ([etude/LINUX-COMPAT.md](etude/LINUX-COMPAT.md)).

## S'en servir

```
claude-acp --set-key          # une fois : la clé (sk-ant-...), saisie cachée, rangée dans /sd0/.claude-acp/.env
agent --claude                # le client terminal, dans le dossier de travail
agent --claude --continue     # reprend la dernière séance de ce dossier
```

Ou l'app **Claude** (lanceur, catégorie Développement) : la façade graphique de Vibe avec le profil Claude
(`src/apps/claude/main.c` inclut `src/apps/vibe/main.c` avec `KORIN_CLAUDE` défini). Sans clé, elle ouvre d'elle-même
le dialogue « Clé d'API de Claude » (champ masqué, bouton vers la console dans Jet), qui écrit `.env` et relance
claude-acp ; Menu Claude > Clé d'API... le rouvre. Le modèle vient de `config.toml` (pas de question à chaque séance) ;
Menu Claude > Modèle de la séance... le change. Le journal : `/sd0/.claude-acp/logs/agent.log`.

Dans `agent --claude` : Entrée envoie ; Échap interrompt le tour ; Maj+Tab change le mode (`ask`, `accept-edits` :
les fichiers sans demander, `auto-approve` : rien ne demande) ; `/model` change de modèle ; `/cost` donne les jetons
et le coût estimé de la séance ; `/account` change la clé ; `/help`. À la fin de chaque tour : la durée et la place
prise dans le contexte.

## Ce qu'il y a

| Fichier | Rôle |
|---|---|
| `tools/claude/claude_acp/main.py` | le serveur ACP : `initialize`, `session/new`, `/list`, `/load`, `/prompt`, `/cancel`, `/set_mode`, `/set_config_option` (le modèle) ; `--set-key` |
| `tools/claude/claude_acp/agent.py` | la séance : le prompt système, la boucle des outils, les accords (`session/request_permission`), le coût, l'historique sur disque |
| `tools/claude/claude_acp/tools.py` | les outils : `read_file`, `write_file`, `edit_file`, `bash`, `glob`, `grep` (rg), `skill`, `update_plan` ; et côté serveur `web_search`, `web_fetch` |
| `tools/claude/claude_acp/rpc.py` | JSON-RPC sur stdin/stdout ; stdout ne porte que du JSON (le reste va au journal) |
| `tools/claude/jiter/` | le remplaçant en Python pur de `jiter` (un module Rust du SDK) |
| `tools/claude/home/AGENTS.md`, `home.py` | ce que l'agent sait de Korin ; `home.py` pose `.claude-acp/` avec les compétences de `.agents/skills` et de Vibe |
| `tools/claude/build.sh` | le SDK, `claude_acp`, `/bin/claude-acp` et `.claude-acp/` dans l'arbre de la carte (appelé par `site-packages.sh`) |
| `tools/claude/fakeapi.py`, `acptest.py` | une fausse API Messages (flux SSE) et l'essai de bout en bout, sans clé ni crédit |
| `tools/agent/agent.c` | le client terminal : `--claude` lance `claude-acp` |
| `src/apps/claude/` | l'app Claude : le source de Vibe (`src/apps/vibe/main.c`, macros `IS_CLAUDE`, `AGENT_NAME`, `VC`), son manifeste ; icône `claude` de `tools/icons/art.py` |

Le dossier de l'agent est `$CLAUDE_ACP_HOME`, par défaut `$HOME/.claude-acp` (`/sd0/.claude-acp`) :

* `.env` : `ANTHROPIC_API_KEY=...` (à l'utilisateur, jamais dans un paquet) ;
* `config.toml` (facultatif) : `model` (`claude-opus-5-5` par défaut, ou `claude-sonnet-5-5`, `claude-haiku-5-5`,
  `claude-fable-5-2`), `effort` (`high` ; `low`... `max`), `max_tokens` (64000), `web`, `compaction`, `fallbacks`
  (vrais par défaut) ;
* `AGENTS.md` et `skills/` : posés par `home.py`, remplacés à chaque mise à jour ;
* `sessions/<id>.json` : l'historique de chaque séance (reprise par `--continue`) ; `logs/agent.log` : le journal.

Le prompt système : les consignes de l'agent, `AGENTS.md`, la liste des compétences (premier bloc, en cache, le même
pour toutes les séances), puis le dossier de travail, la date et les `AGENTS.md` / `CLAUDE.md` du projet et de ses
parents.

## Les choix de l'API

* Requêtes en flux (`client.beta.messages.stream`), pensée adaptative résumée (montrée en gris dans le terminal),
  `effort` de `config.toml`, cache automatique (`cache_control` en tête) plus un point de cache sur la partie fixe du
  prompt système.
* **Fallbacks côté serveur** (`fallbacks: "default"`, bêta `server-side-fallback-2026-07-01`) : quand un modèle refuse
  une demande, un autre la reprend dans le même appel. Pas pour Haiku. **Compaction** côté serveur (bêta
  `compact-2026-01-12`) : une longue séance est résumée par l'API au-delà de 150 000 jetons. Si l'API refuse l'un des
  deux (400), il est retiré pour la séance et la requête refaite.
* L'historique est gardé tel que l'API l'a rendu (blocs de pensée et leurs signatures compris) et ne fait que
  s'allonger : un tour interrompu pendant le flux n'est pas gardé, sa demande est fusionnée avec la suivante ; un tour
  interrompu pendant les outils donne un résultat « interrompu » à chaque appel.
* Les entrées d'outils ne sont **pas** demandées en flux (`eager_input_streaming`) : avec le jiter en Python pur, le
  SDK relirait toute l'entrée à chaque morceau. L'API les envoie d'un bloc, validées ; `tools.validate` revérifie.
* Le coût de `/cost` est une estimation faite sur les prix publics (entrée, sortie, lecture et écriture du cache),
  hors recherche web.

## Le jiter de Korin

Le SDK importe `jiter` (Rust) pour lire les entrées d'outils pendant le flux (`from_json(..., partial_mode=...)`).
`jiter` 0.13 et plus ne se construit pas pour Korin (un banc d'essai, `codspeed`, veut une `libc` de Rust plus
récente que celle de `tools/rust`) ; 0.12.0 se construit pour le Pi par `build-rust-ext.sh`, mais pas pour le PC sans
sa chaîne Rust. `tools/claude/jiter` le remplace en Python pur : `json.loads`, et pour un JSON coupé une lecture
tolérante qui rend la même chose que jiter (comparé à lui coupe par coupe). Aucun module natif : les mêmes fichiers
pour le Pi et le PC.

## Construire et essayer

```
wsl bash tools/claude/build.sh                     # le Pi : build/python/sdroot (après site-packages.sh)
wsl ARCH=x86_64 bash tools/claude/build.sh         # le PC : build/pc/python/sdroot
wsl DEST=/tmp/claude-pc ARCH=x86_64 bash tools/claude/build.sh   # un arbre à part (installation à la main)
```

Essai sur le PC (WSL), sans clé : un venv avec `anthropic==2.8.0`, puis `python3 tools/claude/acptest.py` (fausse
API, `claude-acp` lancé par `python -m claude_acp`, une cinquantaine de vérifications : séance, flux, pensée rejouée avec sa signature,
accords, diff, bash, interruption, refus, changement de modèle, reprise, clé refusée). Sous Korin :
`python3 /sd0/home/pytest/acptest.py --installed` (carte d'essai de `testimage.py`, ou `TESTS=1` pour le PC).

## Paquet

`[claude]` dans `tools/pkg/packages-pc.ini` (avant `[vibe]`, qui prend le reste de site-packages), `needs = vibe` :
le client `agent` et pydantic, httpx2, anyio... sont dans Vibe. Pour le Pi, à ajouter à `packages.ini` à côté de
`[vibe]` (la branche `main` l'a ; pas celle du PC).
