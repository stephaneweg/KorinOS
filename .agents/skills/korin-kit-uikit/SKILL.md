---
name: korin-kit-uikit
description: UIKit, le kit des fenêtres de Korin OS (fonctions ui_*) — fenêtre et boucle d'évènements, widgets, vues dessinées, menus, dialogues, thème (jetons), plusieurs fenêtres, presse-papiers, associations de fichiers, traduction. À charger pour toute interface graphique ; la façon de l'appeler dépend du langage (korin-lang-*).
---

# UIKit : fenêtres, widgets, thème

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Le guide complet | `SD0:/apps/help.app/api/uikit_fr.md` | `src/apps/help/data/api/uikit_fr.md` |
| Un squelette d'app complet | `…/api/skeleton_fr.md`, `…/api/skeleton/main.cpp` | idem sous `src/apps/help/data/api/` |
| L'en-tête C | `/gcc/include/kits/uikit.h` (+ `uikit_abi.h`) | `src/kits/uikit/include/uikit.h` |
| La table (asm) | — | `src/kits/uikit/uikit.abi`, `uikit.inc` |

Les sections du guide sont : Ouvrir le kit, Fenêtre et boucle, Chrome, Évènements, Dessin et texte, Thème et jetons,
Widgets, Vues dessinées, Dialogues, Menus, Presse-papiers et glisser-déposer, Images, Plusieurs fenêtres, Traduction
et Pièges. **Lisez « Pièges » avant d'écrire une vue dessinée ou plusieurs fenêtres.**

## Ouvrir le kit

- **En C ou C++ :** `#include <uikit.h>`, puis `static struct ui_vars ui;` et `if (!uikit_open(&ui)) return 1;`.
  - `ui.w`, `ui.h` et `ui.font` décrivent la fenêtre courante.
  - Tous les arguments sont des `long` : un pointeur se passe en `(long) p`, un texte en `(long) "…"`.
  - On lie avec `-luikit`, ou `pibuild`, qui lie tous les kits.
- **En assembleur :** `kit_use '../../kits/uikit/uikit.abi'`, `bl kits_open`, puis `adr x0, ui_win` et `bl ui_init`.
  Les arguments vont dans `x0`… et le résultat revient dans `x0` (voir `korin-lang-asm`).

## Fenêtre et boucle

- `ui_window(w, h, titre)`, ou `ui_window_ex(x, y, w, h, titre, drapeaux)`. Les drapeaux sont `WF_RESIZABLE`,
  `WF_MODAL`, `WF_GLYPH_LIGHT` et `WF_BODY_DARK`.
- `ui_min_size`, `ui_on_resize(fn, data)`, `ui_anchor(widget, UA_*)` et `ui_metric(UMT_*)` gèrent la taille.
- **La boucle :** `t = ui_event(délai_ms)`, puis selon `t` :
  - `WEV_CLOSE` : on sort. C'est aussi « Quitter » du menu de l'app.
  - `WEV_MENU` : l'id choisi est `ui_ev_field(&ui, EV_CODE)`.
  - `t > 0` : `uikit_w_event(&code, &taken)` rend le widget touché ; `code == UN_ACTIVATE` pour un clic ou Entrée.
  - Rien de tout cela : `ui_w_idle()` ; une touche non prise : `ui_menu_key(0)`.
  - Avec des vues dessinées, `ui_view_flush()` à chaque tour.
- **Autre style :** `ui_w_on(widget, fn(widget, code), data)` donne un rappel par widget.
- `ui_w_draw(0)` puis `ui_update_all()` affichent la fenêtre.

## Widgets et dessin

- **Widgets :**
  - `ui_pushbutton(x, y, w, h, texte, UB_DEFAULT|0)` et `ui_label(x, y, w, h, texte, ULB_*)` ;
  - `ui_textbox`, avec `ui_textbox_hint`, et `ui_textarea` ;
  - `ui_list`, avec `ui_list_add`, `ui_list_clear`, `ui_list_column` et `uikit_list_get` ;
  - les listes déroulantes et les autres, décrites au guide (section Widgets, avec un exemple de formulaire).
- **Vue dessinée :** `ui_view(...)`, puis `ui_view_on(v, peindre, souris, touche, data)`. Ses coordonnées partent de
  son coin.
- **Dessin :** `ui_fill(x, y, w, h, couleur)` et `ui_text(texte, x, y, couleur, taille, police, drapeaux)` ; les
  images ont leur section dans le guide.

## Menus et dialogues

- **Menus :** `ui_menu("MFichier\nI1\tNouveau\tCtrl+N\n-\nI2\tOuvrir…\tCtrl+O\n")`.
  - `M<titre>` ouvre un menu, `I<id>[d|c]\t<texte>\t<raccourci>` est une entrée, `-` un séparateur.
  - La barre ajoute elle-même le menu de l'app.
  - `ui_menu_state` grise ou coche une entrée.
- **Dialogues :** `ui_message(titre, texte, UM_OK)`, `ui_ask`, `ui_file_open`, `ui_file_save`, `ui_folder_choose`,
  `ui_color_dialog`, et `ui_dialog_begin/button/run/cancel/end` pour un dialogue maison.
  - Ils sont **modaux et bloquent tout le programme**.
  - Lisez les widgets d'un dialogue maison **avant** `ui_dialog_end`, qui les détruit.

## Thème : obligatoire

- **Jamais de couleur en dur.** `const unsigned *jet = (const unsigned *) ui_tokens();`, puis
  `jet[TJ_x / 4] & 0xFFFFFF` : les `TJ_*` sont des décalages en octets.
- **Les jetons :** `TJ_FACE` (le fond des fenêtres), `TJ_SURFACE`, `TJ_SUNKEN`, `TJ_LINE`, `TJ_TEXT1/2/3`,
  `TJ_ACCENT`, `TJ_ACCENT_INK`, `TJ_DANGER`, `TJ_SUCCESS` et `TJ_WARNING`. `TJ_DARK` est un drapeau, pas une couleur. `TJ_HOVER` : le survol ; `TJ_AXIS_X/Y/Z` : les axes d'une vue 3D.
- **Les nuances :** `ui_tone`, `ui_mix` et `ui_ink`.
- **Le chrome :** il n'y a pas de barre de titre. Laissez libres les `FRAME_CAP_W` pixels en haut à droite (les
  boutons de la fenêtre) ; les 32 px du haut du contenu déplacent la fenêtre.
- **Avant de livrer**, essayez Givre (clair) et Onyx Noir (sombre).

## Plusieurs fenêtres, fichiers, langue

- **Plusieurs fenêtres :**
  - chaque `ui_window_ex` devient la fenêtre courante ; `ui_window_use` en change ;
  - `EV_WIN` dit laquelle a reçu l'évènement ; sur `WEV_CLOSE`, appelez `ui_window_close(win)` ;
  - un seul menu par app : celui de la première fenêtre.
- **Fichiers :** `ui_assoc_app(chemin, buf, taille)`, `ui_assoc_open(chemin)`, `ui_assoc_set(ext, app)` et
  `ui_open_with(chemin)`. Fichiers passe le chemin à l'app en `argv`.
- **Traduction :** `ui_tr_load("SD0:/apps/<app>.app/lang-<ui_lang()>.txt")`, puis `ui_tr("texte français")` ;
  la clé est le texte français.
- **Aide :** un `help_fr.md` dans le dossier de l'app fait ajouter un menu Aide par la barre.

## Exemple

La plus petite app, aux couleurs du thème, est dans `korin-lang-c/template/main.c` : c'est l'exemple vérifié de
`app_fr.md`.
