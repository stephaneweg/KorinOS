/* access.c : les règles d'accès par chemin (docs/etude/MULTI-UTILISATEUR.md §2.3, étape 3). FAT32 et exFAT
 * ne gardent pas de propriétaire : les règles dépendent du chemin (externe) et de l'uid de l'appelant.
 *
 *   uid 0 (system)                         tout
 *   /tmp, /media/..., /net/...             tout le monde (propriétaire du montage : plus tard)
 *   /tmp/<uid>                             ce compte seulement (TMPDIR de libposix, E27)
 *   le dossier personnel du compte          lui (lecture, écriture)
 *   /home/public                           tout le monde
 *   le dossier personnel d'un autre compte  rien (question 3 de l'étude : 0700 proposé)
 *   /etc/shadow.ini, wifi.txt, wifi.bak, remote.txt, smbd.ini, smbfs.ini, /etc/web/certs
 *                                          uid 0 seulement (même en lecture)
 *   /var/keyvault, /etc/keyvault           le coffre à secrets (docs/KEYVAULT.md) : uid 0 seulement, et refusé
 *                                          même en mode « log » (rien ne les lit, le coffre seul)
 *   le reste (/bin, /apps, /sys, /etc, /var, /lib, /res, /devtools...)   lecture pour tous, écriture uid 0
 *
 * Mode (« access = » de /etc/system.ini, homes.c) : « log » (le défaut de l'étape 3) note les refus au
 * journal et laisse faire ; « enforce » refuse (E_ACCES) ; « off » ne regarde rien. */
#include "vfs.h"

#define E_ACCES -13
#define NOTES 32 /* refus déjà notés (le journal ne répète pas le même) */

static const char *const secrets[] = {"/etc/shadow.ini", "/etc/wifi.txt", "/etc/wifi.bak", "/etc/remote.txt",
				      "/etc/smbd.ini", "/etc/smbfs.ini", "/etc/web/certs", 0};
/* refusés aux autres que l'uid 0 quel que soit le mode (sauf « off ») : les fichiers du coffre */
static const char *const toujours[] = {"/var/keyvault", "/etc/keyvault", "/etc/shadow.ini", 0};
/* en écriture, aux autres que l'uid 0, quel que soit le mode (sauf « off ») : les marques de session de /tmp,
 * écrites par login, Elegant et init2 (uid 0) ; lues par tous. /tmp/session.ini dit sous quel compte vont la
 * console, telnet et startup.txt : un compte qui l'écrivait y mettait l'uid 0 ou celui d'un autre (A1, E29) */
static const char *const marques[] = {"/tmp/session.ini", "/tmp/login.halt", "/tmp/login.manual",
				      /* et les comptes : qui est qui, qui peut s'élever (useradd, usermod, userdel par sudo,
				       * Réglages élevé, osinstall : tous en uid 0) ; en mode journal, un compte se donnait
				       * l'uid 0 dans users.ini, ou effaçait shadow.ini (plus de mot de passe nulle part) */
				      "/etc/users.ini", "/etc/elevate.ini",
				      /* ce qui décide de ce qui tourne en uid 0 ou avec les droits : le démarrage (init2,
				       * pcid, hwmanager), les services, la session, l'accès à distance, les programmes du
				       * système et les clés des paquets ; leurs écrivains sont pkg, osinstall et Réglages
				       * élevé (A1, S5, chaînes C1 et C2 sans attendre « enforce ») */
				      "/etc/init.txt", "/etc/services.txt", "/etc/services.off", "/etc/services.manual",
				      "/etc/services.deps", "/etc/preload.txt", "/etc/drivers.txt", "/etc/exec.txt",
				      "/etc/paths.txt", "/etc/session", "/etc/remote.txt", "/etc/pkg", "/etc/interpreters.ini",
				      "/sys", "/sbin", "/bin", "/lib", "/korin", "/EFI",
				      /* les paquets : ce qui est étagé (/var/pkg/stage) est installé par init2 en uid 0 au
				       * démarrage ; la base dit ce qui est installé (K1) */
				      "/var/pkg", 0};

/* le chemin commence-t-il par le dossier d (« /home » couvre « /home » et « /home/a », pas « /homex ») ?
 * Sans la casse : FAT32 et exFAT trouvent « /ETC/SHADOW.INI » comme « /etc/shadow.ini » (E24, F1) */
static int sous(const char *p, const char *d)
{
	u64 n = strlen(d);
	if (n == 1 && d[0] == '/')
		return 1;
	return nom_egal_n(p, d, n) && (p[n] == 0 || p[n] == '/');
}

