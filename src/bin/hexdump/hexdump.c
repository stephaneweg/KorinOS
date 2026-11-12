/* hexdump - les octets d'un fichier : hexdump [-s POSITION] [-n NOMBRE] [fichier] (docs/OUTILS.md),
 * comme hexdump.asm : la position sur 8 chiffres, 16 octets en hexadécimal (un blanc de plus après
 * le 8e), les mêmes en texte entre « | » (« . » hors de 32..126), puis la position finale seule.
 * -s saute POSITION octets, -n s'arrête après NOMBRE octets (décimal ou 0x...). Sans fichier :
 * l'entrée standard (« - » aussi). Usage : code 2 ; fichier absent : code 1.
 *
 * Comme l'asm : une option n'est lue que si un mot la suit (« hexdump -s » ouvre le fichier « -s ») ;
 * une option de plus d'une lettre, une autre lettre que s et n, un nombre vide ou faux : usage ;
 * seul le premier fichier compte ; la position ne compte que les octets vraiment sautés ; un
 * NOMBRE de 2^63 ou plus vaut « tout » (comparé signé) ; la position garde ses 32 bits du bas. */
#include "korinrt.h"

static void hex(u64 v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		out_putc("0123456789abcdef"[(v >> (i * 4)) & 15]);
}

/* texte -> 0 et *v, -1 : pas un nombre (parse_num) */
static int parse_num(const char *s, u64 *v)
{
	u64 x = 0;
	int r = -1;
	if (s[0] == '0' && (s[1] | 0x20) == 'x') {
		for (s += 2; *s; s++) {
			unsigned c = (u8)*s, d = c - '0';
			if (d > 9) {
				d = (c | 0x20) - 'a';
				if (d > 5)
					return -1;
				d += 10;
			}
			x = (x << 4) + d;
			r = 0;
		}
	} else {
		for (; *s; s++) {
			unsigned d = (u8)*s - '0';
			if (d > 9)
				return -1;
			x = x * 10 + d;
			r = 0;
		}
	}
	*v = x;
	return r;
}

int main(int argc, char **argv)
{
	int i = 1;
	u64 skip = 0;
	i64 count = -1;
	u8 row[16];
	while (i + 1 < argc) {
		const char *o = argv[i];
		if (o[0] != '-' || !o[1])
			break;
		u64 v;
		if (o[2] || parse_num(argv[i + 1], &v) < 0)
			goto usage;
		i += 2;
		if (o[1] == 's')
			skip = v;
		else if (o[1] == 'n')
			count = (i64)v;
		else
			goto usage;
	}
	const char *path = i < argc ? argv[i] : 0;
	long fd = in_open(path);
	if (fd < 0) {
		err_file(path ? path : "-");
		return 1;
	}
	rd_init(&krt_in, fd);
	u64 pos = 0;
	while (pos < skip && rd_getc(&krt_in) >= 0)
		pos++;
	for (;;) {
		int n = 0;
		while (n < 16 && count) {
			int c = rd_getc(&krt_in);
			if (c < 0)
				break;
			row[n++] = (u8)c;
			if (count > 0)
				count--;
		}
		if (!n)
			break;
		hex(pos, 8);
		out_putc(' ');
		out_putc(' ');
		for (int k = 0; k < 16; k++) {
			if (k < n)
				hex(row[k], 2);
			else {
				out_putc(' ');
				out_putc(' ');
			}
			out_putc(' ');
			if (k == 7)
				out_putc(' ');
		}
		out_putc(' ');
		out_putc('|');
		for (int k = 0; k < n; k++)
			out_putc(row[k] < 32 || row[k] >= 127 ? '.' : row[k]);
		out_putc('|');
		out_nl();
		pos += (u64)n;
		if (n != 16)
			break;
	}
	hex(pos, 8);
	out_nl();
	in_close(fd);
	return 0;
usage:
	err_msg("usage : hexdump [-s POSITION] [-n NOMBRE] [fichier]");
	return 2;
}
