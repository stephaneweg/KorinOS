---
name: armpi-pdf-lecteur
description: "Lecteur PDF (24/11) : polices standard cassées par les options FreeType d'Onyx, fenêtre qui ne se déplaçait pas (barre d'onglets)"
metadata:
  type: project
---

Signalé par l'utilisateur le 24/11/2026 avec un devis scanné (Adobe Scan : un JPEG progressif plus un texte d'OCR invisible en Times-Roman non incorporée).

* **Rien ne se dessinait** : `tools/pdf/mu/armpi_ftoption.h` (repris d'Onyx) coupait `FT_CONFIG_OPTION_POSTSCRIPT_NAMES`. Sans lui le module psnames n'a pas de service, et le pilote CFF refuse les polices de base de MuPDF (les Nimbus de URW sont des CFF) : « FT_New_Memory_Face: missing module ». L'erreur fait échouer toute la liste d'affichage, donc la page entière, l'image du scan comprise. Remis, avec `FT_CONFIG_OPTION_ADOBE_GLYPH_LIST` et `TT_CONFIG_OPTION_POSTSCRIPT_NAMES`, comme dans la configuration réduite de MuPDF (`scripts/freetype/slimftoptions.h`). Touchait tout PDF à police standard non incorporée.
* Vérifié ici en construisant `mupdf.mk` avec le gcc de l'hôte, sur les sources publiques de MuPDF 1.28.5 (même FreeType 2.14.3) : la page se dessine. Ni sous QEMU ni sur le Pi.
* `mupdf.mk` : les objets de FreeType dépendent maintenant de `armpi_ftoption.h` et de `armpi_muftmodule.h`. Avant, changer ces fichiers ne recompilait rien.
* **Déplacement** : en haut de la fenêtre il n'y a que le `ui_tabstrip`, et `UT_TABSTRIP` n'avait pas `VTF_MOVE`, donc l'appui allait aux onglets. Le drapeau est maintenant ajouté dans UIKit (C et asm), ce qui vaut aussi pour QBStudio et WebStudio.

**How to apply:** pour reconstruire : `tools/kits/build_c.sh uikit`, `tools/pdf/build.sh` (et `ARCH=x86_64` pour le PC), `tools/office/build.sh pdf` / `tools/pc/apps.sh pdf`. À la publication, `--bump` monte pdf et le paquet système.
