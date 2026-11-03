//
// disque.h -- l'etape Disque de Setup : la table de partitions de la carte (blk.sd0, src/inc/blk.inc), et trois gestes :
//
//   etendre    une partition, sur l'espace libre qui la suit (FAT32 : jusqu'a ce que sa FAT permet ; les images de
//              Korin OS ont une FAT taillee pour 128 Go, tools/mkimages.py)
//   creer      une partition FAT32 dans un espace libre (formatee ici), montee en SD<n>: par etc/mounts.txt
//   supprimer  une partition, sauf celle du systeme (blk.sd0p1, SD0:)
//
// Jusqu'a 4 partitions : toutes primaires. Au-dela, celle du systeme reste primaire (p1) et les autres deviennent
// logiques (p5, p6..., une table EBR devant chacune, dans une partition etendue) ; src/svc/inc/blkserv.inc les sert.
// Rien n'est ecrit avant « Continuer » : alors disque_ecrit, et l'appareil redemarre (le noyau ne lit la table qu'au
// demarrage) ; Setup reprend au Wi-Fi, avant les logiciels, qui vont donc sur la carte agrandie. Setup n'ecrit jamais
// dans une partition montee : une partition etendue, fatfs l'agrandit a son montage (fat_agrandir, fat32.asm).
//
enum { SYS_BUF_CREATE_ = 0x01, SYS_BUF_GRANT_ = 0x02, BLK_INFO_ = 1, BLK_ATTACHER_ = 2, BLK_LIRE_ = 3, BLK_ECRIRE_ = 4 };
static const u64 BUF_LOW_ = 1ul << 62, BUF_VA_BASE_ = 0x6800000000ul, BUF_SLOT_ = 0x4000000;
enum { DK_TAMPON = 256, DK_MO = 2048, DK_MIN = 64 * DK_MO, DK_PMAX = 13, DK_LOGMAX = 8, DK_SEGMAX = 2 * DK_PMAX + 1 };

// ---- les secteurs de la carte ----------------------------------------------------------------------------------------
static long s_blk = -1; static unsigned char *s_buf; static u64 s_secteurs;
static bool blk_ouvre ()
{
	if (s_blk >= 0) return true;
	long ch = sys_call (SYS_SVC_OPEN_, (long) "blk.sd0", 1000);
	if (ch < 0) return false;
	long m[64] = {}; m[0] = BLK_INFO_;
	if (sys_call (SYS_SVC_CALL_, ch, (long) m) < 0 || m[1]) return false;	// (lecture seule)
	s_secteurs = (u64) m[2];
	long n = sys_call (SYS_BUF_CREATE_, (long) (DK_TAMPON * 512 | BUF_LOW_), 0);
	if (n < 0) return false;
	sys_call (SYS_BUF_GRANT_, n, m[4]);
	memset (m, 0, sizeof m); m[0] = BLK_ATTACHER_; m[1] = n; m[2] = DK_TAMPON * 512;
	if (sys_call (SYS_SVC_CALL_, ch, (long) m) < 0) return false;
	s_buf = (unsigned char *) (BUF_VA_BASE_ + (u64) n * BUF_SLOT_);
	s_blk = ch;
	return true;
}
// data = 0 en ecriture : des zeros
static bool blk_rw (bool ecrire, u64 s, u64 n, void *data)
{
	unsigned char *p = (unsigned char *) data;
	if (ecrire && !p) memset (s_buf, 0, DK_TAMPON * 512);
	while (n)
	{
		u64 k = n < DK_TAMPON ? n : DK_TAMPON;
		if (ecrire && p) memcpy (s_buf, p, k * 512);
		long m[64] = {}; m[0] = ecrire ? BLK_ECRIRE_ : BLK_LIRE_; m[1] = (long) s; m[2] = (long) k; m[3] = (long) s_buf;
		if (sys_call (SYS_SVC_CALL_, s_blk, (long) m) != 0) return false;
		if (!ecrire) memcpy (p, s_buf, k * 512);
		s += k; n -= k; if (p) p += k * 512;
	}
	return true;
}
static inline unsigned le32 (const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned) p[3] << 24; }
static inline unsigned le16 (const unsigned char *p) { return p[0] | p[1] << 8; }
static inline void put32 (unsigned char *p, u64 v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24); }
static inline void put16 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }

