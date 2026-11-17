/*
 * applets/remote.cpp -- le volet Accès à distance du panneau de configuration (docs/RESEAU.md) :
 *   * Services : quatre cases (lignes du cadre) -- Terminal à distance (telnetd), Bureau à distance (vncd), Fenêtres
 *     à distance (rdpd), Partage de fichiers (ftpd). Elles lisent et écrivent /etc/services.txt comme Setup : la
 *     ligne « /sys/svc/<nom> » lance le service au démarrage, la même précédée de « # » le coupe ; les autres
 *     lignes sont gardées. Effet au prochain démarrage.
 *   * Mot de passe : deux champs masqués (ap.extra, sous les cases ; clic ou Tab pour passer de l'un à l'autre).
 *     Appliquer écrit « password = ... » dans /etc/remote.txt (src/inc/remotepw.inc) ; vides : il ne change pas ;
 *     différents : un message. Effet tout de suite (relu à chaque connexion).
 *   * Un service coché sans mot de passe défini : un avertissement.
 *
 * Port en C++ de applets/remote.inc et de la lecture de src/inc/remotepw.inc (Korin ; Onyx n'avait pas ce volet).
 */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "../control.h"

enum
{
	RA_N = 4,				/* services */
	RA_LW = 130,				/* libellé d'un champ */
	RA_FW = 220,				/* un champ */
	RA_FH = 26,
	RA_RH = 34,				/* une ligne de champ */
	RA_COLW = 320,				/* les cases : une colonne large */
	RA_Y = AP_HEAD + 4 + (RA_N + 1) * ROW_H + 10,	/* haut du bloc du mot de passe */
	RP_MAX = 63,				/* (remotepw.inc) octets du mot de passe */
	RP_BUF = 1024,
};

static const char ra_s_common[] = "Mot de passe commun";
static const char ra_s_services[] = "Services";
static const char *const ra_labels[RA_N] = {
	"Terminal \xC3\xA0 distance (telnet)", "Bureau \xC3\xA0 distance (VNC)", "Fen\xC3\xAAtres \xC3\xA0 distance (RDP)",
	"Partage de fichiers (FTP)",
};
static const char *const ra_paths[RA_N] = {
	"/sys/svc/telnetd", "/sys/svc/vncd", "/sys/svc/rdpd", "/sys/svc/ftpd",
};
static const char *const ra_pw_labels[2] = { "Mot de passe", "Confirmer" };
static const char ra_s_set[] = "Un mot de passe est d\xC3\xA9" "fini. Champs vides : il ne change pas.";
static const char ra_s_none[] = "Aucun mot de passe d\xC3\xA9" "fini.";
static const char ra_s_warn[] = "Sans mot de passe, les services activ\xC3\xA9s refusent toute connexion.";
static const char ra_s_note1[] = "Services : effet au prochain d\xC3\xA9marrage.";
static const char ra_s_note2[] = "Mot de passe : effet imm\xC3\xA9" "diat (relu \xC3\xA0 chaque connexion).";
static const char ra_s_differ[] = "Les deux mots de passe sont diff\xC3\xA9rents.";
static const char ra_s_spaces[] = "Pas d'espace au d\xC3\xA9" "but ni \xC3\xA0 la fin du mot de passe.";
static const char ra_s_write[] = "Enregistrement impossible.";
static const char ra_s_ok[] = "Enregistr\xC3\xA9. Services : au prochain d\xC3\xA9marrage.";
static const char ra_s_okpw[] = "Mot de passe chang\xC3\xA9. Services : au prochain d\xC3\xA9marrage.";
#define RA_SERVICES	"/etc/services.txt"
#define RP_FILE		"/etc/remote.txt"

static long ra_len[2];				/* longueurs saisies (octets) */
static long ra_field;				/* champ choisi (0, 1) */
static long ra_pwok;				/* longueur du mot de passe de remote.txt */
static unsigned char ra_en[RA_N];		/* services au démarrage (lus) */
static char ra_pw[2][RP_MAX + 1];

/* ---- le mot de passe (src/inc/remotepw.inc) ---- */

/* -> longueur du mot de passe de /etc/remote.txt (0 : aucun) : une ligne « password = ... » (UTF-8, 63 octets au
 * plus, espaces de fin retirées) */
