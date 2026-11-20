#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
static int cb (void *u, int n, char **v, char **c) { printf ("  row: %s\n", v[0] ? v[0] : "(null)"); return 0; }
static void ex (sqlite3 *db, const char *sql)
{
	char *err = 0;
	int r = sqlite3_exec (db, sql, cb, 0, &err);
	printf ("%s -> %d %s (autocommit %d)\n", sql, r, err ? err : "", sqlite3_get_autocommit (db));
	sqlite3_free (err);
}
/* compte les « holdperm » du fichier, lu par morceaux de taille donnée (sans grep : le grep de Korin coupe une
 * ligne au premier octet nul) ; 1 Mo passe par FS_READ_DIRECT, 4 Ko par les messages et le tampon partagé */
static void scan (const char *p, size_t chunk)
{
	static char buf[1 << 21];
	int fd = open (p, O_RDONLY);
	if (fd < 0) { printf ("scan %zu: open %s failed\n", chunk, p); return; }
	size_t n = 0;
	for (;;)
	{
		ssize_t r = read (fd, buf + n, chunk);
		if (r <= 0) break;
		n += (size_t) r;
		if (n + chunk > sizeof buf) break;
	}
	close (fd);
	int c = 0;
	for (size_t i = 0; i + 8 <= n; i++)
		if (!memcmp (buf + i, "holdperm", 8)) c++;
	printf ("scan %zu: %zu bytes, holdperm x%d\n", chunk, n, c);
}
int main (int argc, char **argv)
{
	const char *p = argc > 1 ? argv[1] : "/home/sqltest.db";
	if (argc > 2 && !strcmp (argv[2], "scan"))
	{
		scan (p, 1 << 20);
		scan (p, 4096);
		scan (p, 100);
		return 0;
	}
	if (argc > 2 && !strcmp (argv[2], "read"))
	{
		sqlite3 *h;
		sqlite3_open (p, &h);
		ex (h, "SELECT k FROM t;");
		sqlite3_close (h);
		return 0;
	}
	if (argc > 2 && !strcmp (argv[2], "hold"))
	{
		/* comme le process réseau de WebKit : la base ouverte, une écriture, jamais fermée */
		sqlite3 *h;
		sqlite3_open (p, &h);
		ex (h, "PRAGMA synchronous = 1;");
		ex (h, "CREATE TABLE IF NOT EXISTS t (k TEXT PRIMARY KEY, v TEXT);");
		ex (h, "INSERT OR REPLACE INTO t VALUES ('holdperm', 'yes');");
		fflush (stdout);
		sleep (25);
		_exit (0);
	}
	sqlite3 *db;
	printf ("sqlite %s, %s\n", sqlite3_libversion (), p);
	int r = sqlite3_open (p, &db);
	printf ("open %d\n", r);
	ex (db, "PRAGMA journal_mode;");
	ex (db, "PRAGMA auto_vacuum = 2;");
	ex (db, "CREATE TABLE IF NOT EXISTS t (k TEXT PRIMARY KEY, v TEXT);");
	ex (db, "INSERT OR REPLACE INTO t VALUES ('jetperm', 'yes');");
	ex (db, "SELECT v FROM t;");
	sqlite3_close (db);
	struct stat st; stat (p, &st); printf ("size after close %ld\n", (long) st.st_size);
	sqlite3_open (p, &db);
	ex (db, "SELECT v FROM t;");
	sqlite3_close (db);
	return 0;
}
