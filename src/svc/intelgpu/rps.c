/* rps.c : la fréquence du GT (Tiger Lake). Sans pilote, le GPU garde la
 * fréquence que le micrologiciel lui a laissée, souvent la plus basse (RPn) :
 * le rendu tourne alors 3 à 4 fois sous ce que la puce sait faire. Le service
 * demande donc une fréquence fixe au PCU (registre de requête logicielle), la
 * plus haute permise par défaut ; le PCU reste seul juge (limites thermiques
 * et de puissance), il peut descendre sous la demande, jamais au-dessus de RP0.
 *
 * Pas de régulation (« turbo » piloté par les interruptions du PM) : une
 * demande fixe, choisie à la ligne du service (drivers.txt) :
 *     /sys/svc/intelgpu serve freq=max | freq=rp1 | freq=min | freq=<MHz>
 *
 * Registres d'après le PRM public de Tiger Lake (« GT Interface », « Power
 * Management ») ; les unités comparées à ce qu'i915 en fait (Gen9+ : requêtes
 * et fréquence courante en pas de 50/3 MHz, RP_STATE_CAP en pas de 50 MHz).
 * Rien n'est écrit dans l'affichage. */
#include "igpu.h"

#define RP_STATE_CAP 0x145998 /* miroir du MCHBAR : RP0 7:0, RP1 15:8, RPn 23:16, en pas de 50 MHz */
#define RPNSWREQ 0xA008       /* Gen9+ : fréquence demandée 31:23, en pas de 50/3 MHz */
#define RP_CONTROL 0xA024     /* lu pour le journal : 0 = mode logiciel, la requête fait foi */
#define RPSTAT1_GEN12 0x1381B4 /* fréquence courante (CAGF) 19:11, en pas de 50/3 MHz */
#define REQ_SHIFT 23
#define SCALER 3              /* pas de 50 MHz -> pas de 50/3 MHz */

static u32 rp0, rp1, rpn;     /* en pas de 50/3 MHz */
static u32 req_before, req_now;
static int rps_ok;

static u32 mhz(u32 units)
{
	return units * 50 / 3;
}

u32 rps_cagf(void)
{
	return (rd(RPSTAT1_GEN12) >> 11) & 0x1FF;
}

/* la fréquence demandée, d'après « max », « rp1 », « min » ou un nombre de MHz -> pas de 50/3 MHz */
static u32 choose(const char *want)
{
	if (!want || !*want || !strcmp(want, "max"))
		return rp0;
	if (!strcmp(want, "rp1"))
		return rp1;
	if (!strcmp(want, "min"))
		return rpn;
	long m = strtol(want, 0, 10);
	if (m <= 0) {
		glog("fréquence : « %s » incompris, max pris\n", want);
		return rp0;
	}
	u32 u = (u32)((m * 3 + 25) / 50);
	if (u > rp0)
		u = rp0;
	if (u < rpn)
		u = rpn;
	return u;
}

void rps_init(const char *want)
{
	u32 cap = rd(RP_STATE_CAP);
	rp0 = (cap & 0xFF) * SCALER;
	rp1 = ((cap >> 8) & 0xFF) * SCALER;
	rpn = ((cap >> 16) & 0xFF) * SCALER;
	if (!rp0 || rp0 < rpn || rp0 > 0x1FF) {
		glog("fréquence : RP_STATE_CAP illisible (%08x), rien demandé\n", cap);
		return;
	}
	req_before = rd(RPNSWREQ);
	u32 before = rps_cagf();
	req_now = choose(want);
	wr(RPNSWREQ, req_now << REQ_SHIFT);
	rps_ok = 1;
	/* le PCU met quelques centaines de microsecondes à changer de palier */
	long t0 = now_ms();
	u32 after = rps_cagf();
	while (after != req_now && now_ms() - t0 < 20)
		after = rps_cagf();
	glog("fréquence du GT : RPn %u, RP1 %u, RP0 %u MHz ; demandée %u MHz (avant : requête %08x, courante %u "
	     "MHz) ; courante %u MHz ; RP_CONTROL %08x\n",
	     mhz(rpn), mhz(rp1), mhz(rp0), mhz(req_now), req_before, mhz(before), mhz(after), rd(RP_CONTROL));
}

/* à l'arrêt du service : la requête d'avant remise (le GPU retrouve l'état où on l'a pris) */
void rps_fini(void)
{
	if (!rps_ok)
		return;
	wr(RPNSWREQ, req_before);
	rps_ok = 0;
}

int rps_status(char *b, int max)
{
	if (!rps_ok)
		return snprintf(b, max, "  fréquence : non réglée\n");
	/* court : tout le texte de status tient en 464 octets (serve.c) */
	return snprintf(b, max, "  fréquence : %u MHz demandés, %u courants (RPn %u, RP0 %u)\n", mhz(req_now),
			mhz(rps_cagf()), mhz(rpn), mhz(rp0));
}
