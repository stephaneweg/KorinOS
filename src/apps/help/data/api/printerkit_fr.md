# PrinterKit : imprimer

**PrinterKit** imprime : il montre le dialogue d'impression, enregistre les pages d'un travail, et les confie au service `print` (**printd**). Le service les rejoue pour l'imprimante choisie, sans que l'application ait à attendre :

- l'imprimante **PDF**, toujours là : les pages deviennent un fichier PDF, le texte reste du texte ;
- une imprimante **réseau** sans pilote (IPP Everywhere, AirPrint) : les pages sont rendues à sa résolution et envoyées en PWG Raster, ou en PDF si elle ne prend que cela.

L'utilisateur est prévenu par une notification à la fin. Les imprimantes s'ajoutent dans **Réglages > Imprimantes**. Le kit est en assembleur, dans `SD0:/lib/printerkit.so` ; il se sert d'AppKit, d'UIKit (le dialogue) et d'ArchiveKit (les images). Paint, Photos, Sheet, Slides, Letters et le Lecteur PDF impriment par lui.

## Ouvrir le kit

```c
#include <printerkit.h>		/* printerkit_abi.h et les structures PrintSetup... */

if (!printerkit_kit_open ())
	return 1;		/* SD0:/lib/printerkit.so absent */
```

Rien à ajouter au lien. Chaque argument est un `long` ; une structure se passe par son adresse, `(long) &s`.

**Longueurs** : en **millièmes de point** (1/72 000 de pouce), des entiers : une page A4 fait 595 276 sur 841 890. `print_mpt (pt)` convertit des points (`float`) ; 72 000 font un pouce, 28 346 un centimètre. L'axe **y descend** depuis le haut de la page. **Couleurs** : `0xRRGGBB`.

## Le dialogue

| Fonction | Arguments | Résultat |
|---|---|---|
| `print_setup_default` | `PrintSetup *` | l'imprimante par défaut, son papier, portrait, un exemplaire |
| `print_dialog` | `PrintSetup *`, `PrintDialogInfo *` | 1 : imprimer (le `PrintSetup` est rempli), 0 : abandon |
| `print_setup_paper` | `PrintSetup *`, papier IPP (0 : garder), orientation | 1 ; 0 : papier inconnu |

