# TerraCube

Un monde sans fin fait de blocs : explorez-le, creusez, construisez.

## Jouer

* **Cliquez dans la vue** pour jouer ; **Échap** rend la souris.
* **ZQSD** ou les **flèches** pour marcher, la **souris** pour regarder, **Espace** pour sauter, **Maj** pour
  s'accroupir (on ne tombe pas du bord), **Ctrl** pour courir.
* **Clic gauche** : casser ; **clic droit** : poser ; **clic du milieu** ou **A** : prendre le bloc visé.
* **1** à **9** ou la **molette** : choisir la case ; **B** : la liste de tous les blocs.
* **F** : voler (Espace monte, Maj descend). **F3** : infos détaillées ; **F9** : plein écran.
* **T** : écrire un message, ou une commande : `/tp x y z`, `/time 18`, `/fly`, `/seed`, `/spawn`.

## Les mondes

Partie > Nouveau monde : un nom et une graine (la même graine donne le même monde). Le monde est sauvé à la
fermeture, chaque minute s'il a changé, et par Ctrl+S, dans `HOME:/terracube/worlds`.

## Piloter le jeu

* **Python** et **BASIC** : Pilotage > Scripts (port 4730). Exemples :
  `python3 /apps/terracube.app/python/examples/house.py`, `basic /apps/terracube.app/basic/pyramid.bas`.
* **Un agent IA** (Claude Code…) : Pilotage > Piloter par un agent IA. La commande à donner à l'agent est mise
  dans le presse-papiers ; il voit le monde, le parcourt et construit, et vous voyez tout en direct.
