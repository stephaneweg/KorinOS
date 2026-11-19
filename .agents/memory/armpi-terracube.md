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

- 19/11 (suite) : eau animée par les shaders ; **mode survie** (minage au temps près selon l'outil, ramassage,
  inventaire 36 cases, outils bois/pierre/fer/cristal avec usure, établi et four), puis à la demande de
  l'utilisateur une **table de fabrication à grille** (2 × 2 à la main, 3 × 3 à l'établi, recettes à forme,
  déplaçables et retournables, `matchGrid` dans `items.cpp` ; d'abord un dialogue UIKit, puis à la demande de l'utilisateur **dessiné dans le jeu** par le GPU
  (CraftDraw), table en haut, inventaire en bas, glisser-déposer ; textes par UIKit sur fond transparent ; console
  semi-transparente : l'octet haut d'un pixel de ui_fill est sa transparence par-dessus une surface, ui_text écrit
  opaque (0) ; pour un BMP décodé par UIKit, l'alpha 0 est opaque), **console de commandes** (T, / ou ² : time, weather, mode, tp, give, craft… : `console.cpp`, aussi
  `game.command` et l'outil MCP `run_command`), **météo** (pluie/neige/orage, gouttes dans le vertex shader,
  cachées sous les toits par une carte des hauteurs 64 × 64). Version 1.1.0.

- Pièges vus par l'utilisateur sur le ZBook (19/11, 1.1.1) : pointeur verrouillé (`ui_pointer_lock`), Elegant n'envoie
  QUE des WEV_M_REL, boutons dans EV_CODE (pas d'appui ni de relâché) : sinon le bouton du clic d'entrée reste « tenu »
  et tout ce qu'on vise se casse ; scintillement des textes par-dessus la vue : dessiner dans un tampon à part
  (détourner `ui.surf`, comme `ui_canvas_begin`) puis recopier les zones finies ; ça ne suffisait pas : 1.1.2 quitte
  `armgl_window` + `ui_surface_overlay` pour le chemin de Cosmo Zoom (rendu hors écran, glReadPixels, textes dans
  l'image, recopie d'un coup, ui_update_all), à confirmer sur le ZBook.

- 1.2.0 (19/11) : son synthétisé dans le jeu (`audio.cpp`, thread + flux AudioKit, ≈ 1 % d'un cœur) : musique
  générative selon la situation, ambiance, effets par matière. Essai sans son réel : moteur compilé sur l'hôte avec un
  faux AudioKit qui écrit un WAV (scratchpad/snd), niveaux et spectrogramme vérifiés ; pas encore écouté sur le ZBook.

- 1.3.0 (19/11) : musique refaite à la demande (« nappes moins présentes, une mélodie chill par-dessus, pour être
  focus ») : piano électrique FM à motifs (A A' B A''), accords jazz, batterie feutrée de jour ; démo 3 min envoyée.
  **Lave** et eau qui coule (états de bloc : `Chunk::m`, un octet par case, fichiers TCC2 ; TCC1 relus), obsidienne,
  seaux ; **circuits de cuivre** (nom original, pas « redstone ») dans `sim.cpp` : fil en réseau (Dijkstra par
  seaux), forte/faible alimentation, torche-inverseur (grille), répéteur (verrou), comparateur, observateur,
  pistons + colle (12 blocs), lampe, bloc musical, capteur de jour, dynamite. Banc d'essai hôte de sim.cpp :
  scratchpad/simt/t.cpp. Suite demandée : santé/faim, créatures de nuit, animaux apprivoisables (poules, loups).

- 1.4.0 (19/11) : créatures (`entity.cpp`) : poule, cochon, loup (apprivoisé par des os : garde assis, suit debout),
  rôdeur (brûle au soleil) et araignée (grimpe) la nuit ou dans le noir ; santé, faim, air, chute, lave, mort ->
  point de départ (inventaire gardé), en survie seulement ; épées, cuisine, graines et pommes. Corps en boîtes
  animées (flottants, même shader, u_tint rouge au coup). Banc d'essai hôte : scratchpad/entt/t.cpp.

**How to apply:** les numéros de blocs ne bougent jamais (ajout à la fin de `blocks.h`, de `terracube.py`, de la doc) ; de même les objets (100+, `items.h`).
