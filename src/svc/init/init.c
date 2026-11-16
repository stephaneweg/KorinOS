/* init - étape 1 du démarrage en mode services (docs/MICRO-NOYAU.md, section
 * 4), repris de init.asm : seul module que le noyau lance lui-même
 * (initramfs). Il lit la liste init.txt (un module texte de initramfs : « nom
 * arguments » par ligne), lance ces services de démarrage (sd, vfs qui monte
 * / avec fatfs), attend que / réponde, donne au noyau les fichiers qu'il
 * attend (options.txt : appel BOOT_FILE ; le noyau ne lit aucun fichier), puis
 * lève le verrou du démarrage et lance l'étape 2 depuis le disque : init2
 * (/sys/svc/init2). Commun au Pi et au PC.
 *
 * « wait NOM ms » attend un service. « wait A|B ms » attend le premier venu de
 * plusieurs (A d'abord : si B vient, A a encore 300 ms) ; ensuite, dans les
 * lignes suivantes, le texte « A|B » est remplacé par le nom trouvé
 * (« vfs / A|B fatfs »). Le PC : « wait blk.boot|blk.korin » (blk.boot : la
 * partition d'où le chargeur a démarré ; blk.korin : la règle de nvme.txt, pour
 * un démarrage depuis un autre volume, le dossier esp de QEMU). */
#include "../svclib/svclib.h"

#define LISTE_MAX 2048
#define NOMS_MAX 512 /* boot_file(0) : noms attendus */
#define ATTENTE 300  /* x 100 ms : / doit répondre */

static char liste[LISTE_MAX];
static char alt_pat[96];   /* « A|B » du dernier wait à plusieurs noms ("" : aucun) */
static char alt_found[48]; /* le nom trouvé */
static char noms[NOMS_MAX];
static long infos[2];

/* les fichiers que le noyau attend (BOOT_FILE(0) : la liste) : chacun est lu
 * entier dans un tampon partagé temporaire et donné au noyau ; absent : passe */
static void boot_files(void)
{
	if (korin_sys3(SYS_BOOT_FILE, 0, noms, NOMS_MAX) <= 1)
		return;
	for (char *n = noms; *n; n += str_len(n) + 1) {
		long fd = sys_open(n, O_READ);
		if (fd < 0)
			continue;
		long size = sys_seek(fd, 0, 2); /* taille : position de la fin */
		sys_seek(fd, 0, 0);
		if (size > 0) {
			long b = korin_sys1(SYS_BUF_CREATE, size);
			if (b >= 0) {
				void *va = (void *)(BUF_VA_BASE + ((u64)b << 26)); /* (BUF_SLOT = 64 Mo) */
				if (sys_read(fd, va, (u64)size) == size) {
					long r = korin_sys3(SYS_BOOT_FILE, n, va, size);
					say("init : pour le noyau "); /* « init : pour le noyau /etc/options.txt -> 0 » */
					say(n);
					say(" -> ");
					say_dec(r);
					say("\n");
				}
				korin_sys1(SYS_BUF_FREE, b);
			}
		}
		sys_close(fd);
	}
}

/* le texte « pat » remplacé par « by » (pas plus long) dans s, sur place */
static void replace_in(char *s, const char *pat, const char *by)
{
	long lp = str_len(pat), lb = str_len(by);
	for (char *c = s; *c; c++) {
		long k = 0;
		while (k < lp && c[k] == pat[k])
			k++;
		if (k < lp)
			continue;
		for (long i = 0; i < lb; i++)
			c[i] = by[i];
		char *from = c + lp, *to = c + lb;
		while ((*to++ = *from++))
			;
		c += lb - 1;
	}
}

