# arabesque : la composition algorithmique par fractales

Un chemin calculé (un système de fonctions itérées, IFS) parcourt une image ; la couleur de chaque point visité devient
une note de l'une des dix parties General MIDI, qui jouent en direct sur le synthétiseur d'AudioKit. L'idée est celle
d'**Art Song** de David T. Strohbeen (shareware pour Windows 3.1, 1996) ; le programme est écrit ici, sans son code ni ses
fichiers, et lit ses formats `.ifs`, `.stl` et `.sca`. Le nom n'est pas repris : « Art Song » était une marque.

    arabesque                         le premier tracé et le premier style livrés, sur un mandala calculé
    arabesque SD0:/home/x.ifs         ouvre un tracé (aussi .stl, .sca, .mid, .arb, une image BMP ou PNG)
    arabesque SD0:/apps/arabesque.app/exemples/nocturne.arb    recompose l'exemple

Construire : `wsl sh tools/office/build.sh arabesque`. Version Windows (WPF, NAudio, MeltySynth) : `C:\Projects\arabesque`,
hors du dépôt, même moteur : une même graine donne le même `.mid` octet pour octet, et un préréglage passe de l'une à
l'autre (vérifié dans les deux sens, composition en direct comprise). Les évolutions se font d'abord dans la version
Windows, puis passent ici.

## Le préréglage (.arb)

Morceau > Enregistrer le préréglage (Ctrl+Maj+S) garde de quoi recomposer le morceau à l'identique : la graine, la
gamme, le tracé au bit près (cadre compris), le style, l'image (calculée : sa sorte et sa graine ; ouverte : ses octets,
le fichier ne dépend de rien d'autre), puis le journal de la composition en direct (gamme, style, champ du formulaire,
tracé tiré à la souris, image, partie effacée, chacun daté de la pulsation où en était le compositeur), la longueur et les
retouches d'orchestre faites après coup. L'ouvrir recompose le morceau et remet les réglages de départ, graine gardée.
Sans morceau, il ne garde que les réglages. Une seule composition : les notes restées d'une précédente sur une partie
éteinte n'y sont pas.

## Les images calculées

Image > Mandala, Mandelbrot, Julia, Burning Ship, Lyapunov, Newton ; F9 : une autre de la même sorte. La graine choisit
le cadrage, les paramètres et la palette (un cosinus par composante, à phases différentes : hauteur, durée et nuances ne
bougent pas ensemble) ; l'intérieur des ensembles suit l'orbite, pour ne pas donner une plage unie. Calcul sur quatre
fils. L'application se compile sans contraction en FMA (`-ffp-contract=off`, comme pinball) : les mêmes bits que la
version Windows.

## Fichiers

* `ifs.h`, `style.h`, `song.h`, `compose.h` : le moteur, sans UIKit (il se compile et s'essaie sur le PC).
* `recipe.h` : le préréglage et son journal ; `replay.h` : le rejeu.
* `fmsout.h` : l'export FM Song pour FM Tracker (instruments lus dans `SD0:/apps/fmtracker.app/ins`).
* `audio.h` : le séquenceur et l'export WAV / MP3 (`ak_enc_*`), dans un thread qui nourrit le flux d'AudioKit.
* `image.h` : les images calculées. `main.cpp` : la fenêtre.
* `data/` : six tracés, sept styles, `default.sca`, `exemples/nocturne.arb` (piano, cordes, violoncelle, harpe,
  pentatonique de ré, sur un Mandelbrot ; sa forme est dans le journal) ; ce qu'on enregistre va dans
  `SD0:/home/arabesque`.

## Ce qu'Art Song ne dit pas, et ce qui est choisi ici

Son manuel décrit les paramètres mais pas le calcul. Déduit de ses exemples (`.stl` livrés, fichiers MIDI de démonstration
à 120 pulsations par noire) : valeurs par défaut des clés absentes, articulation fixe à 90 %, nuances
`120 - VR + c * VR / 255`, composantes 0 rouge, 1 vert, 2 bleu, `MA` absent = articulation fixe. Choisi ici : la hauteur
prend la note de la gamme d'après la composante (pas d'intervalle relatif), la durée sans rythme suit la table
1, 2, 3, 4, 6, 8 unités, toutes les parties partent de la même graine (la phase les décale), les accords restent dans la
tessiture quand elle est assez large.

## Pièges

* `ui_w_move` sur `ui_tabs` ne déplace pas ses pages : `lay_out` les déplace lui-même, sinon le fond de la page
  visible se peint à sa place de création, par-dessus l'image.
* Lancé par `startup.txt` pendant que Jet se précharge, la fenêtre attend `vfs` près d'une minute : elle s'affiche avant
  de lire ses fichiers.
* Un `ui_numeric` créé avec un minimum égal au maximum a semblé bloquer la création des widgets : le sélecteur de
  transformation est créé de 1 à 20.
