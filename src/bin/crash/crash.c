/* crash - essai de l'isolation, comme crash.asm : un pointeur du noyau donné
 * à un appel système est refusé (SYS_PRINT de 0x80000 : le code d'erreur est
 * affiché), puis, 2 s plus tard, une lecture de la mémoire du noyau en EL0
 * (ring 3 sur PC) : le process est tué, pas le noyau. « ERREUR, toujours
 * vivant » ne doit jamais sortir (code 0 dans ce cas, comme l'asm). Sorties
 * par puts et print_dec d'AppKit (sans tampon). */
#include "korinrt.h"

#define KERNEL_ADDR 0x80000L /* adresse du noyau (Pi) ; sur PC aussi hors du process */

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ak_puts("crash : print(pointeur noyau) -> ");
	long r = ksys(SYS_PRINT, KERNEL_ADDR, 16, 0, 0, 0, 0);
	ak_puts("retour ");
	print_dec(r);
	ak_puts("\n");
	sys_sleep(2000);
	ak_puts("crash : lecture de la memoire du noyau...\n");
	long v;
#if defined(__aarch64__)
	/* (une vraie instruction ldr : le compilateur ne doit pas l'ôter) */
	__asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"(KERNEL_ADDR) : "memory");
#else
	__asm__ volatile("movq (%1), %0" : "=r"(v) : "r"(KERNEL_ADDR) : "memory");
#endif
	(void)v;
	ak_puts("crash : ERREUR, toujours vivant\n"); /* jamais atteint */
	_exit(0);
}
