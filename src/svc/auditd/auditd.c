/* auditd - le journal d'audit (E56 du plan de sécurité, constat B12) : /var/log/auth.log, en ajout seulement, écrit
 * par ce service d'uid 0 (lancé par init2, /etc/services.txt). PC seulement.
 *
 * Plutôt que d'être appelé par chaque service (vfs ne peut pas attendre un service qui écrit par vfs : interblocage),
 * auditd relit le journal du noyau (SYS_KLOG_FROM : seulement ce qui est nouveau, toutes les 2 s) et garde les lignes
 * qui comptent pour la sécurité :
 *   « authd : »               élévations, sudo, connexions, passwd, accès à distance (et leurs refus) ;
 *   « vfs : acces refuse »    les refus des règles d'accès (et « journal seulement » en mode log) ;
 *   « vfs : ecriture refusee » l'ESP hors de \korin et \EFI\Korin (E54) ;
 *   « vfs : commande systeme » une commande système sans sudo ;
 *   « init2 : svc_ »          un service arrêté ou lancé par Réglages ;
 *   « droits : »              un programme qui use des droits du système (E33) ;
 * et les lignes nouvelles de /var/pkg/commit.log (les commits de paquets, préfixe « pkg : »). Chaque ligne est
 * précédée de la date locale ; « -- démarrage » au début de chaque démarrage. Au-delà de 1 Mo, auth.log devient
 * auth.log.1 (un seul ancien gardé). /var/log est au système : un compte ne l'écrit pas (vfs).
 * Pas encore : boot.cfg (sur l'ESP, montée à la demande seulement). */
#include "korinrt.h"

#ifndef SYS_KLOG_FROM
#define SYS_KLOG_FROM 0x98
#endif

#define LOG_DIR "/var/log"
#define LOG_FILE "/var/log/auth.log"
#define LOG_OLD "/var/log/auth.log.1"
#define LOG_MAX (1024 * 1024)
#define COMMIT_LOG "/var/pkg/commit.log"
#define PERIOD_MS 2000
#define CHUNK 16384

static const char *const keep[] = {"authd : ", "vfs : acces refuse", "vfs : ecriture refusee", "vfs : commande systeme",
				   "init2 : svc_", "droits : ", 0};

static u8 kbuf[8 + CHUNK];
static char line[1024];
static u64 line_n;
static char out[4096];
static u64 out_n;

static int starts(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return 1;
}

static void put_dec2(char *o, u64 v)
{
	o[0] = (char)('0' + v / 10 % 10);
	o[1] = (char)('0' + v % 10);
}

/* « AAAA-MM-JJ HH:MM:SS » (heure locale) dans o (20 octets, sans 0) */
static void stamp(char *o)
{
	struct civil c;
	long s = sys_clock(2);
	civil_from_secs(s > 0 ? (u64)s : 0, &c);
	u64 y = c.year;
	o[0] = (char)('0' + y / 1000 % 10);
	o[1] = (char)('0' + y / 100 % 10);
	put_dec2(o + 2, y);
	o[4] = '-';
	put_dec2(o + 5, c.month);
	o[7] = '-';
	put_dec2(o + 8, c.day);
	o[10] = ' ';
	put_dec2(o + 11, c.hour);
	o[13] = ':';
	put_dec2(o + 14, c.min);
	o[16] = ':';
	put_dec2(o + 17, c.sec);
	o[19] = ' ';
}

/* une ligne (sans fin de ligne) dans out, datée ; prefix : devant (0 : aucun) */
static void emit(const char *prefix, const char *s, u64 n)
{
	u64 pl = prefix ? (u64)str_len(prefix) : 0;
	if (out_n + 20 + pl + n + 1 > sizeof out)
		return; /* (trop pour ce tour : le suivant) */
	stamp(out + out_n);
	out_n += 20;
	if (pl) {
		memcpy(out + out_n, prefix, pl);
		out_n += pl;
	}
	memcpy(out + out_n, s, n);
	out_n += n;
	out[out_n++] = '\n';
}

/* out -> auth.log (ajout) ; au-delà de LOG_MAX, l'ancien devient auth.log.1 */
static void flush(void)
{
	if (!out_n)
		return;
	long st[2];
	if (sys_stat(LOG_FILE, st) == 0 && st[0] > LOG_MAX) {
		sys_unlink(LOG_OLD);
		vfs_rename(LOG_FILE, LOG_OLD);
	}
	long fd = sys_open(LOG_FILE, O_WRITE | O_CREATE | O_APPEND);
	if (fd >= 0) {
		sys_write(fd, out, out_n);
		sys_close(fd);
	}
	out_n = 0;
}

/* une ligne complète du journal du noyau : gardée si elle compte */
static void klog_line(void)
{
	line[line_n] = 0;
	for (int i = 0; keep[i]; i++)
		if (starts(line, keep[i])) {
			emit(0, line, line_n);
			break;
		}
	line_n = 0;
}

static u64 kpos;

static void read_klog(void)
{
	for (;;) {
		long n = korin_sys3(SYS_KLOG_FROM, (long)kpos, (long)kbuf, sizeof kbuf);
		if (n <= 0)
			return;
		u64 at = *(u64 *)kbuf;
		if (at != kpos)
			line_n = 0; /* (des octets perdus : l'anneau a tourné) */
		for (long i = 0; i < n; i++) {
			char c = (char)kbuf[8 + i];
			if (c == '\n')
				klog_line();
			else if (line_n < sizeof line - 1)
				line[line_n++] = c;
		}
		kpos = at + (u64)n;
		if (n < CHUNK)
			return;
	}
}

/* les lignes nouvelles de /var/pkg/commit.log (sa taille au démarrage d'auditd : déjà vu) */
static long commit_seen = -1;
static char cbuf[4096];

static void read_commits(void)
{
	long st[2];
	if (sys_stat(COMMIT_LOG, st) != 0)
		return;
	if (commit_seen < 0 || st[0] < commit_seen) { /* (le premier tour, ou le fichier recommencé) */
		commit_seen = commit_seen < 0 ? st[0] : 0;
		if (st[0] == commit_seen)
			return;
	}
	if (st[0] == commit_seen)
		return;
	long fd = sys_open(COMMIT_LOG, O_READ);
	if (fd < 0)
		return;
	sys_seek(fd, commit_seen, 0);
	long n = sys_read(fd, cbuf, sizeof cbuf);
	sys_close(fd);
	if (n <= 0)
		return;
	long last = -1; /* (jusqu'à la dernière ligne complète) */
	for (long i = 0; i < n; i++)
		if (cbuf[i] == '\n')
			last = i;
	if (last < 0)
		return;
	long b = 0;
	for (long i = 0; i <= last; i++)
		if (cbuf[i] == '\n') {
			if (i > b)
				emit("pkg : ", cbuf + b, (u64)(i - b));
			b = i + 1;
		}
	commit_seen += last + 1;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	sys_mkdir(LOG_DIR);
	static const char boot[] = "-- démarrage";
	emit(0, boot, sizeof boot - 1);
	for (;;) {
		read_klog();
		read_commits();
		flush();
		korin_sys1(SYS_SLEEP, PERIOD_MS);
	}
}
