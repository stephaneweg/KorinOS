/* ftpd - transfert de fichiers (docs/RESEAU.md 5.2, étape 9), port 21, lancé
 * par init2 (SD0:/etc/services.txt) ; traduction de ftpd.asm (mêmes réponses,
 * mêmes commandes, mêmes bizarreries : « comme l'asm »). 4 sessions à la fois
 * (un thread chacune). Commandes : USER (tout nom), PASS, SYST, FEAT, OPTS,
 * AUTH, PWD, CWD, CDUP, TYPE, MODE, STRU, NOOP, PASV, EPSV, PORT, LIST, NLST,
 * RETR, STOR, SIZE, MKD, RMD, DELE, XCRC (CRC32 d'un fichier), QUIT. Pas de
 * RNFR/RNTO.
 * Chemins : ceux de Korin vus comme une arborescence Unix : « /SD0/sys/svc/netd »
 * <-> SD0:/sys/svc/netd ; « / » : les volumes.
 * Mode passif : port de données 2100 + numéro de session.
 * Mot de passe : celui de l'accès à distance (remotepw.c), relu à chaque PASS :
 * 230 si bon, 530 sinon. Avant, seuls USER, PASS, SYST, FEAT, OPTS, QUIT et
 * AUTH (502 : pas de TLS) répondent ; le reste : 530.
 * Écart voulu : la pile d'un thread de session est gardée par sa place et
 * reprise par la session suivante (l'asm en prenait une neuve à chaque
 * connexion, jamais rendue). */
#include "korinrt.h"
#include "netkit_import.h"
#include "../inc/remotepw.h"

#define PORT 21
#define DATA_PORT 2100
#define SESS_MAX 4
#define TSTACK 32768
#define BLOC 8192 /* morceau de transfert */

struct session {
	long used;
	long ctrl;          /* connexion de commande */
	long num;           /* numéro (port passif) */
	long pasv;          /* socket en écoute (passif ; -1 : aucune) */
	u64 active_ip;      /* PORT : adresse, port (0 : aucun) */
	u64 active_port;
	long data;          /* connexion de données ouverte */
	char cwd[256];      /* dossier FTP (« /SD0/home ») */
	char line[512];     /* ligne reçue */
	u8 in[512];         /* octets reçus non encore lus */
	u64 in_i, in_n;
	char path[512];     /* chemin Korin */
	char rep[640];      /* réponse */
	struct dirent dirent;
	long st[6];         /* {taille, attributs} ; net_info : 32 o */
	long auth;          /* 1 : bon PASS reçu */
	u8 buf[BLOC];       /* tampon de transfert */
};

static struct session *sess;
static u8 *stacks[SESS_MAX];
static long rp_lock;

static const char s_crlf[] = "\r\n";
static const char s_dir[] = "drwxr-xr-x 1 korin korin ";
static const char s_file[] = "-rw-r--r-- 1 korin korin ";
static const char s_date[] = " Jan 01 2026 ";
static const char r_hello[] = "220 Korin OS ftpd\r\n";
static const char r_full[] = "421 Trop de sessions\r\n";
static const char r_user[] = "331 Mot de passe requis\r\n";
static const char r_pass[] = "230 Bienvenue\r\n";
static const char r_530[] = "530 Connexion requise\r\n";
static const char r_530wrong[] = "530 Mot de passe incorrect\r\n";
static const char r_530none[] = "530 Aucun mot de passe : definissez-le dans Reglages\r\n";
static const char r_syst[] = "215 UNIX Type: L8\r\n";
static const char r_feat[] = "211-Extensions\r\n SIZE\r\n XCRC\r\n EPSV\r\n PASV\r\n UTF8\r\n211 Fin\r\n";
static const char r_ok[] = "200 OK\r\n";
static const char r_type[] = "200 Binaire\r\n";
static const char r_quit[] = "221 Au revoir\r\n";
static const char r_no[] = "502 Non disponible\r\n";
static const char r_unknown[] = "500 Commande inconnue\r\n";
static const char r_257[] = "257 \"";
static const char r_257b[] = "\" est le dossier";
static const char r_cwd[] = "250 Dossier change\r\n";
static const char r_250[] = "250 Fait\r\n";
static const char r_250x[] = "250 ";
static const char r_213[] = "213 ";
static const char r_mkd[] = "257 Dossier cree\r\n";
static const char r_550[] = "550 Introuvable ou refuse\r\n";
static const char r_553[] = "553 Ecriture refusee\r\n";
static const char r_452[] = "452 Ecriture interrompue\r\n";
static const char r_501[] = "501 Argument invalide\r\n";
static const char r_227[] = "227 Entering Passive Mode (";
static const char r_227b[] = ")\r\n";
static const char r_229[] = "229 Entering Extended Passive Mode (|||";
static const char r_229b[] = "|)\r\n";
static const char r_port[] = "200 PORT accepte\r\n";
static const char r_150[] = "150 Ouverture de la connexion de donnees\r\n";
static const char r_226[] = "226 Transfert fini\r\n";
static const char r_425[] = "425 Pas de connexion de donnees\r\n";
static const char r_426[] = "426 Transfert interrompu\r\n";
static const char m_ready[] = "ftpd : en écoute sur le port 21\n";
static const char m_session[] = "ftpd : session\n";
static const char m_ended[] = "ftpd : session finie\n";
static const char volumes[] = "SD0\0SD1\0RAM0\0USB0\0USB1\0IMG0\0IMG1\0";

