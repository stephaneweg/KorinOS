# arabesque : la composition algorithmique par fractales

Un chemin calculé (un système de fonctions itérées, IFS) parcourt une image ; la couleur de chaque point visité devient
une note de l'une des dix parties General MIDI, qui jouent en direct sur le synthétiseur d'AudioKit. L'idée est celle
d'**Art Song** de David T. Strohbeen (shareware pour Windows 3.1, 1996) ; le programme est écrit ici, sans son code ni ses
fichiers, et lit ses formats `.ifs`, `.stl` et `.sca`. Le nom n'est pas repris : « Art Song » était une marque.

    arabesque                         le premier tracé et le premier style livrés, sur un mandala calculé
    arabesque SD0:/home/x.ifs         ouvre un tracé (aussi .stl, .sca, .mid, .arb, une image BMP ou PNG)
    arabesque SD0:/apps/arabesque.app/exemples/nocturne.arb    recompose l'exemple

Construire : `wsl sh tools/office/build.sh arabesque`. Version Windows (WPF, NAudio, MeltySynth) : un projet à part, hors de ce dépôt,
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

## La grille d'accords

Sous la séquence, une case par mesure, à la même échelle (molette : zoom, Maj+molette : défiler ; huit mesures au
départ). Un clic ouvre la fenêtre Accord de la mesure (aussi Composition > Accord de la mesure, F2) : degré, altération,
qualité, sus, septième, couleurs (9, 11, 13 et altérations), basse (renversement ou voice leading), open voicing ; chaque
clic s'écrit dans la case et fait entendre l'accord. Une case vide prolonge l'accord d'avant, « Sans accord » rend la
composition libre ; le trait de couleur marque la fin de la grille (à tirer), qui boucle si on veut. Chaque partie suit
la grille selon son réglage Harmonie (Style > Algorithme) : l'accord aux temps et des notes de passage entre, l'accord
seul, sa basse aux temps, ou libre. Sans grille, un morceau est composé note pour note comme avant. La grille est dans
le préréglage (`chords=`, `chord=`), Harmonie dans le style (`Har=`, écrit seulement s'il n'est pas nul).

Composition > Cadence... remplit la grille d'après une cadence, comme dans Koton : un style parmi trente (parfaite,
plagale, ii-V-I, pop, andalouse, blues, cycle des quintes, Coltrane, baroque...), le nombre de mesures, le degré de
départ, le voice leading, et « Autre tirage » pour une autre graine. Chaque réglage réécrit la grille ; Annuler rend
celle d'avant. Mêmes tirages que Koton et que la version Windows (`cadence.h`, le `System.Random` de .NET refait).

Pendant la lecture et la composition, l'image s'assombrit : chaque note s'allume au point d'où elle vient, puis dévoile
pour de bon les couleurs autour ; à l'arrêt, ce qui est dévoilé reste jusqu'à un clic sur l'image.

## Les images calculées

Image > Mandala, Mandelbrot, Julia, Burning Ship, Lyapunov, Newton, Rayons ; F9 : une autre de la même sorte. La graine
choisit le cadrage, les paramètres et la palette (un cosinus par composante, à phases différentes : hauteur, durée et
nuances ne bougent pas ensemble) ; l'intérieur des ensembles suit l'orbite, pour ne pas donner une plage unie. Rayons,
dans l'esprit de l'image de départ d'Art Song : des bâtonnets de couleur en anneaux, chaque rayon se divisant en deux
tous les deux anneaux. Calcul sur quatre fils. L'application se compile sans contraction en FMA
(`-ffp-contract=off`, comme pinball) : les mêmes bits que la version Windows.

Image > Réglages de l'image (Ctrl+I) : pour Rayons, le nombre de rayons, d'anneaux, la saturation, la luminosité et un
dégradé vers l'extérieur. Un réglage « à la graine » montre la valeur tirée ; le bouger le règle à la main (le tirage a
lieu quand même : régler une chose ne change pas les autres). Les réglages vont dans le préréglage (`set=`, seulement
s'il y en a). Les autres sortes n'ont pas encore de réglages.

## Les règles d'Art Song

Lues dans ARTSNG20.EXE (Delphi 1, Windows 16 bits), et suivies ici comme dans le port Windows :

* le motif rythmique (`DR`) se joue dans l'ordre, en boucle ; sans motif, la durée suit les bits bas de la couleur
  (8 unités, sinon 1 à 7, sinon 1) ;
* la vélocité va de 127 − VR à 127 (`VR` absent d'un .stl : 20) ;
* la hauteur bouge par degrés depuis la note précédente : le pas vaut à peu près (127 − c) / 255 de la tessiture (au
  plus une octave de la gamme), 127 garde la note, plus sombre monte, plus clair descend ; trop haut, la note redescend
  d'autant sous le haut ; sous le bas, la partie se tait jusqu'à ce que la couleur la fasse remonter. Le pas part de
  la note réellement jouée, après la grille d'accords.

## La forme

Une suite de sections, une lettre chacune (`form=A B A* C A**`) : une lettre nouvelle se compose dans sa région de
l'image (une fractale y est recalculée à ce cadrage), une lettre déjà venue est recopiée, une étoile en fait une
variation au même rythme, d'autres hauteurs. Chaque lettre a sa longueur, sa région, sa tonalité (`section=`) et
peut avoir son propre style (`[Letter]` dans le préréglage : instruments, rythmes, liaisons, parties allumées, au
début de chacune de ses sections). La forme finie, la composition s'arrête. Sans forme, rien ne change.

Pour l'instant, la forme vient d'un préréglage fait dans la version Windows : Korin la rejoue, la garde pour
recomposer et l'enregistre, mais n'a pas encore la bande des sections ni la fenêtre d'une lettre.

## Fichiers

* `ifs.h`, `style.h`, `song.h`, `chords.h`, `compose.h` : le moteur, sans UIKit (il se compile et s'essaie sur le PC).
* `recipe.h` : le préréglage et son journal ; `replay.h` : le rejeu.
* `fmsout.h` : l'export FM Song pour FM Tracker (instruments lus dans `SD0:/apps/fmtracker.app/ins`).
* `audio.h` : le séquenceur et l'export WAV / MP3 (`ak_enc_*`), dans un thread qui nourrit le flux d'AudioKit.
* `image.h` : les images calculées et leur cadrage ; `form.h` : la forme, ses régions et ses images. `main.cpp` : la fenêtre.
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
