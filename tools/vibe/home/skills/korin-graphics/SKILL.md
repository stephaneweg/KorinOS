---
name: korin-graphics
description: Dessin, animation et jeux sous Korin OS avec UIKit - le modèle d'affichage (tampon de la fenêtre, ui_update, pas de scintillement), la boucle de jeu cadencée par le compositeur (ui_frame_request, WEV_FRAME), vues, canevas, surfaces, images, clavier tenu, souris et tactile, redimensionnement, performances. À utiliser pour tout ce qui dessine soi-même ou bouge à l'écran (jeu, animation, graphique, éditeur de dessin).
user-invocable: true
---

# Dessiner et animer sous Korin

Compléter avec `korin-app` (dossier, `app.txt`, `pibuild`) et le guide `../korin/reference/uikit_en.md` (« Drawing and
text », « Hand-drawn views », « Canvases and surfaces », « Images »). Modèles réels à lire : `/sd0/src/apps/pinball`,
`critters`, `cosmozoom` (C++, boucle de jeu ci-dessous), `paint` (dessin), `mandelbrot`.

## 1. Le modèle d'affichage : déjà un double tampon

* La fenêtre a son **tampon de pixels** (`ui.surf`, `0x00RRGGBB`, `ui.w` × `ui.h`, une ligne = `ui.w` pixels). Tous les
  `ui_*` de dessin (`ui_fill`, `ui_text`, `ui_vp_*`, `ui_image_draw`) écrivent **dans ce tampon** : rien ne se voit.
* **`ui_update_all ()`** (ou `ui_update (x, y, l, h)` pour une zone) donne l'image au compositeur (Elegant), qui la montre
  d'un coup. C'est le « swap » : **dessiner toute l'image, puis un seul `ui_update_all`** par image. Jamais de
  `ui_update` entre deux morceaux d'une même image (on verrait le fond effacé : le scintillement).
