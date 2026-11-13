---
name: korin-app
description: Écrire, compiler avec le GCC de la carte (pibuild) et installer une application graphique ou une commande pour Korin OS, en C ou C++ sur UIKit. À utiliser dès qu'on demande de créer, modifier, construire ou installer une app ou un programme pour Korin.
user-invocable: true
---

# Créer une app Korin OS

Une app Korin est un dossier **`/sd0/apps/<nom>.app/`** (aussi écrit `/apps/<nom>.app` ou `SD0:/apps/<nom>.app`)
qui contient **`main`** (l'exécutable, un ELF statique AArch64) et **`app.txt`** (le manifeste). Le menu Korin, le
lanceur du panneau et PocketQI la trouvent tout seuls : rien d'autre à enregistrer.

On ne l'écrit pas à la main dans `/apps` : on écrit les sources dans **`/sd0/home/projects/<nom>/`**, puis
**`pibuild`** compile, lie avec tous les kits et installe. (Si `/gcc/bin/gcc` manque : `pkg add gcc make` ; si
`/gcc/lib/libuikit.a` manque : `pkg update gcc`, à demander à l'utilisateur.) Pour
une app en C# ou en FreeBASIC : compétences `korin-csharp`, `korin-freebasic`.

## Les étapes

1. Choisir un **nom en anglais**, en minuscules, sans espace (`counter`, `notes2`) : le dossier des sources, l'app
   installée (`/apps/<nom>.app`) et l'argument de `pibuild` portent ce nom.
2. Créer `/sd0/home/projects/<nom>/` avec :
   * `main.c` (ou `main.cpp`) ; **tous** les `.c` et `.cpp` du dossier et de ses sous-dossiers sont compilés ;
   * `app.txt`, le manifeste (plus bas) ;
   * au besoin `data/lang-en.txt` (traduction anglaise), `help_fr.md` et `help_en.md` (l'aide du menu Aide).
3. Construire et installer :

   ```sh
   pibuild app /sd0/home/projects/<nom> <nom>
   ```

   Réussi quand la dernière ligne est `Installe : /apps/<nom>.app/main`. Les erreurs de `gcc` s'affichent comme
   d'habitude ; corriger et relancer (seul ce qui a changé est recompilé ; les objets sont dans
   `/tmp/pibuild/<nom>`). Compter de quelques secondes à une minute par fichier.
4. Vérifier : `ls -l /apps/<nom>.app` doit montrer `main` et `app.txt`.
5. Lancer l'app **sans bloquer le shell** (une app graphique ne rend pas la main) :

   ```sh
   (/apps/<nom>.app/main >/dev/null 2>&1 &)
   ```

   ou dire à l'utilisateur de l'ouvrir depuis le lanceur (catégorie de `app.txt`). Ne jamais la lancer au premier
   plan.

`pibuild` sait aussi : `pibuild bin <dossier> <nom>` → une commande `/bin/<nom>` (programme de terminal) ;
`sys`, `svc` → un programme système, un service (voir la compétence `korin`). Pour un petit essai sans dossier :
`gcc -O2 test.c -o /tmp/test && /tmp/test` ; avec un kit, ajouter ses en-têtes et son relais :
`gcc -O2 -I/gcc/include/kits test.c -L/gcc/lib -luikit -o /tmp/test` (`pibuild` les donne tous d'office).

## app.txt

Lignes `clé = valeur`, `#` commence un commentaire (pas de `#` dans une valeur). Fichier en UTF-8.

```
# Manifeste de Counter (lu par la barre de menus, le lanceur et PocketQI)
name        = Compteur
category    = Accessoires
description = Compte les clics sur un bouton.
keywords    = compteur clic bouton
help        = Le bouton ajoute un au compteur.
```

| Clé | Rôle |
|---|---|
| `name` | le nom affiché (en français : le dictionnaire du système le traduit) |
| `category` | catégorie du lanceur, **en français**, l'une de : Accessoires, Bureautique, Création, Développement, Internet, Jeux, Multimédia, Système (sans elle : Autres) |
| `opens` | extensions ouvertes (`txt png`) : Fichiers passe le chemin en `argv[1]` |
| `description`, `keywords`, `help` | ce que l'assistant PocketQI sait de l'app (plusieurs `help` possibles) |

L'icône est `icon.rgba` dans le dossier de l'app (facultative : sans elle, le lanceur en dessine une).

## Le modèle : une fenêtre, un texte, un bouton

À copier et adapter. C'est du C ; le C++ marche pareil (`main.cpp`, `g++`).

```c
// main.c -- <ce que fait l'app> (Korin OS, UIKit)
#include <stdio.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))	// texte traduit (la clé est le texte français)

static struct ui_vars ui;		// fenêtre courante : ui.w, ui.h, ui.surf, dernier événement, polices
static const unsigned *s_jet;		// les jetons du thème
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

static long g_label, g_button;		// les widgets
static int g_count;

// place les widgets d'après la taille ; appelée aussi quand la fenêtre change de taille
static long on_resize (long w, long h)
{
	int ctl = (int) ui_metric (UMT_CTL);	// hauteur d'un contrôle (plus grande en mode tactile)
	ui_fill (0, 0, w, h, jet (TJ_FACE));	// le fond de la fenêtre
	ui_w_move (g_label, 24, h / 2 - ctl - 8, w - 48, ctl);
	ui_w_move (g_button, (w - 160) / 2, h / 2 + 8, 160, ctl);
	ui_w_draw (0);				// dessine tous les widgets
	ui_update_all ();			// montre la fenêtre
	return 0;
}

static void show_count (void)
{
	char t[64];
	snprintf (t, sizeof t, TR ("Clics : %d"), g_count);
	ui_w_set_text (g_label, (long) t);	// le widget se redessine seul
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;	// ouvre UIKit et AppKit, charge les polices
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 420, 240, (long) TR ("Compteur"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (300, 200);
	g_label = ui_label (0, 0, 10, 10, (long) "", ULB_CENTER | ULB_BOLD);
	g_button = ui_pushbutton (0, 0, 10, 10, (long) TR ("Cliquer"), UB_DEFAULT);
	ui_on_resize ((long) on_resize, 0);
	show_count ();
	on_resize (ui.w, ui.h);

	for (;;)
	{
		long t = ui_event (200);		// attend un événement (200 ms au plus) -> WEV_* (0 : rien)
		if (t == WEV_CLOSE) break;		// case de fermeture, ou Quitter du menu de l'app
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);	// le widget concerné, son code UN_*
			if (w == g_button && code == UN_ACTIVATE) { g_count++; show_count (); }
		}
		else ui_w_idle ();			// rien pendant l'attente : les infobulles
		ui_view_flush ();			// redessine les vues marquées (ui_view_dirty)
	}
	return 0;
}
```

Les apps du système sont des modèles réels, sources sur la carte : `/sd0/src/apps/<app>` (en C/C++ : `disks`,
`photos`, `calendar`, `cardfile`, `notes`, `vibe` en C). Chercher comment elles font avant d'inventer :
`rg -n "ui_menu" /sd0/src/apps --glob "*.c*"`. Pour plus (menus, champs, listes, vue dessinée, dialogues, plusieurs
fenêtres) : le squelette complet en C++ dans
`skeleton/` (à côté de ce fichier), et le guide UIKit `../korin/reference/uikit_en.md`. **Lire le guide plutôt que
deviner** une fonction : toutes les fonctions sont des `long ui_xxx (long, ...)`, les chaînes se passent en
`(long) "texte"`, et leur liste exacte est dans `/gcc/include/kits/uikit_abi.h` (`grep -n "ui_xxx" ...`).

## Dessiner, animer (jeux)

**Avant d'appeler une fonction de dessin, lire son tableau** dans `../korin/reference/uikit_en.md` (« Drawing and
text », « Hand-drawn views ») : les unités et l'ordre des arguments ne se devinent pas.

| Fonction | Arguments | Unité |
|---|---|---|
| `ui_fill`, `ui_frame` | x, y, largeur, hauteur, couleur | pixels |
| `ui_pixel` | x, y, couleur | pixels |
| `ui_rbox` | x, y, l, h, rayon, couleur haut, couleur bas | pixels |
| `ui_text` | texte, x, **ligne de base**, couleur, taille px, police (`(long) ui.font`), largeur max | pixels |
| `ui_vp_line` | x0, y0, x1, y1, **épaisseur**, couleur | **1/16 de pixel** : `x * 16`, une épaisseur de 2 px = `32` |
| `ui_vp_disc` | cx, cy, rayon, couleur | **1/16 de pixel** |
| `ui_vp_poly` | tableau `int32_t` (x, y, x, y…), nombre de points, couleur | **1/16 de pixel** |

Les couleurs sont `0x00RRGGBB` (les jetons du thème). Les codes de touches (`KEY_LEFT`, `KEY_RIGHT`, `KEY_UP`,
`KEY_DOWN`, `KEY_ENTER`, `KEY_ESC`, `KEY_SPACE`...) sont **déjà** dans `uikit_abi.h` : ne pas les redéfinir
(`grep -n "define KEY_" /gcc/include/kits/uikit_abi.h`). `WEV_KEY` : `EV_KIND` 1 appui, 0 relâche ; `EV_CODE` le code
HID.

**Une animation ou un jeu : charger la compétence `korin-graphics`** (le modèle d'affichage sans scintillement, la
boucle cadencée par le compositeur, le clavier tenu, le tactile, les vues, canevas, surfaces, images). Exemples réels :
`/sd0/src/apps/pinball`, `critters`, `cosmozoom` (C++).

## Règles qu'une app Korin doit suivre

* **Pas de barre de titre** (chrome de Korin) : Elegant dessine trois boutons (réduire, agrandir, fermer) **en haut
  à droite du contenu**, sur `FRAME_CAP_W` pixels de large et 32 de haut. **Rien d'interactif là** : un bouton
  du haut se place à `x + largeur ≤ w - FRAME_CAP_W - 8`. Les 32 px du haut servent aussi à déplacer la fenêtre
  (un clic simple arrive quand même au programme).
* **Couleurs du thème uniquement** : `jet (TJ_FACE)` fond de fenêtre, `TJ_SURFACE` zones de contenu, `TJ_CREUX`
  creux, `TJ_LIGNE` traits, `TJ_TEXTE1`/`TJ_TEXTE2`/`TJ_TEXTE3` textes, `TJ_ACCENT` (+ `TJ_ACCENT_INK` pour écrire
  dessus), `TJ_DANGER`, `TJ_SUCCES`, `TJ_ALERTE`. Jamais une couleur en dur pour l'interface : le thème (Givre clair,
  Onyx Noir sombre) change à chaud. Les widgets se colorent seuls.
* **Mode poche** (petit écran tactile, 800 × 600) : chaque fenêtre y est mise **en plein écran**. Donc toujours
  `WF_RESIZABLE`, une mise en page calculée dans `on_resize` à partir de `w` et `h` (pas de positions figées pour
  une seule taille), `ui_min_size` ≤ 760 × 520, les hauteurs de contrôles prises de `ui_metric (UMT_CTL)` (36 px en
  tactile), des cibles assez grandes pour le doigt.
* **Menus** dans la barre de menus du système : `ui_menu ((long) "MFichier\nI1\tNouveau\tCtrl+N\nMAide\nI2\tÀ propos\n")`
  (`M` un menu, `I<id>` une entrée, tabulation avant le raccourci) ; le choix arrive en `WEV_MENU`, l'id par
  `ui_ev_field (&ui, EV_CODE)`. Quitter, dans le menu au nom de l'app, arrive en `WEV_CLOSE`.
* **Textes** écrits en français dans le source, passés par `TR (...)` ; la traduction anglaise dans
  `data/lang-en.txt` (`texte français = English text`), chargée au début par
  `ui_tr_load ((long) "/sd0/apps/<nom>.app/lang-en.txt")` (ou `lang-<ui_lang ()>.txt`). Les menus donnés à
  `ui_menu` sont traduits par UIKit lui-même.
* **Noms en anglais** (dossier, fichiers, fonctions, variables) ; **commentaires en français**.
* Le `y` de `ui_text` est la **ligne de base** ; `ui_view_flush ()` à chaque tour de boucle ; `ui_w_idle ()` quand
  `ui_event` rend 0. Un texte rendu par le kit (`ui_w_get_text`, `ui_tr`) lui appartient : le copier pour le garder.
* Une app sans barre de menus n'a rien à faire de plus ; une aide (`help_fr.md`, `help_en.md`, Markdown) ajoute le
  menu Aide.

## Si ça ne marche pas

* **Vérifier d'abord** que les kits sont installés pour le C : `ls /gcc/include/kits/uikit_abi.h /gcc/lib/libuikit.a`.
  S'ils manquent (le paquet `gcc` avant 1.0.10 ne les avait pas), dire à l'utilisateur de lancer
  `pkg update gcc` (ou `pkg add gcc`) : ne pas chercher à les refaire, ni à écrire les déclarations à la main.
* `uikit.h: No such file` : passer par `pibuild` (il donne `-I/gcc/include -I/gcc/include/kits` et tous les
  `/gcc/lib/lib<kit>.a`) ; s'il échoue aussi, le point précédent.
* `undefined reference to ui_xxx` : la fonction n'existe pas sous ce nom : `grep -n "ui_xxx" /gcc/include/kits/uikit_abi.h`.
* `pibuild : ... main.c introuvable` : le dossier doit contenir `main.c`, `main.cpp`, `<nom>.c` ou `<nom>.cpp`.
* La fenêtre ne s'ouvre pas quand on lance `main` depuis le shell de Vibe : c'est normal sans session graphique (il
  faut le bureau de Korin) ; demander à l'utilisateur de l'ouvrir depuis le lanceur.
* Ce que la libc et la couche POSIX n'ont pas (pas de `/proc`, pas d'IPv6, pas de fuseaux horaires...) :
  compétence `korin`, `reference/libc_en.md`.
