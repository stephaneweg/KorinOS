/* ftp - client FTP en ligne de commande (docs/OUTILS.md ; comme ftp.asm) :
 *   ftp [-u UTILISATEUR] [-p MOT_DE_PASSE] [HOTE [PORT]]
 * puis une commande par ligne sur l'entrée standard (invite « ftp> ») :
 *   open hote [port]    user nom [mot de passe]    close    bye / quit / exit
 *   ls [chemin]    dir [chemin]    cd chemin    cdup    pwd
 *   get distant [local]    put local [distant]    mget noms...    mput noms...
 *   delete nom    mkdir nom    rmdir nom    rename de vers    size nom
 *   lcd [dossier]    lpwd    quote COMMANDE    help / ?
 * Mode passif (PASV ; les données vont à l'adresse de la connexion de
 * contrôle), binaire (TYPE I). Sans -u, l'utilisateur est demandé ; vide :
 * anonymous. Les réponses du serveur sont affichées telles quelles.
 * Comme l'asm : une ligne a 16 mots au plus (le reste est perdu) ; un CR fait
 * partie du mot ; « pwd » et « cdup » ignorent leur argument ; « quote »
 * rejoint ses mots par une espace ; « open » avec un port illisible donne le
 * port 0 ; un fichier reçu vide (pas de données ou refus) est retiré ; la fin
 * de l'entrée envoie QUIT ; code 0 (usage : 2).
 * Différence voulue : un fichier local inaccessible (get vers un chemin
 * impossible, put d'un fichier absent) fait tomber l'asm en faute (t_err_fic
 * y lit argv dans x20, qui tient alors un chemin) ; ici, « ftp: chemin :
 * inaccessible » et la session continue. */
#include "korinrt.h"
#include "netkit_import.h"

#define WORDS_MAX 16
#define DATA 16384
#define NAME_MAX 128

static long ctl = -1; /* connexion de contrôle (< 0 : aucune) */
static u32 ctl_ip;
static const char *opt_user, *opt_pass;
static int done;
static char *words[WORDS_MAX + 1];
static char host[NAME_MAX];
static char user[NAME_MAX];
static char pass[NAME_MAX];
static char cwd[256];
static char cmdbuf[NT_LINE + 32];
static char last[NT_LINE]; /* la dernière ligne de la dernière réponse */
static u8 data[DATA];

/* ---- outils */

/* ligne : coupée en mots (words[]) -> nombre */
static int split(char *s)
{
	int n = 0;
	for (;;) {
		while (*s == ' ' || *s == 9)
			*s++ = 0;
		if (!*s || n >= WORDS_MAX)
			break;
		words[n++] = s;
		while (*s && *s != ' ' && *s != 9)
			s++;
		if (!*s)
			break;
	}
	words[n] = 0;
	return n;
}

/* destination, source, taille de la destination (0 compris) : au plus n - 1 octets */
static void copy_bounded(char *d, const char *s, u64 n)
{
	if (n > 1)
		for (n--; *s && n; n--)
			*d++ = *s++;
	*d = 0;
}

/* -> 1 si connecté (sinon « Pas connecté. ») */
static int connected(void)
{
	if (ctl >= 0)
		return 1;
	out_say("Pas connecté.\n");
	return 0;
}

/* invite (traduite) -> ligne lue sur l'entrée standard (0 à la fin) */
static char *ask(const char *prompt)
{
	out_say(prompt);
	out_flush();
	return rd_line(&krt_in, 0);
}

/* la connexion de contrôle est perdue */
static void lose_ctl(void)
{
	if (ctl < 0)
		return;
	long c = ctl;
	ctl = -1;
	net_close(c);
	out_say("ftp : connexion perdue\n");
}

/* ligne -> son code à trois chiffres (0 s'il n'y en a pas) */
static u64 code_of(const char *l)
{
	u64 v = 0;
	for (int i = 0; i < 3; i++) {
		u32 d = (u32)(u8)l[i] - '0';
		if (d > 9)
			return 0;
		v = v * 10 + d;
	}
	return v;
}

