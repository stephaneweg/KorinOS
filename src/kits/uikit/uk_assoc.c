/* uk_assoc.c -- UIKit : l'association des fichiers (uikit_assoc.inc ;
 * fileassoc.h de SystemKit d'Onyx).
 *
 * Chaque application dit dans son manifeste (/apps/<nom>.app/app.txt) les
 * extensions qu'elle ouvre : « opens = rtf docx odt ». Le choix par défaut
 * d'une extension est dans /etc/fileassoc.ini, « ext = app » une par
 * ligne (# : commentaire), qui l'emporte : « Ouvrir avec... » l'écrit quand
 * « Toujours » est coché. Sans ligne, la première application qui déclare
 * l'extension ; sans application, l'Éditeur.
 *
 *	ui_assoc_app (chemin, tampon, taille)	le programme qui ouvre le fichier
 *	ui_assoc_open (chemin)			l'ouvrir
 *	ui_assoc_set (extension, app)		le choix par défaut d'une extension
 *	ui_open_with (chemin)			le dialogue « Ouvrir avec »
 *	ui_arg (ligne d'arguments)		le premier argument, sans guillemets
 *
 * Port ligne à ligne de l'assembleur (src/kits/uikit/PORT.md). Variables :
 * uk.fa_* (uk_fa_* de l'asm). Le dialogue utilise le bloc DG_* de
 * uk_dialog.c (DG_LIST). */
#include "uk.h"

/* chaînes de uikit_assoc.inc */
static const char uk_fa_s_apps[] = "/apps";
static const char uk_fa_s_dotapp[] = ".app";
static const char uk_fa_s_app[] = "app";
static const char uk_fa_s_apptxt[] = "/app.txt";
static const char uk_fa_s_main[] = "/main";
static const char uk_fa_s_files[] = "/apps/files.app/main";
static const char uk_fa_s_editor[] = "editor";
static const char uk_fa_s_opens[] = "opens";
static const char uk_fa_s_name[] = "name";
static const char uk_fa_s_eq[] = " = ";
static const char uk_fa_s_paren[] = " (.";
static const char uk_fa_s_inihead[] = "# Associations des fichiers : « extension = application »\n"
				      "# (/apps/<application>.app ; UIKit, ui_assoc_*).\n";
static const char uk_fa_s_title[] = "Ouvrir avec";
static const char uk_fa_s_choose[] = "Choisir l'application qui ouvre ce fichier :";
static const char uk_fa_s_sugg[] = "Applications proposées";
static const char uk_fa_s_other[] = "Autres applications";
static const char uk_fa_s_always[] = "Toujours ouvrir ce type de fichier avec elle";

/* ligne « clé = valeur » : x0 = ligne suivante (0 : fin), x1, x2 = clé, x3, x4 = valeur */
typedef struct {
	long next, k0, k1, v0, v1;
} uk_kv;

/* ---------------------------------------------------------------------------
 *  Lecture des manifestes
 * ------------------------------------------------------------------------- */

/* uk_fa_ext : x0 = chemin -> x0 = 1 : son extension en minuscules dans
 * uk_fa_ext_b ; 0 : aucune (pas de point, point en tête du nom, plus de 15
 * octets) */
static long uk_fa_ext(long path)
{
	long start = path, dot = 0, p = path, b = UKA(fa_ext_b), i, c;

	for (;;) {
		c = LDB(p, 0);
		p++;
		if (!c)
			break;
		if (c == '/' || c == ':') {
			start = p;
			dot = 0;
			continue;
		}
		if (c != '.')
			continue;
		if (p - 1 == start) /* « .profil » : pas d'extension */
			continue;
		dot = p;
	}
	if (!dot)
		goto no;
	i = 0;
	for (;;) {
		c = LDB(dot, i);
		if (!c)
			break;
		if (!ULT(i, 15))
			goto no;
		if ((u32)(c - 'A') <= 25)
			c += 32;
		STB(b, i, c);
		i++;
	}
	if (!i)
		goto no;
	STB(b, i, 0);
	return 1;
no:
	STB(b, 0, 0);
	return 0;
}

