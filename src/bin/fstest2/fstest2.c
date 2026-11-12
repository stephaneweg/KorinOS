/* fstest2 - essai des services de fichiers en direct (docs/FICHIERS.md, étape 0),
 * comme fstest2.asm : sans le relais du noyau, par SVC_CALL au service du volume
 * (« fs.ram0 », « fs.img0 » s'il est monté) :
 *  * ops 14-16 : tampon partagé (FS_BUFFER, buf_map), écriture et relecture par le
 *    tampon de 1 octet, 4 Ko, 64 Ko, à cheval sur 64 Ko, fin de fichier, refus
 *    au-delà de 64 Ko, fermeture (appel) ;
 *  * propriétaire : un fils (« fstest2 enfant <numéro> ») essaie le fichier du
 *    père (E_BADF attendu), ouvre N_ENF fichiers et se termine sans les fermer ;
 *    le service doit les avoir fermés (avis de fin) : le père les rouvre tous ;
 *  * fatfs (« fs.sd0 », FAT32 ; « fs.sd1 », exFAT, s'il est monté), fichier
 *    tmp/fstest2.nd ouvert plusieurs fois : écritures vues des autres numéros,
 *    O_APPEND, O_TRUNC, unlink d'un fichier ouvert ;
 *  * registres à travers un appel de fichier (file_sys d'AppKit) : sur AArch64,
 *    x1-x15, x17, x18 et NZCV rendus comme par svc 0 (asm en ligne, appel par
 *    l'adresse de la table d'import comme le relais de kit.inc, qui ne touche que
 *    x16) ; sur x86-64, les registres que l'ABI garde (rbx, rbp, r12-r15).
 * Lignes « fstest2 : PASS ... » / « fstest2 : FAIL ... » ; code de sortie : le
 * nombre d'échecs. Comme l'asm : écrit SD0:/tmp/fstest2.nd et, si SD1: est monté,
 * SD1:/tmp/fstest2.nd (effacés ; le dossier tmp reste) ; « enfant » : tout argument
 * qui commence par « e ». */
#include "korinrt.h"

#ifndef FS_OPEN
#define FS_OPEN 1
#endif
#ifndef FS_STAT
#define FS_STAT 2
#endif
#ifndef FS_MKDIR
#define FS_MKDIR 3
#endif
#ifndef FS_UNLINK
#define FS_UNLINK 4
#endif
#ifndef FS_READ
#define FS_READ 10
#endif
#ifndef FS_WRITE
#define FS_WRITE 11
#endif
#ifndef FS_CLOSE
#define FS_CLOSE 12
#endif
#ifndef FS_BUFFER
#define FS_BUFFER 14
#endif
#ifndef FS_READ_BUF
#define FS_READ_BUF 15
#endif
#ifndef FS_WRITE_BUF
#define FS_WRITE_BUF 16
#endif

#define N_ENF 60      /* fichiers laissés ouverts par le fils */
#define T64 65536     /* FS_BUFFER_SIZE */
#define CHEV 64536    /* seconde écriture (à cheval sur 64 Ko) */

static const char s_ram0[] = "fs.ram0";
static const char p_nd[] = "tmp/fstest2.nd";
static const char p_moi[] = "SD0:/bin/fstest2";
static const char p_bin[] = "fstest2.bin";
static const char p_own[] = "fstest2.own";

static long echecs;
static long canal;
static const char *vol_cour;
static long nids;
static long ids[N_ENF];
static char libelle[128];
static char argument[64];
static u8 msg[MSG_SIZE] __attribute__((aligned(8)));

#define M(off) (*(long *)(msg + (off)))
#define MLEN (*(u32 *)(msg + MSG_LEN))

/* « fstest2 : PASS fs.ram0 : libellé -> valeur » */
static void verdict(int ok, const char *label, long val)
{
	if (!ok)
		echecs++;
	ak_puts(ok ? "fstest2 : PASS " : "fstest2 : FAIL ");
	ak_puts(vol_cour);
	ak_puts(" : ");
	ak_puts(label);
	ak_puts(" -> ");
	print_dec(val);
	ak_puts("\n");
}