/* -> code de la réponse du serveur (affichée), -1 si la connexion est perdue ;
 * last = sa dernière ligne */
static long reply(void)
{
	char *l = nt_line();
	if (!l)
		goto lost;
	out_puts(l);
	out_nl();
	u64 code = code_of(l);
	if (l[3] == '-') /* réponse sur plusieurs lignes : jusqu'à « NNN » suivi d'une espace */
		for (;;) {
			l = nt_line();
			if (!l)
				goto lost;
			out_puts(l);
			out_nl();
			if (l[3] == ' ' && code_of(l) == code)
				break;
		}
	copy_bounded(last, l, NT_LINE);
	return (long)code;
lost:
	lose_ctl();
	return -1;
}

/* commande, argument (0 : aucun) : envoyée -> code de la réponse, -1 */
static long send_cmd(const char *cmd, const char *arg)
{
	char *p = str_cat(cmdbuf, cmd);
	if (arg) {
		*p++ = ' ';
		p = str_cat(p, arg);
	}
	str_cat(p, "\r\n");
	if (ctl < 0 || nt_send_str(ctl, cmdbuf) < 0) {
		lose_ctl();
		return -1;
	}
	return reply();
}

/* QUIT, puis la connexion de contrôle fermée (rien si pas connecté) */
static void quit_ctl(void)
{
	if (ctl < 0)
		return;
	send_cmd("QUIT", 0);
	if (ctl < 0)
		return;
	long c = ctl;
	ctl = -1;
	net_close(c);
}

/* utilisateur (0 : demandé), mot de passe (0 : demandé) : USER, PASS, TYPE I */
static void login(const char *u, const char *pw)
{
	if (!u) {
		out_say("Utilisateur (");
		out_puts(host);
		u = ask(") : ");
		if (u && !*u)
			u = 0;
	}
	copy_bounded(user, u ? u : "anonymous", NAME_MAX);
	long r = send_cmd("USER", user);
	if (r != 230) {
		if (r != 331 && r != 332)
			goto refused;
		if (!pw) {
			if (str_eq(user, "anonymous")) /* anonymous : une adresse pour mot de passe */
				pw = "korin@";
			else {
				pw = ask("Mot de passe : ");
				if (!pw)
					pw = "";
			}
		}
		copy_bounded(pass, pw, NAME_MAX);
		r = send_cmd("PASS", pass);
		if (r != 230 && r != 202)
			goto refused;
	}
	send_cmd("TYPE", "I");
	return;
refused:
	if (r >= 0)
		out_say("Identification refusée.\n");
}

/* hôte, port : connexion, accueil, identification */
static void open_host(const char *h, long port)
{
	copy_bounded(host, h, NAME_MAX);
	quit_ctl();
	out_say("Connexion à ");
	out_puts(host);
	out_puts("...\n");
	out_flush();
	if (!nt_network()) {
		err_say("ftp : pas de réseau\n");
		return;
	}
	long sd = nt_tcp(host, port, &ctl_ip);
	if (sd < 0) {
		err_say("ftp : connexion impossible : ");
		nt_err_text(sd);
		err_puts("\n");
		return;
	}
	ctl = sd;
	nt_read_start(sd);
	if (reply() != 220) { /* 220 : bienvenue */
		quit_ctl();
		return;
	}
	login(opt_user, opt_pass);
}

/* PASV -> socket de données connecté, < 0 si impossible */
static long passive(void)
{
	if (send_cmd("PASV", 0) != 227)
		goto no;
	const char *p = last + 4; /* « 227 ... (h1,h2,h3,h4,p1,p2) » */
	while ((u32)(u8)*p - '0' > 9) {
		if (!*p)
			goto no;
		p++;
	}
	u64 nums = 0, v = 0;
	for (;;) {
		u64 x = 0;
		u8 c;
		while ((u32)((c = (u8)*p) - '0') <= 9u) {
			x = x * 10 + (c - '0');
			p++;
		}
		nums++;
		v = v << 8 | (x & 0xFF);
		if (nums >= 6)
			break;
		if (c != ',')
			goto no;
		p++;
	}
	/* (l'adresse de la connexion de contrôle : celle du 227 peut être privée, ou NAT) */
	long sd = nt_tcp_ip(ctl_ip, (long)(v & 0xFFFF));
	if (sd >= 0)
		return sd;
no:
	out_say("ftp : pas de connexion de données\n");
	return -1;
}