/* -> fin (sur le 0) */
static char *str_cpy(char *d, const char *s)
{
	while ((*d++ = *s++))
		;
	return d - 1;
}

static long send_str(long sock, const char *s)
{
	return net_send(sock, (long)s, str_len(s));
}

/* sur la connexion de commande */
static void reply(struct session *s, const char *r)
{
	send_str(s->ctrl, r);
}

/* réponse fixe (une ligne, CR LF compris) : traduite sans son CR LF (le code au
 * début en fait partie : la clé du dictionnaire est « 220 Bonjour ») */
static void say_on(long sock, const char *r)
{
	char b[256];
	char *d = b;
	while (*r && *r != 13)
		*d++ = *r++;
	*d = 0;
	const char *t = lang_tr(b);
	d = str_cpy(b, t); /* (copie vers l'avant : sur place si non traduite) */
	str_cpy(d, s_crlf);
	send_str(sock, b);
}

static void say(struct session *s, const char *r)
{
	say_on(s->ctrl, r);
}

/* -> line : une ligne de commande (sans CR LF) ; 0, < 0 si fin */
static long read_line(struct session *s)
{
	u64 len = 0;
	for (;;) {
		if (s->in_i >= s->in_n) { /* recevoir (5 min de silence : fin) */
			long n = net_recv(s->ctrl, (long)s->in, 512, 300000);
			if (n <= 0)
				return -1;
			s->in_n = (u64)n;
			s->in_i = 0;
			continue;
		}
		u8 c = s->in[s->in_i++];
		if (c == 10)
			break;
		if (c == 13 || len >= 500)
			continue;
		s->line[len++] = (char)c;
	}
	s->line[len] = 0;
	return 0;
}

/* ---- chemins */

/* 1 si c est dans la chaîne avant le premier « / » */
static int has_before_slash(const char *p, char c)
{
	for (; *p && *p != '/'; p++)
		if (*p == c)
			return 1;
	return 0;
}

/* chemin absolu « /a/./b/../c// » -> « /a/c » (sur place : l'écriture, « / »
 * puis le segment, ne dépasse jamais la lecture) */
static void normalize(char *p)
{
	char *r = p, *w = p;
	for (;;) {
		while (*r == '/') /* « / » multiples */
			r++;
		if (!*r)
			break;
		char *seg = r; /* un segment */
		while (*r && *r != '/')
			r++;
		long n = r - seg;
		if (n == 1 && seg[0] == '.')
			continue;
		if (n == 2 && seg[0] == '.' && seg[1] == '.') {
			while (w > p) /* « .. » : segment précédent retiré */
				if (*--w == '/')
					break;
			continue;
		}
		*w++ = '/';
		while (seg < r)
			*w++ = *seg++;
	}
	if (w <= p) /* rien : la racine */
		*w++ = '/';
	*w = 0;
}

/* argument (chemin FTP, relatif ou absolu ; vide : le dossier) -> rep = chemin
 * FTP normalisé (« /SD0/a/b »), path = chemin Korin (« SD0:/a/b ») ; 0 : la
 * racine (pas de chemin Korin), 1 : un chemin. Accepte aussi « SD0:/a/b ». */
