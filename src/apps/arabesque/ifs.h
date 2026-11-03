//
// ifs.h -- le trace d'Arabesque : un systeme de fonctions iterees (IFS), de 2 a 20 transformations affines tirees au
// hasard selon leurs probabilites. Le fichier .ifs est celui de Fractint et d'Art Song : nom { a b c d e f p ... }, avec
// x' = ax + by + e et y' = cx + dy + f.
//
#ifndef ARABESQUE_IFS_H
#define ARABESQUE_IFS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

// un fichier entier, termine par NUL (a liberer) ; 0 s'il ne se lit pas
static char *read_all (const char *path, long *size = 0)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	long cap = 65536, n = 0; char *b = (char *) malloc (cap + 1);
	for (;;)
	{
		if (n == cap) { cap *= 2; b = (char *) realloc (b, cap + 1); }
		long r = read (f, b + n, cap - n);
		if (r <= 0) break;
		n += r;
	}
	close (f);
	b[n] = 0;
	if (size) *size = n;
	return b;
}
static bool write_all (const char *path, const void *data, long n)
{
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f < 0) return false;
	bool ok = write (f, data, n) == n;
	close (f);
	return ok;
}

struct Rng
{
	uint32_t s;
	explicit Rng (uint32_t seed = 1) : s (seed ? seed : 1) {}
	uint32_t next () { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	double unit () { return next () / 4294967296.0; }
	int below (int n) { return (int) (next () % (uint32_t) n); }
};

enum { IFS_MAX = 20 };
struct Xform { double a, b, c, d, e, f, p; };

struct Ifs
{
	char name[64];
	Xform t[IFS_MAX];
	int n;
	double cum[IFS_MAX];			// probabilites cumulees
	double x0, y0, x1, y1;			// le cadre du trace : ce que couvre l'image

	Ifs () : n (0), x0 (0), y0 (0), x1 (1), y1 (1) { name[0] = 0; }

	void prepare ()
	{
		double s = 0;
		for (int i = 0; i < n; i++) s += t[i].p > 0 ? t[i].p : 0;
		double c = 0;
		for (int i = 0; i < n; i++) { c += s > 0 ? (t[i].p > 0 ? t[i].p : 0) / s : 1.0 / n; cum[i] = c; }
		if (n) cum[n - 1] = 1.01;
	}
	void step (double &x, double &y, Rng &r) const
	{
		double u = r.unit ();
		int i = 0;
		while (i < n - 1 && u >= cum[i]) i++;
		const Xform &m = t[i];
		double nx = m.a * x + m.b * y + m.e, ny = m.c * x + m.d * y + m.f;
		if (nx > 1e9 || nx < -1e9 || ny > 1e9 || ny < -1e9) { nx = 0; ny = 0; }	// (un code qui diverge, en cours d'edition)
		x = nx; y = ny;
	}
	// le cadre d'apres le chemin : tout le trace tient dans l'image
	void limits ()
	{
		if (n < 1) { x0 = y0 = 0; x1 = y1 = 1; return; }
		prepare ();
		Rng r (0x5EED);
		double x = 0, y = 0;
		for (int i = 0; i < 64; i++) step (x, y, r);
		x0 = x1 = x; y0 = y1 = y;
		for (int i = 0; i < 30000; i++)
		{
			step (x, y, r);
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
		double mx = (x1 - x0) * 0.02, my = (y1 - y0) * 0.02;
		if (mx < 1e-9) mx = 0.5;
		if (my < 1e-9) my = 0.5;
		x0 -= mx; x1 += mx; y0 -= my; y1 += my;
	}
	// point du trace -> 0..1 dans l'image
	void to_unit (double x, double y, double &u, double &v) const
	{
		u = (x - x0) / (x1 - x0); v = 1.0 - (y - y0) / (y1 - y0);
		u = u < 0 ? 0 : u > 1 ? 1 : u; v = v < 0 ? 0 : v > 1 ? 1 : v;
	}
	void uniform ()
	{
		for (int i = 0; i < n; i++) t[i].p = 1.0 / n;
		prepare ();
	}
	// probabilites selon la taille de chaque transformation (son determinant)
	void relative ()
	{
		double s = 0;
		for (int i = 0; i < n; i++) { double d = t[i].a * t[i].d - t[i].b * t[i].c; d = d < 0 ? -d : d; if (d < 0.01) d = 0.01; t[i].p = d; s += d; }
		for (int i = 0; i < n; i++) t[i].p /= s;
		prepare ();
	}
	void add ()
	{
		if (n >= IFS_MAX) return;
		Rng r ((uint32_t) (n * 7919 + 13));
		double w = x1 - x0, h = y1 - y0;
		Xform &m = t[n++];
		m.a = 0.4; m.b = 0; m.c = 0; m.d = 0.4;
		double cx = x0 + w * (0.25 + 0.5 * r.unit ()), cy = y0 + h * (0.25 + 0.5 * r.unit ());
		m.e = cx * 0.6; m.f = cy * 0.6;
		m.p = 1.0 / n;
		prepare ();
	}
	void remove (int i)
	{
		if (i < 0 || i >= n) return;
		memmove (t + i, t + i + 1, sizeof t[0] * (size_t) (n - 1 - i));
		n--;
		prepare ();
	}
	// le premier code d'un texte .ifs (les commentaires de Fractint : « ; » jusqu'a la fin de la ligne)
	bool parse (const char *s)
	{
		const char *o = strchr (s, '{');
		if (!o) return false;
		const char *b = o;
		while (b > s && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n')) b--;
		const char *a = b;
		while (a > s && a[-1] != '\n' && a[-1] != '}') a--;
		int l = (int) (b - a) < (int) sizeof name - 1 ? (int) (b - a) : (int) sizeof name - 1;
		memcpy (name, a, (size_t) l); name[l] = 0;
		double v[7]; int k = 0;
		n = 0;
		for (const char *p = o + 1; *p && *p != '}'; )
		{
			if (*p == ';') { while (*p && *p != '\n') p++; continue; }
			char *e;
			double d = strtod (p, &e);
			if (e == p) { p++; continue; }
			p = e;
			v[k++] = d;
			if (k == 7)
			{
				k = 0;
				if (n < IFS_MAX) { Xform &m = t[n++]; m.a = v[0]; m.b = v[1]; m.c = v[2]; m.d = v[3]; m.e = v[4]; m.f = v[5]; m.p = v[6]; }
			}
		}
		if (n < 2) return false;
		prepare ();
		limits ();
		return true;
	}
	int format (char *out, int cap) const
	{
		int w = snprintf (out, cap, "%s {\r\n", name[0] ? name : "trace");
		for (int i = 0; i < n; i++)
			w += snprintf (out + w, cap - w, "%.6f %.6f %.6f %.6f %.6f %.6f %.6f\r\n", t[i].a, t[i].b, t[i].c, t[i].d, t[i].e, t[i].f, t[i].p);
		w += snprintf (out + w, cap - w, "}\r\n");
		return w;
	}
	bool load (const char *path)
	{
		char *s = read_all (path);
		if (!s) return false;
		bool ok = parse (s);
		free (s);
		return ok;
	}
	bool save (const char *path) const
	{
		char b[IFS_MAX * 100 + 200];
		return write_all (path, b, format (b, sizeof b));
	}
};

#endif
