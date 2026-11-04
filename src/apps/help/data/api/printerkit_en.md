# PrinterKit: printing

**PrinterKit** prints: it shows the print dialog, records the pages of a job, and hands them to the `print` service (**printd**). The service replays them for the chosen printer, without the application having to wait:

- the **PDF** printer, always there: the pages become a PDF file, text stays text;
- a driverless **network** printer (IPP Everywhere, AirPrint): the pages are rendered at its resolution and sent as PWG Raster, or as PDF if that is all it takes.

The user is told by a notification at the end. Printers are added in **Settings > Printers**. The kit is written in assembly, in `SD0:/lib/printerkit.so`; it uses AppKit, UIKit (the dialog) and ArchiveKit (images). Paint, Photos, Sheet, Slides, Letters and the PDF Reader print through it.

## Opening the kit

```c
#include <printerkit.h>		/* printerkit_abi.h and the PrintSetup... structures */

if (!printerkit_kit_open ())
	return 1;		/* SD0:/lib/printerkit.so missing */
```

Nothing to add to the link. Every argument is a `long`; a structure is passed by its address, `(long) &s`.

**Lengths**: in **thousandths of a point** (1/72,000 inch), as integers: an A4 page is 595,276 by 841,890. `print_mpt (pt)` converts points (`float`); 72,000 is an inch, 28,346 a centimetre. The **y axis goes down** from the top of the page. **Colours**: `0xRRGGBB`.

## The dialog

| Function | Arguments | Result |
|---|---|---|
| `print_setup_default` | `PrintSetup *` | the default printer, its paper, portrait, one copy |
| `print_dialog` | `PrintSetup *`, `PrintDialogInfo *` | 1: print (the `PrintSetup` is filled in), 0: cancelled |
| `print_setup_paper` | `PrintSetup *`, IPP paper (0: keep), orientation | 1; 0: unknown paper |

`PrintSetup` (what the user chose): `size` (to fill in: `sizeof`), `printer` (name; "PDF" for the PDF printer), `media` (paper, IPP name such as `iso_a4_210x297mm`), `paper_w`, `paper_h` (the page, orientation applied), `margin_l`, `margin_t`, `margin_r`, `margin_b` (what the printer cannot print), `orientation` (`PRINT_PORTRAIT`, `PRINT_LANDSCAPE`), `copies`, `mono`, `quality` (`PRINT_DRAFT`, `PRINT_NORMAL`, `PRINT_HIGH`), `from`, `to` (pages to print, from 1; 0, 0: all), `output` (the file the PDF printer writes).

`PrintDialogInfo` (what the application says about the document): `size`, `title`, `pages` (0: unknown), `current` (the page shown, for the "current page" choice; 0: no such choice), `flags` (`PRINT_DLG_OWN_PAPER`: the document has its own page size, like a slide, and the dialog offers no paper), `paper_w`, `paper_h`.

The dialog is made of UIKit widgets: it is meant for graphical applications (UIKit opened by `uikit_open`). With the PDF printer, it also asks where to save the file (in `SD0:/home/documents` by default).

## A job and its pages

One job at a time per program.

| Function | Arguments | Result |
|---|---|---|
| `print_begin` | `PrintSetup *`, title | job; 0: the queue cannot be written |
| `print_page` | job, width, height (0, 0: the paper) | 1; 0: page outside those chosen, do not draw it |
| `print_end` | job | number of the job in the queue (> 0); < 0: refused |
| `print_abort` | job | nothing is printed |

A page starts at `print_page` and ends at the next one (or at `print_end`). When the user chose "pages 3 to 5", `print_page` returns 0 for the others: the application skips drawing them, which saves time.

## Drawing

| Function | Arguments | Result |
|---|---|---|
| `print_rect` | job, x, y, w, h, colour | filled rectangle |
| `print_frame` | job, x, y, w, h, thickness, colour | rectangle outline |
| `print_line` | job, x0, y0, x1, y1, thickness, colour | line |
| `print_path_move` | job, x, y | starts a path |
| `print_path_line` | job, x, y | segment |
| `print_path_curve` | job, x1, y1, x2, y2, x3, y3 | cubic Bézier curve |
| `print_path_close` | job | closes the current outline |
| `print_path_fill` | job, colour | fills the path |
| `print_path_stroke` | job, thickness, colour | strokes the outline |
| `print_image_mode` | job, `PRINT_IMG_ALPHA` or 0 | for the following images: `0xAARRGGBB` pixels, or opaque `0x00RRGGBB` |
| `print_image` | job, pixels, width, height (in pixels), x, y, w, h | image scaled into the rectangle; reduced to 300 dots per inch and compressed by the kit |
| `print_image_jpeg` | job, JPEG file, bytes, `width \| height << 32`, x, y, w, h | a JPEG as is (Photos) |

## Text

