---
name: armpi-idee-presse-papier-dnd
description: Pour plus tard (14/11) : historique du presse-papier dans l'applet, coller une image (Telegram), glisser-déposer du gestionnaire de fichiers vers les apps
metadata:
  type: project
---

Demandes de l'utilisateur du 14/11/2026 (retours du ZBook), **pour plus tard** :
- applet « presse-papier » du panneau : un clic affiche les éléments copiés (historique), comme dans Onyx ;
- Ctrl+V d'une image dans Telegram (et les autres apps) : vérifier que le presse-papier porte les images, sinon l'ajouter à UIKit ;
- glisser-déposer depuis le gestionnaire de fichiers vers les apps : image déposée dans Telegram → envoyée dans la conversation ; fichier déposé sur une app → elle l'ouvre.

Glisser-déposer (précision du 14/11) : un message à l'app avec le chemin, l'app décide. Le protocole existe (WS_OP_DRAG / WEV_DROP / ui_drop_get, UDT_FILES) et Telegram, Koton, letters, notes, paint… reçoivent déjà ; il manque le glisser UDT_FILES depuis la liste de src/apps/files.

**Why:** confort d'usage du bureau, comme dans Onyx.

**How to apply:** noté dans docs/etude/PC-PILOTES.md (« Pour plus tard ») ; c'est UIKit/Elegant (communs Pi et PC), pas une app seule. Voir [[armpi-association-fichiers]], [[armpi-mail-irc-telegram]].
