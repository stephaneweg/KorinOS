# UIKit : l'interface

UIKit est le kit de l'interface graphique de Korin OS : il ouvre les fenêtres (servies par Elegant, le serveur de fenêtres), rend les événements du clavier, de la souris et de la barre de menus, dessine (rectangles, coins arrondis, formes anticrénelées, texte TrueType) et fournit les widgets : boutons, champs, listes, arbres, onglets, barres d'outils, curseurs, vues dessinées à la main, canevas et surfaces. Il apporte aussi les dialogues (message, fichiers, couleur, dialogues à soi), les menus de la barre de menus, le presse-papiers, le glisser-déposer, les images BMP et PNG et la traduction des textes.

Tout est dessiné aux couleurs du thème du bureau (Givre, Onyx Noir...) : une app qui dessine elle-même prend ses couleurs dans les **jetons** du thème, jamais des couleurs écrites en dur.

> **Remarque** : UIKit est écrit en assembleur (`SD0:/lib/uikit.so`). Depuis le C, chaque fonction prend des `long` : un pointeur se passe converti, `(long) p`, et un texte rendu par le kit se relit par `(const char *)`.

## Ouvrir le kit

```c
#include <uikit.h>                      // uikit_abi.h (prototypes, constantes) + struct ui_vars et les aides uikit_*

static struct ui_vars ui;               // la fenêtre courante : poignée, pixels, taille, dernier événement, polices

// au début du programme :
	if (!uikit_open (&ui)) return 1;    // le kit ouvert (et AppKit qu'il prend), les polices chargées
```

- `uikit.h` est dans `/gcc/include/kits` ; rien à ajouter au lien : `pibuild` prend toutes les bibliothèques `/gcc/lib/lib*.a`, dont les relais de UIKit.
- `struct ui_vars` contient `win` (poignée de la fenêtre courante), `surf` (ses pixels, `0x00RRGGBB`, pas = `w * 4`), `w`, `h`, `ev` (le dernier événement) et les trois polices `font` (Inter), `font_b` (Inter SemiBold), `font_m` (DejaVu Sans Mono), à passer aux fonctions de texte par `(long) ui.font`.
- `ui_ev_field (&ui, EV_CODE)` lit un champ 32 bits de l'événement.
- Les fonctions qui rendent plusieurs valeurs ont une aide dans `uikit.h` : `uikit_w_event`, `uikit_list_get`, `uikit_drop_get`, `uikit_list_sorted`, `uikit_text_hit`, `uikit_textbox_selection`, `uikit_textarea_caret`, `uikit_canvas_pixels`, `uikit_image_size`. Pour les autres (`ui_tree_get`, `ui_clip_take`, `ui_clip_get_image`...), `uikit_call3` lit les deuxième et troisième résultats : `uikit_call3 ((long (*) ()) ui_tree_get, arbre, id, 0, 0, &donnee, 0)`.

## La fenêtre et la boucle d'événements

Une app crée sa fenêtre, puis tourne dans une boucle : `ui_event` attend un événement, `uikit_w_event` le donne au widget concerné, et le programme réagit à ce qui lui revient.

```cpp
for (;;)
{
	long t = ui_event (200);                     // attend 200 ms au plus -> type (0 : rien)
	if (t == WEV_CLOSE) break;                   // case de fermeture, ou Quitter du menu de l'app
	if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
	else if (t > 0)
	{
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);  // w : le widget, code : UN_*, taken : 1 s'il a pris l'événement
		if (code) on_widget (w, code);
	}
	else ui_w_idle ();                           // rien pendant l'attente : les infobulles
	ui_view_flush ();                            // redessine les vues marquées par ui_view_dirty
}
```

### Le chrome de Korin OS

Les fenêtres n'ont pas de barre de titre : Elegant dessine le cadre et trois boutons (réduire, agrandir, fermer) **en haut à droite du contenu**, sur `FRAME_CAP_W` pixels de large. Rien d'interactif ne doit s'y trouver : laissez cette place vide (ou n'y mettez qu'un dessin). Les 32 px du haut du contenu déplacent la fenêtre dès que la souris bouge de 8 px pendant un appui ; un simple clic arrive normalement au programme ou au widget.

### Fonctions de la fenêtre

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_init` | variables (`&ui`) | 1 si les polices sont chargées (`uikit_open` l'appelle) |
| `ui_window` | largeur, hauteur, titre | poignée (≤ 0 : échec) ; fenêtre placée par Elegant |
| `ui_window_ex` | x, y (-1, -1 : automatique), largeur, hauteur, titre, rôle `WF_*` | poignée ; `WF_RESIZABLE` : redimensionnable |
| `ui_title` | titre | change le titre (montré par la barre de menus et le panel) |
| `ui_close` | | ferme la fenêtre courante (ses widgets restent : préférer `ui_window_close`) |
| `ui_screen` | tampon de 4 `long` | largeur et hauteur de l'écran, place réservée en bas puis en haut (barre de menus) |
| `ui_event` | attente en ms | type `WEV_*` (0 : rien), l'événement dans `ui.ev` |
| `ui_update` | x, y, largeur, hauteur | montre une zone dessinée dans `ui.surf` |
| `ui_update_all` | | montre toute la fenêtre |
| `ui_resize` | largeur, hauteur | la fenêtre change de taille (nouveau tampon, fond `FACE`) |
| `ui_min_size` | largeur, hauteur | taille minimale d'une fenêtre `WF_RESIZABLE` |
| `ui_on_resize` | fonction `(largeur, hauteur)`, 0 | appelée par `uikit_w_event` après un `WEV_RESIZE` |
| `ui_maximize` | | comme le bouton agrandir (agrandie, ou rendue à sa taille d'avant) |
| `ui_minimize` | | la fenêtre courante réduite, comme le bouton de son cadre (`ui_raise` la rend) |
| `ui_window_drag` | | pendant un appui dans le contenu : la fenêtre suit la souris |
| `ui_frame_request` | | 1 : un `WEV_FRAME` viendra quand Elegant aura pris l'image (animations) |
| `ui_hit` | x, y, largeur, hauteur | 1 si la souris (dernier événement) est dans le rectangle |
| `ui_cursor` | forme `UC_*` | curseur sur la fenêtre (`UC_ARROW`, `UC_TEXT`, `UC_HAND`, `UC_CROSS`, `UC_WAIT`, `UC_MOVE`...) |
| `ui_pointer_lock` | 1 prend, 0 rend | la fenêtre garde le pointeur : `WEV_MOUSE` de genre `WEV_M_REL`, `EV_X`, `EV_Y` = déplacement |

Quelques rôles de `ui_window_ex` : `WF_RESIZABLE` (bords, bouton agrandir), `WF_GLYPH_LIGHT` (boutons du cadre en clair, pour un contenu sombre), `WF_BODY_DARK` (cadre noir, pour un contenu noir).

### Les événements

| Type | Quand | Champs utiles (`ui_ev_field`) |
|---|---|---|
| `WEV_KEY` (1) | une touche | `EV_KIND` 1 appui / 0 relâché, `EV_CODE` code HID (`KEY_ENTER`, `KEY_ESC`, `KEY_LEFT`...), `EV_CHAR` caractère, `EV_MODS` (`UI_MOD_CTRL`, `UI_MOD_SHIFT`, `UI_MOD_ALT`) |
| `WEV_MOUSE` (2) | la souris | `EV_KIND` 0 déplacement, 1 appui, 2 relâché ; `EV_X`, `EV_Y` dans le contenu ; `EV_CODE` boutons (bit 0 gauche, bit 1 droit) ; `EV_MODS` |
| `WEV_CLOSE` (3) | fermeture demandée | `EV_WIN` : la fenêtre |
| `WEV_WHEEL` (4) | la molette | `EV_Y` : crans (positif : vers le haut) |
| `WEV_FOCUS` (5) | clavier gagné ou perdu | `EV_KIND` 1 / 0 |
| `WEV_MENU` (7) | un choix dans la barre de menus | `EV_CODE` : id de l'entrée |
| `WEV_RESIZE` (8) | nouvelle taille demandée | traité par `uikit_w_event`, puis la fonction de `ui_on_resize` |
| `WEV_DROP` (9) | un dépôt (glisser-déposer) | `EV_X`, `EV_Y` ; données par `uikit_drop_get` |
| `WEV_FRAME` (10) | l'image a été prise | après `ui_frame_request` |
| `WEV_PAD` (11) | une manette | (voir [EmuKit](emukit_fr.md)) |
| `WEV_SCREEN` (12) | la résolution a changé | `EV_X`, `EV_Y` : taille de l'écran |

Chaque événement porte `EV_WIN`, la fenêtre qui le reçoit (utile avec [plusieurs fenêtres](#plusieurs-fenêtres)).

### Exemple : une fenêtre qui dessine

Le plus petit programme graphique : une fenêtre redimensionnable, dessinée aux couleurs du thème, redessinée à chaque changement de taille. Il est en C ; les autres exemples sont en C++.

```c
// hello.c -- une fenêtre qui dessine (UIKit, sans widget)
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;			// les jetons du thème (TJ_*)