/* libellé suivi du nombre d'octets (« FS_WRITE_BUF 4096 ») */
static void verdict_n(int ok, const char *label, long val, u64 n)
{
	fmt_dec(n, str_cat(libelle, label));
	verdict(ok, libelle, val);
}

/* op, a, b, c -> résultat (réponse dans msg ; d n'est pas touché, comme l'asm) */
static long appel(long op, long a, long b, long c)
{
	M(0) = op;
	M(MSG_A) = a;
	M(MSG_B) = b;
	M(MSG_C) = c;
	MLEN = 0;
	return korin_sys2(SYS_SVC_CALL, canal, (long)msg);
}

/* op, a, chemin (relatif au volume) -> résultat */
static long appel_chemin(long op, long a, const char *path)
{
	M(0) = op;
	M(MSG_A) = a;
	u32 i = 0;
	char c;
	do {
		c = path[i];
		msg[MSG_DATA + i++] = (u8)c;
	} while (c);
	MLEN = i;
	return korin_sys2(SYS_SVC_CALL, canal, (long)msg);
}

/* octet du motif : indice, graine */
static u8 motif(u64 i, u64 seed)
{
	u64 a = i >> 8, b = i >> 16;
	return (u8)(seed * 29 + i + a + (a << 1) + b + (b << 2));
}

static void remplir(u8 *buf, u64 n, u64 start, u64 seed)
{
	for (u64 i = 0; i < n; i++)
		buf[i] = motif(start + i, seed);
}

/* -> octets différents du motif */
static long verifier(const u8 *buf, u64 n, u64 start, u64 seed)
{
	long d = 0;
	for (u64 i = 0; i < n; i++)
		if (motif(start + i, seed) != buf[i])
			d++;
	return d;
}

static void brouiller(u8 *buf, u64 n)
{
	while (n)
		buf[--n] = 0xEE;
}

static void attendu_badf(long r, const char *label)
{
	verdict(r == E_BADF, label, r);
}

static void attendu(long r, const char *label, long exp)
{
	verdict(r == exp, label, r);
}

/* -> fichiers ouverts (p_own, lecture : N_ENF au plus, dans ids) */
static long ouvrir_n(void)
{
	long i = 0;
	while (i < N_ENF) {
		if (appel_chemin(FS_OPEN, O_READ, p_own) < 0)
			break;
		ids[i++] = M(MSG_B);
	}
	nids = i;
	return i;
}

static void fermer_n(void)
{
	for (long i = 0; i < nids; i++)
		appel(FS_CLOSE, ids[i], 0, 0);
}

/* ---- le fils : « enfant <numéro d'un fichier du père> » */
static NORETURN void enfant(const char *line)
{
	long f = (long)parse_dec(line + 7);
	vol_cour = "fs.ram0 (fils)";
	long c = korin_sys2(SYS_SVC_OPEN, (long)s_ram0, 3000);
	if (c >= 0) {
		canal = c;
		attendu_badf(appel(FS_READ, f, 0, 10), "FS_READ du fichier du pere : E_BADF");
		appel(FS_BUFFER, 0, 0, 0); /* (son tampon : rendu à sa fin) */
		korin_sys1(SYS_BUF_MAP, M(MSG_A));
		attendu_badf(appel(FS_READ_BUF, f, 0, 10), "FS_READ_BUF du fichier du pere : E_BADF");
		attendu_badf(appel(FS_WRITE_BUF, f, 0, 10), "FS_WRITE_BUF du fichier du pere : E_BADF");
		attendu_badf(appel(FS_CLOSE, f, 0, 0), "FS_CLOSE du fichier du pere : E_BADF");
		long n = ouvrir_n(); /* N_ENF fichiers, jamais fermés */
		verdict(n == N_ENF, "ouvertures laissees ouvertes", n);
	}
	_exit((int)echecs);
}

