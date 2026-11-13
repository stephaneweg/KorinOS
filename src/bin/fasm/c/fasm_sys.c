/* fasm_sys.c : les appels système de fasm sur Korin PC (src/bin/fasm/system.inc), au-dessus du
 * runtime des commandes en C (korinrt : fichiers par AppKit, chemins à volume, dossier courant).
 * Appelées depuis le coeur de fasm par csave / crestore (System V). Dans un sous-dossier : pas une
 * commande de tools/bin/build_c.sh, mais une partie de tools/pc/fasm.sh. */
#include "korinrt.h"

/* la mémoire de travail de fasm (init_memory de system.inc) : un .bss paresseux, sous 4 Go comme le
 * veut le coeur (adresses de 32 bits), de la taille de KORIN_MEMORY de korin.asm */
#define KORIN_MEMORY 0x10000000
__attribute__((aligned(4096))) char korin_memory[KORIN_MEMORY];

long fk_open(const char *path)
{
	return f_open(path, O_READ);
}

/* exec : le fichier de sortie d'un ELF exécutable (format ELF executable), qui reçoit le bit x
 * comme sous Linux (vfs, V_CHMOD, comme chmod +x) */
long fk_create(const char *path, long exec)
{
	long fd = f_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd >= 0 && exec) {
		long m[MSG_SIZE / 8];
		vfs_msg(m, V_CHMOD, 0755, 0, path, 0);
		vfs_call(m);
	}
	return fd;
}

/* tout ce qui est demandé, sauf fin du fichier ou erreur (le coeur veut le compte exact) */
long fk_read(long fd, void *buf, u64 n)
{
	u64 done = 0;
	while (done < n) {
		long r = f_read(fd, (u8 *)buf + done, n - done);
		if (r < 0)
			return r;
		if (r == 0)
			break;
		done += (u64)r;
	}
	return (long)done;
}

long fk_write(long fd, const void *buf, u64 n)
{
	u64 done = 0;
	while (done < n) {
		long r = f_write(fd, (const u8 *)buf + done, n - done);
		if (r < 0)
			return r;
		if (r == 0)
			return E_IO;
		done += (u64)r;
	}
	return (long)done;
}

long fk_seek(long fd, long off, long whence)
{
	return f_seek(fd, off, (u64)whence);
}

long fk_close(long fd)
{
	return f_close(fd);
}

/* 1 : sortie standard, 2 : erreurs */
long fk_print(long fd, const char *s, u64 n)
{
	return sys_write(fd, s, n);
}

long fk_time_ms(void)
{
	return sys_time_ms();
}

/* secondes UTC (__time__ de fasm, horodatage des formats) */
long fk_time(void)
{
	long t = sys_clock(0);
	return t < 0 ? 0 : t;
}

const char *fk_getenv(const char *name)
{
	return getenv(name);
}

void fk_exit(long code)
{
	exit((int)code);
}
