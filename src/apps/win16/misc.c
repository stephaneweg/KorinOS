/* misc.c -- win16 : les petites DLL du système (WIN87EM, KEYBOARD, TOOLHELP, VER, SHELL…), d'après winevdm. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "w16.h"
#include "be.h"

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

/* IsDBCSLeadByte : pas de jeu de caractères sur deux octets (Windows d'Europe de l'Ouest) */
void api_IsDBCSLeadByte (struct w16call *c) { c->ret = 0; }

/* ======================================================================== fonctions de Windows 3.11 (21/11) */
/* ---- SHELL ---- */
/* ShellAbout (hwnd, application, autres lignes, icône) : la boîte « À propos » */
void api_ShellAbout16 (struct w16call *c)
{
	char app[256], other[512], t[256], txt[1024];
	w16_to_utf8 (c->a[1] ? far_ptr (c->a[1]) : (const uint8_t *) "", -1, app, sizeof app);
	w16_to_utf8 (c->a[2] ? far_ptr (c->a[2]) : (const uint8_t *) "", -1, other, sizeof other);
	char *hash = strchr (app, '#');	/* (« titre#nom » : le titre avant, le nom après) */
	if (hash)
		*hash = 0;
	snprintf (t, sizeof t, "À propos de %s", app);
	snprintf (txt, sizeof txt, "%s\n%s%s\nWindows 3.x sur Korin OS (win16)", hash ? hash + 1 : app, other[0] ? "\n" : "",
		other);
	be_message (t, txt, 0);
	c->ret = 1;
}

/* le glisser-déposer vers un programme Win16 n'existe pas : aucun fichier */
void api_DragQueryFile16 (struct w16call *c) { c->ret = 0; }
void api_DragFinish16 (struct w16call *c) { (void) c; }
void api_DragQueryPoint16 (struct w16call *c) { c->ret = 0; }

/* ShellExecute (hwnd, opération, fichier, paramètres, dossier, affichage) : un programme (.exe, .com, .bat) est
 * lancé comme WinExec ; un document n'a pas d'association dans win16 : SE_ERR_NOASSOC (31) */
void api_ShellExecute16 (struct w16call *c)
{
	extern uint16_t lz_win_exec (const char *cmd);
	const char *file = c->a[2] ? w16_raw (c->a[2]) : "";
	const char *params = c->a[3] ? w16_raw (c->a[3]) : "";
	const char *dot = strrchr (file, '.');
	if (dot && (!strcasecmp (dot, ".exe") || !strcasecmp (dot, ".com") || !strcasecmp (dot, ".bat")))
	{
		char cmd[512];
		snprintf (cmd, sizeof cmd, "%s%s%s", file, params[0] ? " " : "", params);
		c->ret = lz_win_exec (cmd);
		return;
	}
	c->ret = 31;
}

/* FindExecutable (fichier, dossier, résultat) : un programme se trouve lui-même ; un document : 31 */
void api_FindExecutable16 (struct w16call *c)
{
	const char *file = c->a[0] ? w16_raw (c->a[0]) : "";
	const char *dot = strrchr (file, '.');
	if (dot && (!strcasecmp (dot, ".exe") || !strcasecmp (dot, ".com") || !strcasecmp (dot, ".bat")))
	{
		snprintf (far_ptr (c->a[2]), 144, "%s", file);
		c->ret = 33;
		return;
	}
	if (c->a[2])
		*(char *) far_ptr (c->a[2]) = 0;
	c->ret = 31;
}

/* ExtractIcon (instance, fichier, numéro) : -1 compte les icônes ; ici, aucune icône lue dans un autre fichier */
void api_ExtractIcon16 (struct w16call *c) { c->ret = 0; }
void api_InternalExtractIcon16 (struct w16call *c) { c->ret = 0; }
/* ExtractAssociatedIcon : l'icône d'application par défaut */
void api_ExtractAssociatedIcon16 (struct w16call *c)
{
	extern void api_LoadIcon16 (struct w16call *);
	struct w16call d = { .a = { 0, 32512 }, .n = 2 };	/* IDI_APPLICATION */
	api_LoadIcon16 (&d);
	c->ret = d.ret;
}

/* DoEnvironmentSubst (chaîne, taille) : %VAR% remplacé par l'environnement DOS du programme (PATH, TEMP…) ;
 * -> (1 ou 0 si trop long) | longueur << 16 */
