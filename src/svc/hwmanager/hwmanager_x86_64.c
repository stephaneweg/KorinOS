/* hwmanager_x86_64.c : la couche du PC de hwmanager (hwmanager.h).
 *  * Pas de bus I2C 1 sur des GPIO, ni de SoC, ni de HDMI du Pi : rien n'est
 *    sondé (les adresses 0xFE200000 et 0xFEF00000 du Pi sont sur un PC des
 *    registres du chipset : jamais touchées).
 *  * Tant que le PC n'a pas de disque servi (ni vfs ni /), l'initrd tient
 *    lieu de / : drivers.txt absent -> le module « drivers.txt » de
 *    l'initrd (tools/pc/build.sh) ; un pilote que SYS_SPAWN ne trouve pas ->
 *    le module de l'initrd du même nom (« /sys/svc/usbd » -> « usbd »), par
 *    MOD_LAUNCH. Avec un disque, rien ne change. */
#include "hwmanager.h"

static char drivers[4096];

long plat_spawn(const char *path, const char *args)
{
	long pid = ksys(SYS_SPAWN, (long)path, (long)args, 0, 0, 0, 0);
	if (pid >= 0)
		return pid;
	const char *b = path; /* le module du même nom */
	for (const char *c = path; *c; c++)
		if (*c == '/' || *c == ':')
			b = c + 1;
	long m = korin_sys2(SYS_MOD_LAUNCH, b, args);
	return m >= 0 ? m : pid;
}

int plat_drivers_missing(ini_fn fn)
{
	long n = korin_sys3(SYS_MOD_READ, "drivers.txt", drivers, sizeof drivers - 1);
	if (n < 0)
		return 0;
	drivers[n] = 0;
	ini_parse(drivers, fn);
	say("hwmanager : drivers.txt de l'initrd\n");
	return 1;
}

long plat_i2c_open(void) { return E_NOSYS; }
int plat_i2c_present(u64 adr) { (void)adr; return 0; }
void plat_soc_start(void) {}
int plat_hdmi_watched(void) { return 0; }
void plat_hdmi_poll(void) {}
int plat_hdmi_end(long pid) { (void)pid; return 0; }