/* uk_fa_read : x0 = chemin -> x0 = octets lus dans uk_fa_buf (terminé par 0 ; 0 : absent) */
static long uk_fa_read(long path)
{
	long fd, n;

	STB(UKA(fa_buf), 0, 0);
	fd = SYS2(SYS_OPEN, path, O_READ);
	if (fd < 0)
		return 0;
	n = SYS3(SYS_READ, fd, UKA(fa_buf), UK_FA_BUF - 1);
	SYS1(SYS_CLOSE, fd);
	if (n < 0)
		n = 0;
	STB(UKA(fa_buf), n, 0);
	return n;
}

/* uk_fa_kv : x0 = curseur dans un texte « clé = valeur » -> ligne suivante (0 :
 * fin du texte), clé et valeur sans blancs autour ; ligne vide, sans « = » ou
 * commentaire : clé vide (comme l'asm : à la fin, seul x0 est écrit ; les
 * appelants ne lisent alors pas le reste) */
static uk_kv uk_fa_kv(long s)
{
	uk_kv r = {0, 0, 0, 0, 0};
	long e, k, q, v, c;

	if (!LDB(s, 0))
		return r; /* (fin) */
	e = s; /* fin de la ligne */
	for (;;) {
		c = LDB(e, 0);
		if (!c || c == 10)
			break;
		e++;
	}
	r.next = e; /* ligne suivante */
	if (c)
		r.next = e + 1;
	k = s;
	for (;;) {
		if (UGE(k, e))
			goto empty;
		c = LDB(k, 0);
		if (c != ' ' && c != 9)
			break;
		k++;
	}
	if (c == '#' || c == ';')
		goto empty;
	q = k; /* le « = » */
	for (;;) {
		if (UGE(q, e))
			goto empty;
		if (LDB(q, 0) == '=')
			break;
		q++;
	}
	r.k0 = k;
	r.k1 = q; /* fin de la clé */
	while (UGT(r.k1, k) && LDB(r.k1 - 1, 0) <= ' ')
		r.k1--;
	v = q + 1; /* début de la valeur */
	while (ULT(v, e) && LDB(v, 0) <= ' ')
		v++;
	r.v0 = v;
	r.v1 = v; /* fin de la valeur : # (commentaire), blancs, CR */
	while (ULT(r.v1, e) && LDB(r.v1, 0) != '#')
		r.v1++;
	while (UGT(r.v1, v) && LDB(r.v1 - 1, 0) <= ' ')
		r.v1--;
	return r;
empty:
	r.k0 = r.k1 = r.v0 = r.v1 = s;
	return r;
}

/* uk_fa_eq : x0, x1 = texte (début, fin), x2 = mot en minuscules (terminé par
 * 0) -> x0 = 1 s'ils sont égaux, casse ASCII ignorée (le mot n'est pas mis en
 * minuscules, comme l'asm) */
static long uk_fa_eq(long a, long end, long word)
{
	long c, d;

	while (ULT(a, end)) {
		c = LDB(a, 0);
		a++;
		d = LDB(word, 0);
		word++;
		if (!d)
			return 0;
		if ((u32)(c - 'A') <= 25)
			c += 32;
		if (c != d)
			return 0;
	}
	return LDB(word, 0) == 0;
}

/* uk_fa_has : x0, x1 = valeur (début, fin), x2 = mot -> x0 = 1 si l'un de ses
 * mots (séparés par des blancs ou des virgules) est celui-là */
static long uk_fa_has(long p, long end, long word)
{
	long q, c;

	for (;;) {
		for (;;) {
			if (UGE(p, end))
				return 0;
			c = LDB(p, 0);
			if (c != ',' && c > ' ')
				break;
			p++;
		}
		q = p; /* fin du mot */
		while (ULT(q, end)) {
			c = LDB(q, 0);
			if (c == ',' || c <= ' ')
				break;
			q++;
		}
		c = uk_fa_eq(p, q, word);
		p = q;
		if (c)
			return 1;
	}
}

/* uk_fa_get : x0 = clé voulue (minuscules) -> x0 = 1 si uk_fa_buf l'a, *v0, *v1
 * = sa valeur (début, fin) */
