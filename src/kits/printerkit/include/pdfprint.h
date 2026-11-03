// pdfprint.h -- imprimer par le code d'export PDF d'une application (printerkit/pdfprint.h d'Onyx) : PrintWriter est un
// pdfw::Writer (pdfwrite.h de Letters) dont les pages vont dans un travail d'impression au lieu d'un PDF. Une application
// qui a « Exporter en PDF » imprime avec le meme code de dessin :
//
//   PrintJob j = print_ask (nom, pages, courante, l, h);
//   if (j) { { PrintWriter w (j); export_pages (w); } print_end (j); }
//
// Les pages hors de celles choisies sont ecartees, l'application n'a rien a savoir. Liens et signets n'ont pas de sens sur
// le papier : ignores. A inclure apres pdfwrite.h.
#ifndef PDFPRINT_H
#define PDFPRINT_H

#include <printerkit.h>

class PrintWriter : public pdfw::Writer
{
public:
	long job;
	bool wanted;				// la page commencee est a imprimer (une page couteuse peut etre sautee)
	PrintWriter (long j) : job (j), wanted (false) { print_image_mode (job, PRINT_IMG_ALPHA); }

	int  add_font (const unsigned char *ttf, unsigned len) override { return (int) print_font_data (job, (long) ttf, len); }
	void begin_page (float w, float h) override { wanted = print_page (job, print_mpt (w), print_mpt (h)) != 0; }
	void fill_rect (float x, float y, float w, float h, unsigned rgb) override { print_rect (job, print_mpt (x), print_mpt (y), print_mpt (w), print_mpt (h), rgb); }
	void glyph (int f, float size, float x, float y, unsigned gid, unsigned unicode, unsigned rgb, bool bold = false, bool italic = false) override
	{
		print_glyph (job, f | (bold ? PRINT_BOLD : 0) << 16 | (italic ? PRINT_ITALIC : 0) << 16, print_mpt (size), print_mpt (x), print_mpt (y), gid, unicode, rgb);
	}
	void image (const unsigned *px, int pw, int ph, float x, float y, float w, float h, bool jpeg = false, int quality = 85) override
	{
		(void) jpeg; (void) quality;
		print_image (job, (long) px, pw, ph, print_mpt (x), print_mpt (y), print_mpt (w), print_mpt (h));
	}
	void image_jpeg (const unsigned char *jpg, unsigned len, int pw, int ph, float x, float y, float w, float h) override
	{
		print_image_jpeg (job, (long) jpg, len, (long) pw | (long) ph << 32, print_mpt (x), print_mpt (y), print_mpt (w), print_mpt (h));
	}
	void end_page () override {}
	bool page_wanted () const override { return wanted; }
};

// La suite habituelle : le dialogue pour un document qui a sa taille de page, puis un travail -> 0 : abandon (ou la file ne
// peut etre ecrite). L'appelant dessine par PrintWriter, puis print_end (job).
static inline long print_ask (const char *title, int pages, int current, float pageW, float pageH)
{
	if (!printerkit_kit_open ()) return 0;
	PrintSetup s; print_setup_default ((long) &s);
	PrintDialogInfo di = { sizeof (PrintDialogInfo), title, pages, current, PRINT_DLG_OWN_PAPER, print_mpt (pageW) > 0 ? (int) print_mpt (pageW) : 0, (int) print_mpt (pageH) };
	if (!print_dialog ((long) &s, (long) &di)) return 0;
	return print_begin ((long) &s, (long) title);
}

#endif
