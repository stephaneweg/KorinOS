"""claude-acp : un agent de code pour Korin sur l'API de Claude (SDK anthropic), qui parle ACP sur stdio.

Les façades de Korin (le client terminal `agent --claude`, et toute autre sur libacp) le lancent comme un process fils
et lui parlent en JSON-RPC 2.0, un message par ligne (docs/CLAUDE-ACP.md).
"""

__version__ = "0.1.0"
