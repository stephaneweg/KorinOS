/* vfstest - essai des chemins et dossiers courants de vfs (docs/FICHIERS.md, étape 1),
 * comme vfstest.asm : ops 30-36 par SVC_CALL au service « vfs », sans le noyau :
 *  * relatif, « . », « .. », « SD: » (volume 0), « sd0:a » ;
 *  * « /a » : racine du volume courant (dossier /tmp/vt, « / » -> /tmp/) ;
 *  * chdir / getcwd, E_NOTDIR, ouverture pour l'appelant (écriture et fermeture
 *    directes chez fs.ram0 : il en est le propriétaire), exec (nom seul, paquet
 *    .app, ATTR_SYS sous /sys/) ;
 *  * un fils lancé après un chdir hérite du dossier du père (il écrit son getcwd) ;
 *    il change de dossier puis se termine ; un second fils reçoit le dossier du
 *    père et non celui du premier.
 * Lignes « vfstest : PASS ... » / « vfstest : FAIL ... » ; code de sortie : le
 * nombre d'échecs. Écrit seulement dans /tmp/vt (effacé). Comme l'asm : le
 * dossier de départ attendu est / ; « enfant » : tout argument qui commence
 * par « e ». */
#include "korinrt.h"

#ifndef V_OPEN
#define V_OPEN 30
#endif
#ifndef V_MKDIR
#define V_MKDIR 32
#endif
#ifndef V_UNLINK
#define V_UNLINK 33
#endif
#ifndef V_CHDIR
#define V_CHDIR 34
#endif
#ifndef V_GETCWD
#define V_GETCWD 35
#endif
#ifndef V_EXEC
#define V_EXEC 36
#endif
#ifndef FS_WRITE
#define FS_WRITE 11
#endif
#ifndef FS_CLOSE
#define FS_CLOSE 12
#endif
#ifndef ATTR_SYS
#define ATTR_SYS 0x100
#endif

static const char s_vfs[] = "vfs";
static const char p_moi[] = "/bin/vfstest";
static const char p_vt[] = "/tmp/vt";
static const char p_sous[] = "sous";
static const char p_sous_abs[] = "/tmp/vt/sous";
static const char p_f[] = "f.txt";
static const char p_f_abs[] = "/tmp/vt/f.txt";
static const char p_racine[] = "/";
static const char p_sd[] = "SD:bin";
static const char c_sd0[] = "/";
static const char c_vt[] = "/tmp/vt";
static const char l_chdir_racine[] = "V_CHDIR /";

static long canal;
static long echecs;
static u8 msg[MSG_SIZE] __attribute__((aligned(8)));

#define M(off) (*(long *)(msg + (off)))
#define MLEN (*(u32 *)(msg + MSG_LEN))
#define MDATA ((char *)msg + MSG_DATA)

/* op, chemin (données) -> résultat (réponse dans msg) ; vop_a garde le champ a */
static long vop_a(long op, const char *path)
{
	M(0) = op;
	M(MSG_B) = 0;
	M(MSG_C) = 0;
	M(MSG_D) = 0;
	u32 i = 0;
	char c;
	do {
		c = path[i];
		msg[MSG_DATA + i++] = (u8)c;
	} while (c);
	MLEN = i;
	return korin_sys2(SYS_SVC_CALL, canal, (long)msg);
}

static long vop(long op, const char *path)
{
	M(MSG_A) = 0;
	return vop_a(op, path);
}

/* « vfstest : PASS libellé -> valeur » */
static void verdict(int ok, const char *label, long val)
{
	if (!ok)
		echecs++;
	ak_puts(ok ? "vfstest : PASS " : "vfstest : FAIL ");
	ak_puts(label);
	ak_puts(" -> ");
	print_dec(val);
	ak_puts("\n");
}

static void attendu(long op, const char *path, const char *label, long exp)
{
	long r = vop(op, path);
	verdict(r == exp, label, r);
}

static void cwd_vaut(const char *label, const char *want)
{
	int ok = 0;
	if (vop(V_GETCWD, "") >= 0)
		ok = str_eq(MDATA, want) != 0;
	verdict(ok, label, 0);
	ak_puts("          getcwd : "); /* (le dossier rendu) */
	ak_puts(MDATA);
	ak_puts("\n");
}

/* ---- le fils : « enfant <dossier attendu> » : écrit son getcwd, puis va dans
 * /tmp/vt/sous ; code 0 si le dossier est celui attendu */
