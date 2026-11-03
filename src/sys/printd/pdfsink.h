//
// pdfsink.h (printerkit/pdfsink.h d'Onyx) -- les pages d'un travail en PDF (job.h rejoue dans pdfwrite.h de Letters) :
// ce que fait l'imprimante PDF. Le texte reste du texte (polices embarquees en sous-ensembles), les formes des formes.
//
#ifndef PRINTD_PDFSINK_H
#define PRINTD_PDFSINK_H

#include "job.h"
#include "pdfwrite.h"

namespace pjob {

class PdfSink : public Sink
{
public:
	pdfw::Writer w;
	PdfSink () { for (int i = 0; i < 64; i++) m_font[i] = -1; }

	void font (int id, const unsigned char *ttf, unsigned len) { if (id >= 0 && id < 64) m_font[id] = w.add_font (ttf, len); }
	void begin_page (float pw, float ph) { w.begin_page (pw, ph); }
	void rect (float x, float y, float rw, float rh, unsigned rgb) { w.fill_rect (x, y, rw, rh, rgb); }
	void glyphs (const Ttf *, int font, unsigned style, float size, unsigned rgb, const Glyph *g, int n)
	{
		int f = font >= 0 && font < 64 ? m_font[font] : -1;
		for (int i = 0; i < n; i++) w.glyph (f, size, g[i].x, g[i].y, g[i].gid, g[i].uni, rgb, (style & ST_BOLD) != 0, (style & ST_ITALIC) != 0);
	}
	void image (float x, float y, float iw, float ih, int pw, int ph, int kind, const unsigned char *data, unsigned len)
	{
		if (kind == IMG_JPEG) { w.image_jpeg (data, len, pw, ph, x, y, iw, ih); return; }
		if (len >= (unsigned) pw * ph * 4) w.image ((const unsigned *) data, pw, ph, x, y, iw, ih);
	}
	void path (const PathPt *pt, int n, unsigned rgb, float strokeW)
	{
		float *xy = new float[n * 2]; unsigned char *v = new unsigned char[n];
		for (int i = 0; i < n; i++) { xy[i * 2] = pt[i].x; xy[i * 2 + 1] = pt[i].y; v[i] = (unsigned char) pt[i].verb; }
		// (job.h marque chacun des trois points d'une courbe ; pdfwrite veut la marque sur le premier)
		w.path (xy, v, n, rgb, strokeW);
		delete[] xy; delete[] v;
	}
	void end_page () { w.end_page (); }

private:
	int m_font[64];
};

} // namespace pjob

#endif