// ---- la table ---------------------------------------------------------------------------------------------------------
struct Part
{
	u64 deb, n;		// secteurs
	u64 max;		// la taille jusqu'ou son systeme de fichiers peut grandir (FAT32 ; 0 : il ne grandit pas)
	unsigned char type;
	int num;		// blk.sd0p<num> au demarrage (0 : nouvelle)
	char vol[8];		// son volume (« SD1 »), d'apres etc/mounts.txt
};
static Part g_parts[DK_PMAX], g_orig[DK_PMAX]; static int g_np, g_norig;
static bool g_dk_lu, g_dk_ok; static const char *g_dk_err;
struct Seg { bool libre; u64 deb, n; int p; };
static Seg g_seg[DK_SEGMAX]; static int g_nseg, g_dsel = -1;

static bool est_fat32 (unsigned char t) { return t == 0x0B || t == 0x0C; }
static const char *type_nom (unsigned char t)
{
	static char b[16];
	if (est_fat32 (t)) return "FAT32";
	if (t == 0x07) return "exFAT";
	if (t == 0x83) return "Linux";
	snprintf (b, sizeof b, TR ("type %02X"), t);
	return b;
}
// FAT32 : la taille (secteurs) que permet sa FAT, d'apres son secteur de demarrage
static u64 fat32_max (u64 deb)
{
	unsigned char b[512];
	if (!blk_rw (false, deb, 1, b) || b[510] != 0x55 || b[511] != 0xAA || memcmp (b + 82, "FAT32", 5)) return 0;
	u64 spc = b[13], res = le16 (b + 14), nf = b[16], fsz = le32 (b + 36);
	if (!spc || !fsz || le16 (b + 11) != 512) return 0;
	return res + nf * fsz + (fsz * 128 - 2) * spc;
}
static void volumes_lit ()
{
	char *f = lire ("SD0:/etc/mounts.txt");
	for (char *l = f ? strtok (f, "\n") : 0; l; l = strtok (0, "\n"))
	{
		char v[8]; int n;
		if (l[0] == '#' || sscanf (l, " %7[A-Za-z0-9] = blk.sd0p%d", v, &n) != 2) continue;
		for (int i = 0; i < g_np; i++) if (g_parts[i].num == n) cpy (g_parts[i].vol, v, sizeof g_parts[i].vol);
	}
	free (f);
	for (int i = 0; i < g_np; i++) if (g_parts[i].num == 1) cpy (g_parts[i].vol, "SD0", sizeof g_parts[i].vol);
}
static void ajoute (u64 deb, u64 n, unsigned char t, int num)
{
	if (g_np >= DK_PMAX || !n) return;
	Part &p = g_parts[g_np++];
	memset (&p, 0, sizeof p);
	p.deb = deb; p.n = n; p.type = t; p.num = num;
	p.max = est_fat32 (t) ? fat32_max (deb) : 0;
}
static void segments ();
static bool table_lit ()
{
	g_dk_lu = true; g_np = 0;
	unsigned char s[512];
	if (!blk_ouvre () || !blk_rw (false, 0, 1, s)) { g_dk_err = "La carte ne se lit pas."; return false; }
	if (!s_secteurs || s_secteurs == ~0ul) { g_dk_err = "La taille de la carte est inconnue."; return false; }
	if (s[510] != 0x55 || s[511] != 0xAA || !memcmp (s + 82, "FAT32", 5)) { g_dk_err = "La carte n'a pas de table de partitions."; return false; }
	u64 ext = 0;
	for (int i = 0; i < 4; i++)
	{
		unsigned char *e = s + 446 + i * 16;
		if (!e[4]) continue;
		if (e[4] == 0x05 || e[4] == 0x0F) ext = le32 (e + 8);
		else ajoute (le32 (e + 8), le32 (e + 12), e[4], i + 1);
	}
	for (u64 ebr = ext, num = 5; ext && num < 5 + DK_LOGMAX; num++)
	{
		if (!blk_rw (false, ebr, 1, s) || s[510] != 0x55 || s[511] != 0xAA || !s[446 + 4]) break;
		ajoute (ebr + le32 (s + 446 + 8), le32 (s + 446 + 12), s[446 + 4], (int) num);
		if (!s[462 + 4]) break;
		u64 nx = ext + le32 (s + 462 + 8);
		if (nx <= ebr) break;
		ebr = nx;
	}
	for (int i = 1; i < g_np; i++)					// (dans l'ordre du disque)
		for (int j = i; j > 0 && g_parts[j].deb < g_parts[j - 1].deb; j--) { Part t = g_parts[j]; g_parts[j] = g_parts[j - 1]; g_parts[j - 1] = t; }
	bool sys = false;
	for (int i = 0; i < g_np; i++) if (g_parts[i].num == 1) sys = true;
	if (!sys) { g_dk_err = "La partition du système (la première) est introuvable."; return false; }
	volumes_lit ();
	memcpy (g_orig, g_parts, sizeof g_parts); g_norig = g_np;
	g_dk_ok = true;
	segments ();
	return true;
}
static bool dk_change ()
{
	if (g_np != g_norig) return true;
	for (int i = 0; i < g_np; i++) if (g_parts[i].deb != g_orig[i].deb || g_parts[i].n != g_orig[i].n || g_parts[i].num != g_orig[i].num) return true;
	return false;
}
// les partitions et les espaces libres (de plus de 1 Mo : en dessous, c'est l'alignement)
static void segments ()
{
	g_nseg = 0;
	u64 pos = 0;
	for (int i = 0; i <= g_np; i++)
	{
		u64 fin = i < g_np ? g_parts[i].deb : s_secteurs;
		if (fin > pos && fin - pos > DK_MO) { Seg &s = g_seg[g_nseg++]; s.libre = true; s.deb = pos; s.n = fin - pos; s.p = -1; }
		if (i == g_np) break;
		Seg &s = g_seg[g_nseg++]; s.libre = false; s.deb = g_parts[i].deb; s.n = g_parts[i].n; s.p = i;
		pos = g_parts[i].deb + g_parts[i].n;
	}
	if (g_dsel >= g_nseg) g_dsel = g_nseg - 1;
}
// ou peut finir ce qui commence dans l'espace libre qui suit la partition i (ou a « pos ») : 1 Mo avant la partition
// suivante, la place de sa table si elle devient logique ; sinon la fin de la carte
static u64 fin_libre (u64 pos)
{
	for (int i = 0; i < g_np; i++) if (g_parts[i].deb >= pos) return g_parts[i].deb > pos + DK_MO ? g_parts[i].deb - DK_MO : pos;
	return s_secteurs;
}

