---
name: armpi-relics
description: "Relics, jeu de plates-formes et d'exploration à la Rick Dangerous (19/11/2026) : C sur gamefb, 7 mondes, 21 niveaux générés, musique par monde, sprites et tuiles des paquets achetés par l'utilisateur, banc d'essai sur l'hôte"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-19T21:40:00.000Z
---

Demandé le 19/11 : « un superbe jeu en side scrolling, comme Rick Dangerous, une dizaine de niveaux, pièges, ennemis ».
`src/apps/relics` (C, comme Arkanoid/Bubbles : gamefb.h, gamedraw.h, gamepause.h, gamesound.h ; 640 x 384 mis à
l'échelle). Nom original (pas de reprise de Rick Dangerous). Sur la branche `claude/gifted-maxwell-bhyryb`.

**Why:** l'utilisateur a demandé en cours de route (avec des captures de Rick Dangerous 1 et 2 et un tileset « forest
platformer » comme inspiration, non repris) : des **tuiles** dessinées au trait plutôt qu'un rendu calculé, et un
**personnage plus grand** par rapport au décor (environ une tuile). D'où : cases de 20 unités de jeu grossies de 1,6
(tuiles de 32 points, 20 x 12 visibles), personnages dessinés à 1,8, trait de 2 points.

**How to apply:**
- Niveaux : `tools/relics/mklevels.py` génère `levels.h` (ne pas éditer levels.h) ; légende en tête de levels.h.
- Vérifier (le texte des images : FreeType et les polices Inter de sdcard/fonts, pas des pavés) : `sh tools/relics/run.sh check` (parcours en largeur par la vraie physique : sortie et objets atteignables),
  `shot n out.png "R30 J40"` (image ; `RELICS_AT=tx,ty`), `fuzz` (12 niveaux au hasard). Contraintes : marche de
  2 cases, trou de 3 cases, 4 cases au-dessus du sol pour un saut entier.
- Pas essayé dans Korin : sous QEMU raspi4b dans le cloud (4 cœurs), même la carte d'origine de `carte_linux.py`
  s'arrête à « / non servi » (fatfs ne répond pas) ; à lancer sous QEMU ou sur le Pi chez l'utilisateur.
- Intégré : listes de `tools/office/build.sh` et `tools/pc/apps.sh`, `[app.relics]` des deux packages*.ini, icône dans
  `tools/icons/art.py`. Pas publié (versions.ini : 1.0.0 par défaut).

**Suite du 19/11 :** cinq mondes (jungle, désert « Les sables de Kharga » demandé par l'utilisateur avant le temple,
pyramide, glace, volcan), 15 niveaux. Personnages : « Chibi Character Bundle » acheté par l'utilisateur (Google Drive),
l'explorateur fait par `tools/relics/explorer.py` (morceaux + Spriter, `spriter.py`), ennemis par `sprites.sh` ;
tuiles : tileset « forest » de l'utilisateur pour la jungle, les autres mondes DESSINÉS par `mktiles.py` dans son style
(l'utilisateur ne veut pas de simple recoloration ; il a validé ces dessins ; le désert suit son image de référence :
pavés bruns, sable lisse, figuiers de Barbarie, panneaux). Les paquets restent hors du dépôt : seuls les `.spr` et
`.til` (data/sprites, data/tiles) y sont. L'utilisateur peut acheter d'autres tilesets : ils remplaceront les dessins
de `mktiles.py` monde par monde.

**Publié pour le PC le 19/11** (main e8e0700, demande de l'utilisateur) : relics 1.0.0 dans packages/pc (115 paquets),
publication partielle (scratchpad pub_relics.py : mkrepo.plan sur build/pc/sysroot, write_opk, section ajoutée,
index re-signé, checkrepo ; versions.ini [pc] relics = 1.0.0). Le code de Relics n'est pas encore dans main (branche
claude/gifted-maxwell-bhyryb). Le Pi n'est pas publié. Essayé avant dans Korin PC (QEMU q35, NVMe d'essai, sysroot
minimal : tools/pc/build.sh, sysroot.sh, sysroot_apps.sh, mkkorindisk) : lanceur, titre, partie, tir, dynamite.
Pièges vus là : lire les fichiers d'un bloc (chaque read est un aller-retour IPC) ; retenir les frappes brèves.
Dans cet environnement du cloud, la clé de signature est la variable **ONYX_PKG_KEY** (pas OnyxPkgKey) ; vérifiée par
booléens seulement (PEM, correspond à onyx.pub).

**Ajouts du soir (19/11, demandés par l'utilisateur)** :
- Musique de fond « tendue, qui presse » : `music.h` (thread, synthé General MIDI d'AudioKit), une piste par monde +
  titre (piste = 1 + monde) ; `tools/relics/music.c` écrit les pistes en .mid (écoute : fluidsynth sur l'hôte).
- Échelle vue de dos (explorer.py `back_climb`, 4 images, mains derrière la tête : demandé ainsi).
- Rochers de 2 cases ; `escape()` de mklevels.py = fuite à la Rick (couloir de 2 cases, fosse de 2 où il tombe) dans
  un niveau par monde ; le rocher revient après une chute. `run.sh chase n tx ty script` rejoue une fuite.
- Mondes 5 « Le bois des Ombres » (forêt hantée) et 6 « La vallée de la Mort », d'après une capture de l'utilisateur ;
  dans l'ordre du voyage avant le volcan (niveaux 13-18) ; « Niveau x - y » vient du rang, pas du monde. Tuiles et fonds
  dessinés dans mktiles.py (`spooky`, `valley`), ennemis : Pumpkin Head Guy, Vampire, Hooded Girl ; Barbarian, Thug,
  Indian Tribe Knight. BANK 64 Mo (33 pris au départ).
