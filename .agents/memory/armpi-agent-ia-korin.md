---
name: armpi-agent-ia-korin
description: "Agent de code sur Korin (11/11/2026) : abonnement sans tokens d'API, Mistral Vibe envisagé avec façade native, AGENTS.md + compétences"
metadata:
  node_type: memory
  type: project
  originSessionId: 54c6338b-cfac-439e-8a3c-ac2dadb96b3a
  modified: 2026-11-11T13:45:42.912Z
---

Le propriétaire veut un agent de code qui tourne SUR Korin et agit sur le système (pas seulement du dev).

- Refus de payer des tokens d'API : seul un client officiel couvert par un abonnement convient (client Claude maison exclu).
- (13/11 : remplacé par la couche Linux, voir [[armpi-linux-compat]]) Claude Code : voie Node (docs/NODEJS.md), 2.1.112 en JS toujours sur npm (vérifié le 11/11) ; recommandation Node 24 ; connexion par `claude setup-token` → CLAUDE_CODE_OAUTH_TOKEN. Voie A (bundle extrait du natif) bloquée par les conditions d'Anthropic, voir docs/etude/ETUDE-CLAUDE-NATIF.md.
- 4e voie : Mistral Vibe (Apache 2.0, inclus dans Le Chat Pro, compte gratuit possible avec quota). Vibe 3.x = interface en Rust (vibe/cli-rust) + moteur Python (vibe-app-server sur stdio) ; dépendances Python avec extensions Rust (pydantic-core, rpds-py, cryptography, watchfiles, uuid-utils).
- **Préférence du propriétaire : notre propre façade, « Vibe natif Korin »** (app/terminal à nous qui parle à vibe-app-server), plutôt que porter l'interface Rust.
- AGENTS.md + 30 compétences .agents/skills (format SKILL.md commun à Vibe et Claude Code) : commit 9b5e82ef sur claude/agents-korin (11/11), pas poussé. Découpage voulu par le propriétaire : une compétence par kit (korin-kit-*), par langage (korin-lang-* : asm, c, cpp, freebasic, qbasic, csharp, csharp-avalonia, csharp-winforms), et pour agir sur le système (korin-sys-*). Pas encore installées sur la carte.
- Étude Python lancée dans une session à part (docs/PYTHON.md, branche etude-python) : Python vu comme langage général de Korin + « artefacts à la demande » par l'agent, pas seulement pour Vibe.

**Why:** abonnement uniquement ; l'agent doit connaître Korin (chemins SD0:, UIKit, kits) et pouvoir vérifier.
**How to apply:** tout travail d'agent IA sur Korin vise d'abord Vibe + façade native ; les compétences restent communes aux deux agents. Lié à [[armpi-pocketqi]], [[armpi-posix-par-necessite]].
