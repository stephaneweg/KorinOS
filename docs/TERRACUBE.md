# TerraCube : bac à sable de blocs, scripts et agent IA

TerraCube (`src/apps/terracube`, `/apps/terracube.app`) est un jeu de construction en blocs pour Korin PC : un
monde sans bord fait de blocs d'un mètre, généré à partir d'une graine (prairies, forêts, déserts, neige, montagnes,
mer, grottes, minerais), qu'on parcourt à la première personne, qu'on creuse et où l'on construit. Deux modes : créatif
(tous les blocs, tout de suite, le vol) et survie (on mine, on ramasse, on fabrique ses outils, § 2 bis). Une console
de commandes règle l'heure, la météo (pluie, neige, orage), le mode, etc. (§ 2 ter). Le rendu passe par
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
| clic gauche / droit / milieu | casser (survie : tenir) / poser le bloc choisi / prendre le bloc visé |
| 1 à 9, molette | la case de la barre |
| B | créatif : la liste rapide des blocs, pour la case choisie ; survie : l'inventaire |
| I ou C | survie : inventaire et fabrication (aussi : clic droit sur un établi ou un four ; Maj + clic droit pose contre eux) ; créatif : le catalogue de tous les objets (blocs, outils, épées, nourriture, os, seaux, circuits), à glisser vers la barre (Maj + clic : dans la case choisie ; 1 à 9 : dans cette case ; lâché ailleurs : il disparaît) |
| A (Q en QWERTY) | prendre le bloc visé dans la case |
| E | « toucher » le bloc visé (un évènement pour les scripts) |
| F | voler ou non |
| T, / ou ² | la console de commandes (§ 2 ter) ; Entrée exécute, ↑ ↓ l'historique, Échap ferme |
| F3, F9 | infos détaillées, plein écran |

On voit ce qu'on tient, en bas à droite : un bloc en petit cube, un outil, une arme ou de la nourriture en relief, la
main nue sinon ; chaque coup (casser, frapper, poser, se servir, manger) fait un geste, et le minage frappe en
cadence ; le bloc qu'on casse se fissure, il en sort des éclats ; une créature frappée laisse voler des bribes de
sa peau, une créature morte disparaît dans une bouffée de fumée.

Menus : **Partie** (nouveau monde avec nom, graine et mode, ouvrir, sauver, mode survie, inventaire, console, plein
écran), **Affichage** (distance de
vue, vol, temps qui passe, infos), **Pilotage** (serveur des scripts, agent IA, où sont les exemples).

Une journée dure 20 minutes. Le monde est sauvé à la fermeture, toutes les minutes s'il a changé, et par Ctrl+S :
`HOME:/terracube/worlds/<nom>/` (`world.ini` : graine, joueur, heure, barre, mode, météo, inventaire, chauffe du
four ; un fichier `c.<cx>.<cz>.chk` par
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
| | | | | | | 40 | workbench |
| | | | | | | 41 | furnace |

Depuis la 1.3 (les liquides et les circuits, § 2 quinquies et § 2 sexies) :

| n° | nom | n° | nom | n° | nom | n° | nom |
|---|---|---|---|---|---|---|---|
| 42 | lava | 48 | lever | 54 | comparator | 60 | note_block |
| 43 | obsidian | 49 | stone_button | 55 | observer | 61 | daylight_sensor |
| 44 | copper_ore | 50 | wood_button | 56 | piston | 62 | glue_block |
| 45 | wire | 51 | stone_plate | 57 | sticky_piston | 63 | tnt |
| 46 | copper_block | 52 | wood_plate | 58 | piston_head | | |
| 47 | copper_torch | 53 | repeater | 59 | signal_lamp | | |

Un bloc a aussi un **état** (un octet, sauvé avec lui) : le niveau d'un liquide, la direction d'un composant,
allumé ou non, la puissance d'un fil. Les directions : 0 est (+x), 1 ouest (−x), 2 haut, 3 bas, 4 sud (+z), 5 nord
(−z) ; + 8 : allumé (levier abaissé, bouton enfoncé, piston sorti…). Les scripts l'écrivent en nombre ou en mots
(`east`, `down+on`).

## 2 bis. Le mode survie

* Les blocs se cassent **en tenant le clic** : le temps dépend du bloc et de l'outil en main (une barre sous le
  viseur). Ce qui tombe va dans l'**inventaire** (36 cases, les 9 premières sont la barre, avec leur nombre). Poser
  un bloc en consomme un. Pas de vol.
