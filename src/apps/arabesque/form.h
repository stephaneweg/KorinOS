//
// form.h -- la forme du morceau : une suite de sections (« A B A* C A** »), chaque lettre avec sa longueur en mesures,
// sa region de l'image et sa tonalite. Une lettre est composee la premiere fois qu'elle vient, dans sa region : le chemin
// y parcourt une image calculee a ce cadrage (une fractale y montre d'autres details), ou la partie de l'image ouverte,
// agrandie. Reprise telle quelle, elle est recopiee note pour note. Port de Form.cs du port Windows, memes calculs dans
// le meme ordre : memes regions, memes images, memes notes.
//
// Une etoile en fait une variation, et chaque nombre d'etoiles une variation differente (A* et A** ne se ressemblent pas
// plus qu'a A) : le chemin et l'image de la lettre sont les memes, donc le rythme, les nuances et les accords aussi ; les
// hauteurs des parties melodiques sont lues dans une region un peu decalee, une par variation. La basse (Harmonie
// « basse »), les accords (« l'accord seul ») et les percussions gardent leurs notes.
//
// Une lettre peut avoir son propre style (instruments, rythmes, liaisons, parties allumees) : il prend effet au debut de
// chacune de ses sections, variations et reprises comprises ; sans style a part, c'est celui du morceau.
//
// Les images se preparent avant de composer (prepare) : le compositeur n'en calcule aucune pendant qu'il joue. Une region
// se donne dans l'image de depart (centre de 0 a 1, grossissement), si bien que les notes d'une section s'allument la ou
// elle est dans cette image.
//
#ifndef ARABESQUE_FORM_H
#define ARABESQUE_FORM_H

// (image.h, inclus plus loin)
static void image_make (Image &im, const char *kind, int w, int h, uint32_t seed, const ImgSet *set);
static const double IMG_MIN_ZOOM = 0.25, IMG_MAX_ZOOM = 1e12;

struct FormLetter
{
	int bars;
	double u, v, zoom;			// la region : son centre dans l'image (0..1), son grossissement (1 : toute l'image)
	int key;				// 0 la tonalite du morceau, 1 la dominante, 2 la sous-dominante, 3 le relatif
	bool autoR;				// la region se choisit d'apres l'image, tant qu'on ne l'a pas reglee
	Style *style;				// le style a part de la lettre (ses variations comprises) ; 0 : celui du morceau
	FormLetter () : bars (8), u (0.5), v (0.5), zoom (1), key (0), autoR (true), style (0) {}
};

// les nombres au plus court qui se relisent a l'identique (« R » en C#)
static void num_r (double x, char *o, int cap)
{
	for (int p = 1; p <= 17; p++) { snprintf (o, (size_t) cap, "%.*g", p, x); if (strtod (o, 0) == x && !strchr (o, 'e')) return; }
}

struct MusicForm
{
	enum { MAX_SECTIONS = 64, MAX_BARS = 64 };
	static constexpr double AUTO_ZOOM = 3, VAR_SHIFT = 0.07;
	int n; int sl[MAX_SECTIONS], sv[MAX_SECTIONS];	// la suite : la lettre et la variation de chaque section
	FormLetter def[26];
	// les images preparees, par lettre et variation (own : calculee pour elle, sinon l'image de depart)
	struct Prep { int l, v; Image *im; bool own; };
	Prep prep[MAX_SECTIONS]; int nprep;

	MusicForm () : n (0), nprep (0) {}
	~MusicForm () { drop (); for (int l = 0; l < 26; l++) delete def[l].style; }
	MusicForm (const MusicForm &) = delete;
	MusicForm &operator= (const MusicForm &) = delete;
	// une copie : la suite et les lettres (styles compris) ; pas les images, qui se refont
	MusicForm *clone () const
	{
		MusicForm *f = new MusicForm ();
		f->n = n;
		memcpy (f->sl, sl, sizeof sl); memcpy (f->sv, sv, sizeof sv);
		for (int l = 0; l < 26; l++) { f->def[l] = def[l]; f->def[l].style = def[l].style ? new Style (*def[l].style) : 0; }
		return f;
	}
	void drop ()
	{
		for (int i = 0; i < nprep; i++) if (prep[i].own) { image_free (*prep[i].im); delete prep[i].im; }
		nprep = 0;
	}