static long uk_fa_get(long key, long *v0, long *v1)
{
	long p = UKA(fa_buf);
	uk_kv r;

	for (;;) {
		r = uk_fa_kv(p);
		if (!r.next)
			return 0;
		p = r.next;
		if (uk_fa_eq(r.k0, r.k1, key)) {
			*v0 = r.v0;
			*v1 = r.v1;
			return 1;
		}
	}
}

/* uk_fa_cat : x0 = destination, x1 = source -> x0 = fin (sur le 0) */
static long uk_fa_cat(long d, long s)
{
	long c;

	do {
		c = LDB(s, 0);
		s++;
		STB(d, 0, c);
		d++;
	} while (c);
	return d - 1;
}

/* uk_fa_apppath : x0 = nom d'application, x1 = suite (« /main ») -> x0 =
 * uk_fa_path « /apps/<nom>.app<suite> » */
static long uk_fa_apppath(long name, long suffix)
{
	long p = uk_fa_cat(UKA(fa_path), S(uk_fa_s_apps));

	STB(p, 0, '/');
	p = uk_fa_cat(p + 1, name);
	p = uk_fa_cat(p, S(uk_fa_s_dotapp));
	uk_fa_cat(p, suffix);
	return UKA(fa_path);
}

/* uk_fa_scan : x0 = fonction (x0 = uk_fa_name ; le manifeste dans uk_fa_buf ->
 * x0 = 0 : suivante) : appelée pour chaque application de /apps -> x0 = ce
 * qu'elle a rendu d'autre que 0 (le parcours s'arrête), 0 à la fin */
static long uk_fa_scan(long fn)
{
	long fd, len, off, i, c, d, e, r;

	fd = SYS2(SYS_OPEN, S(uk_fa_s_apps), O_READ);
	if (fd < 0)
		return 0;
	for (;;) {
		if (SYS2(SYS_READDIR, fd, UKA(fa_ent)) != 1)
			break;
		len = uk_strlen(UKA(fa_ent)); /* « <nom>.app », nom court */
		if (ULE(len, 4))
			continue;
		if (UGT(len, UK_FA_NAME + 3))
			continue;
		off = UKA(fa_ent) + len - 4;
		for (i = 0;; i++) {
			d = LDB(S(uk_fa_s_dotapp), i);
			if (!d)
				break;
			c = LDB(off, i) | 0x20;
			if (c != d)
				break;
		}
		if (d)
			continue;
		e = uk_memcpy(UKA(fa_name), UKA(fa_ent), len - 4);
		STB(e, 0, 0);
		if (!uk_fa_read(uk_fa_apppath(UKA(fa_name), S(uk_fa_s_apptxt))))
			continue;
		r = UK_CB1(fn, UKA(fa_name));
		if (!r)
			continue;
		SYS1(SYS_CLOSE, fd);
		return r;
	}
	SYS1(SYS_CLOSE, fd);
	return 0;
}

/* uk_fa_cb_opens : fonction de uk_fa_scan : 1 si le manifeste ouvre uk_fa_ext_b */
static long uk_fa_cb_opens(long name)
{
	long v0, v1;

	(void)name;
	if (!uk_fa_get(S(uk_fa_s_opens), &v0, &v1))
		return 0;
	return uk_fa_has(v0, v1, UKA(fa_ext_b));
}

/* uk_fa_present : x0 = nom d'application -> x0 = 1 si son paquet a un main */
static long uk_fa_present(long name)
{
	u8 st[16] __attribute__((aligned(16)));
	long r;

	r = SYS2(SYS_STAT, uk_fa_apppath(name, S(uk_fa_s_main)), (long)(uintptr_t)st);
	return LSR(~r, 63); /* (>= 0 : présent) */
}

/* uk_fa_find : x0 = chemin -> x0 = 1 : uk_fa_name est l'application qui
 * l'ouvre ; 0 : aucune, uk_fa_name est l'Éditeur. uk_fa_ext_b : son extension */