* Les outils : **pioche** (pierre, minerais, briques), **hache** (bois), **pelle** (terre, sable, gravier, neige), en
  bois, pierre, fer ou cristal (de plus en plus rapides et solides ; leur usure est sous leur icône). Sans pioche,
  la pierre ne donne rien ; le fer demande une pioche en pierre ; l'or et le cristal une pioche en fer.
* Ce qui tombe : la pierre donne des pavés, l'herbe de la terre, le charbon du charbon, le cristal du cristal, les
  feuilles parfois un bâton ; le verre et la glace rien.
* **La fabrication** (I, ou clic droit sur un établi) : un panneau **dessiné dans le jeu**, par-dessus la vue
  assombrie (pas une fenêtre) : en haut la table, une **grille 2 × 2 à la main, 3 × 3 à l'établi** (posé à moins de 4
  blocs) et son résultat, le four à droite s'il y en a un près ; en bas l'inventaire (le sac, puis la barre). Ce qui
  sort dépend des objets **et de leur agencement** : la forme peut être posée n'importe où dans la grille, et
  retournée de gauche à droite. On **glisse-dépose** les objets de l'inventaire vers la grille (bouton gauche : toute
  la pile ; bouton droit : un seul objet), et le résultat vers l'inventaire ; lâché ailleurs, l'objet revient. Un
  clic sans glisser prend l'objet, un second clic le pose. **Maj + clic** sur le résultat en fabrique le plus
  possible ; Maj + clic dans l'inventaire passe un objet du sac à la barre et inversement ; **1 à 9** sur une case
  l'échange avec cette case de la barre. **R** ouvre le livre des recettes (la molette le fait défiler). Échap, I
  ou E ferme ; ce qui reste dans la grille revient dans l'inventaire.
* **Le four** (posé à moins de 4 blocs) a sa place dans la même fenêtre : une case d'entrée, une case de résultat ;
  il brûle un combustible pris dans l'inventaire, un point de chauffe par objet cuit : charbon 8, tronc 4, planches
  2, bâton 1.

Les formes (une ligne par rangée, `·` : une case vide ; les recettes de `items.cpp`) :

| Fait | Forme | Grille |
|---|---|---|
| 4 planches | un tronc, n'importe où | 2 × 2 |
| 4 bâtons | `P` / `P` (deux planches l'une sur l'autre) | 2 × 2 |
| établi | `PP` / `PP` | 2 × 2 |
| pioche | `MMM` / `·S·` / `·S·` (M : planches, pavés, lingots de fer ou cristaux ; S : bâton) | 3 × 3 |
| hache | `MM` / `MS` / `·S` (ou retournée) | 3 × 3 |
| pelle | `M` / `S` / `S` | 3 × 3 |
| four | `CCC` / `C·C` / `CCC` (pavés) | 3 × 3 |
| 2 lampes | `·G·` / `GKG` / `·I·` (verre, charbon, lingot de fer) | 3 × 3 |
| 4 pierres taillées · 2 grès · laine blanche | `SS` / `SS` en pierre · en sable · en hautes herbes | 2 × 2 |
| bibliothèque | `PPP` / `KKK` / `PPP` (planches, charbon) | 3 × 3 |
| bloc d'or | 9 lingots d'or | 3 × 3 |
| laines de couleur | sans forme : laine blanche + fleur rouge (ou jaune), + charbon, + 2 feuilles ; rouge + jaune → 2 orange ; 4 blanches + cristal → 4 bleues ; rouge + bleue → 2 violettes | 2 × 2 ou 3 × 3 |
| au four | minerai de fer ou d'or → lingot, sable → verre, pavé → pierre, argile → briques, tronc → charbon | four |

