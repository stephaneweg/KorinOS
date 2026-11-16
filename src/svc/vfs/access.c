/* access.c : les règles d'accès par chemin (docs/etude/MULTI-UTILISATEUR.md §2.3, étape 3). FAT32 et exFAT
 * ne gardent pas de propriétaire : les règles dépendent du chemin (externe) et de l'uid de l'appelant.
 *
 *   uid 0 (system)                         tout
 *   /tmp, /media/..., /net/...             tout le monde (propriétaire du montage : plus tard)
 *   le dossier personnel du compte          lui (lecture, écriture)
 *   /home/public                           tout le monde
 *   le dossier personnel d'un autre compte  rien (question 3 de l'étude : 0700 proposé)
 *   /etc/shadow.ini, wifi.txt, remote.txt, smbd.ini   uid 0 seulement (même en lecture)
 *   le reste (/bin, /apps, /sys, /etc, /var, /lib, /res, /devtools...)   lecture pour tous, écriture uid 0
 *
 * Mode (« access = » de /etc/system.ini, homes.c) : « log » (le défaut de l'étape 3) note les refus au
 * journal et laisse faire ; « enforce » refuse (E_ACCES) ; « off » ne regarde rien. */
#include "vfs.h"

#define E_ACCES -13
#define NOTES 32 /* refus déjà notés (le journal ne répète pas le même) */

static const char *const secrets[] = {"/etc/shadow.ini", "/etc/wifi.txt", "/etc/remote.txt", "/etc/smbd.ini", 0};

/* le chemin commence-t-il par le dossier d (« /home » couvre « /home » et « /home/a », pas « /homex ») ? */
static int sous(const char *p, const char *d)
{
	u64 n = strlen(d);
	if (n == 1 && d[0] == '/')
		return 1;
	return !strncmp(p, d, n) && (p[n] == 0 || p[n] == '/');
}

/* uid, chemin externe, écriture ? -> 1 permis */
static int permis(long uid, const char *p, int ecrire)
{
	if (uid == 0)
		return 1;
	for (int i = 0; secrets[i]; i++)
		if (!strcmp(p, secrets[i]))
			return 0;
	if (sous(p, "/tmp") || sous(p, "/media") || sous(p, "/net") || sous(p, "/home/public"))
		return 1;
	const char *mine = home_externe(uid);
	if (sous(p, mine))
		return 1;
	if (sous(p, "/home")) /* le dossier d'un autre compte (ou /home lui-même, s'il n'est pas le sien) */
		return home_autre(p, uid) ? 0 : !ecrire;
	return !ecrire;
}

/* le propriétaire d'un chemin (même découpage que permis) pour V_STATX : bits 32-55 = uid + 1, bits
 * 56-57 = genre (0 : système, lecture pour tous ; 1 : privé, dossier d'un compte ; 2 : public) ; libposix en
 * fait st_uid et st_mode. 0 : rien à dire (règles désactivées). */
u64 acces_bits(const char *interne)
{
	if (acces_mode() == ACCESS_OFF)
		return 0;
	char p[PATH_MAX];
	chemin_externe(interne, p);
	long owner = 0;
	u64 kind = 0;
	if (sous(p, "/tmp") || sous(p, "/media") || sous(p, "/net") || sous(p, "/home/public")) {
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

/* pid, chemin interne résolu, écriture ? -> 0, ou E_ACCES (mode enforce) ; un refus est noté */
long acces_verifier(long pid, const char *interne, int ecrire)
{
	int mode = acces_mode();
	if (mode == ACCESS_OFF)
		return 0;
	long uid = pid_uid(pid);
	if (uid == 0)
		return 0;
	char ext[PATH_MAX];
	chemin_externe(interne, ext);
	if (permis(uid, ext, ecrire))
		return 0;
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
