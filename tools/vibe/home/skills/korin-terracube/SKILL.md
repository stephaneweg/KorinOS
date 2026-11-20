---
name: korin-terracube
description: Piloter TerraCube, le bac à sable de blocs en 3D de Korin (sur le PC), par son serveur MCP ou par un script Python - construire (maisons, pyramides, paysages), poser et faire marcher des circuits de cuivre (fil, leviers, torches-inverseurs, répéteurs, comparateurs, observateurs, pistons et pistons collants, colle, lampes, blocs musicaux, dynamite), faire couler l'eau et la lave, les créatures (poules, cochons, loups apprivoisés qui gardent ou suivent, rôdeurs et araignées la nuit), la santé et la faim, changer l'heure, la météo, le mode (créatif, survie), donner et fabriquer des objets, vérifier par capture. À utiliser dès qu'on parle de TerraCube, de construire dans le jeu, de blocs, de circuits, de « redstone », de portes logiques, d'horloge, de piston, de machine, d'animaux, de ferme, de loup ou de monstres dans le jeu.
user-invocable: true
---

# Piloter TerraCube

TerraCube est le jeu de blocs de Korin (`/apps/terracube.app`, sur le PC x86-64 seulement : `ls /apps/terracube.app`
le dit). Un monde sans fin de cubes, des modes créatif et survie, l'eau et la lave qui coulent, des **circuits de
cuivre** qui marchent comme ceux des jeux du genre. Deux façons de le piloter, au choix :

* **le MCP** (port 7312) : des outils `terracube_*` (poser des blocs, des listes de blocs, des pavés, lire, se servir
  d'un levier, capture d'écran…) ; le mieux pour toi ;
* **un script Python** (port 4730) : `import terracube`, pour une construction longue ou calculée, ou un programme
  qui réagit au joueur (les évènements).

## 1. Brancher le jeu

**Le jeu doit tourner.** L'utilisateur le lance (le menu des apps), ou toi, **sans bloquer le shell** :

```sh
(/apps/terracube.app/main --mcp --mcp-token JETON >/dev/null 2>&1 &)
```

`JETON` : une suite au hasard d'au moins 32 caractères hexadécimaux, à toi. Sinon le jeu garde le sien dans le coffre
à secrets (que tu ne lis pas) : l'utilisateur le voit dans *Pilotage > Piloter par un agent IA*, qui ouvre aussi le
serveur sur un jeu déjà lancé. Sans coffre, il est dans `HOME:/terracube/mcp.txt` (`token = …`). Options utiles :
`--world nom`, `--seed n` (un monde neuf), `--api-any` (les scripts depuis le réseau).

**Le MCP** : dans le `config.toml` de l'agent (`~/.local/vibe/config.toml` pour Vibe,
`~/.local/claude-acp/config.toml` pour Claude, `~/.local/mai-acp/config.toml` pour MAI) ; le demander d'abord à l'utilisateur, ne pas toucher au reste du fichier :

```toml
[[mcp_servers]]
name = "terracube"
transport = "streamable-http"
url = "http://127.0.0.1:7312/mcp"
headers = { Authorization = "Bearer JETON" }
startup_timeout_sec = 30
tool_timeout_sec = 300
```

Puis une nouvelle séance : les outils s'appellent `terracube_<outil>`. Avec Claude, chaque outil qui change le monde
demande l'accord de l'utilisateur (mode « ask »), sauf s'il l'a donné pour la séance.

**Python** (le serveur des scripts est ouvert par défaut, *Pilotage > Scripts*) :

```python
import sys; sys.path.insert(0, "/apps/terracube.app/python")
from terracube import TerraCube, Block, Item, Facing
tc = TerraCube()                      # 127.0.0.1:4730 (TERRACUBE_HOST, TERRACUBE_PORT)
x, y, z = tc.player.tile()
tc.set_block(x + 2, y, z, Block.STONE)
```