/* code -> 1 si 1xx (le transfert commence) */
static int start_ok(long code)
{
	return (u64)(code - 100) < 100;
}

/* octets, ms : « N octets en M ms (K Ko/s) » */
static void report(u64 bytes, u64 ms)
{
	out_putn(bytes);
	out_say(" octets en ");
	out_putn(ms);
	out_puts(" ms");
	if (ms) {
		out_puts(" (");
		out_putn(bytes / ms);
		out_say(" Ko/s)");
	}
	out_nl();
}

/* ---- commandes : nombre de mots, mots */

static void cmd_bye(int n, char **w)
{
	(void)n, (void)w;
	quit_ctl();
	done = 1;
}

static void cmd_close(int n, char **w)
{
	(void)n, (void)w;
	if (!connected())
		return;
	quit_ctl();
	out_say("Déconnecté.\n");
}

static void cmd_help(int n, char **w)
{
	(void)n, (void)w;
	out_say("open hôte [port]   user nom [mot de passe]   close   bye / quit\n"
		"ls [chemin]   dir [chemin]   cd chemin   cdup   pwd\n"
		"get distant [local]   put local [distant]   mget noms...   mput noms...\n"
		"delete nom   mkdir nom   rmdir nom   rename de vers   size nom\n"
		"lcd [dossier]   lpwd   quote COMMANDE   (binaire, passif ; pas de FTPS)\n");
}

static void cmd_mode(int n, char **w)
{
	(void)n, (void)w;
	out_say("(toujours binaire, passif)\n");
}

static void cmd_open(int n, char **w)
{
	if (n < 2) {
		out_say("usage : open hôte [port]\n");
		return;
	}
	open_host(w[1], n >= 3 ? (long)parse_dec(w[2]) : 21);
}

static void cmd_user(int n, char **w)
{
	if (!connected())
		return;
	if (n < 2) {
		out_say("usage : user nom [mot de passe]\n");
		return;
	}
	login(w[1], n >= 3 ? w[2] : 0);
}

/* une commande simple, son argument facultatif */
static void simple_opt(int n, char **w, const char *cmd)
{
	if (!connected())
		return;
	send_cmd(cmd, n >= 2 ? w[1] : 0);
}

/* une commande simple à argument */
static void simple_arg(int n, char **w, const char *cmd, const char *use)
{
	if (!connected())
		return;
	if (n < 2) {
		out_say(use);
		return;
	}
	send_cmd(cmd, w[1]);
}

/* (pwd et cdup : sans argument, même s'il y en a un, comme l'asm) */
static void cmd_pwd(int n, char **w) { (void)n; simple_opt(1, w, "PWD"); }
static void cmd_cdup(int n, char **w) { (void)n; simple_opt(1, w, "CDUP"); }
static void cmd_size(int n, char **w) { simple_arg(n, w, "SIZE", "usage : size nom\n"); }
static void cmd_cd(int n, char **w) { simple_arg(n, w, "CWD", "usage : cd chemin\n"); }
static void cmd_delete(int n, char **w) { simple_arg(n, w, "DELE", "usage : delete nom\n"); }
static void cmd_mkdir(int n, char **w) { simple_arg(n, w, "MKD", "usage : mkdir nom\n"); }
static void cmd_rmdir(int n, char **w) { simple_arg(n, w, "RMD", "usage : rmdir nom\n"); }

