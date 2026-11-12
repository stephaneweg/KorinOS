/* selfhost - vérifie l'auto-hébergement de Korin, comme selfhost.asm : lance FASMARM natif
 * (SD0:/bin/fasmarm) sur une série de sources, chacune dans son répertoire, attend chaque
 * assemblage et affiche code de sortie et durée ; code de sortie : le nombre d'échecs.
 * Avec une ligne d'arguments : un seul assemblage dans le répertoire courant, avec cette ligne
 * (code : celui de fasmarm, 1 si le lancement échoue).
 * Comme l'asm : une ligne présente mais vide fait aussi un seul assemblage (pas la série) ; le
 * répertoire de chaque travail est pris par SYS_CHDIR sans vérifier ; sortie sans tampon. */
#include "korinrt.h"

/* (répertoire, arguments) */
static const char *const jobs[][2] = {
	{ "SD0:/t/bin/hello", "hello.asm hello.elf" },
	{ "SD0:/t", "InstructionFormatsARM64.asm if64.bin" },
	{ "SD0:/t", "InstructionFormatsTHUMB32.asm t32.bin" },
	{ "SD0:/src/kernel", "kernel.asm kernel8.img" },
	{ "SD0:/src/kernel", "kernel.asm kernel_2712.img -d ARCH=pi5" },
	{ "SD0:/src/kits/uikit", "uikit.asm uikit.so" },
	{ "SD0:/src/apps/control", "control.asm control.elf" },
	{ "SD0:/src/svc/echo", "echo.asm echo.elf" },
	{ "SD0:/src/svc/hwtest", "hwtest.asm hwtest.elf" },
	{ "SD0:/src/svc/vfs", "vfs.asm vfs.elf" },
	{ "SD0:/src/bin/kittest", "kittest.asm kittest.elf" },
	{ "SD0:/src/svc/ramfs", "ramfs.asm ramfs.elf" },
	{ "SD0:/src/svc/fatfs", "fatfs.asm fatfs.elf" },
	{ "SD0:/src/svc/imgblk", "imgblk.asm imgblk.elf" },
	{ "SD0:/src/svc/sd", "sd.asm sd.elf" },
	{ "SD0:/src/svc/init", "init.asm init.elf" },
	{ "SD0:/src/svc/init2", "init2.asm init2.elf" },
	{ "SD0:/src/svc/hwmanager", "hwmanager.asm hwmanager.elf" },
	{ "SD0:/src/svc/hidmanager", "hidmanager.asm hidmanager.elf" },
	{ "SD0:/src/svc/gt911", "gt911.asm gt911.elf" },
	{ "SD0:/src/svc/usbd", "usbd.asm usbd.elf" },
	{ "SD0:/src/svc/usbkeyboard", "usbkeyboard.asm usbkeyboard.elf" },
	{ "SD0:/src/svc/usbmouse", "usbmouse.asm usbmouse.elf" },
	{ "SD0:/src/svc/usbstorage", "usbstorage.asm usbstorage.elf" },
	{ "SD0:/src/bin/fasmarm", "fasmarm.asm fasmarm.elf" },
	{ "SD0:/src/svc/usbnet", "usbnet.asm usbnet.elf" },
	{ "SD0:/src/svc/netd", "netd.asm netd.elf" },
	{ "SD0:/src/kits/netkit", "netkit.asm netkit.so" },
	{ "SD0:/src/svc/genet", "genet.asm genet.elf" },
	{ "SD0:/src/svc/wifi", "wifi.asm wifi.elf" },
	{ "SD0:/src/sys/wifimenu", "wifimenu.asm wifimenu.elf" },
	{ "SD0:/src/svc/telnetd", "telnetd.asm telnetd.elf" },
	{ "SD0:/src/svc/ftpd", "ftpd.asm ftpd.elf" },
	{ "SD0:/src/bin/sh", "sh.asm sh.elf" },
	{ "SD0:/src/bin/cat", "cat.asm cat.elf" },
	{ "SD0:/src/bin/mkdir", "mkdir.asm mkdir.elf" },
	{ "SD0:/src/bin/rmdir", "rmdir.asm rmdir.elf" },
	{ "SD0:/src/bin/rm", "rm.asm rm.elf" },
	{ "SD0:/src/bin/cp", "cp.asm cp.elf" },
	{ "SD0:/src/bin/mv", "mv.asm mv.elf" },
	{ "SD0:/src/bin/touch", "touch.asm touch.elf" },
	{ "SD0:/src/bin/ls", "ls.asm ls.elf" },
	{ "SD0:/src/bin/basename", "basename.asm basename.elf" },
	{ "SD0:/src/bin/dirname", "dirname.asm dirname.elf" },
	{ "SD0:/src/bin/sleep", "sleep.asm sleep.elf" },
	{ "SD0:/src/bin/test", "test.asm test.elf" },
	{ "SD0:/src/bin/true", "true.asm true.elf" },
	{ "SD0:/src/bin/false", "false.asm false.elf" },
	{ "SD0:/src/bin/echo", "echo.asm echo.elf" },
	{ "SD0:/src/bin/pwd", "pwd.asm pwd.elf" },
	{ "SD0:/src/bin/wc", "wc.asm wc.elf" },
	{ "SD0:/src/bin/head", "head.asm head.elf" },
	{ "SD0:/src/bin/tail", "tail.asm tail.elf" },
	{ "SD0:/src/bin/tee", "tee.asm tee.elf" },
	{ "SD0:/src/bin/tr", "tr.asm tr.elf" },
	{ "SD0:/src/bin/uniq", "uniq.asm uniq.elf" },
	{ "SD0:/src/bin/cut", "cut.asm cut.elf" },
	{ "SD0:/src/bin/sort", "sort.asm sort.elf" },
	{ "SD0:/src/bin/grep", "grep.asm grep.elf" },
	{ "SD0:/src/bin/find", "find.asm find.elf" },
	{ "SD0:/src/bin/sed", "sed.asm sed.elf" },
	{ "SD0:/src/bin/reboot", "reboot.asm reboot.elf" },
	{ "SD0:/src/bin/screenshot", "screenshot.asm screenshot.elf" },
	{ "SD0:/src/svc/vncd", "vncd.asm vncd.elf" },
	{ "SD0:/src/svc/rdpd", "rdpd.asm rdpd.elf" },
	{ "SD0:/src/bin/rngtest", "rngtest.asm rngtest.elf" },
	{ "SD0:/src/sys/elegant", "elegant.asm elegant.elf" },
	{ "SD0:/src/apps/uidemo", "uidemo.asm uidemo.elf" },
	{ "SD0:/src/apps/viewer", "viewer.asm viewer.elf" },
	{ "SD0:/src/apps/editor", "editor.asm editor.elf" },
	{ "SD0:/src/sys/menubar", "menubar.asm menubar.elf" },
	{ "SD0:/src/kits/audiokit", "audiokit.asm audiokit.so" },
	{ "SD0:/src/svc/sound", "sound.asm sound.elf" },
	{ "SD0:/src/svc/pwmaudio", "pwmaudio.asm pwmaudio.elf" },
	{ "SD0:/src/svc/hdmiaudio", "hdmiaudio.asm hdmiaudio.elf" },
	{ "SD0:/src/svc/i2smic", "i2smic.asm i2smic.elf" },
	{ "SD0:/src/svc/usbaudio", "usbaudio.asm usbaudio.elf" },
	{ "SD0:/src/apps/mixer", "mixer.asm mixer.elf" },
	{ "SD0:/src/sys/soundmenu", "soundmenu.asm soundmenu.elf" },
	{ "SD0:/src/bin/soundctl", "soundctl.asm soundctl.elf" },
	{ "SD0:/src/bin/sontest", "sontest.asm sontest.elf" },
	{ "SD0:/src/kits/appkit", "appkit.asm appkit.so" },
	{ "SD0:/src/bin/threadtest", "threadtest.asm threadtest.elf" },
	{ "SD0:/src/bin/memtest", "memtest.asm memtest.elf" },
	{ "SD0:/src/bin/surfacetest", "surfacetest.asm surfacetest.elf" },
	{ "SD0:/src/bin/multiwin", "multiwin.asm multiwin.elf" },
	{ "SD0:/src/bin/mediatest", "mediatest.asm mediatest.elf" },
	{ "SD0:/src/apps/mediaplayer", "mediaplayer.asm mediaplayer.elf" },
	{ "SD0:/src/bin/codectest", "codectest.asm codectest.elf" },
	{ "SD0:/src/bin/play", "play.asm play.elf" },
	{ "SD0:/src/apps/music", "music.asm music.elf" },
	{ "SD0:/src/bin/synthtest", "synthtest.asm synthtest.elf" },
	{ "SD0:/src/apps/fmtracker", "fmtracker.asm fmtracker.elf" },
	{ "SD0:/src/svc/v3d", "v3d.asm v3d.elf" },
	{ "SD0:/src/kits/gpukit", "gpukit.asm gpukit.so" },
	{ "SD0:/src/bin/gputest", "gputest.asm gputest.elf" },
	{ "SD0:/src/bin/pibuild", "pibuild.asm pibuild.elf" },
	{ "SD0:/src/apps/builder", "builder.asm builder.elf" },
	{ "SD0:/src/kits/archivekit", "archivekit.asm archivekit.so" },
	{ "SD0:/src/bin/arc", "arc.asm arc.elf" },
	{ "SD0:/src/kits/documentkit", "documentkit.asm documentkit.so" },
};