static long resolve(struct session *s, const char *arg)
{
	char *r = s->rep;
	if (arg[0] == '/') {
		str_cpy(r, arg);
	} else if (has_before_slash(arg, ':')) { /* « SD0:/a » -> « /SD0/a » (tous les « : » sautés, comme l'asm) */
		char *d = r;
		*d++ = '/';
		for (const char *q = arg;; q++) {
			if (*q == ':')
				continue;
			*d++ = *q;
			if (!*q)
				break;
		}
	} else { /* relatif : dossier + « / » + argument */
		char *d = str_cpy(r, s->cwd);
		*d++ = '/';
		str_cpy(d, arg);
	}
	normalize(r); /* « . », « .. », « // » */
	if (!r[1])
		return 0;
	const char *q = r + 1; /* « /VOL[/...] » -> « VOL:/... » */
	char *d = s->path;
	char c;
	while ((c = *q++) && c != '/')
		*d++ = c;
	*d++ = ':';
	*d++ = '/';
	if (!c)
		*d = 0;
	else
		str_cpy(d, q);
	return 1;
}

/* ---- connexion de données */

/* ferme la connexion de données (et l'écoute passive, une fois servie) */
static void data_close(struct session *s)
{
	if (s->data >= 0) {
		net_close(s->data);
		s->data = -1;
	}
	if (s->pasv >= 0) {
		net_close(s->pasv);
		s->pasv = -1;
	}
}

/* -> connexion de données, < 0 si aucune (réponse 425 faite) */
static long data_open(struct session *s)
{
	long sd;
	if (s->active_ip) { /* actif : on se connecte */
		sd = net_socket(SOCK_TCP);
		if (sd >= 0) {
			if (net_connect(sd, (long)s->active_ip, (long)s->active_port) >= 0)
				goto ok;
			net_close(sd);
		}
	} else if (s->pasv >= 0) {
		sd = net_accept(s->pasv, 20000);
		if (sd >= 0)
			goto ok;
	}
	say(s, r_425);
	return -1;
ok:
	s->data = sd;
	return sd;
}

static long data_send(struct session *s, const void *p, u64 n)
{
	return net_send(s->data, (long)p, (long)n);
}

/* une ligne de liste (« drwxr-xr-x 1 korin korin 0 Jan 01 2026 nom ») -> < 0 si échec */
static long list_line(struct session *s, const char *name, u64 attr, u64 size, int nlst)
{
	char *d = s->rep;
	if (!nlst) {
		d = str_cpy(d, (attr & ATTR_DIR) ? s_dir : s_file);
		d = fmt_dec(size, d);
		d = str_cpy(d, s_date);
	}
	d = str_cpy(d, name);
	d = str_cpy(d, s_crlf);
	return data_send(s, s->rep, (u64)(d - s->rep));
}

/* ---- commandes (argument) -> 0 (1 : fin) */