static void cmd_rename(int n, char **w)
{
	if (!connected())
		return;
	if (n < 3) {
		out_say("usage : rename de vers\n");
		return;
	}
	if (send_cmd("RNFR", w[1]) == 350)
		send_cmd("RNTO", w[2]);
}

/* quote : la ligne telle quelle (ses mots rejoints) */
static void cmd_quote(int n, char **w)
{
	if (!connected())
		return;
	if (n < 2) {
		out_say("usage : quote COMMANDE [arguments]\n");
		return;
	}
	for (int i = 2; i < n; i++)
		w[i][-1] = ' '; /* (l'espace remise à la place du 0) */
	send_cmd(w[1], 0);
}

/* NLST / LIST : la liste par une connexion de données */
static void list(int n, char **w, const char *cmd)
{
	if (!connected())
		return;
	long d = passive();
	if (d < 0)
		return;
	if (!start_ok(send_cmd(cmd, n >= 2 ? w[1] : 0))) {
		net_close(d);
		return;
	}
	for (;;) {
		long r = net_recv(d, (long)data, DATA, 30000);
		if (r <= 0)
			break;
		for (long i = 0; i < r; i++) /* (sans les CR) */
			if (data[i] != 13)
				out_putc(data[i]);
	}
	net_close(d);
	reply(); /* 226 */
}

static void cmd_ls(int n, char **w) { list(n, w, "NLST"); }
static void cmd_dir(int n, char **w) { list(n, w, "LIST"); }

/* fichier distant, local (0 : le dernier composant du distant) : RETR */
static void receive(char *remote, char *local)
{
	if (!connected())
		return;
	if (!local)
		local = path_base(remote);
	long fd = sys_open(local, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0) {
		err_file(local);
		return;
	}
	long d = passive();
	if (d < 0)
		goto none;
	long t0 = sys_time_ms();
	if (!start_ok(send_cmd("RETR", remote))) {
		net_close(d);
		goto none;
	}
	u64 bytes = 0;
	for (;;) {
		long r = net_recv(d, (long)data, DATA, 30000);
		if (r <= 0)
			break;
		bytes += (u64)r;
		if (sys_write(fd, data, (u64)r) < 0) {
			out_say("ftp : écriture impossible, transfert arrêté\n"); /* le disque refuse : on arrête */
			break;
		}
	}
	net_close(d);
	sys_close(fd);
	reply(); /* 226 */
	long ms = sys_time_ms() - t0;
	out_puts(local);
	out_puts(" : ");
	report(bytes, (u64)ms);
	return;
none: /* rien reçu : le fichier vide est retiré */
	sys_close(fd);
	sys_unlink(local);
}

static void cmd_get(int n, char **w)
{
	if (n < 2) {
		out_say("usage : get distant [local]\n");
		return;
	}
	receive(w[1], n >= 3 ? w[2] : 0);
}

static void cmd_mget(int n, char **w)
{
	for (int i = 1; i < n; i++)
		receive(w[i], 0);
}

/* fichier local, distant (0 : le dernier composant du local) : STOR */
static void send_file(char *local, char *remote)
{
	if (!connected())
		return;
	if (!remote)
		remote = path_base(local);
	long fd = sys_open(local, O_READ);
	if (fd < 0) {
		err_file(local);
		return;
	}
	long d = passive();
	if (d < 0)
		goto none;
	long t0 = sys_time_ms();
	if (!start_ok(send_cmd("STOR", remote))) {
		net_close(d);
		goto none;
	}
	u64 bytes = 0;
	for (;;) {
		long r = sys_read(fd, data, DATA);
		if (r <= 0)
			break;
		bytes += (u64)r;
		const u8 *p = data; /* tout envoyer */
		u64 left = (u64)r;
		while (left) {
			long s = net_send(d, (long)p, (long)left);
			if (s <= 0) {
				out_say("ftp : envoi interrompu\n");
				goto end;
			}
			p += s;
			left -= (u64)s;
		}
	}
end:
	sys_close(fd);
	net_shutdown(d); /* fin des données : FIN, puis la réponse */
	reply(); /* 226 */
	net_close(d);
	long ms = sys_time_ms() - t0;
	out_puts(remote);
	out_puts(" : ");
	report(bytes, (u64)ms);
	return;
none:
	sys_close(fd);
}