La console (`craft <recette>`), les scripts (`player.craft`) et l'agent (`craft`) fabriquent directement depuis
l'inventaire, sans poser la forme (la même recette, les mêmes ingrédients, l'établi ou le four à côté).

Les objets qui ne sont pas des blocs (scripts, MCP, console) : `stick` 100, `coal` 101, `iron_ingot` 102,
`gold_ingot` 103, `crystal` 104, `wood_pickaxe` 105 à `crystal_pickaxe` 108, `wood_axe` 109 à `crystal_axe` 112,
`wood_shovel` 113 à `crystal_shovel` 116, `bucket` 117, `water_bucket` 118, `lava_bucket` 119, `resin` 120, `seeds`
121, `apple` 122, `raw_chicken` 123, `cooked_chicken` 124, `raw_pork` 125, `cooked_pork` 126, `feather` 127, `bone`
128, `string` 129, `wood_sword` 130 à `crystal_sword` 133. Créatif et survie ont chacun leur barre ; passer de l'un à l'autre la garde.

## 2 quinquies. Les liquides

* **L'eau coule** : une source s'étale de 7 blocs sur un sol plat, tombe dans les trous, va d'abord vers la pente
  la plus proche (4 blocs à la ronde) ; un écoulement sans source tarit ; deux sources côte à côte au-dessus d'un
  sol font une troisième source (une source sans fin). L'eau d'avant la 1.3 (la mer) ne bouge que si on la touche :
  casser un bloc au bord d'un lac le fait couler dans le trou.
