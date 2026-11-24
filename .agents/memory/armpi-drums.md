---
name: armpi-drums
description: "Boîte à rythmes (src/apps/drums, 24/11/2026) : séquenceur MIDI des 61 percussions, paquets PC drums et drums-samples publiés"
metadata:
  type: project
---

App C sur UIKit, écrite et construite dans Korin PC (pibuild), le 24/11/2026, à la demande de l'utilisateur (« comme
celle de GNOME », sortie en MIDI, apparence soignée).

- **Sortie MIDI** : notes 0x90 sur le canal 10 vers `ak_synth_*` (SoundFont du système) ; pas de sortie MIDI
  matérielle dans Korin. Thread du son à soi (rend le synthé par blocs de 64 trames, pas à la trame près,
  `g_lock` autour du synthé). Kits : la banque 128 de la SoundFont lue par `ak_sf_preset` (13 dans GeneralUser GS).
- **Grille comme Koton** (demande de l'utilisateur) : toutes les percussions du kit visibles (GM 35-81 + GS 27-34,
  82-87), une rangée chacune, par famille (en-têtes repliables), vue compacte. Signatures 3/4, 4/4, 5/4, 6/8, 12/8 ;
  1 à 8 mesures (onglets). Fichiers `.drum` texte ; export `.mid` (format 0).
- **Exemples** : `src/apps/drums/models/*.drum` (refaits par `models.py`), posés dans `/home/public/DrumMachine` par
  `tools/pc/apps.sh` et `tools/office/build.sh` ; paquet `drums-samples`. Icône : `data/icon.rgba` par
  `make_icon.py` (pas dans tools/icons/art.py : Korin n'a pas PIL).
- **Publié (PC seulement)** : drums 1.0.0 et drums-samples 1.0.0 par `pub.py --new --tree /tmp/drums-tree`
  depuis Korin PC (main 98cfcb93). Le Pi : sections dans packages.ini, rien de publié ni d'essayé.
- Non essayé à la main (pas de clic possible depuis l'agent) : menus surgissants, repli des familles, peinture à la
  souris ; le son lui-même n'a pas été écouté (le flux avance).
