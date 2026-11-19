# TerraCube : bac à sable de blocs, scripts et agent IA

TerraCube (`src/apps/terracube`, `/apps/terracube.app`) est un jeu de construction en blocs pour Korin PC : un
monde sans bord fait de blocs d'un mètre, généré à partir d'une graine (prairies, forêts, déserts, neige, montagnes,
mer, grottes, minerais), qu'on parcourt à la première personne, qu'on creuse et où l'on construit. Le rendu passe par
OpenGL ES 3.0 (GlKit : Mesa iris sur le GPU Intel, softpipe sur le processeur sans lui). Tout le contenu est dessiné
par le programme (aucune image) ; aucun nom, ni dessin, ni niveau n'est repris d'un autre jeu.

En option, deux façons de le piloter de l'extérieur, qui passent par la même boucle que le joueur :

| Pour | Comment | Port |
|---|---|---|
| Python, BASIC, tout langage qui ouvre une connexion TCP | protocole texte, une commande par ligne (§ 3) | 4730, 127.0.0.1 (`--api-any` : le réseau) |
| un agent IA (Claude Code, Vibe…) | MCP, transport HTTP « Streamable » sans flux SSE, jeton (§ 4) | 7312 |

## 1. Jouer

| Touche | Effet |
|---|---|
| clic dans la vue | la souris passe au jeu ; Échap la rend |
| ZQSD (WASD) ou flèches, souris | marcher, regarder |
| Espace / Maj / Ctrl (ou R) | sauter (en vol : monter) / s'accroupir, ne pas tomber du bord (en vol : descendre) / courir |
| clic gauche / droit / milieu | casser / poser le bloc choisi / prendre le bloc visé |
| 1 à 9, molette | la case de la barre |
| B | la liste des blocs, pour la case choisie |
| A (Q en QWERTY) | prendre le bloc visé dans la case |
| E | « toucher » le bloc visé (un évènement pour les scripts) |
| F | voler ou non |
| T | un message, ou une commande : `/tp x y z`, `/time 18`, `/fly`, `/seed`, `/spawn`, ou une ligne du protocole (`/world.setBlocks(0,60,0,9,60,9,glass)`) |
| F3, F9 | infos détaillées, plein écran |

Menus : **Partie** (nouveau monde avec nom et graine, ouvrir, sauver, plein écran), **Affichage** (distance de
vue, vol, temps qui passe, infos), **Pilotage** (serveur des scripts, agent IA, où sont les exemples).

Une journée dure 20 minutes. Le monde est sauvé à la fermeture, toutes les minutes s'il a changé, et par Ctrl+S :
`HOME:/terracube/worlds/<nom>/` (`world.ini` : graine, joueur, heure, barre ; un fichier `c.<cx>.<cz>.chk` par
tronçon changé, en plages). `HOME:/terracube/terracube.ini` garde le dernier monde et la distance de vue.

Ligne de commande : `terracube [--world nom] [--seed n] [--distance n] [--no-api] [--api-any] [--mcp [port]]
[--mcp-token t|-] [--selftest [s]]`.

## 2. Le monde

* x vers l'est, **y vers le haut** (0 : le socle, 127 : le haut), z vers le sud ; la mer à y = 48.
* yaw 0 regarde le nord (−z), 90 l'est (+x) ; pitch positif vers le haut.
* Tronçons de 16 × 128 × 16 ; faits autour du joueur (distance 2 à 16 tronçons) **et partout où un script lit ou
  écrit** ; ceux qui ont changé sont sauvés quand ils sont oubliés au loin.
* Les blocs, par numéro ou par nom anglais (le protocole, Python, le BASIC et le MCP prennent les deux). On ajoute
  des blocs à la fin, on n'en retire ni n'en déplace jamais (`blocks.h`).

| n° | nom | n° | nom | n° | nom | n° | nom |
|---|---|---|---|---|---|---|---|
| 0 | air | 10 | log | 20 | crystal_ore | 30 | wool_blue |
| 1 | stone | 11 | leaves | 21 | lamp | 31 | wool_purple |
| 2 | grass | 12 | glass | 22 | tall_grass | 32 | wool_black |
| 3 | dirt | 13 | brick | 23 | flower_red | 33 | gold_block |
| 4 | cobblestone | 14 | sandstone | 24 | flower_yellow | 34 | stone_brick |
| 5 | planks | 15 | snow | 25 | wool_white | 35 | clay |
| 6 | bedrock | 16 | ice | 26 | wool_red | 36 | pine_log |
| 7 | water | 17 | coal_ore | 27 | wool_orange | 37 | pine_leaves |
| 8 | sand | 18 | iron_ore | 28 | wool_yellow | 38 | cactus |
| 9 | gravel | 19 | gold_ore | 29 | wool_green | 39 | bookshelf |