/* ---- un volume : ops 14-16 */

/* écrit au début par le tampon, relit, compare */
static void essai_taille(u64 n, u8 *buf, long file)
{
	remplir(buf, n, 0, n);
	long r = appel(FS_WRITE_BUF, file, 0, (long)n);
	u64 size = (u64)M(MSG_B);
	verdict_n(r == (long)n && size >= n, "FS_WRITE_BUF ", r, n);
	brouiller(buf, T64);
	r = appel(FS_READ_BUF, file, 0, (long)n);
	verdict_n(r == (long)n, "FS_READ_BUF ", r, n);
	long d = verifier(buf, n, 0, n);
	verdict_n(d == 0, "octets differents apres relecture de ", d, n);
}

/* 64 Ko (graine 1) à 0, 64 Ko (graine 2) à CHEV ; relecture de 64 Ko à 30000,
 * fin de fichier */
static void essai_chevauche(u8 *buf, long file)
{
	remplir(buf, T64, 0, 1);
	appel(FS_WRITE_BUF, file, 0, T64);
	remplir(buf, T64, 0, 2);
	long r = appel(FS_WRITE_BUF, file, CHEV, T64);
	long size = M(MSG_B);
	verdict(r == T64 && size == CHEV + T64,
		"FS_WRITE_BUF 64 Ko a 64536 (a cheval sur 64 Ko) : taille", size);
	brouiller(buf, T64);
	r = appel(FS_READ_BUF, file, 30000, T64);
	long d = verifier(buf, CHEV - 30000, 30000, 1);
	d += verifier(buf + (CHEV - 30000), T64 - (CHEV - 30000), 0, 2);
	verdict(r == T64 && d == 0,
		"FS_READ_BUF 64 Ko a 30000 (deux ecritures) : octets differents", d);
	r = appel(FS_READ_BUF, file, CHEV + T64 - 100, T64);
	verdict(r == 100, "FS_READ_BUF 64 Ko a 100 octets de la fin", r);
}

static void skip(void)
{
	ak_puts("fstest2 : SKIP ");
	ak_puts(vol_cour);
	ak_puts(" absent\n");
}

/* nom du service, 1 s'il doit exister */
static void essai_volume(const char *name, int must)
{
	vol_cour = name;
	long c = korin_sys2(SYS_SVC_OPEN, (long)name, 5000);
	if (c < 0) {
		if (must)
			verdict(0, "svc_open", c);
		else
			skip();
		return;
	}
	canal = c;
	long r = appel(FS_BUFFER, 0, 0, 0); /* 14 : le tampon partagé */
	long id = M(MSG_A);
	verdict(r == 0, "FS_BUFFER (numero)", id);
	if (r < 0)
		return;
	long map = korin_sys1(SYS_BUF_MAP, id); /* mappé ici */
	verdict(map > 0, "buf_map (adresse)", map);
	if (map < 0)
		return;
	u8 *buf = (u8 *)map;
	r = appel(FS_BUFFER, 0, 0, 0); /* une seconde fois : le même */
	verdict(r == 0 && M(MSG_A) == id, "FS_BUFFER une seconde fois : meme numero", M(MSG_A));
	r = appel_chemin(FS_OPEN, O_READ | O_WRITE | O_CREATE | O_TRUNC, p_bin);
	long file = M(MSG_B);
	verdict(r == 0, "FS_OPEN fstest2.bin (creation)", r);
	if (r < 0)
		return;
	essai_taille(1, buf, file);
	essai_taille(4096, buf, file);
	essai_taille(T64, buf, file);
	essai_chevauche(buf, file);
	r = appel(FS_READ_BUF, file, 0, T64 + 1); /* plus que le tampon : refusé */
	verdict(r < 0, "FS_READ_BUF de 64 Ko + 1 refuse", r);
	r = appel(FS_CLOSE, file, 0, 0); /* 12 : appel du propriétaire */
	verdict(r == 0, "FS_CLOSE (appel du proprietaire)", r);
	r = appel(FS_READ_BUF, file, 0, 1); /* fermé : plus de lecture */
	verdict(r < 0, "FS_READ_BUF apres fermeture refuse", r);
	r = appel_chemin(FS_UNLINK, 0, p_bin);
	verdict(r == 0, "FS_UNLINK fstest2.bin", r);
}