`PrintSetup` (ce que l'utilisateur a choisi) : `size` (à remplir : `sizeof`), `printer` (nom ; « PDF » pour l'imprimante PDF), `media` (papier, nom IPP comme `iso_a4_210x297mm`), `paper_w`, `paper_h` (la page, orientation appliquée), `margin_l`, `margin_t`, `margin_r`, `margin_b` (ce que l'imprimante ne peut pas imprimer), `orientation` (`PRINT_PORTRAIT`, `PRINT_LANDSCAPE`), `copies`, `mono`, `quality` (`PRINT_DRAFT`, `PRINT_NORMAL`, `PRINT_HIGH`), `from`, `to` (pages à imprimer, depuis 1 ; 0, 0 : toutes), `output` (le fichier qu'écrit l'imprimante PDF).

`PrintDialogInfo` (ce que l'application dit du document) : `size`, `title`, `pages` (0 : inconnu), `current` (la page montrée, pour le choix « page courante » ; 0 : pas de choix), `flags` (`PRINT_DLG_OWN_PAPER` : le document a sa propre taille de page, comme une diapositive, et le dialogue ne propose pas de papier), `paper_w`, `paper_h`.

Le dialogue est fait de widgets d'UIKit : il sert aux applications graphiques (UIKit ouvert par `uikit_open`). Avec l'imprimante PDF, il demande aussi où enregistrer le fichier (dans `SD0:/home/documents` par défaut).

## Un travail et ses pages

Un seul travail à la fois par programme.

| Fonction | Arguments | Résultat |
|---|---|---|
| `print_begin` | `PrintSetup *`, titre | travail ; 0 : la file ne peut pas être écrite |
| `print_page` | travail, largeur, hauteur (0, 0 : le papier) | 1 ; 0 : page hors de celles choisies, à ne pas dessiner |
| `print_end` | travail | numéro du travail dans la file (> 0) ; < 0 : refusé |
| `print_abort` | travail | rien n'est imprimé |

Une page commence à `print_page` et finit à la suivante (ou à `print_end`). Quand l'utilisateur a choisi « pages 3 à 5 », `print_page` rend 0 pour les autres : l'application saute leur dessin, ce qui gagne du temps.

## Dessiner

| Fonction | Arguments | Résultat |
|---|---|---|
| `print_rect` | travail, x, y, l, h, couleur | rectangle plein |
| `print_frame` | travail, x, y, l, h, épaisseur, couleur | contour d'un rectangle |
| `print_line` | travail, x0, y0, x1, y1, épaisseur, couleur | trait |
| `print_path_move` | travail, x, y | commence un tracé |
| `print_path_line` | travail, x, y | segment |
| `print_path_curve` | travail, x1, y1, x2, y2, x3, y3 | courbe de Bézier cubique |
| `print_path_close` | travail | ferme le contour courant |
| `print_path_fill` | travail, couleur | remplit le tracé |
| `print_path_stroke` | travail, épaisseur, couleur | trace le contour |
| `print_image_mode` | travail, `PRINT_IMG_ALPHA` ou 0 | pour les images suivantes : pixels `0xAARRGGBB`, ou opaques `0x00RRGGBB` |
| `print_image` | travail, pixels, largeur, hauteur (en pixels), x, y, l, h | image mise à l'échelle dans le rectangle ; réduite à 300 points par pouce et compressée par le kit |
| `print_image_jpeg` | travail, fichier JPEG, octets, `largeur \| hauteur << 32`, x, y, l, h | un JPEG tel quel (Photos) |

## Texte

| Fonction | Arguments | Résultat |
|---|---|---|
| `print_font` | travail, famille (« DejaVu Sans »...), `PRINT_BOLD` \| `PRINT_ITALIC` | police ; -1 : aucune |
| `print_text` | travail, police, taille, x, ligne de base, texte UTF-8, couleur | avance (largeur écrite) |
| `print_text_width` | travail, police, taille, texte UTF-8 | avance, sans rien écrire |
| `print_font_metrics` | travail, police, taille, `long m[3]` | ascendante, descendante, interligne |
| `print_font_data` | travail, fichier TrueType, octets | police ; les octets sont gardés jusqu'à `print_end` |
| `print_glyph` | travail, police \| style << 16, taille, x, base, glyphe, caractère, couleur | un glyphe placé par l'application |

`print_text` passe le texte à printd, qui le dispose (sans crénage). Une application qui met elle-même son texte en page (Letters) charge ses polices par `print_font_data` et place chaque glyphe par `print_glyph`.

## Les imprimantes et la file

Ce que fait l'applet **Imprimantes** de Réglages, à la portée de toute application.

| Fonction | Arguments | Résultat |
|---|---|---|
| `print_printers` | `PrintPrinter[]`, places | nombre d'imprimantes |
| `print_printer_media` | imprimante, rang, nom, taille, libellé, taille | 1 : le papier n° rang (nom IPP, libellé) ; 0 : plus de papier |
| `print_printer_add` | nom, adresse, out, taille | 1 (out : son nom) ; 0 (out : pourquoi) |
| `print_printers_find` | `PrintFound[]`, places | imprimantes trouvées sur le réseau local (mDNS, quelques secondes) |
| `print_printer_remove` | nom | 1 ; 0 : inconnue (l'imprimante PDF reste) |
| `print_printer_default` | nom | 1 ; 0 : inconnue |
| `print_printer_status` | nom, out, taille | 1 : prête ; 0 : non (out : ce qu'elle dit) |
| `print_jobs` | `PrintJobInfo[]`, places | nombre de travaux : la file, puis les derniers finis |
| `print_job_cancel` | numéro | 1 ; 0 : inconnu |
| `print_jobs_forget` | aucun | les travaux finis quittent la liste |

`PrintPrinter` : `name`, `kind` (« pdf », « ipp »), `uri`, `model`, `color`, `copies`, `dpi`, `is_default`, `quality`, `media_default`. `PrintFound` : `address`, `name`, `model`, `usable`. `PrintJobInfo` : `id`, `state` (`PRINT_JOB_QUEUED`, `_PREPARING`, `_SENDING`, `_PRINTING`, `_DONE`, `_FAILED`, `_CANCELED`), `pages`, `page` (en cours), `printer`, `title`, `message`.

## Exemple : un PDF sans dialogue

Un programme du Terminal qui imprime une page sur l'imprimante PDF, dans `/tmp/demo.pdf`, sans montrer de dialogue, puis suit son travail dans la file.

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <printerkit.h>

int main (void)
{
	if (!printerkit_kit_open ())
		return 1;
	PrintSetup s;
	print_setup_default ((long) &s);
	strcpy (s.printer, "PDF");
	strcpy (s.output, "/tmp/demo.pdf");
	if (!s.paper_w)
		print_setup_paper ((long) &s, (long) "iso_a4_210x297mm", PRINT_PORTRAIT);

	long j = print_begin ((long) &s, (long) "Demo");
	if (!j)
	{
		printf ("la file d'impression ne peut etre ecrite\n");
		return 1;
	}
	long cm = 28346, left = 2 * cm, width = s.paper_w - 4 * cm;
	long f = print_font (j, (long) "DejaVu Sans", 0), fb = print_font (j, (long) "DejaVu Sans", PRINT_BOLD);
	if (print_page (j, 0, 0))
	{
		long m[3];
		print_font_metrics (j, fb, print_mpt (24), (long) m);
		long y = 3 * cm + m[0];				/* ligne de base sous la marge */
		print_text (j, fb, print_mpt (24), left, y, (long) "Korin OS imprime", 0x203040);
		print_line (j, left, y + m[1] + 6000, left + width, y + m[1] + 6000, 1500, 0x3060C0);

		const char *t = "Rectangles, traits, traces et texte : tout est en milliemes de point.";
		long tw = print_text_width (j, f, print_mpt (12), (long) t);
		print_text (j, f, print_mpt (12), left + (width - tw) / 2, y + 2 * cm, (long) t, 0x000000);

		print_rect (j, left, y + 3 * cm, 4 * cm, 2 * cm, 0xE8EEF8);
		print_frame (j, left, y + 3 * cm, 4 * cm, 2 * cm, 1000, 0x3060C0);

		long cx = left + 8 * cm, cy = y + 3 * cm;	/* une goutte : deux courbes de Bezier */
		print_path_move (j, cx, cy);
		print_path_curve (j, cx + 2 * cm, cy + cm, cx + cm, cy + 2 * cm, cx, cy + 2 * cm);
		print_path_curve (j, cx - cm, cy + 2 * cm, cx - 2 * cm, cy + cm, cx, cy);
		print_path_close (j);
		print_path_fill (j, 0x60A0E0);
		print_path_stroke (j, 800, 0x203040);

		static unsigned px[64 * 64];			/* un degrade, pixels 0x00RRGGBB */
		for (int i = 0; i < 64 * 64; i++)
			px[i] = (unsigned) (i % 64 * 4) << 16 | (unsigned) (i / 64 * 4) << 8 | 0xC0;
		print_image_mode (j, 0);
		print_image (j, (long) px, 64, 64, left + 12 * cm, y + 3 * cm, 2 * cm, 2 * cm);
	}
	long id = print_end (j);
	if (id < 0)
	{
		printf ("travail refuse (%ld)\n", id);
		return 1;
	}
	printf ("travail %ld en file\n", id);

	PrintJobInfo jobs[16];
	for (int tries = 0; tries < 60; tries++)		/* une minute au plus */
	{
		long n = print_jobs ((long) jobs, 16);
		for (long i = 0; i < n; i++)
			if (jobs[i].id == (unsigned) id && jobs[i].state >= PRINT_JOB_DONE)
			{
				printf ("%s\n", jobs[i].message);
				return jobs[i].state == PRINT_JOB_DONE ? 0 : 1;
			}
		sleep (1);
	}
	printf ("toujours en cours\n");
	return 0;
}
```

## Exemple : le menu Imprimer d'une application

Une application UIKit qui dessine un tableau et l'imprime par **Fichier > Imprimer...** (Ctrl+P). Le dialogue donne l'imprimante, le papier, les marges : le dessin se place dans ce que l'imprimante peut imprimer.

```cpp
#include <stdio.h>
#include <string.h>
#include <uikit.h>
#include <printerkit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
enum { J_FACE = 0, J_SURFACE = 1, J_LIGNE = 5, J_TEXTE1 = 7 };
static unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

enum { M_PRINT = 1 };
static long g_view;
static const int VALUES[6] = { 12, 30, 22, 45, 38, 50 };

static void view_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_SURFACE));
	long bw = (w - 40) / 6;
	for (int i = 0; i < 6; i++)
	{
		long bh = (h - 40) * VALUES[i] / 50;
		ui_fill (20 + i * bw + 4, h - 20 - bh, bw - 8, bh, jet (J_LIGNE));
	}
}

// le meme tableau sur le papier, dans la zone imprimable
static void print_chart ()
{
	if (!printerkit_kit_open ())
	{
		ui_message ((long) TR ("Imprimer"), (long) TR ("Le service d'impression n'est pas install\xC3\xA9."), UM_OK);
		return;
	}
	PrintSetup s;
	print_setup_default ((long) &s);
	PrintDialogInfo di = { sizeof di, TR ("Tableau"), 1, 0, 0, 0, 0 };
	if (!print_dialog ((long) &s, (long) &di))
		return;
	long j = print_begin ((long) &s, (long) TR ("Tableau"));
	if (j && print_page (j, 0, 0))
	{
		long m = 36000;						// un demi-pouce, ou la marge de l'imprimante
		long l = s.margin_l > m ? s.margin_l : m, t = s.margin_t > m ? s.margin_t : m;
		long w = s.paper_w - l - (s.margin_r > m ? s.margin_r : m), h = w * 2 / 3;
		long f = print_font (j, (long) "DejaVu Sans", PRINT_BOLD);
		print_text (j, f, print_mpt (18), l, t + print_mpt (18), (long) TR ("Tableau"), 0x000000);
		t += print_mpt (36);
		print_frame (j, l, t, w, h, 1000, 0x808080);
		long bw = w / 6;
		for (int i = 0; i < 6; i++)
		{
			long bh = h * VALUES[i] / 50;
			print_rect (j, l + i * bw + bw / 8, t + h - bh, bw * 3 / 4, bh, 0x3060C0);
		}
	}
	if (!j || print_end (j) < 0)
		ui_message ((long) TR ("Imprimer"), (long) TR ("Le travail n'a pas pu \xC3\xAAtre mis dans la file."), UM_OK);
}

static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_w_move (g_view, 12, 48, w - 24, h - 60);
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	return 0;
}