static void rp_take(void)
{
	long z = 0;
	while (!__atomic_compare_exchange_n(&rp_lock, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		z = 0;
		korin_sys0(SYS_YIELD);
	}
}

static void rp_give(void)
{
	__atomic_store_n(&rp_lock, 0, __ATOMIC_RELEASE);
}

static int c_user(struct session *s, const char *a)
{
	(void)a;
	s->auth = 0; /* (nouvelle connexion : PASS à refaire) */
	say(s, r_user);
	return 0;
}

static int c_pass(struct session *s, const char *a)
{
	rp_take(); /* (rp_pw commun aux sessions) */
	if (!rp_read()) {
		rp_give();
		s->auth = 0;
		say(s, r_530none);
		return 0;
	}
	s->auth = rp_equal(a, (u64)str_len(a));
	rp_give();
	say(s, s->auth ? r_pass : r_530wrong);
	return 0;
}

static int c_syst(struct session *s, const char *a)
{
	(void)a;
	say(s, r_syst);
	return 0;
}

static int c_feat(struct session *s, const char *a)
{
	(void)a;
	reply(s, r_feat); /* (plusieurs lignes, technique : telle quelle) */
	return 0;
}

static int c_ok(struct session *s, const char *a)
{
	(void)a;
	say(s, r_ok);
	return 0;
}

static int c_quit(struct session *s, const char *a)
{
	(void)a;
	say(s, r_quit);
	return 1;
}

static int c_no(struct session *s, const char *a)
{
	(void)a;
	say(s, r_no);
	return 0;
}

static int c_type(struct session *s, const char *a)
{
	(void)a;
	say(s, r_type);
	return 0;
}

static int c_pwd(struct session *s, const char *a)
{
	(void)a;
	char *d = str_cpy(s->rep, r_257); /* 257 "/SD0/..." est le dossier */
	d = str_cpy(d, s->cwd);
	d = str_cpy(d, lang_tr(r_257b));
	str_cpy(d, s_crlf);
	reply(s, s->rep);
	return 0;
}

static int c_cwd(struct session *s, const char *a)
{
	long r = resolve(s, a);
	if (r) { /* « / » : toujours là */
		long fd = sys_open(s->path, O_READ); /* existe ? */
		if (fd < 0)
			goto no;
		sys_close(fd);
		/* un fichier : refusé (la racine d'un volume n'a pas de stat) */
		if (sys_stat(s->path, s->st) >= 0 && !(s->st[1] & ATTR_DIR))
			goto no;
	}
	str_cpy(s->cwd, s->rep); /* nouveau dossier */
	say(s, r_cwd);
	return 0;
no:
	say(s, r_550);
	return 0;
}

static int c_cdup(struct session *s, const char *a)
{
	(void)a;
	return c_cwd(s, "..");
}

static int c_size(struct session *s, const char *a)
{
	if (resolve(s, a) <= 0 || sys_stat(s->path, s->st) < 0) {
		say(s, r_550);
		return 0;
	}
	char *d = str_cpy(s->rep, r_213);
	d = fmt_dec((u64)s->st[0], d);
	str_cpy(d, s_crlf);
	reply(s, s->rep);
	return 0;
}

static int c_mkd(struct session *s, const char *a)
{
	if (resolve(s, a) <= 0 || sys_mkdir(s->path) < 0)
		say(s, r_550);
	else
		say(s, r_mkd);
	return 0;
}

static int c_dele(struct session *s, const char *a)
{
	if (resolve(s, a) <= 0 || sys_unlink(s->path) < 0)
		say(s, r_550);
	else
		say(s, r_250);
	return 0;
}

/* CRC32, polynôme 0xEDB88320, bit à bit */
static u32 crc32_update(u32 c, const u8 *p, u64 n)
{
	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : c >> 1;
	}
	return c;
}

/* XCRC chemin -> 250 XXXXXXXX (CRC32 du fichier) */
static int c_xcrc(struct session *s, const char *a)
{
	long fd;
	if (resolve(s, a) <= 0 || (fd = sys_open(s->path, O_READ)) < 0) {
		say(s, r_550);
		return 0;
	}
	u32 crc = ~0u;
	for (;;) {
		long n = sys_read(fd, s->buf, BLOC);
		if (n <= 0)
			break;
		crc = crc32_update(crc, s->buf, (u64)n);
	}
	sys_close(fd);
	crc = ~crc;
	char *d = str_cpy(s->rep, r_250x);
	for (int sh = 28; sh >= 0; sh -= 4) { /* 8 chiffres hexadécimaux */
		u32 v = (crc >> sh) & 15;
		*d++ = (char)(v < 10 ? '0' + v : 'A' + v - 10);
	}
	str_cpy(d, s_crlf);
	reply(s, s->rep);
	return 0;
}

/* PASV : écoute sur DATA_PORT + numéro -> 227 (a,b,c,d,p1,p2) ; EPSV : 229 */
static int pasv_common(struct session *s, int epsv)
{
	data_close(s);
	if (s->pasv < 0) { /* (écoute déjà ouverte : resert ; data_close vient de la fermer, comme l'asm) */
		long sd = net_socket(SOCK_TCP);
		if (sd < 0)
			goto no;
		if (net_bind(sd, s->num + DATA_PORT) < 0 || net_listen(sd, 1) < 0) {
			net_close(sd);
			goto no;
		}
		s->pasv = sd;
	}
	s->active_ip = 0;
	u64 port = (u64)s->num + DATA_PORT;
	char *d;
	if (!epsv) {
		d = str_cpy(s->rep, r_227);
		net_info((long)s->st); /* notre adresse */
		u32 ip = *(u32 *)s->st;
		for (int sh = 24; sh >= 0; sh -= 8) {
			d = fmt_dec((ip >> sh) & 0xFF, d);
			*d++ = ',';
		}
		d = fmt_dec(port >> 8, d);
		*d++ = ',';
		d = fmt_dec(port & 0xFF, d);
		str_cpy(d, r_227b);
	} else {
		d = str_cpy(s->rep, r_229);
		d = fmt_dec(port, d);
		str_cpy(d, r_229b);
	}
	reply(s, s->rep);
	return 0;
no:
	say(s, r_425);
	return 0;
}