// ---- l'ecriture : la table (MBR, EBR), numeros nouveaux ----------------------------------------------------------------
static unsigned char s_mbr[512], s_ebr[DK_LOGMAX][512]; static u64 s_ebrs[DK_LOGMAX]; static int s_nebr;
static int s_num[DK_PMAX];				// le numero de chaque partition apres
static void entree (unsigned char *e, unsigned char t, u64 deb, u64 n)
{
	e[0] = 0; e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF; e[4] = t; e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
	put32 (e + 8, deb); put32 (e + 12, n);
}
// -> 0, ou pourquoi ce plan ne s'ecrit pas
static const char *plan ()
{
	memset (s_ebr, 0, sizeof s_ebr); s_nebr = 0;
	if (!blk_rw (false, 0, 1, s_mbr)) return "La carte ne se lit pas.";
	memset (s_mbr + 446, 0, 64);
	int sys = -1;
	for (int i = 0; i < g_np; i++) if (g_parts[i].num == 1) sys = i;
	entree (s_mbr + 446, g_parts[sys].type, g_parts[sys].deb, g_parts[sys].n);	// le systeme : p1, toujours
	s_num[sys] = 1;
	if (g_np <= 4)
	{
		for (int i = 0, k = 1; i < g_np; i++)
			if (i != sys) { entree (s_mbr + 446 + k * 16, g_parts[i].type, g_parts[i].deb, g_parts[i].n); s_num[i] = ++k; }
		return 0;
	}
	if (sys != 0) return "Les partitions logiques doivent suivre celle du système.";
	u64 fin = g_parts[0].deb + g_parts[0].n;
	for (int i = 1; i < g_np; i++)
	{
		u64 d = g_parts[i].deb;
		u64 e = d >= fin + DK_MO ? d - DK_MO : d - 1;			// la table devant elle, hors de la precedente
		if (d <= fin || e < fin) return "Pas de place pour la table d'une partition logique.";
		s_ebrs[s_nebr] = e; s_num[i] = 5 + s_nebr;
		unsigned char *t = s_ebr[s_nebr++];
		entree (t + 446, g_parts[i].type, d - e, g_parts[i].n);
		t[510] = 0x55; t[511] = 0xAA;
		fin = d + g_parts[i].n;
	}
	u64 ext = s_ebrs[0];
	for (int k = 0; k + 1 < s_nebr; k++)
	{
		const Part &nx = g_parts[k + 2];
		entree (s_ebr[k] + 462, 0x05, s_ebrs[k + 1] - ext, nx.deb + nx.n - s_ebrs[k + 1]);
	}
	entree (s_mbr + 446 + 16, 0x0F, ext, fin - ext);
	return 0;
}