/* le chemin externe tel que les règles le lisent : chaque composant sans ses points ni ses espaces de
 * fin (FAT et Windows les ignorent : « shadow.ini. ») ; un composant avec « ~ » sous /etc ou /var (un
 * nom court 8.3 de FAT, « SHADOW~1.INI ») devient « ~ », que seules les règles de l'uid 0 laissent passer */
static void normaliser(const char *p, char *o)
{
	char *d = o;
	while (*p) {
		if (*p == '/') {
			*d++ = *p++;
			continue;
		}
		char *c = d;
		int tilde = 0;
		while (*p && *p != '/') {
			tilde |= *p == '~';
			*d++ = *p++;
		}
		while (d > c && (d[-1] == '.' || d[-1] == ' '))
			d--;
		if (tilde) {
			d = c;
			*d++ = '~';
		}
	}
	*d = 0;
}

static int court_protege(const char *p)
{
	if (!(sous(p, "/etc") || sous(p, "/var")))
		return 0;
	for (; *p; p++)
		if (p[0] == '~' && (p[1] == 0 || p[1] == '/') && p[-1] == '/')
			return 1;
	return 0;
}

/* « /tmp/<nombre>[/...] » -> le nombre (l'uid du compte dont c'est le dossier temporaire) ; -1 sinon */
static long tmp_uid(const char *p)
{
	if (!sous(p, "/tmp") || p[4] != '/')
		return -1;
	const char *c = p + 5;
	long n = 0;
	int k = 0;
	while (*c >= '0' && *c <= '9' && k < 10) {
		n = n * 10 + (*c++ - '0');
		k++;
	}
	return k && (*c == 0 || *c == '/') ? n : -1;
}

/* uid, chemin externe, écriture ? -> 1 permis */
static int permis(long uid, const char *p, int ecrire)
{
	if (uid == 0)
		return 1;
	for (int i = 0; secrets[i]; i++)
		if (sous(p, secrets[i]))
			return 0;
	for (int i = 0; toujours[i]; i++)
		if (sous(p, toujours[i]))
			return 0;
	for (int i = 0; ecrire && marques[i]; i++)
		if (sous(p, marques[i]))
			return 0;
	if (court_protege(p))
		return 0; /* un nom court 8.3 sous /etc ou /var : peut-être un secret */
	{
		long t = tmp_uid(p); /* (le dossier temporaire d'un compte : à lui seul, même en lecture ; un autre ne
				      * peut pas le créer d'avance ni y poser un fichier qu'il ouvrirait : D11) */
		if (t >= 0)
			return t == uid;
	}
	if (sous(p, "/tmp") || sous(p, "/media") || sous(p, "/net") || sous(p, "/home/public"))
		return 1;
	if (sous(p, "/home/system"))
		return 0; /* (le compte system : à l'uid 0) */
	/* le dossier d'un autre compte d'abord : un compte dont le dossier est /home (images de
	 * développement) ne passe pas par-dessus ceux des autres */
	if (sous(p, "/home") && home_autre(p, uid))
		return 0;
	const char *mine = home_compte(uid); /* (un uid sans compte n'a pas de dossier à lui) */
	if (mine && sous(p, mine))
		return 1;
	return !ecrire;
}

/* le propriétaire d'un chemin (même découpage que permis) pour V_STATX : bits 32-55 = uid + 1, bits
 * 56-57 = genre (0 : système, lecture pour tous ; 1 : privé, dossier d'un compte ; 2 : public) ; libposix en
 * fait st_uid et st_mode. 0 : rien à dire (règles désactivées). */
u64 acces_bits(const char *interne)
{
	if (acces_mode() == ACCESS_OFF)
		return 0;
	char e[PATH_MAX], p[PATH_MAX];
	chemin_externe(interne, e);
	normaliser(e, p);
	long owner = 0;
	u64 kind = 0;
	if (tmp_uid(p) >= 0) {
		owner = tmp_uid(p);
		kind = 1;
	} else if (sous(p, "/tmp") || sous(p, "/media") || sous(p, "/net") || sous(p, "/home/public")) {
		owner = session_uid_de(); /* (pas de propriétaire : celui de la session) */
		kind = 2;
	} else if (sous(p, "/home")) {
		owner = home_proprio(p);
		kind = owner > 0 ? 1 : 0;
	}
	if (owner < 0)
		owner = 0;
	return ((u64)(owner + 1) << 32) | (kind << 56);
}

static u32 notes[NOTES];
static int note_next;