Exemples à lire et à lancer : `/apps/terracube.app/python/examples/` (`house.py`, `pyramid.py`, `circuits.py`,
`flying_machine.py`…).
Un script se lance dans le shell (`python3 fichier.py`) ; le jeu reste ouvert.

## 2. Les outils MCP

| Outil | Pour |
|---|---|
| `get_game_info`, `get_player` | le monde, la version, le mode, l'heure ; où est le joueur et où il regarde |
| `teleport`, `look` | déplacer le joueur, tourner sa tête (`at = [x, y, z]`) |
| `get_block`, `get_blocks`, `get_heightmap`, `list_block_types` | lire un bloc (avec `state` et `power` pour un liquide ou un circuit), un pavé (`states: true` : l'état, la puissance et l'état en clair de chaque bloc, **tout un circuit en un appel**), les hauteurs du terrain, la liste des blocs |
| `test_circuit` | **essayer un mécanisme en un appel** : un clic droit (`use` : [x, y, z]), puis `ticks` pas de simulation tout de suite ; rend chaque changement de la boîte `from`..`to`, pas par pas (le pas 0 : le clic) |
| `explain_block` | **pourquoi** un fil, un piston, une lampe, un répéteur, une torche, un bloc ne réagit pas : son état, ce que lui donne chaque voisin, où un fil se relie et pointe, si un piston pourrait pousser |
| `set_block`, `set_blocks`, `fill` | poser un bloc (`state`), une liste (`[[x, y, z, bloc, état], …]`, 200 000 au plus : **un montage entier d'un coup**), un pavé plein ou creux (`hollow`), `"air"` efface |
| `use_block` | un clic droit : basculer un levier, appuyer un bouton, changer le délai d'un répéteur… |
| `list_entities`, `spawn_entity`, `remove_entities` | les créatures autour du joueur ; en faire apparaître (`type`, `position`, `count` ; un loup `tamed`, `sitting` ; `baby`) ; en enlever (`id`, `type`, `all`) |
| `run_command` | une commande de la console : `time 18`, `weather rain`, `mode survival`, `give wire 64`, `craft piston`, `tp x y z`, `fly`, `recipes`, `help` |
| `screenshot` | une image de la vue du joueur, ou d'ailleurs (`eye`, `look_at`) sans le bouger : **pour vérifier** |
| `get_events` | ce que le joueur a cassé, posé, touché (E) |
| `get_inventory`, `list_recipes`, `craft` | le mode survie |
| `post_message`, `set_time`, `save_world` | un message à l'écran, l'heure, sauver |

**La règle d'or** : `get_player` d'abord, et construire **près du joueur, devant lui** (relatif à sa case) ; poser
d'un coup avec `set_blocks` ou `fill` ; puis **regarder** (`screenshot` avec `eye` et `look_at` bien placés, à
quelques blocs du montage, un peu au-dessus) ; pour un circuit, **l'essayer** avec `test_circuit` (le levier, puis
ce qui bouge, pas par pas) et, si quelque chose ne réagit pas, `explain_block` sur lui : c'est plus rapide et plus sûr
qu'une suite de `use_block`, `get_block` et de captures.

En Python, les mêmes : `tc.get_states(x1, y1, z1, x2, y2, z2)` (numéro, état, puissance), `tc.test(boîte, pas,
use=(x, y, z))`, `tc.explain(x, y, z)` ; une requête prend une vingtaine de millisecondes.

### Une grande construction (un temple, une ville, un château)

1. **Le site** : `get_player`, puis `get_heightmap` du terrain (128 × 128 au plus) ; le niveau du sol = la médiane
   des hauteurs dont le bloc du dessus est de la terre, de l'herbe, du sable ou de la pierre (pas les arbres).
   Construire à quelques dizaines de blocs du joueur, pas sur lui.
2. **Le terrain** : `fill` d'air au-dessus (les arbres, les collines ; 1 000 000 de blocs au plus par appel), `fill`
   de pierre dessous (les creux, les grottes), puis le dallage.