// FAT32 neuve : deb, n secteurs
static bool mkfs32 (u64 deb, u64 n)
{
	u64 spc = n < 16ul << 21 ? 8 : n < 32ul << 21 ? 16 : n < 64ul << 21 ? 32 : 64, res = 32, fsz, cl;
	for (;;)
	{
		fsz = 1;
		for (;;) { cl = (n - res - 2 * fsz) / spc; u64 f = ((cl + 2) * 4 + 511) / 512; if (f == fsz) break; fsz = f; }
		if (cl >= 65525 || spc == 1) break;
		spc /= 2;
	}
	if (cl < 65525) return false;
	unsigned char bs[512] = {}, fi[512] = {}, fat[512] = {};
	bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90; memcpy (bs + 3, "KORINOS ", 8);
	put16 (bs + 11, 512); bs[13] = (unsigned char) spc; put16 (bs + 14, (unsigned) res); bs[16] = 2;
	bs[21] = 0xF8; put16 (bs + 24, 63); put16 (bs + 26, 255); put32 (bs + 28, deb); put32 (bs + 32, n);
	put32 (bs + 36, fsz); put32 (bs + 44, 2); put16 (bs + 48, 1); put16 (bs + 50, 6);
	bs[64] = 0x80; bs[66] = 0x29; put32 (bs + 67, (u64) clock_ms () ^ deb); memcpy (bs + 71, "KORIN DATA ", 11); memcpy (bs + 82, "FAT32   ", 8);
	bs[510] = 0x55; bs[511] = 0xAA;
	put32 (fi, 0x41615252); put32 (fi + 484, 0x61417272); put32 (fi + 488, cl - 1); put32 (fi + 492, 3);
	fi[510] = 0x55; fi[511] = 0xAA;
	put32 (fat, 0x0FFFFFF8); put32 (fat + 4, 0x0FFFFFFF); put32 (fat + 8, 0x0FFFFFFF);	// (2 : la racine)
	u64 data = res + 2 * fsz;
	if (!blk_rw (true, deb, res, 0) || !blk_rw (true, deb + res, 2 * fsz, 0) || !blk_rw (true, deb + data, spc, 0)) return false;
	unsigned char lab[512] = {};						// la racine : son etiquette
	memcpy (lab, "KORIN DATA ", 11); lab[11] = 0x08;
	return blk_rw (true, deb, 1, bs) && blk_rw (true, deb + 1, 1, fi) && blk_rw (true, deb + 6, 1, bs) && blk_rw (true, deb + 7, 1, fi)
	    && blk_rw (true, deb + res, 1, fat) && blk_rw (true, deb + res + fsz, 1, fat) && blk_rw (true, deb + data, 1, lab);
}
// etc/mounts.txt : les numeros qui changent, les partitions supprimees en commentaire, les nouvelles en SD<n>
static void montages_ecrit ()
{
	char *f = lire ("SD0:/etc/mounts.txt");
	FILE *o = fopen ("SD0:/etc/mounts.txt", "wb");
	if (!o) { free (f); return; }
	bool pris[10] = { true };						// (SD0 : le systeme)
	for (char *l = f ? strtok (f, "\n") : 0; l; l = strtok (0, "\n"))
	{
		char v[8], reste[96] = ""; int n;
		if (l[0] == '#' || sscanf (l, " %7[A-Za-z0-9] = blk.sd0p%d%95[^\n]", v, &n, reste) < 2) { fprintf (o, "%s\n", l); continue; }
		int nv = 0;
		for (int i = 0; i < g_np; i++) if (g_parts[i].num == n) nv = s_num[i];
		if (!nv) { fprintf (o, "# %s\n", l); continue; }			// (partition supprimee, ou absente)
		fprintf (o, "%s = blk.sd0p%d%s\n", v, nv, reste);
		if (v[0] == 'S' && v[1] == 'D' && v[2] >= '0' && v[2] <= '9' && !v[3]) pris[v[2] - '0'] = true;
	}
	for (int i = 0; i < g_np; i++)
	{
		if (g_parts[i].num) continue;
		int k = 1; while (k < 10 && pris[k]) k++;
		if (k == 10) break;
		pris[k] = true;
		fprintf (o, "# une partition creee par Setup\nSD%d = blk.sd0p%d, fatfs\n", k, s_num[i]);
	}
	fclose (o);
	free (f);
}
// en quittant l'etape -> 0, ou ce qui n'a pas pu s'ecrire
static const char *disque_ecrit ()
{
	if (!g_dk_ok || !dk_change ()) return 0;
	const char *e = plan ();
	if (e) return e;
	montages_ecrit ();
	for (int i = 0; i < g_np; i++)					// (etendue : fatfs l'agrandit au montage)
		if (!g_parts[i].num && !mkfs32 (g_parts[i].deb, g_parts[i].n)) return "Le formatage d'une nouvelle partition a échoué.";
	for (int k = 0; k < s_nebr; k++) if (!blk_rw (true, s_ebrs[k], 1, s_ebr[k])) return "La table des partitions logiques ne s'écrit pas.";
	if (!blk_rw (true, 0, 1, s_mbr)) return "La table des partitions ne s'écrit pas.";
	return 0;
}

