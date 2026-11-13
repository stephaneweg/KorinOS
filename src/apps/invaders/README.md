# Invaders

Space Invaders, le jeu d'Onyx, en C++ (`main.cpp`), un seul source pour le Pi (AArch64) et le PC (x86-64).
`invaders.asm` reste dans le dossier jusqu'à la validation sur le vrai Pi ; `tools/office/build.sh invaders` (Pi) et
`tools/pc/apps.sh invaders` (PC) construisent le C++.

## D'où vient le code

* **Onyx** (`user/Apps/invaders/main.cpp`) : la classe `Invaders` et ses règles, mêmes noms et mêmes nombres :
  `newGame`, `newWave`, `buildShields`, `hitShield` (l'explosion qui ronge les boucliers), `fire`, `march` (la
  cadence `30 + n × 14 − vague × 20`, la basse de quatre notes, la flotte qui mange les boucliers et gagne au sol),
  `alienFire`, `shipHit`, la soucoupe (50, 100, 150 ou 300 points), les dessins des sprites (calmar, crabe, poulpe,
  canon, soucoupe, explosion), les sons (tir, impact, soucoupe, fins de partie).
* **L'asm de Korin** (`invaders.asm`), qui fait foi pour ce qu'on voit : plein écran sur `gamefb.h` (taille logique
  448 × 480), sprites « néon » calculés depuis les dessins d'Onyx (dégradé, lueur), ciel de nébuleuse, éclats
  (particules), bandeau de verre, écran titre, record `SD0:/home/invaders/best.txt`, manette (croix, A tenu tire),
  menu de pause de `gamepause.h` (Start, Échap, P, le bouton ||), le menu Partie, le son on/off, `lang/en.txt`.
  Mêmes pixels que l'asm : `gamefb.h`, `gamedraw.h`, `gamepause.h`, `gamesound.h` sont les traductions de ses
  `.inc`.

## Différences

* **Avec l'asm** : aucune voulue. Le hasard est celui de l'asm (`gf_below`, xorshift64 semé à l'heure), tiré dans
  le même ordre ; Onyx avait son xorshift32 (`rng_n`).
* **L'asm (donc ce port) avec Onyx** : écran titre ; record gardé dans un fichier (Onyx : en mémoire) ; P ouvre le
  menu de pause (Onyx : pause à bascule) ; plein écran mis à l'échelle (Onyx : fenêtre redimensionnable) ; les
  couleurs du bandeau et des messages sont du décor fixe (Onyx : les teintes du thème), comme dans l'asm ; le temps
  en millisecondes (Onyx : des tics de 10 ms).

## Ce qui manque

Rien par rapport à l'asm. Le son du PC (pas de pilote) : le jeu y reste muet.