	// ---- la suite ----
	// « A B A* C A** », espaces facultatifs, minuscules permises ; false : rien de lisible (la suite ne change pas)
	bool set_text (const char *s)
	{
		int ol[MAX_SECTIONS], ov[MAX_SECTIONS], k = 0;
		for (int i = 0; s[i]; )
		{
			char c = s[i] >= 'a' && s[i] <= 'z' ? (char) (s[i] - 32) : s[i];
			if (c == ' ' || c == ',' || c == '-') { i++; continue; }
			if (c < 'A' || c > 'Z') return false;
			i++;
			int v = 0;
			while (s[i] == '*' || s[i] == '\'') { v++; i++; }
			if (k == MAX_SECTIONS) return false;
			ol[k] = c - 'A'; ov[k] = v < 9 ? v : 9; k++;
		}
		if (!k) return false;
		n = k;
		memcpy (sl, ol, sizeof (int) * (size_t) k); memcpy (sv, ov, sizeof (int) * (size_t) k);
		return true;
	}
	int text (char *o, int cap) const
	{
		int w = 0; o[0] = 0;
		for (int i = 0; i < n && w < cap - 12; i++)
		{
			if (i) o[w++] = ' ';
			o[w++] = (char) ('A' + sl[i]);
			for (int k = 0; k < sv[i]; k++) o[w++] = '*';
		}
		o[w] = 0;
		return w;
	}
	// la premiere section de cette lettre et de cette variation
	int first (int k) const { for (int i = 0; i < k; i++) if (sl[i] == sl[k] && sv[i] == sv[k]) return i; return k; }
	bool uses (int l) const { for (int i = 0; i < n; i++) if (sl[i] == l) return true; return false; }

	// ---- le temps, en pulsations ----
	uint32_t start (int k, uint32_t bar) const
	{
		uint32_t t = 0;
		for (int i = 0; i < k && i < n; i++) t += (uint32_t) def[sl[i]].bars * bar;
		return t;
	}
	uint32_t length (uint32_t bar) const { return start (n, bar); }
	int section_at (uint32_t tick, uint32_t bar) const
	{
		uint32_t t = 0;
		for (int i = 0; i < n; i++) { t += (uint32_t) def[sl[i]].bars * bar; if (tick < t) return i; }
		return -1;
	}

	// ---- la tonalite d'une lettre : la gamme (tonique au bit 0) et la tonique ----
	static void transpose (int key, unsigned &mask, int &root)
	{
		int off = key == 1 ? 7 : key == 2 ? 5
			// le relatif : la gamme vue depuis son sixieme degre, ou depuis son troisieme si elle est mineure (memes notes)
			: key == 3 ? ((mask >> 3 & 1) && !(mask >> 4 & 1) ? degree_offset (3, mask) : degree_offset (6, mask)) : 0;
		if (!off) return;
		if (key == 3) mask = ((mask >> off) | (mask << (12 - off))) & 0xFFF;
		root = (root + off) % 12;
	}

