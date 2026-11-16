/* notify.c : lot « sys » (docs/BIN-C.md) : envoyer une notification à notifyd, comme
 * src/inc/notify.inc (notifier, notifier_app) et src/inc/preload_notify.inc (preload_name,
 * preload_notify) ; pour notify et preload. */
#include "korinrt.h"

#define NOTIFY_OP 1 /* titre, 0, texte, 0 [, app, 0] -> 0 */

/* titre, texte, app (0 : le process appelant) -> 0, ou < 0 si notifyd est absent
 * (notifier_app). Le message est coupé à 463 octets de données, comme l'asm ; ses
 * autres champs sont à zéro (l'asm laissait ce qu'il y avait sur la pile). */
long notify_send(const char *title, const char *text, const char *app)
{
	u8 msg[MSG_SIZE];
	long ch = korin_sys2(SYS_SVC_OPEN, "notify", 0); /* (sans attendre) */
	if (ch < 0)
		return ch;
	memset(msg, 0, sizeof msg);
	*(u64 *)msg = NOTIFY_OP;
	char *d = (char *)msg + MSG_DATA, *lim = (char *)msg + MSG_DATA + 463;
	const char *s = title;
	for (;;) {
		char c = *s++;
		*d++ = c;
		if (!c)
			break;
		if (d >= lim) {
			d[-1] = 0;
			break;
		}
	}
	s = text;
	for (;;) {
		if (d >= lim)
			goto cut;
		char c = *s++;
		*d++ = c;
		if (!c)
			break;
	}
	if (app) {
		s = app;
		for (;;) {
			if (d >= lim)
				goto cut;
			char c = *s++;
			*d++ = c;
			if (!c)
				break;
		}
	}
	*d = 0; /* (pas d'app : un 0 de plus) */
	goto send;
cut:
	*lim = 0;
send:
	return korin_sys2(SYS_SVC_CALL, ch, msg);
}

/* chemin (« /apps/jet.app/main », « /bin/fasmarm »), out (64 o) : le nom (« jet »,
 * « fasmarm ») -> sa fin (preload_name) */
char *preload_name(const char *path, char *out)
{
	const char *e = path;
	while (*e)
		e++;
	const char *b = e; /* début du dernier composant */
	while (b > path && b[-1] != '/')
		b--;
	if (e - b == 4 && b[0] == 'm' && b[1] == 'a' && b[2] == 'i' && b[3] == 'n' && b > path) {
		e = b - 1; /* « main » : le paquet */
		b = e;
		while (b > path && b[-1] != '/')
			b--;
		if (e - b > 4 && e[-4] == '.' && e[-3] == 'a' && e[-2] == 'p' && e[-1] == 'p')
			e -= 4;
	}
	long n = 63;
	while (b < e && n) {
		*out++ = *b++;
		n--;
	}
	*out = 0;
	return out;
}

/* chemin : notification « Jet est préchargé » (preload_notify) */
void preload_notify(const char *path)
{
	char t[96];
	char *e = preload_name(path, t);
	if ((u8)(t[0] - 'a') <= 25) /* une majuscule au début */
		t[0] -= 32;
	const char *s = " est préchargé";
	while ((*e++ = *s++))
		;
	notify_send("Préchargement", t, 0);
}