static long rp_read (void)
{
	static char buf[RP_BUF];
	char pw[RP_MAX + 1];
	int fd = open (RP_FILE, O_RDONLY);
	if (fd < 0)
		return 0;
	long n = read (fd, buf, RP_BUF - 1);
	close (fd);
	if (n <= 0)
		return 0;
	buf[n] = 0;
	const char *line = buf;
	while (*line)
	{
		const char *p = line;
		if (!strncmp (p, "password", 8))
		{
			p += 8;
			while (*p == ' ' || *p == '\t')	/* espaces, « = », espaces */
				p++;
			if (*p == '=')
			{
				p++;
				while (*p == ' ')
					p++;
				long l = 0;		/* la valeur, jusqu'à la fin de ligne */
				while (p[l] && p[l] != '\n' && p[l] != '\r' && l < RP_MAX)
				{
					pw[l] = p[l];
					l++;
				}
				while (l && pw[l - 1] == ' ')	/* (espaces de fin retirées) */
					l--;
				return l;
			}
		}
		while (*line && *line++ != '\n')	/* ligne suivante */
			;
	}
	return 0;
}

/* ---- /etc/services.txt ---- */

/* service -> 1 si sa case est cochée (lignes 1 à RA_N) */
static int ra_checked (long s) { return row_ptr (s + 1)->checked; }

/* p = début de ligne (après « # » et espaces) -> service (-1 : aucun) */
static long ra_match (const char *p)
{
	for (long s = 0; s < RA_N; s++)
	{
		size_t l = strlen (ra_paths[s]);
		if (strncmp (p, ra_paths[s], l))
			continue;
		char c = p[l];			/* puis fin de ligne ou espace */
		if (!c || c == '\n' || c == '\r' || c == ' ' || c == '\t')
			return s;
	}
	return -1;
}

/* le fichier dans ap_file (terminé par 0) -> octets lus */
static long ra_svc_load (void)
{
	long n = 0;
	int fd = open (RA_SERVICES, O_RDONLY);
	if (fd >= 0)
	{
		n = read (fd, ap_file, AP_FILE_MAX - 1);
		if (n < 0)
			n = 0;
		close (fd);
	}
	ap_file[n] = 0;
	return n;
}

/* ra_en : 1 pour chaque service lancé au démarrage (une ligne sans « # ») */
static void ra_svc_read (void)
{
	memset (ra_en, 0, sizeof ra_en);
	ra_svc_load ();
	const char *p = ap_file;
	while (*p)
	{
		const char *q = p;		/* « # » et espaces de tête */
		int commented = 0;
		while (*q == ' ' || *q == '#')
			if (*q++ == '#')
				commented = 1;
		long s = ra_match (q);
		if (s >= 0 && !commented)
			ra_en[s] = 1;
		while (*p && *p++ != '\n')	/* ligne suivante */
			;
	}
	static const char *const lists[2] = { "/etc/services.off", "/etc/services.manual" };
	for (int k = 0; k < 2; k++)		/* désactivé ou manuel dans le volet Démarrage : pas coché */
	{
		static char off[1024];
		int fd = open (lists[k], O_RDONLY);
		long n = fd >= 0 ? read (fd, off, sizeof off - 1) : 0;
		if (fd >= 0)
			close (fd);
		off[n > 0 ? n : 0] = 0;
		for (long s = 0; s < RA_N; s++)
			if (off_has (off, ra_paths[s]))
				ra_en[s] = 0;
	}
}

/* les cases -> /etc/services.txt (règle de Setup : « # » devant une ligne de service coupe, sans le « # »
 * lance ; un service coché absent : ajouté à la fin) -> 0 ou erreur */
