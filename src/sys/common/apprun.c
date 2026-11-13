/* apprun.c : les applications qui tournent (src/inc/apprun.inc), voir apprun.h. */
#include "sys_consts.h"
#include "uikit_import.h"
#include "apprun.h"

long pids[AP_MAX];
static long pids_new[AP_MAX];
static long run_seen; /* un premier tour fait */
static u8 wlist[WL_PER_MSG * WL_SIZE];
static u8 procs[APPRUN_PROCS * PE_SIZE];

long apps_running(void)
{
	for (long i = 0; i < AP_MAX; i++) /* rien de trouvé encore */
		pids_new[i] = 0;
	long from = 0; /* tous les process, par morceaux (SYS_PROCLIST_FROM : pas de plafond) */
	for (;;) {
		long n = ksys(SYS_PROCLIST_FROM, (long)procs, APPRUN_PROCS, from, 0, 0, 0);
		if (n <= 0)
			break;
		for (long k = 0; k < n; k++) {
			u8 *e = procs + k * PE_SIZE;
			from = (long)(M64(e, 0) & 0xFFFF); /* (la suite : après ce process) */
			for (long i = 0; i < ap_n; i++) { /* une application de ce nom ? */
				if (!str_eq((const char *)e + 24, ap_nom[i]))
					continue;
				if (!pids_new[i]) /* (le premier process de ce nom) */
					pids_new[i] = M32(e, 0);
				break;
			}
		}
		if (n != APPRUN_PROCS) /* morceau plein : la suite */
			break;
	}
	long changed = 0;
	for (long i = 0; i < ap_n; i++) {
		long old = pids[i], now = pids_new[i];
		if (old != now)
			changed++;
		pids[i] = now;
		if (!old && now && run_seen) /* elle démarre : une récente */
			rc_touch(i);
	}
	run_seen = 1;
	return changed;
}

void app_open(long app)
{
	long pid = pids[app];
	if (pid) {
		for (long first = 0;; first += WL_PER_MSG) { /* ses fenêtres : la première */
			long n = ui_winlist(first, (long)wlist);
			if (n <= first)
				break; /* pas de fenêtre : relancer */
			n -= first;
			if (n > WL_PER_MSG)
				n = WL_PER_MSG;
			for (long k = 0; k < n; k++) {
				u8 *e = wlist + k * WL_SIZE;
				if ((long)M32(e, WL_PID) == pid) {
					ui_raise((long)M64(e, WL_ID));
					return;
				}
			}
		}
	}
	app_launch(app);
	rc_touch(app); /* (sans attendre le prochain tour) */
}