* Pas besoin d'un second tampon à soi pour éviter le scintillement. En faire un seulement pour **garder** une image
  coûteuse d'une image à l'autre (un décor fixe, un dessin de l'utilisateur) : voir les canevas (§4) ou un tableau de
  pixels recopié (§5).
* Effacer = `ui_fill (0, 0, ui.w, ui.h, fond)` au début de chaque image ; tout redessiner ensuite.

## 2. La boucle d'une animation ou d'un jeu (celle des jeux du système)

```c
#include <time.h>
static unsigned now_ms (void)
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

	unsigned last = now_ms ();
	while (!g_quit)
	{
		long t;
		while ((t = ui_event (0)) > 0) one_event (t);	/* tous les événements en attente, sans attendre */
		unsigned now = now_ms (), dt = now - last; last = now;
		if (dt > 100) dt = 100;				/* une pause (menu, fenêtre cachée) : pas de saut */
		step (dt);					/* la logique, en millisecondes écoulées */
		draw ();					/* ui_fill du fond, puis tout le reste, dans ui.surf */
		ui_update_all ();				/* l'image donnée au compositeur */
		if (ui_frame_request ())			/* attendre qu'il l'ait prise : cadence de l'écran, */
			for (unsigned t0 = now_ms (); !g_quit && now_ms () - t0 < 200; )	/* pas de déchirure, */
			{								/* pas de 100 % CPU */
				long e = ui_event (10);
				if (e == WEV_FRAME) break;
				if (e > 0) one_event (e);
			}
	}
```

* **Les mouvements suivent `dt`** (vitesse en pixels par seconde × `dt / 1000.0`), jamais « un pas par tour de boucle » :
  le Pi, QEMU et le mode poche n'ont pas la même cadence.
* `one_event` traite `WEV_CLOSE` (quitter), `WEV_MENU`, `WEV_KEY`, `WEV_MOUSE`, `WEV_RESIZE` (passer à
  `uikit_w_event` pour que la fenêtre change de taille, puis recalculer la mise en page), `WEV_FOCUS` (perdu : relâcher
  toutes les touches tenues).
* Un jeu en pause ou une app qui n'anime rien : `ui_event (200)` qui attend, et ne redessiner que quand quelque chose
  change (ne pas faire tourner la boucle à vide).

## 3. Le clavier tenu, la souris, le tactile

* `WEV_KEY` : `EV_KIND` 1 appui, 0 relâche ; `EV_CODE` code HID (`KEY_LEFT`, `KEY_RIGHT`, `KEY_UP`, `KEY_DOWN`,
  `KEY_SPACE`, `KEY_ENTER`, `KEY_ESC`... déjà dans `uikit_abi.h`) ; `EV_CHAR` le caractère (disposition du clavier :
  pour du texte, pas pour des commandes de jeu).
* Garder **un tableau des touches tenues** (`bool held[256]`, mis à jour à l'appui et à la relâche) et le lire dans
  `step` : le mouvement continue tant que la touche est tenue, sans dépendre de la répétition du clavier. Le tout vidé
  sur `WEV_FOCUS` perdu.
* Les raccourcis de menu d'abord : à l'appui, `ui_menu_key (0)` (≥ 0 : une entrée de menu).
* `WEV_MOUSE` : `EV_KIND` 0 mouvement, 1 appui, 2 relâche ; `EV_X`, `EV_Y` dans le contenu ; `EV_CODE` les boutons.
  **Le tactile arrive comme la souris** (appui, mouvement, relâche), sans survol : en mode poche (écran tactile
  800 × 600, parfois sans clavier), prévoir des **boutons à l'écran** (gauche, droite, tir...) assez grands
  (≥ `ui_metric (UMT_CTL)`, 36 px en tactile) en plus des touches.
* Les manettes : `WEV_PAD` (voir `reference/emukit_en.md`).

## 4. Vues, canevas, surfaces : quoi choisir

| Besoin | Outil |
|---|---|
| un jeu ou une animation qui occupe toute la fenêtre | dessiner directement dans la fenêtre (§1, §2) |
| une zone dessinée **à côté de widgets** (graphique, plateau, aperçu) | une **vue** : `ui_view` + `ui_view_on (v, paint, mouse, key, wheel)` ; `ui_view_dirty (v)` quand elle change ; `ui_view_flush ()` à chaque tour de boucle. Le dessin de `paint` part de son coin (0, 0) |
| une image **persistante** qu'on enrichit (dessin à main levée, décor calculé une fois) | un **canevas** : `ui_canvas (x, y, l, h)`, puis `ui_canvas_begin (c)` … dessins … `ui_canvas_end ()`, `ui_w_draw (c)` pour le montrer ; ses pixels : `uikit_canvas_pixels` |
| beaucoup de pixels écrits soi-même à chaque image (émulateur, vidéo, rendu logiciel) | une **surface** : `ui_surface (x, y, l, h, WS_SF_XRGB8888)`, ses pixels `ui_surface_pixels`, puis `ui_surface_damage (s, x, y, l, h)` (pas de `ui_update`) : Elegant la compose sans copie |
| des sprites, un fond en image | `ui_image_load ("/sd0/apps/<nom>.app/sprite.png")` une fois au démarrage (PNG avec transparence, BMP), `ui_image_draw (img, x, y, 0, 0)` à chaque image (0, 0 : sa taille ; sinon mise à l'échelle) |
| la 3D, le GPU | GpuKit (`reference/gpukit_en.md`) ; sous QEMU il n'y a pas de GPU |

Les fichiers de l'app (images, sons, niveaux) vont dans `data/` du projet : `pibuild` les pose dans
`/apps/<nom>.app/`.

## 5. Dessiner vite

* Unités : `ui_fill`, `ui_frame`, `ui_pixel`, `ui_rbox`, `ui_text` en **pixels** ; `ui_vp_line (x0, y0, x1, y1,
  épaisseur, couleur)`, `ui_vp_disc`, `ui_vp_poly` en **1/16 de pixel** (lissés : `x * 16`, épaisseur 1,5 px = `24`).
  Les formes vectorielles (vaisseau, astéroïdes) : `ui_vp_poly` (rempli) ou une suite de `ui_vp_line`.
* Écrire soi-même dans `ui.surf` (`ui.surf[y * ui.w + x] = couleur`) est permis pour un rendu au pixel (fractale,
  particules nombreuses), en restant dans `0 ≤ x < ui.w`, `0 ≤ y < ui.h`. **Après un changement de taille**, le tampon
  change : relire `ui.surf`, `ui.w`, `ui.h` (ne jamais garder le pointeur).
* `ui_clip (x, y, l, h)` limite le dessin suivant (une zone de jeu sous un bandeau) ; `ui_clip (0, 0, 0, 0)` l'enlève.
* Le texte coûte cher : mesurer (`ui_text_width`) une fois, pas à chaque image ; un score se dessine par `ui_text` dans
  l'image, **pas** par un widget `ui_label` mis à jour à chaque image.
* Ne pas allouer dans la boucle (tableaux fixes d'objets, comme `MAX_BULLETS`).
* Le `y` de `ui_text` est la ligne de base : texte de 16 px dans une bande qui commence en `y0` → `y0 + 13`.

## 6. Thème, taille, chrome

* L'interface (fond de fenêtre, bandeaux, textes, boutons dessinés) prend les **jetons du thème** (`TJ_FACE`,
  `TJ_SURFACE`, `TJ_TEXTE1`, `TJ_ACCENT`...), qui changent à chaud (Givre clair, Onyx Noir sombre). Le **contenu** d'un
  jeu (ciel étoilé, sprites) peut avoir ses propres couleurs ; vérifier qu'il reste lisible dans les deux thèmes.
* `TJ_SOMBRE` vaut 1 si le thème est sombre : utile pour choisir la variante d'un sprite.
* `WF_RESIZABLE` toujours ; tout se place d'après `ui.w`, `ui.h` (le mode poche met la fenêtre en plein écran, 800 ×
  600). Une aire de jeu de taille fixe : la centrer et la mettre à l'échelle, ou agrandir le monde.
* Rien d'interactif dans le coin haut droit (`FRAME_CAP_W` × 32 px : les boutons de la fenêtre). Un bouton de pause
  dessiné : en haut à droite **sous** cette bande, ou ailleurs.
* Plein écran sans cadre : voir `ui_window_ex` et ses drapeaux `WF_*` dans le guide (le pinball le fait) ; ne pas en
  abuser.

## 7. Le son

AudioKit (`reference/audiokit_en.md`) : effets et musique. Ouvrir une fois au démarrage, jouer depuis la boucle sans
attendre ; prévoir une entrée de menu Son (coupé / actif) cochable (`ui_menu_state`).

## 8. Vérifier avant de rendre

* `pibuild` réussi (`Installe : ...`), puis la lancer détachée : `(/apps/<nom>.app/main >/dev/null 2>&1 &)` ;
  `sysstat | grep <nom>` dit si elle tourne ; `screenshot /tmp/screen.bmp` capture l'écran.
* Une image vide = presque toujours une **unité** (1/16 de pixel), un `ui_update_all` oublié, un dessin fait puis
  effacé par un `ui_fill` avant l'update, ou une couleur égale au fond.
