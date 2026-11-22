/* fsbench - banc des recherches de noms du système de fichiers (docs/agents/FICHIERS-PERF.md,
 * « Comment vérifier ») :
 *	fsbench mk DOSSIER N            crée DOSSIER et N fichiers vides f0 ... f<N-1> (noms longs)
 *	fsbench run DOSSIER N P MS      P process, chacun MS millisecondes : open + close de noms
 *	                                présents et absents (moitié, moitié) et stat ; affiche les
 *	                                opérations par seconde de chacun et le total
 *	fsbench read FICHIER P MS       P process qui relisent FICHIER par blocs de 4 Ko dispersés
 *	                                (le débit du cache et du périphérique en parallèle)
 *	fsbench child ...               (un process de run ou read : sa ligne de résultat)
 * Sortie : « fsbench : <total> op/s (<P> process) » ; code 1 en cas d'erreur. */
#include "korinrt.h"

static char path[300], line[400];

static void put(const char *s)
{
	sys_print(s, strlen(s));
}

static void put_dec(u64 v)
{
	char b[24];
	put(u64_to_str(v, b) ? b : "?");
}

/* « DOSSIER/<longname><i> » ; absent : un autre préfixe */
static void name_of(const char *dir, u64 i, int absent)
{
	char num[24];
	u64_to_str(i, num);
	path[0] = 0;
	str_cat(str_cat(str_cat(str_cat(path, dir), absent ? "/Absent_header_file_" : "/Present_header_file_"), num), ".h");
}

static u64 next_rand(u64 *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 7;
	*s ^= *s << 17;
	return *s;
}

static int mk(const char *dir, u64 n)
{
	sys_mkdir(dir);
	for (u64 i = 0; i < n; i++) {
		name_of(dir, i, 0);
		long fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
		if (fd < 0) {
			put("fsbench : création refusée : ");
			put(path);
			put("\n");
			return 1;
		}
		sys_close(fd);
	}
	put("fsbench : ");
	put_dec(n);
	put(" fichiers dans ");
	put(dir);
	put("\n");
	return 0;
}

static int child_names(const char *dir, u64 n, long ms)
{
	u64 seed = (u64)sys_getpid() * 2654435761u + 1, ops = 0, errs = 0, bad[4] = {0, 0, 0, 0};
	long first = 0;
	long end = sys_time_ms() + ms;
	while (sys_time_ms() < end) {
		for (int k = 0; k < 64; k++) {
			u64 r = next_rand(&seed);
			int absent = (r >> 32) & 1;
			name_of(dir, r % n, absent);
			if ((r >> 40) & 1) {
				long st[2];
				long x = sys_stat(path, st);
				if ((x < 0) != absent) {
					errs++;
					bad[2 + absent]++;
					if (!first) {
						first = x ? x : 1;
						put("fsbench : stat ");
						put(path);
						put(x < 0 ? " : erreur\n" : " : trouvé\n");
					}
				}
			} else {
				long fd = sys_open(path, O_READ);
				if ((fd < 0) != absent) {
					errs++;
					bad[absent]++;
					if (!first) {
						first = fd ? fd : 1;
						put("fsbench : open ");
						put(path);
						put(fd < 0 ? " : erreur\n" : " : trouvé\n");
					}
				}
				if (fd >= 0)
					sys_close(fd);
			}
			ops++;
		}
	}
	put("fsbench :   process ");
	put_dec((u64)sys_getpid());
	put(" : ");
	put_dec(ops * 1000 / (u64)ms);
	put(" op/s");
	if (errs) {
		put(", ");
		put_dec(errs);
		put(" RÉSULTATS FAUX (open présent ");
		put_dec(bad[0]);
		put(", open absent ");
		put_dec(bad[1]);
		put(", stat présent ");
		put_dec(bad[2]);
		put(", stat absent ");
		put_dec(bad[3]);
		put(")");
	}
	put("\n");
	return errs ? 1 : 0;
}

static int child_read(const char *file, long ms)
{
	static u8 buf[4096];
	long fd = sys_open(file, O_READ);
	if (fd < 0)
		return 1;
	long size = sys_seek(fd, 0, 2);
	if (size < 4096)
		return 1;
	u64 seed = (u64)sys_getpid() * 2654435761u + 7, ops = 0;
	long end = sys_time_ms() + ms;
	while (sys_time_ms() < end) {
		u64 blocks = (u64)size / 4096;
		sys_seek(fd, (long)((next_rand(&seed) % blocks) * 4096), 0);
		if (sys_read(fd, buf, 4096) != 4096)
			return 1;
		ops++;
	}
	sys_close(fd);
	put("fsbench :   process ");
	put_dec((u64)sys_getpid());
	put(" : ");
	put_dec(ops * 1000 / (u64)ms);
	put(" lectures de 4 Ko/s\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 4 && str_eq(argv[1], "mk"))
		return mk(argv[2], (u64)atoi(argv[3]));
	if (argc >= 5 && str_eq(argv[1], "child"))
		return str_eq(argv[2], "read") ? child_read(argv[3], atoi(argv[4]))
					       : child_names(argv[3], (u64)atoi(argv[4]), atoi(argv[5]));
	int names = argc >= 6 && str_eq(argv[1], "run"), rd = argc >= 5 && str_eq(argv[1], "read");
	if (!names && !rd) {
		put("fsbench mk DOSSIER N | run DOSSIER N P MS | read FICHIER P MS\n");
		return 1;
	}
	long p = atoi(argv[rd ? 3 : 4]), ms = atoi(argv[rd ? 4 : 5]);
	if (p < 1 || p > 64 || ms < 100)
		return 1;
	long pids[64];
	long t0 = sys_time_ms();
	for (long i = 0; i < p; i++) {
		line[0] = 0;
		if (names)
			str_cat(str_cat(str_cat(str_cat(str_cat(str_cat(line, "child names "), argv[2]), " "), argv[3]), " "), argv[5]);
		else
			str_cat(str_cat(str_cat(str_cat(line, "child read "), argv[2]), " "), argv[4]);
		pids[i] = f_spawn("/bin/fsbench", line);
		if (pids[i] < 0) {
			put("fsbench : lancement refusé\n");
			return 1;
		}
	}
	int bad = 0;
	for (long i = 0; i < p; i++)
		if (korin_sys1(SYS_WAIT, pids[i]) != 0)
			bad = 1;
	put("fsbench : ");
	put_dec((u64)(sys_time_ms() - t0));
	put(" ms pour ");
	put_dec((u64)p);
	put(" process");
	put(bad ? " (ERREURS)\n" : "\n");
	return bad;
}
