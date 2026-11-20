# mai-acp : MAI de Microsoft sur Korin, par Microsoft Foundry

`mai-acp` est un agent de code qui tourne **sur Korin** et parle aux modèles **MAI-Code** de Microsoft par une API
**compatible OpenAI** (chat completions, appels de fonctions, en flux). Comme [claude-acp](CLAUDE-ACP.md), dont il
reprend le code, il lit, écrit et modifie des fichiers, lance des commandes par bash, cherche dans le code, charge les
compétences de Korin et lit des pages web. Il demande l'accord de l'utilisateur avant chaque modification et chaque
commande (mode `ask`).

C'est un process Python qui parle **ACP** (JSON-RPC sur stdio) aux façades de Korin sur `libacp` : l'app **MAI** et
`agent --mai`. Le cœur est générique, `oai_acp` : un point d'accès, une clé et des modèles. MAI en est le profil par
défaut. Un autre fournisseur compatible OpenAI demande seulement un profil de plus (`PROFILES` dans
`tools/oai/oai_acp/agent.py`) ou un `config.toml`.

## Les modèles MAI (état au 19/11/2026)

* **MAI-Code-1.1-Flash** (août 2026) est le modèle par défaut. Il est servi par **Microsoft Foundry** en préversion,
  avec une fenêtre de 256 000 jetons, 64 000 en sortie, les images en entrée et les appels de fonctions. Prix de la
  grille Azure : 0,20 $ par million de jetons en entrée, 0,02 $ en cache, 1,20 $ en sortie.
* **Retrait** : l'API Azure Models annonce le retrait de MAI-Code-1.1-Flash de Foundry au **9 décembre 2026**. GitHub
  Copilot le retire le 18 décembre (changelog GitHub du 17/11/2026). Le successeur est **MAI Code 2 Flash**,
  annoncé le 17/11/2026, en préversion à accès limité dans Foundry, sans prix publié. Il est déjà dans la liste
  (`MAI-Code-2-Flash`). Quand il sera accessible : le déployer, puis Menu MAI > Modèle de la séance..., ou
  `model = "..."` dans `config.toml`. `/cost` dit « coût inconnu » tant que son prix n'est pas dans la config.
* Pas d'OpenRouter : aucun modèle MAI Code n'y est proposé. Copilot CLI (`copilot --acp`) parle ACP, mais il
  demande Node.js, absent de Korin.

## S'en servir

