# Morpho

L'animal de compagnie qui évolue, à la manière des Tamagotchi : un petit être en 3D à nourrir, faire jouer, laver,
soigner, coucher et câliner. Il vit en temps réel (même l'app fermée) de l'œuf à l'âge d'ancien, et sa forme change à
chaque stade. Jeu original : les créatures sont **paramétriques**, sur le principe de celles de Spore (un corps sur une
colonne, des pièces posées dessus), sans en reprendre aucune.

L'app est un « studio » (direction proposée par Gemini à partir de `tools/morpho`, le 20/11) : la vue 3D, sombre dans les
deux thèmes, est encadrée au milieu de la fenêtre, sans décor (un dégradé, un halo, un anneau de socle, l'ombre) ; autour,
aux jetons du thème : l'en-tête (nom, stade, caractère, Fiche), les cinq jauges en cartes, la rangée des six soins. La
fiche glisse en tiroir depuis la droite de la vue ; messages et bulle restent dans la vue, en verre fumé.
La fenêtre s'ouvre en **portrait** (jauges sur deux colonnes, soins en grille de 3 × 2) ; élargie, elle passe à la mise
en page large (jauges et soins en une rangée chacun). Le fond de la vue : un dégradé (ardoise bleutée en haut, presque
noir en bas), un vignettage doux, un halo derrière l'animal.

    sh tools/pc/apps.sh morpho                 # Korin PC (avec GlKit construit : le GPU Intel, sinon le processeur)
    sh tools/office/build.sh morpho            # le Pi (gk_draw : le V3D, sinon le processeur)
    sh tools/morpho/run.sh gallery 4 1 4 3 out.png   # sur l'hôte Linux : des créatures adultes au hasard
    sh tools/morpho/run.sh shot out.png MORPHO_ARGS="new stage=3 hour=19" MORPHO_DO="1:feed 1.5:meal"
    sh tools/morpho/sheet.sh out.png "titre|VAR=... VAR=..." ...   # une planche de scénarios
    sh tools/morpho/run.sh sound DOSSIER         # la musique et les bruitages en .wav (AudioKit pour l'hôte, banque FM)

Traduction : `data/lang-en.txt`, posé dans `/apps/morpho.app/` et lu par l'app (`ui_tr_load`) : le paquet est complet seul.
Sauvegarde : `HOME:/morpho/pet.ini`. Essais : `main new` (nouvel œuf), `speed=N`, `hour=H`, `stage=N`, `seed=N`,
`trace` (les clics au journal).

## Les fichiers

| Fichier | Rôle |
|---|---|
| `genome.h` | le génome : plan du corps (ordinaire, serpent, carapace), une à trois têtes, corps, membres, face, dos, ailes, couleurs, caractère ; `evolve` tire le stade suivant d'après le précédent et les soins ; `blend` (la métamorphose) |
| `creature.cpp` | la créature : surface balayée le long d'une colonne (sections elliptiques lissées ; le serpent y ondule), les têtes de plus sur les épaules (leur propre cou, la même face), la carapace en dôme à écailles, pièces posées par lancer de rayon contre une distance signée, jambes et bras en cinématique inverse à deux os ; la texture de la peau (motifs) et de la coquille |
| `mesh.h` | maillages indexés et primitives : ellipsoïdes, tubes lissés (repères transportés, bouts arrondis), feuilles à deux faces |
| `render.cpp` | éclairage par sommet (lumière de studio qui suit la caméra, ciel et sol, reflet, contre-jour) ; l'heure ne fait que teinter le fond et la lumière (nuit plus bleue, aube et crépuscule plus chauds, lampe éteinte : lune) ; le fond 2D (dégradé tramé, halo, socle) |
| `scene.cpp` | le dessin par `gk_draw` dans le cadre de la vue ; au processeur, demi-taille quand une image dépasse 45 ms |
| `pet.cpp` | la vie : besoins, sommeil, crottes, maladie, poids, stades, départ ; rattrapage du temps passé app fermée (plus doux, au plus 3 jours) ; sauvegarde |
| `actor.cpp` | ce que fait l'animal (flâner, regarder, manger, jouer à la balle, dormir, bain, médicament, câlin, éclosion, métamorphose) -> une pose ; les objets 3D et les particules |
| `hud.cpp` | la mise en page (`layout` : portrait ou large, la place de la vue ; compacte quand la place manque, des icônes seules) et l'interface 2D aux jetons du thème : en-tête, jauges, soins à icônes dessinées, choix du repas, tiroir de la fiche, messages, bulle, cartes des grands moments |
| `sound.h` | le son, dans un fil à lui, sur le synthé General MIDI d'AudioKit : une musique générée (grille, nappe, basse, harpe, mélodie pentatonique en motifs variés) qui suit l'humeur, des bruitages en couches (voix d'ocarina, percussions, glockenspiel, harpe, chœur) ; un limiteur doux |
| `main.cpp` | fenêtre, boucle (30 images/s au plus, `WEV_FRAME`), affichage en double tampon (surface de deux images, `ui_surface_source`), souris, clavier, menus |

## Les stades

Œuf 90 s, bébé 1 h, enfant 8 h, adolescent 1 jour, adulte 4 jours, ancien 3 jours (temps simulé ; menu Rythme : ×10,
×60). À la fin, l'ancien s'envole et laisse un œuf (la génération suivante garde un peu de ses couleurs) ; laissé trop
longtemps à santé nulle, il part plus tôt.

## Limites, à essayer

- Essayé sous QEMU q35 (Korin PC, processeur : environ 3 images/s, demi-taille) : lancement, œuf, éclosion, cartes,
  menu des repas, repas. **Pas essayé** sur le GPU Intel du ZBook ni sur le V3D du Pi : à voir (rendu, vitesse,
  textures de `gk_texture`).
- Le ciel suit l'heure de Korin (UTC + le décalage de `/etc/clock.cfg`), pas encore `korin.clock.offset` du bus.