| Function | Arguments | Result |
|---|---|---|
| `print_font` | job, family ("DejaVu Sans"...), `PRINT_BOLD` \| `PRINT_ITALIC` | font; -1: none |
| `print_text` | job, font, size, x, baseline, UTF-8 text, colour | advance (width written) |
| `print_text_width` | job, font, size, UTF-8 text | advance, without writing anything |
| `print_font_metrics` | job, font, size, `long m[3]` | ascent, descent, line gap |
| `print_font_data` | job, TrueType file, bytes | font; the bytes are kept until `print_end` |
| `print_glyph` | job, font \| style << 16, size, x, baseline, glyph, character, colour | a glyph placed by the application |

`print_text` hands the text to printd, which lays it out (without kerning). An application that lays out its own text (Letters) loads its fonts with `print_font_data` and places each glyph with `print_glyph`.

## Printers and the queue

What the **Printers** applet of Settings does, within reach of any application.

| Function | Arguments | Result |
|---|---|---|
| `print_printers` | `PrintPrinter[]`, slots | number of printers |
| `print_printer_media` | printer name, rank, name, size, label, size | 1: paper number rank (IPP name, label); 0: no more paper |
| `print_printer_add` | name, address, out, size | 1 (out: its name); 0 (out: why) |
| `print_printers_find` | `PrintFound[]`, slots | printers found on the local network (mDNS, a few seconds) |
| `print_printer_remove` | name | 1; 0: unknown (the PDF printer stays) |
| `print_printer_default` | name | 1; 0: unknown |
| `print_printer_status` | name, out, size | 1: ready; 0: not (out: what it says) |
| `print_jobs` | `PrintJobInfo[]`, slots | number of jobs: the queue, then the latest finished |
| `print_job_cancel` | number | 1; 0: unknown |
| `print_jobs_forget` | none | finished jobs leave the list |

`PrintPrinter`: `name`, `kind` ("pdf", "ipp"), `uri`, `model`, `color`, `copies`, `dpi`, `is_default`, `quality`, `media_default`. `PrintFound`: `address`, `name`, `model`, `usable`. `PrintJobInfo`: `id`, `state` (`PRINT_JOB_QUEUED`, `_PREPARING`, `_SENDING`, `_PRINTING`, `_DONE`, `_FAILED`, `_CANCELED`), `pages`, `page` (in progress), `printer`, `title`, `message`.

## Example: a PDF without a dialog

A Terminal program that prints one page on the PDF printer, into `/tmp/demo.pdf`, without showing a dialog, then follows its job in the queue.

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
		printf ("the print queue cannot be written\n");
		return 1;
	}
	long cm = 28346, left = 2 * cm, width = s.paper_w - 4 * cm;
	long f = print_font (j, (long) "DejaVu Sans", 0), fb = print_font (j, (long) "DejaVu Sans", PRINT_BOLD);
	if (print_page (j, 0, 0))
	{
		long m[3];
		print_font_metrics (j, fb, print_mpt (24), (long) m);
		long y = 3 * cm + m[0];				/* baseline below the margin */
		print_text (j, fb, print_mpt (24), left, y, (long) "Korin OS prints", 0x203040);
		print_line (j, left, y + m[1] + 6000, left + width, y + m[1] + 6000, 1500, 0x3060C0);

		const char *t = "Rectangles, lines, paths and text: all in thousandths of a point.";
		long tw = print_text_width (j, f, print_mpt (12), (long) t);
		print_text (j, f, print_mpt (12), left + (width - tw) / 2, y + 2 * cm, (long) t, 0x000000);

		print_rect (j, left, y + 3 * cm, 4 * cm, 2 * cm, 0xE8EEF8);
		print_frame (j, left, y + 3 * cm, 4 * cm, 2 * cm, 1000, 0x3060C0);

		long cx = left + 8 * cm, cy = y + 3 * cm;	/* a drop: two Bezier curves */
		print_path_move (j, cx, cy);
		print_path_curve (j, cx + 2 * cm, cy + cm, cx + cm, cy + 2 * cm, cx, cy + 2 * cm);
		print_path_curve (j, cx - cm, cy + 2 * cm, cx - 2 * cm, cy + cm, cx, cy);
		print_path_close (j);
		print_path_fill (j, 0x60A0E0);
		print_path_stroke (j, 800, 0x203040);

		static unsigned px[64 * 64];			/* a gradient, 0x00RRGGBB pixels */
		for (int i = 0; i < 64 * 64; i++)
			px[i] = (unsigned) (i % 64 * 4) << 16 | (unsigned) (i / 64 * 4) << 8 | 0xC0;
		print_image_mode (j, 0);
		print_image (j, (long) px, 64, 64, left + 12 * cm, y + 3 * cm, 2 * cm, 2 * cm);
	}
	long id = print_end (j);
	if (id < 0)
	{
		printf ("job refused (%ld)\n", id);
		return 1;
	}
	printf ("job %ld queued\n", id);

	PrintJobInfo jobs[16];
	for (int tries = 0; tries < 60; tries++)		/* one minute at most */
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
	printf ("still in progress\n");
	return 0;
}
```

## Example: an application's Print menu

A UIKit application that draws a chart and prints it through **File > Print...** (Ctrl+P). The dialog gives the printer, the paper, the margins: the drawing fits into what the printer can print.

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

// the same chart on paper, inside the printable area
static void print_chart ()
{
	if (!printerkit_kit_open ())
	{
		ui_message ((long) TR ("Print"), (long) TR ("The print service is not installed."), UM_OK);
		return;
	}
	PrintSetup s;
	print_setup_default ((long) &s);
	PrintDialogInfo di = { sizeof di, TR ("Chart"), 1, 0, 0, 0, 0 };
	if (!print_dialog ((long) &s, (long) &di))
		return;
	long j = print_begin ((long) &s, (long) TR ("Chart"));
	if (j && print_page (j, 0, 0))
	{
		long m = 36000;						// half an inch, or the printer's margin
		long l = s.margin_l > m ? s.margin_l : m, t = s.margin_t > m ? s.margin_t : m;
		long w = s.paper_w - l - (s.margin_r > m ? s.margin_r : m), h = w * 2 / 3;
		long f = print_font (j, (long) "DejaVu Sans", PRINT_BOLD);
		print_text (j, f, print_mpt (18), l, t + print_mpt (18), (long) TR ("Chart"), 0x000000);
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
		ui_message ((long) TR ("Print"), (long) TR ("The job could not be put in the print queue."), UM_OK);
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
	if (ui_window_ex ((scr[0] - 560) / 2, scr[3] + 40, 560, 400, (long) TR ("Chart"), WF_RESIZABLE) <= 0)
		return 1;
	ui_min_size (320, 240);
	ui_on_resize ((long) on_resize, 0);
	g_view = ui_view (0, 0, 10, 10, 0);
	ui_view_on (g_view, (long) view_paint, 0, 0, 0);
	ui_menu ((long) "MFile\n" "I1\tPrint...\tCtrl+P\n");
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

## Example: printers and the queue

Lists the known printers, their papers and their state, then the job queue.

```c
#include <stdio.h>
#include <printerkit.h>