int main (int, char **)
{
	if (!uikit_open (&ui))
		return 1;
	s_jet = (const unsigned *) ui_jetons ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	if (ui_window_ex ((scr[0] - 560) / 2, scr[3] + 40, 560, 400, (long) TR ("Tableau"), WF_RESIZABLE) <= 0)
		return 1;
	ui_min_size (320, 240);
	ui_on_resize ((long) on_resize, 0);
	g_view = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_view, (long) view_paint, 0, 0, 0);
	ui_menu ((long) "MFichier\n" "I1\tImprimer...\tCtrl+P\n");
	on_resize (ui.w, ui.h);

	for (;;)
	{
		long t = ui_event (100);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MENU)
		{
			if (ui_ev_field (&ui, EV_CODE) == M_PRINT)
				print_chart ();
		}
		else if (t > 0)
		{
			long code = 0, taken = 0;
			uikit_w_event (&code, &taken);
			if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND) && !taken && ui_menu_key (0) == M_PRINT)
				print_chart ();
		}
		else
			ui_w_idle ();
		ui_view_flush ();
	}
	return 0;
}
```

## Exemple : les imprimantes et la file

Liste les imprimantes connues, leurs papiers et leur état, puis la file des travaux.

```c
#include <stdio.h>
#include <printerkit.h>