/* « A|B|… » : le premier service venu en t ms (A préféré) -> canal, ou < 0 ; found = son nom */
static long wait_any(char *names, long t, char *found, long max)
{
	char *alt[8];
	int n = 0;
	for (char *c = names; n < 8;) {
		alt[n++] = c;
		while (*c && *c != '|')
			c++;
		if (!*c)
			break;
		*c++ = 0;
	}
	long t0 = sys_time_ms(), ch = -1;
	int k = -1;
	for (;;) {
		for (int i = 0; i < n && k < 0; i++)
			if ((ch = korin_sys2(SYS_SVC_OPEN, alt[i], 0)) >= 0)
				k = i;
		if (k >= 0 || sys_time_ms() - t0 >= t)
			break;
		sys_sleep(50);
	}
	for (int i = 0; i < k; i++) { /* un nom préféré, publié un peu plus tard (même pilote) ? */
		long c2 = korin_sys2(SYS_SVC_OPEN, alt[i], 300);
		if (c2 >= 0) {
			ch = c2;
			k = i;
			break;
		}
	}
	found[0] = 0;
	if (k >= 0) { /* (avant de remettre les « | » : alt[k] finit encore à son nom) */
		long l = str_len(alt[k]);
		if (l >= max)
			l = max - 1;
		for (long i = 0; i < l; i++)
			found[i] = alt[k][i];
		found[l] = 0;
	}
	for (int i = 1; i < n; i++) /* les « | » remis : le motif sert au remplacement */
		alt[i][-1] = '|';
	return ch;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long n = korin_sys3(SYS_MOD_READ, "init.txt", liste, LISTE_MAX - 1); /* les services de démarrage */
	if (n < 0)
		say("init : pas de liste init.txt\n");
	else {
		liste[n] = 0;
		char *l = liste;
		while (*l) {
			char *e = l; /* fin de ligne -> 0 */
			while (*e && *e != 10 && *e != 13)
				e++;
			if (*e)
				*e++ = 0;
			if (*l && *l != '#') { /* vide, ou commentaire */
				char *args = 0; /* nom, puis arguments */
				for (char *c = l; *c; c++)
					if (*c == ' ') {
						*c = 0;
						args = c + 1;
						break;
					}
				if (!strcmp(l, "wait") && args) { /* « wait service ms » : attendre un service */
					char *ms = args;
					while (*ms && *ms != ' ')
						ms++;
					long t = 30000;
					if (*ms) {
						*ms++ = 0;
						t = 0;
						while (*ms >= '0' && *ms <= '9')
							t = t * 10 + (*ms++ - '0');
					}
					long t0 = sys_time_ms();
					long ch;
					int multi = 0;
					for (char *c = args; *c; c++)
						multi |= *c == '|';
					if (multi && str_len(args) < (long)sizeof alt_pat) {
						for (long i = 0; (alt_pat[i] = args[i]); i++)
							;
						ch = wait_any(args, t, alt_found, sizeof alt_found);
						if (!alt_found[0])
							alt_pat[0] = 0;
					} else
						ch = korin_sys2(SYS_SVC_OPEN, args, t);
					say("init : attente de "); /* « init : attente de blk.korin -> 3 (4210 ms) » */
					say(args);
					if (multi && alt_found[0]) {
						say(" : ");
						say(alt_found);
					}
					say(" -> ");
					say_dec(ch);
					say(" (");
					say_dec(sys_time_ms() - t0);
					say(" ms)\n");
					l = e;
					continue;
				}
				if (args && alt_pat[0])
					replace_in(args, alt_pat, alt_found);
				long pid = korin_sys2(SYS_MOD_LAUNCH, l, args);
				say("init : "); /* « init : sd -> 5 » */
				say(l);
				say(" -> ");
				say_dec(pid);
				say("\n");
			}
			l = e;
		}
	}
	for (int i = ATTENTE;; ) { /* / répond ? */
		if (sys_stat("/", infos) == 0)
			break;
		sys_sleep(100);
		if (!--i) {
			say("init : / ne repond pas\n");
			return 0;
		}
	}
	boot_files(); /* options.txt -> noyau */
	korin_sys0(SYS_BOOT_READY); /* le verrou est levé */
	say("init : / pret, verrou du demarrage leve\n");
	ksys(SYS_SPAWN, (long)"/sys/svc/init2", 0, 0, 0, 0, 0); /* étape 2, depuis le disque */
	return 0;
}