static const char *STATES[] = { "queued", "preparing", "sending", "printing", "done", "failed", "canceled" };

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
		printf ("%s%s (%s, %s): %s%s%s\n", p[i].name, p[i].is_default ? " *" : "", p[i].kind,
			p[i].color ? "colour" : "black and white", ready ? "ready" : "not ready",
			st[0] ? " - " : "", st);
		char media[64], label[64];
		for (long k = 0; k < 4 && print_printer_media ((long) p[i].name, k, (long) media, sizeof media,
			(long) label, sizeof label); k++)
			printf ("    %s (%s)\n", label, media);
	}

	PrintJobInfo jobs[16];
	long nj = print_jobs ((long) jobs, 16);
	printf ("%ld job(s)\n", nj);
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

## Printing through PDF export code: `pdfprint.h`

An application that can already "Export as PDF" with `pdfwrite.h` (the PDF writer of Letters, class `pdfw::Writer`) prints with the same drawing code. `pdfprint.h` provides `PrintWriter`, a `pdfw::Writer` whose pages go into a print job, and `print_ask`, which opens the kit, shows the dialog and starts the job. Pages outside those chosen are dropped without the application having to know; links and bookmarks are ignored. The header is included **after** `pdfwrite.h`, which comes from Letters (`src/apps/letters/pdfwrite.h`):

```cpp
#include "pdfwrite.h"
#include <pdfprint.h>

void print_document ()
{
	long job = print_ask ("Report", page_count (), current_page (), 595.28f, 841.89f);	// points
	if (!job)
		return;					// cancelled, or the queue cannot be written
	{
		PrintWriter w (job);
		export_pages (w);			// the PDF export code, unchanged
	}
	print_end (job);
}
```

`print_ask (title, pages, current page, width, height)`: the page size in points (`PRINT_DLG_OWN_PAPER`: the document has its own size); returns the job, or 0.

## Pitfalls

- **One job at a time** per program: finish with `print_end` or `print_abort` before starting another.
- **`print_page` returning 0** means "page not asked for", not an error: draw nothing, go to the next one.
- **The margins** of the `PrintSetup` are what the printer cannot print: a drawing must stay inside them (the PDF printer has none).
- **The dialog** is a window: a program without a window fills in the `PrintSetup` itself (`printer`, `output`), as in the first example.
- **No kerning** in `print_text`, nor in its measurement; for fine layout, `print_font_data` and `print_glyph`.
- **Plain IPP only** (port 631): a printer that requires IPPS is refused, and the message says so.

## See also

- [UIKit](uikit_en.md): the window, menus, the event loop.
- [ArchiveKit](archivekit_en.md) and [DocumentKit](documentkit_en.md): the documents to print.
- [The app skeleton](skeleton_en.md).