static const char *STATES[] = { "en file", "preparation", "envoi", "impression", "fini", "echec", "annule" };

int main (void)
{
	if (!printerkit_kit_open ())
		return 1;
	PrintPrinter p[8];
	long n = print_printers ((long) p, 8);
	for (long i = 0; i < n; i++)
	{
		char st[128];
		long ready = print_printer_status ((long) p[i].name, (long) st, sizeof st);
		printf ("%s%s (%s, %s) : %s%s%s\n", p[i].name, p[i].is_default ? " *" : "", p[i].kind,
			p[i].color ? "couleur" : "noir et blanc", ready ? "prete" : "pas prete",
			st[0] ? " - " : "", st);
		char media[64], label[64];
		for (long k = 0; k < 4 && print_printer_media ((long) p[i].name, k, (long) media, sizeof media,
			(long) label, sizeof label); k++)
			printf ("    %s (%s)\n", label, media);
	}

	PrintJobInfo jobs[16];
	long nj = print_jobs ((long) jobs, 16);
	printf ("%ld travail(s)\n", nj);
	for (long i = 0; i < nj; i++)
	{
		int s = jobs[i].state;
		printf ("  %u %-20s %-12s page %d/%d %s\n", jobs[i].id, jobs[i].title,
			s >= 0 && s <= PRINT_JOB_CANCELED ? STATES[s] : "?", jobs[i].page, jobs[i].pages, jobs[i].message);
	}
	print_jobs_forget ();
	return 0;
}
```

## Imprimer par le code d'export PDF : `pdfprint.h`

Une application qui sait déjà « Exporter en PDF » avec `pdfwrite.h` (l'écrivain PDF de Letters, classe `pdfw::Writer`) imprime par le même code de dessin. `pdfprint.h` donne `PrintWriter`, un `pdfw::Writer` dont les pages vont dans un travail d'impression, et `print_ask`, qui ouvre le kit, montre le dialogue et commence le travail. Les pages hors de celles choisies sont écartées sans que l'application ait à le savoir ; liens et signets sont ignorés. L'en-tête s'inclut **après** `pdfwrite.h`, qui vient de Letters (`src/apps/letters/pdfwrite.h`) :

```cpp
#include "pdfwrite.h"
#include <pdfprint.h>

