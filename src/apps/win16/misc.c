/* misc.c -- win16 : les petites DLL du système (WIN87EM, KEYBOARD, TOOLHELP, VER, SHELL…), d'après winevdm. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "w16.h"

/* ---- WIN87EM : le processeur émulé a son x87 ; l'émulateur de Windows n'a presque rien à faire ---- */
static void set16 (int r, uint16_t v) { cpu_set_reg (r, (cpu_reg (r) & 0xffff0000u) | v); }

void api__fpMath (struct w16call *c)
{
	(void) c;
	static uint16_t ctrl = 0x1332;
	switch ((uint16_t) cpu_reg (R_EBX))
	{
	case 0:	/* installer */
	case 2:	/* désinstaller */
	case 8:
	case 9:
	case 10:
		set16 (R_EAX, 0);
		break;
	case 1:
	case 3:
	case 4:
	case 12:
		break;
	case 5:
		set16 (R_EAX, ctrl);
		break;
	case 11:	/* installé ? un x87 est toujours là */
		set16 (R_EDX, 0);
		set16 (R_EAX, 1);
		break;
	default:
		w16_log ("__fpMath %d pas pris en charge\n", (uint16_t) cpu_reg (R_EBX));
		set16 (R_EAX, 0);
		break;
	}
}

/* GetKBCodePage : la page OEM des fichiers DOS (437, comme AnsiToOem) */
void api_GetKBCodePage16 (struct w16call *c) { c->ret = 437; }

/* ---- KEYBOARD : OEM = CP437 ; ici, copie telle quelle (les accents de l'OEM ne servent qu'aux fichiers DOS) ---- */
void api_AnsiToOem16 (struct w16call *c)
{
	const char *s = far_ptr (c->a[0]);
	if (c->a[1] != c->a[0])
		memmove (far_ptr (c->a[1]), s, strlen (s) + 1);
	c->ret = 1;
}

void api_OemToAnsi16 (struct w16call *c) { api_AnsiToOem16 (c); }

/* ---- TOOLHELP ---- */
void api_InterruptRegister16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_InterruptUnRegister16 (struct w16call *c) { (void) c; c->ret = 1; }

void api_TerminateApp16 (struct w16call *c)
{
	(void) c;
	exit (0);
}

void api_GlobalEntryHandle16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_ModuleFindHandle16 (struct w16call *c) { (void) c; c->ret = 0; }
