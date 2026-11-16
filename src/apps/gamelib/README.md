# Game Library (gamelib)

La bibliothèque de jeux de Korin : le lanceur des émulateurs (pas un émulateur). À gauche les consoles installées
avec leur nombre de jeux, à droite les cartes (image, nom). Un clic choisit, un double-clic ou Entrée lance la ROM
avec son émulateur ; View > Play Full Screen ajoute `--full`. Clavier : flèches, Entrée, F5 ; manette : croix,
Start ou B, L et R changent de console.

`main.cpp` (C++, un seul source pour le Pi et le PC) remplace `gamelib.asm`, qui reste dans le dossier jusqu'à sa
validation sur le Pi.

## D'où vient le code

* **D'Onyx** (`user/Apps/gamelib/main.cpp`, la Game Library) : la structure (les consoles, les jeux par console puis
  par nom, le filtre par console, les jeux montrés `g_vis`, la carte choisie qui reste visible, la manette par
  fronts, le plein écran par `--full`), la comparaison d'extension sans casse.
* **De l'asm de Korin** (le comportement qui fait foi) : les huit consoles et leurs émulateurs
  (`/apps/<emu>.app/main`, une console n'apparaît que si son émulateur est là), les dossiers (`HOME:/roms`,
  `/media/sd1/roms`, `HOME:/gamelib/folders.txt`, quatre niveaux de sous-dossiers), les images (`--thumb sortie rom`,
  une à la fois, 2 minutes au plus, `HOME:/gamelib/thumbs/<nom>.raw`, 160 x 120 en 32 bits), la mise en page des
  cartes, les menus (Library : Refresh F5, Add Folder..., Quit ; View : Play Full Screen), les textes (anglais,
  traduits par le dictionnaire).
* Ce qu'Onyx faisait en plus et que l'asm n'a pas (groupes repliables de la barre, menus du clic droit, images
  faites dans le programme par les cœurs d'émulateurs, étiquette N64, bannière GameCube, mode poche) n'est pas
  repris : le port fait ce que fait l'asm.

## Écarts avec l'asm

* Les émulateurs sont lancés par `f_spawn` d'AppKit (rang 26, `armpi_kit_open`), comme l'asm : la même ligne
  d'arguments (`--full chemin`, `--thumb image chemin`), sans guillemets autour d'un chemin qui contient des
  espaces. `posix_spawn` les aurait mis, ce que les émulateurs en assembleur ne relisent pas.
* Le nom de la carte choisie prend `TJ_ACCENT_INK` (l'encre lisible sur l'accent) au lieu du blanc en dur ; le
  fond d'une carte sans image reste celui de l'asm (`TJ_DARK`, un drapeau, donc noir) avec le nom de la console en
  blanc.
* Les images d'un ancien parcours sont rendues (`free`) à Refresh ; l'image se lit jusqu'au bout (l'asm : un seul
  `read`).
* Le tri compare au plus les 64 octets du nom (l'asm pouvait lire après le 0 de fin d'un nom plus court).

## Sur le PC

La plupart des émulateurs n'y existent pas : leurs consoles n'apparaissent pas, et sans aucun émulateur la liste
montre seulement « All Games 0 » avec le message « No games yet… », comme l'asm le ferait. AppKit (pour `f_spawn`)
est celui du PC (`src/kits/appkit/appkit.c`).
