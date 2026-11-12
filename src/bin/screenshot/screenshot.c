/* screenshot - capture de l'écran composé (docs/RESEAU.md 5.3, étape 10) dans
 * un fichier BMP 32 bits (op WS_OP_SNAPSHOT d'Elegant : tel qu'à l'écran,
 * curseur compris), comme screenshot.asm :
 *   screenshot [fichier]       (RAM0:/screen.bmp par défaut)
 * Comme l'asm : le fichier est toute la ligne d'arguments du noyau (espaces
 * compris) ; en-tête de 54 octets (hauteur négative : de haut en bas), pixels
 * tels quels par blocs de 64 Ko ; « screenshot : LxH -> fichier » code 0 ;
 * Elegant absent ou tampon refusé : code 1 ; fichier impossible à écrire :
 * code 2 (un fichier ouvert puis mal écrit reste tel quel) ; sortie sans
 * tampon (lang_puts, puts, print_dec). */
#include "korinrt.h"

#define BLOC 65536

static u8 msg[WS_MSG];
static u8 bmp[64];

/* 4 octets petit-boutistes (adresse non alignée) */
static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *path = "RAM0:/screen.bmp";
	if (krt_line && krt_line[0])
		path = krt_line;
	*(u64 *)(msg + WM_OP) = WS_OP_SNAPSHOT; /* l'écran entier (génération 0) */
	*(u64 *)(msg + WM_B) = 0;
	*(u32 *)(msg + WM_LEN) = 0;
	if (ksys(SYS_WS_CALL, (long)msg, 0, 0, 0, 0, 0) < 0)
		goto ko_ws;
	u64 w = *(u64 *)(msg + WM_A), h = *(u64 *)(msg + WM_B); /* largeur, hauteur */
	long px = ksys(SYS_BUF_MAP, *(long *)(msg + WM_D), 0, 0, 0, 0, 0); /* le tampon */
	if (px < 0)
		goto ko_ws;
	u64 bytes = w * h * 4; /* octets des pixels */
	bmp[0] = 'B';
	bmp[1] = 'M';
	put32(bmp + 2, (u32)(bytes + 54)); /* taille du fichier */
	put32(bmp + 10, 54);               /* décalage des pixels */
	put32(bmp + 14, 40);               /* BITMAPINFOHEADER */
	put32(bmp + 18, (u32)w);
	put32(bmp + 22, (u32)-(u32)h); /* hauteur négative : de haut en bas */
	*(u16 *)(bmp + 26) = 1;        /* plans, 32 bits */
	*(u16 *)(bmp + 28) = 32;
	put32(bmp + 30, 0); /* BI_RGB */
	put32(bmp + 34, (u32)bytes);
	put32(bmp + 38, 2835); /* 72 ppp */
	put32(bmp + 42, 2835);
	put32(bmp + 46, 0);
	put32(bmp + 50, 0);
	long fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		goto ko_f;
	if (sys_write(fd, bmp, 54) < 0)
		goto ko_w;
	for (u64 off = 0; off < bytes;) { /* pixels : B, G, R, 0 = 0x00RRGGBB */
		u64 n = bytes - off;
		if (n > BLOC)
			n = BLOC;
		long r = sys_write(fd, (u8 *)px + off, n);
		if (r <= 0)
			goto ko_w;
		off += (u64)r;
	}
	sys_close(fd);
	lang_puts("screenshot : "); /* « screenshot : 1024x768 -> RAM0:/screen.bmp » */
	print_dec((long)w);
	ak_puts("x");
	print_dec((long)h);
	ak_puts(" -> ");
	ak_puts(path);
	ak_puts("\n");
	_exit(0);
ko_w:
	sys_close(fd);
ko_f:
	lang_puts("screenshot : fichier impossible a écrire\n");
	_exit(2);
ko_ws:
	lang_puts("screenshot : Elegant ne répond pas\n");
	_exit(1);
}
