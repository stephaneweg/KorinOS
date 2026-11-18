# Étude : ImageKit, des traducteurs d'images (à la façon du Translation Kit de Haiku)

Demandée le 18/11/2026. Rien n'est fait. But : **un seul endroit** où Korin lit et écrit les formats d'image, par
des traducteurs installés à part, et que toutes les apps (et UIKit) s'en servent.

## 1. Aujourd'hui

* **UIKit** (`src/kits/uikit/uk_image.c`) décode lui-même le **BMP** et le **PNG** (sans Adam7) en pixels
  0xAARRGGBB : `ui_image_load`, `ui_image_decode`. Tout autre format rend 0.
* **Les apps décodent le reste elles-mêmes**, chacune dans son binaire : Paint, Photos, ImageView et le fond d'écran
  de Réglages (`stb_image` + `simplewebp` + PCX : BMP, GIF, PNG, JPEG, WebP, PCX), Mail (JPEG, PNG, GIF, BMP),
  Letters et Slides (JPEG), Media Player (pochettes), Jet (ses bibliothèques). Les formats lisibles dépendent donc
  de l'app, et un format nouveau demande de toucher chacune.

## 2. L'image canonique : un document à calques (décision de l'utilisateur, 18/11)

Un traducteur rend toujours **un document image**, pas un simple bitmap :

| Champ | Contenu |
|---|---|
| `width`, `height` | la taille de l'image (le canevas) |
| `layers` | la liste des calques, du fond vers le dessus |
| pour chaque calque | son **bitmap** (largeur, hauteur, pixels 0xAARRGGBB), sa **position** (x, y) par rapport à l'image, et ce que le format sait en dire : nom, visible, opacité, mode de fusion (normal par défaut) |
| métadonnées (facultatif) | résolution (ppp), profil de couleur, orientation EXIF déjà appliquée |

**Le calque 0 est toujours l'image aplatie** (précision de l'utilisateur, 18/11) : de la taille de l'image, placé
en (0, 0). Les **vrais calques**, quand il y en a, suivent à partir du calque 1, du fond vers le dessus.

* Un BMP, un JPEG, un PNG… n'a que le **calque 0** (l'image elle-même, en (0, 0)).
* Un PSD (ou le format de Paint, un ORA/OpenRaster) a le calque 0 (l'image aplatie) puis ses calques 1 à n, chacun
  à sa place. Le traducteur prend l'aplatie que le fichier contient quand il en a une (le PSD en garde une), sinon
  le kit la calcule par fusion (visibilité, opacité, mode de fusion).
* Une app qui ne veut qu'une image plate (afficher, une miniature, Mail) prend **le calque 0**, sans fusion ; une
  app d'édition (Paint) prend les calques 1 à n.
* À l'écriture vers un format sans calques, seul le calque 0 part ; vers un format à calques, le traducteur écrit
  les calques 1 à n (et l'aplatie si le format en garde une, recalculée si les calques ont changé).

### 2.1 Les images animées : une liste d'images (décision de l'utilisateur, 18/11)

Le document a **deux listes** : les **calques** (ci-dessus) et les **images successives** (*frames*). Chaque image
successive est une **image plate** de la taille du document, avec sa **durée** d'affichage (ms) ; le document dit
aussi combien de fois l'animation se répète (0 : sans fin).

* Une image fixe (BMP, JPEG, PNG) : le calque 0, **aucune image successive**.
* Une image à calques (PSD, Paint) : le calque 0 aplati, les calques 1 à n, aucune image successive.
* Une image animée (GIF, WebP animé, APNG, AVIF animé) : les images successives, chacune **déjà composée** par le
  traducteur (les règles d'effacement du GIF ou de l'APNG, les rectangles partiels, sont appliquées : l'app reçoit
  des images complètes) ; le **calque 0 est la première image**, pour qu'une app qui ignore l'animation (une
  miniature, Mail, `ui_image_load`) ait quand même l'image.
* Ainsi le même format canonique couvre les images normales, les calques et les animées ; une app prend ce dont
  elle a besoin : le calque 0, les calques, ou les images successives (ImageView et Photos les jouent ; UIKit
  pourra avoir un widget d'image animée).
* À l'écriture : vers un format animé, les images successives ; vers un format fixe, le calque 0.

## 3. Le kit

* **Un nouveau kit, `ImageKit`** (`/lib/imagekit.so`, table `imagekit.abi`, API C comme les autres kits) : le
  document à calques, la fusion, la mise à l'échelle et la rotation simples, le registre des traducteurs.
* **Les traducteurs** sont des modules `.so` (`dlopen`, en place depuis le 11/11) dans `/lib/translators/image/`,
  chacun avec : reconnaître (`identify` : la signature, une note de confiance), lire (vers le document), et s'il
  sait, écrire (depuis le document, avec ses réglages : qualité JPEG, compression PNG…). Livrés de base : BMP, PNG,
  JPEG, GIF (première image) ; puis en paquets : WebP, PCX, TIFF, AVIF, HEIC, ICO, et **PSD** (calques) ; le
  format de Paint s'il a des calques (Paint en a : `src/apps/paint`).
* **Fonctions principales** : charger un fichier ou des données → document ; fusionner → image plate ; écrire un
  document dans un format choisi ; lister les formats lisibles et ceux qu'on sait écrire (pour « Ouvrir » et
  « Enregistrer sous ») avec leurs extensions et types.
* **UIKit s'appuie dessus** : `ui_image_load` et `ui_image_decode` passent par ImageKit (charger, prendre le calque 0) au
  lieu de décoder BMP et PNG eux-mêmes ; le décodeur BMP/PNG d'UIKit devient le premier traducteur (et le repli si
  ImageKit manque). Toutes les méthodes de chargement d'images du système sont ainsi au même endroit.
* **Les apps** retirent leurs copies de `stb_image`, `simplewebp` et du lecteur PCX, et appellent ImageKit (ou
  `ui_image_load` pour une image plate). Paint et Photos ouvrent alors les PSD à calques ; Mail, Letters, Media
  Player tous les formats installés.

## 4. Étapes

1. ImageKit : le document à calques, la fusion, le registre, les traducteurs BMP et PNG (repris d'UIKit), JPEG et
   GIF ; une commande d'essai (`imgconv` : lire, fusionner, écrire) jugée sur un jeu d'images de référence.
2. UIKit passe par ImageKit (`ui_image_load`, `ui_image_decode`), même résultat qu'avant sur le jeu de référence.
3. Les apps une à une : Paint, Photos, ImageView, Réglages, Mail, Letters, Slides, Media Player.
4. Traducteurs en paquets : WebP, PCX, TIFF, AVIF, HEIC, ICO, PSD (lecture des calques ; écriture plus tard).
5. Plus tard, d'autres familles sur le même modèle : le texte stylé (RTF, DOCX, ODT ; avec DocumentKit), le son
   (déjà centralisé par AudioKit et MediaKit).

## 5. Questions

1. ~~Les images animées ?~~ **Réglée le 18/11** : une liste d'images successives à côté des calques (§2.1).
2. Les modes de fusion des calques à prendre en charge d'abord (normal seul, ou aussi multiplier, superposition… du
   PSD) ?
3. Les groupes de calques (dossiers du PSD) : aplatis à la lecture, ou gardés ?
