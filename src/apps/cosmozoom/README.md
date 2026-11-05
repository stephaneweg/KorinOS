# Cosmo Zoom! sur ARMPI

Le jeu de course anti-gravité d'Onyx (`user/Apps/cosmozoom`), sans pont : 13 circuits, 4 coupes, 8 pilotes, vaisseaux à
composer au hangar, objets, Grand Prix, course simple, contre-la-montre avec fantôme. Application
`SD0:/apps/cosmozoom.app`, tiroir Jeux du dock. Hors du selfhost, comme la suite bureautique.

    sh tools/office/build.sh cosmozoom          # (WSL) -> build/office/sdroot/apps/cosmozoom.app
    pibuild app /src/apps/cosmozoom cosmozoom   # sous ARMPI, par le GCC de la carte

Sauvegarde : `SD0:/home/cosmozoom/` (`save.ini`, `ghosts/`). Lancement d'essai :
`main [demo] [auto] [fps] [players=N] [seed=N] [race=<circuit>] [screen=menu|hangar|cups|options]`.

## Assembleur ou C++

**Le jeu reste en C++, sa partie coûteuse est en assembleur dans GpuKit.**

Le jeu fait 6 700 lignes de C++ dense : physique et pilotes de l'ordinateur en flottants, circuits, génération du monde,
lecteur d'OBJ et lissage des normales, gabarits et lambdas. En asm il en faudrait plusieurs dizaines de milliers, pour un
gain nul : une image passe l'essentiel de son temps à transformer et à dessiner, pas dans la logique. La physique doit
aussi rester celle d'Onyx, circuit par circuit. Le code du jeu (`math3d.h`, `mesh.h`, `parts.h`, `track.h`, `race.h`,
`world.h`, `scene.h`, `textures.h`, `draw2d.h`) est donc repris tel quel.

Ce qui gagne à l'asm va dans **`gk_draw`** (`src/kits/gpukit/draw.asm`, [V3D.md](../../../docs/V3D.md)) : transformation et
découpe de tous les sommets, puis GPU, sinon rastériseur du processeur. Tout programme peut s'en servir.

## Écrit pour ARMPI

* **Fenêtre** (`main.cpp`) : `ui_window_ex`, `ui_menu`, pixels de la fenêtre dessinés en place. L'image suivante attend
  `WEV_FRAME` : sans cela Elegant montrerait une image à moitié dessinée.
* **Texte** (`ui.h`) : `ui_text` avec les polices de UIKit, gras italique compris.
* **Clavier** : l'état des touches vient des évènements. Une frappe brève compte, même si l'image dure longtemps. Les
  lettres suivent la disposition du clavier.
* **Souris** (ajoutée, Onyx n'en a pas dans ce jeu) : clic sur ce que montrent les menus, molette pour monter et descendre.
* **Son** (`audio.h`) : `ak_synth_*` pour la musique, dans un thread qui charge la SoundFont et nourrit le flux, la fenêtre
  n'attend donc pas ; `ak_fm_*` pour les effets.
* **Fichiers** (`sys.h`) : POSIX et un petit lecteur `clé = valeur`, à la place de `fk_kv`.
* **Thème** : jetons de `ui_jetons` pour les panneaux, leur texte et le voile des menus. Sur panneau clair, un accent se
  rapproche de l'encre du thème. Restent fixes le monde du jeu, le ciel du hangar et les couleurs d'accent des choix.

## Différences avec Onyx

* **Plein écran**, toujours : fenêtre sans cadre au-dessus de tout, de la taille de l'écran (`ga::fullscreen` de
  `src/inc/gameart.h`, comme les émulateurs) ; elle suit un changement de résolution (`WEV_SCREEN`). Les entrées 800 x 600
  et 1024 x 576 du menu Affichage sont retirées, la bascule F / F9 aussi.
* **Pause** : le menu commun des jeux (`ga::Pause`, le même que `gamepause.inc` des jeux en asm) : Échap, P, Start ou le
  bouton || en course ; Reprendre, Recommencer, Quitter (le jeu se ferme). La perte du focus en course l'ouvre aussi.
* **Manettes** (`WEV_PAD`) : la manette N pilote le joueur N ; la manette 0 (le clavier mappé par HIDManager) est écartée,
  sauf construction d'essai `-DGP_PAD0` (QEMU).
* **Processeur** : à demi-taille puis doublé, comme Onyx. Mélanges 0 à 4 seulement (tout ce que le jeu emploie).
* Le mode `sharp` (taille entière sans GPU) est retiré.

## Vérifié (QEMU raspi4b, captures lues)

* Thème clair par défaut, Onyx Noir, Milk. Accueil, menu, options, hangar, choix du circuit, course, pause, plein écran
  aller et retour, chargement à plein écran.
* Clavier et souris dans les menus, accélération, pause. Son : signal lu dans le `.wav` de QEMU une fois la SoundFont chargée.
* Rastériseur asm comparé à `bas3d.h` d'Onyx sur trois images de course de 480 x 270 : 129 600 pixels chacune, 2 pixels en
  tout à plus de 16 d'écart, le reste identique ou à ±14 au plus.
* `gk_draw` vers `gk_render` en listes à blanc (`GK_DBG_DRY`) : 240 images de Skyline, jusqu'à 180 978 sommets et 93 lots,
  aucun échec. `gputest` PASS, `gputest lists` identique à `tools/v3d_ref/gk_ref.cpp`.
* `pibuild app /src/apps/cosmozoom cosmozoom` par le GCC natif sous QEMU : moins de 25 minutes, `data/` et ses sous-dossiers
  installés, binaire de 529 480 octets comme la chaîne croisée, qui s'ouvre sur le menu. Demande `/gcc/bin/make` (`tools/gcc/make.sh`).
* 3 à 5 images/s sous QEMU, moins quand d'autres QEMU tournent : sans rapport avec le vrai Pi.

## Non vérifié

**Rien n'a tourné sur le vrai Pi.** Le rendu par le GPU n'a jamais été vu : ni les pixels, ni la vitesse. Onyx non plus ne
connaissait pas cette vitesse (50 à 120 k sommets par image, 180 k mesurés ici). Si c'est trop lent : moins de tours
(`city =` dans le `.trk`), distance de vue plus courte (`visible` de `Scene::view`). Mesure : F12 ou `fps`. Pas essayé non
plus : les coupes jusqu'au podium, plusieurs joueurs, le fantôme relu, la sauvegarde après redémarrage.
