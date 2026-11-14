---
name: armpi-vibe-kimi
description: "Pour plus tard (13/11) : utiliser Mistral Vibe sur Korin avec le modèle Kimi K3 au lieu de Mistral, une fois le système au point"
metadata:
  type: project
---

Le 13/11/2026, l'utilisateur a noté pour plus tard, quand le système sera au point : Python et tout ce qu'il fallait pour Vibe sont portés, et Vibe tourne bien sur le Pi. Vibe est normalement ouvert à d'autres LLM que Mistral ; l'utilisateur veut l'utiliser avec **Kimi K3** (Moonshot).

**Why:** choix de modèle de l'utilisateur pour l'agent dans Korin.

**How to apply:** pas maintenant. Quand on y viendra : vérifier comment Vibe déclare un fournisseur (API compatible OpenAI ?), la clé reste à l'utilisateur (jamais saisie par Claude), et adapter les compétences/AGENTS.md de tools/vibe/home si le modèle se comporte autrement. Sur le PC, Vibe est déjà dans le plan après Jet (voir [[armpi-x86-pc]]). Voir [[armpi-python-vibe]], [[armpi-vibe-facade]], [[armpi-vibe-competences]], [[armpi-agent-ia-korin]].

**Précisions du 13/11** : il faudra aussi
- un **assistant (wizard) pour ajouter un compte** de fournisseur de modèle ;
- éventuellement un **lien vers la page de connexion du fournisseur, ouvert dans Jet**, pour obtenir la clé d'API puis l'utiliser (la clé est saisie ou collée par l'utilisateur ; elle se garde dans ses réglages, jamais en clair dans un dépôt) ;
- le **choix du modèle (LLM) au démarrage de chaque nouvelle session** de Vibe.
Ces écrans vont dans la façade native de Vibe (app UIKit, [[armpi-vibe-facade]]) ; Jet doit donc exister (après .NET dans le plan du PC).