1. Dans [Microsoft Foundry](https://ai.azure.com) : une ressource Foundry, puis un **déploiement** de
   MAI-Code-1.1-Flash. La page du déploiement donne le point d'accès (`https://<ressource>.services.ai.azure.com`)
   et la clé.
2. Sur Korin :

```
mai-acp --set-key             # une fois : le point d'accès puis la clé (saisie cachée), rangés dans ~/.local/mai-acp/.env
agent --mai                   # le client terminal, dans le dossier de travail
agent --mai --continue        # reprend la dernière séance de ce dossier
```

Ou l'app **MAI** (lanceur, catégorie Développement), la façade de Vibe avec le profil MAI. Sans point d'accès ni
clé, elle ouvre d'elle-même le dialogue « Point d'accès et clé de MAI ». Ce dialogue a deux champs et un bouton qui
ouvre Foundry dans Jet. Le point d'accès va dans `.env` (`MAI_BASE_URL`) et la clé au coffre à secrets
([KEYVAULT.md](KEYVAULT.md), sous `env/MAI_API_KEY`) ; laisser la clé vide garde celle qui est rangée. Menu MAI >
Clé d'API... rouvre le dialogue. Le journal est dans `~/.local/mai-acp/logs/agent.log`.

Les touches et commandes sont celles de Claude : Entrée envoie ; Échap interrompt ; Maj+Tab change le mode (`ask`,
`accept-edits`, `auto-approve`) ; `/model`, `/cost` (jetons et coût estimé), `/account`, `/help`.

Le **nom du modèle** est celui du **déploiement** : Foundry le met dans le champ `model` de la requête. Un déploiement
nommé autrement que le modèle se déclare dans `config.toml` (`model = "mon-deploiement"`). Une erreur 404 vient
presque toujours d'un mauvais point d'accès ou d'un mauvais nom de déploiement, et le message le dit.

## La configuration

Le dossier de l'agent est `$MAI_ACP_HOME`, par défaut `~/.local/mai-acp` (`/home/.local/mai-acp`), comme les
données des autres apps. L'ancien `~/.mai-acp` est ignoré depuis le 20/11. Le paquet pose `AGENTS.md` et `skills/`
dans `/.mai-acp`, et libacp (`acp_home`) les recopie dans le dossier du compte. Ce dossier contient :

* `.env` : `MAI_BASE_URL=...` et `MAI_API_KEY=...`. Ils appartiennent à l'utilisateur et ne vont jamais dans un
  paquet ; l'app passe la clé au coffre. L'environnement l'emporte sur `.env`, et `MAI_BASE_URL` sur `base_url`.
* `config.toml` (facultatif) :

```toml
base_url = "https://ma-ressource.services.ai.azure.com/mai/v1"
model = "MAI-Code-1.1-Flash"         # le nom du déploiement
max_tokens = 64000                    # envoyé en max_completion_tokens (token_param = "max_tokens" pour d'autres API)
auth = "api-key"                      # ou "bearer" ; par défaut : api-key pour *.azure.com, bearer ailleurs
# reasoning_effort = "medium"         # transmis tel quel s'il est donné
# temperature = 0.2
# extra = { ... }                     # des champs de plus dans chaque requête
web = true                            # l'outil web_fetch
models = [                            # la liste offerte (Modèle de la séance...) ; prix en $ par million de jetons
  { id = "MAI-Code-1.1-Flash", input = 0.20, output = 1.20, cached = 0.02, context = 256000 },
  { id = "MAI-Code-2-Flash", name = "MAI Code 2 Flash", context = 256000 },
]
# [[mcp_servers]] : comme claude-acp (Forge...)
```

* Le **point d'accès** est normalisé : un hôte `*.services.ai.azure.com` seul reçoit `/mai/v1`, le chemin des modèles
  MAI dans la documentation de Microsoft, et `*.openai.azure.com` reçoit `/openai/v1`. Un `/chat/completions` final
  est retiré, un `?api-version=...` est gardé. Une autre adresse est prise telle quelle.
* `AGENTS.md` et `skills/` sont ceux de claude-acp (`tools/claude/home.py ... .mai-acp`). `sessions/<id>.json` garde
  l'historique au format de l'API, rejoué tel quel ; `logs/agent.log` est le journal.

## Ce qu'il y a

| Fichier | Rôle |
|---|---|
| `tools/oai/oai_acp/agent.py` | les profils (`mai`, `openai`), la config, la séance : requête (urllib, flux SSE), historique au format chat completions, `web_fetch`, coût, reprise |
| `tools/oai/oai_acp/main.py` | le serveur ACP de claude-acp avec cette séance ; `--set-key`, `--profile` |
| `tools/claude/claude_acp/` | ce qui est repris tel quel : outils, accords, MCP, JSON-RPC, le tour, la mémoire sur disque |
| `tools/oai/build.sh` | `oai_acp`, `/bin/mai-acp` et `.mai-acp/` dans l'arbre de la carte (après `tools/claude/build.sh` ; appelé par `site-packages.sh`) |
| `tools/oai/fakeoai.py`, `oaitest.py` | une fausse API chat completions (SSE) et l'essai de bout en bout, sans clé ni crédit |
| `tools/agent/agent.c` | le client terminal : `--mai` lance `mai-acp` |
| `src/apps/mai/` | l'app MAI : le source de Vibe (`src/apps/vibe/main.c`, `KORIN_MAI`), son manifeste ; icône `mai` de `tools/icons/art.py` |
| `src/lib/acp/home.c` | `acp_home (ACP_PROFILE_MAI, ...)` : `$MAI_ACP_HOME`, les compétences du paquet recopiées pour le compte |

## Les choix

* **Pas de SDK** : la requête passe par `urllib` (HTTPS de Python), sans dépendance de plus. Le SDK `openai`
  demanderait `distro`, `tqdm` et `jiter`, pour peu de chose : un POST et la lecture d'un flux SSE. Une réponse qui
  n'est pas en SSE (un proxy qui ignore `stream`) est lue aussi.
* **Ce qui vient de claude-acp** : la séance (`Session`) est une sous-classe de celle de claude-acp. Seules changent
  la boucle du tour (les `tool_calls`), la requête, le coût et la reprise. Les outils sont les mêmes, donnés en
  fonctions (`{"type": "function", ...}`), et `tools.validate` revérifie leurs entrées. Pour s'y prêter,
  `claude_acp` a reçu quatre crochets : le dossier (`HOME_ENV`, `HOME_DIR`), la base du prompt système, la liste des
  modèles (`Session.models()`), et la séance et le nom du serveur (`Server.SESSION`, `INFO`). `anthropic` n'y est
  plus importé qu'à la requête.
* **web_fetch** est exécuté par l'agent, alors que chez Claude c'est un outil côté serveur. Il fait un GET et
  convertit le HTML en texte, 40 000 caractères au plus. Il n'y a pas de `web_search` : les API compatibles OpenAI
  n'en ont pas de standard.
* **L'historique** est gardé au format de l'API : `assistant` avec `tool_calls`, puis un message `tool` par appel.
  Un tour interrompu pendant le flux n'est pas gardé, et sa demande est fusionnée avec la suivante ; un tour
  interrompu pendant les outils donne « Interrompu » à chaque appel. La pensée (`reasoning_content` ou `reasoning`)
  est montrée en gris, mais n'est pas gardée.
* **Les images** des outils MCP (captures de Forge) ne peuvent pas aller dans un message `tool`. Elles suivent dans
  un message `user`, en `image_url` (data:).
* **Les erreurs** : 401 et 403 donnent -32002, et la façade rouvre le dialogue de la clé. Les codes 408, 409, 429,
  5xx et les erreurs réseau sont réessayés, jusqu'à quatre fois.

## Construire et essayer

```
wsl ARCH=x86_64 bash tools/claude/build.sh && wsl ARCH=x86_64 bash tools/oai/build.sh    # le PC
wsl python3 tools/oai/oaitest.py                   # l'essai, sans clé (n'importe quel Python 3.11 et plus)
wsl sh tools/pc/apps.sh mai                        # l'app MAI
```

`oaitest.py` lance la fausse API puis `mai-acp` (`python -m oai_acp`) et lui parle comme une façade, avec le client
de `tools/claude/acptest.py`. Il fait 44 vérifications :

* clé absente, prompt système, flux et pensée ;
* appels d'outils coupés en morceaux, accord, refus, deux appels dans un message, bash permis pour la séance ;
* `web_fetch`, réponse sans flux, 429 réessayé ;
* interruption, `/cost`, changement de modèle, reprise d'une séance, clé refusée.

Sous Korin : `python3 /home/pytest/oaitest.py --installed` (`TESTS=1` pour le PC).

**Vérifié sous QEMU PC le 19/11** (`tools/python/pctest.py`, lancé depuis WSL avec l'OVMF de `~/korin-cloud`) :

* les 44 vérifications passent en 39 s, avec les modules installés sur le disque ;
* l'app MAI s'ouvre et montre « MAI (démarrage…) », « Démarrage de mai-acp » et le dossier du compte (alors `/home/.mai-acp`, `~/.local/mai-acp` depuis le 20/11).

Ce jour-là, `python3 fichier.py` plantait sous Korin PC (`Segmentation fault` dès le démarrage, avec `-P`, `-I` ou
`-S`, même pour un `print`), alors que `python3 -m`, `-c`, `< fichier` et `runpy` marchaient. Cela touchait aussi
`/bin/mai-acp` et `/bin/claude-acp`, qui sont des scripts `#!/bin/python3`. L'essai est donc passé par `runpy` sans
`--installed` : l'agent est lancé par `python3 -m oai_acp`, et la fausse API par `-m fakeoai`.

Corrigé le 20/11 : le patch de CPython du coffre à secrets (18/11) déclarait le script au noyau en encodant son
chemin par `Py_EncodeLocale`, donc par obmalloc, avant qu'il y ait un interpréteur ; il passe maintenant par
l'allocateur brut (`tools/python/korin.patch`, paquet python 1.0.3). `oaitest.py --installed` passe alors aussi.

Il n'y a pas eu d'essai contre le vrai Foundry, faute de déploiement. Le chemin `/mai/v1` et l'en-tête `api-key`
viennent de la documentation de MAI-Thinking-1. Si MAI-Code les veut autrement, il suffit de donner `base_url` et
`auth` dans `config.toml`.

## Paquet

`[mai]` dans `tools/pkg/packages-pc.ini`, avant `[claude]` et `[vibe]`, avec `needs = claude` : `oai_acp` reprend
`claude_acp`. Le client `agent` est dans Vibe. La compétence `korin-forge` de `.mai-acp` va avec Forge, comme pour
Vibe et Claude.