3. **Les volumes** : de grands `fill` (pleins, puis `hollow` pour une peau d'un autre bloc) ; creuser les salles et
   les couloirs par des `fill` d'air ensuite.
4. **Les détails** : calculés, puis posés par `set_blocks` en lots (frises, escaliers, colonnes, lampes, circuits ;
   200 000 au plus par appel, 50 000 est confortable).
5. **Les mécanismes** (une porte secrète, un piège) : les poser au repos, puis les mettre en marche avec `use_block`
   et vérifier l'état avec `get_block`.
6. **Regarder** : `screenshot` d'ensemble (de loin, en hauteur), puis de près (l'entrée, une salle : l'œil dedans) ;
   corriger ce qui ne va pas. Enfin `teleport` le joueur devant, un `post_message` qui dit quoi visiter, `save_world`.

Un exemple complet, à lire : `/apps/terracube.app/python/examples/temple_mcp.py` (un temple aztèque : le site, la
pyramide à gradins, l'escalier double, les sanctuaires, un tunnel, une salle, une porte secrète à piston, la chambre
du trésor, les captures).

Le dedans d'une construction est dans le noir (pas de ciel) : les créatures de la nuit peuvent y naître ;
`run_command` `mobs off` pendant le chantier, le dire à l'utilisateur.

## 3. Le monde

* x vers l'est, **y vers le haut** (0 à 127, la mer à 48), z vers le sud ; yaw 0 regarde le nord (−z), 90 l'est.
* Un bloc par son **nom anglais** ou son numéro. Les blocs : `air stone grass dirt cobblestone planks bedrock water
  sand gravel log leaves glass brick sandstone snow ice coal_ore iron_ore gold_ore crystal_ore lamp tall_grass
  flower_red flower_yellow wool_white wool_red wool_orange wool_yellow wool_green wool_blue wool_purple wool_black
  gold_block stone_brick clay pine_log pine_leaves cactus bookshelf workbench furnace` (0 à 41), puis `lava obsidian
  copper_ore wire copper_block copper_torch lever stone_button wood_button stone_plate wood_plate repeater comparator
  observer piston sticky_piston piston_head signal_lamp note_block daylight_sensor glue_block tnt` (42 à 63).
* **L'état** d'un bloc (`state`) : une direction `east` (+x) `west` `up` `down` `south` (+z) `north`, plus `+on`
  (allumé), ou un nombre (direction 0 à 5 : est, ouest, haut, bas, sud, nord ; + 8 allumé). Le répéteur : + 8 × (délai
  − 1) (`repeater` délai 4 vers l'est : `24` = 0 + 3 × 8). Le bloc musical : la note, 0 à 24.
* Un tronçon (16 × 128 × 16) se fait là où tu écris : on peut construire loin. Ne pas noyer le joueur : vérifier que
  la place est libre (`get_blocks`), ou dégager d'abord (`fill` d'air).

## 4. Les circuits de cuivre

**La puissance** va de 0 à 15. Une source donne 15 ; le **fil** (`wire`, la poudre de cuivre posée sur un bloc
plein) perd 1 par bloc : au-delà de 15 blocs, un **répéteur** redonne 15. Le fil se relie seul à ses voisins (fil,
composants, la marche d'un bloc vers le haut ou le bas) ; seul, il pointe partout ; relié d'un seul côté, tout droit ;
sinon vers ses liaisons. Il alimente le bloc **sous** lui et les blocs **vers lesquels il pointe**.

**Les blocs pleins conduisent** (pierre, laine…, pas le verre, la colle, les pistons, l'observateur, le bloc de
cuivre) :
* alimenté **fort** : par un répéteur, un comparateur ou un observateur qui sort dedans, une torche **dessous**, un
  levier ou un bouton **fixé à lui**, une plaque posée dessus ; il alimente alors tout ce qui le touche, **fil
  compris** ;
* alimenté **faible** : par du fil qui pointe dedans ou est posé dessus ; il n'alimente alors que les composants
  (lampe, piston, répéteur, torche fixée à lui, bloc musical, dynamite), **pas le fil**.

**Le temps** : 20 pas par seconde ; un « temps » de circuit = 2 pas (0,1 s).

**Les règles exactes** (celles de `sim.cpp` ; inutile de lire les sources) :
* le fil ne prend d'un bloc plein que sa puissance **forte** ; d'un composant, ce que ce composant donne vers lui ;
* un fil se relie à : un fil voisin ; la marche d'un bloc plus bas (si le bloc d'à côté n'est pas opaque) ; la marche
  d'un bloc plus haut (si rien d'opaque n'est au-dessus de lui) ; un levier, un bouton, une plaque, une torche, un bloc
  de cuivre, un capteur de jour ; un répéteur ou un comparateur **dans son axe** ; un observateur **par son dos** ;
* un composant (lampe, piston, bloc musical, dynamite) prend de chacun de ses six voisins : d'un bloc plein, sa
  puissance (forte ou faible) ; d'un composant, ce qu'il donne vers lui ; **un piston rien par sa face** ;
* le répéteur ne lit que son dos ; le comparateur son arrière (un bloc plein ou un composant) et, sur ses côtés,
  seulement un fil, un répéteur, un comparateur ou un bloc de cuivre ;
* la torche s'éteint quand le bloc qui la porte est alimenté (fort ou faible) ; allumée, elle donne 15 à tous ses
  voisins sauf son support, et alimente fort le bloc au-dessus d'elle ;
* rien ne tombe (le sable reste en l'air) ; le levier, le bouton, la plaque, le fil, le répéteur, le comparateur, la
  torche et le capteur ne sont pas solides (on passe au travers) ;
* `get_blocks` et `world.getStates` rangent les blocs par y, puis z, puis x (x le plus rapide).

**Le codage des états** (`state`, un octet ; les directions : 0 est, 1 ouest, 2 haut, 3 bas, 4 sud, 5 nord) :

| Bloc | État |
|---|---|
| `wire` | la puissance, 0 à 15 |
| `lever`, `stone_button`, `wood_button` | la direction du support + 8 (abaissé, enfoncé) |
| `copper_torch` | la direction du support (jamais 2) + 8 (**éteinte**) |
| `stone_plate`, `wood_plate`, `signal_lamp`, `tnt` | 8 : enfoncée, allumée |
| `repeater` | la sortie + 8 × (délai − 1) + 32 (allumé) + 64 (bloqué) |
| `comparator` | la sortie + 8 (soustraction) ; la puissance de sortie × 16 (bits 4 à 7) |
| `observer` | la direction regardée + 8 (l'impulsion) |
| `piston`, `sticky_piston` | la direction de la poussée + 8 (sorti) ; `piston_head` : sa direction + 8 (collante) |
| `note_block` | la note (0 à 24) + 32 (alimenté) |
| `daylight_sensor` | 8 (inversé) ; la puissance × 16 |
| `water`, `lava` | 0 source, 1 à 7 le niveau, 8 qui tombe |

Pour un montage au repos, poser les pistons **rentrés** et les composants éteints : la simulation fait le reste dès
que la source est posée (poser la source, le levier, en dernier).

| Bloc | Ce qu'il fait | Son état (`state`) |
|---|---|---|
| `lever` | source 15 tant qu'il est abaissé ; alimente fort le bloc qui le porte | la direction **de son support** (`down` : posé au sol ; `west` : fixé au bloc à l'ouest) ; `+on` abaissé |
| `stone_button`, `wood_button` | une impulsion de 10 / 15 temps | la direction de son support |
| `stone_plate`, `wood_plate` | 15 tant que le joueur est dessus | — (sur un bloc) |
| `copper_block` | source 15, toujours, tout autour | — |
| `copper_torch` | **l'inverseur** : allumée (15, tout autour sauf son support ; fort vers le haut) tant que son support n'est pas alimenté ; s'éteint 1 temps après. Plus de 8 changements en 3 s : grillée 8 s | la direction de son support : `down` (au sol) ou est, ouest, sud, nord (au mur) ; jamais `up` |
| `repeater` | redonne 15 **vers l'avant** après 1 à 4 temps ; allonge une impulsion courte ; **bloqué** (garde son état) si un répéteur ou comparateur allumé sort dans son côté ; n'écoute que **son arrière** | sa **sortie** (est, ouest, sud, nord) + 8 × (délai − 1) ; sur un bloc |
| `comparator` | sortie = l'arrière si ≥ les côtés, sinon 0 ; `+on` : soustraction (arrière − le plus fort côté). Les côtés : fil, répéteur, comparateur, bloc de cuivre | sa sortie ; `+on` soustraction |
| `observer` | une impulsion de 1 temps par **son dos** quand le bloc **qu'il regarde** change (posé, cassé, changé d'état), ou quand un piston le déplace | la direction **qu'il regarde** (son dos est à l'opposé) |
| `piston` | alimenté (par tout côté sauf sa face) : pousse jusqu'à **12 blocs** ; casse ce qui est fragile (fil, torches, plantes, liquides) ; ne pousse ni `bedrock`, ni `obsidian`, ni `furnace`, ni un piston sorti | la direction **où il pousse** ; `+on` sorti (laisser à `off` : il sort seul s'il est alimenté) |
| `sticky_piston` | pareil, et **ramène** le bloc collé à sa tête en rentrant | idem |
| `glue_block` | poussé ou tiré, il **entraîne les blocs qui le touchent** (de tous côtés) : les machines | — |
| `signal_lamp` | allumée quand alimentée ; s'éteint 2 temps après | — |
| `note_block` | une note à chaque front montant ; l'instrument selon le bloc **dessous** (bois : basse, pierre : grosse caisse, sable : caisse claire, verre : clic, or/fer/cuivre : cloche, laine : guitare, neige : carillon, terre et autres : harpe) | la note, 0 à 24 (0 : fa dièse ; 6 : do) |
| `daylight_sensor` | sa puissance suit le jour (0 la nuit, 15 à midi) ; `+on` : inversé (la nuit) | `on` inversé |
| `tnt` | allumée par un signal : 4 s puis explosion (rayon 3,5) ; une dynamite prise dedans part aussi | — |