static long uk_fa_find(long path)
{
	long v0, v1, n, e;

	if (!uk_fa_ext(path))
		goto none;
	if (!uk_fa_read(uk_conf_path("fileassoc.ini", 0))) /* le choix par défaut (personnel, sinon /etc) */
		goto scan;
	if (!uk_fa_get(UKA(fa_ext_b), &v0, &v1))
		goto scan;
	n = v1 - v0;
	if (!n) /* « ext = » : rien ne l'ouvre */
		goto none;
	if (!ULT(n, UK_FA_NAME - 1))
		goto scan;
	e = uk_memcpy(UKA(fa_name), v0, n);
	STB(e, 0, 0);
	if (uk_fa_present(UKA(fa_name)))
		return 1;
scan: /* (une application retirée : les manifestes) */
	if (uk_fa_scan((long)uk_fa_cb_opens))
		return 1;
none:
	uk_fa_cat(UKA(fa_name), S(uk_fa_s_editor));
	return 0;
}

/* uk_fa_spawn : x0 = programme, x1 = chemin (0 : sans argument) -> x0 = pid
 * (< 0 : erreur). Un chemin avec des espaces ou des guillemets : entre
 * guillemets, \" et \\. */
static long uk_fa_spawn(long prog, long path)
{
	long arg = UKA(fa_arg), p, d, lim, c;

	if (!path)
		goto go;
	p = path; /* à citer ? */
	for (;;) {
		c = LDB(p, 0);
		p++;
		if (!c) { /* (copie : le tampon de l'appelant peut être uk_fa_path) */
			uk_fa_cat(arg, path);
			path = arg;
			goto go;
		}
		if (c == ' ' || c == '"')
			break;
	}
	d = arg;
	STB(d, 0, '"');
	d++;
	lim = arg + UK_PATH_MAX * 2 - 4; /* (borne : la place du " et du 0) */
	for (;;) {
		c = LDB(path, 0);
		path++;
		if (!c)
			break;
		if (UGE(d, lim))
			break;
		if (c == '"' || c == '\\') {
			STB(d, 0, '\\');
			d++;
		}
		STB(d, 0, c);
		d++;
	}
	STB(d, 0, '"');
	STB(d, 1, 0);
	path = arg;
go:
	return SYS2(SYS_SPAWN, prog, path);
}

/* ---------------------------------------------------------------------------
 *  Fonctions du kit
 * ------------------------------------------------------------------------- */

/* x0 = chemin d'un fichier, x1 = tampon, x2 = taille -> x0 = 1 : le main de
 * l'application qui l'ouvre dans le tampon ; 0 : aucune, celui de l'Éditeur */
long ui_assoc_app(long path, long buf, long size)
{
	long r, p, n, m, e;

	r = uk_fa_find(path);
	p = uk_fa_apppath(UKA(fa_name), S(uk_fa_s_main));
	if (size) {
		n = uk_strlen(p);
		m = size - 1;
		if (ULT(n, m))
			m = n;
		e = uk_memcpy(buf, p, m);
		STB(e, 0, 0);
	}
	return r;
}

/* x0 = chemin -> x0 = pid du programme lancé (< 0 : erreur). Un dossier
 * s'ouvre dans Fichiers, un paquet .app et un programme (ELF) se lancent, un
 * fichier s'ouvre dans son application (ui_assoc_app). */
long ui_assoc_open(long path)
{
	u8 st[16] __attribute__((aligned(16)));
	long sp = (long)(uintptr_t)st, r, fd, p;

	r = SYS2(SYS_STAT, path, sp);
	if (r < 0)
		return r;
	if (LDX(sp, 8) & ATTR_DIR) {
		/* un dossier : .app ? */
		if (!uk_fa_ext(path) || !uk_str_eq(UKA(fa_ext_b), S(uk_fa_s_app)))
			return uk_fa_spawn(S(uk_fa_s_files), path);
		STB(UKA(fa_path), 0, 0); /* son main */
		p = uk_fa_cat(UKA(fa_path), path);
		uk_fa_cat(p, S(uk_fa_s_main));
		return uk_fa_spawn(UKA(fa_path), 0);
	}
	fd = SYS2(SYS_OPEN, path, O_READ); /* un programme ? (en-tête ELF) */
	if (fd >= 0) {
		STW(sp, 0, 0);
		SYS3(SYS_READ, fd, sp, 4);
		SYS1(SYS_CLOSE, fd);
		if (LDW(sp, 0) == 0x464C457F) /* 7F 'E' 'L' 'F' */
			return uk_fa_spawn(path, 0);
	}
	uk_fa_find(path);
	return uk_fa_spawn(uk_fa_apppath(UKA(fa_name), S(uk_fa_s_main)), path);
}