static long ra_svc_write (void)
{
	unsigned char seen[RA_N] = { 0 };
	ra_svc_load ();
	const char *p = ap_file;		/* lecture */
	char *o = ap_out;			/* écriture */
	while (*p && o - ap_out < AP_FILE_MAX + 64)	/* (borne : ap_out a 128 octets de plus) */
	{
		const char *q = p;		/* après « # » et espaces */
		while (*q == ' ' || *q == '#')
			q++;
		long s = ra_match (q);
		if (s >= 0)
		{
			seen[s] = 1;
			if (!ra_checked (s))
				*o++ = '#';	/* coupe */
			p = q;			/* la ligne sans son ancien préfixe */
		}
		while (*p)			/* jusqu'au saut de ligne compris */
		{
			char c = *p++;
			*o++ = c;
			if (c == '\n')
				break;
		}
	}
	for (long s = 0; s < RA_N; s++)		/* les services cochés absents */
	{
		if (seen[s] || !ra_checked (s))
			continue;
		if (o != ap_out && o[-1] != '\n')	/* (ligne précédente terminée) */
			*o++ = '\n';
		o = stpcpy (o, ra_paths[s]);
		*o++ = '\n';
	}
	int fd = open (RA_SERVICES, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return fd;
	long r = write (fd, ap_out, (size_t) (o - ap_out));
	close (fd);
	return r >= 0 ? 0 : r;
}

/* ---- Appliquer ---- */

static void ra_apply (void)
{
	long l0 = ra_len[0], l1 = ra_len[1];
	int pw = 0;				/* 1 : mot de passe à écrire */
	if (l0 | l1)
	{
		if (l0 != l1 || memcmp (ra_pw[0], ra_pw[1], (size_t) l0))	/* les deux champs : égaux */
		{
			ap.msg = ra_s_differ;
			return;
		}
		if (ra_pw[0][0] == ' ' || ra_pw[0][l0 - 1] == ' ')	/* (remote.txt perd les espaces autour) */
		{
			ap.msg = ra_s_spaces;
			return;
		}
		pw = 1;
	}
	unsigned me;
	if (ap_elevate (TR ("Réglages veut modifier l'accès à distance."), &me))	/* (/etc : uid 0) */
	{
		ap.msg = TR ("Autorisation refusée.");
		return;
	}
	long werr = ra_svc_write ();
	for (long s = 0; !werr && s < RA_N; s++)	/* coché : plus coupé dans le volet Démarrage */
		if (ra_checked (s) && (off_set ("/etc/services.off", ra_paths[s], 0) < 0
				       || off_set ("/etc/services.manual", ra_paths[s], 0) < 0))
			werr = -1;
	if (!werr && pw)
	{
		char buf[128];				/* « password = ... » */
		strcpy (stpcpy (stpcpy (buf, "password = "), ra_pw[0]), "\n");
		werr = file_write (RP_FILE, buf);
	}
	ap_unelevate (me);
	if (werr)
	{
		ap.msg = ra_s_write;
		return;
	}
	if (!pw)
	{
		ap.msg = ra_s_ok;
		return;
	}
	ra_len[0] = ra_len[1] = 0;		/* champs vides, mot de passe relu */
	ra_pw[0][0] = ra_pw[1][0] = 0;
	ra_field = 0;
	ra_pwok = rp_read ();
	ap.msg = ra_s_okpw;
}

/* ---- le dessin (ap.extra : sous les cases, sur toute la largeur du volet) ---- */

/* x = bord gauche, i = champ, y = haut de sa ligne : le champ masqué, comme la clé du Wi-Fi (une puce par octet,
 * comme l'asm, le curseur dans le champ choisi) */
static void ra_field_draw (long x, long i, long y)
{
	int sel = ra_field == i;
	uint32_t c = tok (sel ? TJ_ACCENT : TJ_LINE_STRONG);	/* contour (accent : choisi) */
	ui_rbox (x + RA_LW, y, RA_FW, RA_FH, 5, c, c);
	ui_rbox (x + RA_LW + 1, y + 1, RA_FW - 2, RA_FH - 2, 4, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
	char buf[128], *o = buf;		/* puces (U+2022), puis le curseur */
	long n = ra_len[i] > 28 ? 28 : ra_len[i];
	while (n-- > 0)
		o = stpcpy (o, "\xE2\x80\xA2");
	if (sel)
		*o++ = '|';
	*o = 0;
	ui_text ((long) buf, x + RA_LW + 8, y + 18, tok (TJ_TEXT1), 13, F (), RA_FW - 12);
}

static void ra_draw (long, long, long)
{
	long x = SIDE_W + AP_MARGIN, y = RA_Y, w = ui.w - (SIDE_W + 2 * AP_MARGIN);
	ui_text ((long) TR (ra_s_common), x, y + 18, tok (TJ_TEXT2), 13, FB (), w);	/* titre (comme ceux des lignes) */
	y += ROW_H + 2;
	for (long i = 0; i < 2; i++)		/* les deux champs */
	{
		ui_text ((long) TR (ra_pw_labels[i]), x, y + 18, tok (TJ_TEXT1), 13, F (), RA_LW - 8);
		ra_field_draw (x, i, y);
		y += RA_RH;
	}
	y += 4;					/* mot de passe défini ou pas */
	ui_text ((long) TR (ra_pwok ? ra_s_set : ra_s_none), x, y + 14, tok (TJ_TEXT2), 12, F (), w);
	y += 20;
	if (!ra_pwok)				/* un service coché sans mot de passe */
		for (long s = 0; s < RA_N; s++)
			if (ra_checked (s))
			{
				ui_text ((long) TR (ra_s_warn), x, y + 14, tok (TJ_DANGER), 12, FB (), w);
				y += 20;
				break;
			}
	y += 6;
	ui_text ((long) TR (ra_s_note1), x, y + 14, tok (TJ_TEXT3), 12, F (), w);
	y += 18;
	ui_text ((long) TR (ra_s_note2), x, y + 14, tok (TJ_TEXT3), 12, F (), w);
}

/* ---- clics et clavier ---- */

/* (ap.xclick) -> 1 si sur un champ (il devient le choisi) */
static int ra_xclick (long x, long y)
{
	if (x < SIDE_W + AP_MARGIN + RA_LW || x >= SIDE_W + AP_MARGIN + RA_LW + RA_FW)
		return 0;
	y -= RA_Y + (long) ROW_H + 2;			/* haut du premier champ */
	if (y < 0)
		return 0;
	long k = y / RA_RH;
	if (k >= 2 || y - k * RA_RH >= RA_FH)
		return 0;
	ra_field = k;
	ap.msg = 0;
	return 1;
}

/* c = point de code -> octets UTF-8 dans out */
static long utf8_put (unsigned long c, char *out)
{
	if (c < 0x80)
	{
		out[0] = (char) c;
		return 1;
	}
	if (c < 0x800)
	{
		out[0] = (char) (0xC0 | c >> 6);
		out[1] = (char) (0x80 | (c & 63));
		return 2;
	}
	if (c < 0x10000)
	{
		out[0] = (char) (0xE0 | c >> 12);
		out[1] = (char) (0x80 | (c >> 6 & 63));
		out[2] = (char) (0x80 | (c & 63));
		return 3;
	}
	out[0] = (char) (0xF0 | (c >> 18 & 7));
	out[1] = (char) (0x80 | (c >> 12 & 63));
	out[2] = (char) (0x80 | (c >> 6 & 63));
	out[3] = (char) (0x80 | (c & 63));
	return 4;
}

/* (ap.key) -> 1 si pris (le champ choisi ; Tab : l'autre ; Entrée reste au cadre : Appliquer) */
static int ra_key (void)
{
	long f = ra_field;
	long code = ui_ev_field (&ui, EV_CODE);
	char *pw = ra_pw[f];
	if (code == KEY_TAB)
	{
		ra_field = f ^ 1;
		return 1;
	}
	if (code == KEY_BACKSPACE)		/* (dernier caractère UTF-8) */
	{
		long l = ra_len[f];
		if (!l)
			return 1;
		while (--l && ((unsigned char) pw[l] & 0xC0) == 0x80)
			;
		ra_len[f] = l;
		pw[l] = 0;
		ap.msg = 0;			/* (une saisie efface le message) */
		return 1;
	}
	unsigned long c = (unsigned long) (unsigned) ui_ev_field (&ui, EV_CHAR);	/* un caractère */
	if (c < 32 || c == 127)
		return 0;
	if (ra_len[f] >= RP_MAX)
		return 1;
	char tmp[8];				/* (octets ; trop long : refusé) */
	long n = utf8_put (c, tmp);
	if (ra_len[f] + n > RP_MAX)
		return 1;
	memcpy (pw + ra_len[f], tmp, (size_t) n);
	ra_len[f] += n;
	pw[ra_len[f]] = 0;
	ap.msg = 0;
	return 1;
}

/* ---- le volet ---- */

static void ra_open (void)
{
	ap.extra = ra_draw;
	ap.xclick = ra_xclick;
	ap.key = ra_key;
	ap.colw = RA_COLW;
	ra_len[0] = ra_len[1] = 0;		/* champs vides, le premier choisi */
	ra_field = 0;
	ra_pw[0][0] = ra_pw[1][0] = 0;
	ra_svc_read ();
	ra_pwok = rp_read ();
	applet_row (ra_s_services, R_TITLE);
	for (long s = 0; s < RA_N; s++)		/* les cases, dans l'ordre de ra_labels */
		applet_row (ra_labels[s], R_CHECK, 0, ra_en[s], s);
}

const Applet remote_applet = { "remote", "Acc\xC3\xA8s distant", "Acc\xC3\xA8s \xC3\xA0 distance", "terminal", ra_open, ra_apply };
