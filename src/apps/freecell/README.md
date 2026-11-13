# FreeCell

Réussite FreeCell en C++ (`main.cpp`), un seul source pour le Pi (AArch64) et le PC (x86-64). Construite par
`tools/office/build.sh freecell` (Pi) et `tools/pc/apps.sh freecell` (PC). `freecell.asm` reste dans le dossier
jusqu'à la validation du C sur le vrai Pi.

## D'où vient le code

* **D'Onyx** (`user/Apps/freecell/main.cpp`, `games/cards.h`) : le modèle (`Col`, `FState`, carte = enseigne × 13 +
  rang), la donne de Microsoft (parties 1 à 32000), les règles (`canStack`, `isRun`, `maxRun` = (cases libres + 1)
  << colonnes vides, la suite trop longue coupée sur une colonne vide), la montée automatique sans risque
  (`safe`, `autoPlay`), l'annulation sur 128 coups, la structure de la classe et le nom de ses méthodes.
* **De l'asm de Korin** (`freecell.asm`, `src/inc/cards.inc`, ce qui se voit et ce que l'utilisateur a validé) : la
  table qui suit la taille de la fenêtre (cartes de 32 à 200 px, `cards_layout`), les cartes dessinées (enseignes
  anticrénelées, figures V D R, dos), le bandeau du haut (Nouvelle, Annuler, Partie…, l'état, la place libre qui
  déplace la fenêtre), le glisser sans scintillement (couches T et K), le **toucher sans glisser** qui envoie la
  carte à la destination évidente (fondation, colonne garnie, case libre, colonne vide ; le second appui d'un
  double-clic ignoré), le message « Pas assez de cases libres… » dans le bandeau pendant 2,5 s, le message de fin et
  l'animation, le menu Partie (Nouvelle partie Ctrl+N, Choisir une partie…, Recommencer, Annuler le coup Ctrl+Z,
  Quitter), `freecell <numéro>` (`run.play` d'`app.txt`), les textes et `lang/`.
* Le dessin des cartes et de la table est dans **`src/inc/cards.h`**, partagé avec Solitaire : `games/cards.h`
  d'Onyx pour le modèle et l'animation de fin, la traduction de `cards.inc` pour le reste (mêmes noms, mêmes calculs
  entiers, donc mêmes pixels). Le générateur pseudo-aléatoire est celui de `cards.inc` (pas le xorshift d'Onyx).

## Différences avec l'asm

* Aucune différence voulue. Les arguments : `argv[1]` (l'asm lisait la ligne brute) ; seul le premier mot compte,
  comme avant.

## Différences avec Onyx (venues de l'asm, gardées)

* Pas de son (Onyx : `sfx` à chaque coup et « Sound On / Off ») : l'asm n'en a pas.
* Le simple toucher envoie la carte (Onyx : double clic vers la fondation ou une case) ; la table et les cartes
  suivent la fenêtre (Onyx : 64 × 88 et une image mise à l'échelle) ; le dialogue de choix est `ui_ask`.

## Ce qui manque

* Le menu de la barre ne s'ouvre pas au clic sous QEMU (ni pour l'asm) : Recommencer et Quitter n'ont été essayés
  que par le code.
