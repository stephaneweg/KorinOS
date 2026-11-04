//
// recipe.h -- le prereglage d'Arabesque (.arb) : tout ce qu'il faut pour recomposer un morceau a l'identique. La graine,
// la gamme, le trace (au bit pres, cadre compris), le style et l'image de depart ; puis le journal de ce qui a change
// pendant la composition, chaque changement date de la pulsation ou en etait le compositeur ; enfin la longueur composee
// et les retouches d'orchestre faites apres coup. Une image ouverte est gardee dans le fichier, octets d'origine compris :
// le prereglage ne depend d'aucun autre fichier. Le format est celui de Recipe.cs du port Windows : un prereglage passe
// de l'un a l'autre.
//
// Le compositeur avance par blocs, mais son resultat ne depend pas du decoupage : rejouer le journal (avancer jusqu'a la
// date, appliquer, recommencer) redonne les memes notes. Le rejeu est dans replay.h.
//
#ifndef ARABESQUE_RECIPE_H
#define ARABESQUE_RECIPE_H

#include <math.h>
#include <stdarg.h>
#include "chords.h"

// les reglages d'une image calculee (image.h) : « fade=-0.5 rays=40 », tries par cle. Ceux qui manquent, la graine les
// choisit.
enum { IMGSET_MAX = 8 };
struct ImgSet
{
	int n; char key[IMGSET_MAX][12]; double v[IMGSET_MAX];		// (sans constructeur : n = 0 a la main)
	int find (const char *k) const { for (int i = 0; i < n; i++) if (!strcmp (key[i], k)) return i; return -1; }
	bool get (const char *k, double &x) const { int i = find (k); if (i < 0) return false; x = v[i]; return true; }
	void put (const char *k, double x)
	{
		int i = find (k);
		if (i < 0)
		{
			if (n >= IMGSET_MAX || strlen (k) >= sizeof key[0]) return;
			for (i = n; i > 0 && strcmp (key[i - 1], k) > 0; i--) { memcpy (key[i], key[i - 1], sizeof key[0]); v[i] = v[i - 1]; }
			snprintf (key[i], sizeof key[i], "%s", k);
			n++;
		}
		v[i] = x;
	}
	void remove (const char *k)
	{
		int i = find (k);
		if (i < 0) return;
		for (; i + 1 < n; i++) { memcpy (key[i], key[i + 1], sizeof key[0]); v[i] = v[i + 1]; }
		n--;
	}
	// les nombres au plus court qui se relisent a l'identique (comme « R » en C#)
	int format (char *o, int cap) const
	{
		int w = 0;
		o[0] = 0;
		for (int i = 0; i < n && w < cap; i++)
		{
			char num[32];
			for (int p = 1; p <= 17; p++) { snprintf (num, sizeof num, "%.*g", p, v[i]); if (strtod (num, 0) == v[i] && !strchr (num, 'e')) break; }
			w += snprintf (o + w, (size_t) (cap - w), "%s%s=%s", i ? " " : "", key[i], num);
		}
		return w < cap ? w : cap - 1;
	}
	void parse (const char *s)
	{
		n = 0;
		while (*s)
		{
			while (*s == ' ') s++;
			const char *e = s;
			while (*e && *e != ' ') e++;
			const char *eq = (const char *) memchr (s, '=', (size_t) (e - s));
			if (eq && eq > s && eq - s < (long) sizeof key[0])
			{
				char k[12], num[40]; char *end;
				memcpy (k, s, (size_t) (eq - s)); k[eq - s] = 0;
				int l = (int) (e - eq - 1) < 39 ? (int) (e - eq - 1) : 39;
				memcpy (num, eq + 1, (size_t) l); num[l] = 0;
				double x = strtod (num, &end);
				if (l && !*end && isfinite (x)) put (k, x);
			}
			s = e;
		}
	}
};