static int c_pasv(struct session *s, const char *a)
{
	(void)a;
	return pasv_common(s, 0);
}

static int c_epsv(struct session *s, const char *a)
{
	(void)a;
	return pasv_common(s, 1);
}

/* PORT a,b,c,d,p1,p2 : mode actif (le serveur se connecte au client) */
static int c_port(struct session *s, const char *a)
{
	u64 acc = 0;
	int n = 0; /* six nombres */
	for (;;) {
		acc = acc << 8 | parse_dec(a); /* (un nombre > 255 déborde sur le voisin, comme l'asm) */
		n++;
		char c;
		while ((c = *a++) && c != ',')
			;
		if (!c)
			break;
	}
	if (n != 6) {
		say(s, r_501);
		return 0;
	}
	s->active_ip = (acc >> 16) & 0xFFFFFFFF; /* adresse */
	s->active_port = acc & 0xFFFF;
	say(s, r_port);
	return 0;
}

/* LIST, NLST */
static int list(struct session *s, const char *a, int nlst)
{
	if (*a == '-') { /* options « -la » : ignorées */
		char c;
		while ((c = *a)) {
			a++;
			if (c == ' ')
				break;
		}
	}
	long root = !resolve(s, a); /* la racine : les volumes */
	long fd = -1;
	if (!root && (fd = sys_open(s->path, O_READ)) < 0) {
		say(s, r_550);
		return 0;
	}
	say(s, r_150);
	if (data_open(s) < 0)
		goto close;
	if (!root) {
		while (sys_readdir(fd, &s->dirent) == 1)
			if (list_line(s, s->dirent.name, s->dirent.attr, s->dirent.size, nlst) < 0)
				break;
	} else {
		for (const char *v = volumes; *v; v += str_len(v) + 1) { /* ceux qui répondent */
			char *d = str_cpy(s->path, v);
			str_cpy(d, ":/");
			long f = sys_open(s->path, O_READ);
			if (f < 0)
				continue;
			sys_close(f);
			list_line(s, v, ATTR_DIR, 0, nlst);
		}
	}
	data_close(s);
	say(s, r_226);
close:
	if (!root)
		sys_close(fd);
	return 0;
}

static int c_list(struct session *s, const char *a)
{
	return list(s, a, 0);
}

static int c_nlst(struct session *s, const char *a)
{
	return list(s, a, 1);
}

static int c_retr(struct session *s, const char *a)
{
	long fd;
	if (resolve(s, a) <= 0 || (fd = sys_open(s->path, O_READ)) < 0) {
		say(s, r_550);
		return 0;
	}
	say(s, r_150);
	if (data_open(s) >= 0) {
		for (;;) {
			long n = sys_read(fd, s->buf, BLOC);
			if (n <= 0) {
				data_close(s);
				say(s, r_226);
				break;
			}
			if (data_send(s, s->buf, (u64)n) < 0) {
				data_close(s);
				say(s, r_426);
				break;
			}
		}
	}
	sys_close(fd);
	return 0;
}

static int c_stor(struct session *s, const char *a)
{
	long fd;
	if (resolve(s, a) <= 0 || (fd = sys_open(s->path, O_WRITE | O_CREATE | O_TRUNC)) < 0) {
		say(s, r_553);
		return 0;
	}
	say(s, r_150);
	if (data_open(s) >= 0) {
		for (;;) {
			long n = net_recv(s->data, (long)s->buf, BLOC, 60000);
			if (n <= 0) { /* 0 : le client a tout envoyé (60 s sans rien aussi, comme l'asm) */
				data_close(s);
				say(s, r_226);
				break;
			}
			if (sys_write(fd, s->buf, (u64)n) < 0) {
				data_close(s);
				say(s, r_452);
				break;
			}
		}
	}
	sys_close(fd);
	return 0;
}

struct verb {
	const char *name;
	int (*fn)(struct session *, const char *);
};