static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

// tout le contenu, à la taille donnée ; aussi appelée après un changement de taille
static long paint (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_rbox (24, 44, w - 48, h - 68, 12, jet (TJ_SURFACE), jet (TJ_SURFACE));
	ui_text ((long) TR ("Bonjour, Korin OS"), 48, 92, jet (TJ_TEXTE1), 24, (long) ui.font_b, 0);
	ui_text_wrap ((long) TR ("Ce texte est coupé en lignes à la largeur de la carte. Agrandissez la fenêtre par son bord droit ou son bord bas."),
		48, 110, w - 96, jet (TJ_TEXTE2), (long) ui.font, 14, 0);
	// trois disques anticrénelés (coordonnées en 1/16 de pixel)
	unsigned c[3] = { jet (TJ_ACCENT), jet (TJ_SUCCES), jet (TJ_ALERTE) };
	for (int i = 0; i < 3; i++)
		ui_vp_disc ((72 + i * 56) * 16, (h - 72) * 16, 20 * 16, c[i]);
	ui_update_all ();
	return 0;
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 480, 320, (long) TR ("Bonjour"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (360, 260);
	ui_on_resize ((long) paint, 0);
	paint (ui.w, ui.h);
	for (;;)
	{
		long t = ui_event (1000);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			uikit_w_event (&code, &taken);	// (WEV_RESIZE : nouvelle taille, puis paint)
		}
	}
	return 0;
}
```

> **Remarque** : les textes visibles des exemples sont écrits en français, la langue des sources de Korin OS ; `TR` (`ui_tr`) les traduit par `lang/en.txt` (voir [Traduction](#traduction)).

## Dessin et texte

On dessine dans les pixels de la fenêtre courante (ou d'une vue, d'un canevas : voir plus bas), puis `ui_update` ou `ui_update_all` montre le résultat. Les couleurs sont des entiers `0x00RRGGBB`. Le texte est en UTF-8 ; `y` de `ui_text` est la **ligne de base**, pas le haut du texte.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_fill` | x, y, largeur, hauteur, couleur | rectangle plein |
| `ui_frame` | x, y, largeur, hauteur, couleur | contour d'un pixel |
| `ui_pixel` | x, y, couleur | un pixel |
| `ui_rbox` | x, y, largeur, hauteur, rayon, couleur du haut, du bas | rectangle à coins arrondis, en dégradé |
| `ui_ombre` | largeur, hauteur | ombre et contour d'un corps en retrait de 4 px, rayon 8 |
| `ui_clip` | x, y, largeur, hauteur (largeur ≤ 0 : aucune) | découpe de tous les dessins suivants |
| `ui_text` | texte, x, ligne de base, couleur, taille px, police, largeur max (0 : jusqu'au bord) | x de la fin du texte |
| `ui_text_fit` | comme `ui_text`, la largeur max en dernier | texte coupé par « … » s'il dépasse |
| `ui_text_width` | texte, taille px, police | largeur en pixels |
| `ui_text_prefix` | texte, octets, taille px, police | largeur des premiers octets (placer un curseur) |
| `ui_text_hit` | texte, taille px, police, x | octets avant le point x (aide : `uikit_text_hit`, qui rend aussi le x exact) |
| `ui_text_wrap` | texte, x, y (haut), largeur, couleur, police, taille px, 1 : mesurer seulement | hauteur occupée ; coupe aux espaces, `\n` : nouvelle ligne |
| `ui_load_font` | chemin d'un `.ttf`, bloc de `UI_FONT_SIZE` octets | 1 si chargée |
| `ui_font_italic` | | police italique (sinon la régulière) |
| `ui_font_get` | `UFN_*` | police : `UFN_SANS`, `UFN_BOLD`, `UFN_MONO`, `UFN_ITALIC`, `UFN_BOLDITALIC`, `UFN_MONOBOLD`, `UFN_SERIF`, `UFN_SERIFBOLD`, `UFN_SERIFITALIC`, `UFN_SERIFBOLDITALIC` |
| `ui_vp_disc` | cx, cy, rayon, couleur (1/16 px) | disque anticrénelé |
| `ui_vp_line` | x0, y0, x1, y1, épaisseur, couleur (1/16 px) | trait épais à bouts ronds |
| `ui_vp_poly` | points (paires `int32_t` x, y en 1/16 px), nombre, couleur | polygone plein (pair-impair) |
| `ui_icone` | nom d'une app, x, y, taille | dessine l'icône de l'app |
| `ui_glyph` | nom `glyph-*`, x, y, côté, couleur | un glyphe d'interface colorié (un point si le fichier manque) |
| `ui_button` | x, y, largeur, hauteur, libellé, 1 : accentué | dessine un bouton (dessin seul : pour un vrai bouton, `ui_pushbutton`) |

Le rastériseur avance une tabulation au taquet suivant (tous les quatre espaces) et ne dessine pas le retour chariot.

## Thème et jetons

`ui_jetons ()` rend l'adresse d'un tableau de couleurs 32 bits, les **jetons** du thème. `TJ_*` est un décalage en octets : le jeton se lit à l'indice `TJ_x / 4`, et son octet haut se masque (`& 0xFFFFFF`).

```c
static const unsigned *s_jet;           // s_jet = (const unsigned *) ui_jetons ();
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }
```

| Jeton | Usage |
|---|---|
| `TJ_FACE` | fond de la fenêtre |
| `TJ_SURFACE` | cartes, panneaux, fond d'une zone de contenu |
| `TJ_CREUX` | zones en creux (rails, panneaux latéraux) |
| `TJ_ELEVE`, `TJ_ELEVE_BAS` | éléments en relief (haut, bas du dégradé) |
| `TJ_LIGNE`, `TJ_LIGNE_FORTE` | traits, contours |
| `TJ_TEXTE1`, `TJ_TEXTE2`, `TJ_TEXTE3` | texte principal, secondaire, grisé |
| `TJ_ACCENT`, `TJ_ACCENT_CLAIR`, `TJ_ACCENT_FONCE`, `TJ_ACCENT_VIF` | couleur d'accent et ses variantes (sélection, focus) |
| `TJ_ACCENT_INK` | encre lisible sur `TJ_ACCENT` |
| `TJ_DANGER`, `TJ_SUCCES`, `TJ_ALERTE` | erreur, réussite, avertissement |
| `TJ_MENU` | fond des menus |
| `TJ_BARRE`, `TJ_DOCK`, `TJ_CONTOUR` | barre de menus, dock, contour des fenêtres (octet haut : transparence) |
| `TJ_TITRE_ACTIF`, `TJ_TITRE_INACTIF` | cadre de la fenêtre active, des autres |
| `TJ_SOMBRE` | 1 si le thème est sombre (ce n'est pas une couleur) |

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_jetons` | | adresse des jetons (`TJ_*`, lecture seule) |
| `ui_theme` | | adresse du thème du bureau (`TH_*` : style, cadres, accent...) |
| `ui_echelle` | | 0 grande, 1 moyenne, 2 petite (selon l'écran) |
| `ui_tone` | couleur, niveau (128 : inchangée) | couleur éclaircie (> 128) ou assombrie (< 128) |
| `ui_mix` | couleur a, couleur b, 0 à 256 | mélange (0 : a, 256 : b) |
| `ui_ink` | couleur de fond | encre lisible dessus (claire ou foncée) |
| `ui_theme_texte` | texte d'un `theme.txt` (0 : le fichier) | aperçu d'un thème (panneau de configuration) |

## Widgets

Une fonction de création rend une **poignée** (0 : échec) ; le widget appartient à la fenêtre courante. Il se dessine lui-même quand il change ; `ui_w_draw (0)` les dessine tous, par exemple après avoir peint le fond. `uikit_w_event` gère le survol, le focus clavier (Tab, Maj+Tab, dans l'ordre de création), le glisser, le double-clic et la molette, et rend un code :

| Code | Sens |
|---|---|
| `UN_CHANGE` | valeur, sélection ou texte changés |
| `UN_ACTIVATE` | clic d'un bouton, Entrée, double-clic d'un élément |
| `UN_SORT` | liste triée par un clic sur un en-tête |
| `UN_CLOSE`, `UN_NEW` | barre d'onglets : croix d'un onglet, « + » |
| `UN_DROP` | un dépôt sur ce widget (lire par `uikit_drop_get`) |
| `UN_CONTEXT` | bouton droit (liste : la ligne est choisie) |

Les coordonnées sont celles de la fenêtre ; une largeur ou une hauteur de 0 (là où c'est dit) prend la taille naturelle. La hauteur ordinaire d'un contrôle est de 30 px.

### Fonctions communes

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_w_event` | 0 (l'événement de `ui.ev`) | widget ; code et « pris » : aide `uikit_w_event (&code, &taken)` |
| `ui_w_draw` | widget (0 : tous ceux de la fenêtre courante) | dessine et montre |
| `ui_w_move` | widget, x, y, largeur, hauteur | place et taille |
| `ui_w_show` | widget, 1 / 0 | montré / caché |
| `ui_w_enable` | widget, 1 / 0 | actif / grisé |
| `ui_w_focus` | widget | lui donne le clavier |
| `ui_w_get_focus` | | widget qui a le clavier |
| `ui_w_on` | widget, fonction `(widget, code)`, donnée | fonction appelée depuis `ui_w_event` |
| `ui_w_user` | widget | la donnée de `ui_w_on` |
| `ui_w_get_value`, `ui_w_set_value` | widget (, valeur) | valeur : élément choisi (-1 : aucun), position, case cochée... |
| `ui_w_set_range` | widget, min, max, page, pas | plage d'une valeur ; champ : longueur max (`0, max, 0, 0`) |
| `ui_w_set_text`, `ui_w_get_text` | widget (, texte) | texte d'un champ, d'une étiquette, d'un bouton ; le texte rendu reste au kit |
| `ui_w_text_len` | widget | longueur du texte en octets |
| `ui_w_style` | widget, drapeaux | remplace les drapeaux de création (ex. `UTF_ERROR` sur un champ) |
| `ui_w_set_parent` | widget, panneau (0 : la fenêtre) | met le widget dans un panneau, une page d'onglets, une vue |
| `ui_w_cursor` | widget, `UC_*` (-1 : celui du type) | curseur au survol |
| `ui_w_tip` | widget, texte (0 : aucun) | infobulle après 600 ms de survol |
| `ui_w_idle` | | à appeler quand `ui_event` rend 0 : montre les infobulles |
| `ui_tip_at` | x, y, texte (0 : retire) | une infobulle tout de suite, à un point de la fenêtre |
| `ui_w_window` | widget | sa fenêtre |
| `ui_w_destroy` | widget | retire et libère |

### Boutons, étiquettes, choix

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_pushbutton` | x, y, largeur, hauteur (0 : auto), texte, `UB_DEFAULT` / `UB_GHOST` | bouton : `UN_ACTIVATE` |
| `ui_label` | x, y, largeur, hauteur, texte, `ULB_*` | étiquette : `ULB_BOLD`, `ULB_DIM`, `ULB_WRAP`, `ULB_RIGHT`, `ULB_CENTER`, `ULB_TITLE` |
| `ui_checkbox` | x, y, largeur, hauteur, libellé, 1 : cochée | case, valeur 0 / 1, `UN_CHANGE` |
| `ui_radio` | x, y, largeur, hauteur, libellé, groupe | bouton radio ; ceux d'un même groupe s'excluent |
| `ui_switch` | x, y, largeur, hauteur, libellé, 1 : allumé | interrupteur |
| `ui_dropdown` | x, y, largeur, hauteur, éléments | liste déroulante ; valeur = élément choisi |
| `ui_combobox` | x, y, largeur, hauteur, éléments | liste modifiable ; `ui_w_get_text` : le texte tapé ou choisi |
| `ui_segmented` | x, y, largeur, hauteur (0 : auto), segments | contrôle segmenté ; valeur = segment choisi |

Les éléments d'une liste déroulante, des segments ou des onglets s'écrivent dans une seule chaîne, séparés par une barre verticale : `"Rouge|Vert|Bleu"`. Une liste déroulante accepte aussi `ui_list_add`, `ui_list_clear` et `uikit_list_get` comme une liste.

### Champs de texte

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_textbox` | x, y, largeur, hauteur, `UTF_*` | champ d'une ligne ; Entrée : `UN_ACTIVATE` ; `UTF_PASSWORD`, `UTF_READONLY`, `UTF_MONO`, `UTF_RIGHT`, `UTF_ERROR` |
| `ui_textbox_hint` | champ, indication | texte grisé quand le champ est vide |
| `ui_textbox_select` | champ, début, fin (octets) | choisit une partie du texte |
| `ui_textbox_selection` | champ | début (aide : `uikit_textbox_selection`, qui rend aussi la fin) |
| `ui_textarea` | x, y, largeur, hauteur, drapeaux | texte multiligne (1 Mio par défaut) ; drapeaux ci-dessous |
| `ui_textarea_caret` | texte multiligne | ligne (aide : `uikit_textarea_caret`, qui rend aussi la colonne) |
| `ui_textarea_goto` | texte multiligne, décalage | curseur à ce décalage, rendu visible |
| `ui_textarea_insert` | texte multiligne, texte | insère au curseur (à la place de la sélection) → `UN_CHANGE` |
| `ui_textarea_edit` | texte multiligne, `UTE_*` | `UTE_UNDO`, `UTE_REDO`, `UTE_CUT`, `UTE_COPY`, `UTE_PASTE`, `UTE_ALL` → `UN_CHANGE` si le texte change |
| `ui_textarea_find` | texte multiligne, mot | 1 trouvé (et choisi), sans casse, après la sélection |
| `ui_textarea_highlight` | texte multiligne, ligne (-1 : aucune) | une ligne éclairée (erreur, pas à pas) |
| `ui_textarea_keywords` | texte multiligne, fonction `(mot, longueur)` → 1 si mot-clé | mots-clés de l'éditeur de code |
| `ui_textarea_font` | texte multiligne, taille px (0 : celle de l'interface) | taille du texte |
| `ui_textarea_paper` | texte multiligne, papier (-1 : le thème), encre | couleurs du fond et du texte |

Drapeaux de `ui_textarea` : `UTF_READONLY`, `UTF_MONO`, `UTA_LINENUM` (numéros de ligne), `UTA_VSCROLL`, `UTA_HSCROLL` (barres de défilement), `UTA_CODE` (couleurs de l'assembleur ; avec `UTA_BASIC`, `UTA_CS` ou `UTA_XML` : BASIC, C#, XML), `UTA_TABS` (Tab insère une tabulation), `UTA_FORMTAB` (dans un formulaire, Tab passe au champ suivant). Les deux sortes de champs connaissent Ctrl+A, C, X, V ; le texte multiligne a aussi Annuler et Rétablir (Ctrl+Z, Ctrl+Y).

### Valeurs

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_slider` | x, y, largeur, hauteur, min, max, valeur | glissière (verticale si plus haute que large) |
| `ui_progress` | x, y, largeur, hauteur, max, valeur | barre de progression |
| `ui_numeric` | x, y, largeur, hauteur, min, max, valeur | nombre avec boutons + et − |
| `ui_knob` | x, y, largeur, hauteur, min, max, valeur | bouton rotatif |
| `ui_vumeter` | x, y, largeur, hauteur, max | vu-mètre |
| `ui_vumeter_level` | vu-mètre, niveau | niveau (la crête est gardée) |
| `ui_lcd` | x, y, largeur, hauteur, texte | afficheur à sept segments (chiffres, `-`, `:`, `.`) |
| `ui_scrollbar` | x, y, largeur, hauteur, `USB_HORIZONTAL` ou 0 | barre de défilement ; plage par `ui_w_set_range (barre, 0, contenu, visible, pas)` |

### Listes et arbres

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_list` | x, y, largeur, hauteur, `UL_*` | liste à une ou plusieurs colonnes |
| `ui_list_column` | liste, titre, largeur, `ULC_RIGHT` ou 0 | ajoute une colonne |
| `ui_list_add` | liste, texte (colonnes séparées par `\t`), donnée | numéro de l'élément |
| `ui_list_set` | liste, numéro, texte | remplace le texte |
| `ui_list_get` | liste, numéro | texte (aide : `uikit_list_get (liste, n, &donnee)`) |
| `ui_list_remove` | liste, numéro | retire |
| `ui_list_clear` | liste | vide |
| `ui_list_count` | liste | nombre d'éléments |
| `ui_list_sort` | liste, colonne, 1 : décroissant | trie |
| `ui_list_sorted` | liste | colonne triée (aide : `uikit_list_sorted`, qui dit aussi le sens) |
| `ui_list_virtual` | liste, lignes, fonction `(liste, ligne)` → texte | liste virtuelle : le programme garde ses lignes |
| `ui_list_cell` | liste, fonction `(liste, ligne, colonne, x, y, l, h, choisie)` → 1 dessinée | cellules dessinées par le programme |
| `ui_tree` | x, y, largeur, hauteur | arborescence ; valeur = id choisi |
| `ui_tree_add` | arbre, parent (-1 : racine), libellé, donnée | id |
| `ui_tree_expand` | arbre, id, 1 déplie / 0 replie | |
| `ui_tree_get` | arbre, id | libellé (la donnée est rendue en second registre) |
| `ui_tree_clear` | arbre | vide |

Options de `ui_list` : `UL_HEADER` (en-têtes), `UL_NOSORT`, `UL_USERSORT` (un clic d'en-tête rend `UN_SORT`, le programme trie), `UL_DRAG` (on glisse le texte d'un élément), `UL_STRIPES`, `UL_GRID`, `UL_HSCROLL`, `UL_SIDEBAR` (panneau latéral : un élément `#titre` est une section), `UL_ICONS` (icône d'après la donnée & 3 : 0 fichier, 1 dossier, 2 volume).

### Conteneurs, onglets, barres d'outils

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_panel` | x, y, largeur, hauteur, `UPN_SURFACE` ou 0 | panneau ; ses enfants sont montrés et cachés avec lui |
| `ui_groupbox` | x, y, largeur, hauteur, titre | cadre de groupe titré (un panneau) |
| `ui_layout` | panneau, `ULY_VSTACK` / `ULY_HSTACK` / `ULY_GRID`, écart, colonnes | place les enfants (ne redessine pas) |
| `ui_splitter` | x, y, largeur, hauteur, min, max | séparateur que l'on glisse ; valeur = sa position, `UN_CHANGE` |
| `ui_tabs` | x, y, largeur, hauteur, onglets | onglets à pages |
| `ui_tabs_page` | onglets, numéro | panneau de la page (y mettre ses widgets par `ui_w_set_parent`) |
| `ui_tabstrip` | x, y, largeur, hauteur, `UTS_CLOSE` / `UTS_PLUS` | barre d'onglets de documents |
| `ui_tabstrip_add` | barre, titre, donnée | numéro (l'onglet est choisi) |
| `ui_tabstrip_closing` | barre | l'onglet dont la croix a été cliquée (après `UN_CLOSE` ; le retirer par `ui_list_remove`) |
| `ui_toolbar` | x, y, largeur, hauteur (0 : auto) | barre d'outils (`UTBS_NOLINE` par `ui_w_style` : sans trait) |
| `ui_toolbar_add` | barre, glyphe, libellé, `UTB_TOGGLE` / `UTB_SEP` | bouton fantôme : `UN_ACTIVATE` ; à bascule : valeur 0 / 1 |

### Dates, couleurs, manette

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_calendar` | x, y, largeur, hauteur (0 : auto), date | calendrier d'un mois ; date = `année << 16` + `mois << 8` + jour |
| `ui_calendar_today` | calendrier, date | jour entouré (UIKit n'a pas l'heure : le programme la donne) |
| `ui_calendar_show` | calendrier, date | montre et choisit ce jour |
| `ui_datepicker` | x, y, largeur, hauteur, date | champ de date, calendrier dessous |
| `ui_datepicker_today` | sélecteur, date | aujourd'hui |
| `ui_date_popup` | widget, date, aujourd'hui | calendrier sous un widget → jour choisi (-1 : abandon) |
| `ui_palette` | x, y, largeur, hauteur (0 : auto), couleur | palette 8 × 5 |
| `ui_colorpicker` | x, y, largeur, hauteur, couleur | pastille ; un clic ouvre le dialogue de couleur |
| `ui_pad_hints` | `"A\tchoisir\nB\tretour"`, x, y, hauteur, 1 : mesurer | indications de manette ; largeur |
| `ui_objnav` | rectangles (`int32_t` x, y, l, h), nombre, courant, direction (0 gauche, 1 droite, 2 haut, 3 bas) | l'objet voisin (-1 : aucun) |

### Exemple : un formulaire

Un formulaire à gauche, une liste à droite : **Ajouter** (ou Entrée dans le champ) met une ligne dans la liste, un double-clic sur une ligne remet son nom dans le champ. La glissière met à jour son étiquette par une fonction attachée (`ui_w_on`) ; les autres widgets passent par la boucle.

```cpp
// form.cpp -- un formulaire de widgets (UIKit)
#include <stdio.h>
#include <string.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

enum { M = 16, CH = 30, LW = 96, FW = 340 };	// marge, hauteur d'un contrôle, étiquettes, largeur du formulaire
static long g_name, g_kind, g_size, g_sizeVal, g_bold, g_left, g_center, g_note, g_confirm, g_add, g_clear, g_list;

// la glissière : appelée par ui_w_event (ui_w_on)
static long on_size (long w, long code)
{
	if (code != UN_CHANGE) return 0;
	char t[16];
	snprintf (t, sizeof t, "%ld px", ui_w_get_value (w));
	ui_w_set_text (g_sizeVal, (long) t);
	return 0;
}

static void add_row ()
{
	const char *name = (const char *) ui_w_get_text (g_name);
	if (!name || !*name)
	{
		ui_w_style (g_name, UTF_ERROR);			// contour rouge
		ui_message ((long) TR ("Formulaire"), (long) TR ("Donnez un nom."), UM_OK);
		ui_w_focus (g_name);
		return;
	}
	ui_w_style (g_name, 0);
	long data = 0;
	const char *kind = (const char *) uikit_list_get (g_kind, ui_w_get_value (g_kind), &data);
	char row[256];
	snprintf (row, sizeof row, "%s\t%s\t%ld\t%s%s", name, kind ? kind : "", ui_w_get_value (g_size),
		ui_w_get_value (g_bold) ? TR ("gras, ") : "", ui_w_get_value (g_center) ? TR ("centré") : TR ("à gauche"));
	ui_list_add (g_list, (long) row, 0);
	ui_w_set_text (g_name, (long) "");
	ui_w_focus (g_name);
}

static void clear_list ()
{
	if (ui_w_get_value (g_confirm) &&
	    ui_message ((long) TR ("Formulaire"), (long) TR ("Effacer toute la liste ?"), UM_YESNO) != 1) return;
	ui_list_clear (g_list);
}

// une ligne de la liste : son nom (la première colonne) revient dans le champ
static void pick_row ()
{
	long row = ui_w_get_value (g_list), data = 0;
	if (row < 0) return;
	const char *t = (const char *) uikit_list_get (g_list, row, &data);
	char name[128];
	int n = 0;
	while (t && t[n] && t[n] != '\t' && n < (int) sizeof name - 1) { name[n] = t[n]; n++; }
	name[n] = 0;
	ui_w_set_text (g_name, (long) name);
}

// les widgets suivent la taille de la fenêtre (la liste prend la place)
static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_w_move (g_note, M + LW, 252, FW - LW, h - 252 - M - CH - 12);
	ui_w_move (g_add, M, h - M - CH, 120, CH);
	ui_w_move (g_clear, M + 128, h - M - CH, 140, CH);
	ui_w_move (g_list, FW + 2 * M, 52, w - FW - 3 * M, h - 52 - M);
	ui_w_draw (0);
	ui_update_all ();
	return 0;
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 760, 480, (long) TR ("Formulaire"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (680, 420);

	// (le titre n'est pas interactif : il peut aller dans le haut qui déplace la fenêtre)
	ui_label (M, 12, FW, 28, (long) TR ("Nouvel élément"), ULB_TITLE);
	int y = 52;
	ui_label (M, y + 6, LW, 20, (long) TR ("Nom"), 0);
	g_name = ui_textbox (M + LW, y, FW - LW, CH, 0);
	ui_textbox_hint (g_name, (long) TR ("Obligatoire"));
	y += 40;
	ui_label (M, y + 6, LW, 20, (long) TR ("Genre"), 0);
	g_kind = ui_dropdown (M + LW, y, FW - LW, CH, (long) TR ("Titre|Paragraphe|Note"));
	ui_w_set_value (g_kind, 0);
	y += 40;
	ui_label (M, y + 6, LW, 20, (long) TR ("Taille"), 0);
	g_size = ui_slider (M + LW, y, FW - LW - 60, CH, 8, 48, 14);
	g_sizeVal = ui_label (M + FW - 52, y + 6, 52, 20, (long) "14 px", ULB_DIM | ULB_RIGHT);
	ui_w_on (g_size, (long) on_size, 0);
	y += 40;
	g_bold = ui_checkbox (M + LW, y, 80, CH, (long) TR ("Gras"), 0);
	g_left = ui_radio (M + LW + 90, y, 80, CH, (long) TR ("Gauche"), 1);
	g_center = ui_radio (M + LW + 170, y, 90, CH, (long) TR ("Centre"), 1);
	ui_w_set_value (g_left, 1);
	y += 40;
	g_confirm = ui_switch (M + LW, y, FW - LW, CH, (long) TR ("Confirmer l'effacement"), 1);
	y += 40;
	ui_label (M, y + 6, LW, 20, (long) TR ("Remarque"), 0);
	g_note = ui_textarea (0, 0, 10, 10, UTA_FORMTAB | UTA_VSCROLL);	// (placé par on_resize, à y = 252)
	g_add = ui_pushbutton (0, 0, 10, CH, (long) TR ("Ajouter"), UB_DEFAULT);
	g_clear = ui_pushbutton (0, 0, 10, CH, (long) TR ("Effacer la liste"), 0);
	ui_w_tip (g_clear, (long) TR ("Vide la liste de droite"));

	g_list = ui_list (0, 0, 10, 10, UL_HEADER | UL_STRIPES);
	ui_list_column (g_list, (long) TR ("Nom"), 150, 0);
	ui_list_column (g_list, (long) TR ("Genre"), 100, 0);
	ui_list_column (g_list, (long) TR ("Taille"), 60, ULC_RIGHT);
	ui_list_column (g_list, (long) TR ("Style"), 140, 0);

	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);
	ui_w_focus (g_name);

	for (;;)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code == UN_ACTIVATE && (w == g_add || w == g_name)) add_row ();
			else if (code == UN_ACTIVATE && w == g_clear) clear_list ();
			else if (code == UN_ACTIVATE && w == g_list) pick_row ();
		}
		else ui_w_idle ();
	}
	return 0;
}
```

## Vues dessinées à la main

Une **vue** (`ui_view`) est un widget dont le programme dessine lui-même le contenu : un document, une grille, un graphique, un jeu. UIKit lui donne sa place, ses propres pixels, le survol, la capture de la souris pendant un glisser, le double-clic, le focus et ses enfants. Le programme donne quatre fonctions :

| Fonction du programme | Arguments | Rend |
|---|---|---|
| dessin | vue, largeur, hauteur | (rien) ; les `ui_*` dessinent dans la vue, coin (0, 0) |
| souris | vue, genre (0 déplacement, 1 appui, 2 relâché, 3 sortie), x, y, infos | un code `UN_*` (0 : rien) ; infos = clics + boutons `<< 8` + modificateurs `<< 16` |
| touche | vue, code HID, caractère, modificateurs | -1 si la touche n'est pas prise (elle reste au programme, par ex. pour les raccourcis) |
| molette | vue, crans (positif : vers le haut) | un code `UN_*` |

On marque la vue par `ui_view_dirty` quand son contenu change ; `ui_view_flush`, à chaque tour de la boucle, redessine les vues marquées. Une vue non marquée est recopiée telle quelle.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_view` | x, y, largeur, hauteur, `UVW_FOCUS` ou 0 | vue ; `UVW_FOCUS` : elle prend le clavier (clic, Tab) |
| `ui_view_on` | vue, dessin, souris, touche, molette (0 : aucune) | attache les fonctions et marque la vue |
| `ui_view_dirty` | vue | à redessiner |
| `ui_view_flush` | | redessine et montre les vues marquées de la fenêtre courante |

Les enfants d'une vue (`ui_w_set_parent`) sont coupés par elle : un formulaire qui défile se fait en déplaçant ses widgets (`ui_w_move`) puis `ui_view_dirty`.

### Canevas et surfaces

Un **canevas** est une image à soi, montrée comme un widget ; entre `ui_canvas_begin` et `ui_canvas_end`, tous les dessins de UIKit y vont. Une **surface** est un tampon partagé qu'Elegant compose directement dans la fenêtre, sans copie : pour la vidéo, les émulateurs, un rendu par le GPU ([GpuKit](gpukit_fr.md), [MediaKit](mediakit_fr.md)).

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_canvas` | x, y, largeur, hauteur | canevas (blanc) ; la souris y rend `UN_CHANGE` |
| `ui_canvas_pixels` | canevas | pixels (aide : `uikit_canvas_pixels (c, &l, &h)`) |
| `ui_canvas_begin` | canevas | les dessins vont dans le canevas |
| `ui_canvas_end` | | fin ; le montrer par `ui_w_draw (canevas)` |
| `ui_surface` | x, y (zone du contenu), largeur, hauteur, `WS_SF_XRGB8888` / `WS_SF_RGB565` | surface (0 : échec) |
| `ui_surface_pixels` | surface | pixels (le pas, la largeur et la hauteur suivent dans x1 à x3) |
| `ui_surface_damage` | surface, x, y, largeur, hauteur (largeur ≤ 0 : tout) | signale une zone changée (pas de `ui_update`) |
| `ui_surface_damage_list` | surface, rectangles (x, y, l, h en 16 bits), nombre | plusieurs zones |
| `ui_surface_move` | surface, x, y, largeur, hauteur | déplace ou redimensionne → 1 |
| `ui_surface_source` | surface, tampon partagé (-1 : le sien), pas, décalage | compose depuis un tampon écrit ailleurs → 1 |
| `ui_surface_free` | surface | détachée, tampon rendu |

### Exemple : une vue avec la souris et la molette

Un carnet de croquis : on dessine en glissant le bouton gauche, la molette change l'épaisseur du pinceau (la glissière suit), le bouton droit ouvre un menu contextuel, Retour arrière efface le dernier trait.

```cpp
// sketch.cpp -- une vue dessinée à la main : souris, molette, clavier (UIKit)
#include <stdio.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

enum { TOPH = 48, MAXP = 8192 };
struct Point { short x, y; unsigned char size, start; };	// start : premier point d'un trait
static Point g_pts[MAXP];
static int g_np, g_brush = 6;
static long g_view, g_slider, g_clear;
static bool g_menu;						// menu contextuel demandé
static int g_menuX, g_menuY;					// (dans la fenêtre)

static void view_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_SURFACE));
	for (int x = 32; x < w; x += 32) ui_fill (x, 0, 1, h, jet (TJ_LIGNE));
	for (int y = 32; y < h; y += 32) ui_fill (0, y, w, 1, jet (TJ_LIGNE));
	unsigned ink = jet (TJ_ACCENT);
	for (int i = 0; i < g_np; i++)
	{
		const Point &p = g_pts[i];
		if (p.start) ui_vp_disc (p.x * 16, p.y * 16, p.size * 8, ink);
		else ui_vp_line (g_pts[i - 1].x * 16, g_pts[i - 1].y * 16, p.x * 16, p.y * 16, p.size * 16, ink);
	}
	char t[64];
	snprintf (t, sizeof t, TR ("%d points, pinceau de %d px"), g_np, g_brush);
	ui_text ((long) t, 12, h - 12, jet (TJ_TEXTE2), 12, (long) ui.font, 0);
}

static void add_point (long x, long y, bool start)
{
	if (g_np >= MAXP) return;
	if (g_np == 0) start = true;
	g_pts[g_np++] = { (short) x, (short) y, (unsigned char) g_brush, (unsigned char) start };
	ui_view_dirty (g_view);
}

static void undo_stroke ()
{
	while (g_np > 0 && !g_pts[--g_np].start) {}
	ui_view_dirty (g_view);
}

static void set_brush (int b)
{
	g_brush = b < 1 ? 1 : b > 40 ? 40 : b;
	ui_w_set_value (g_slider, g_brush);
	ui_view_dirty (g_view);
}

static long view_mouse (long, long kind, long x, long y, long info)
{
	int buttons = (int) (info >> 8) & 0xFF;
	if (kind == 1 && (buttons & 2)) { g_menu = true; g_menuX = (int) x; g_menuY = (int) y + TOPH; }
	else if (kind == 1 && (buttons & 1)) add_point (x, y, true);
	else if (kind == 0 && (buttons & 1)) add_point (x, y, false);	// glisser : la vue garde la souris
	return 0;
}

static long view_wheel (long, long steps)
{
	set_brush (g_brush + (int) steps);
	return 0;
}

static long view_key (long, long code, long, long)
{
	if (code == KEY_BACKSPACE) { undo_stroke (); return 0; }
	return -1;							// pas prise : au programme
}

static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_w_move (g_view, 0, TOPH, w, h - TOPH);
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	return 0;
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 720, 500, (long) TR ("Croquis"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (420, 300);

	ui_label (16, 14, 70, 20, (long) TR ("Pinceau"), ULB_DIM);
	g_slider = ui_slider (86, 9, 180, 30, 1, 40, g_brush);
	g_clear = ui_pushbutton (280, 9, 0, 30, (long) TR ("Effacer"), 0);
	g_view = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_view, (long) view_paint, (long) view_mouse, (long) view_key, (long) view_wheel);
	ui_w_cursor (g_view, UC_CROSS);
	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);
	ui_w_focus (g_view);

	for (;;)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (w == g_slider && code == UN_CHANGE) set_brush ((int) ui_w_get_value (w));
			else if (w == g_clear && code == UN_ACTIVATE) { g_np = 0; ui_view_dirty (g_view); }
		}
		if (g_menu)
		{
			g_menu = false;
			// libellés à la suite, une chaîne vide à la fin ; « - » : un trait (il compte dans les numéros)
			static const char items[] = "Annuler le trait\0-\0Pinceau fin\0Pinceau épais\0Tout effacer\0";
			switch (ui_popup_local (g_menuX, g_menuY, (long) items, 200))
			{
			case 0: undo_stroke (); break;
			case 2: set_brush (2); break;
			case 3: set_brush (16); break;
			case 4: g_np = 0; ui_view_dirty (g_view); break;
			}
		}
		ui_view_flush ();
	}
	return 0;
}
```

## Dialogues

Les dialogues de UIKit sont modaux : une vraie fenêtre de plus (rôle `WF_MODAL`), qui bloque **toutes** les autres fenêtres du programme jusqu'à la réponse. Ils traduisent eux-mêmes leurs boutons.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_message` | titre, texte, `UM_OK` / `UM_OKCANCEL` / `UM_YESNO` / `UM_YESNOCANCEL` | 1 OK / Oui, 0 Annuler / Non ; 2 : Non de `UM_YESNOCANCEL` ; Échap : 0 |
| `ui_ask` | titre, question, tampon (texte de départ), taille | 1 OK, 0 Annuler ; la réponse dans le tampon |
| `ui_file_open` | dossier de départ, tampon, taille | 1 : le chemin choisi dans le tampon |
| `ui_file_save` | dossier, nom proposé, tampon, taille | 1 ; demande « Remplacer ? » si le fichier existe |
| `ui_folder_choose` | dossier, tampon, taille | 1 : le dossier choisi |
| `ui_file_filter` | motifs (`"*.txt;*.md"`, 0 : tous) | fichiers montrés par les dialogues suivants |
| `ui_color_dialog` | titre, couleur | couleur choisie (-1 : Annuler) |
| `ui_dialog_begin` | largeur, hauteur, titre | dialogue ; il devient la fenêtre courante : les widgets créés ensuite sont à lui |
| `ui_dialog_button` | dialogue, place 0 à 2, texte, réponse, `UB_*`, bord droit | bord droit du bouton suivant |
| `ui_dialog_run` | dialogue, fonction `(dialogue, widget, code)` ou 0, 0 | la réponse (bouton, Échap, Entrée) |
| `ui_dialog_close` | dialogue, réponse | finit `ui_dialog_run` (depuis la fonction) |
| `ui_dialog_cancel` | dialogue, réponse | réponse d'Échap et de la case de fermeture |
| `ui_dialog_timer` | dialogue, fonction `(dialogue)`, période ms | appelée pendant l'attente (compte à rebours) |
| `ui_dialog_end` | dialogue | ses widgets détruits, fenêtre fermée, la fenêtre d'avant redevient courante |
| `ui_dialog_at` | x, y | le prochain `ui_dialog_begin` : sans cadre, devant tout, à ce point (plein écran d'un diaporama) |

`ui_file_filter` accepte aussi plusieurs types nommés, séparés par des barres verticales : `"Textes|*.txt;*.md|Tous|*"` ; `ui_file_save` ajoute alors l'extension du type choisi à un nom qui n'en a pas.

Un dialogue à soi :

```cpp
// (extrait) demander un nom et une option
long d = ui_dialog_begin (420, 170, (long) TR ("Renommer"));
long field = ui_textbox (16, 48, 388, 30, 0);
ui_w_set_text (field, (long) old_name);
long keep = ui_checkbox (16, 88, 388, 24, (long) TR ("Garder l'extension"), 1);
long e = ui_dialog_button (d, 0, (long) TR ("Renommer"), 1, UB_DEFAULT, 420 - 12);
ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, e);
ui_dialog_cancel (d, 0);
ui_w_focus (field);
long r = ui_dialog_run (d, 0, 0);			// 1 : Renommer, 0 : Annuler ou Échap
// lire les widgets AVANT ui_dialog_end, qui les détruit
snprintf (new_name, sizeof new_name, "%s", (const char *) ui_w_get_text (field));
bool keep_ext = ui_w_get_value (keep) != 0;
ui_dialog_end (d);
```

## Menus et raccourcis

Chaque app publie ses menus dans la **barre de menus** en haut de l'écran par `ui_menu`, après la création de sa fenêtre. Le texte des menus est fait de lignes :

- `M<titre>` ouvre un menu ;
- `I<id>\t<libellé>\t<raccourci>` une entrée (id décimal ≥ 0, raccourci facultatif) ; juste après l'id, `d` grise l'entrée et `c` la coche (`I4c\tMono`) ;
- `-` un séparateur.

La barre ajoute d'elle-même le menu au nom de l'app (avec **Quitter**, qui arrive en `WEV_CLOSE`) et le menu **Aide** quand l'app a un `help_fr.md` ou un `help_en.md`. Un choix arrive en `WEV_MENU`, l'id dans `EV_CODE`. Le raccourci n'est qu'affiché : le programme passe les touches que ses widgets n'ont pas prises à `ui_menu_key`, qui rend l'id de l'entrée dont c'est le raccourci (« Ctrl+S », « Ctrl+Maj+Z », « Alt+X », « F3 »).

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_menu` | texte des menus (0 : aucun) | 0 (< 0 : plus de 2048 octets) ; les libellés passent par `ui_tr` |
| `ui_menu_state` | id, `UMS_DISABLED` / `UMS_CHECKED` / 0 | grise, coche ou rend normale une entrée |
| `ui_menu_key` | 0 (l'événement de `ui.ev`) | id du raccourci (-1 : aucun) |
| `ui_popup_local` | x, y (dans la fenêtre), libellés, largeur | menu contextuel → numéro choisi (-1 : abandon) |
| `ui_popup` | x, y (à l'écran), libellés, largeur | menu surgissant → numéro (-1 : abandon) |
| `ui_popup_ex` | comme `ui_popup`, drapeaux | bit 0 : `icône\ttexte` ; bit 4 : `texte\traccourci` ; bit 1 : rester ouvert sur un sous-menu (`>texte`) |
| `ui_popup_close` | | ferme un menu gardé ouvert |
| `ui_menu_get`, `ui_menu_command` | | pour la barre de menus elle-même (lire le menu de l'app active, transmettre un choix) |

Les libellés d'un menu surgissant se suivent dans une seule chaîne, chacun terminé par un NUL, avec une chaîne vide à la fin ; `-` est un trait, un premier octet `0x01` grise l'entrée.

> **Remarque** : UIKit retient la traduction d'un texte de menus par son adresse. Pour changer les menus, passez un **autre** tampon (deux tampons en alternance), ou gardez le même texte et changez les états par `ui_menu_state`.

### Exemple : menus et dialogue de fichier

Un petit éditeur de texte : menus **Fichier** (Nouveau, Ouvrir, Enregistrer, Enregistrer sous), **Édition** (Annuler, Couper, Copier, Coller, Tout sélectionner) et **Présentation** (une entrée cochée), raccourcis, dialogues d'ouverture et d'enregistrement, et une question avant de perdre des changements.

```cpp
// notepad.cpp -- menus, raccourcis, dialogues de fichiers (UIKit)
#include <stdio.h>
#include <string.h>
#include <string>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

enum { M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_ALL, M_MONO };
static const char MENUS[] =
	"MFichier\n"
	"I1\tNouveau\tCtrl+N\n" "I2\tOuvrir...\tCtrl+O\n" "-\n"
	"I3\tEnregistrer\tCtrl+S\n" "I4\tEnregistrer sous...\tCtrl+Maj+S\n"
	"MÉdition\n"
	"I5\tAnnuler\tCtrl+Z\n" "I6\tRétablir\tCtrl+Y\n" "-\n"
	"I7\tCouper\tCtrl+X\n" "I8\tCopier\tCtrl+C\n" "I9\tColler\tCtrl+V\n" "-\n" "I10\tTout sélectionner\tCtrl+A\n"
	"MPrésentation\n"
	"I11\tPolice à chasse fixe\n";

static long g_text;
static std::string g_path, g_saved;		// le fichier, et son texte tel qu'enregistré
static bool g_mono;

static std::string base_name (const std::string &p)
{
	size_t s = p.find_last_of ("/:");
	return s == std::string::npos ? p : p.substr (s + 1);
}

static void set_title ()
{
	std::string t = g_path.empty () ? std::string (TR ("Sans titre")) : base_name (g_path);
	ui_title ((long) (t + " - " + TR ("Bloc-notes")).c_str ());
}

static const char *text () { const char *t = (const char *) ui_w_get_text (g_text); return t ? t : ""; }

static bool save_to (const std::string &path)
{
	FILE *f = fopen (path.c_str (), "wb");
	const char *t = text ();
	size_t n = strlen (t);
	bool ok = f && fwrite (t, 1, n, f) == n;
	if (f) fclose (f);
	if (!ok) { ui_message ((long) TR ("Bloc-notes"), (long) TR ("Impossible d'enregistrer le fichier."), UM_OK); return false; }
	g_path = path; g_saved = t;
	set_title ();
	return true;
}

static bool save_as ()
{
	char p[512];
	ui_file_filter ((long) "*.txt;*.md");
	std::string name = g_path.empty () ? "notes.txt" : base_name (g_path);
	if (ui_file_save ((long) "SD0:/home", (long) name.c_str (), (long) p, sizeof p) != 1) return false;
	return save_to (p);
}

static bool save () { return g_path.empty () ? save_as () : save_to (g_path); }

// des changements à perdre ? -> true : on peut continuer
static bool may_discard ()
{
	if (g_saved == text ()) return true;
	long r = ui_message ((long) TR ("Bloc-notes"), (long) TR ("Enregistrer les changements ?"), UM_YESNOCANCEL);
	if (r == 1) return save ();
	return r == 2;					// Non : on perd les changements ; Annuler (0) : on reste
}

static void load (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f) { ui_message ((long) TR ("Bloc-notes"), (long) TR ("Impossible d'ouvrir le fichier."), UM_OK); return; }
	std::string s;
	char buf[4096];
	size_t n;
	while ((n = fread (buf, 1, sizeof buf, f)) > 0) s.append (buf, n);
	fclose (f);
	ui_w_set_text (g_text, (long) s.c_str ());
	g_path = path; g_saved = s;
	set_title ();
}

static void command (long id)
{
	switch (id)
	{
	case M_NEW:
		if (!may_discard ()) break;
		ui_w_set_text (g_text, (long) "");
		g_path.clear (); g_saved.clear ();
		set_title ();
		break;
	case M_OPEN:
	{
		if (!may_discard ()) break;
		char p[512];
		ui_file_filter ((long) "*.txt;*.md");
		if (ui_file_open ((long) "SD0:/home", (long) p, sizeof p) == 1) load (p);
		break;
	}
	case M_SAVE: save (); break;
	case M_SAVEAS: save_as (); break;
	case M_UNDO: ui_textarea_edit (g_text, UTE_UNDO); break;
	case M_REDO: ui_textarea_edit (g_text, UTE_REDO); break;
	case M_CUT: ui_textarea_edit (g_text, UTE_CUT); break;
	case M_COPY: ui_textarea_edit (g_text, UTE_COPY); break;
	case M_PASTE: ui_textarea_edit (g_text, UTE_PASTE); break;
	case M_ALL: ui_textarea_edit (g_text, UTE_ALL); break;
	case M_MONO:
		g_mono = !g_mono;
		ui_w_style (g_text, UTA_VSCROLL | (g_mono ? UTF_MONO : 0));
		ui_w_draw (g_text);
		ui_menu_state (M_MONO, g_mono ? UMS_CHECKED : 0);
		break;
	}
	ui_w_focus (g_text);
}

static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_w_move (g_text, 8, 36, w - 16, h - 44);	// (les 36 px du haut : la fenêtre se déplace)
	ui_w_draw (0);
	ui_update_all ();
	return 0;
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 640, 460, (long) TR ("Bloc-notes"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (360, 240);
	g_text = ui_textarea (0, 0, 10, 10, UTA_VSCROLL);
	ui_menu ((long) MENUS);
	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);
	if (argc > 1) load (argv[1]);			// (un fichier donné par Fichiers ou le Terminal)
	set_title ();
	ui_w_focus (g_text);

	for (;;)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) { if (may_discard ()) break; continue; }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			uikit_w_event (&code, &taken);
			if (press && !taken)
			{
				long id = ui_menu_key (0);	// Ctrl+S, Ctrl+O...
				if (id >= 0) command (id);
			}
		}
		else ui_w_idle ();
	}
	return 0;
}
```

Les libellés du code sont en français, la langue des sources de Korin OS ; `ui_menu` les traduit (voir [Traduction](#traduction)).

## Presse-papiers et glisser-déposer

Le presse-papiers est tenu par le service `clipd`, qui garde les dix dernières copies, chacune avec ses représentations (un format, ses octets). `ui_clip_set` et `ui_clip_get` suffisent pour du texte.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_clip_set` | texte, octets (-1 : jusqu'au NUL) | copie un texte |
| `ui_clip_get` | tampon, taille | octets lus (texte terminé par NUL) |
| `ui_clip_put` | format, données, octets, copie à compléter (0 : nouvelle) | id de la copie (0 : refusée) ; plusieurs formats pour une copie |
| `ui_clip_take` | formats (`"rtf\0text\0\0"`, le préféré d'abord), tampon (0 : taille seule), taille | octets (0 : rien) ; le rang du format trouvé suit en second registre |
| `ui_clip_set_image` | pixels `0x00RRGGBB`, largeur, hauteur | 1 (0 : refusée) |
| `ui_clip_get_image` | tampon (0 : taille seule), taille | largeur (0 : aucune image) ; hauteur et octets suivent en second et troisième registres |
| `ui_clip_set_files` | chemins séparés par `\n`, 1 : coupés | 1 |
| `ui_clip_get_files` | tampon, taille | octets (0 : aucun) ; « coupés » suit en second registre |
| `ui_clip_done_cut` | | les fichiers coupés ont été déplacés : la copie s'en va |

Formats connus : `text`, `image`, `files`, `files-cut`, `url`, `rtf`, et `x-<app>` pour les formats propres à une app.

**Glisser-déposer** : pendant un appui dans sa fenêtre, un programme commence un glisser par `ui_drag_start` ; au relâché, la fenêtre dessous reçoit `WEV_DROP`. `uikit_w_event` insère un texte déposé sur un champ (`UN_CHANGE`) ; sur un autre widget, il rend `UN_DROP`, et le programme lit les données.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_drag_start` | données, octets (-1 : jusqu'au NUL ; 464 au plus), `UDT_TEXT` / `UDT_FILES` | commence un glisser |
| `ui_drop_get` | tampon, taille | octets du dernier dépôt (aide : `uikit_drop_get (tampon, taille, &genre)`) |

```cpp
// (extrait) des fichiers déposés sur la fenêtre, depuis Fichiers
if (t == WEV_DROP)
{
	long code = 0, taken = 0;
	uikit_w_event (&code, &taken);			// un texte sur un champ : inséré (UN_CHANGE)
	if (code != UN_CHANGE)
	{
		static char b[1024];
		long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
		if (n > 0 && kind == UDT_FILES) { b[n] = 0; open_files (b); }	// un chemin par ligne
	}
}
```

## Images

UIKit lit les images **BMP** (24 et 32 bits, palettes) et **PNG** (gris, couleurs, palette, transparence ; pas d'entrelacement). Une image se dessine à l'échelle et se mélange par son alpha.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_image_load` | chemin | image (0 : échec) |
| `ui_image_decode` | données, octets | image décodée d'un tampon |
| `ui_image_size` | image | largeur (aide : `uikit_image_size (img, &h, &pixels)`, pixels `0xAARRGGBB`) |
| `ui_image_draw` | image, x, y, largeur, hauteur (0, 0 : sa taille) | dessine dans la cible courante (fenêtre, vue, canevas) |
| `ui_image_free` | image | libère |
| `ui_imagebox` | x, y, largeur, hauteur, image, `UIM_STRETCH` / `UIM_NOSCALE` / 0 | boîte d'image (0 : ajustée en gardant les proportions) |
| `ui_imagebox_set` | boîte, image | change l'image |

```cpp
// (extrait) une image choisie par l'utilisateur, dans une vue
static long g_img;
static void paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_SURFACE));
	long ih = 0, px = 0, iw = g_img ? uikit_image_size (g_img, &ih, &px) : 0;
	if (iw > 0 && iw <= w && ih <= h) ui_image_draw (g_img, (w - iw) / 2, (h - ih) / 2, 0, 0);
	else if (iw > 0) ui_image_draw (g_img, 0, 0, w, h);			// (étirée)
}
...
char p[512];
ui_file_filter ((long) "*.png;*.bmp");
if (ui_file_open ((long) "SD0:/home", (long) p, sizeof p) == 1)
{
	if (g_img) ui_image_free (g_img);
	g_img = ui_image_load ((long) p);
	ui_view_dirty (g_view);
}
```

## Plusieurs fenêtres

Un programme peut ouvrir plusieurs fenêtres : chaque `ui_window_ex` en rend une de plus, qui devient la **fenêtre courante**. Les dessins, les widgets créés, `ui_menu`, `ui_title`, `ui_min_size`, `ui_on_resize` valent pour la fenêtre courante ; `ui_window_use` en change. `ui_event` rend les événements de toutes les fenêtres, `EV_WIN` dit laquelle ; `uikit_w_event` va de lui-même à la bonne. `WEV_CLOSE` reste au programme, qui ferme la fenêtre par `ui_window_close`.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_window_use` | fenêtre | la fenêtre courante d'avant |
| `ui_window_close` | fenêtre (0 : la courante) | ses widgets détruits, fermée |
| `ui_window_count` | | nombre de fenêtres ouvertes du programme |
| `ui_w_window` | widget | sa fenêtre |
| `ui_raise` | id | met une fenêtre au premier plan |
| `ui_winlist` | première, tampon (`WL_*`) | fenêtres du bureau (pour le panel, le dock) |
| `ui_win_details` | première, tampon (7 entrées `WE_*`) | toutes les fenêtres : place, taille, rôle, état, espace |
| `ui_screen_grab` | pixels, largeur, hauteur (celles de `ui_screen`) | 1 : l'écran tel qu'il est montré, pointeur compris, recopié là (Capture d'écran) |
| `ui_desk` | espace à montrer (-1 : garder), nombre (0 : garder) | espace courant + nombre `<< 8` |

Un seul menu par app, comme sur Mac : la barre montre celui de la première fenêtre qui en a un.

### Exemple : une fenêtre de plus

Un bouton ouvre (ou ramène) une fenêtre d'inspecteur avec une liste ; les fichiers déposés sur la fenêtre principale vont dans la liste ; fermer l'inspecteur ne ferme pas l'app.

```cpp
// twowin.cpp -- deux fenêtres dans un programme (UIKit)
#include <string.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

static long g_main, g_insp;			// les deux fenêtres (0 : fermée)
static long g_open, g_hint, g_list;

static void insp_open ()
{
	if (g_insp) { ui_raise (g_insp); return; }
	g_insp = ui_window_ex (-1, -1, 360, 300, (long) TR ("Inspecteur"), 0);	// elle devient la courante
	if (g_insp <= 0) { g_insp = 0; ui_window_use (g_main); return; }
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	ui_label (16, 12, 200, 24, (long) TR ("Fichiers déposés"), ULB_BOLD);
	g_list = ui_list (16, 44, ui.w - 32, ui.h - 60, 0);
	ui_w_draw (0);
	ui_update_all ();
	ui_window_use (g_main);			// les dessins reviennent à la fenêtre principale
}

static void insp_close ()
{
	ui_window_close (g_insp);		// (sa liste est détruite avec elle)
	g_insp = 0; g_list = 0;
	ui_window_use (g_main);
}

static void dropped (char *paths)
{
	insp_open ();
	for (char *p = strtok (paths, "\n"); p; p = strtok (0, "\n")) ui_list_add (g_list, (long) p, 0);
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	g_main = ui_window_ex (-1, -1, 420, 220, (long) TR ("Deux fenêtres"), 0);
	if (g_main <= 0) return 1;
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	g_hint = ui_label (16, 48, ui.w - 32, 60, (long) TR ("Déposez des fichiers ici, depuis Fichiers."), ULB_WRAP | ULB_DIM);
	g_open = ui_pushbutton (16, 120, 0, 0, (long) TR ("Ouvrir l'inspecteur"), UB_DEFAULT);
	ui_w_draw (0);
	ui_update_all ();

	for (;;)
	{
		long t = ui_event (200);
		long win = ui_ev_field (&ui, EV_WIN);
		if (t == WEV_CLOSE)
		{
			if (g_insp && win == g_insp) { insp_close (); continue; }
			break;					// la fenêtre principale : fin du programme
		}
		if (t == WEV_DROP)
		{
			static char b[1024];
			long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
			if (n > 0 && kind == UDT_FILES) { b[n] = 0; dropped (b); }
		}
		else if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);	// (va à la fenêtre de l'événement)
			if (w == g_open && code == UN_ACTIVATE) insp_open ();
		}
		else ui_w_idle ();
	}
	return 0;
}
```

## Traduction

Les textes d'une app s'écrivent en français dans le source et passent par `ui_tr`, qui rend leur traduction dans la langue du système (choisie dans Réglages, Région) ou le texte lui-même. Les dictionnaires sont des fichiers texte, une ligne par texte :

```sh
# data/lang-en.txt -- clé = le texte français du source
Formulaire = Form
Nouvel élément = New item
%d points, pinceau de %d px = %d points, %d px brush
```

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_tr` | texte | sa traduction (le texte reste au kit), ou le texte |
| `ui_tr_load` | chemin d'un dictionnaire | 1 si lu ; ses lignes s'ajoutent à celles du système |
| `ui_lang` | | la langue : `"fr"`, `"en"`... |

- Le dictionnaire du système est `SD0:/res/lang/<langue>.txt` ; la construction de la carte y fond le `lang/<langue>.txt` de chaque app.
- Une app peut aussi lire le sien : `ui_tr_load ("SD0:/apps/<app>.app/lang-en.txt")`, avec la langue de `ui_lang ()`.
- `ui_menu`, les dialogues et les widgets de UIKit traduisent leurs propres textes ; les libellés des widgets et les messages passent par `ui_tr` dans le programme (la macro `TR`).
- Une chaîne de format se traduit avant `snprintf`, comme dans les exemples : la clé est le format.

## Pièges

- **Les couleurs** : jamais de couleurs écrites en dur pour le fond et le texte ; les jetons (`TJ_*`) suivent le thème, clair ou sombre.
- **Le coin haut droit** : `FRAME_CAP_W` px laissés libres en haut à droite, pour les boutons de la fenêtre.
- **`ui_view_flush`** à chaque tour de la boucle, sinon une vue marquée n'est jamais redessinée.
- **`ui_w_idle`** quand `ui_event` rend 0, sinon les infobulles n'apparaissent pas.
- **Après `ui_window_ex`** d'une deuxième fenêtre, la courante a changé : `ui_window_use` pour revenir.
- **Un dialogue à soi** : lire ses widgets avant `ui_dialog_end`, qui les détruit.
- **Les textes rendus** par le kit (`ui_w_get_text`, `ui_tr`, `uikit_list_get`) lui appartiennent : les copier pour les garder.
- **`y` de `ui_text`** est la ligne de base ; pour un texte de 14 px dans une ligne de 30 px, `y` ≈ haut + 20.
- **Sous QEMU**, pas de GPU : une app UIKit ordinaire tourne partout, les surfaces aussi.

## Voir aussi

- [Le sommaire du guide](index_fr.md)
- [Le squelette d'une app](skeleton_fr.md) : un projet complet, prêt à copier
- [Une application](app_fr.md) : dossier, `app.txt`, construction
- [La libc et AppKit](libc_fr.md)
- [GpuKit](gpukit_fr.md), [MediaKit](mediakit_fr.md), [EmuKit](emukit_fr.md), [PrinterKit](printerkit_fr.md)