// une image, et d'ou elle vient : calculee (sa sorte, sa graine et ses reglages, image.h) ou un fichier (ses octets,
// pour le prereglage)
struct Image
{
	uint32_t *px; int w, h;
	char kind[16]; uint32_t seed;
	ImgSet set;				// les reglages donnes a la main
	ImgSet used;				// les valeurs prises au calcul, reglees ou tirees (pour les montrer)
	char file[64]; unsigned char *data; long size;
	uint32_t serial;			// change avec l'image : le journal la reconnait a lui
	// le cadrage, pour la forme et la souris : le point de l'image (u, v de 0 a 1) a pour coordonnees
	// vo + va (u - 0.5) + vb (v - 0.5) dans la sorte ; canView : une image calculee qui se recadre
	bool canView;
	double vox, voy, vax, vay, vbx, vby;
	Image () : px (0), w (0), h (0), seed (0), data (0), size (0), serial (0), canView (false), vox (0), voy (0), vax (0), vay (0), vbx (0), vby (0) { kind[0] = file[0] = 0; set.n = used.n = 0; }
	void view_at (double u, double v, double &x, double &y) const { x = vox + vax * (u - 0.5) + vbx * (v - 0.5); y = voy + vay * (u - 0.5) + vby * (v - 0.5); }
};
static uint32_t g_imageSerial;
static void image_source_gen (Image &im, const char *kind, uint32_t seed, const ImgSet *set)
{
	snprintf (im.kind, sizeof im.kind, "%s", kind); im.seed = seed;
	im.set.n = 0; if (set) im.set = *set;
	free (im.data); im.data = 0; im.size = 0; im.file[0] = 0;
	im.serial = ++g_imageSerial;
}
static void image_source_file (Image &im, const char *name, const void *data, long size)
{
	im.kind[0] = 0; im.seed = 0; im.set.n = im.used.n = 0; im.canView = false;
	snprintf (im.file, sizeof im.file, "%s", name);
	free (im.data);
	im.data = (unsigned char *) malloc ((size_t) size);
	memcpy (im.data, data, (size_t) size); im.size = size;
	im.serial = ++g_imageSerial;
}
static void image_free (Image &im) { free (im.px); free (im.data); im.px = 0; im.data = 0; im.w = im.h = 0; im.size = 0; }

#include "form.h"

