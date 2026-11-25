/* fsowner.c : propriétaires des fichiers ouverts et tampons partagés par
 * process, communs à fatfs et ramfs ; traduction de src/svc/inc/fsowner.inc
 * (docs/FICHIERS.md, 4.2).
 *  * propriétaire : le pid de l'appelant (MSG_PID). vfs transmet les
 *    ouvertures des programmes (SVC_FORWARD) : MSG_PID est alors le programme
 *    lui-même, d = 0. Seule V_EXEC passe encore par un appel de vfs, avec le
 *    pid du programme en d ;
 *  * ouverture transmise par vfs : c = attributs à ajouter (ATTR_SYS,
 *    ATTR_LIB) ; la réponse porte en a le canal du service ;
 *  * un fichier ouvert a un ou plusieurs propriétaires (table proprios :
 *    couples numéro + 1, pid) ; après un fork, le fils tient les fichiers du
 *    père aux mêmes numéros ; le fichier se ferme avec son dernier ;
 *  * accès (ops 10-17, 26, fermer) : un propriétaire ou le noyau (pid 0 :
 *    avis, SVC_PID_KERNEL) ; sinon E_BADF ;
 *  * tampon (ops 14-16) : un par process client, FS_BUFFER_SIZE octets, créé
 *    par le service et accordé à ce pid ; rendu à l'avis de fin. */
#include "fsowner.h"
#include "caller.h"

long fs_canal;
static long pid_vfs;     /* pid du service vfs (connu à sa première ouverture) */
static long open_extras; /* attributs demandés par vfs (ouverture en cours) */
static struct {
	i64 pid, num;
} tampons[TP_MAX];
static u32 proprios[FOW_MAX][2]; /* numéro + 1 (0 : libre), pid */
static u8 proclist[PL_SIZE] __attribute__((aligned(8)));

long proprietaire(struct msg *m)
{
	long pid = m->pid;
	long d = m->d; /* (pid pour qui vfs ouvre) */
	if (!d)
		return pid;
	if (pid == pid_vfs)
		return d;
	if (pid == SVC_PID_KERNEL)
		return pid;
	if (korin_sys2(SYS_PROC_INFO, pid, proclist) != 0)
		return pid;
	if (*(u32 *)(proclist + 24) != 0x00736676) /* « vfs », 0 */
		return pid;
	if (!caller_rights(pid)) /* (le nom se choisit au lancement : vfs a aussi les droits de /sys) */
		return pid;
	pid_vfs = pid;
	return d;
}

void open_extras_take(struct msg *m)
{
	open_extras = m->c & (ATTR_SYS | ATTR_LIB);
}

void open_extras_put(struct msg *m)
{
	m->c |= open_extras;
	m->a = (u32)fs_canal; /* (comme l'asm : 32 bits) */
}

int fs_path_ok(long req, const struct msg *m)
{
	u64 op = m->op;
	if (!(op >= 1 && op <= 9) && op != 18) /* (FS_CACHE : 18) */
		return 1;
	long pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL || (pid_vfs && pid == pid_vfs))
		return 1;
	long info[4];
	long r = korin_sys2(SYS_SVC_REQ_INFO, req, (long)info);
	if (r == CALLER_E_NOSYS)
		return 1; /* (le Pi : pas d'uid, rien à protéger de plus que vfs) */
	if (r == 0 && info[1]) /* transmise (SVC_FORWARD) : par vfs, ou un autre service aux droits de /sys */
		return info[1] == pid_vfs || caller_rights(info[1]);
	if (caller_rights(pid))
		return 1; /* (vfs lui-même, avant sa première ouverture transmise) */
	return r == 0 ? info[2] == 0 : caller_uid(pid) == 0;
}

long fo_access(struct msg *m, long num)
{
	u32 pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL)
		return 0;
	return fo_own_find(num, pid) ? 0 : E_BADF;
}