void api_DoEnvironmentSubst16 (struct w16call *c)
{
	char *s = far_ptr (c->a[0]);
	int cap = (uint16_t) c->a[1];
	char out[1024];
	int o = 0, ok = 1;
	for (const char *p = s; *p && o < (int) sizeof out - 1;)
	{
		const char *e = *p == '%' ? strchr (p + 1, '%') : 0;
		if (e && e > p + 1)
		{
			char name[64];
			snprintf (name, sizeof name, "%.*s", (int) (e - p - 1), p + 1);
			const char *v = !strcasecmp (name, "WINDIR") || !strcasecmp (name, "PATH") ? "C:\\WINDOWS"
				: !strcasecmp (name, "TEMP") || !strcasecmp (name, "TMP") ? "C:\\WINDOWS\\TEMP" : 0;
			if (v)
			{
				o += snprintf (out + o, sizeof out - (size_t) o, "%s", v);
				p = e + 1;
				continue;
			}
		}
		out[o++] = *p++;
	}
	out[o] = 0;
	if (o + 1 > cap)
		ok = 0;
	else
		memcpy (s, out, (size_t) o + 1);
	c->ret = (uint32_t) ok | (uint32_t) (uint16_t) (ok ? o : (int) strlen (s)) << 16;
}

void api_FindEnvironmentString16 (struct w16call *c) { c->ret = 0; }
void api_RegisterShellHook16 (struct w16call *c) { c->ret = 1; }	/* (aucun événement du shell : jamais appelé) */

/* ---- les ports série (COMM.DRV) : aucun port sous Korin ; OpenComm rend IE_BADID (-1) ---- */
void api_OpenComm16 (struct w16call *c) { c->ret = 0xffff; }
void api_CloseComm16 (struct w16call *c) { c->ret = 0xffff; }
void api_ReadComm16 (struct w16call *c) { c->ret = 0; }
void api_WriteComm16 (struct w16call *c) { c->ret = 0; }
void api_GetCommError16 (struct w16call *c) { c->ret = 0; }
void api_GetCommState16 (struct w16call *c) { c->ret = 0xffff; }
void api_SetCommState16 (struct w16call *c) { c->ret = 0xffff; }
void api_FlushComm16 (struct w16call *c) { c->ret = 0xffff; }
void api_SetCommEventMask16 (struct w16call *c) { c->ret = 0; }
void api_EscapeCommFunction16 (struct w16call *c) { c->ret = 0xffff; }
void api_SetCommBreak16 (struct w16call *c) { c->ret = 0xffff; }
void api_ClearCommBreak16 (struct w16call *c) { c->ret = 0xffff; }

/* ---- le réseau de Windows for Workgroups (WNet) : pas de réseau Microsoft sous win16 : WN_NOT_SUPPORTED (1) ---- */
#define WN_NOT_SUPPORTED 1
void api_WNetGetConnection16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetGetCaps16 (struct w16call *c) { c->ret = 0; }	/* (aucune capacité) */
void api_WNetAddConnection16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetCancelConnection16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetGetErrorText16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetErrorText16 (struct w16call *c) { c->ret = 0; }
void api_WNetGetDirectoryType16 (struct w16call *c) { if (c->a[1]) wr16 (far_lin (c->a[1]), 0); c->ret = WN_NOT_SUPPORTED; }
void api_WNetDirectoryNotify16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetBrowseDialog16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetConnectDialog16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetDisconnectDialog16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetConnectionDialog16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetPropertyDialog16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetGetPropertyText16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }
void api_WNetRestoreConnection16 (struct w16call *c) { c->ret = WN_NOT_SUPPORTED; }

/* ---- les pilotes installables (OpenDriver) : aucun ---- */
void api_OpenDriver16 (struct w16call *c) { c->ret = 0; }
void api_CloseDriver16 (struct w16call *c) { c->ret = 0; }

/* ---- SOUND.DRV (les voix du haut-parleur de Windows 1-3) : pas de pilote : S_SERDVNA (-1) ---- */
void api_OpenSound16 (struct w16call *c) { c->ret = 0xffff; }
void api_CloseSound16 (struct w16call *c) { c->ret = 0; }
void api_SetVoiceNote16 (struct w16call *c) { c->ret = 0; }
void api_SetVoiceAccent16 (struct w16call *c) { c->ret = 0; }
void api_StartSound16 (struct w16call *c) { c->ret = 0; }
void api_StopSound16 (struct w16call *c) { c->ret = 0; }
