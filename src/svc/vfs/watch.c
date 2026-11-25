/* watch.c : le guet d'un dossier (VFS_WATCH, 25/11/2026), à la manière d'inotify de Linux, sur lequel GNOME
 * (GFileMonitor de GIO, Nautilus) suit le dossier qu'il montre : un programme demande à guetter un dossier,
 * vfs le prévient, lui seul, quand un nom y change.
 *  * VFS_WATCH : a = rang du guet du programme (0..W_SLOTS-1 : une vue de Fichiers), données : le dossier tel que le
 *    programme l'écrit (« HOME:/music », relatif...) ; vide : le guet est retiré. Le droit de lire le dossier est
 *    vérifié (access.c). Un programme qui se termine perd ses guets.
 *  * Ce que vfs voit (ops_chemins, chemin_renommer) : création ou ouverture en écriture, mkdir, suppression,
 *    renommage (les deux dossiers), date, droits. Pas les écritures elles-mêmes ni la fermeture : elles vont droit
 *    au service du volume (le programme relit un peu plus tard pour la taille finale).
 *  * L'avis : « korin.fs.watch.<pid> » sur le bus (pas gardé), valeur : les rangs changés (« 0 », « 0 1 »),
 *    W_DELAY ms après le premier changement (les changements rapprochés n'en font qu'un : une copie, gcc, git...).
 *    Le sujet ne dit que « ton dossier a changé », jamais lequel ni ce qui a changé. */
#include "vfs.h"

#define W_MAX 32     /* guets de tout le système */
#define W_SLOTS 8    /* par programme */
#define W_DELAY 200  /* ms entre le premier changement et l'avis */

struct guet {
	long pid;          /* 0 : libre */
	long slot;
	u64 due;           /* l'avis à envoyer (0 : rien en attente) */
	char dir[PATH_MAX]; /* forme interne (« /root/home/music ») */
};
static struct guet guets[W_MAX];

/* deux chemins internes égaux, sans casse (FAT) ; n : la longueur de a à comparer */
static int meme(const char *a, u64 n, const char *b)
{
	for (u64 i = 0; i < n; i++) {
		u8 x = (u8)a[i], y = (u8)b[i];
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y || !y)
			return 0;
	}
	return !b[n];
}

/* (msg) VFS_WATCH -> 0, ou E_INVAL, E_ACCES, E_NOMEM */
long watch_op(void)
{
	long pid = msg.pid, slot = msg.a;
	if ((u64)slot >= W_SLOTS)
		return E_INVAL;
	((u8 *)&msg)[MSG_SIZE - 1] = 0;
	struct guet *g = 0, *libre = 0;
	for (long i = 0; i < W_MAX; i++) {
		if (guets[i].pid == pid && guets[i].slot == slot)
			g = &guets[i];
		else if (!guets[i].pid && !libre)
			libre = &guets[i];
	}
	if (!msg.data[0]) { /* retiré */
		if (g)
			g->pid = 0;
		return 0;
	}
	long r = resoudre(pid, (char *)msg.data, resolu);
	if (r)
		return r;
	r = acces_verifier(pid, resolu, 0); /* (le droit de lire le dossier) */
	if (r)
		return r;
	if (!g)
		g = libre;
	if (!g)
		return E_NOMEM;
	g->pid = pid;
	g->slot = slot;
	g->due = 0;
	strcpy(g->dir, resolu);
	msg.len = 0;
	return 0;
}

/* un nom a changé (forme interne) : les guets de son dossier attendent leur avis */
void watch_touch(const char *interne)
{
	u64 n = 0, last = 0;
	for (; interne[n]; n++)
		if (interne[n] == '/')
			last = n;
	if (!last) /* (« /root » : la racine d'un volume elle-même) */
		return;
	u64 now = 0;
	for (long i = 0; i < W_MAX; i++) {
		struct guet *g = &guets[i];
		if (!g->pid || g->due || !meme(interne, last, g->dir))
			continue;
		if (!now)
			now = korin_sys0(SYS_TIME_MS);
		g->due = now + W_DELAY;
	}
}

/* un programme terminé : ses guets sont rendus */
void watch_pid_gone(long pid)
{
	for (long i = 0; i < W_MAX; i++)
		if (guets[i].pid == pid)
			guets[i].pid = 0;
}

static char *put_dec(char *d, u64 v)
{
	char t[24];
	int n = 0;
	do
		t[n++] = (char)('0' + v % 10);
	while ((v /= 10));
	while (n)
		*d++ = t[--n];
	*d = 0;
	return d;
}

/* (verrou tenu) les avis dus envoyés -> ms jusqu'au prochain (-1 : aucun en attente) */
long watch_flush(void)
{
	u64 now = 0;
	long next = -1;
	for (long i = 0; i < W_MAX; i++) {
		struct guet *g = &guets[i];
		if (!g->pid || !g->due)
			continue;
		if (!now)
			now = korin_sys0(SYS_TIME_MS);
		if (g->due > now) {
			long w = (long)(g->due - now);
			if (next < 0 || w < next)
				next = w;
			continue;
		}
		/* un avis par programme : tous ses rangs dus à la fois */
		static const char pre[] = "korin.fs.watch.";
		char topic[48], val[2 * W_SLOTS + 2], *v = val;
		for (u64 k = 0; k < sizeof pre; k++)
			topic[k] = pre[k];
		put_dec(topic + sizeof pre - 1, (u64)g->pid);
		for (long k = i; k < W_MAX; k++) {
			struct guet *h = &guets[k];
			if (h->pid != g->pid || !h->due || h->due > now)
				continue;
			if (v != val)
				*v++ = ' ';
			v = put_dec(v, (u64)h->slot);
			if (h != g)
				h->due = 0;
		}
		g->due = 0;
		bus_publier(topic, val);
	}
	return next;
}