u32 *fo_own_find(long num, long pid)
{
	u32 n = (u32)num + 1, p = (u32)pid;
	for (int i = 0; i < FOW_MAX; i++)
		if (proprios[i][0] == n && proprios[i][1] == p)
			return proprios[i];
	return 0;
}

long fo_own_add(long num, long pid)
{
	if (fo_own_find(num, pid))
		return 1;
	for (int i = 0; i < FOW_MAX; i++)
		if (!proprios[i][0]) {
			proprios[i][0] = (u32)num + 1;
			proprios[i][1] = (u32)pid;
			return 1;
		}
	return 0;
}

long fo_own_count(long num)
{
	u32 n = (u32)num + 1;
	long c = 0;
	for (int i = 0; i < FOW_MAX; i++)
		c += proprios[i][0] == n;
	return c;
}

void fo_own_clear(long num)
{
	u32 n = (u32)num + 1;
	for (int i = 0; i < FOW_MAX; i++)
		if (proprios[i][0] == n)
			proprios[i][0] = proprios[i][1] = 0;
}

long fo_own_close(struct msg *m, long num)
{
	u32 pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL) { /* le noyau : fermé pour tous */
		fo_own_clear(num);
		return 1;
	}
	u32 *o = fo_own_find(num, pid);
	if (o)
		o[0] = o[1] = 0;
	return fo_own_count(num) == 0;
}

void fo_own_end(long pid, void (*close_num)(long num))
{
	if (!pid)
		return;
	for (int i = 0; i < FOW_MAX; i++) {
		u32 n = proprios[i][0];
		if (!n || proprios[i][1] != (u32)pid)
			continue;
		proprios[i][0] = proprios[i][1] = 0;
		if (!fo_own_count(n - 1))
			close_num(n - 1);
	}
}

long fo_own_any_last(long pid, long (*fn)(long num))
{
	if (!pid)
		return 0;
	for (int i = 0; i < FOW_MAX; i++) {
		u32 n = proprios[i][0];
		if (n && proprios[i][1] == (u32)pid && fo_own_count(n - 1) == 1 && fn(n - 1))
			return 1;
	}
	return 0;
}

long fo_own_fork(long child, long parent)
{
	long refused = 0;
	if (!child || !parent)
		return 0;
	for (int i = 0; i < FOW_MAX; i++) {
		u32 n = proprios[i][0];
		if (!n || proprios[i][1] != (u32)parent)
			continue;
		if (!fo_own_add(n - 1, child))
			refused++;
	}
	return refused;
}

/* pid -> son entrée (pid 0 : une place libre), 0 : aucune */
static int tampon_entree(long pid)
{
	for (int i = 0; i < TP_MAX; i++)
		if (tampons[i].pid == pid)
			return i;
	return -1;
}

void *tampon_de(struct msg *m)
{
	long pid = m->pid;
	if (!pid)
		return 0;
	int i = tampon_entree(pid);
	if (i < 0)
		return 0;
	return (void *)(BUF_VA_BASE + (u64)tampons[i].num * BUF_SLOT);
}

long op_tampon(struct msg *m)
{
	long pid = m->pid;
	if (!pid || pid == SVC_PID_KERNEL)
		return E_INVAL;
	int i = tampon_entree(pid); /* déjà le sien ? */
	if (i < 0) {
		i = tampon_entree(0); /* une place libre */
		if (i < 0)
			return E_NOMEM;
		long num = korin_sys1(SYS_BUF_CREATE, FS_BUFFER_SIZE);
		if (num < 0)
			return num;
		tampons[i].pid = pid;
		tampons[i].num = num;
		korin_sys2(SYS_BUF_GRANT, num, pid);
	}
	m->a = tampons[i].num;
	return 0;
}

void tampons_fin(long pid)
{
	if (pid_vfs == pid)
		pid_vfs = 0;
	if (!pid)
		return;
	int i = tampon_entree(pid);
	if (i < 0)
		return;
	long num = tampons[i].num;
	tampons[i].pid = tampons[i].num = 0;
	korin_sys1(SYS_BUF_FREE, num); /* démappé ici : libéré à la dernière référence */
}