/* ---- fatfs : un fichier ouvert plusieurs fois (nœud partagé) */

/* drapeaux -> numéro de tmp/fstest2.nd (résultat < 0 sinon) ; *size : la taille rendue */
static long ouvrir_nd(long flags, long *size)
{
	long r = appel_chemin(FS_OPEN, flags, p_nd);
	if (r >= 0)
		r = M(MSG_B);
	*size = M(MSG_D);
	return r;
}

/* numéro, position, octets (<= 464), graine, indice du motif -> résultat de
 * FS_WRITE ; *size : la nouvelle taille */
static long ecrire_m(long file, long pos, long n, long seed, long idx, long *size)
{
	remplir(msg + MSG_DATA, (u64)n, (u64)idx, (u64)seed);
	M(0) = FS_WRITE;
	M(MSG_A) = file;
	M(MSG_B) = pos;
	M(MSG_C) = n;
	MLEN = (u32)n;
	long r = korin_sys2(SYS_SVC_CALL, canal, (long)msg);
	*size = M(MSG_B);
	return r;
}

/* numéro, position, octets, graine -> octets lus ; *diff : octets différents */
static long lire_m(long file, long pos, long n, long seed, long *diff)
{
	long r = appel(FS_READ, file, pos, n);
	*diff = 0;
	if (r > 0)
		*diff = verifier(msg + MSG_DATA, (u64)r, (u64)pos, (u64)seed);
	return r;
}

static void lu_bien(long r, long diff, long exp, const char *label)
{
	verdict(r == exp && diff == 0, label, r);
}

static void essai_noeuds(const char *name, int must)
{
	long size, diff, r;
	vol_cour = name;
	long c = korin_sys2(SYS_SVC_OPEN, (long)name, 5000);
	if (c < 0) {
		if (must)
			verdict(0, "svc_open", c);
		else
			skip();
		return;
	}
	canal = c;
	appel_chemin(FS_MKDIR, 0, "tmp"); /* (déjà là : E_EXIST) */
	long a = ouvrir_nd(O_READ | O_WRITE | O_CREATE | O_TRUNC, &size);
	verdict(a >= 0, "FS_OPEN tmp/fstest2.nd (A, creation)", a);
	if (a < 0)
		return;
	long b = ouvrir_nd(O_READ | O_WRITE, &size);
	verdict(b >= 0, "FS_OPEN du meme fichier (B)", b);
	if (b < 0)
		return;
	attendu(ecrire_m(a, 0, 300, 5, 0, &size), "A : FS_WRITE 300 o", 300);
	r = lire_m(b, 0, 300, 5, &diff);
	lu_bien(r, diff, 300, "B : FS_READ des 300 o ecrits par A");
	appel_chemin(FS_STAT, 0, p_nd); /* stat avant toute fermeture */
	attendu(M(MSG_B), "FS_STAT avant fermeture : taille 300", 300);
	long cc = ouvrir_nd(O_WRITE | O_APPEND, &size);
	attendu(size, "FS_OPEN O_APPEND (C) : taille 300", 300);
	if (cc < 0)
		return;
	ecrire_m(cc, 0, 50, 7, 300, &size);
	attendu(size, "C : FS_WRITE 50 o en 0 avec O_APPEND : taille 350", 350);
	r = lire_m(a, 300, 50, 7, &diff);
	lu_bien(r, diff, 50, "A : FS_READ des 50 o ajoutes a la fin par C");
	r = lire_m(a, 0, 300, 5, &diff);
	lu_bien(r, diff, 300, "A : FS_READ des 300 premiers octets (intacts)");
	long d = ouvrir_nd(O_WRITE | O_TRUNC, &size);
	attendu(size, "FS_OPEN O_TRUNC (D) : taille 0", 0);
	if (d < 0)
		return;
	attendu(appel(FS_READ, b, 0, 100), "B : FS_READ apres O_TRUNC de D : 0 octet", 0);
	ecrire_m(a, 0, 200, 9, 0, &size);
	attendu(size, "A : FS_WRITE 200 o : taille 200", 200);
	attendu(appel_chemin(FS_UNLINK, 0, p_nd), "FS_UNLINK du fichier ouvert", 0);
	attendu(appel_chemin(FS_STAT, 0, p_nd), "FS_STAT apres unlink : E_NOENT", E_NOENT);
	r = lire_m(b, 0, 200, 9, &diff);
	lu_bien(r, diff, 200, "B : FS_READ apres unlink : encore lisible");
	long e = ouvrir_nd(O_READ | O_WRITE | O_CREATE | O_TRUNC, &size);
	attendu(size, "FS_OPEN creation du meme nom (E) : taille 0", 0);
	if (e < 0)
		return;
	attendu(ecrire_m(e, 0, 20, 11, 0, &size), "E : FS_WRITE 20 o", 20);
	r = lire_m(b, 0, 200, 9, &diff);
	lu_bien(r, diff, 200, "B : FS_READ de l ancien fichier (inchange par E)");
	appel(FS_CLOSE, a, 0, 0); /* fermetures */
	appel(FS_CLOSE, b, 0, 0);
	appel(FS_CLOSE, cc, 0, 0);
	appel(FS_CLOSE, d, 0, 0);
	appel(FS_CLOSE, e, 0, 0);
	appel_chemin(FS_STAT, 0, p_nd); /* le nouveau : 20 o */
	attendu(M(MSG_B), "FS_STAT apres les fermetures : 20 (E)", 20);
	attendu(appel_chemin(FS_UNLINK, 0, p_nd), "FS_UNLINK du nouveau fichier", 0);
}

