/* ls - liste un dossier : ls [-a] [-l] [-1] [-C] [--color[=QUAND]] [chemins] (docs/OUTILS.md),
 * comme ls.asm. -l : « drwxr-xr-x taille nom » (le bit x : vfs, V_STATX) ; -C : en colonnes,
 * triées de haut en bas comme GNU ls (largeur : le pty de la sortie, VT_GWINSZ, sinon $COLUMNS,
 * sinon 80) ; --color : les couleurs de dircolors (LS_COLORS : di, ex, fi et *.ext). Sans option :
 * un nom par ligne, sans couleur. Comme l'asm :
 *  - les options sont en tête seulement (après le premier chemin, « -l » est un chemin) ; une lettre
 *    inconnue est ignorée ; « -1 » oublie -C ; « -- » finit les options ; une option longue autre
 *    que --color[=QUAND] : message et code 2 ;
 *  - les fichiers donnés sont affichés dans l'ordre donné, ensemble (en colonnes avec -C), avant le
 *    dossier qui les suit ; un dossier : ses entrées triées par octets, sans en-tête (plusieurs
 *    dossiers se suivent) ; « . » et les noms en « . » cachés sans -a ; 16 384 noms au plus ;
 *  - la recherche dans LS_COLORS ignore la casse, et saute l'entrée qui suit quand la différence
 *    tombe sur le « : » (et lit au-delà de la fin sur une valeur coupée, comme l'asm). */
#include "korinrt.h"

#define OPT_A 1
#define OPT_L 2
#define OPT_COL 4
#define OPT_C 8
#define NAMES_MAX 16384

struct entry {
	u64 size, attr;
	char name[];
};

static long opts;
static long when;        /* --color : 0 jamais, 1 toujours, 2 sur un terminal */
static u64 width = 80;
static const char *lsc;  /* LS_COLORS (0 : absente) */
static int reset_done;   /* ESC[0m déjà écrit */
static struct entry **pending; /* les fichiers donnés, en attente */
static u64 pending_n;

/* « --... » -> 0 (option lue), 1 (« -- » : fin des options), < 0 (erreur dite) */
static int long_opt(const char *a)
{
	static const char words[] = "1always\0" "1yes\0" "1force\0" "0never\0" "0no\0" "0none\0"
				    "2auto\0" "2tty\0" "2if-tty\0";
	if (!a[2])
		return 1;
	const char *p = a + 2;
	for (const char *c = "color"; *c;)
		if (*c++ != *p++)
			goto unknown;
	if (!*p) { /* « --color » seul : toujours */
		when = 1;
		return 0;
	}
	if (*p != '=')
		goto unknown;
	p++;
	for (const char *w = words; *w; w += str_len(w) + 1)
		if (str_eq(p, w + 1)) {
			when = *w - '0';
			return 0;
		}
	err_msg("--color : QUAND vaut always, auto ou never");
	return -1;
unknown:
	err_msg("option inconnue (--color[=QUAND], -a, -l, -C, -1)");
	return -1;
}

/* texte décimal -> valeur (0 : illisible) */
static u64 number(const char *s)
{
	u64 v = 0;
	for (; *s; s++) {
		u8 d = (u8)(*s - '0');
		if (d > 9)
			return 0;
		v = v * 10 + d;
	}
	return v;
}

/* la couleur et la largeur de -C : le pty de la sortie standard (V_STDIO : un bout de vterm),
 * sinon $COLUMNS, 80 */