/* répertoire, arguments -> code de sortie de fasmarm (run_job) */
static long run_job(const char *dir, const char *args)
{
	sys_chdir(dir);
	lang_puts("selfhost : fasmarm dans ");
	ak_puts(dir);
	lang_puts(" : ");
	ak_puts(args);
	ak_puts("\n");
	long t = sys_time_ms();
	long pid = ksys(SYS_SPAWN, (long)"SD0:/bin/fasmarm", (long)args, 0, 0, 0, 0);
	if (pid < 0) {
		lang_puts("selfhost : lancement impossible, erreur ");
		print_dec(pid);
		ak_puts("\n");
		return 1;
	}
	long code = korin_sys1(SYS_WAIT, pid);
	t = sys_time_ms() - t;
	lang_puts("selfhost : code de sortie ");
	print_dec(code);
	ak_puts(", ");
	print_dec(t);
	ak_puts(" ms\n");
	return code;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (krt_line) /* arguments fournis : un seul travail */
		_exit((int)run_job(".", krt_line));
	long fails = 0;
	for (u64 i = 0; i < sizeof jobs / sizeof jobs[0]; i++)
		if (run_job(jobs[i][0], jobs[i][1]))
			fails++;
	lang_puts("selfhost : termine, echecs = ");
	print_dec(fails);
	ak_puts("\n");
	_exit((int)fails);
}
