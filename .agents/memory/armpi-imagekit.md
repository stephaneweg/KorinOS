---
name: armpi-imagekit
description: "Au plan le 18/11 : ImageKit, traducteurs d'images à la Haiku ; image canonique = document à calques (taille, calques avec bitmap et position ; JPEG = 1 calque en 0,0) ; UIKit s'appuie dessus ; docs/etude/IMAGEKIT.md, rien de fait"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026), après une explication du Translation Kit de Haiku. Décision : un traducteur
rend une **image canonique à calques** (taille de l'image ; liste de calques, chacun avec son bitmap 0xAARRGGBB et
sa position dans l'image) ; BMP, JPEG, PNG… = un seul calque en (0, 0) ; PSD et le format de Paint en ont plusieurs.
Dans un kit adéquat (proposé : nouveau kit ImageKit, traducteurs .so dans /lib/translators/image/), et **UIKit
s'appuie dessus** pour centraliser le chargement d'images (`ui_image_load` = charger puis fusionner ; le décodeur
BMP/PNG d'UIKit devient le premier traducteur). Les apps retirent leurs copies de stb_image/simplewebp/PCX.
Trois questions en attente (images animées, modes de fusion, groupes de calques).

**How to apply:** relire docs/etude/IMAGEKIT.md avant de commencer. Voir [[armpi-dlopen]], [[armpi-ports-uikit-os]].