static void output_init(void)
{
	long m[MSG_SIZE / 8];
	long vt = 0; /* bout de vterm de la sortie (0 : aucun) */
	if (when == 1)
		opts |= OPT_COL;
	if ((opts & OPT_C) || when == 2) {
		memset(m, 0, sizeof m);
		m[0] = V_STDIO;
		if (vfs_call(m) >= 0 && (m[MSG_B / 8] & VT_TAG))
			vt = m[MSG_B / 8];
		if (when == 2 && vt)
			opts |= OPT_COL;
	}
	u64 w = 80;
	if (opts & OPT_C) {
		const char *c = getenv("COLUMNS");
		if (c) {
			i64 v = (i64)number(c);
			if (v > 0)
				w = (u64)v;
		}
		if (vt) { /* le pty : sa taille */
			long ch = korin_sys2(SYS_SVC_OPEN, "vterm", 5000);
			if (ch >= 0) {
				memset(m, 0, sizeof m);
				m[0] = VT_GWINSZ;
				m[MSG_A / 8] = vt;
				if (korin_sys2(SYS_SVC_CALL, ch, m) >= 0 && m[MSG_B / 8] > 0)
					w = (u64)m[MSG_B / 8];
			}
		}
	}
	width = w;
	if (opts & OPT_COL)
		lsc = getenv("LS_COLORS");
}

/* nom, taille, attributs -> entrée (le nom copié) */
static struct entry *new_entry(const char *name, u64 size, u64 attr)
{
	struct entry *e = ak_malloc((u64)str_len(name) + 17);
	e->size = size;
	e->attr = attr;
	char *o = e->name;
	while ((*o++ = *name++))
		;
	return e;
}

/* clé (« di », « *.png ») -> sa valeur dans LS_COLORS, *n = longueur (0 : absente) */
static const char *lsc_find(const char *key, u64 *n)
{
	const char *p = lsc;
	*n = 0;
	if (!p)
		return 0;
	while (*p) {
		const char *k = key;
		for (;;) {
			u8 a = (u8)*k++;
			if (!a) {
				if (*p == '=')
					goto found;
				break;
			}
			u8 b = (u8)*p++;
			if ((u8)(b - 'A') <= 25)
				b += 32;
			if ((u8)(a - 'A') <= 25)
				a += 32;
			if (a != b)
				break;
		}
		for (;;) { /* entrée suivante (depuis là où la différence est tombée) */
			char c = *p;
			if (!c)
				return 0;
			p++;
			if (c == ':')
				break;
		}
	}
	return 0;
found:
	p++;
	u64 l = 0;
	while (p[l] && p[l] != ':')
		l++;
	*n = l;
	return p;
}

/* les couleurs de dircolors (GNU) : un groupe = sa couleur, ses extensions, 0 */
static const char ext_table[] =
	"01;31\0tar\0tgz\0gz\0bz2\0xz\0zst\0zip\0" "7z\0rar\0opk\0deb\0rpm\0jar\0\0"
	"01;35\0png\0jpg\0jpeg\0gif\0bmp\0svg\0webp\0tif\0tiff\0ico\0mp4\0webm\0mkv\0avi\0mov\0\0"
	"00;36\0mp3\0wav\0flac\0ogg\0opus\0m4a\0aac\0mid\0midi\0\0";

/* entrée -> sa couleur (« 01;34 »), *n = longueur (0 : aucune). LS_COLORS d'abord (di, ex,
 * *.ext, fi), puis les couleurs de dircolors */
static const char *color_of(struct entry *e, u64 *n)
{
	const char *c;
	if (e->attr & ATTR_DIR) {
		c = lsc_find("di", n);
		if (*n)
			return c;
		c = "01;34";
		goto fixed;
	}
	if (e->attr & ATTR_EXEC) {
		c = lsc_find("ex", n);
		if (*n)
			return c;
		c = "01;32";
		goto fixed;
	}
	const char *x = 0, *p = e->name; /* l'extension : après le dernier point */
	for (;;) {
		char ch = *p++;
		if (!ch)
			break;
		if (ch == '.')
			x = p;
	}
	if (!x || !*x || (u64)(p - x) >= 64)
		goto fi;
	{
		char key[80] = "*.";
		str_cat(key + 2, x);
		c = lsc_find(key, n);
		if (*n)
			return c;
	}
	for (const char *g = ext_table; *g;) {
		const char *gc = g;
		g += str_len(g) + 1;
		for (; *g; g += str_len(g) + 1) {
			const char *a = g, *b = x;
			for (;;) {
				u8 u = (u8)*a++, v = (u8)*b++;
				if ((u8)(v - 'A') <= 25)
					v += 32;
				if (u != v)
					break;
				if (!u) {
					c = gc;
					goto fixed;
				}
			}
		}
		g++; /* groupe suivant */
	}
fi:
	return lsc_find("fi", n);
fixed:
	*n = (u64)str_len(c);
	return c;
}

