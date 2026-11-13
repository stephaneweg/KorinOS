/* shell.c : le bus de la coquille (shell.inc ; busd, docs/BUS.md ; contrat :
 * docs/MODE-POCHE.md). Elegant parle à busd directement (src/svc/inc/buspub.inc, sans BusKit) :
 *	korin.shell.mode	(gardé) « desktop » ou « poche », publié dès que busd répond
 *				(Elegant démarre avant lui)
 *	korin.shell.home,	publiés par les touches système du mode poche ;
 *	korin.shell.switch,	korin.shell.home est aussi suivi (le losange de la barre de menus,
 *	korin.shell.quick,	le glyphe ‒ du cadre) : l'événement revient dans la file d'événements
 *	korin.shell.sleep	d'Elegant (la même que celle d'un programme), lue à chaque tour
 * Un mode donne à sh_tick sa fonction pour les événements (un événement de 32 octets, WEV_BUS ;
 * 0 : aucune). */
#include "elegant.h"

#define SH_RETRY 500 /* ms entre deux essais d'ouvrir busd */

long bp_canal; /* canal de busd + 1 (buspub.inc) */
static u64 sh_try; /* prochain essai d'ouverture (ms) */
long sh_sub_home, sh_sub_theme; /* abonnements */
static u8 sh_ev[32] __attribute__((aligned(16)));

const char sh_topic_mode[] = "korin.shell.mode";
const char sh_topic_home[] = "korin.shell.home";
const char sh_topic_switch[] = "korin.shell.switch";
const char sh_topic_quick[] = "korin.shell.quick";
const char sh_topic_sleep[] = "korin.shell.sleep";
const char sh_topic_theme[] = "korin.theme";
const char sh_val_1[] = "1";

/* fonction des événements du bus (0 : aucune) : à chaque tour */
void sh_tick(sh_ev_fn fn)
{
	if (!bp_canal) {
		u64 now = (u64)korin_sys0(SYS_TIME_MS); /* busd pas encore là : un essai de temps en
							   temps */
		if (now < sh_try)
			return;
		sh_try = now + SH_RETRY;
		if (!bp_open(0))
			return;
		bp_publish(sh_topic_mode, policy->name, BUS_RETAIN); /* le mode, gardé */
		if (policy->flags & POLF_POCKET) { /* les sujets suivis par le mode */
			sh_sub_home = sh_subscribe(sh_topic_home, 0);
			sh_sub_theme = sh_subscribe(sh_topic_theme, BUS_CURRENT);
		}
	}
	for (;;) { /* les événements en attente */
		long t = korin_sys2(SYS_EV_WAIT, sh_ev, 0);
		if (t <= 0)
			return;
		if (t == WEV_BUS && fn)
			fn(sh_ev);
	}
}

/* sujet, valeur (chaîne) : publiée, non gardée -> 1 si busd l'a prise (0 : pas de bus, à faire
 * sans lui) */
long sh_publish(const char *topic, const char *val)
{
	return bp_publish(topic, val, 0) >= 0;
}

/* motif, drapeaux (BUS_CURRENT) -> abonnement (<= 0 : aucun) */
long sh_subscribe(const char *pattern, long flags)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	M64(m, 0) = BUS_OP_SUBSCRIBE; /* op, a = drapeaux */
	M64(m, 8) = (u64)flags;
	u8 *d = m + BUS_DATA;
	long k = BUS_NAME_MAX - 1;
	for (;;) {
		u8 c = (u8)*pattern++;
		*d++ = c;
		if (!c)
			break;
		if (!--k) {
			*d = 0;
			break;
		}
	}
	if (!bp_canal)
		return 0;
	return korin_sys2(SYS_SVC_CALL, bp_canal - 1, m);
}

/* ---- publier sur le bus sans BusKit (src/svc/inc/buspub.inc) */

/* attente en ms -> 1 si le canal de busd est ouvert */
long bp_open(long ms)
{
	if (bp_canal)
		return 1;
	long c = korin_sys2(SYS_SVC_OPEN, "bus", ms);
	if (c < 0)
		return 0;
	bp_canal = c + 1;
	return 1;
}

/* sujet, valeur (chaîne, 255 octets au plus ; 0 : vide), BUS_RETAIN, BUS_DELETE -> réponse de
 * busd, < 0 : erreur ou pas de canal (busd relancé : le canal est oublié, à rouvrir) */
long bp_publish(const char *topic, const char *val, long flags)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	M64(m, 0) = BUS_OP_PUBLISH; /* op, a = drapeaux */
	M64(m, 8) = (u64)flags;
	u8 *d = m + BUS_DATA;
	for (long k = BUS_NAME_MAX;;) { /* le nom */
		u8 c = (u8)*topic++;
		*d++ = c;
		if (!c)
			break;
		if (!--k)
			return BUS_EINVAL;
	}
	u8 *v = d; /* la valeur */
	if (val)
		for (long k = BUS_VALUE_MAX - 1; k && *val; k--)
			*d++ = (u8)*val++;
	*d = 0;
	M64(m, 16) = (u64)(d - v); /* b = octets */
	long c = bp_canal;
	if (!c)
		return BUS_ENODEV;
	long r = korin_sys2(SYS_SVC_CALL, c - 1, m);
	if (r >= 0 || r == (long)M64(m, 0)) /* réponse de busd ? */
		return r;
	bp_canal = 0; /* non : le canal est mort */
	return BUS_ENODEV;
}
