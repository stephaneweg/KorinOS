---
name: armpi-terracube
description: "19/11 : TerraCube, bac à sable de blocs (genre « cubes ») pour Korin PC sur GlKit, pilotable par Python, BASIC (port 4730) et MCP (7312)"
metadata:
  type: project
---

Demandé le 19/11/2026 : « un jeu comme Minecraft pour Korin (version PC) utilisant le GPU », en option un MCP et le
pilotage par Python ou BASIC. Fait dans `src/apps/terracube` (nom et contenu originaux : aucun nom, dessin ni niveau
repris d'un jeu existant), doc `docs/TERRACUBE.md`.

- Rendu OpenGL ES 3.0 par GlKit (iris sur le GPU Intel, softpipe sous QEMU) ; tronçons 16 × 128 × 16, mer à 48,
  maillage CPU avec occlusion ambiante et lumière du ciel lissée ; HUD texte par UIKit (`ui_surface_overlay`).
- Scripts : protocole texte une commande par ligne, port 4730 (127.0.0.1, `--api-any` : réseau) ; Python
  `data/python/terracube.py` (+ exemples) ; BASIC d'Onyx par NetKit, sans inclusion : SUB à recopier
  (`data/basic/terracube.bas`).
- MCP : transport repris de Forge (`mcp.cpp`, copie : à mettre en commun un jour dans une lib), 16 outils dont
  `screenshot` d'un autre point de vue (surface hors écran), jeton dans le coffre comme Forge.
- Construire sans Mesa : `tools/mesa/glkit-pc-from-pkg.sh` tire libglkit.a du glkit.so du paquet pc (relais par nom).
- Essayé sous QEMU q35 (softpipe) : `--selftest` 22/22, Python et MCP depuis l'hôte par hostfwd, les 3 .bas dans Korin. **Pas essayé sur le
  ZBook** (GPU Intel). Paquet `terracube = 1.0.0` ([pc] de versions.ini), pas publié.

**How to apply:** les numéros de blocs ne bougent jamais (ajout à la fin de `blocks.h`, de `terracube.py`, de la doc).