/* ---- fin d'un process : ses fichiers fermés par le service */
static void essai_fins(void)
{
	vol_cour = s_ram0;
	long c = korin_sys2(SYS_SVC_OPEN, (long)s_ram0, 3000);
	if (c < 0)
		return;
	canal = c;
	if (appel_chemin(FS_OPEN, O_READ | O_WRITE | O_CREATE | O_TRUNC, p_own) < 0)
		return;
	long f = M(MSG_B);
	appel(FS_WRITE, f, 0, 10); /* 10 octets (message) */
	fmt_dec((u64)f, str_cat(argument, "enfant ")); /* « enfant <numéro> » */
	long pid = ksys(SYS_SPAWN, (long)p_moi, (long)argument, 0, 0, 0, 0);
	verdict(pid > 0, "spawn du fils", pid);
	if (pid >= 0) {
		long code = korin_sys1(SYS_WAIT, pid);
		verdict(code == 0, "fils : code de sortie (ses echecs)", code);
		vol_cour = s_ram0;
		long r = appel(FS_READ, f, 0, 10); /* le sien : toujours ouvert */
		verdict(r == 10, "fichier du pere encore ouvert apres le fils (FS_READ)", r);
		long n;
		for (long tries = 0;;) { /* ceux du fils : fermés ? (l'avis de fin suit wait de peu) */
			n = ouvrir_n();
			fermer_n();
			if (n == N_ENF)
				break;
			if (++tries >= 30)
				break;
			sys_sleep(100);
		}
		verdict(n == N_ENF, "fichiers laisses ouverts par le fils fermes a sa fin (rouverts)", n);
	}
	appel(FS_CLOSE, f, 0, 0);
	appel_chemin(FS_UNLINK, 0, p_own);
}

/* ---- registres à travers un appel de fichier */

#if defined(__aarch64__)
/* l'adresse de file_sys dans la table d'import d'AppKit, lue dans le relais de
 * build_c.sh (adrp x16 ; add x16 ; ldr x17, [x16, #n]) ; 0 : pas encore ouverte */