### Des montages essayés (coordonnées relatives à un point (x, y, z), le sol en y − 1 en pierre)

* **Lampe au levier** : `lever` `down` en (x, y, z), `wire` en x+1…x+3, `signal_lamp` en x+4. `use_block` sur le levier.
* **NON** (inverseur) : un bloc plein, une `copper_torch` fixée à un de ses côtés ; alimenter le bloc éteint la torche.
* **OU** : deux fils qui se rejoignent.
* **ET** : trois blocs `stone` en (x, y, z), (x+1, y, z), (x+2, y, z) ; une torche `down` sur x et sur x+2 (y+1) ; du
  `wire` sur x+1 (y+1) ; les entrées : un `lever` contre l'arrière de x et de x+2 (en z−1, état `south`) ; la sortie :
  une torche en (x+1, y, z+1), état `north` (fixée à x+1), puis la lampe en (x+1, y, z+2). La lampe n'est allumée
  que si les deux leviers le sont.
* **Horloge lente** : un bloc B en (x, y, z) ; une torche contre son côté est (x+1, y, z), état `west` ; du fil en
  (x+1, y, z+1), (x+1, y, z+2), (x, y, z+2) ; un `repeater` en (x, y, z+1) **vers le nord** (vers B), délai 4 (état
  `29` = nord 5 + 24) ; une lampe en (x+1, y, z−1) clignote (≈ 1 s). Le délai du répéteur règle la vitesse.