static NORETURN void enfant(const char *line)
{
	long r = vop(V_GETCWD, "");
	ak_puts("vfstest : fils, dossier courant : ");
	ak_puts(MDATA);
	ak_puts("\n");
	long code = 1;
	if (r >= 0) {
		code = str_eq(MDATA, line + 7) ^ 1;
		vop(V_CHDIR, p_sous_abs); /* un dossier à lui avant de finir */
	}
	_exit((int)code);
}

/* ouverture pour l'appelant (op 30), relatif : écriture et fermeture directes chez
 * le service du volume (propriétaire = ce pid) */
static void essai_fichier(void)
{
	M(MSG_A) = O_READ | O_WRITE | O_CREATE | O_TRUNC;
	long r = vop_a(V_OPEN, p_f);
	verdict(r >= 0, "V_OPEN f.txt (relatif, creation)", r);
	long ch = M(MSG_A), num = M(MSG_B);
	verdict(!(M(MSG_C) & ATTR_SYS), "V_OPEN /tmp sans ATTR_SYS", 0);
	M(0) = FS_WRITE; /* 5 octets, directement */
	M(MSG_A) = num;
	M(MSG_B) = 0;
	M(MSG_C) = 5;
	MLEN = 5;
	for (int i = 0; i < 5; i++)
		msg[MSG_DATA + i] = (u8)"salut"[i];
	r = korin_sys2(SYS_SVC_CALL, ch, (long)msg);
	verdict(r == 5, "FS_WRITE direct (proprietaire = ce pid)", r);
	M(0) = FS_CLOSE; /* fermeture (appel du propriétaire) */
	M(MSG_A) = num;
	MLEN = 0;
	r = korin_sys2(SYS_SVC_CALL, ch, (long)msg);
	verdict(r == 0, "FS_CLOSE direct (appel du proprietaire)", r);
	r = vop(V_STAT, "sous/../f.txt");
	verdict(r == 0 && M(MSG_B) == 5, "V_STAT sous/../f.txt : taille 5", M(MSG_B));
}

/* nom ou chemin, nom du process attendu, ATTR_SYS attendu, libellé : V_EXEC,
 * contrôle, fermeture */
static void un_exec(const char *name, const char *proc, long sys, const char *label)
{
	long r = vop(V_EXEC, name);
	int ok = 0;
	if (r >= 0 && str_eq(MDATA, proc) && (M(MSG_C) & ATTR_SYS) == sys) {
		r = M(MSG_D); /* (valeur : la taille) */
		ok = 1;
	}
	verdict(ok, label, r);
	if (M(0) < 0) /* fermeture (si ouvert) */
		return;
	long ch = M(MSG_A), num = M(MSG_B);
	M(0) = FS_CLOSE;
	M(MSG_A) = num;
	MLEN = 0;
	korin_sys2(SYS_SVC_CALL, ch, (long)msg);
}

/* V_EXEC : nom seul (/bin), paquet (.app/main), /sys (ATTR_SYS) */
static void essai_exec(void)
{
	un_exec("vfstest", "vfstest", 0, "V_EXEC vfstest (/bin) : nom, taille");
	un_exec("welcome", "welcome", 0, "V_EXEC welcome (/apps/welcome.app/main) : nom, taille");
	un_exec("/apps/welcome.app", "welcome", 0, "V_EXEC /apps/welcome.app : nom, taille");
	un_exec("/sys/svc/vfs", s_vfs, ATTR_SYS, "V_EXEC /sys/svc/vfs : ATTR_SYS, taille");
	attendu(V_EXEC, "pas_un_programme", "V_EXEC pas_un_programme : E_NOENT", E_NOENT);
	attendu(V_EXEC, "/bin", "V_EXEC /bin : E_ISDIR", E_ISDIR);
}

/* un fils lancé après un chdir hérite du dossier ; après sa fin, son pid repris
 * reçoit le dossier du père, pas le sien */