## 3. Le protocole des scripts (port 4730)

Une connexion TCP ; une commande par ligne (`\n`), en texte : `nom(arg1,arg2,…)`. Les commandes qui demandent
quelque chose ont **une ligne de réponse** (`error: …` si elle est refusée) ; les autres n'en ont pas et partent
sans attendre (des milliers par seconde) : leur erreur s'affiche dans la conversation du jeu (« script : … »). Une
commande à réponse passe après toutes celles envoyées avant elle sur la même connexion (`ping()` sert à attendre
que tout soit fait). 16 connexions au plus.

| Commande | Réponse |
|---|---|
| `world.getBlock(x,y,z)` | le numéro |
| `world.setBlock(x,y,z,bloc)` | — |
| `world.setBlocks(x1,y1,z1,x2,y2,z2,bloc)` | — (un pavé plein, coins compris, 1 000 000 de blocs au plus) |
| `world.getBlocks(x1,y1,z1,x2,y2,z2)` | les numéros séparés par des virgules : y, puis z, puis x (262 144 au plus) |
| `world.getHeight(x,z)` | y du plus haut bloc non vide |
| `world.getBlockTypes()` | `0=air,1=stone,…` |
| `world.getTime()` / `world.setTime(heure[,0\|1])` | l'heure (0 à 24) / — (1 : le temps passe) |
| `world.save()` | `ok` |
| `player.getPos()` / `player.setPos(x,y,z)` | `x,y,z` (les pieds, réels) / — |
| `player.getTile()` / `player.setTile(x,y,z)` | `x,y,z` (entiers) / — |
| `player.getRotation()` / `player.setRotation(yaw)` | yaw en degrés / — |
| `player.getPitch()` / `player.setPitch(pitch)` | pitch / — |
| `player.getDirection()` | `dx,dy,dz` (vecteur unité du regard) |
| `player.setFly(0\|1)` | — |
| `chat.post(texte)` | — (le texte jusqu'à la dernière parenthèse, virgules comprises) |
| `events.blocks()` | `genre,x,y,z,face,bloc\|…` : 1 cassé, 2 posé, 3 touché (E) ; face 0 +x, 1 −x, 2 +y, 3 −y, 4 +z, 5 −z ; la liste est vidée |
| `events.clear()` | — |
| `game.info()` | `TerraCube 1.0.0,<monde>,<graine>` |
| `ping()` | `pong` |

### Python

`/apps/terracube.app/python/terracube.py`, sans dépendance (exemples dans `python/examples` : `house.py`,
`pyramid.py`, `rainbow.py`, `golden_touch.py` (les évènements), `snake_walk.py` (suivre le joueur)) :

```python
import sys; sys.path.insert(0, "/apps/terracube.app/python")
from terracube import TerraCube, Block

tc = TerraCube()                       # 127.0.0.1:4730 ; TERRACUBE_HOST, TERRACUBE_PORT pour un autre
x, y, z = tc.player.tile()
tc.fill(x + 2, y, z - 2, x + 6, y + 4, z + 2, "glass")
tc.set_block(x + 4, y + 2, z, Block.LAMP)
print(tc.get_height(x, z), tc.events())
tc.post("Fait !")
```

Le module marche aussi hors de Korin (un PC qui joint le jeu lancé avec `--api-any`).

### BASIC

Le BASIC de Korin (`basic`, QBasic) passe par NetKit : `#import NetKit`, `net_socket`, `net_connect`, `net_send`,
`net_recv`. Il n'a pas d'inclusion : `/apps/terracube.app/basic/terracube.bas` est le modèle, ses SUB et FUNCTION
(`TcConnect`, `TcSetBlock`, `TcFill`, `TcGetBlock`, `TcHeight`, `TcPost`, `TcPlayerTile`, `TcTeleport`, `TcAsk$`)
sont à recopier ; `pyramid.bas` et `tower.bas` les utilisent. `basic /apps/terracube.app/basic/pyramid.bas`.

## 4. L'agent IA (MCP, port 7312)

Pilotage > **Piloter par un agent IA (MCP)…** démarre le serveur et met dans le presse-papiers la commande pour
Claude Code, sur la machine où il tourne :

```sh
claude mcp add --transport http terracube http://<adresse du PC>:7312/mcp --header "Authorization: Bearer <jeton>"
```

Le jeton est gardé dans le coffre à secrets de Korin (sinon `HOME:/terracube/mcp.txt`) ; une requête de
navigateur (en-tête `Origin`) est refusée. `terracube --mcp` le démarre dès le lancement. Le transport est celui
de Forge (`mcp.cpp`).

| Outil | Ce qu'il fait |
|---|---|
| `get_game_info` | monde, graine, heure, joueur, conventions des coordonnées (à appeler d'abord) |
| `get_player` | position, case, yaw/pitch, regard, le bloc visé et où un bloc serait posé |
| `teleport`, `look` | déplacer le joueur ; tourner la tête (yaw/pitch, ou `at` : un point à regarder) |
| `get_block`, `get_blocks` | un bloc ; les blocs d'un pavé (32 768 au plus ; non vides en `[x,y,z,id]`, ou en couches) |
| `get_heightmap` | relief d'une zone (128 × 128 colonnes au plus) : hauteur et bloc du dessus |
| `list_block_types` | les blocs : numéro, nom, solide, transparent |
| `set_block`, `set_blocks`, `fill` | poser un bloc ; une liste (200 000 au plus) ; un pavé plein ou creux |
| `post_message` | un message au joueur |
| `set_time` | l'heure et si le temps passe |
| `screenshot` | une image PNG de la vue du joueur, ou d'un autre point de vue (`eye` + `look_at` ou yaw/pitch) sans bouger le joueur |
| `get_events` | les blocs cassés, posés, touchés depuis le dernier appel |
| `save_world` | sauver |

## 5. Construire et essayer

```sh
sh tools/mesa/glkit-pc-from-pkg.sh   # sans ~/armpi-mesa : libglkit.a tiré du glkit.so du paquet pc
sh tools/pc/apps.sh terracube        # build/pc/apps/sdroot/apps/terracube.app
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_apps.sh
```

`terracube --selftest [s]` vérifie le protocole, le MCP (dont une capture), la sauvegarde et la relecture d'un
tronçon, et l'image rendue (le ciel en haut, le sol en bas) ; il écrit `HOME:/terracube/selftest.png`, puis garde la
fenêtre `s` secondes (ses scripts et son MCP restent ouverts pendant ce temps).

Vérifié sous QEMU q35 (softpipe, 19/11) : `--selftest` 22 sur 22 ; depuis l'hôte par les ports redirigés, `house.py`,
`pyramid.py`, `rainbow.py` et un client MCP (initialize, tools/list, fill, get_blocks, deux captures) ; dans Korin,
`terracube.bas`, `pyramid.bas` et `tower.bas` par la boucle locale. Sous softpipe, une image prend plusieurs
secondes : la distance de vue tombe à 4 tronçons (et le joueur tombe au ralenti, le pas de temps étant borné). Pas encore
essayé sur le ZBook (GPU Intel).

## 6. Fichiers

| Fichier | Rôle |
|---|---|
| `main.cpp` | fenêtre, boucle, entrées, affichage tête haute par UIKit (`ui_surface_overlay`), menus, mondes, jeton du MCP, `--selftest` |
| `world.h`, `world.cpp` | tronçons, génération (bruit de gradient, biomes, grottes, minerais, arbres), sauvegarde, lancer de rayon |
| `mesh.cpp` | maillage : faces visibles, occlusion ambiante, lumière du ciel lissée, eau, plantes en croix |
| `render.h`, `render.cpp` | OpenGL ES 3 : shaders, ciel et soleil, tronçons (tronc de vue), eau animée (vagues de la surface dans le vertex shader, texture qui glisse et reflets dans le fragment shader), contour visé, barre de blocs |
| `textures.cpp` | l'atlas de 16 × 16 tuiles, dessiné au départ |
| `player.h`, `player.cpp` | marche, saut, nage, vol, collisions |
| `api.cpp` | protocole des scripts et son serveur (un thread par connexion, une file vidée par la boucle) |
| `mcp.h`, `mcp.cpp`, `mcptools.cpp` | transport MCP (repris de Forge) et outils |
| `data/python`, `data/basic` | module Python et exemples ; exemples BASIC |
