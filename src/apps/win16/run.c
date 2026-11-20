/* run.c -- win16 : la boucle du processeur, les points d'entrée des DLL intégrées, les interruptions et les
 * rappels vers le code 16 bits.
 *
 * winevdm reconnaît un appel de l'API quand EIP vaut l'adresse native de __wine_call_from_16 (vm86/msdos.cpp,
 * vm86main) ; ici, chaque DLL intégrée a un sélecteur de code à elle, l'ordinal sert de décalage, et la boucle
 * regarde CS avant chaque instruction. Les interruptions (INT 21h…) passent par l'IDT vers un autre sélecteur,
 * dont le décalage est le numéro du vecteur. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "w16.h"

int w16_exit, w16_exit_code, w16_trace;
static uint16_t sel_mod[8];	/* le sélecteur des points d'entrée de chaque DLL intégrée */
static int nmods;
static uint16_t sel_int, sel_ret;
static int depth;
static const struct w16_api *ord_index[8][1024];

void w16_log (const char *fmt, ...)
{
	if (!w16_trace)
		return;
	va_list a;
	va_start (a, fmt);
	vfprintf (stderr, fmt, a);
	va_end (a);
}

void be_fatal (const char *msg);

void w16_fatal (const char *fmt, ...)
{
	char buf[512];
	va_list a;
	va_start (a, fmt);
	vsnprintf (buf, sizeof buf, fmt, a);
	va_end (a);
	fprintf (stderr, "win16 : %s (CS:IP %04x:%04x)\n", buf, cpu_sreg (S_CS), cpu_eip () & 0xffff);
	be_fatal (buf);
	exit (3);
}

int api_find_module (const char *name)
{
	for (int m = 0; w16_modules[m].name; m++)
		if (!strcasecmp (w16_modules[m].name, name))
			return m;
	return -1;
}

int api_find_name (int mod, const char *name)
{
	const struct w16_module *m = &w16_modules[mod];
	for (int i = 0; i < m->count; i++)
		if (!strcasecmp (m->api[i].name, name))
			return m->api[i].ord;
	return -1;
}

uint32_t api_entry (int mod, int ord)
{
	return MAKESEGPTR (sel_mod[mod], ord);
}

void push16 (uint16_t v)
{
	uint32_t sp = (cpu_reg (R_ESP) - 2) & 0xffff;
	cpu_set_reg (R_ESP, (cpu_reg (R_ESP) & 0xffff0000u) | sp);
	wr16 (cpu_sreg_base (S_SS) + sp, v);
}

uint16_t pop16 (void)
{
	uint32_t sp = cpu_reg (R_ESP) & 0xffff;
	uint16_t v = rd16 (cpu_sreg_base (S_SS) + sp);
	cpu_set_reg (R_ESP, (cpu_reg (R_ESP) & 0xffff0000u) | ((sp + 2) & 0xffff));
	return v;
}

static int arg_size (char t)
{
	return t == 'w' || t == 's' ? 2 : t == 'd' ? 8 : 4;
}

/* un appel d'une DLL intégrée : CS = son sélecteur, IP = l'ordinal, la pile porte l'adresse de retour (RETF) puis
 * les arguments */