* **Horloge rapide** : deux `observer` face à face (l'un `east`, l'autre `west`) : une impulsion tous les 2 temps
  derrière chacun (trop rapide pour éteindre une lampe ; bon pour un piston ou un bloc musical).
* **Porte à pistons** : deux `sticky_piston` empilés, état vers la porte (`east`), un bloc devant chacun ; un bloc
  plein derrière eux avec un `lever` posé dessus (`down`) : le levier alimente fort son bloc, qui alimente les deux
  pistons. Basculer : les blocs avancent ; rebasculer : ils reviennent.
* **Arpège** : un `stone_button` (`down`), puis en alternance `repeater` (`east`, délai 2 : `8`) et `note_block` (sur
  de la terre, notes 3, 7, 10, 15…) : le bloc musical, plein, alimenté fort par le répéteur, alimente le répéteur
  suivant.
* **Machine qui avance seule** (vers l'est, **en l'air**, 8 blocs) : en y+1 : `glue_block` en x−1 et x ; en y :
  `piston` `east` en x ; `glue_block` en x+1 et x+2 ; en y+1 : `sticky_piston` `west` en x+1, `observer` `east` en
  x+2 ; en y : `observer` `west` en x−1. Le départ : poser puis enlever un bloc en (x−2, y). Elle avance d'environ 2,5
  blocs par seconde et s'arrête au premier obstacle. (Un observateur **déplacé** par un piston envoie une impulsion :
  c'est ce qui la fait repartir.) Pour un autre sens, tourner toutes les directions ensemble.