static void essai_fils(void)
{
	static const char l_fils[] = "fils lance dans /tmp/vt : meme dossier (code du fils)";
	static const char l_fils2[] = "second fils lance dans /bin (fin du premier oubliee)";
	vop(V_CHDIR, p_vt);
	long p1 = ksys(SYS_SPAWN, (long)p_moi, (long)"enfant /tmp/vt", 0, 0, 0, 0);
	if (p1 < 0) {
		verdict(0, l_fils, p1);
	} else {
		long code = korin_sys1(SYS_WAIT, p1);
		verdict(code == 0, l_fils, code);
	}
	vop(V_CHDIR, p_sd); /* père : /bin */
	long p2 = ksys(SYS_SPAWN, (long)p_moi, (long)"enfant /bin", 0, 0, 0, 0);
	if (p2 < 0) {
		verdict(0, l_fils2, p2);
		return;
	}
	long code = korin_sys1(SYS_WAIT, p2);
	verdict(code == 0, l_fils2, code);
	ak_puts("vfstest : pids des fils : "); /* (pids des deux fils) */
	print_dec(p1);
	ak_puts(" et ");
	print_dec(p2);
	ak_puts("\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long c = korin_sys2(SYS_SVC_OPEN, (long)s_vfs, 5000);
	if (c < 0) {
		ak_puts("vfstest : FAIL service vfs absent\n");
		_exit(1);
	}
	canal = c;
	if (krt_line && krt_line[0] == 'e')
		enfant(krt_line);
	long r;
	sys_sleep(1500); /* (lancé au démarrage : /tmp monté) */
	vop(V_UNLINK, p_f_abs); /* restes d'un essai précédent */
	vop(V_UNLINK, p_sous_abs);
	vop(V_UNLINK, p_vt);

	cwd_vaut("V_GETCWD au depart = /", c_sd0);
	attendu(V_MKDIR, p_vt, "V_MKDIR /tmp/vt", 0);
	attendu(V_CHDIR, "ram0:vt", "V_CHDIR ram0:vt", 0); /* minuscules, sans « / » */
	cwd_vaut("V_GETCWD = /tmp/vt", c_vt);
	attendu(V_MKDIR, p_sous, "V_MKDIR sous (relatif)", 0);
	r = vop(V_STAT, "./sous/."); /* un répertoire */
	verdict(r == 0 && (M(MSG_C) & ATTR_DIR) == ATTR_DIR, "V_STAT ./sous/. : repertoire (attributs)", M(MSG_C));
	attendu(V_CHDIR, p_sous, "V_CHDIR sous (relatif)", 0);
	cwd_vaut("V_GETCWD = /tmp/vt/sous", "/tmp/vt/sous");
	attendu(V_CHDIR, "..", "V_CHDIR ..", 0);
	cwd_vaut("V_GETCWD apres .. = /tmp/vt", c_vt);
	essai_fichier();
	attendu(V_CHDIR, p_f, "V_CHDIR f.txt : E_NOTDIR", E_NOTDIR);
	attendu(V_STAT, "XYZ9:/rien", "V_STAT XYZ9:/rien : E_NOENT", E_NOENT);

	/* « / » : racine du volume courant (/tmp) */
	r = vop(V_STAT, "/vt/sous/../f.txt");
	verdict(r == 0 && M(MSG_B) == 5, "V_STAT /vt/sous/../f.txt (racine de /tmp) : taille 5", M(MSG_B));
	attendu(V_CHDIR, p_racine, l_chdir_racine, 0);
	cwd_vaut("V_GETCWD apres / = /tmp/", "/tmp/");
	attendu(V_CHDIR, p_sd, "V_CHDIR SD:bin", 0); /* SD: (volume 0), puis « /bin » */
	cwd_vaut("V_GETCWD = /bin", "/bin");
	r = vop(V_STAT, "/bin/vfstest");
	verdict(r == 0, "V_STAT /bin/vfstest (racine de /) : attributs", M(MSG_C));
	attendu(V_CHDIR, p_racine, l_chdir_racine, 0);
	cwd_vaut("V_GETCWD apres / = /", c_sd0);
	r = vop(V_CHDIR, "/.."); /* au-dessus de la racine : refusé */
	verdict(r < 0, "V_CHDIR /.. refuse", r);
	essai_exec();
	essai_fils();
	attendu(V_UNLINK, p_f_abs, "V_UNLINK /tmp/vt/f.txt", 0); /* ménage */
	attendu(V_UNLINK, p_sous_abs, "V_UNLINK /tmp/vt/sous", 0);
	attendu(V_UNLINK, p_vt, "V_UNLINK /tmp/vt", 0);
	ak_puts("vfstest : termine, echecs : ");
	print_dec(echecs);
	ak_puts("\n");
	_exit((int)echecs);
}