static void api_dispatch (int mod, int ord)
{
	const struct w16_api *e = ord < 1024 ? ord_index[mod][ord] : 0;
	uint32_t ss = cpu_sreg_base (S_SS);
	uint16_t sp = (uint16_t) cpu_reg (R_ESP);
	uint16_t ret_ip = rd16 (ss + sp), ret_cs = rd16 (ss + sp + 2);
	if (!e || !e->fn)
		w16_fatal ("fonction pas encore écrite : %s.%d (%s), appelée de %04x:%04x", w16_modules[mod].name, ord,
			e ? e->name : "?", ret_cs, ret_ip);
	struct w16call c;
	memset (&c, 0, sizeof c);
	c.mod = mod;
	c.ord = ord;
	int total = 0;
	if (e->conv == 0 || e->conv == 2)
	{
		for (const char *t = e->args; *t; t++)
			total += arg_size (*t);
		uint32_t p = e->conv == 0 ? ss + sp + 4 + (uint32_t) total : ss + sp + 4;
		for (const char *t = e->args; *t && c.n < 16; t++)
		{
			int s = arg_size (*t);
			if (e->conv == 0)
				p -= (uint32_t) s;
			uint32_t v = s == 2 ? rd16 (p) : rd32 (p);
			if (*t == 's')
				v = (uint32_t) (int32_t) (int16_t) v;
			c.a[c.n++] = v;
			if (e->conv == 2)
				p += (uint32_t) s;
		}
	}
	if (w16_trace)
	{
		fprintf (stderr, "-> %s.%s(", w16_modules[mod].name, e->name);
		for (int i = 0; i < c.n; i++)
			fprintf (stderr, i ? ", %x" : "%x", c.a[i]);
		fprintf (stderr, ") de %04x:%04x\n", ret_cs, ret_ip);
	}
	if (e->conv == 1)
	{
		/* (une fonction « register » lit et règle les registres elle-même ; c.ret = mots à empiler après) */
		e->fn (&c);
		cpu_set_reg (R_ESP, (cpu_reg (R_ESP) & 0xffff0000u) | (uint16_t) (cpu_reg (R_ESP) + 4));
		for (int i = 0; i < (int) c.ret; i++)
			push16 (0);
		cpu_jump (ret_cs, ret_ip);
		return;
	}
	uint32_t si = cpu_reg (R_ESI), di = cpu_reg (R_EDI), bp = cpu_reg (R_EBP), esp = cpu_reg (R_ESP);
	uint16_t ds = cpu_sreg (S_DS), ssel = cpu_sreg (S_SS);
	e->fn (&c);
	if (w16_exit)
		return;
	cpu_set_sreg (S_SS, ssel);
	cpu_set_reg (R_ESI, si);
	cpu_set_reg (R_EDI, di);
	cpu_set_reg (R_EBP, bp);
	cpu_set_sreg (S_DS, sel_valid (ds) ? ds : 0);
	cpu_set_reg (R_ESP, (esp & 0xffff0000u) | (uint16_t) (sp + 4 + (e->conv == 0 ? total : 0)));
	cpu_set_reg (R_EAX, (cpu_reg (R_EAX) & 0xffff0000u) | (c.ret & 0xffff));
	if (!e->ret16)
		cpu_set_reg (R_EDX, (cpu_reg (R_EDX) & 0xffff0000u) | (c.ret >> 16));
	if (w16_trace)
		fprintf (stderr, "<- %s = %x\n", e->name, c.ret);
	cpu_jump (ret_cs, ret_ip);
}

/* une interruption : CS = sel_int, IP = le vecteur ; la pile porte IP, CS, FLAGS (porte 16 bits) */
static void int_dispatch (int vec)
{
	static const uint8_t has_err[32] = { [8] = 1, [10] = 1, [11] = 1, [12] = 1, [13] = 1, [14] = 1, [17] = 1 };
	if (vec < 0x20 && vec != 0x10 && vec != 0x16 && vec != 0x1a)
	{
		uint16_t err = has_err[vec] ? pop16 () : 0;
		uint16_t ip = pop16 (), cs = pop16 ();
		w16_fatal ("exception %d (code %04x) en %04x:%04x", vec, err, cs, ip);
	}
	uint32_t ss = cpu_sreg_base (S_SS);
	uint16_t sp = (uint16_t) cpu_reg (R_ESP);
	uint16_t flags = rd16 (ss + sp + 4);
	/* le gestionnaire voit les drapeaux d'avant l'INT (CF rendu par cpu_flags) */
	cpu_set_flags ((cpu_flags () & ~0xffffu) | flags);
	switch (vec)
	{
	case 0x21:
		dos_int21 ();
		break;
	case 0x2f:	/* multiplex : rien d'installé */
		break;
	default:
		w16_log ("INT %02xh ignorée (AX=%04x)\n", vec, cpu_reg (R_EAX) & 0xffff);
		break;
	}
	if (w16_exit)
		return;
	uint16_t ip = pop16 (), cs = pop16 ();
	uint16_t f = pop16 ();
	f = (uint16_t) ((f & ~0x8d5u) | (cpu_flags () & 0x8d5u));	/* CF, PF, AF, ZF, SF, OF rendus par le gestionnaire */
	cpu_jump (cs, ip);
	cpu_set_flags ((cpu_flags () & ~0xffffu) | f | 0x200);
}

