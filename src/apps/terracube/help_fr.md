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

## Les mondes

Partie > Nouveau monde : un nom et une graine (la même graine donne le même monde). Le monde est sauvé à la
fermeture, chaque minute s'il a changé, et par Ctrl+S, dans `HOME:/terracube/worlds`.

## Piloter le jeu

* **Python** et **BASIC** : Pilotage > Scripts (port 4730). Exemples :
  `python3 /apps/terracube.app/python/examples/house.py`, `basic /apps/terracube.app/basic/pyramid.bas`.
* **Un agent IA** (Claude Code…) : Pilotage > Piloter par un agent IA. La commande à donner à l'agent est mise
  dans le presse-papiers ; il voit le monde, le parcourt et construit, et vous voyez tout en direct.