/* entrée : son nom, en couleur s'il le faut (comme GNU ls : ESC[0m une fois avant la première
 * couleur, puis ESC[<couleur>m nom ESC[0m) */
static void put_name(struct entry *e)
{
	if (opts & OPT_COL) {
		u64 n;
		const char *c = color_of(e, &n);
		if (!n)
			goto plain;
		if (n <= 2 && c[0] == '0' && c[n - 1] == '0') /* « 0 », « 00 » : pas de couleur */
			goto plain;
		if (!reset_done) {
			reset_done = 1;
			out_puts("\033[0m");
		}
		out_putc(27);
		out_putc('[');
		out_write(c, n);
		out_putc('m');
		out_puts(e->name);
		out_puts("\033[0m");
		return;
	}
plain:
	out_puts(e->name);
}

/* entrée : « drwxr-xr-x taille nom » */
static void long_line(struct entry *e)
{
	const char *p = "-rw-r--r--";
	if (e->attr & ATTR_EXEC)
		p = "-rwxr-xr-x";
	if (e->attr & ATTR_DIR)
		p = "drwxr-xr-x";
	out_puts(p);
	out_putc(' ');
	out_putn(e->size);
	out_putc(' ');
	put_name(e);
	out_nl();
}

/* chaîne UTF-8 -> colonnes (un caractère : une) */
static u64 name_width(const char *s)
{
	u64 n = 0;
	for (; *s; s++)
		if (((u8)*s & 0xC0) != 0x80)
			n++;
	return n;
}

/* -C comme GNU ls : le plus de colonnes possible, les noms de haut en bas ; une colonne : son
 * nom le plus long + 2 (la dernière, sans les 2), 3 au moins ; la ligne doit rester plus courte
 * que la largeur */
static void columns(struct entry **v, u64 n)
{
	if (!n)
		return;
	u64 *w = ak_malloc(n * 8);
	for (u64 i = 0; i < n; i++)
		w[i] = name_width(v[i]->name);
	u64 W = width;
	u64 cols = W / 3;
	if (cols < 1)
		cols = 1;
	if (cols > n)
		cols = n;
	u64 *cw = ak_malloc(cols * 8);
	u64 rows;
	for (;;) {
		if (cols <= 1) {
			cols = 1;
			rows = n;
			break;
		}
		rows = (n + cols - 1) / cols;
		for (u64 i = 0; i < cols; i++)
			cw[i] = 3;
		u64 len = cols * 3;
		u64 i;
		for (i = 0; i < n; i++) {
			u64 c = i / rows;
			u64 x = c == cols - 1 ? w[i] : w[i] + 2;
			if (cw[c] < x) {
				len += x - cw[c];
				cw[c] = x;
				if (len >= W)
					break;
			}
		}
		if (i >= n)
			break;
		cols--;
	}
	for (u64 r = 0; r < rows; r++) {
		u64 i = r;
		for (;;) {
			put_name(v[i]);
			u64 pad = cw[i / rows] - w[i]; /* (une colonne : jamais servi) */
			i += rows;
			if (i >= n)
				break;
			for (; pad; pad--)
				out_putc(' ');
		}
		out_nl();
	}
}

/* entrées, nombre, leur dossier (0 : des chemins donnés) : le bit d'exécution (V_STATX, un
 * appel par fichier) s'il sert, avec -l ou la couleur */