static void *file_sys_target(void)
{
	const u32 *p = (const u32 *)(void *)file_sys;
	u32 i0 = p[0], i1 = p[1], i2 = p[2];
	long imm = (long)(((i0 >> 29) & 3) | (((i0 >> 5) & 0x7FFFF) << 2));
	imm = (imm << 43) >> 43; /* 21 bits signés */
	u64 base = ((u64)(uintptr_t)p & ~(u64)0xFFF) + ((u64)imm << 12);
	base += (i1 >> 10) & 0xFFF;
	base += ((i2 >> 10) & 0xFFF) * 8;
	return *(void **)(uintptr_t)base;
}

/* appel nr (x0, x1, x2) avec x3-x7, x9-x15, x17, x18 = 0x100 + n et N = 1, Z = C =
 * V = 0 (cmp x3, x4) ; *ret : x0 rendu -> écarts (drapeaux, registres, x1, x2, x8),
 * comme regs_poser et regs_compter */
static long regs_call(long nr, long a0, long a1, long a2, long *ret)
{
	void *fn = file_sys_target();
	if (!fn) { /* (la table s'ouvre au premier appel) */
		ksys(SYS_GETCWD, (long)libelle, 0, 0, 0, 0, 0);
		fn = file_sys_target();
	}
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;
	register long x2 __asm__("x2") = a2;
	register long x8 __asm__("x8") = nr;
	register long x16 __asm__("x16") = (long)fn;
	register long e1 __asm__("x19") = a1;
	register long e2 __asm__("x20") = a2;
	register long e8 __asm__("x21") = nr;
	__asm__ volatile(
		"str x18, [sp, #-16]!\n"
		"mov x3, #0x103\n mov x4, #0x104\n mov x5, #0x105\n mov x6, #0x106\n"
		"mov x7, #0x107\n mov x9, #0x109\n mov x10, #0x10a\n mov x11, #0x10b\n"
		"mov x12, #0x10c\n mov x13, #0x10d\n mov x14, #0x10e\n mov x15, #0x10f\n"
		"mov x17, #0x111\n mov x18, #0x112\n"
		"cmp x3, x4\n" /* N = 1, Z = 0, C = 0, V = 0 */
		"blr x16\n"    /* (le relais de kit.inc : br x16, rien d'autre) */
		"mov x16, #0\n"
		"b.pl 1f\n b.eq 1f\n b.hs 1f\n b.vs 1f\n b 2f\n"
		"1: add x16, x16, #1\n"
		"2:\n"
		"cmp x3, #0x103\n cinc x16, x16, ne\n cmp x4, #0x104\n cinc x16, x16, ne\n"
		"cmp x5, #0x105\n cinc x16, x16, ne\n cmp x6, #0x106\n cinc x16, x16, ne\n"
		"cmp x7, #0x107\n cinc x16, x16, ne\n cmp x9, #0x109\n cinc x16, x16, ne\n"
		"cmp x10, #0x10a\n cinc x16, x16, ne\n cmp x11, #0x10b\n cinc x16, x16, ne\n"
		"cmp x12, #0x10c\n cinc x16, x16, ne\n cmp x13, #0x10d\n cinc x16, x16, ne\n"
		"cmp x14, #0x10e\n cinc x16, x16, ne\n cmp x15, #0x10f\n cinc x16, x16, ne\n"
		"cmp x17, #0x111\n cinc x16, x16, ne\n cmp x18, #0x112\n cinc x16, x16, ne\n"
		"cmp x1, x19\n cinc x16, x16, ne\n cmp x2, x20\n cinc x16, x16, ne\n"
		"cmp x8, x21\n cinc x16, x16, ne\n"
		"ldr x18, [sp], #16\n"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x8), "+r"(x16)
		: "r"(e1), "r"(e2), "r"(e8)
		: "x3", "x4", "x5", "x6", "x7", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
		  "x17", "x30", "memory", "cc");
	*ret = x0;
	return x16;
}
#else
/* x86-64 : file_sys est une fonction de l'ABI System V (numéro en 7e argument,
 * sur la pile) ; seuls rbx, rbp, r12-r15 doivent revenir (les drapeaux et les
 * registres d'arguments ne sont pas gardés : pas d'équivalent) -> écarts */
