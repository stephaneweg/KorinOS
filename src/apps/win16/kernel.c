/* kernel.c -- win16 : KERNEL (tâche, version, modules) et les appels DOS (INT 21h) dont les programmes Win16 se
 * servent encore. D'après krnl386/task.c et int21.c de winevdm (Wine, LGPL-2.1+). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "w16.h"
#include "be.h"

/* ---- chaînes : le programme parle CP1252 (Windows 3.1 occidental), Korin UTF-8 ---- */
static const uint16_t cp1252_hi[32] = {
	0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d,
	0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d,
	0x17e, 0x178 };

int w16_to_utf8 (const uint8_t *s, int n, char *out, int cap)
{
	int o = 0;
	for (int i = 0; (n < 0 ? s[i] : i < n) && o < cap - 4; i++)
	{
		unsigned c = s[i];
		if (c >= 0x80 && c < 0xa0)
			c = cp1252_hi[c - 0x80];
		if (c < 0x80)
			out[o++] = (char) c;
		else if (c < 0x800)
		{
			out[o++] = (char) (0xc0 | c >> 6);
			out[o++] = (char) (0x80 | (c & 0x3f));
		}
		else
		{
			out[o++] = (char) (0xe0 | c >> 12);
			out[o++] = (char) (0x80 | ((c >> 6) & 0x3f));
			out[o++] = (char) (0x80 | (c & 0x3f));
		}
	}
	out[o] = 0;
	return o;
}

const char *w16_str (uint32_t segptr)
{
	static char bufs[8][1024];
	static int k;
	char *b = bufs[k++ & 7];
	if (!segptr)
	{
		b[0] = 0;
		return b;
	}
	if (!SELECTOROF (segptr))
	{
		snprintf (b, 1024, "#%u", OFFSETOF (segptr));	/* (un atome ou un numéro de ressource) */
		return b;
	}
	w16_to_utf8 (far_ptr (segptr), -1, b, 1024);
	return b;
}

/* ---- la tâche ---- */
void kernel_init (void)
{
	/* le PSP et l'environnement, comme sous DOS : la ligne de commande en 0x80, le chemin après l'environnement */
	task.env = global_alloc (512, 0);
	uint8_t *e = lin_ptr (sel_base (task.env));
	int o = sprintf ((char *) e, "PATH=C:\\WINDOWS") + 1;
	o += sprintf ((char *) e + o, "WINDIR=C:\\WINDOWS") + 1;
	e[o++] = 0;
	e[o++] = 1;
	e[o++] = 0;
	strcpy ((char *) e + o, task.dospath);
	task.psp = global_alloc (256, 0);
	uint8_t *p = lin_ptr (sel_base (task.psp));
	p[0] = 0xcd;
	p[1] = 0x20;
	wr16 (sel_base (task.psp) + 0x2c, task.env);
}

void kernel_set_cmdline (const char *args)
{
	uint8_t *p = lin_ptr (sel_base (task.psp));
	int n = args ? (int) strlen (args) : 0;
	if (n > 125)
		n = 125;
	p[0x80] = (uint8_t) (n + (n ? 1 : 0));
	int o = 0x81;
	if (n)
		p[o++] = ' ';
	memcpy (p + o, args, (size_t) n);
	p[o + n] = 0x0d;
}

/* InitTask (KERNEL.91, registres) : INSTANCEDATA au début de DS, registres de retour, un mot 0 empilé */
void api_InitTask16 (struct w16call *c)
{
	uint32_t ds = cpu_sreg_base (S_DS);
	uint16_t sp = (uint16_t) cpu_reg (R_ESP);
	uint16_t stackmin = (uint16_t) (sp + 4);
	uint16_t bx = (uint16_t) cpu_reg (R_EBX);
	uint16_t stacktop = (uint16_t) ((stackmin > bx ? stackmin - bx : 0) + 150);
	wr16 (ds + 0x0a, stacktop);
	wr16 (ds + 0x0c, stackmin);
	wr16 (ds + 0x0e, stackmin);
	uint8_t *psp = lin_ptr (sel_base (task.psp));
	int bxo = 0x80;
	if (psp[0x80])
	{
		bxo = 0x81;
		while (psp[bxo] == ' ' || psp[bxo] == '\t')
			bxo++;
	}
	cpu_set_reg (R_EAX, 1);
	cpu_set_reg (R_EBX, (uint32_t) bxo);
	cpu_set_reg (R_ECX, stacktop);
	cpu_set_reg (R_EDX, task.cmdshow);
	cpu_set_reg (R_ESI, 0);
	cpu_set_reg (R_EDI, task.hinstance);
	cpu_set_sreg (S_ES, task.psp);
	c->ret = 1;	/* (un mot 0 empilé au retour : le « BP » de Windows) */
}

void api_WaitEvent16 (struct w16call *c) { c->ret = 0; }

void api_GetVersion16 (struct w16call *c)
{
	c->ret = 0x05000a03;	/* Windows 3.10 sur MS-DOS 5.0 */
}