static int run_loop (int stop_off)
{
	for (;;)
	{
		if (w16_exit)
			return 0;
		uint16_t cs = cpu_sreg (S_CS);
		uint32_t ip = cpu_eip ();
		if (cs == sel_ret)
		{
			if ((int) ip == stop_off)
				return 1;
			w16_fatal ("retour de rappel inattendu (%d au lieu de %d)", (int) ip, stop_off);
		}
		if (cs == sel_int)
		{
			int_dispatch ((int) ip);
			continue;
		}
		int handled = 0;
		for (int m = 0; m < nmods; m++)
			if (cs == sel_mod[m])
			{
				api_dispatch (m, (int) ip);
				handled = 1;
				break;
			}
		if (handled)
			continue;
		if (w16_trace > 1)
		{
			char buf[128];
			cpu_disasm (buf, cs, ip);
			fprintf (stderr, "%04x:%04x %-32s AX=%04x BX=%04x CX=%04x DX=%04x SP=%04x DS=%04x ES=%04x\n", cs, ip, buf,
				cpu_reg (R_EAX) & 0xffff, cpu_reg (R_EBX) & 0xffff, cpu_reg (R_ECX) & 0xffff,
				cpu_reg (R_EDX) & 0xffff, cpu_reg (R_ESP) & 0xffff, cpu_sreg (S_DS), cpu_sreg (S_ES));
		}
		cpu_step ();
	}
}

int run_until_return (uint16_t ret_off)
{
	return run_loop (ret_off);
}

/* appelle une fonction 16 bits (convention Pascal : les mots empilés dans l'ordre), DS = AX = ds ; rend DX:AX */
uint32_t call16 (uint32_t target, const uint16_t *args, int nwords, uint16_t ds)
{
	uint32_t regs[8];
	for (int r = 0; r < 8; r++)
		regs[r] = cpu_reg (r);
	uint16_t sregs[6];
	for (int s = 0; s < 6; s++)
		sregs[s] = cpu_sreg (s);
	uint32_t eip = cpu_eip (), flags = cpu_flags ();
	depth++;
	if (depth > 200)
		w16_fatal ("rappels trop imbriqués");
	uint16_t ret_off = (uint16_t) depth;
	for (int i = 0; i < nwords; i++)
		push16 (args[i]);
	push16 (sel_ret);
	push16 (ret_off);
	cpu_set_sreg (S_DS, ds);
	cpu_set_sreg (S_ES, 0);
	cpu_set_reg (R_EAX, ds);
	cpu_jump (SELECTOROF (target), OFFSETOF (target));
	run_loop (ret_off);
	uint32_t ret = (cpu_reg (R_EAX) & 0xffff) | (cpu_reg (R_EDX) & 0xffff) << 16;
	depth--;
	if (w16_exit)
		return ret;
	for (int r = 0; r < 8; r++)
		cpu_set_reg (r, regs[r]);
	cpu_set_sreg (S_SS, sregs[S_SS]);
	cpu_set_sreg (S_DS, sel_valid (sregs[S_DS]) ? sregs[S_DS] : 0);
	cpu_set_sreg (S_ES, sel_valid (sregs[S_ES]) ? sregs[S_ES] : 0);
	cpu_jump (sregs[S_CS], eip);
	cpu_set_flags (flags);
	return ret;
}

void run_init (void)
{
	/* les points d'entrée : 64 Ko de zéros partagés, un sélecteur de code par DLL intégrée */
	for (nmods = 0; w16_modules[nmods].name && nmods < 8; nmods++)
	{
		sel_mod[nmods] = sel_alloc (THUNK_LIN, 0x10000, 1);
		for (int i = 0; i < w16_modules[nmods].count; i++)
		{
			const struct w16_api *e = &w16_modules[nmods].api[i];
			if (e->ord < 1024)
				ord_index[nmods][e->ord] = e;
		}
	}
	sel_int = sel_alloc (THUNK_LIN, 0x10000, 1);
	sel_ret = sel_alloc (THUNK_LIN, 0x10000, 1);
	/* l'IDT : 256 portes d'interruption 16 bits (type 6), DPL 3, vers sel_int:vecteur */
	for (int v = 0; v < 256; v++)
	{
		uint8_t *g = lin_ptr (IDT_LIN + (uint32_t) v * 8);
		g[0] = (uint8_t) v;
		g[1] = 0;
		g[2] = (uint8_t) sel_int;
		g[3] = (uint8_t) (sel_int >> 8);
		g[4] = 0;
		g[5] = 0x80 | 0x60 | 0x06;
		g[6] = 0;
		g[7] = 0;
	}
	cpu_set_tables (LDT_LIN, LDT_ENTRIES * 8 - 1, IDT_LIN, 256 * 8 - 1);
}
