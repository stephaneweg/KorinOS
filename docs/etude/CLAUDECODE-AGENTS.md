# Étude : plusieurs agents dans l'app Claude Code (claude-acp, puis Vibe, Kimi, MAI…)

Demande de l'utilisateur (21/11/2026) : après le retrait de l'app Claude (claude-acp par clé d'API), étudier la greffe
de claude-acp comme **second mode** de l'app Claude Code (`src/apps/claudecode`), au moment où l'on démarre une
conversation ; plus tard Vibe, Kimi, MAI, etc. Rien n'est fait : c'est l'étude.

## Ce qui existe

| Pièce | Ce qu'elle fait | Où |
|---|---|---|
| L'app Claude Code | frontal de `claude -p` en stream-json : conversations de `~/.claude/projects/*.jsonl` (lister, renommer, supprimer, ranger), chat, accords d'outils | `src/apps/claudecode` (`cli.rs` le protocole, `store.rs` les sessions, `chat.rs` l'affichage) |
| Le pilote d'agents de KorinStudio | **un seul pilote pour quatre agents** : Claude Code (stream-json) et les agents ACP (claude-acp, vibe-acp, mai-acp : JSON-RPC 2.0 sur stdio) ; des fils, pas d'async ; une interface commune `AgentEvent` (texte, réflexion, outils, plan, demande d'accord, fin de tour, connexion, erreurs) et `AgentCmd` (demande, arrêt, accord, mode, modèle) | `src/apps/korinstudio/src/agent.rs` (2 000 lignes) |
| claude-acp | l'agent sur l'API de Claude (SDK `anthropic`), ACP ; ses séances dans `$CLAUDE_ACP_HOME/sessions/<id>.json` ; la clé dans `.env` (`claude-acp --set-key`) | `tools/claude` (le paquet `[claude]` est retiré ; le code reste, MAI repose dessus) |
| libacp | le client ACP en C des apps Vibe et MAI | `src/lib/acp` |

## Proposition

1. **Une bibliothèque Rust partagée** tirée de `korinstudio/src/agent.rs` (par exemple `src/lib/korin-agents`) :
   `AgentKind`, `spawn`, `AgentEvent`, `AgentCmd`, et en plus un **magasin de séances par agent** (lister, lire,
   renommer, supprimer : `~/.claude/projects` pour Claude Code, `sessions/*.json` pour claude-acp et MAI, celui de
   Vibe). KorinStudio et l'app Claude Code s'en servent tous les deux (règle du dépôt : ce qui sert à plusieurs
   programmes ne reste pas dans une app). L'extraction doit garder KorinStudio tel qu'il est (autre session :
   prévenir, essayer son copilote après).
2. **L'app Claude Code passe sur `AgentEvent`** : `chat.rs` reçoit les événements communs au lieu du JSON de Claude
   Code (`cli.rs` disparaît dans le pilote partagé). À vérifier : ce que l'app montre aujourd'hui et qu'`AgentEvent` n'a
   pas (les deltas du texte au fil de l'eau, le coût du tour, l'invite de connexion) ; l'ajouter au pilote plutôt que
   de le perdre.
3. **Le choix du moteur** dans « Nouvelle conversation » : Claude Code (abonnement), Claude (clé d'API, claude-acp),
   Vibe, MAI… ; un moteur absent est grisé avec la commande qui l'installe. La liste des conversations montre toutes
   les séances, avec une petite marque du moteur ; une conversation se reprend toujours avec son moteur.
4. **Les comptes** : la clé d'API de claude-acp et de MAI par un dialogue (comme l'ancienne app Claude : champ masqué,
   `.env`), les comptes de Vibe par `vibe-account` ; Claude Code garde sa connexion par Jet.
5. **Les paquets** : l'app reste dans `claude-code` et ne dépend d'aucun autre agent (détection au lancement). Pour le
   mode « clé d'API », remettre un paquet **sans app** : la commande `claude-acp`, `claude_acp` et le SDK `anthropic`
   (ce que portait `[claude]`, moins `apps/claude.app`) ; MAI le prend aussi.
6. **Kimi** : sans doute par Vibe (idée de l'utilisateur), qui prend d'autres fournisseurs de modèles par sa
   configuration : un « moteur » Vibe avec un modèle Kimi plutôt qu'un agent de plus. Les suivants : s'ils parlent ACP,
   une ligne de plus dans `AgentKind` ; sinon
   un pilote de plus dans la bibliothèque. Sous Korin : comme pour Claude Code, par l'hôte Linux si l'agent n'existe
   qu'en binaire Linux, ou en Python s'il en est.

## Étapes

1. Extraire le pilote de KorinStudio dans la bibliothèque, sans changer son comportement ; KorinStudio s'en sert.
2. L'app Claude Code sur la bibliothèque, Claude Code seul (rien ne doit changer pour l'utilisateur).
3. Le magasin de séances de claude-acp, le choix du moteur, le dialogue de clé ; le paquet `claude-acp` sans app.
4. Vibe, MAI ; puis Kimi.

Essais : sous QEMU, une fausse API (`tools/claude/fakeapi.py`) pour claude-acp sans clé ; Claude Code par son
« Not logged in » ; KorinStudio par son copilote.
