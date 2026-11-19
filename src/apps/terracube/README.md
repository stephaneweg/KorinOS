# TerraCube

Bac à sable de blocs en 3D pour Korin PC, par le GPU (OpenGL ES 3 de GlKit), pilotable par Python, BASIC ou un agent
IA (MCP). Référence complète : [docs/TERRACUBE.md](../../../docs/TERRACUBE.md).

* Construire : `sh tools/mesa/glkit-pc-from-pkg.sh` (une fois, sans Mesa construit), puis `sh tools/pc/apps.sh terracube`.
* Essayer : `terracube --selftest` (vérifications sur la sortie, `HOME:/terracube/selftest.png`).
* Scripts : port 4730 (`data/python/terracube.py`, `data/basic/*.bas`) ; agent : port 7312, `/mcp`.

État (19/11/2026) : essayé sous QEMU q35 (softpipe) seulement ; GPU Intel du ZBook à essayer.
