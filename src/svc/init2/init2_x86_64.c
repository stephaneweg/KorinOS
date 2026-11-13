/* init2_x86_64.c : la couche du PC d'init2.
 *  * redémarrage et extinction (INIT_OP_REBOOT, bit 1 : arrêt ; docs/etude/PC-PILOTES.md,
 *    « Extinction et redémarrage du PC ») : les programmes prévenus (SIGTERM, 3 s, puis
 *    SIGKILL : leurs fichiers fermés, entrées des répertoires écrites), sauf les services
 *    des disques et des volumes et les pilotes ; le GPU rendu (intelgpu stop) ; les
 *    volumes vidés (FS_SYNC) et démontés (vfs, SD0: en dernier) ; le disque NVMe arrêté
 *    (BLK_SIG_STOP : FLUSH, CC.SHN) ; puis le noyau (SYS_POWER : ACPI S5, ou RESET_REG,
 *    0xCF9...). Le Pi ne passe pas par là (init2_aarch64.c) ;
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

#define BLK_SIG_STOP 1 /* (src/svc/common/blkserv.h) */
#define SIG_TERM 15
#define POWER_OFF 1 /* (SYS_POWER) */
#define POWER_REBOOT 2
#define PROCS 32 /* entrées de PROCLIST_FROM par appel */
#define TERM_MS 3000
#define NVME_MS 12000
#define GPU_MS 3000
#define WIFI_MS 3000

static u8 pmsg[512] __attribute__((aligned(16)));
static u8 procs[PROCS * 64];

/* les process qui restent jusqu'au bout : les disques et les volumes (leurs services),
 * les pilotes (le matériel s'arrête avec la machine ; intelgpu : par « stop »), init */
static const char *const keep[] = {
	"init", "init2", "pcid", "nvme", "vfs", "fatfs", "ramfs", "imgblk", "usbd", "usbstorage",
	"sdhci", "rtsx", "hda", "intelgpu", "iwl", 0,
};

static int kept(const char *name)
{
	for (int i = 0; keep[i]; i++)
		if (str_eq(name, keep[i]))
			return 1;
	return 0;
}

/* sig > 0 : ce signal à chaque process qui n'est pas gardé ; -> combien en restent (sig 0 :
 * compte seulement ; sig < 0 : SYS_KILL). name : ne compte que les process de ce nom */
static long each_proc(long sig, const char *name)
{
	long me = korin_sys0(SYS_GETPID), left = 0, from = 0;
	for (;;) {
		long n = korin_sys3(SYS_PROCLIST_FROM, procs, PROCS, from);
		if (n <= 0)
			break;
		for (long k = 0; k < n; k++) {
			u8 *e = procs + k * 64;
			long pid = (long)M32(e, 0);
			from = (long)(M64(e, 0) & 0xFFFF);
			const char *nm = (const char *)e + 24;
			e[24 + 31] = 0;
			if (pid <= 0 || pid == me)
				continue;
			if (name) {
				left += str_eq(nm, name) != 0;
				continue;
			}
			if (kept(nm))
				continue;
			left++;
			if (sig > 0)
				korin_sys3(SYS_SIG_SEND, pid, 0, sig);
			else if (sig < 0) {
				say("init2 : arrêt forcé de ");
				say(nm);
				say("\n");
				korin_sys1(SYS_KILL, pid);
			}
		}
		if (n != PROCS)
			break;
	}
	return left;
}

/* attend (ms au plus) que plus aucun process ne réponde à each_proc(0, name) -> 1 si c'est fait */
static int wait_gone(const char *name, long ms)
{
	for (long t = 0;; t += 100) {
		if (!each_proc(0, name))
			return 1;
		if (t >= ms)
			return 0;
		sys_sleep(100);
	}
}

static void say_n(const char *a, long n, const char *b)
{
	say(a);
	say_dec(n);
	say(b);
}

/* les volumes : FS_SYNC, puis démontés (vfs arrête leur service), SD0: en dernier */
static void volumes_off(void)
{
	static char names[VU_MAX][16];
	long n = vu_list();
	if (n <= 0) {
		say_n("init2 : pas de liste des volumes (", n, ")\n");
		return;
	}
	for (long i = 0; i < n; i++)
		str_copy_n(names[i], (const char *)vu_msg + MSG_DATA + i * VU_ENT, 16);
	for (long i = 0; i < n; i++) {
		long r = vu_sync(names[i]);
		say("init2 : ");
		say(names[i]);
		say_n(" vidé (FS_SYNC -> ", r, ")\n");
	}
	for (int last = 0; last < 2; last++)
		for (long i = 0; i < n; i++) {
			if (str_eq(names[i], "sd0") != last)
				continue;
			long r = vu_unmount(names[i]);
			say("init2 : ");
			say(names[i]);
			say_n(" démonté (", r, ")\n");
		}
}

void plat_reboot(u64 flags)
{
	int off = (flags & 2) != 0;
	long t0 = sys_time_ms();
	say(off ? "init2 : extinction : " : "init2 : redémarrage : ");
	say("les programmes sont prévenus (SIGTERM)\n");
	each_proc(SIG_TERM, 0);
	if (!wait_gone(0, TERM_MS))
		each_proc(-1, 0);
	wait_gone(0, 1000);
	sys_sleep(200); /* (les avis de fin : fichiers fermés chez fatfs) */
	if (korin_sys2(SYS_SVC_OPEN, "intelgpu", 0) >= 0) { /* le GPU rendu comme au départ */
		lancer_ligne("SD0:/sys/svc/intelgpu stop", 0);
		if (!wait_gone("intelgpu", GPU_MS))
			say("init2 : intelgpu ne s'est pas arrêté (laissé tel quel)\n");
	}
	long wifi = korin_sys2(SYS_SVC_OPEN, "wifi", 0); /* le Wi-Fi (iwl) : déconnecté, puce arrêtée, DMA coupé */
	if (wifi >= 0) {
		korin_sys2(SYS_SVC_SIGNAL, wifi, 1);
		say(wait_gone("iwl", WIFI_MS) ? "init2 : iwl arrêté\n" : "init2 : iwl ne s'est pas arrêté\n");
	}
	volumes_off();
	long blk = korin_sys2(SYS_SVC_OPEN, "blk.nvme0", 0); /* le disque : cache vidé, CC.SHN */
	if (blk >= 0) {
		long r = korin_sys2(SYS_SVC_SIGNAL, blk, BLK_SIG_STOP);
		say_n("init2 : nvme : arrêt demandé (", r, ")\n");
		say(wait_gone("nvme", NVME_MS) ? "init2 : nvme arrêté\n"
					       : "init2 : nvme ne s'est pas arrêté en 12 s\n");
	}
	say_n("init2 : prêt en ", sys_time_ms() - t0, " ms\n");
	long r = korin_sys1(SYS_POWER, off ? POWER_OFF : POWER_REBOOT);
	say_n("init2 : SYS_POWER refusé (", r, ")\n");
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
