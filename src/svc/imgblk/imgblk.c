/* imgblk - périphérique bloc sur un fichier image (docs/MICRO-NOYAU.md, section 4),
 * traduction de imgblk.asm : blk.<nom> et ses partitions, en lecture et écriture.
 * Sert à essayer les services de fichiers sans toucher à la carte (« img0
 * SD0:/test.img »), et à monter une image disque. Arguments : « <nom> <fichier> ».
 * Le fichier est un client ordinaire de vfs (AppKit, file_sys). */
#include "blkserv.h"

static long fd;

/* secteur, nombre, adresse -> 0 ou erreur (E_IO : transfert incomplet) */
static long transfer(u64 sector, u64 count, void *addr, int write)
{
	long r = sys_seek(fd, (long)(sector << 9), 0);
	if (r < 0)
		return r;
	r = write ? sys_write(fd, addr, count << 9) : sys_read(fd, addr, count << 9);
	if (r < 0)
		return r;
	return (u64)r == count << 9 ? 0 : E_IO;
}

static long img_read(void *ctx, u64 sector, u64 count, void *addr, u64 phys)
{
	(void)ctx;
	(void)phys;
	return transfer(sector, count, addr, 0);
}

static long img_write(void *ctx, u64 sector, u64 count, const void *addr, u64 phys)
{
	(void)ctx;
	(void)phys;
	return transfer(sector, count, (void *)addr, 1);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line)
		SVC_QUIT();
	u64 n = strlen(krt_line) + 1;
	char *name = ak_malloc(n);
	memcpy(name, krt_line, n);
	char *file = name; /* « <nom> <fichier> » */
	while (*file && *file != ' ')
		file++;
	if (!*file)
		SVC_QUIT();
	*file++ = 0;
	long st[2];
	if (sys_stat(file, st) < 0) /* taille de l'image */
		SVC_QUIT();
	fd = sys_open(file, O_READ | O_WRITE);
	if (fd < 0)
		SVC_QUIT();
	static struct blk_driver d; /* le pilote (src/svc/common/blkserv.h) */
	d.version = BLK_DRIVER_V1;
	d.name = name;
	d.sectors = (u64)st[0] >> 9; /* lecture et écriture */
	d.read = img_read;
	d.write = img_write;
	blk_serve(&d);
}
