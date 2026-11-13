# Pipes

Le Pipes d'Onyx (d'après « Pipe Dream ») en C++ (`main.cpp`), un seul source pour le Pi (AArch64) et le PC
(x86-64). `pipes.asm` reste dans le dossier jusqu'à la validation sur le vrai Pi ; `tools/office/build.sh pipes`
(Pi) et `tools/pc/apps.sh pipes` (PC) construisent le C++.

## D'où vient le code

* **Onyx** (`user/Apps/pipes/main.cpp`) : la classe `Pipes`, sa case `Tile`, la table `CONN` des pièces et ses
  règles : `newGame`, `newRound` (le départ loin des bords, les murs dès la manche 3, `need = 10 + 2 × manche`, le
  compte à rebours de 22 s − 1,5 s par manche, 3,2 s − 0,25 s par pièce), `place` (−50 pour une pièce remplacée),
  `advance` (la croix prise dans chaque sens une seule fois), `spill` (1000 + 250 × manche si la manche est
  gagnée), le débit rapide (F, points doublés), les sons.
* **L'asm de Korin** (`pipes.asm`), qui fait foi pour ce qu'on voit : plein écran sur `gamefb.h` (taille logique
  640 × 480, cases de 54), tuyaux de cuivre calculés point par point avec la carte de l'intérieur (l'eau avance
  dedans), mur de briques, volant du départ, curseur arrondi, éclaboussures, tube du compte à rebours, boutons Eau
  et Pause, score qui rattrape, écran titre, record `SD0:/home/pipes/best.txt`, manette (croix, A pose, X l'eau),
  menu de pause de `gamepause.h`, le menu Partie, le son on/off, `lang/en.txt`. Mêmes pixels que l'asm :
  `gamefb.h`, `gamedraw.h`, `gamepause.h`, `gamesound.h` sont les traductions de ses `.inc`.

## Différences

* **Avec l'asm** : aucune voulue. Le hasard est celui de l'asm (`gf_below`, xorshift64 semé à l'heure), tiré dans
  le même ordre ; Onyx avait son xorshift32 (`rng_n`).
* **L'asm (donc ce port) avec Onyx** : le temps avance de 17, 17, 16 ms par image (Onyx : le temps mesuré) ; écran
  titre ; record gardé dans un fichier ; la souris qui survole déplace le curseur ; P ouvre le menu de pause ; plein
  écran mis à l'échelle ; les couleurs sont du décor fixe (Onyx : le fond et les textes du thème), comme dans l'asm.

## Ce qui manque

Rien par rapport à l'asm. Le son du PC (pas de pilote) : le jeu y reste muet.