void api_GetModuleFileName16 (struct w16call *c)
{
	uint32_t buf = c->a[1];
	int n = (int16_t) c->a[2];
	if (n <= 0)
	{
		c->ret = 0;
		return;
	}
	int l = (int) strlen (task.dospath);
	if (l > n - 1)
		l = n - 1;
	memcpy (far_ptr (buf), task.dospath, (size_t) l);
	((char *) far_ptr (buf))[l] = 0;
	c->ret = (uint32_t) l;
}

void api_GetModuleHandle16 (struct w16call *c)
{
	(void) c;
	c->ret = task.hinstance;
}

void api_GetCurrentTask (struct w16call *c) { c->ret = task.psp; }
void api_GetTickCount (struct w16call *c) { c->ret = be_ticks (); }
void api_GetCurrentTime16 (struct w16call *c) { c->ret = be_ticks (); }
void api_LocalInit16 (struct w16call *c) { c->ret = 1; }
void api_LockSegment16 (struct w16call *c) { c->ret = c->a[0]; }
void api_UnlockSegment16 (struct w16call *c) { c->ret = c->a[0]; }

/* ---- INT 21h : ce que le démarrage des compilateurs C de l'époque demande ---- */
static void set_cf (int on)
{
	cpu_set_flags (on ? cpu_flags () | 1 : cpu_flags () & ~1u);
}

#define AX ((uint16_t) cpu_reg (R_EAX))
#define AH ((uint8_t) (cpu_reg (R_EAX) >> 8))
#define AL ((uint8_t) cpu_reg (R_EAX))
#define BX ((uint16_t) cpu_reg (R_EBX))
#define CX ((uint16_t) cpu_reg (R_ECX))
#define DX ((uint16_t) cpu_reg (R_EDX))
static void set16 (int r, uint16_t v) { cpu_set_reg (r, (cpu_reg (r) & 0xffff0000u) | v); }

void w16_terminate (int code);

void dos_int21 (void)
{
	set_cf (0);
	switch (AH)
	{
	case 0x02:	/* écrire un caractère */
		fputc (DX & 0xff, stderr);
		break;
	case 0x09:	/* écrire une chaîne terminée par $ */
	{
		const char *s = (const char *) lin_ptr (cpu_sreg_base (S_DS) + DX);
		for (int i = 0; s[i] != '$' && i < 4096; i++)
			fputc (s[i], stderr);
		break;
	}
	case 0x19:	/* lecteur courant : C: */
		set16 (R_EAX, (uint16_t) (AX & 0xff00) | 2);
		break;
	case 0x1a:	/* adresse du DTA */
	case 0x0e:
		break;
	case 0x25:	/* vecteur d'interruption : retenu pour rien (les programmes Win16 se servent des exceptions de Windows) */
		break;
	case 0x35:
		cpu_set_sreg (S_ES, 0);
		set16 (R_EBX, 0);
		break;
	case 0x2a:	/* date */
	case 0x2c:	/* heure */
	{
		time_t t = time (0);
		struct tm *tm = localtime (&t);
		if (AH == 0x2a)
		{
			set16 (R_ECX, (uint16_t) (tm->tm_year + 1900));
			set16 (R_EDX, (uint16_t) ((tm->tm_mon + 1) << 8 | tm->tm_mday));
			set16 (R_EAX, (uint16_t) ((AX & 0xff00) | tm->tm_wday));
		}
		else
		{
			set16 (R_ECX, (uint16_t) (tm->tm_hour << 8 | tm->tm_min));
			set16 (R_EDX, (uint16_t) (tm->tm_sec << 8));
		}
		break;
	}
	case 0x30:	/* version de DOS : 5.0 */
		set16 (R_EAX, 0x0005);
		set16 (R_EBX, 0);
		set16 (R_ECX, 0);
		break;
	case 0x33:	/* Ctrl-Break */
		set16 (R_EDX, 0);
		break;
	case 0x40:	/* écrire dans un fichier : seulement la console (1, 2) */
		if (BX == 1 || BX == 2)
		{
			fwrite (lin_ptr (cpu_sreg_base (S_DS) + DX), 1, CX, stderr);
			set16 (R_EAX, CX);
		}
		else
		{
			set16 (R_EAX, 6);
			set_cf (1);
		}
		break;
	case 0x44:	/* IOCTL : les poignées 0..4 sont des périphériques */
		if (AL == 0 && BX < 5)
			set16 (R_EDX, BX == 0 ? 0x81 : BX == 1 ? 0x82 : 0x80);
		else
		{
			set16 (R_EAX, 1);
			set_cf (1);
		}
		break;
	case 0x4c:	/* fin du programme */
		w16_terminate (AL);
		break;
	case 0x51:
	case 0x62:	/* le PSP */
		set16 (R_EBX, task.psp);
		break;
	case 0x47:	/* dossier courant : la racine */
		*(char *) lin_ptr (cpu_sreg_base (S_DS) + (uint16_t) cpu_reg (R_ESI)) = 0;
		break;
	default:
		w16_log ("INT 21h AH=%02xh pas prise en charge\n", AH);
		set16 (R_EAX, 1);
		set_cf (1);
		break;
	}
}