// ---- la page ----------------------------------------------------------------------------------------------------------
static long g_dview, g_detend, g_dcree, g_dsuppr, g_dannule, g_dtaille, g_dmsg;
enum { DK_BARRE = 30, DK_LIGNE = 34 };
static void taille_txt (u64 secteurs, char *o, int cap)
{
	u64 mo = secteurs / DK_MO;
	if (mo >= 1024) snprintf (o, (size_t) cap, TR ("%lu,%lu Go"), mo / 1024, mo % 1024 * 10 / 1024);
	else snprintf (o, (size_t) cap, TR ("%lu Mo"), mo);
}
static unsigned seg_couleur (const Seg &s)
{
	if (s.libre) return jet (J_CREUX);
	const Part &p = g_parts[s.p];
	return p.num == 1 ? jet (J_ACCENT) : !p.num ? jet (J_SUCCES) : jet (J_ACCENT_CLAIR);
}
static void seg_nom (const Seg &s, char *o, int cap)
{
	if (s.libre) { cpy (o, TR ("Espace libre"), (size_t) cap); return; }
	const Part &p = g_parts[s.p];
	if (p.num == 1) snprintf (o, (size_t) cap, TR ("Korin OS (SD0:, %s)"), type_nom (p.type));
	else if (!p.num) snprintf (o, (size_t) cap, TR ("Nouvelle partition (%s)"), type_nom (p.type));
	else if (p.vol[0]) snprintf (o, (size_t) cap, TR ("Partition %d (%s:, %s)"), p.num, p.vol, type_nom (p.type));
	else snprintf (o, (size_t) cap, TR ("Partition %d (%s)"), p.num, type_nom (p.type));
}
// la liste defile : s_dtop, sa premiere ligne ; la ligne choisie reste visible
static int s_dtop, s_dh = 246;
static int dk_lignes (int h) { int n = (h - (10 + DK_BARRE + 12)) / DK_LIGNE; return n < 1 ? 1 : n; }
static void dk_borne (int h)
{
	s_dh = h;
	int n = dk_lignes (h);
	if (s_dtop > g_nseg - n) s_dtop = g_nseg - n;
	if (s_dtop < 0) s_dtop = 0;
}
static void dk_montre_sel ()
{
	int n = dk_lignes (s_dh);
	if (g_dsel >= 0 && g_dsel < s_dtop) s_dtop = g_dsel;
	if (g_dsel >= s_dtop + n) s_dtop = g_dsel - n + 1;
	dk_borne (s_dh);
}
static long dview_wheel (long, long steps) { s_dtop -= (int) steps; dk_borne (s_dh); ui_view_dirty (g_dview); return 0; }
static void dview_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_SURFACE));
	if (!g_dk_ok) { text (14, 14, TR (g_dk_err ? g_dk_err : "Lecture de la carte..."), jet (J_TEXTE2)); return; }
	int bw = (int) w - 24;							// la barre : la carte entiere
	rbox (12, 10, bw, DK_BARRE, 6, jet (J_LIGNE));
	for (int i = 0; i < g_nseg; i++)
	{
		const Seg &s = g_seg[i];
		int x0 = 12 + (int) (s.deb * (u64) bw / s_secteurs), x1 = 12 + (int) ((s.deb + s.n) * (u64) bw / s_secteurs);
		if (x1 - x0 < 3) x1 = x0 + 3;
		if (!s.libre) rbox (x0 + 1, 12, x1 - x0 - 2, DK_BARRE - 4, 4, seg_couleur (s));
		if (i == g_dsel) ui_fill (x0 + 1, 10 + DK_BARRE + 2, x1 - x0 - 2, 3, jet (J_ACCENT_FONCE));
	}
	dk_borne ((int) h);
	for (int i = s_dtop; i < g_nseg; i++)
	{
		int y = 10 + DK_BARRE + 12 + (i - s_dtop) * DK_LIGNE;
		if (y + DK_LIGNE > h) break;
		const Seg &s = g_seg[i];
		if (i == g_dsel) rbox (6, y, (int) w - 12, DK_LIGNE - 2, 6, jet (J_ELEVE));
		rbox (16, y + 10, 12, 12, 3, s.libre ? jet (J_LIGNE_FORTE) : seg_couleur (s));
		char t[96]; seg_nom (s, t, sizeof t);
		text (40, y + 8, t, jet (J_TEXTE1), 13, i == g_dsel, (int) w - 170);
		taille_txt (s.n, t, sizeof t);
		if (!s.libre)
		{
			const Part &p = g_parts[s.p];
			for (int j = 0; j < g_norig; j++)
				if (p.num && g_orig[j].num == p.num && g_orig[j].n != p.n)
				{ char a[24]; taille_txt (g_orig[j].n, a, sizeof a); char b[64]; snprintf (b, sizeof b, "%s -> %s", a, t); cpy (t, b, sizeof t); }
		}
		text ((int) w - 130, y + 9, t, jet (J_TEXTE2), 12, 0, 120);
	}
	int n = dk_lignes ((int) h);						// (plus de lignes : la barre)
	if (g_nseg > n)
	{
		int y0 = 10 + DK_BARRE + 12, ph = n * DK_LIGNE;
		rbox ((int) w - 5, y0 + s_dtop * ph / g_nseg, 3, n * ph / g_nseg, 1, jet (J_LIGNE_FORTE));
	}
}
static void disque_boutons ();
static long dview_mouse (long, long kind, long, long my, long info)
{
	if (kind != 1 || !(info >> 8 & 1) || !g_dk_ok) return 0;
	int i = ((int) my - (10 + DK_BARRE + 12)) / DK_LIGNE + s_dtop;
	if (my >= 10 + DK_BARRE + 12 && i >= 0 && i < g_nseg) { g_dsel = i; disque_boutons (); ui_view_dirty (g_dview); }
	return 0;
}
static void dk_dit (const char *m) { ui_w_set_text (g_dmsg, (long) TR (m)); ui_w_draw (g_dmsg); }
static void disque_boutons ()
{
	bool part = g_dsel >= 0 && g_dsel < g_nseg && !g_seg[g_dsel].libre, libre = g_dsel >= 0 && g_dsel < g_nseg && g_seg[g_dsel].libre;
	bool etend = false, suppr = false;
	if (part)
	{
		const Part &p = g_parts[g_seg[g_dsel].p];
		u64 fin = p.deb + p.n;
		etend = g_dsel + 1 < g_nseg && g_seg[g_dsel + 1].libre && p.max > p.n && fin_libre (fin) > fin;
		suppr = p.num != 1;
	}
	ui_w_enable (g_detend, etend); ui_w_enable (g_dsuppr, suppr);
	ui_w_enable (g_dcree, libre && g_np < 1 + DK_LOGMAX); ui_w_enable (g_dtaille, libre);
	ui_w_enable (g_dannule, dk_change ());
	ui_w_draw (g_detend); ui_w_draw (g_dsuppr); ui_w_draw (g_dcree); ui_w_draw (g_dtaille); ui_w_draw (g_dannule);
}
// apres un geste : le plan doit pouvoir s'ecrire, sinon il est defait
static void dk_apres (const Part *avant, int navant, const char *ok)
{
	const char *e = plan ();
	if (e) { memcpy (g_parts, avant, sizeof g_parts); g_np = navant; dk_dit (e); }
	else dk_dit (ok);
	segments (); dk_montre_sel (); disque_boutons (); ui_view_dirty (g_dview);
}
static void disque_etend ()
{
	if (g_dsel < 0 || g_seg[g_dsel].libre) return;
	Part avant[DK_PMAX]; memcpy (avant, g_parts, sizeof avant); int na = g_np;
	Part &p = g_parts[g_seg[g_dsel].p];
	u64 fin = fin_libre (p.deb + p.n);
	u64 n = fin - p.deb;
	if (n > p.max) n = p.max;
	if (n <= p.n) return;
	p.n = n;
	dk_apres (avant, na, n == p.max && fin - p.deb > p.max
		? "Étendue jusqu'à ce que permet son système de fichiers. Écrit en continuant."
		: "Étendue. Écrit en continuant, puis l'appareil redémarre.");
}
static void disque_cree ()
{
	if (g_dsel < 0 || !g_seg[g_dsel].libre) return;
	const Seg s = g_seg[g_dsel];
	u64 deb = (s.deb + 1 + DK_MO - 1) / DK_MO * DK_MO, fin = fin_libre (s.deb);
	if (fin > s.deb + s.n) fin = s.deb + s.n;
	if (fin <= deb || fin - deb < DK_MIN) { dk_dit ("Trop petit : 64 Mo au moins."); return; }
	u64 n = fin - deb;
	const char *t = (const char *) ui_w_get_text (g_dtaille);
	double go = t && *t ? atof (t) : 0;
	u64 voulu = (u64) (go * 1024.0 * 2048.0) / DK_MO * DK_MO;
	if (go > 0 && voulu < n) n = voulu;
	if (n < DK_MIN) { dk_dit ("Trop petit : 64 Mo au moins."); return; }
	Part avant[DK_PMAX]; memcpy (avant, g_parts, sizeof avant); int na = g_np;
	Part &p = g_parts[g_np++];
	memset (&p, 0, sizeof p);
	p.deb = deb; p.n = n; p.type = 0x0C;
	for (int i = g_np - 1; i > 0 && g_parts[i].deb < g_parts[i - 1].deb; i--) { Part x = g_parts[i]; g_parts[i] = g_parts[i - 1]; g_parts[i - 1] = x; }
	dk_apres (avant, na, g_np > 4 ? "Créée (FAT32) ; au-delà de 4, les partitions sont logiques. Écrit en continuant."
				       : "Créée (FAT32). Formatée en continuant, puis l'appareil redémarre.");
}
static void disque_supprime ()
{
	if (g_dsel < 0 || g_seg[g_dsel].libre) return;
	int i = g_seg[g_dsel].p;
	if (g_parts[i].num == 1) return;
	if (g_parts[i].num)
	{
		char m[200], nom[96]; seg_nom (g_seg[g_dsel], nom, sizeof nom);
		snprintf (m, sizeof m, TR ("Supprimer %s ? Ses fichiers seront perdus en continuant."), nom);
		if (ui_message ((long) TR ("Disque"), (long) m, UM_YESNO) != 1) return;
	}
	Part avant[DK_PMAX]; memcpy (avant, g_parts, sizeof avant); int na = g_np;
	for (int j = i; j + 1 < g_np; j++) g_parts[j] = g_parts[j + 1];
	g_np--;
	dk_apres (avant, na, "Supprimée en continuant, puis l'appareil redémarre.");
}
static void disque_annule ()
{
	memcpy (g_parts, g_orig, sizeof g_parts); g_np = g_norig;
	segments (); dk_montre_sel (); disque_boutons (); ui_view_dirty (g_dview);
	dk_dit ("Rien ne change sur la carte.");
}
static void disque_montre ()
{
	if (!g_dk_lu) { table_lit (); if (g_dk_ok && g_dsel < 0) g_dsel = 0; }
	disque_boutons ();
	ui_view_dirty (g_dview);
}
