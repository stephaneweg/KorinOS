/* selfhost - vérifie l'auto-hébergement de Korin, comme selfhost.asm : lance FASMARM natif
 * (/bin/fasmarm) sur une série de sources, chacune dans son répertoire, attend chaque
 * assemblage et affiche code de sortie et durée ; code de sortie : le nombre d'échecs.
 * Avec une ligne d'arguments : un seul assemblage dans le répertoire courant, avec cette ligne
 * (code : celui de fasmarm, 1 si le lancement échoue).
 * Comme l'asm : une ligne présente mais vide fait aussi un seul assemblage (pas la série) ; le
 * répertoire de chaque travail est pris par SYS_CHDIR sans vérifier ; sortie sans tampon. */
#include "korinrt.h"

/* (répertoire, arguments) */
static const char *const jobs[][2] = {
	{ "/t/bin/hello", "hello.asm hello.elf" },
	{ "/t", "InstructionFormatsARM64.asm if64.bin" },
	{ "/t", "InstructionFormatsTHUMB32.asm t32.bin" },
	{ "/src/kernel", "kernel.asm kernel8.img" },
	{ "/src/kernel", "kernel.asm kernel_2712.img -d ARCH=pi5" },
	{ "/src/kits/uikit", "uikit.asm uikit.so" },
	{ "/src/apps/control", "control.asm control.elf" },
	{ "/src/svc/echo", "echo.asm echo.elf" },
	{ "/src/svc/hwtest", "hwtest.asm hwtest.elf" },
	{ "/src/svc/vfs", "vfs.asm vfs.elf" },
	{ "/src/bin/kittest", "kittest.asm kittest.elf" },
	{ "/src/svc/ramfs", "ramfs.asm ramfs.elf" },
	{ "/src/svc/fatfs", "fatfs.asm fatfs.elf" },
	{ "/src/svc/imgblk", "imgblk.asm imgblk.elf" },
	{ "/src/svc/sd", "sd.asm sd.elf" },
	{ "/src/svc/init", "init.asm init.elf" },
	{ "/src/svc/init2", "init2.asm init2.elf" },
	{ "/src/svc/hwmanager", "hwmanager.asm hwmanager.elf" },
	{ "/src/svc/hidmanager", "hidmanager.asm hidmanager.elf" },
	{ "/src/svc/gt911", "gt911.asm gt911.elf" },
	{ "/src/svc/usbd", "usbd.asm usbd.elf" },
	{ "/src/svc/usbkeyboard", "usbkeyboard.asm usbkeyboard.elf" },
	{ "/src/svc/usbmouse", "usbmouse.asm usbmouse.elf" },
	{ "/src/svc/usbstorage", "usbstorage.asm usbstorage.elf" },
	{ "/src/bin/fasmarm", "fasmarm.asm fasmarm.elf" },
	{ "/src/svc/usbnet", "usbnet.asm usbnet.elf" },
	{ "/src/svc/netd", "netd.asm netd.elf" },
	{ "/src/kits/netkit", "netkit.asm netkit.so" },
	{ "/src/svc/genet", "genet.asm genet.elf" },
	{ "/src/svc/wifi", "wifi.asm wifi.elf" },
	{ "/src/sys/wifimenu", "wifimenu.asm wifimenu.elf" },
	{ "/src/svc/telnetd", "telnetd.asm telnetd.elf" },
	{ "/src/svc/ftpd", "ftpd.asm ftpd.elf" },
	{ "/src/bin/sh", "sh.asm sh.elf" },
	{ "/src/bin/cat", "cat.asm cat.elf" },
	{ "/src/bin/mkdir", "mkdir.asm mkdir.elf" },
	{ "/src/bin/rmdir", "rmdir.asm rmdir.elf" },
	{ "/src/bin/rm", "rm.asm rm.elf" },
	{ "/src/bin/cp", "cp.asm cp.elf" },
	{ "/src/bin/mv", "mv.asm mv.elf" },
	{ "/src/bin/touch", "touch.asm touch.elf" },
	{ "/src/bin/ls", "ls.asm ls.elf" },
	{ "/src/bin/basename", "basename.asm basename.elf" },
	{ "/src/bin/dirname", "dirname.asm dirname.elf" },
	{ "/src/bin/sleep", "sleep.asm sleep.elf" },
	{ "/src/bin/test", "test.asm test.elf" },
	{ "/src/bin/true", "true.asm true.elf" },
	{ "/src/bin/false", "false.asm false.elf" },
	{ "/src/bin/echo", "echo.asm echo.elf" },
	{ "/src/bin/pwd", "pwd.asm pwd.elf" },
	{ "/src/bin/wc", "wc.asm wc.elf" },
	{ "/src/bin/head", "head.asm head.elf" },
	{ "/src/bin/tail", "tail.asm tail.elf" },
	{ "/src/bin/tee", "tee.asm tee.elf" },
	{ "/src/bin/tr", "tr.asm tr.elf" },
	{ "/src/bin/uniq", "uniq.asm uniq.elf" },
	{ "/src/bin/cut", "cut.asm cut.elf" },
	{ "/src/bin/sort", "sort.asm sort.elf" },
	{ "/src/bin/grep", "grep.asm grep.elf" },
	{ "/src/bin/find", "find.asm find.elf" },
	{ "/src/bin/sed", "sed.asm sed.elf" },
	{ "/src/bin/reboot", "reboot.asm reboot.elf" },
	{ "/src/bin/screenshot", "screenshot.asm screenshot.elf" },
	{ "/src/svc/vncd", "vncd.asm vncd.elf" },
	{ "/src/svc/rdpd", "rdpd.asm rdpd.elf" },
	{ "/src/bin/rngtest", "rngtest.asm rngtest.elf" },
	{ "/src/sys/elegant", "elegant.asm elegant.elf" },
	{ "/src/apps/uidemo", "uidemo.asm uidemo.elf" },
	{ "/src/apps/viewer", "viewer.asm viewer.elf" },
	{ "/src/apps/editor", "editor.asm editor.elf" },
	{ "/src/sys/menubar", "menubar.asm menubar.elf" },
	{ "/src/kits/audiokit", "audiokit.asm audiokit.so" },
	{ "/src/svc/sound", "sound.asm sound.elf" },
	{ "/src/svc/pwmaudio", "pwmaudio.asm pwmaudio.elf" },
	{ "/src/svc/hdmiaudio", "hdmiaudio.asm hdmiaudio.elf" },
	{ "/src/svc/i2smic", "i2smic.asm i2smic.elf" },
	{ "/src/svc/usbaudio", "usbaudio.asm usbaudio.elf" },
	{ "/src/apps/mixer", "mixer.asm mixer.elf" },
	{ "/src/sys/soundmenu", "soundmenu.asm soundmenu.elf" },
	{ "/src/bin/soundctl", "soundctl.asm soundctl.elf" },
	{ "/src/bin/sontest", "sontest.asm sontest.elf" },
	{ "/src/kits/appkit", "appkit.asm appkit.so" },
	{ "/src/bin/threadtest", "threadtest.asm threadtest.elf" },
	{ "/src/bin/memtest", "memtest.asm memtest.elf" },
	{ "/src/bin/surfacetest", "surfacetest.asm surfacetest.elf" },
	{ "/src/bin/multiwin", "multiwin.asm multiwin.elf" },
	{ "/src/bin/mediatest", "mediatest.asm mediatest.elf" },
	{ "/src/apps/mediaplayer", "mediaplayer.asm mediaplayer.elf" },
	{ "/src/bin/codectest", "codectest.asm codectest.elf" },
	{ "/src/bin/play", "play.asm play.elf" },
	{ "/src/apps/music", "music.asm music.elf" },
	{ "/src/bin/synthtest", "synthtest.asm synthtest.elf" },
	{ "/src/apps/fmtracker", "fmtracker.asm fmtracker.elf" },
	{ "/src/svc/v3d", "v3d.asm v3d.elf" },
	{ "/src/kits/gpukit", "gpukit.asm gpukit.so" },
	{ "/src/bin/gputest", "gputest.asm gputest.elf" },
	{ "/src/bin/pibuild", "pibuild.asm pibuild.elf" },
	{ "/src/apps/builder", "builder.asm builder.elf" },
	{ "/src/kits/archivekit", "archivekit.asm archivekit.so" },
	{ "/src/bin/arc", "arc.asm arc.elf" },
	{ "/src/kits/documentkit", "documentkit.asm documentkit.so" },
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
	long pid = ksys(SYS_SPAWN, (long)"/bin/fasmarm", (long)args, 0, 0, 0, 0);
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