static const struct verb verbs[] = {
	{"USER", c_user}, {"PASS", c_pass}, {"SYST", c_syst}, {"FEAT", c_feat},
	{"OPTS", c_ok}, {"QUIT", c_quit}, {"AUTH", c_no},
#define VERBS_FREE 7 /* (ci-dessus : permis avant PASS) */
	{"MODE", c_ok}, {"STRU", c_ok}, {"NOOP", c_ok}, {"PWD", c_pwd},
	{"XPWD", c_pwd}, {"CWD", c_cwd}, {"CDUP", c_cdup}, {"TYPE", c_type},
	{"PASV", c_pasv}, {"EPSV", c_epsv}, {"PORT", c_port}, {"LIST", c_list},
	{"NLST", c_nlst}, {"RETR", c_retr}, {"STOR", c_stor}, {"SIZE", c_size},
	{"MKD", c_mkd}, {"XMKD", c_mkd}, {"RMD", c_dele}, {"XRMD", c_dele},
	{"DELE", c_dele}, {"XCRC", c_xcrc}, {"RNFR", c_no}, {"RNTO", c_no},
	{0, 0},
};

/* line : une commande -> 1 pour finir la session */
static int command(struct session *s)
{
	char *v = s->line, *a = v; /* verbe en majuscules, argument */
	for (;; a++) {
		if (!*a) {
			a = "";
			break;
		}
		if (*a == ' ') {
			*a++ = 0;
			break;
		}
		if (*a >= 'a' && *a <= 'z')
			*a -= 0x20;
	}
	int found = -1;
	for (int i = 0; verbs[i].name; i++)
		if (str_eq(verbs[i].name, v)) {
			found = i;
			break;
		}
	/* pas encore connecté : seules les premières de la table */
	if (found >= 0 && (s->auth || found < VERBS_FREE))
		return verbs[found].fn(s, a);
	say(s, found >= 0 || !s->auth ? r_530 : r_unknown);
	return 0;
}

/* la place libérée et le thread fini, sans toucher à la pile entre les deux
 * (la session suivante la reprend) */
NORETURN static void slot_free_exit(struct session *s)
{
	__atomic_store_n(&s->used, 0, __ATOMIC_RELEASE);
	korin_sys1(SYS_THREAD_EXIT, 0);
	for (;;)
		;
}

/* (thread) une session */
NORETURN static void session(struct session *s)
{
	s->pasv = -1;
	s->data = -1;
	s->active_ip = 0;
	s->in_n = s->in_i = 0;
	s->auth = 0;
	s->cwd[0] = '/'; /* dossier : la racine */
	s->cwd[1] = 0;
	ak_puts(m_session);
	say(s, r_hello);
	while (read_line(s) >= 0)
		if (command(s))
			break;
	data_close(s);
	if (s->pasv >= 0)
		net_close(s->pasv);
	net_close(s->ctrl);
	ak_puts(m_ended);
	slot_free_exit(s);
}

/* connexion : une session (thread), ou refus */
static void new_session(long sock)
{
	int i;
	for (i = 0; i < SESS_MAX; i++)
		if (!__atomic_load_n(&sess[i].used, __ATOMIC_ACQUIRE))
			break;
	if (i < SESS_MAX) {
		struct session *s = &sess[i];
		s->used = 1;
		s->ctrl = sock;
		s->num = i;
		if (!stacks[i])
			stacks[i] = malloc(TSTACK);
		if (stacks[i]) {
			u8 *top = (u8 *)(((u64)stacks[i] + TSTACK) & ~15UL);
			if (korin_sys3(SYS_THREAD_CREATE, session, top, s) >= 0)
				return;
		}
		s->used = 0;
	}
	say_on(sock, r_full);
	net_close(sock);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	lang_get(); /* (dictionnaires lus ici : malloc, avant les threads) */
	sess = calloc(SESS_MAX, sizeof *sess);
	if (!sess)
		return 1;
	for (;;) {
		long sd = net_socket(SOCK_TCP);
		if (sd >= 0) {
			if (net_bind(sd, PORT) >= 0 && net_listen(sd, SESS_MAX) >= 0) {
				ak_puts(m_ready);
				for (;;) {
					long c = net_accept(sd, 3600000);
					if (c == E_AGAIN)
						continue;
					if (c < 0) {
						if (c == E_PIPE) /* netd relancé : tout reprendre */
							break;
						continue;
					}
					new_session(c);
				}
			}
			net_close(sd);
		}
		korin_sys1(SYS_SLEEP, 2000);
	}
}
