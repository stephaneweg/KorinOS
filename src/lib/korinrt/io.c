/* io.c : sortie tamponnée, erreurs, lecture tamponnée (docs/BIN-C.md),
 * comme src/inc/tool.inc : la sortie standard part par blocs de T_OUT
 * octets (SYS_WRITE sur 1) ; une erreur vide d'abord la sortie, puis écrit
 * sur 2 sans tampon. */
#include "korinrt.h"

static u8 out_buf[T_OUT];
static u64 out_n;
long krt_out_err; /* la première erreur d'écriture de la sortie (E_NOSPC : exit le dit) */

void out_flush(void)
{
	u64 n = out_n;
	if (!n)
		return;
	out_n = 0;
	const u8 *p = out_buf;
	while (n) { /* (une écriture courte : le reste, qui rend l'erreur) */
		long r = sys_write(1, p, n);
		if (r <= 0) {
			if (r < 0 && !krt_out_err)
				krt_out_err = r;
			break;
		}
		p += r;
		n -= (u64)r > n ? n : (u64)r;
	}
}

/* (comme t_putc : le tampon plein n'est vidé qu'au caractère suivant) */
void out_putc(int c)
{
	if (out_n >= T_OUT)
		out_flush();
	out_buf[out_n++] = (u8)c;
}

void out_puts(const char *s)
{
	while (*s)
		out_putc((u8)*s++);
}

void out_write(const void *buf, u64 n)
{
	const u8 *p = buf;
	while (n--)
		out_putc(*p++);
}

void out_nl(void)
{
	out_putc(10);
}

void out_putn(u64 v)
{
	char t[32];
	u64_to_str(v, t);
	out_puts(t);
}

void out_putw(u64 v, int w)
{
	char t[32];
	int n = (int)(u64_to_str(v, t) - t);
	for (; w > n; w--)
		out_putc(' ');
	out_puts(t);
}

void out_say(const char *s)
{
	out_puts(lang_tr(s));
}

void err_puts(const char *s)
{
	out_flush();
	sys_write(2, s, (u64)strlen(s));
}

void err_say(const char *s)
{
	err_puts(lang_tr(s));
}

void err_msg(const char *s)
{
	s = lang_tr(s);
	err_puts(krt_argv[0]);
	err_puts(": ");
	err_puts(s);
	err_puts("\n");
}

void err_file(const char *path)
{
	err_puts(krt_argv[0]);
	err_puts(": ");
	err_puts(path);
	err_say(" : inaccessible\n");
}

void err_file_e(const char *path, long e)
{
	if (e != E_NOSPC) {
		err_file(path);
		return;
	}
	err_puts(krt_argv[0]);
	err_puts(": ");
	err_puts(path);
	err_say(" : disque plein\n");
}

/* ---- lecture */

struct reader krt_in;
char krt_line_buf[T_LINE];

long in_open(const char *path)
{
	if (!path || (path[0] == '-' && !path[1]))
		return 0;
	return sys_open(path, O_READ);
}

void in_close(long fd)
{
	if (fd)
		sys_close(fd);
}

void rd_init(struct reader *r, long fd)
{
	r->fd = fd;
	r->n = 0;
	r->i = 0;
}

int rd_getc(struct reader *r)
{
	if (r->i >= r->n) {
		long n = sys_read(r->fd, r->buf, T_READ);
		if (n <= 0)
			return -1;
		r->n = (u64)n;
		r->i = 0;
	}
	return r->buf[r->i++];
}

/* (comme t_lire_ligne : au-delà de T_LINE - 2 octets, le reste de la ligne
 * est perdu ; une dernière ligne sans fin de ligne compte) */
char *rd_line(struct reader *r, u64 *len)
{
	u64 n = 0;
	int c;
	for (;;) {
		c = rd_getc(r);
		if (c < 0) {
			if (!n)
				return 0;
			break;
		}
		if (c == 10)
			break;
		if (n < T_LINE - 2)
			krt_line_buf[n++] = (char)c;
	}
	krt_line_buf[n] = 0;
	if (len)
		*len = n;
	return krt_line_buf;
}