/* x0 = extension (« docx », « .docx »), x1 = application (nom du paquet sans
 * .app ; 0 ou vide : retirer le choix) -> x0 = 1 si /etc/fileassoc.ini est
 * écrit. Les autres lignes restent telles quelles. */
long ui_assoc_set(long ext, long app)
{
	long b = UKA(fa_ext_b), i, c, n, nb, end, p, wr, len;
	uk_kv r;

	if (LDB(ext, 0) == '.') /* l'extension, en minuscules */
		ext++;
	i = 0;
	do {
		c = LDB(ext, i);
		if (!ULT(i, 16))
			return 0;
		if ((u32)(c - 'A') <= 25)
			c += 32;
		STB(b, i, c);
		i++;
	} while (c);
	if (ULE(i, 1))
		return 0;
	n = uk_fa_read(uk_conf_path("fileassoc.ini", 0)); /* (tout le texte : réécrit en entier, personnel) */
	nb = uk_alloc(n + 256); /* le nouveau texte (en-tête, ligne) */
	if (!nb)
		return 0;
	end = nb;
	if (!n) /* un nouveau fichier : son en-tête */
		end = uk_fa_cat(end, S(uk_fa_s_inihead));
	p = UKA(fa_buf);
	for (;;) {
		r = uk_fa_kv(p);
		if (!r.next)
			break;
		if (!uk_fa_eq(r.k0, r.k1, b)) /* la ligne de cette extension : retirée */
			end = uk_memcpy(end, p, r.next - p);
		p = r.next;
	}
	if (app && LDB(app, 0)) {
		if (end != nb && LDB(end - 1, 0) != 10) { /* (la dernière ligne finie) */
			STB(end, 0, 10);
			end++;
		}
		p = uk_fa_cat(end, b); /* « ext = app » */
		p = uk_fa_cat(p, S(uk_fa_s_eq));
		len = uk_strlen(app);
		if (!ULT(len, UK_FA_NAME - 1))
			len = UK_FA_NAME - 1;
		p = uk_memcpy(p, app, len);
		STB(p, 0, 10);
		end = p + 1;
	}
	i = SYS2(SYS_OPEN, uk_conf_path("fileassoc.ini", 1), O_WRITE | O_CREATE | O_TRUNC);
	if (i < 0) {
		uk_free(nb);
		return 0;
	}
	wr = SYS3(SYS_WRITE, i, nb, end - nb);
	SYS1(SYS_CLOSE, i);
	uk_free(nb);
	return wr == end - nb;
}

/* x0 = ligne d'arguments (0 : aucune) -> x0 = la même, son premier argument
 * sans guillemets (\" et \\ rendus) quand il en a ; sinon telle quelle */
long ui_arg(long line)
{
	long s, d, c, n;

	if (!line || LDB(line, 0) != '"')
		return line;
	s = line + 1;
	d = line;
	for (;;) {
		c = LDB(s, 0);
		s++;
		if (!c || c == '"')
			break;
		if (c == '\\') {
			n = LDB(s, 0);
			if (n == '"' || n == '\\') {
				c = n;
				s++;
			}
		}
		STB(d, 0, c);
		d++;
	}
	STB(d, 0, 0);
	return line;
}

/* ---------------------------------------------------------------------------
 *  Ouvrir avec
 * ------------------------------------------------------------------------- */

/* uk_fa_cb_all : fonction de uk_fa_scan : une entrée de plus dans uk_fa_tab
 * (nom, libellé, drapeau 1 : le manifeste ouvre l'extension) -> 0 */