* **Escalier caché dans le sol** (exemple complet : `python/examples/secret_stairs.py`, essayé) : marches de 2 de
  large qui descendent (marche k en (0..1, y0 − k, z k)) sous des dalles de **sable** (0..1, y0, k) ; de chaque côté
  un `sticky_piston` (x −2 vers l'est, x 3 vers l'ouest) qui, **sorti**, tient la dalle sur l'escalier et, **rentré**,
  la tire sous la rampe ; sous chaque piston un bloc de pierre, derrière lui un `repeater` de **délai k** qui
  l'alimente fort (la cascade : la marche du haut d'abord, 0,1 s d'écart) ; les répéteurs pris sur un bus de fil
  (x −4 et x 5) ; la commande : une `copper_torch` (l'inverseur) contre le bloc qui porte le levier, enfermée sous un
  bloc, qui alimente le bus ; levier baissé : fermé, levier levé : ouvert ; tout le circuit en y0 − 1, des rampes en
  y0 + 1 cachent pistons et têtes. Pour deux leviers (en haut et en bas de l'escalier), deux comparateurs en
  soustraction font un va-et-vient (« A et pas B » ou « B et pas A »).
* **Pistons empilés** (un ascenseur) : un piston pousse ceux qui sont au-dessus de lui s'ils sont rentrés (12 blocs
  au plus) ; il faut alimenter chaque étage **là où il arrive**. À la descente, un piston collant ne ramène que le
  bloc collé à sa tête, et rien ne tombe : redescendre du haut vers le bas, et prévoir de quoi ramener chaque bloc.
* **Lampe de nuit** : `daylight_sensor` état `on` (inversé), du fil, une lampe.
* **Ouvrir au passage** : une `stone_plate` devant la porte à pistons, reliée par du fil au bloc derrière les pistons.

### Les pièges

* Le fil, les plaques, les répéteurs et comparateurs veulent un bloc **plein dessous** ; la torche, le levier et le
  bouton un bloc **plein et opaque** à leur support : sinon ils ne tiennent pas (ils tombent).
* Du fil qui longe un bloc ne l'alimente que s'il **pointe** dedans ; une ligne droite pointe dans son axe.
* Un bloc alimenté faible (par du fil) **n'alimente pas** le fil d'à côté : passer par un répéteur.
* Un répéteur n'écoute que son arrière et ne sort que devant : vérifier sa direction (`get_block`, `state`).
* La colle colle **à tout**, le sol compris : une machine à colle se construit en l'air, sinon le piston est trop
  chargé (plus de 12 blocs) et ne bouge pas.
* Un piston ne s'alimente pas par sa face ; une torche ne s'alimente pas elle-même par son support.
* **La méthode** quand quelque chose ne marche pas : `test_circuit` (qu'est-ce qui bouge, et quand ?), puis
  `explain_block` sur le premier composant qui ne réagit pas ; corriger, réessayer. Construire d'abord un essai **dans
  le ciel**, loin de tout (pas d'eau, pas de joueur), le défaire ensuite.
* Ce qui n'existe pas (contrairement à d'autres jeux du genre) : le « crachat » d'un piston collant sur une impulsion
  très courte, la quasi-connectivité (un piston alimenté par le bloc au-dessus de lui), les blocs qui tombent.

## 5. Les liquides

* `water` : une source s'étale de 7 blocs, tombe, va vers la pente la plus proche ; un écoulement sans source tarit ;
  deux sources côte à côte au-dessus d'un sol font une source.
* `lava` : coule de 3 blocs, lentement (1,5 s par bloc) ; brûle en survie.
* Lave (source) touchée par l'eau → `obsidian` ; un écoulement de lave → `cobblestone` ; lave qui tombe dans l'eau →
  `stone`. L'eau et la lave cassent le fil, les torches, les leviers… : les éloigner des circuits.
* Poser `water` ou `lava` (une source) les fait couler tout de suite ; `fill` d'air les arrête.

## 6. Les créatures

| `type` | Où, quand | Comportement | Laisse |
|---|---|---|---|
| `chicken` | l'herbe | flâne ; suit les **graines** ; nourrie de graines, avec une autre : un poussin | poulet cru, plumes |
| `pig` | l'herbe | pareil avec les **pommes** | porc cru |
| `wolf` | la neige, l'herbe | sauvage : neutre, attaque si on le frappe ; **des os** l'apprivoisent (un sur trois) ; apprivoisé : **assis, il garde sa place** (attaque toute créature de la nuit à moins de 12 blocs, puis revient) ; **debout, il suit** le joueur et le défend | — |
| `rodeur` | la nuit dehors, le noir des grottes | marche vers le joueur et frappe (3) ; **brûle au soleil** | os |
| `spider` | la nuit, le noir | rapide, grimpe aux murs, bondit (2) ; le jour dehors : tranquille | fil d'araignée |

* Elles ne blessent le joueur **qu'en mode survie** ; `run_command` `mobs off` : plus de créatures de la nuit
  (paisible), `mobs on` les rend ; `kill monsters`, `kill all`.
* Un loup qui garde : `spawn_entity type=wolf tamed=true sitting=true position=[x, y, z]` (sa place est là où il
  naît). Une ferme : un enclos de 2 blocs de haut (elles sautent 1 bloc), des animaux dedans, un loup assis à l'entrée
  (exemple Python : `farm.py`).
* La santé et la faim du joueur : `get_player` (`health`, `hunger`, sur 20) ; `heal`, `feed` dans la console.
* Une pièce éclairée et fermée est sûre : les créatures de la nuit n'apparaissent que dans le noir (sans ciel au-dessus,
  ou la nuit dehors), à 18 blocs au moins du joueur.

## 7. Le mode survie

`run_command` : `mode survival` / `mode creative`, `give <objet> [n]`, `craft <recette> [n]` (l'établi ou le four à
moins de 4 blocs), `recipes`, `inv`, `clear`. Le cuivre (`copper_ore`, sous y = 20, pioche en fer) donne 4
`wire` (la poudre). Les recettes des circuits : `copper_torch` (poudre sur bâton), `lever` (bâton sur pavé),
`repeater` (`TDT` / `SSS`), `comparator` (`·T·` / `TKT` / `SSS`, K : cristal), `observer` (`CCC` / `DDK` / `CCC`),
`piston` (`PPP` / `CIC` / `CDC`), `sticky_piston` (résine sur piston ; résine : un cactus au four), `glue_block`
(9 résines), `signal_lamp` (4 poudres autour d'une lampe), `note_block` (planches autour d'une poudre), `tnt`
(charbon et sable en damier), `bucket` (`I·I` / `·I·`), les épées (`M` / `M` / `S`), `cooked_chicken`,
`cooked_pork` (au four), `wool_from_string` (4 fils).

La documentation complète : `/src/docs/TERRACUBE.md` si les sources sont là, ou l'aide du jeu (*Aide*).