* **La lave** : des lacs au fond des grottes (sous y = 10, où il y avait de l'eau). Elle coule de 3 blocs, six fois
  plus lentement que l'eau, et luit (animée par le shader, opaque). Elle bouillonne quand on en est près (le son).
* **Lave et eau** : une source de lave qui touche de l'eau devient de l'**obsidienne** (noire, très dure : une
  pioche en cristal, 80 s à la main) ; un écoulement de lave, des **pavés** ; de la lave qui tombe dans de l'eau, de
  la **pierre**. Un sifflement les accompagne.
* L'eau et la lave cassent ce qu'elles rencontrent de fragile : les plantes, le fil, les torches, les leviers, les
  boutons, les plaques, les répéteurs (survie : ce qui tombe va dans l'inventaire si on est à moins de 8 blocs).
* **Le seau** (`I·I` / `·I·`, du fer) : clic droit sur une source, il la prend ; plein, il la pose (survie ; en
  créatif, l'eau et la lave sont dans la liste des blocs, B).
* Dans la lave on nage lentement, la vue est orange ; en survie, elle brûle (§ 2 septies).

## 2 sexies. Les circuits de cuivre

Un signal a une **puissance de 0 à 15**. La source donne 15 ; le **fil** (la poudre de cuivre posée au sol) perd 1
par bloc, il monte et descend les marches d'un bloc. Un **bloc plein** est alimenté **fort** (par un répéteur, un
comparateur ou un observateur qui pointe dedans, une torche dessous, un levier ou un bouton fixé dessus, une plaque
posée dessus) ou **faible** (par du fil qui pointe dedans) ; fort, il alimente aussi le fil voisin ; faible,
seulement les composants (lampe, piston, répéteur, torche fixée à lui…). La simulation avance de 20 pas par seconde ;
un « temps » (le délai d'un répéteur) vaut 2 pas, 0,1 s.

| Bloc | Rôle | Fabrication |
|---|---|---|
| minerai de cuivre | sous y = 20 ; une pioche en fer ; donne 4 poudres de cuivre | — |
| poudre de cuivre (le fil) | posée sur un bloc : le fil, qui s'oriente seul vers ses voisins (seul : une croix) ; s'allume de son signal | 9 d'un bloc de cuivre |
| bloc de cuivre | une source de 15, toujours | 9 poudres |
| torche de cuivre | **l'inverseur** : allumée (15) tant que le bloc qui la porte n'est pas alimenté, éteinte s'il l'est (1 temps) ; alimente fort le bloc au-dessus. Elle grille si elle change plus de 8 fois en 3 s (8 s) | poudre sur un bâton |
| levier | basculé au clic droit ; posé sur n'importe quelle face d'un bloc | bâton sur un pavé |
| bouton de pierre / de bois | une impulsion d'1 s / 1,5 s | une pierre / une planche |
| plaque de pierre / de bois | alimentée tant que le joueur est dessus | 2 pierres / 2 planches côte à côte |
| répéteur | redonne 15 vers l'avant, après 1 à 4 temps (clic droit : le délai) ; allonge une impulsion trop courte ; **bloqué** par un répéteur ou comparateur allumé sur son côté | `TDT` / `SSS` (torches, poudre, pierres) |
| comparateur | sortie = l'arrière s'il est ≥ les côtés ; en soustraction (clic droit, la torche de devant s'allume) : l'arrière moins le plus fort côté | `·T·` / `TKT` / `SSS` (torches, cristal, pierres) |
| observateur | une impulsion de 1 temps par son dos quand le bloc qu'il regarde change (posé, cassé, changé d'état), ou quand un piston le déplace | `CCC` / `DDK` / `CCC` (pavés, poudre, cristal) |
| piston | alimenté (pas par sa face) : pousse jusqu'à 12 blocs ; casse ce qui est fragile ; ne pousse ni le socle, ni l'obsidienne, ni le four, ni un piston sorti ; pousse le joueur | `PPP` / `CIC` / `CDC` (planches, pavés, fer, poudre) |
| piston collant | ramène le bloc collé à sa tête en rentrant | résine sur un piston |
| bloc de colle | entraîne les blocs qui le touchent quand un piston le pousse ou le tire (les machines qui avancent seules : observateurs, pistons collants et colle) | 9 résines |
| lampe à signal | allumée quand elle est alimentée (s'éteint 2 temps après) | `·D·` / `DLD` / `·D·` (poudre, lampe) |
| bloc musical | une note à chaque front montant ; clic droit : la note suivante (2 octaves) ; l'instrument selon le bloc dessous : bois → basse, pierre → grosse caisse, sable → caisse claire, verre → clic, métal → cloche, laine → guitare, neige → carillon, sinon une harpe | `PPP` / `PDP` / `PPP` |
| capteur de jour | la puissance suit la lumière du jour (clic droit : inversé, la nuit) | `GGG` / `KKK` / `PPP` (verre, cristal, planches) |
| dynamite | allumée par un signal (ou une torche de cuivre en main, clic droit) : 4 s, puis elle explose (rayon 3,5 ; ni le socle, ni l'obsidienne, ni les liquides) ; une dynamite dans l'explosion part aussi | `CSC` / `SCS` / `CSC` (charbon, sable) |
| résine | — | un cactus au four, ou 4 aiguilles de pin |

Poser : la torche, le levier et le bouton contre la face visée ; le fil, les plaques, le répéteur et le comparateur
sur un bloc (le répéteur et le comparateur sortent dans la direction du regard) ; le piston pousse vers le joueur ;
l'observateur regarde là où regarde le joueur. **Maj + clic droit** pose contre un levier, un bouton… au lieu de
s'en servir. Casser la tête d'un piston casse le piston.

## 2 septies. Les créatures, la santé, la faim

**La santé et la faim ne comptent qu'en mode survie** (dix cœurs et dix cuisses au-dessus de la barre ; des bulles
sous l'eau). En créatif rien ne blesse, les créatures y vivent mais n'attaquent pas.

* **Se blesser** : une chute de plus de 3 blocs (un demi-cœur par bloc de plus ; l'eau l'amortit), la lave (un cœur
  par demi-seconde, puis le feu quelques secondes : l'eau ou la pluie l'éteint), l'eau sans air (15 s, puis un cœur
  par seconde), les coups des créatures, une explosion, la faim (jusqu'à un demi-cœur). Mort : on revient au **point
  de départ** (`spawn set` le change), l'inventaire gardé.
* **La faim** baisse lentement, plus vite en courant, en sautant, en se battant, en guérissant ; bien nourri (9 cuisses
  et plus), on guérit d'un demi-cœur toutes les 3 s. **Manger** : clic droit avec la nourriture en main : pomme 4,
  poulet cru 2, **poulet rôti** 6, porc cru 3, **côtelette grillée** 8 (la viande cuit au four).
* **Se battre** : clic gauche sur une créature (3,5 blocs) ; la main fait 1, une **épée** (`M` / `M` / `S` : bois 4,
  pierre 5, fer 6, cristal 7), une hache ou une pioche un peu moins ; le coup repousse. Les épées s'usent.

| Créature | Où, quand | Ce qu'elle fait | Ce qu'elle laisse |
|---|---|---|---|
| **poule** | sur l'herbe, en groupes | flâne ; suit qui tient des **graines** (15 % des hautes herbes) ; nourrie, cherche un partenaire : un poussin, adulte en 5 min | poulet cru, plumes |
| **cochon** | sur l'herbe | pareil avec les **pommes** (6 % des feuilles) | porc cru (1 à 3) |
| **loup** | en meute, sur la neige, parfois sur l'herbe | sauvage, il ne fait rien ; frappé, il attaque. **Des os** (un sur trois) l'apprivoisent : collier rouge. **Clic droit** : assis, il **garde sa place** (il attaque toute créature de la nuit à moins de 12 blocs, puis revient) ; debout, il **suit** son maître (il le rejoint s'il est trop loin) et le défend. La viande le soigne ; deux loups nourris de viande : un louveteau | — |
| **rôdeur** | la nuit dehors, ou dans le noir sous terre | marche vers le joueur (20 blocs) et frappe (1,5 cœur) ; **brûle au soleil** | os |
| **araignée** | la nuit, ou dans le noir | rapide, **grimpe aux murs**, bondit ; le jour, dehors, laisse le joueur tranquille | fil d'araignée (4 : une laine) |

Les animaux sont sauvés avec le monde (`entities.txt`) ; les créatures de la nuit non. Elles apparaissent à 18 à 40
blocs du joueur (10 au plus), les animaux plus loin (14 au plus) ; une créature brûlée par la lave laisse sa viande
cuite. La musique se tend quand une créature de la nuit est proche. Les plaques s'enfoncent sous les créatures ; les
pistons et les explosions les poussent. La console : `summon <créature> [n]`, `kill <créature>|monsters|all`,
`mobs on|off` (sans créatures de la nuit), `heal`, `feed`, `spawn set`.

## 2 quater. Le son

Tout est synthétisé par le jeu (`audio.cpp`), sans aucun fichier : un thread calcule le son et le donne à AudioKit
(un flux PCM s16 stéréo 44,1 kHz), environ 1 % d'un cœur.

* **La musique d'ambiance**, générative et calme (de quoi rester concentré) : une tonalité tirée de la graine du
  monde ; une gamme et un tempo (66 à 77 à la noire) qui suivent la situation (le jour en majeur, la nuit en dorien,
  la pluie en mineur, l'orage en phrygien, le désert en hijaz, la neige en lydien, la grotte grave et lente, sous
  l'eau étouffée). Devant : une **mélodie au piano électrique** doux, sur la pentatonique de la gamme, faite de
  **motifs** (un rythme, un contour) repris et variés en périodes de quatre phrases (A, A', B, A'' qui se pose sur
  la tonique), un peu balancée ; ses accords égrenés sans fondamentale (tierce, quinte, septième, neuvième), une
  basse ronde ; derrière, des **nappes discrètes** et sombres ; de jour, au grand air, parfois une batterie feutrée
  (grosse caisse, balais, charleston) ; la nuit et dans la neige, quelques cloches. Des passages pleins,
  clairsemés et des silences ; casser et poser des blocs l'animent un peu.
* **L'ambiance** : la pluie, le vent (en altitude, sous l'orage, dans la neige), les oiseaux le jour, les grillons la
  nuit, les gouttes dans les grottes, la lave qui bouillonne, le tonnerre un peu après l'éclair.
* **Les effets**, selon la matière du bloc (pierre, bois, terre, sable, verre, feuilles, laine, neige, métal) :
  casser, les coups de la pioche, poser, les pas, le saut, la chute, plonger dans l'eau, fabriquer, ramasser, un
  outil qui casse, les clics du panneau ; les circuits : leviers et boutons, pistons, la lave qui fige, la mèche et
  l'explosion de la dynamite, les blocs musicaux (au volume des effets, plus faibles au loin).
* Menu **Son** (musique, effets, musique plus ou moins fort) ou la console : `music on|off|<0..100>`,
  `sound on|off|<0..100>` ; gardé dans `terracube.ini`.

## 2 ter. La console de commandes

T (ou / ou ²) ouvre la console, semi-transparente, en bas de la vue ; le `/` du début est facultatif ; un texte qui n'est pas une
commande part en message. La même console sert aux scripts (`game.command(…)`) et à l'agent (`run_command`).

| Commande | Effet |
|---|---|
| `time <heure>` · `time 18:30` · `time day\|noon\|sunset\|night\|midnight` · `time stop\|run` · `time` | l'heure (0 à 24), arrêter ou relancer le temps |
| `weather clear\|rain\|snow\|storm [secondes]` · `weather cycle on\|off` · `weather` | la météo ; sans le cycle, elle reste ; avec une durée, elle revient ensuite (au beau sans le cycle) |
| `mode creative\|survival` (aussi `gamemode`, `c`, `s`) | le mode de jeu |
| `tp <x> <y> <z>` (`~` : relatif au joueur) · `tp spawn` · `spawn` | se déplacer |
| `give <objet> [n]` · `clear` · `inv` | l'inventaire (survie) |
| `recipes [mot]` · `craft <recette> [fois]` | les recettes, fabriquer (survie) |
| `music on\|off\|<0..100>` · `sound on\|off\|<0..100>` | la musique, les effets sonores |
| `fly` · `distance <2..16>` · `seed` · `save` · `info` · `say <texte>` · `help` | divers |
| une ligne du protocole (`world.setBlocks(0,60,0,9,60,9,glass)`) | exécutée, sa réponse affichée |

La météo : pluie (neige là où il fait froid : sol de neige ou de glace, pins, altitude), neige, orage (ciel plus
sombre, éclairs). Elle ne tombe pas sous un toit ni sous les arbres. Par défaut elle change d'elle-même.

## 3. Le protocole des scripts (port 4730)

Une connexion TCP ; une commande par ligne (`\n`), en texte : `nom(arg1,arg2,…)`. Les commandes qui demandent
quelque chose ont **une ligne de réponse** (`error: …` si elle est refusée) ; les autres n'en ont pas et partent
sans attendre (des milliers par seconde) : leur erreur s'affiche dans la conversation du jeu (« script : … »). Une
commande à réponse passe après toutes celles envoyées avant elle sur la même connexion (`ping()` sert à attendre
que tout soit fait). 16 connexions au plus.

| Commande | Réponse |
|---|---|
| `world.getBlock(x,y,z)` | le numéro |
| `world.setBlock(x,y,z,bloc[,état])` | — (l'état : un nombre, ou `east`, `down+on`… ; § 2) |
| `world.getBlockWithData(x,y,z)` | `numéro,état` |
| `entities.list([rayon])` | `id,type,x,y,z,santé,apprivoisé\|…` (64 blocs autour du joueur par défaut) |
| `entities.spawn(type,x,y,z[,tamed\|sitting])` | l'id (`chicken`, `pig`, `wolf`, `rodeur`, `spider` ; un loup apprivoisé, assis) |
| `entities.remove(id\|type\|all)` | — |
| `player.getHealth()` / `player.setHealth(santé[,faim])` | `santé,faim` (0 à 20) / — |
| `world.getPower(x,y,z)` | la puissance d'un circuit (0 à 15) : un fil, la sortie d'un composant, un bloc alimenté |
| `world.use(x,y,z)` | `1` si le bloc a servi (levier basculé, bouton enfoncé, délai d'un répéteur…), sinon `0` |
| `world.getStates(x1,y1,z1,x2,y2,z2)` | `numéro:état:puissance,…` (y, puis z, puis x ; 32 768 au plus) : un circuit lu d'un coup |
| `world.test(x1,y1,z1,x2,y2,z2,pas[,ux,uy,uz])` | un clic droit en (ux,uy,uz), puis tant de pas de simulation tout de suite (1 pas = 0,05 s) : `pas:x,y,z,bloc,état,puissance\|…`, ce qui a changé dans la boîte |
| `world.explain(x,y,z)` | pourquoi un composant est (ou non) alimenté : des lignes séparées par `\|` |
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
| `game.getMode()` / `game.setMode(creative\|survival)` | le mode / — |
| `world.getWeather()` / `world.setWeather(clear\|rain\|snow\|storm[,secondes])` | la météo / — |
| `game.command(ligne)` | une commande de la console : ses lignes, séparées par ` \| ` |
| `player.getInventory()` | `case:objet:nombre\|…` (survie ; les cases 0 à 8 sont la barre) |
| `player.give(objet[,n])` | — (survie) |
| `player.craft(recette[,fois])` | les fois faites, ou `error: …` (survie) |
| `game.info()` | `TerraCube 1.4.1,<monde>,<graine>` |
| `ping()` | `pong` |

### Python

`/apps/terracube.app/python/terracube.py`, sans dépendance (exemples dans `python/examples` : `house.py`,
`pyramid.py`, `rainbow.py`, `golden_touch.py` (les évènements), `snake_walk.py` (suivre le joueur),
`survival_kit.py` (le mode survie : outils fabriqués par le script), `circuits.py` (trois montages : un piston
collant, un arpège de blocs musicaux, une horloge à torche et répéteur), `flying_machine.py` (une machine qui
avance seule : observateurs, pistons, colle), `farm.py` (un enclos, des poules, des cochons, un loup qui garde), `temple_mcp.py` (un client MCP, comme un agent :
un temple aztèque explorable, 80 appels, captures et journal)) ;
`creatures()`, `spawn()`, `remove()`, `health()`, `set_health()` ; `get_states()`, `test()`, `explain()` (la mise
au point des circuits) ; l'exemple `secret_stairs.py` (un escalier caché dans le sol, des dalles tirées par des
pistons collants en cascade, un levier et une torche-inverseur ; il s'essaie lui-même) ; `set_block(x, y, z, bloc, état)`,
`get_block_state()`, `get_power()`, `use()`, les directions dans `Facing` ; aussi `mode()`, `set_mode()`, `weather()`,
`set_weather()`, `command()`, `inventory()`, `give()`, `craft()` :

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
| `get_block`, `get_blocks` | un bloc (un liquide ou un composant : avec son état et sa puissance) ; les blocs d'un pavé (32 768 au plus ; non vides en `[x,y,z,id]`, ou en couches) |
| `get_heightmap` | relief d'une zone (128 × 128 colonnes au plus) : hauteur et bloc du dessus |
| `list_block_types` | les blocs : numéro, nom, solide, transparent |
| `set_block`, `set_blocks`, `fill` | poser un bloc (avec un état : `"east"`, `"down+on"`…) ; une liste (200 000 au plus) ; un pavé plein ou creux |
| `use_block` | un clic droit : basculer un levier, appuyer un bouton, changer un répéteur… |
| `get_blocks` avec `states: true`, `test_circuit`, `explain_block` | la mise au point des circuits : tout un montage lu d'un coup (état, puissance, état en clair) ; un essai (le levier, puis ce qui change, pas par pas) ; pourquoi un bloc ne réagit pas |
| `list_entities`, `spawn_entity`, `remove_entities` | les créatures autour ; en faire apparaître (un loup `tamed`, `sitting`, des petits `baby`) ; en enlever |
| `post_message` | un message au joueur |
| `set_time` | l'heure et si le temps passe |
| `screenshot` | une image PNG de la vue du joueur, ou d'un autre point de vue (`eye` + `look_at` ou yaw/pitch) sans bouger le joueur |
| `get_events` | les blocs cassés, posés, touchés depuis le dernier appel |
| `run_command` | une commande de la console (heure, météo, mode, tp, give, craft…) et sa réponse |
| `get_inventory`, `list_recipes`, `craft` | l'inventaire du mode survie, les recettes (formes de la grille, et combien sont possibles), fabriquer |
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
| `main.cpp` | fenêtre, boucle, entrées ; la présentation : le GPU rend hors écran, l'image est relue (`glReadPixels`), les textes de UIKit y sont dessinés (console mélangée à demi), puis elle est recopiée d'un coup dans la fenêtre comme le fait Cosmo Zoom (`armgl_window` avec des zones posées par-dessus scintillait sur le ZBook) ; le panneau de fabrication (ses cases, le glisser-déposer), la console, menus, mondes, jeton du MCP, `--selftest` |
| `world.h`, `world.cpp` | tronçons, génération (bruit de gradient, biomes, grottes, minerais, arbres), sauvegarde, lancer de rayon |
| `mesh.cpp` | maillage : faces visibles, occlusion ambiante, lumière du ciel lissée, liquides en pente, plantes en croix, les formes des composants (des boîtes tournées par quarts de tour) |
| `entity.h`, `entity.cpp` | les créatures : espèces, esprit (flâner, fuir, suivre, chasser, garder, se reproduire), corps (collisions, nage, escalade), apparitions, sauvegarde, et leurs corps en boîtes animées pour le rendu |
| `sim.h`, `sim.cpp` | la simulation, 20 pas par seconde : l'écoulement de l'eau et de la lave, les circuits (puissance, fil en réseau, composants, pistons et colle, dynamite), les pas programmés |
| `render.h`, `render.cpp` | OpenGL ES 3 : le panneau de fabrication (`CraftDraw`), shaders, ciel et soleil, pluie et neige (gouttes faites par le vertex shader, cachées sous les toits par une carte des hauteurs), tronçons (tronc de vue), eau animée (vagues de la surface dans le vertex shader, texture qui glisse et reflets dans le fragment shader), contour visé, barre de blocs |
| `textures.cpp` | l'atlas de 16 × 16 tuiles, dessiné au départ |
| `player.h`, `player.cpp` | marche, saut, nage, vol, collisions |
| `api.cpp` | protocole des scripts et son serveur (un thread par connexion, une file vidée par la boucle) |
| `audio.h`, `audio.cpp` | le son : musique générative, ambiance, effets, synthétisés dans un thread sur un flux d'AudioKit |
| `items.h`, `items.cpp` | objets, inventaire, minage (dureté, outils, ce qui tombe), recettes à forme et leur reconnaissance dans une grille |
| `console.cpp` | la console de commandes |
| `mcp.h`, `mcp.cpp`, `mcptools.cpp` | transport MCP (repris de Forge) et outils |
| `data/python`, `data/basic` | module Python et exemples ; exemples BASIC |
