/* caller.h : qui appelle un service (docs/etude/PLAN-CORRECTIONS-SECURITE.md, E17).
 *
 * Un service lit l'appelant d'une requête dans son message (pid en +44, posé par le noyau). Ces
 * fonctions décident d'un droit à partir de lui :
 *   - svc_from_kernel : un avis du noyau (fin, naissance, fork, exec, setuid, signal) a le pid 0 ;
 *     depuis le 25/11/2026 le noyau du PC refuse aussi à un process toute op >= 0xFFF0 ;
 *   - caller_uid : l'uid de l'appelant ; le noyau du Pi n'a pas d'uid (E_NOSYS) : 0, comme
 *     vfs/pids.c ; un appelant qui a disparu (E_SRCH), ou toute autre erreur : -1, c'est-à-dire un refus ;
 *   - caller_rights : 1 si l'appelant a les droits de /sys ;
 *   - caller_admin : droits de /sys ou uid 0 (les outils lancés par sudo : mkfs, gpt, osinstall).
 * Avec le numéro de la requête (req), l'uid est lu par SVC_REQ_INFO pendant que l'appelant attend sa
 * réponse : sans course sur la réutilisation des pids. */
#ifndef KORIN_SVC_CALLER_H
#define KORIN_SVC_CALLER_H

#include "korinrt.h"

#ifndef SYS_SVC_REQ_INFO
#define SYS_SVC_REQ_INFO 0x97
#endif

#define CALLER_E_NOSYS (-38)

/* le message vient-il du noyau ? (pid de l'appelant en +44 nul) */
static inline int svc_from_kernel(const void *msg)
{
	return *(const volatile unsigned int *)((const char *)msg + 44) == 0;
}

/* uid de l'appelant de la requête req (0 : pas de numéro, l'uid du pid seulement) ; -1 : refus */
static inline long caller_uid_req(long pid, long req)
{
	if (pid <= 0)
		return -1;
	if (req > 0) {
		long info[4];
		long r = korin_sys2(SYS_SVC_REQ_INFO, req, (long)info);
		if (r == 0)
			return info[2];
		if (r != CALLER_E_NOSYS && r != -22) /* (E_INVAL : requête déjà transmise, etc. : par le pid) */
			return -1;
	}
	long u = korin_sys1(SYS_PROC_UID, pid);
	if (u == CALLER_E_NOSYS)
		return 0; /* le Pi : pas d'uid */
	return u < 0 ? -1 : u;
}

static inline long caller_uid(long pid)
{
	return caller_uid_req(pid, 0);
}

/* l'appelant a-t-il les droits de /sys ? */
static inline int caller_rights(long pid)
{
	return pid > 0 && korin_sys1(SYS_PROC_RIGHTS, pid) == 1;
}

/* droits de /sys, ou uid 0 */
static inline int caller_admin(long pid, long req)
{
	return caller_rights(pid) || caller_uid_req(pid, req) == 0;
}

#endif
