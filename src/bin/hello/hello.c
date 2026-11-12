/* hello - premier process : affiche, dort, compte, se termine (comme hello.asm : AppKit puts,
 * sans tampon ; la ligne d'arguments telle que le noyau la donne) */
#include "korinrt.h"

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ak_puts("hello : bonjour depuis EL0 ! pid ");
	print_dec(sys_getpid());
	ak_puts("\n");
	if (krt_line) {
		ak_puts("hello : arguments = ");
		ak_puts(krt_line);
		ak_puts("\n");
	}
	for (long i = 1; i <= 2; i++) {
		sys_sleep(1000);
		ak_puts("hello : seconde ");
		print_dec(i);
		ak_puts("\n");
	}
	_exit(0);
}
