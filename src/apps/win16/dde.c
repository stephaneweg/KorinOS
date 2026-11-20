/* dde.c -- win16 : DDEML (la DDE des programmes de Windows 3.1), le peu qu'il faut pour que ses clients continuent.
 *
 * Sous Korin, il n'y a pas de serveur DDE (le Gestionnaire de programmes de Windows, PROGMAN, que les installateurs
 * appellent pour créer un groupe d'icônes) : DdeInitialize réussit, DdeConnect ne trouve personne (comme un Windows
 * où le serveur ne tourne pas), et le programme fait sans. Les poignées de chaînes sont gardées ici. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "w16.h"

#define MAX_HSZ 256
static char *hsz[MAX_HSZ];
static uint16_t last_error;

void api_DdeInitialize16 (struct w16call *c)
{
	if (c->a[0])
		wr32 (far_lin (c->a[0]), 0x4b4f);	/* (l'identifiant de l'instance) */
	last_error = 0;
	c->ret = 0;	/* DMLERR_NO_ERROR */
}

void api_DdeUninitialize16 (struct w16call *c) { c->ret = 1; }

void api_DdeConnect16 (struct w16call *c)
{
	(void) c;
	w16_log ("DDE : pas de serveur sous Korin (DdeConnect)\n");
	last_error = 0x400a;	/* DMLERR_NO_CONV_ESTABLISHED */
	c->ret = 0;
}

void api_DdeConnectList16 (struct w16call *c) { last_error = 0x400a; c->ret = 0; }
void api_DdeGetLastError16 (struct w16call *c) { c->ret = last_error; last_error = 0; }
void api_DdeClientTransaction16 (struct w16call *c) { last_error = 0x400a; c->ret = 0; }
void api_DdeNameService16 (struct w16call *c) { c->ret = 1; }

void api_DdeCreateStringHandle16 (struct w16call *c)
{
	const char *s = w16_raw (c->a[1]);
	for (int i = 1; i < MAX_HSZ; i++)
		if (hsz[i] && !strcasecmp (hsz[i], s))
		{
			c->ret = (uint32_t) i;
			return;
		}
	for (int i = 1; i < MAX_HSZ; i++)
		if (!hsz[i])
		{
			hsz[i] = strdup (s);
			c->ret = (uint32_t) i;
			return;
		}
	c->ret = 0;
}

void api_DdeFreeStringHandle16 (struct w16call *c) { c->ret = 1; }
void api_DdeKeepStringHandle16 (struct w16call *c) { c->ret = 1; }

void api_DdeQueryString16 (struct w16call *c)
{
	uint32_t i = c->a[1];
	const char *s = i < MAX_HSZ && hsz[i] ? hsz[i] : "";
	char *d = far_ptr (c->a[2]);
	uint32_t cap = c->a[3];
	if (d && cap)
		snprintf (d, cap, "%s", s);
	c->ret = (uint32_t) strlen (s);
}

void api_DdeCmpStringHandles16 (struct w16call *c)
{
	uint32_t a = c->a[0], b = c->a[1];
	const char *sa = a < MAX_HSZ && hsz[a] ? hsz[a] : "", *sb = b < MAX_HSZ && hsz[b] ? hsz[b] : "";
	int r = strcasecmp (sa, sb);
	c->ret = (uint16_t) (r < 0 ? -1 : r > 0 ? 1 : 0);
}
