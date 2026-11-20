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

/* ---- SHELL : le registre (REG.DAT de Windows 3.1) ; ici un registre vide : la racine s'ouvre, aucune
 * sous-clé n'existe, l'énumération est finie tout de suite ; créer et écrire réussissent sans rien garder
 * (le Gestionnaire de fichiers énumère HKEY_CLASSES_ROOT : un « succès » à chaque RegEnumKey bouclait) ---- */
#define REG_ERROR_BADKEY	2
#define REG_ERROR_NO_MORE_ITEMS	259
void api_RegOpenKey16 (struct w16call *c)
{
	const char *sub = c->a[1] ? w16_raw (c->a[1]) : "";
	if (sub[0])
	{
		c->ret = REG_ERROR_BADKEY;
		return;
	}
	if (c->a[2])
		wr32 (far_lin (c->a[2]), c->a[0]);
	c->ret = 0;
}
void api_RegCreateKey16 (struct w16call *c)
{
	static uint32_t next = 0x100;
	if (c->a[2])
		wr32 (far_lin (c->a[2]), next++);
	c->ret = 0;
}
void api_RegCloseKey16 (struct w16call *c) { c->ret = 0; }
void api_RegDeleteKey16 (struct w16call *c) { c->ret = 0; }
void api_RegSetValue16 (struct w16call *c) { c->ret = 0; }
void api_RegQueryValue16 (struct w16call *c) { c->ret = REG_ERROR_BADKEY; }
void api_RegEnumKey16 (struct w16call *c) { c->ret = REG_ERROR_NO_MORE_ITEMS; }

/* DragAcceptFiles : le glisser-déposer du Gestionnaire de fichiers ; pas de dépôt vers un programme Win16 */
void api_DragAcceptFiles16 (struct w16call *c) { (void) c; }
