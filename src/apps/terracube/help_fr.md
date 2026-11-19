# TerraCube

Un monde sans fin fait de blocs : explorez-le, creusez, construisez.

## Jouer

* **Cliquez dans la vue** pour jouer ; **Échap** rend la souris.
* **ZQSD** ou les **flèches** pour marcher, la **souris** pour regarder, **Espace** pour sauter, **Maj** pour
  s'accroupir (on ne tombe pas du bord), **Ctrl** pour courir.
* **Clic gauche** : casser ; **clic droit** : poser ; **clic du milieu** ou **A** : prendre le bloc visé.
* **1** à **9** ou la **molette** : choisir la case ; **B** : la liste de tous les blocs.
* **F** : voler (Espace monte, Maj descend). **F3** : infos détaillées ; **F9** : plein écran.
* **T** : la console de commandes (l'heure, la météo, le mode…) ; un texte simple part en message.

## Le mode survie

Partie > Mode survie, ou la commande `mode survival`. Les blocs se cassent en tenant le clic gauche et se ramassent ;
poser un bloc en consomme un. **I** ouvre, dans le jeu, la **table de fabrication** (en haut) et l'inventaire (en
bas) : on glisse les matières de l'inventaire sur la grille (2 × 2 à la main, 3 × 3 près d'un établi ; bouton droit : un seul
objet) et leur agencement décide de ce qui est fait : trois planches en haut et deux bâtons au milieu en dessous font
une pioche. On glisse ensuite le résultat dans l'inventaire ; Maj + clic sur le résultat en fait le plus possible.
**R** montre le livre des recettes ; Échap ou I referme. Le bouton **Livre des recettes** montre toutes les formes. Un four
proche cuit le minerai, le sable, l'argile. Sans pioche, la pierre ne donne rien.

## La console

**T** (ou **/**) ouvre la console : `time 18`, `weather rain`, `weather storm`, `mode survival`, `give log 10`,
`recipes`, `craft wood_pickaxe`, `tp ~ ~10 ~`, `help`… Entrée exécute, Échap ferme.

## L'eau, la lave

L'eau coule (7 blocs) et tombe dans les trous ; la lave, au fond des grottes, coule plus lentement et brûle en mode
survie. Une source de lave touchée par l'eau devient de l'**obsidienne**, un écoulement de lave des pavés. Le
**seau** (deux lingots de fer de chaque côté, un en dessous au milieu) prend une source et la repose.

## Les circuits

Le **cuivre** (sous y = 20, pioche en fer) donne de la poudre : posée au sol, c'est du **fil**, qui porte un signal
de 15 blocs. Les sources : **levier**, **bouton**, **plaque**, **bloc de cuivre**, **torche de cuivre** (elle
s'éteint quand son bloc est alimenté : l'inverseur), **capteur de jour**, **observateur** (il voit un bloc changer).
Ce qui s'en sert : **lampe à signal**, **piston** et **piston collant** (avec des **blocs de colle**, des machines
qui avancent seules), **bloc musical**, **dynamite**. Le **répéteur** redonne la force du signal avec un délai
(clic droit), le **comparateur** compare ou soustrait. Clic droit pour se servir d'un levier ou d'un bouton ; Maj +
clic droit pour poser contre lui. Toutes les recettes : **R** dans la fabrication, ou `recipes`.

## Le son

Une musique d'ambiance calme composée par le jeu au fil de la partie : une mélodie de piano électrique faite de
motifs, sur des nappes discrètes, qui change avec le jour, la nuit, la météo, les grottes et les biomes ; et des
effets sonores (blocs, pas, eau, fabrication, circuits, lave). Menu **Son**, ou `music off` et
`sound 50` dans la console.

## Les mondes

Partie > Nouveau monde : un nom et une graine (la même graine donne le même monde). Le monde est sauvé à la
fermeture, chaque minute s'il a changé, et par Ctrl+S, dans `HOME:/terracube/worlds`.

## Piloter le jeu

* **Python** et **BASIC** : Pilotage > Scripts (port 4730). Exemples :
  `python3 /apps/terracube.app/python/examples/house.py`, `basic /apps/terracube.app/basic/pyramid.bas`.
* **Un agent IA** (Claude Code…) : Pilotage > Piloter par un agent IA. La commande à donner à l'agent est mise
  dans le presse-papiers ; il voit le monde, le parcourt et construit, et vous voyez tout en direct.