	// ---- les regions ----
	// le centre permis : la region reste dans l'image
	static double cl (double x, double a, double b) { return x < a ? a : x > b ? b : x; }
	static int cli (int x, int a, int b) { return x < a ? a : x > b ? b : x; }
	static void clamp (FormLetter &d)
	{
		d.zoom = cl (d.zoom, 1, 64);
		double h = 0.5 / d.zoom;
		d.u = cl (d.u, h, 1 - h); d.v = cl (d.v, h, 1 - h);
	}
	// les lettres reglees d'elles-memes : A prend toute l'image, les autres une region ou les couleurs varient beaucoup,
	// chacune ailleurs que les precedentes
	void auto_regions (const Image &p)
	{
		const FormLetter *taken[26]; int nt = 0;
		for (int i = 0; i < n; i++)
		{
			FormLetter &d = def[sl[i]];
			bool seen = false;
			for (int k = 0; k < nt; k++) if (taken[k] == &d) seen = true;
			if (seen) continue;
			if (d.autoR) pick (p, sl[i], d, taken, nt, 0);
			taken[nt++] = &d;
		}
	}
	struct Cand { double score, u, v; };
	static int cand_cmp (const void *a, const void *b)
	{
		const Cand &x = *(const Cand *) a, &y = *(const Cand *) b;
		if (x.score != y.score) return x.score > y.score ? -1 : 1;
		if (x.v != y.v) return x.v < y.v ? -1 : 1;
		return x.u < y.u ? -1 : x.u > y.u ? 1 : 0;
	}
	void pick (const Image &p, int l, FormLetter &d, const FormLetter *const *others, int nothers, int skip) const
	{
		if (l == 0 && skip == 0) { d.u = d.v = 0.5; d.zoom = 1; return; }
		double z = AUTO_ZOOM, h = 0.5 / z;
		enum { G = 9 };
		Cand cands[G * G]; int nc = 0;
		for (int gy = 0; gy < G; gy++)
			for (int gx = 0; gx < G; gx++)
			{
				double u = h + (1 - 2 * h) * gx / (G - 1), v = h + (1 - 2 * h) * gy / (G - 1);
				bool near = false;
				for (int k = 0; k < nothers; k++)
				{
					const FormLetter &o = *others[k];
					if (o.zoom > 1.01 && fabs (o.u - u) < 0.5 / o.zoom + h * 0.6 && fabs (o.v - v) < 0.5 / o.zoom + h * 0.6) near = true;
				}
				cands[nc++] = { (double) (variety (p, u, v, z) - (near ? 1000 : 0)), u, v };
			}
		qsort (cands, (size_t) nc, sizeof (Cand), cand_cmp);
		const Cand &c = cands[skip < nc - 1 ? skip : nc - 1];
		d.u = c.u; d.v = c.v; d.zoom = z;
	}
	// combien de couleurs differentes dans la region (16 niveaux par composante)
	static int variety (const Image &p, double u, double v, double z)
	{
		static unsigned char seen[4096];
		memset (seen, 0, sizeof seen);
		const int N = 24;
		int cnt = 0;
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++)
			{
				double uu = u + ((x + 0.5) / N - 0.5) / z, vv = v + ((y + 0.5) / N - 0.5) / z;
				int px = cli ((int) (uu * (p.w - 1)), 0, p.w - 1), py = cli ((int) (vv * (p.h - 1)), 0, p.h - 1);
				uint32_t q = (p.px[py * p.w + px] >> 4) & 0x0F0F0F;
				int i = (int) ((q >> 16 & 15) << 8 | (q >> 8 & 15) << 4 | (q & 15));
				if (!seen[i]) { seen[i] = 1; cnt++; }
			}
		return cnt;
	}
	// la region de la variation v : decalee d'un petit pas, assez pour que les couleurs lues changent (dans l'image de
	// depart, plus d'une sur deux) mais pas plus qu'il ne faut ; les pas essayes partent d'une direction propre a v
	// (l'angle d'or), et une variation s'ecarte aussi des precedentes de la meme lettre
	static void var_region (const Image &p, const FormLetter &d, int v, const double *bu0, const double *bw0, int nb, double &u, double &w)
	{
		double bu = 0, bw = 0, best = -1;
		for (int k = 0; k < 32; k++)
		{
			double a = v * 2.399963229728653 + k * 0.7, r = (VAR_SHIFT + 0.025 * (k / 8)) / d.zoom;
			double cu = d.u + r * cos (a), cw = d.v + r * sin (a);
			double s = differ (p, d.u, d.v, cu, cw, d.zoom);
			for (int i = 0; i < nb; i++) { double t = differ (p, bu0[i], bw0[i], cu, cw, d.zoom); if (t < s) s = t; }
			if (s > best) { best = s; bu = cu; bw = cw; }
			if (s >= 0.55) break;
		}
		u = bu; w = bw;
	}
	// la part des points de deux regions de meme taille dont la couleur differe nettement
	static double differ (const Image &p, double u1, double v1, double u2, double v2, double z)
	{
		const int N = 24;
		int n = 0;
		auto at = [&] (double u, double v) { return p.px[cli ((int) (v * (p.h - 1)), 0, p.h - 1) * p.w + cli ((int) (u * (p.w - 1)), 0, p.w - 1)]; };
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++)
			{
				double du = ((x + 0.5) / N - 0.5) / z, dv = ((y + 0.5) / N - 0.5) / z;
				uint32_t a = at (u1 + du, v1 + dv), b = at (u2 + du, v2 + dv);
				int dd = abs ((int) (a >> 16 & 255) - (int) (b >> 16 & 255)) + abs ((int) (a >> 8 & 255) - (int) (b >> 8 & 255)) + abs ((int) (a & 255) - (int) (b & 255));
				if (dd > 48) n++;
			}
		return (double) n / (N * N);
	}

	// ---- les images ----
	const Image *img (int l, int v) const { for (int i = 0; i < nprep; i++) if (prep[i].l == l && prep[i].v == v) return prep[i].im; return 0; }
	// les images de chaque lettre et de chaque variation
	void prepare (const Image &p)
	{
		drop ();
		// (la region de A** s'ecarte de celle de A*, qu'A* soit dans la forme ou non : elle ne depend que de l'image)
		double vu[26][10], vw[26][10]; int nv[26] = { 0 };
		for (int i = 0; i < n; i++)
		{
			int l = sl[i], v = sv[i];
			if (img (l, v)) continue;
			const FormLetter &d = def[l];
			double u = d.u, w = d.v;
			if (v > 0)
			{
				while (nv[l] < v) { var_region (p, d, nv[l] + 1, vu[l], vw[l], nv[l], vu[l][nv[l]], vw[l][nv[l]]); nv[l]++; }
				u = vu[l][v - 1]; w = vw[l][v - 1];
			}
			Prep &q = prep[nprep++];
			q.l = l; q.v = v;
			q.im = region (p, u, w, d.zoom, q.own);
		}
	}
	// l'image de la region : recalculee a ce cadrage si elle se recadre, sinon la partie de l'image, agrandie
	static Image *region (const Image &p, double u, double w, double z, bool &own)
	{
		own = false;
		if (z == 1 && u == 0.5 && w == 0.5) return (Image *) &p;
		own = true;
		Image *r = new Image ();
		if (p.canView && !p.data)
		{
			double x, y;
			p.view_at (u, w, x, y);
			ImgSet s = p.set;
			double z0, uz;
			z0 = p.used.get ("zoom", uz) ? uz : 1;
			s.put ("x", x); s.put ("y", y); s.put ("zoom", cl (z0 * z, IMG_MIN_ZOOM, IMG_MAX_ZOOM));
			image_make (*r, p.kind, p.w, p.h, p.seed, &s);
		}
		else
		{
			r->px = (uint32_t *) malloc (sizeof (uint32_t) * (size_t) p.w * (size_t) p.h);
			r->w = p.w; r->h = p.h;
			for (int y = 0; y < p.h; y++)
			{
				int sy = cli ((int) ((w + ((double) y / p.h - 0.5) / z) * p.h), 0, p.h - 1);
				for (int x = 0; x < p.w; x++)
				{
					int sx = cli ((int) ((u + ((double) x / p.w - 0.5) / z) * p.w), 0, p.w - 1);
					r->px[y * p.w + x] = p.px[sy * p.w + sx];
				}
			}
		}
		return r;
	}

	// ---- le prereglage : « form=A B A* » puis une ligne par lettre, « section=A mesures u v zoom tonalite » ----
	template <class Out> void format (Out out) const
	{
		char t[MAX_SECTIONS * 12], b[200], a[3][40];
		text (t, sizeof t);
		snprintf (b, sizeof b, "form=%s\r\n", t); out (b);
		for (int l = 0; l < 26; l++)
		{
			if (!uses (l)) continue;
			const FormLetter &d = def[l];
			num_r (d.u, a[0], 40); num_r (d.v, a[1], 40); num_r (d.zoom, a[2], 40);
			snprintf (b, sizeof b, "section=%c %d %s %s %s %d\r\n", 'A' + l, d.bars, a[0], a[1], a[2], d.key);
			out (b);
		}
	}
	void parse_section (const char *s)
	{
		char w[6][40]; int k = 0;
		while (*s && k < 6)
		{
			while (*s == ' ') s++;
			if (!*s) break;
			int i = 0;
			while (*s && *s != ' ') { if (i < 39) w[k][i++] = *s; s++; }
			w[k][i] = 0; k++;
		}
		if (k < 6 || strlen (w[0]) != 1) return;
		char c = w[0][0] >= 'a' && w[0][0] <= 'z' ? (char) (w[0][0] - 32) : w[0][0];
		if (c < 'A' || c > 'Z') return;
		FormLetter &d = def[c - 'A'];
		auto P = [] (const char *x) { char *e; double v = strtod (x, &e); return *x && !*e && isfinite (v) ? v : 0.0; };
		d.bars = cli ((int) P (w[1]), 1, MAX_BARS);
		d.u = P (w[2]); d.v = P (w[3]); d.zoom = P (w[4]);
		d.key = cli ((int) P (w[5]), 0, 3);
		d.autoR = false;
		clamp (d);
	}
};

#endif