static long uk_fa_cb_all(long name)
{
	long n = uk.fa_tn, e, v0, v1, len, d;

	(void)name;
	if (!ULT(n, UK_FA_MAX))
		return 0;
	uk.fa_tn = n + 1;
	e = uk.fa_tab + n * UK_FA_ESZ;
	uk_fa_cat(e, UKA(fa_name)); /* le nom */
	if (uk_fa_get(S(uk_fa_s_name), &v0, &v1) && (len = v1 - v0)) { /* le libellé (name = ...) */
		if (!ULT(len, UK_FA_LABEL - 1)) {
			len = UK_FA_LABEL - 1;
			/* (sans couper un caractère UTF-8 ; comme l'asm : pas de borne basse) */
			while ((LDB(v0, len) & 0xC0) == 0x80)
				len--;
		}
		d = uk_memcpy(e + UK_FA_NAME, v0, len);
		STB(d, 0, 0);
	} else {
		uk_fa_cat(e + UK_FA_NAME, UKA(fa_name));
	}
	/* l'Éditeur ouvre tout (drapeau laissé à 0 sinon : uk_alloc met à zéro) */
	if (!uk_str_eq(UKA(fa_name), S(uk_fa_s_editor))) {
		if (!LDB(UKA(fa_ext_b), 0))
			return 0;
		if (!uk_fa_cb_opens(0))
			return 0;
	}
	STB(e, UK_FA_FLAGS, 1);
	return 0;
}

/* uk_fa_icmp : x0, x1 = chaînes -> x0 < 0, 0, > 0 (casse ASCII ignorée) */
static long uk_fa_icmp(long a, long b)
{
	long c, d;

	for (;;) {
		c = LDB(a, 0);
		a++;
		d = LDB(b, 0);
		b++;
		if ((u32)(c - 'A') <= 25)
			c += 32;
		if ((u32)(d - 'A') <= 25)
			d += 32;
		if (c != d)
			return c - d;
		if (!c)
			return 0;
	}
}

/* uk_fa_rows : x0 = liste, x1 = drapeau voulu -> les entrées de uk_fa_tab qui
 * l'ont, par ordre de libellé ; la ligne de l'application par défaut dans
 * uk_fa_row */
static void uk_fa_rows(long list, long flag)
{
	long tab = uk.fa_tab, best, n, e, f, row;

	for (;;) {
		best = -1; /* la plus petite qui reste */
		n = uk.fa_tn;
		while (n) {
			n--;
			e = tab + n * UK_FA_ESZ;
			f = LDB(e, UK_FA_FLAGS);
			if (f & 2) /* déjà dans la liste */
				continue;
			if ((f & 1) != flag)
				continue;
			if (best >= 0 && uk_fa_icmp(e + UK_FA_NAME, tab + best * UK_FA_ESZ + UK_FA_NAME) >= 0)
				continue;
			best = n;
		}
		if (best < 0)
			return;
		e = tab + best * UK_FA_ESZ;
		STB(e, UK_FA_FLAGS, LDB(e, UK_FA_FLAGS) | 2);
		row = ui_list_add(list, e + UK_FA_NAME, best);
		if (uk_str_eq(e, UKA(fa_def))) /* l'application par défaut ? */
			uk.fa_row = row;
	}
}

/* uk_fa_section : x0 = dialogue, x1 = titre de section -> « #titre » traduit dans la liste */
static long uk_fa_section(long dlg, long title)
{
	long list = LDX(dlg, DG_LIST), t = ui_tr(title);

	STB(UKA(fa_arg), 0, '#');
	uk_fa_cat(UKA(fa_arg) + 1, t);
	return ui_list_add(list, UKA(fa_arg), -1);
}

/* uk_ow_fn : fonction du dialogue (x0 = dialogue, x1 = widget, x2 = code) : un
 * double-clic dans la liste ouvre -> x0 = dialogue (inchangé, comme l'asm) */
static long uk_ow_fn(long dlg, long w, long code)
{
	if (code != UN_ACTIVATE)
		return dlg;
	if (w != LDX(dlg, DG_LIST))
		return dlg;
	return ui_dialog_close(dlg, 1);
}

/* uk_fa_base : x0 = chemin -> x0 = son dernier composant */
static long uk_fa_base(long path)
{
	long p = path, c;

	for (;;) {
		c = LDB(p, 0);
		p++;
		if (!c)
			return path;
		if (c == '/' || c == ':')
			path = p;
	}
}

/* x0 = chemin d'un fichier -> x0 = pid du programme lancé (0 : Annuler, < 0 :
 * erreur). Le dialogue « Ouvrir avec » : les applications qui déclarent son
 * extension (et l'Éditeur) d'abord, puis les autres ; « Toujours » en fait le
 * choix par défaut (ui_assoc_set). */
