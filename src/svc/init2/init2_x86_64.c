/* init2_x86_64.c : la couche du PC d'init2.
 *  * redémarrage (INIT_OP_REBOOT) : pas encore d'appel pour redémarrer depuis un
 *    service (le moniteur du noyau a « reboot ») : rien, init2 écrit que le
 *    redémarrage n'a pas eu lieu ;
 *  * les bus : pcid tourne depuis init (init.txt de l'initrd : « pcid -n », sans
 *    annonce, hwmanager n'étant lancé qu'ici) ; dès que hwmanager répond, pcid
 *    annonce les fonctions qu'aucun pilote ne tient (PCI_OP_ANNOUNCE : sans
 *    relire le bus, le NVMe déjà servi n'est pas touché) et hwmanager lance
 *    leurs pilotes (drivers.txt : usbd par xHCI, sdhci, rtsx). Pas d'usbd
 *    lancé sans adresse comme sur le Pi : un par contrôleur, par hwmanager ;
 *  * Elegant (en C, commun au Pi et au PC) : sauf avec shell=console dans
 *    SD0:/etc/system.ini, ou sans SD0:/sys/elegant (alors la console texte) ;
 *    s'il ne répond pas ou tombe trop souvent, la console texte en secours. */
#include "init2.h"

#define PCI_OP_ANNOUNCE 9 /* (src/svc/pcid/pci.h) */

static u8 pmsg[512] __attribute__((aligned(16)));

void plat_reboot(u64 flags)
{
	(void)flags;
}

void plat_buses(void)
{
	if (korin_sys2(SYS_SVC_OPEN, "hwmanager", 5000) < 0) { /* (pcid lui parle : il doit répondre) */
		say("init2 : hwmanager ne repond pas, pas d'annonce du PCIe\n");
		return;
	}
	long pci = korin_sys2(SYS_SVC_OPEN, "pci", 2000);
	if (pci < 0) {
		say("init2 : pcid absent (init.txt), pas d'annonce du PCIe\n");
		return;
	}
	for (int i = 0; i < 64; i++)
		pmsg[i] = 0;
	M64(pmsg, 0) = PCI_OP_ANNOUNCE;
	long r = korin_sys2(SYS_SVC_CALL, pci, pmsg);
	say("init2 : pcid annonce le PCIe a hwmanager -> "); /* « … -> 7 » (fonctions) */
	say_dec(r);
	say("\n");
}

static char shell[16];

static void shell_line(char *key, char *val)
{
	if (str_eq(key, "shell"))
		str_copy_n(shell, val, sizeof shell);
}

int plat_text_console(void)
{
	long st[2];
	ini_read("SD0:/etc/system.ini", shell_line);
	if (str_eq(shell, "console")) {
		say("init2 : shell=console : console texte\n");
		return 1;
	}
	if (sys_stat("SD0:/sys/elegant", st) < 0) {
		say("init2 : pas d'Elegant sur ce PC : console texte\n");
		return 1;
	}
	return 0;
}

/* Elegant ne répond pas, ou abandonné après ses relances : la console texte en secours (une
 * seule fois ; l'écran revient à la console du noyau quand Elegant n'est plus là) */
static int console_started;

void plat_shell_lost(void)
{
	if (console_started)
		return;
	console_started = 1;
	say("init2 : pas de bureau : console texte en secours\n");
	lancer_ligne("SD0:/sys/svc/console", 1);
}