static void prepare(struct entry **v, u64 n, const char *dir)
{
	if (!(opts & (OPT_L | OPT_COL)))
		return;
	long m[MSG_SIZE / 8];
	char path[320];
	for (u64 i = 0; i < n; i++) {
		struct entry *e = v[i];
		if (e->attr & ATTR_DIR)
			continue;
		const char *p = e->name;
		if (dir) {
			path_join2(path, dir, e->name);
			p = path;
		}
		vfs_msg(m, V_STATX, 0, 0, p, 0);
		if (vfs_call(m) < 0)
			continue;
		if (m[MSG_C / 8] & ATTR_EXEC)
			e->attr |= ATTR_EXEC;
	}
}

/* entrées, nombre : -l, -C, ou un nom par ligne */
static void show(struct entry **v, u64 n)
{
	if (opts & OPT_L) {
		for (u64 i = 0; i < n; i++)
			long_line(v[i]);
	} else if (opts & OPT_C) {
		columns(v, n);
	} else {
		for (u64 i = 0; i < n; i++) {
			put_name(v[i]);
			out_nl();
		}
	}
}

/* les fichiers donnés en attente : affichés */
static void flush_pending(void)
{
	u64 n = pending_n;
	if (!n)
		return;
	pending_n = 0;
	prepare(pending, n, 0);
	show(pending, n);
}

/* tableau d'entrées : tri par insertion, octet par octet (non signé) */
static void sort_entries(struct entry **v, u64 n)
{
	for (u64 i = 1; i < n; i++) {
		struct entry *x = v[i];
		u64 j = i;
		while (j > 0) {
			const u8 *a = (const u8 *)v[j - 1]->name, *b = (const u8 *)x->name;
			while (*a == *b && *a)
				a++, b++;
			if (*a <= *b)
				break;
			v[j] = v[j - 1];
			j--;
		}
		v[j] = x;
	}
}

/* chemin d'un dossier -> 0, 1 */
static int list_dir(const char *path)
{
	static struct dirent ent;
	long fd = sys_open(path, O_READ);
	if (fd < 0) {
		err_file(path);
		return 1;
	}
	struct entry **v = ak_malloc(8 * NAMES_MAX);
	u64 n = 0;
	while (sys_readdir(fd, &ent) == 1) {
		if (ent.name[0] == '.' && !(opts & OPT_A))
			continue;
		if (n >= NAMES_MAX)
			continue;
		v[n++] = new_entry(ent.name, ent.size, ent.attr);
	}
	sys_close(fd);
	sort_entries(v, n);
	prepare(v, n, path);
	show(v, n);
	return 0;
}

/* chemin donné -> 0, 1 (inaccessible). Un dossier : listé ; un fichier : mis en attente */
static int operand(const char *path)
{
	long st[2];
	if (sys_stat(path, st) < 0) {
		flush_pending();
		err_file(path);
		return 1;
	}
	if (st[1] & ATTR_DIR) {
		flush_pending();
		return list_dir(path);
	}
	struct entry *e = new_entry(path, (u64)st[0], (u64)st[1]);
	if (pending_n < NAMES_MAX) {
		if (!pending)
			pending = ak_malloc(8 * NAMES_MAX);
		pending[pending_n++] = e;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int i = 1;
	while (i < argc && is_opt(argv[i])) {
		const char *a = argv[i++];
		if (a[1] == '-') { /* --color[=QUAND], -- */
			int r = long_opt(a);
			if (r == 0)
				continue;
			if (r > 0)
				break;
			return 2;
		}
		for (const char *o = a + 1; *o; o++) {
			if (*o == 'a')
				opts |= OPT_A;
			else if (*o == 'l')
				opts |= OPT_L;
			else if (*o == 'C')
				opts |= OPT_C;
			else if (*o == '1') /* un par ligne : -C oublié */
				opts &= ~OPT_C;
		}
	}
	output_init();
	int code = 0;
	if (i >= argc)
		code = operand(".");
	for (; i < argc; i++)
		code |= operand(argv[i]);
	flush_pending();
	return code;
}
