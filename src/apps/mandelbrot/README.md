# Fractales (mandelbrot)

Fractales en C++ (`main.cpp` : la fenêtre, les vues, les threads, le PNG ; `kernels.cpp` : les calculs et les
couleurs ; `fractal.h` : ce qu'ils partagent), un seul source pour le Pi (AArch64) et le PC (x86-64).
`mandelbrot.asm` et `kernels.inc` restent dans le dossier jusqu'à la validation sur le vrai Pi ;
`tools/office/build.sh mandelbrot` (Pi) et `tools/pc/apps.sh mandelbrot` (PC) construisent le C++.

## D'où vient le code

* **Onyx** (`user/Apps/mandelbrot/main.cpp`, le « fractal browser ») : peu de chose, il était petit (quatre types en
  virgule fixe Q4.28, un rendu d'un coup) : le centre et la largeur de la vue, `reset_view`, le budget d'itérations
  qui monte avec le zoom, la liste des types en haut, R pour revenir au départ.
* **L'asm de Korin** (`mandelbrot.asm`, `kernels.inc`), qui fait foi et a presque tout ajouté : treize types
  (Mandelbrot, Julia et le choix de c à la souris, Burning Ship, Tricorn, Multibrot 3 à 5, Newton, Phoenix, Lyapunov
  et sa suite AB, attracteurs de Clifford et de De Jong et leurs « Autres valeurs », Buddhabrot), les mêmes vues de
  départ, huit palettes de 4 096 couleurs, seize signets, l'animation des couleurs, `[` `]` pour étirer la palette,
  les flèches, + et −, la molette sur le point visé, le glisser, le double toucher, le clic droit ; le rendu
  progressif sur tous les cœurs (un point sur 8, puis 2, puis tous ; un changement de vue arrête les threads et
  repart), les valeurs gardées par point (changer de palette ne recalcule rien), les comptes des attracteurs et du
  Buddhabrot par lots, rehaussés toutes les 250 ms ; la ligne d'état (zoom, itérations ou points, temps) ; le PNG
  dans `HOME:/pictures` (`fractale.png` proposé ; filtre 0, RVB 8 bits, deflate et CRC d'ArchiveKit : même
  fichier) ; la taille de départ bornée par l'écran, la fenêtre redimensionnable (640 × 400 au moins) ; les jetons
  du thème pour le fond, les palettes en dur (du contenu) ; `lang/en.txt`, `app.txt` inchangés.
* **Les calculs** (`kernels.cpp`) : `kernels.inc` (NEON, deux points à la fois en double précision) en C++ portable,
  **mêmes opérations dans le même ordre, mêmes constantes** (vérifiées octet pour octet dans le binaire de l'asm),
  mêmes approximations (`log2d` et le sinus par polynôme, pas la libm) : les points vont par quatre dans des
  vecteurs de GCC (`vector_size`, NEON sur le Pi, SSE2 sur le PC, sans intrinsèque ni asm) ; pas de contraction
  (`fp-contract=off`), et un `fma` explicite là où l'asm a `fmla` / `fmls` ; les conversions `fcvtzs` / `fcvtzu`
  imitées (saturées, NaN → 0) ; le fichier se compile en `-O3` (`#pragma`, sans toucher aux scripts). Les couleurs
  (`color_line`, Newton, Lyapunov, `color_hist`, `set_lut`) sont celles de `mandelbrot.asm`, en entiers.

## Différences avec l'asm

* Threads : POSIX (`pthread`, mutex et condition), autant que `sysconf (_SC_NPROCESSORS_ONLN)`, 16 au plus (l'asm :
  `SYS_SYSINFO`, 4 au plus) ; la mémoire par `malloc` / `calloc` (l'asm : `SYS_VM_MAP`).
* Les calculs vont par quatre points (l'asm : deux) ; une ligne peut calculer jusqu'à trois points de trop (tampon
  agrandi de 8) ; une paire de points dans la cardioïde est sautée comme dans l'asm, mêmes résultats.
* Sur le PC sans FMA (x86-64 de base), `fma` fait deux arrondis : les derniers bits de Newton, Multibrot 4 et 5,
  Phoenix et des attracteurs peuvent y différer de ceux du Pi (Mandelbrot, Julia, Burning Ship, Tricorn,
  Multibrot 3, Lyapunov et le Buddhabrot n'en ont pas : mêmes bits).
* Bornes d'un attracteur : un NaN serait ignoré (l'asm le propageait par `fmin`) ; impossible avec des sinus.
* PNG : les tampons sont libérés (l'asm gardait l'image brute, et le deflate si l'ouverture échouait) ; un champ de
  suite AB sans texte ne plante pas.
* Le Buddhabrot et les attracteurs dépendent du partage des lots entre threads (comme l'asm) : leurs images ne sont
  pas identiques d'une fois à l'autre.

## Vérifié (Pi sous QEMU raspi4b, 4 cœurs, 13/11)

`tools/apps/compare.sh` : l'asm puis le C, mêmes clics (les treize types, molette, `P`, `]`, glisser, clic droit,
flèches et `=`, signets 1, 5, 11 et 13, choix de c puis Julia, Buddhabrot, PNG) : **l'image est identique au pixel**
pour Mandelbrot, Julia, Burning Ship, Tricorn, Multibrot 3 à 5, Newton, Phoenix, Lyapunov, le zoom, la palette,
l'étirement, le glisser, le recul, les touches, les signets et le choix de c ; Clifford, De Jong et le Buddhabrot ne
diffèrent que par le hasard du partage des lots (même allure). Le PNG enregistré (relu sur la carte) est l'image de
l'écran au pixel, mêmes morceaux (IHDR, un IDAT, IEND). Le texte du bas ne diffère que par le temps mesuré.

Vitesse (le temps de la ligne d'état, asm / C, QEMU par TCG : il ne dit rien du vrai NEON, à mesurer sur le Pi) :
Mandelbrot 536 / 552 ms, Julia 766 / 981, Burning Ship 1 147 / 1 210, Tricorn 668 / 378, Multibrot 3 1 591 / 973,
Multibrot 4 3 077 / 2 597, Multibrot 5 4 144 / 3 903, Newton 689 / 523, Phoenix 1 258 / 1 005, Lyapunov 7 766 / 4 168,
Clifford (4 000 lots) plus de 13 s / 13 775, Buddhabrot 9 825 / 10 009. Le C va aussi vite que l'asm sous QEMU.

## Ce qui manque

Rien par rapport à l'asm.
