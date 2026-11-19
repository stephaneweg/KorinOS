"""oai-acp : un agent de code pour Korin sur une API compatible OpenAI (chat completions), qui parle ACP sur stdio.

Il reprend claude_acp (tools/claude/claude_acp) : les outils, les accords, l'historique, les serveurs MCP, le
JSON-RPC ; seuls changent la requête (HTTP, flux SSE, appels de fonctions), la forme de l'historique et la
configuration. Le profil « mai » (mai-acp) vise les modèles MAI de Microsoft, servis par Microsoft Foundry
(docs/MAI-ACP.md).
"""

__version__ = "0.1.0"