// ---- base64 ----
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static long b64_encode (const unsigned char *s, long n, char *o)
{
	long w = 0;
	for (long i = 0; i < n; i += 3)
	{
		uint32_t v = (uint32_t) s[i] << 16 | (i + 1 < n ? (uint32_t) s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
		o[w++] = B64[v >> 18 & 63]; o[w++] = B64[v >> 12 & 63];
		o[w++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
		o[w++] = i + 2 < n ? B64[v & 63] : '=';
	}
	return w;
}
static long b64_decode (const char *s, long n, unsigned char *o)
{
	long w = 0; uint32_t v = 0; int bits = 0;
	for (long i = 0; i < n; i++)
	{
		const char *p = s[i] ? strchr (B64, s[i]) : 0;
		if (!p) continue;				// '=', fins de ligne
		v = v << 6 | (uint32_t) (p - B64); bits += 6;
		if (bits >= 8) { bits -= 8; o[w++] = (unsigned char) (v >> bits); }
	}
	return w;
}

// ---- le journal ----
enum { CH_STATE, CH_SCALE, CH_STYLE, CH_CLEAR };
struct Change
{
	int kind;
	uint32_t at;				// ou en etait le compositeur
	uint32_t now;				// CH_SCALE, CH_STYLE : la pulsation de la demande (la barriere en decoule)
	Style *style;				// CH_STATE, CH_STYLE
	Ifs *ifs;				// CH_STATE
	int image;				// CH_STATE : rang dans Recipe::img
	ChordGrid *chords;			// CH_STATE : la grille d'accords, 0 si elle est vide
	unsigned mask; int root;		// CH_SCALE
	int part;				// CH_CLEAR
};
// d'ou vient une image du journal (les pixels se refont au rejeu)
struct ImgRef { uint32_t serial; char kind[16]; uint32_t seed; ImgSet set; int w, h; char file[64]; unsigned char *data; long size; };

struct Recipe
{
	uint32_t seed; int tempo; char copyright[120];
	uint32_t length;			// pulsations composees ; 0 : les reglages seuls
	unsigned mask; int root; char scaleName[32];
	ImgRef *img; int nimg, capImg;
	Change *ch; int nch, capCh;		// ch[0] : l'etat de depart
	// apres coup : l'instrument, le volume et le panoramique de depart de chaque partie, ou -1 ; une partie videe
	int mix[NPART][3]; bool emptied[NPART];
	MusicForm *form;			// 0 : pas de forme (une seule coulee, comme avant)

	Recipe () : seed (1), tempo (120), length (0), mask (0xAB5), root (0), img (0), nimg (0), capImg (0), ch (0), nch (0), capCh (0), form (0)
	{
		copyright[0] = scaleName[0] = 0;
		for (int i = 0; i < NPART; i++) { mix[i][0] = mix[i][1] = mix[i][2] = -1; emptied[i] = false; }
	}
	~Recipe () { clear (); free (img); free (ch); delete form; }
	void set_form (const MusicForm *f) { delete form; form = f && f->n ? f->clone () : 0; }
	Recipe (const Recipe &) = delete;
	Recipe &operator= (const Recipe &) = delete;

	void clear ()
	{
		for (int i = 0; i < nch; i++) { delete ch[i].style; delete ch[i].ifs; delete ch[i].chords; }
		for (int i = 0; i < nimg; i++) free (img[i].data);
		nch = nimg = 0;
	}
	Change &add (int kind, uint32_t at)
	{
		if (nch == capCh) { capCh = capCh ? capCh * 2 : 16; ch = (Change *) realloc (ch, sizeof (Change) * (size_t) capCh); }
		Change &c = ch[nch++];
		memset (&c, 0, sizeof c);
		c.kind = kind; c.at = at;
		return c;
	}
	ImgRef &add_image ()
	{
		if (nimg == capImg) { capImg = capImg ? capImg * 2 : 8; img = (ImgRef *) realloc (img, sizeof (ImgRef) * (size_t) capImg); }
		ImgRef &r = img[nimg++];
		memset (&r, 0, sizeof r);
		return r;
	}
	// le rang d'une image, ajoutee au premier besoin
	int image_index (const Image &im)
	{
		for (int i = 0; i < nimg; i++) if (img[i].serial == im.serial) return i;
		ImgRef &r = add_image ();
		r.serial = im.serial; r.seed = im.seed; r.set = im.set; r.w = im.w; r.h = im.h;
		snprintf (r.kind, sizeof r.kind, "%s", im.kind);
		snprintf (r.file, sizeof r.file, "%s", im.file);
		if (im.data) { r.data = (unsigned char *) malloc ((size_t) im.size); memcpy (r.data, im.data, (size_t) im.size); r.size = im.size; }
		return nimg - 1;
	}
	void state (uint32_t at, const Style &st, const Ifs &f, const Image &im, const ChordGrid *g)
	{
		Change &c = add (CH_STATE, at);
		c.style = new Style (st); c.ifs = new Ifs (f);
		c.image = image_index (im);
		c.chords = g && g->length > 0 ? new ChordGrid (*g) : 0;
	}
	// les reglages de la fenetre, sans rien de compose
	void from_settings (uint32_t sd, int tp, const char *copy, unsigned m, int rt, const char *scale, const Style &st, const Ifs &f, const Image &im, const ChordGrid *g, const MusicForm *fm = 0)
	{
		clear ();
		set_form (fm);
		seed = sd; tempo = tp; mask = m; root = rt; length = 0;
		snprintf (copyright, sizeof copyright, "%s", copy);
		snprintf (scaleName, sizeof scaleName, "%s", scale);
		for (int i = 0; i < NPART; i++) { mix[i][0] = mix[i][1] = mix[i][2] = -1; emptied[i] = false; }
		state (0, st, f, im, g);
	}
	// ce que la sequence a de different de ce que la composition a ecrit : les retouches faites apres coup
	void take_mix (const Song &s)
	{
		for (int i = 0; i < NPART; i++)
		{
			const Track &t = s.tr[i];
			emptied[i] = t.count == 0;
			mix[i][0] = mix[i][1] = mix[i][2] = -1;
			for (int k = 0; k < t.count && t.n[k].tick == 0; k++)
			{
				const Note &m = t.n[k];
				if (m.kind == NK_PROGRAM) mix[i][0] = m.key;
				else if (m.kind == NK_CC && m.key == 7) mix[i][1] = m.vel;
				else if (m.kind == NK_CC && m.key == 10) mix[i][2] = m.vel;
			}
		}
	}

	// ---- le fichier ----
	struct Buf
	{
		char *p; long n, cap;
		Buf () : p (0), n (0), cap (0) {}
		~Buf () { free (p); }
		void need (long k) { if (n + k + 1 > cap) { cap = (n + k + 1) * 2; p = (char *) realloc (p, (size_t) cap); } }
		void f (const char *fmt, ...) __attribute__ ((format (printf, 2, 3)))
		{
			va_list a; va_start (a, fmt);
			va_list b; va_copy (b, a);
			int k = vsnprintf (0, 0, fmt, a);
			va_end (a);
			need (k);
			vsnprintf (p + n, (size_t) k + 1, fmt, b);
			va_end (b);
			n += k;
		}
	};
	static void put_trace (Buf &o, const Ifs &f)
	{
		o.f ("trace=%s\r\nframe=%.17g %.17g %.17g %.17g\r\n", f.name, f.x0, f.y0, f.x1, f.y1);
		for (int i = 0; i < f.n; i++)
		{
			const Xform &m = f.t[i];
			o.f ("t=%.17g %.17g %.17g %.17g %.17g %.17g %.17g\r\n", m.a, m.b, m.c, m.d, m.e, m.f, m.p);
		}
	}
	static void put_style (Buf &o, const Style &s)
	{
		static char b[NPART * 300];
		o.f ("style=%s\r\n", s.name);
		int k = s.format (b, sizeof b);
		o.need (k); memcpy (o.p + o.n, b, (size_t) k); o.n += k;
	}
	bool save (const char *path) const
	{
		Buf o;
		o.f ("; Arabesque : de quoi recomposer le morceau à l'identique\r\n[Arabesque]\r\nversion=1\r\n");
		o.f ("seed=%u\r\ntempo=%d\r\ncopyright=%s\r\nlength=%u\r\nscale=%s\r\nmask=%u\r\nroot=%d\r\n", seed, tempo, copyright, length, scaleName, mask, root);
		if (form && form->n) form->format ([&] (const char *s) { o.f ("%s", s); });	// (sans forme : comme avant)
		for (int i = 0; i < NPART; i++)
			if (emptied[i] || mix[i][0] >= 0 || mix[i][1] >= 0 || mix[i][2] >= 0)
				o.f ("mix=%d %d %d %d %d\r\n", i + 1, mix[i][0], mix[i][1], mix[i][2], emptied[i] ? 1 : 0);
		o.f ("\r\n");
		// les styles a part des lettres de la forme
		if (form && form->n)
			for (int l = 0; l < 26; l++)
				if (form->def[l].style && form->uses (l)) { o.f ("[Letter]\r\nletter=%c\r\n", 'A' + l); put_style (o, *form->def[l].style); }
		for (int i = 0; i < nimg; i++)
		{
			const ImgRef &r = img[i];
			o.f ("[Image]\r\n");
			if (!r.data)
			{
				o.f ("gen=%s %u\r\nsize=%d %d\r\n", r.kind[0] ? r.kind : "mandala", r.seed, r.w, r.h);
				char s[400];
				if (r.set.n) { r.set.format (s, sizeof s); o.f ("set=%s\r\n", s); }	// (sans reglage : comme avant)
			}
			else
			{
				o.f ("file=%s\r\ndata=", r.file);
				o.need ((r.size + 2) / 3 * 4);
				o.n += b64_encode (r.data, r.size, o.p + o.n);
				o.f ("\r\n");
			}
			o.f ("\r\n");
		}
		for (int i = 0; i < nch; i++)
		{
			const Change &c = ch[i];
			switch (c.kind)
			{
			case CH_STATE:
				o.f ("[State]\r\nat=%u\r\nimage=%d\r\n", c.at, c.image);
				if (c.chords && c.chords->length > 0) c.chords->format ([&] (const char *s) { o.f ("%s", s); });	// (sans grille : comme avant)
				put_trace (o, *c.ifs); put_style (o, *c.style);
				break;
			case CH_SCALE: o.f ("[Scale]\r\nat=%u\r\nnow=%u\r\nmask=%u\r\nroot=%d\r\n\r\n", c.at, c.now, c.mask, c.root); break;
			case CH_STYLE: o.f ("[Style]\r\nat=%u\r\nnow=%u\r\n", c.at, c.now); put_style (o, *c.style); break;
			case CH_CLEAR: o.f ("[Clear]\r\nat=%u\r\npart=%d\r\n\r\n", c.at, c.part + 1); break;
			}
		}
		return write_all (path, o.p, o.n);
	}

	// -> 0, ou le message d'erreur
	const char *load (const char *path)
	{
		long size = 0;
		char *text = read_all (path, &size);
		if (!text) return "Le fichier ne se lit pas.";
		clear ();
		delete form; form = 0;
		seed = 1; tempo = 120; length = 0; mask = 0xAB5; root = 0; copyright[0] = scaleName[0] = 0;
		for (int i = 0; i < NPART; i++) { mix[i][0] = mix[i][1] = mix[i][2] = -1; emptied[i] = false; }
		char sect[16] = "";
		Change *cur = 0;
		Buf style; bool inStyle = false; char styleName[64] = "";
		Ifs *ifs = 0;
		ImgRef pending; memset (&pending, 0, sizeof pending); pending.w = pending.h = 512;
		int letter = -1;
		auto end_section = [&] ()
		{
			if (!strcmp (sect, "Letter") && inStyle && letter >= 0 && form)
			{
				style.need (0); style.p[style.n] = 0;
				Style *st = new Style ();
				st->parse (style.p);
				snprintf (st->name, sizeof st->name, "%s", styleName);
				delete form->def[letter].style; form->def[letter].style = st;
			}
			if (!strcmp (sect, "Image"))
			{
				ImgRef &r = add_image ();
				r = pending;
				if (!r.data && !r.kind[0]) strcpy (r.kind, "mandala");
				memset (&pending, 0, sizeof pending); pending.w = pending.h = 512;
			}
			if (cur)
			{
				if (inStyle)
				{
					style.need (0); style.p[style.n] = 0;
					cur->style = new Style ();
					cur->style->parse (style.p);
					snprintf (cur->style->name, sizeof cur->style->name, "%s", styleName);
				}
				if (ifs) { ifs->prepare (); cur->ifs = ifs; ifs = 0; }
			}
			delete ifs; ifs = 0;
			cur = 0; inStyle = false; style.n = 0; letter = -1;
		};
		for (char *s = text; *s; )
		{
			char *e = s;
			while (*e && *e != '\n') e++;
			char *next = *e ? e + 1 : e;
			*e = 0;
			if (e > s && e[-1] == '\r') e[-1] = 0;
			char *line = s;
			s = next;
			if (line[0] == ';') continue;
			if (line[0] == '[' && strncmp (line, "[Pt_", 4))
			{
				end_section ();
				snprintf (sect, sizeof sect, "%s", line + 1);
				char *b = strchr (sect, ']'); if (b) *b = 0;
				int kind = !strcmp (sect, "State") ? CH_STATE : !strcmp (sect, "Scale") ? CH_SCALE : !strcmp (sect, "Style") ? CH_STYLE : !strcmp (sect, "Clear") ? CH_CLEAR : -1;
				if (kind >= 0) cur = &add (kind, 0);
				continue;
			}
			if (inStyle) { size_t l = strlen (line); style.need ((long) l + 1); memcpy (style.p + style.n, line, l); style.n += (long) l; style.p[style.n++] = '\n'; continue; }
			char *eq = strchr (line, '=');
			if (!eq) continue;
			*eq = 0;
			const char *k = line, *v = eq + 1;
			#define K(x) (!strcmp (k, x))
			if (!strcmp (sect, "Arabesque"))
			{
				if (K ("seed")) seed = (uint32_t) strtoul (v, 0, 10);
				else if (K ("tempo")) { tempo = atoi (v); tempo = tempo < 20 ? 20 : tempo > 300 ? 300 : tempo; }
				else if (K ("copyright")) snprintf (copyright, sizeof copyright, "%s", v);
				else if (K ("length")) length = (uint32_t) strtoul (v, 0, 10);
				else if (K ("scale")) snprintf (scaleName, sizeof scaleName, "%s", v);
				else if (K ("mask")) mask = (unsigned) strtoul (v, 0, 10) & 0xFFF;
				else if (K ("root")) root = atoi (v) % 12;
				else if (K ("form")) { if (!form) form = new MusicForm (); if (!form->set_text (v)) { delete form; form = 0; } }
				else if (K ("section")) { if (form) form->parse_section (v); }
				else if (K ("mix"))
				{
					int part, a, b, c, em;
					if (sscanf (v, "%d %d %d %d %d", &part, &a, &b, &c, &em) == 5 && part >= 1 && part <= NPART)
					{
						int q[3] = { a, b, c };
						for (int j = 0; j < 3; j++) mix[part - 1][j] = q[j] < -1 ? -1 : q[j] > 127 ? 127 : q[j];
						emptied[part - 1] = em == 1;
					}
				}
			}
			else if (!strcmp (sect, "Letter"))
			{
				char c = v[0] >= 'a' && v[0] <= 'z' ? (char) (v[0] - 32) : v[0];
				if (K ("letter") && strlen (v) == 1 && c >= 'A' && c <= 'Z') letter = c - 'A';
				else if (K ("style")) { snprintf (styleName, sizeof styleName, "%s", v); inStyle = true; style.n = 0; }
			}
			else if (!strcmp (sect, "Image"))
			{
				if (K ("mandala")) { strcpy (pending.kind, "mandala"); pending.seed = (uint32_t) strtoul (v, 0, 10); }
				else if (K ("gen")) { char kd[16]; unsigned sd; if (sscanf (v, "%15s %u", kd, &sd) == 2) { snprintf (pending.kind, sizeof pending.kind, "%s", kd); pending.seed = sd; } }
				else if (K ("size")) { int w, h; if (sscanf (v, "%d %d", &w, &h) == 2) { pending.w = w < 16 ? 16 : w > 4096 ? 4096 : w; pending.h = h < 16 ? 16 : h > 4096 ? 4096 : h; } }
				else if (K ("set")) pending.set.parse (v);
				else if (K ("file")) snprintf (pending.file, sizeof pending.file, "%s", v);
				else if (K ("data"))
				{
					long n = (long) strlen (v);
					free (pending.data);
					pending.data = (unsigned char *) malloc ((size_t) (n / 4 * 3 + 3));
					pending.size = b64_decode (v, n, pending.data);
				}
			}
			else if (cur)
			{
				if (K ("at")) cur->at = (uint32_t) strtoul (v, 0, 10);
				else if (K ("now")) cur->now = (uint32_t) strtoul (v, 0, 10);
				else if (K ("image")) cur->image = atoi (v);
				else if (K ("chords")) { if (!cur->chords) cur->chords = new ChordGrid (); cur->chords->parse_header (v); }
				else if (K ("chord")) { if (!cur->chords) cur->chords = new ChordGrid (); cur->chords->parse_chord (v); }
				else if (K ("mask")) cur->mask = (unsigned) strtoul (v, 0, 10) & 0xFFF;
				else if (K ("root")) cur->root = atoi (v) % 12;
				else if (K ("part")) { int p = atoi (v) - 1; cur->part = p < 0 ? 0 : p >= NPART ? NPART - 1 : p; }
				else if (K ("trace")) { delete ifs; ifs = new Ifs (); snprintf (ifs->name, sizeof ifs->name, "%s", v); }
				else if (K ("frame")) { if (ifs) sscanf (v, "%lf %lf %lf %lf", &ifs->x0, &ifs->y0, &ifs->x1, &ifs->y1); }
				else if (K ("t"))
				{
					Xform m;
					if (ifs && ifs->n < IFS_MAX && sscanf (v, "%lf %lf %lf %lf %lf %lf %lf", &m.a, &m.b, &m.c, &m.d, &m.e, &m.f, &m.p) == 7) ifs->t[ifs->n++] = m;
				}
				else if (K ("style")) { snprintf (styleName, sizeof styleName, "%s", v); inStyle = true; style.n = 0; }
			}
			#undef K
		}
		end_section ();
		free (pending.data);
		free (text);
		// ce qui doit y etre
		if (!nch || ch[0].kind != CH_STATE || !nimg) return "Ce fichier n'est pas un préréglage d'Arabesque.";
		for (int i = 0; i < nch; i++)
		{
			const Change &c = ch[i];
			if (c.kind == CH_STATE && (!c.style || !c.ifs || c.ifs->n < 2 || c.image < 0 || c.image >= nimg)) return "Le préréglage est abîmé.";
			if (c.kind == CH_STYLE && !c.style) return "Le préréglage est abîmé.";
		}
		return 0;
	}
};

#endif
