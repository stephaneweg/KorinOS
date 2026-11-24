---
name: armpi-fichiers-acces-rapides
description: Accès rapides de Fichiers (24/11/2026) — Accueil, Musiques, Images, Téléchargements, Projets avant Applications, glyphes glyph-place-* à la GNOME
metadata:
  type: project
---

Demande de l'utilisateur du 24/11/2026, branche claude/lucid-wright-6o2o4b, fusionnée dans main le 24/11.

- `skel/sd0/etc/places.ini` : Accueil (HOME:/), Musiques, Images, Téléchargements (HOME:/downloads), Projets
  (HOME:/projects), puis Applications, Documents, Vidéos.
- Icônes : `tools/icons/art.py` (table PLACES) rend `glyph-place-{home,music,pictures,downloads,projects,documents,
  videos,apps,folder}` (projects : équerre et crayon parallèle à l'hypoténuse, demandé le 24/11 ; au trait, comme les icônes symboliques de Nautilus) ; `src/apps/files/main.c` `place_glyph`
  choisit d'après le dossier et dessine par `ui_glyph` (20 px, jeton TEXT2) ; un dossier épinglé quelconque a
  `glyph-place-folder`.
- Fichiers crée au démarrage les dossiers types manquants de HOME:/ (`std_dirs_make` : music, pictures, downloads,
  projects, documents, video), comme xdg-user-dirs : sinon un accès rapide vers un dossier absent n'est pas montré
  (le home/ du PC est vide).
- `PL_MAX` 16, `ID_PLACE` passé de 20 à 100 (20 + 10 touchait ID_EJECT 30).
- Piège : un HOME:/etc/places.ini personnel (créé dès qu'on épingle ou retire un accès) masque celui de /etc : il
  garde l'ancienne liste. Le supprimer (ou l'éditer) pour voir la nouvelle.
- Pas essayé sous QEMU ni sur le Pi au moment de l'écriture ; paquets (armpi, pc) pas publiés.