static void cmd_put(int n, char **w)
{
	if (n < 2) {
		out_say("usage : put local [distant]\n");
		return;
	}
	send_file(w[1], n >= 3 ? w[2] : 0);
}

static void cmd_mput(int n, char **w)
{
	for (int i = 1; i < n; i++)
		send_file(w[i], 0);
}

static void lpwd(void)
{
	sys_getcwd(cwd, 256);
	out_puts(cwd);
	out_nl();
}

static void cmd_lpwd(int n, char **w)
{
	(void)n, (void)w;
	lpwd();
}

static void cmd_lcd(int n, char **w)
{
	if (n >= 2 && sys_chdir(w[1]) < 0) {
		out_say("lcd : dossier introuvable : ");
		out_puts(w[1]);
		out_nl();
	}
	out_say("Dossier local : ");
	lpwd();
}

static const struct {
	const char *name;
	void (*fn)(int, char **);
} cmds[] = {
	{"open", cmd_open}, {"user", cmd_user}, {"close", cmd_close},
	{"disconnect", cmd_close}, {"bye", cmd_bye}, {"quit", cmd_bye},
	{"exit", cmd_bye}, {"help", cmd_help}, {"?", cmd_help},
	{"ls", cmd_ls}, {"dir", cmd_dir}, {"pwd", cmd_pwd},
	{"cd", cmd_cd}, {"cdup", cmd_cdup}, {"get", cmd_get},
	{"recv", cmd_get}, {"put", cmd_put}, {"send", cmd_put},
	{"mget", cmd_mget}, {"mput", cmd_mput}, {"size", cmd_size},
	{"delete", cmd_delete}, {"del", cmd_delete}, {"rm", cmd_delete},
	{"mkdir", cmd_mkdir}, {"md", cmd_mkdir}, {"rmdir", cmd_rmdir},
	{"rd", cmd_rmdir}, {"rename", cmd_rename}, {"ren", cmd_rename},
	{"lcd", cmd_lcd}, {"lpwd", cmd_lpwd}, {"quote", cmd_quote},
	{"binary", cmd_mode}, {"bin", cmd_mode}, {"ascii", cmd_mode},
	{"passive", cmd_mode}, {"prompt", cmd_mode},
	{0, 0},
};

int main(int argc, char **argv)
{
	const char *h = 0;
	long port = 21;
	for (int i = 1; i < argc;) {
		const char *a = argv[i++];
		if (a[0] != '-') {
			if (!h)
				h = a;
			else {
				port = (long)parse_dec(a);
				if (!port)
					goto usage;
			}
			continue;
		}
		if (a[2]) /* (« - » seul : l'octet après sa fin, comme l'asm) */
			goto usage;
		if (i >= argc)
			goto usage;
		const char *v = argv[i++];
		if (a[1] == 'u')
			opt_user = v;
		else if (a[1] == 'p')
			opt_pass = v;
		else
			goto usage;
	}
	rd_init(&krt_in, 0); /* les commandes : l'entrée standard */
	if (h)
		open_host(h, port);
	for (;;) {
		out_puts("ftp> ");
		out_flush();
		char *l = rd_line(&krt_in, 0);
		if (!l) {
			out_nl();
			quit_ctl();
			break;
		}
		int n = split(l);
		if (!n)
			continue;
		int k = 0;
		while (cmds[k].name && !str_eq(cmds[k].name, words[0]))
			k++;
		if (!cmds[k].name) {
			out_say("?Commande inconnue : ");
			out_puts(words[0]);
			out_nl();
			continue;
		}
		cmds[k].fn(n, words);
		if (done)
			break;
	}
	return 0;
usage:
	err_say("usage : ftp [-u utilisateur] [-p mot de passe] [hôte [port]]\n");
	return 2;
}
