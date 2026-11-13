# Solitaire

Réussite Klondike en C++ (`main.cpp`), un seul source pour le Pi (AArch64) et le PC (x86-64). Construite par
`tools/office/build.sh solitaire` (Pi) et `tools/pc/apps.sh solitaire` (PC). `solitaire.asm` reste dans le dossier
jusqu'à la validation du C sur le vrai Pi.

## D'où vient le code

* **D'Onyx** (`user/Apps/solitaire/main.cpp`, `games/cards.h`) : le modèle (`Pile`, `State`, carte = enseigne × 13
  + rang, cartes retournées ou non), la donne (mélange puis sept colonnes, le reste au talon), les règles
  (`canTab` : un roi sur une colonne vide, couleurs alternées), le talon à une ou trois cartes et le rebut qui
  revient, le score de Windows (+5 du rebut vers une colonne, +10 vers une fondation, −15 d'une fondation, +5 pour
  une carte retournée, −100 ou −20 au retour du rebut, −2 à l'annulation, prime de temps 700000 / s / 10 après
  30 s), l'annulation sur 64 coups, la structure de la classe `Klondike` et le nom de ses méthodes.
* **De l'asm de Korin** (`solitaire.asm`, `src/inc/cards.inc`, ce qui se voit et ce que l'utilisateur a validé) :
  la table qui suit la taille de la fenêtre (`cards_layout` sur 7 colonnes, pas des cartes cachées et éventail du
  rebut à trois cartes proportionnels), les cartes dessinées, le bandeau du haut (Nouvelle, Défaire, Auto, score
  et temps, la place libre qui déplace la fenêtre), le glisser sans scintillement, le **toucher sans glisser** qui
  envoie la carte à la destination évidente (sa fondation pour la carte du sommet, sinon la première colonne qui
  la prend ; un roi déjà au fond ne bouge pas ; le second appui d'un double-clic ignoré), le bouton droit et Auto
  qui montent tout, la barre d'espace qui retourne le talon, **une fondation par enseigne** (Onyx : n'importe
  laquelle des quatre), le signe ↻ sur le talon vide, le message de fin et l'animation, le menu Partie (Distribuer
  Ctrl+N, Annuler le coup Ctrl+Z, Cartes vers les fondations, Tirer une carte / trois cartes cochés, Quitter),
  `solitaire <n>` (la graine de la donne, `run.deal` d'`app.txt`), le score qui ne descend jamais sous 0 (à chaque
  ajout), les textes et `lang/`.
* Le dessin des cartes et de la table est dans **`src/inc/cards.h`**, partagé avec FreeCell (traduction de
  `cards.inc` : mêmes calculs entiers, donc mêmes pixels ; le même générateur pseudo-aléatoire, donc la même donne
  pour la même graine).

## Différences avec l'asm

* Aucune différence voulue. Les arguments : `argv[1]` (l'asm lisait la ligne brute) ; seul le premier mot compte,
  comme avant.

## Différences avec Onyx (venues de l'asm, gardées)

* Pas de son ni de « Sound On / Off » (l'asm n'en a pas) ; le bandeau ne dit pas « Draw one / three » (le menu
  coché le dit).
* Le simple toucher envoie la carte (Onyx : double clic vers la fondation) ; la table et les cartes suivent la
  fenêtre (Onyx : 64 × 88).

## Ce qui manque

* Le menu de la barre ne s'ouvre pas au clic sous QEMU (ni pour l'asm) : Tirer trois cartes et Quitter n'ont été
  essayés que par le code.