/* une seule ligne au journal par (uid, chemin, sens) récent */
static int deja_note(long uid, const char *p, int ecrire)
{
	u32 h = 2166136261u ^ (u32)uid ^ (u32)(ecrire << 31);
	for (; *p; p++)
		h = (h ^ (u8)*p) * 16777619u;
	for (int i = 0; i < NOTES; i++)
		if (notes[i] == h)
			return 1;
	notes[note_next] = h;
	note_next = (note_next + 1) % NOTES;
	return 0;
}

static void dec(long v, char *o)
{
	char t[24];
	int n = 0;
	if (v < 0) {
		*o++ = '-';
		v = -v;
	}
	do
		t[n++] = (char)('0' + v % 10);
	while ((v /= 10) && n < 23);
	while (n)
		*o++ = t[--n];
	*o = 0;
}

/* V_EXEC sous /sbin : l'uid 0 seulement (sudo) ; un refus est noté (« sudo <commande> »), E_ACCES en
 * mode enforce */
long acces_sbin(long pid, const char *interne)
{
	const char *s = sp_sbin;
	const char *p = interne;
	while (*s && (*p | 0x20) == (*s | 0x20))
		p++, s++;
	if (*s || !*p)
		return 0;
	int mode = acces_mode();
	if (mode == ACCESS_OFF || pid_uid(pid) == 0)
		return 0;
	char ext[PATH_MAX];
	chemin_externe(interne, ext);
	if (!deja_note(pid_uid(pid), ext, 2)) {
		ak_puts(mode == ACCESS_ENFORCE ? "vfs : commande systeme refusee (sudo " :
						 "vfs : commande systeme sans sudo (journal seulement) : sudo ");
		ak_puts(p);
		ak_puts(mode == ACCESS_ENFORCE ? ")\n" : "\n");
	}
	return mode == ACCESS_ENFORCE ? E_ACCES : 0;
}

/* pid, chemin interne résolu, écriture ? -> 0, ou E_ACCES (mode enforce) ; un refus est noté */
/* les listes qui valent dans tous les modes (même « off » : /etc/system.ini, que Réglages écrit sans élévation,
 * ne doit pas pouvoir tout ouvrir) : le coffre, shadow.ini, et en écriture les marques et fichiers du système */
static int critique(const char *p, int ecrire)
{
	if (tmp_uid(p) >= 0)
		return 1; /* (/tmp/<uid> : nouveau, rien n'y comptait sur l'accès des autres) */
	for (int i = 0; toujours[i]; i++)
		if (sous(p, toujours[i]))
			return 1;
	for (int i = 0; ecrire && marques[i]; i++)
		if (sous(p, marques[i]))
			return 1;
	return court_protege(p);
}

long acces_verifier(long pid, const char *interne, int ecrire)
{
	int mode = acces_mode();
	long uid = pid_uid(pid);
	if (uid == 0)
		return 0;
	char brut[PATH_MAX], ext[PATH_MAX];
	chemin_externe(interne, brut);
	normaliser(brut, ext);
	if (mode == ACCESS_OFF && !critique(ext, ecrire))
		return 0;
	if (permis(uid, ext, ecrire))
		return 0;
	for (int i = 0; toujours[i]; i++)
		if (sous(ext, toujours[i]))
			mode = ACCESS_ENFORCE; /* (le coffre : refusé même en mode journal) */
	for (int i = 0; ecrire && marques[i]; i++)
		if (sous(ext, marques[i]))
			mode = ACCESS_ENFORCE; /* (les marques de session aussi) */
	if (court_protege(ext) || tmp_uid(ext) >= 0)
		mode = ACCESS_ENFORCE; /* (un nom court 8.3 sous /etc ou /var : peut-être shadow.ini ; /tmp/<uid>) */
	if (!deja_note(uid, ext, ecrire)) {
		char n[24], nom[64];
		u64 info[8] = {0};
		nom[0] = 0;
		if (korin_sys2(SYS_PROC_INFO, pid, info) == 0) {
			memcpy(nom, &info[3], 32);
			nom[32] = 0;
		}
		ak_puts(mode == ACCESS_ENFORCE ? "vfs : acces refuse : uid " : "vfs : acces refuse (journal seulement) : uid ");
		dec(uid, n);
		ak_puts(n);
		ak_puts(", ");
		ak_puts(nom[0] ? nom : "?");
		ak_puts(" (");
		dec(pid, n);
		ak_puts(n);
		ak_puts(ecrire ? ") ecrit " : ") lit ");
		ak_puts(ext);
		ak_puts("\n");
	}
	return mode == ACCESS_ENFORCE ? E_ACCES : 0;
}