long ui_open_with(long path)
{
	long res = 0, t, mt, dlg, list, cb, x, ctl, p;
	ukr r;

	uk_fa_find(path); /* l'application par défaut */
	uk_fa_cat(UKA(fa_def), UKA(fa_name));
	uk.fa_row = -1;
	t = uk_alloc(UK_FA_MAX * UK_FA_ESZ); /* les applications */
	if (!t)
		return 0;
	uk.fa_tab = t;
	uk.fa_tn = 0;
	uk_fa_scan((long)uk_fa_cb_all);
	mt = uk_met();
	/* hauteur : texte, nom, liste, case, boutons */
	dlg = ui_dialog_begin(UK_OW_W, LSL(LDX(mt, MT_CTL), 2) + UK_OW_LIST + 80, S(uk_fa_s_title));
	if (!dlg)
		goto nodlg;
	ui_label(UK_DLG_PAD, UK_DLG_PAD - 8, UK_OW_W - 2 * UK_DLG_PAD, LDX(mt, MT_CTL), S(uk_fa_s_choose), 0);
	t = uk_fa_base(path); /* le nom du fichier, en gras */
	ctl = LDX(mt, MT_CTL);
	ui_label(UK_DLG_PAD, ctl + UK_DLG_PAD - 8, UK_OW_W - 2 * UK_DLG_PAD, ctl, t, ULB_BOLD);
	ctl = LDX(mt, MT_CTL); /* la liste */
	list = ui_list(UK_DLG_PAD, ctl + ctl + UK_DLG_PAD, UK_OW_W - 2 * UK_DLG_PAD, UK_OW_LIST, UL_SIDEBAR);
	STX(dlg, DG_LIST, list);
	uk_fa_section(dlg, S(uk_fa_s_sugg));
	uk_fa_rows(list, 1);
	uk_fa_section(dlg, S(uk_fa_s_other));
	uk_fa_rows(list, 0);
	if (uk.fa_row >= 0) /* l'application par défaut choisie */
		ui_w_set_value(list, uk.fa_row);
	cb = 0; /* « Toujours » (avec une extension) */
	if (LDB(UKA(fa_ext_b), 0)) {
		p = uk_fa_cat(UKA(fa_arg), ui_tr(S(uk_fa_s_always)));
		p = uk_fa_cat(p, S(uk_fa_s_paren));
		p = uk_fa_cat(p, UKA(fa_ext_b));
		STB(p, 0, ')');
		STB(p, 1, 0);
		ctl = LDX(mt, MT_CTL);
		cb = ui_checkbox(UK_DLG_PAD, ctl + ctl + UK_DLG_PAD + UK_OW_LIST + 10, UK_OW_W - 2 * UK_DLG_PAD,
				 LDX(mt, MT_CTL), UKA(fa_arg), 0);
	}
	uk_dlg_bar(dlg);
	x = ui_dialog_button(dlg, 0, S(uk_s_bopen), 1, UB_DEFAULT, UK_OW_W - 16);
	ui_dialog_button(dlg, 1, S(uk_s_cancel), 0, 0, x);
	ui_w_focus(list);
	if (!ui_dialog_run(dlg, (long)uk_ow_fn))
		goto end;
	x = ui_w_get_value(list); /* l'application choisie */
	if (x < 0)
		goto end;
	r = ui_list_get(list, x);
	if (r.r1 < 0) /* (une section) */
		goto end;
	STB(UKA(fa_def), 0, 0); /* (son nom : la table part avec le dialogue) */
	uk_fa_cat(UKA(fa_def), uk.fa_tab + r.r1 * UK_FA_ESZ);
	res = 1;
	if (!cb)
		goto end;
	if (!ui_w_get_value(cb))
		goto end;
	ui_assoc_set(UKA(fa_ext_b), UKA(fa_def)); /* le choix par défaut */
end:
	ui_dialog_end(dlg);
nodlg:
	uk_free(uk.fa_tab);
	if (!res)
		return 0;
	/* ouvrir le fichier avec elle */
	return uk_fa_spawn(uk_fa_apppath(UKA(fa_def), S(uk_fa_s_main)), path);
}
