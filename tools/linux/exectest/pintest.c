/* pintest -- l'hôte Linux (docs/etude/LINUX-HOST.md) : un chemin « épinglé » comme le fait Claude Code pour ses
 * fichiers de /tmp (sortie des commandes) : chaque dossier ouvert par openat (O_PATH | O_DIRECTORY | O_NOFOLLOW)
 * depuis son parent, puis fstat comparé à lstat (périphérique, inode, type) et /proc/self/fd/N comparé au chemin.
 * Une différence : Claude Code refuse (« tasks dir moved or linked »).
 *   linuxhost run pintest [chemin]     (défaut /tmp/pintest/a/b, créé) -> une ligne par dossier, RÉUSSI */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/tmp/pintest/a/b";
	if (argc <= 1)
	{
		mkdir ("/tmp/pintest", 0700);
		mkdir ("/tmp/pintest/a", 0700);
		mkdir ("/tmp/pintest/a/b", 0700);
	}
	int ok = 1, fd = open ("/", O_PATH | O_DIRECTORY);
	char cur[1024] = "";
	const char *p = path + 1;
	while (*p)
	{
		const char *e = strchr (p, '/');
		size_t n = e ? (size_t) (e - p) : strlen (p);
		char name[256];
		memcpy (name, p, n);
		name[n] = 0;
		snprintf (cur + strlen (cur), sizeof cur - strlen (cur), "/%s", name);
		int nfd = openat (fd, name, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		struct stat fs, ls;
		int r1 = nfd >= 0 ? fstat (nfd, &fs) : -1, r2 = lstat (cur, &ls);
		char link[1024] = "", fdp[64];
		snprintf (fdp, sizeof fdp, "/proc/self/fd/%d", nfd);
		ssize_t k = nfd >= 0 ? readlink (fdp, link, sizeof link - 1) : -1;
		if (k >= 0)
			link[k] = 0;
		int same = nfd >= 0 && r1 == 0 && r2 == 0 && fs.st_dev == ls.st_dev && fs.st_ino == ls.st_ino
			&& S_ISDIR (fs.st_mode) && S_ISDIR (ls.st_mode) && !strcmp (link, cur);
		printf ("pintest : %-24s fd %d ; fstat dev %lu ino %lu mode %o uid %u ; lstat dev %lu ino %lu mode %o ; lien %s : %s\n",
			cur, nfd, (unsigned long) fs.st_dev, (unsigned long) fs.st_ino, (unsigned) fs.st_mode, (unsigned) fs.st_uid,
			(unsigned long) ls.st_dev, (unsigned long) ls.st_ino, (unsigned) ls.st_mode, link, same ? "ok" : "DIFFÉRENT");
		ok &= same;
		if (nfd < 0)
		{
			printf ("pintest : openat %s : %s\n", name, strerror (errno));
			return 1;
		}
		close (fd);
		fd = nfd;
		p += n;
		while (*p == '/')
			p++;
	}
	printf ("pintest : %s\n", ok ? "RÉUSSI" : "ÉCHEC");
	return ok ? 0 : 1;
}