void print_document ()
{
	long job = print_ask ("Rapport", page_count (), current_page (), 595.28f, 841.89f);	// points
	if (!job)
		return;					// abandon, ou file impossible a ecrire
	{
		PrintWriter w (job);
		export_pages (w);			// le code de l'export PDF, inchange
	}
	print_end (job);
}
```

`print_ask (titre, pages, page courante, largeur, hauteur)` : la taille de page en points (`PRINT_DLG_OWN_PAPER` : le document a sa taille) ; rend le travail, ou 0.

## Pièges

- **Un travail à la fois** par programme : finir par `print_end` ou `print_abort` avant d'en commencer un autre.
- **`print_page` à 0** veut dire « page non demandée », pas une erreur : ne dessinez rien, passez à la suivante.
- **Les marges** du `PrintSetup` sont ce que l'imprimante ne sait pas imprimer : un dessin doit rester dedans (l'imprimante PDF n'en a pas).
- **Le dialogue** est une fenêtre : un programme sans fenêtre remplit lui-même le `PrintSetup` (`printer`, `output`), comme dans le premier exemple.
- **Pas de crénage** dans `print_text`, ni dans sa mesure ; pour une mise en page fine, `print_font_data` et `print_glyph`.
- **IPP en clair seulement** (port 631) : une imprimante qui exige IPPS est refusée, et le message le dit.

## Voir aussi

- [UIKit](uikit_fr.md) : la fenêtre, les menus, la boucle d'événements.
- [ArchiveKit](archivekit_fr.md) et [DocumentKit](documentkit_fr.md) : les documents à imprimer.
- [Le squelette d'une app](skeleton_fr.md).