static u64 regs_out[6]; /* (adresses fixes : pas relatives à rsp, que l'asm change) */
static long regs_call(long nr, long a0, long a1, long a2, long *ret)
{
	long rax;
	__asm__ volatile(
		"mov %[nr], %%r11\n" /* (avant de toucher à rsp et aux registres gardés) */
		"mov %%rsp, %%rax\n"
		"and $-16, %%rsp\n"
		"push %%rax\n push %%rax\n"
		"push %%rbx\n push %%rbp\n push %%r12\n push %%r13\n push %%r14\n push %%r15\n"
		"mov $0x103, %%rbx\n mov $0x105, %%rbp\n mov $0x10c, %%r12\n"
		"mov $0x10d, %%r13\n mov $0x10e, %%r14\n mov $0x10f, %%r15\n"
		"sub $8, %%rsp\n push %%r11\n" /* 7e argument : le numéro */
		"xor %%ecx, %%ecx\n xor %%r8d, %%r8d\n xor %%r9d, %%r9d\n"
		"call file_sys\n"
		"add $16, %%rsp\n"
		"mov %%rbx, regs_out(%%rip)\n mov %%rbp, regs_out+8(%%rip)\n"
		"mov %%r12, regs_out+16(%%rip)\n mov %%r13, regs_out+24(%%rip)\n"
		"mov %%r14, regs_out+32(%%rip)\n mov %%r15, regs_out+40(%%rip)\n"
		"pop %%r15\n pop %%r14\n pop %%r13\n pop %%r12\n pop %%rbp\n pop %%rbx\n"
		"pop %%rsp\n"
		: "=a"(rax), "+D"(a0), "+S"(a1), "+d"(a2), [o0] "=m"(regs_out[0]), [o1] "=m"(regs_out[1]),
		  [o2] "=m"(regs_out[2]), [o3] "=m"(regs_out[3]), [o4] "=m"(regs_out[4]), [o5] "=m"(regs_out[5])
		: [nr] "r"(nr)
		: "rcx", "r8", "r9", "r10", "r11", "memory", "cc");
	static const u64 want[6] = {0x103, 0x105, 0x10c, 0x10d, 0x10e, 0x10f};
	long d = 0;
	for (int i = 0; i < 6; i++)
		d += regs_out[i] != want[i];
	*ret = rax;
	return d;
}
#endif

static void essai_registres(void)
{
	static u8 st[8192];
	long r;
	vol_cour = "file_sys";
	long d = regs_call(SYS_STAT, (long)p_moi, (long)st, 0x1002, &r); /* stat */
	verdict(d == 0, "stat : x1-x15, x17, x18, NZCV gardes (ecarts)", d);
	long fd = ksys(SYS_OPEN, (long)p_moi, O_READ, 0, 0, 0, 0); /* read de 4098 octets */
	d = regs_call(SYS_READ, fd, (long)st, 0x1002, &r);
	d += r != 0x1002;
	verdict(d == 0, "read 4098 o : x1-x15, x17, x18, NZCV gardes (ecarts)", d);
	ksys(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (krt_line && krt_line[0] == 'e')
		enfant(krt_line);
	sys_sleep(2000); /* (lancé au démarrage : le temps que vfs monte RAM0: et IMG0:) */
	essai_volume(s_ram0, 1);
	essai_volume("fs.img0", 0);
	essai_noeuds("fs.sd0", 1);
	essai_noeuds("fs.sd1", 0);
	essai_fins();
	essai_registres();
	ak_puts("fstest2 : termine, echecs : ");
	print_dec(echecs);
	ak_puts("\n");
	_exit((int)echecs);
}
