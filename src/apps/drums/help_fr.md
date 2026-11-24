# Boîte à rythmes

Un séquenceur de percussions : chaque piste est un instrument, chaque case une double croche.
Les notes partent en **MIDI** sur le canal 10 vers le synthé General MIDI du système (SoundFont).

## Composer

- **Clic** sur une case : une note ; encore un clic : elle s'efface.
- **Clic droit** ou **Maj+clic** : un **accent** (note plus forte, marquée d'un point).
- **Glisser** : peint ou efface plusieurs cases d'un coup.
- **Espace** : lecture et arrêt.

## Les instruments

- La grille a une rangée pour chacun des **61 instruments du kit** (General MIDI et GS), rangés par familles :
  grosses caisses, caisses et claps, charlestons, toms, cymbales, peaux latines, secoués et grelots, bois et
  métaux, effets. Un clic sur l'en-tête d'une famille la replie ou la déplie ; la molette fait défiler.
- **Compact** (au-dessus des noms, ou Présentation, Vue compacte) ne montre que la batterie de base et les
  rangées qui jouent ; **Tous** les montre toutes.
- **Clic sur le nom** : écouter l'instrument ; **clic droit** : son menu (vider, muet, solo, niveau).
- La **barre de niveau** règle sa force ; **M** le rend muet, **S** le joue seul.

## Mesures et signature

- **Signature** : 3/4, 4/4, 5/4, 6/8 ou 12/8 ; **Mesures** : de 1 à 8.
- Les onglets **1, 2, 3…** au-dessus de la grille montrent une mesure ; **Suivre la lecture** fait suivre la
  grille ; **Recopier dans les suivantes** copie la mesure montrée ; Page préc. et Page suiv. changent de mesure.

## Son

- **Kit** : les kits de batterie de la SoundFont (Standard, Room, Power, Electronic, 808/909, Jazz, Brush...).
- **Swing** retarde les contretemps ; **Tempo** en noires par minute.

## Fichiers

- **Enregistrer** écrit un fichier `.drum` (texte). Des modèles sont dans `/home/public/DrumMachine`.
- **Exporter en MIDI** écrit un fichier `.mid` (canal 10, tempo, signature, kit), lisible par Musique,
  Koton ou tout logiciel de musique.
