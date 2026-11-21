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
static uint16_t sel_mod[MAX_BUILTIN];	/* le sélecteur des points d'entrée de chaque DLL intégrée */
static int nmods;
static uint16_t sel_native, sel_int, sel_ret;

/* les procédures de fenêtre écrites en C (contrôles, dialogues) : une adresse 16 bits chacune (sel_native:n), pour
 * que le programme puisse les garder, les sous-classer et les rappeler (CallWindowProc) */
#define MAX_NATIVE 64
static native_wndproc natives[MAX_NATIVE];
static int nnatives;

uint32_t native_register (native_wndproc fn)
{
	for (int i = 0; i < nnatives; i++)
		if (natives[i] == fn)
			return MAKESEGPTR (sel_native, i);
	if (nnatives >= MAX_NATIVE)
		w16_fatal ("trop de procédures natives");
	natives[nnatives] = fn;
	return MAKESEGPTR (sel_native, nnatives++);
}

native_wndproc native_get (uint32_t proc)
{
	if (SELECTOROF (proc) != sel_native || OFFSETOF (proc) >= nnatives)
		return 0;
	return natives[OFFSETOF (proc)];
}

/* appelée par le code 16 bits (une procédure native rangée par le programme) : Pascal, hwnd msg wParam lParam */
static void native_dispatch (int n)
{
	uint32_t ss = cpu_sreg_base (S_SS);
	uint16_t sp = (uint16_t) cpu_reg (R_ESP);
	uint16_t ret_ip = rd16 (ss + sp), ret_cs = rd16 (ss + sp + 2);
	uint32_t lp = rd32 (ss + sp + 4);
	uint16_t wp = rd16 (ss + sp + 8), msg = rd16 (ss + sp + 10), hwnd = rd16 (ss + sp + 12);
	uint32_t si = cpu_reg (R_ESI), di = cpu_reg (R_EDI), bp = cpu_reg (R_EBP), esp = cpu_reg (R_ESP);
	uint16_t ds = cpu_sreg (S_DS), ssel = cpu_sreg (S_SS);
	uint32_t r = n < nnatives ? natives[n] (hwnd, msg, wp, lp) : 0;
	if (w16_exit)
		return;
	cpu_set_sreg (S_SS, ssel);
	cpu_set_reg (R_ESI, si);
	cpu_set_reg (R_EDI, di);
	cpu_set_reg (R_EBP, bp);
	cpu_set_sreg (S_DS, sel_valid (ds) ? ds : 0);
	cpu_set_reg (R_ESP, (esp & 0xffff0000u) | (uint16_t) (sp + 14));
	cpu_set_reg (R_EAX, (cpu_reg (R_EAX) & 0xffff0000u) | (r & 0xffff));
	cpu_set_reg (R_EDX, (cpu_reg (R_EDX) & 0xffff0000u) | (r >> 16));
	cpu_jump (ret_cs, ret_ip);
}
static int depth;

/* la table est triée par ordinal (spec2c.py) */
const struct w16_api *api_lookup (int mod, int ord)
{
	const struct w16_module *m = &w16_modules[mod];
	int lo = 0, hi = m->count - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (m->api[mid].ord == ord)
			return &m->api[mid];
		if (m->api[mid].ord < ord)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return 0;
}

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
	const struct w16_api *e = api_lookup (mod, ord);
	if (e && e->conv == 4)	/* une constante (__AHINCR…) : la valeur, comme décalage et comme sélecteur */
		return MAKESEGPTR (e->value, e->value);
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
	const struct w16_api *e = api_lookup (mod, ord);
	uint32_t ss = cpu_sreg_base (S_SS);
	uint16_t sp = (uint16_t) cpu_reg (R_ESP);
	uint16_t ret_ip = rd16 (ss + sp), ret_cs = rd16 (ss + sp + 2);
	if (!e || e->conv == 3 || (!e->fn && e->conv == 1))
		w16_fatal ("fonction pas encore écrite : %s.%d (%s), appelée de %04x:%04x", w16_modules[mod].name, ord,
			e ? e->name : "?", ret_cs, ret_ip);
	static uint8_t warned[MAX_BUILTIN][8192];
	if (!e->fn && !warned[mod][ord & 8191])
	{
		/* (pas encore écrite : notée une fois, rend 0 ; le programme continue, souvent sans dommage) */
		warned[mod][ord & 8191] = 1;
		fprintf (stderr, "win16 : %s.%s pas encore écrite (rend 0)\n", w16_modules[mod].name, e->name);
	}
	struct w16call c;
	memset (&c, 0, sizeof c);
	c.mod = mod;
	c.ord = ord;
	int total = 0;
	if ((e->conv == 0 || e->conv == 1 || e->conv == 2) && e->args)
	{
		for (const char *t = e->args; *t; t++)
			total += arg_size (*t);
		uint32_t p = e->conv != 2 ? ss + sp + 4 + (uint32_t) total : ss + sp + 4;
		for (const char *t = e->args; *t && c.n < 16; t++)
		{
			int s = arg_size (*t);
			if (e->conv != 2)
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
		/* (débogage : W16_TRACE2_AT=Nom passe à la trace des instructions à cet appel, W16_TRACE2_OFF=Nom en revient) */
		static const char *t2_at, *t2_off;
		static int t2_init;
		if (!t2_init)
		{
			t2_at = getenv ("W16_TRACE2_AT");
			t2_off = getenv ("W16_TRACE2_OFF");
			t2_init = 1;
		}
		if (t2_at && !strcmp (t2_at, e->name))
			w16_trace = 2;
		if (t2_off && !strcmp (t2_off, e->name))
			w16_trace = 1;
		fprintf (stderr, "-> %s.%s(", w16_modules[mod].name, e->name);
		for (int i = 0; i < c.n; i++)
			fprintf (stderr, i ? ", %x" : "%x", c.a[i]);
		fprintf (stderr, ") de %04x:%04x\n", ret_cs, ret_ip);
	}
	if (e->conv == 1)
	{
		/* (une fonction « register » lit et règle les registres elle-même ; ses arguments sont dépilés) */
		e->fn (&c);
		if (w16_exit)
			return;
		cpu_set_reg (R_ESP, (cpu_reg (R_ESP) & 0xffff0000u) | (uint16_t) (cpu_reg (R_ESP) + 4 + total));
		for (int i = 0; i < c.push_words; i++)
			push16 (0);
		if (c.retset)
		{
			cpu_set_reg (R_EAX, (cpu_reg (R_EAX) & 0xffff0000u) | (c.ret & 0xffff));
			cpu_set_reg (R_EDX, (cpu_reg (R_EDX) & 0xffff0000u) | (c.ret >> 16));
		}
		cpu_jump (ret_cs, ret_ip);
		return;
	}
	uint32_t si = cpu_reg (R_ESI), di = cpu_reg (R_EDI), bp = cpu_reg (R_EBP), esp = cpu_reg (R_ESP);
	uint16_t ds = cpu_sreg (S_DS), ssel = cpu_sreg (S_SS);
	if (e->fn)
		e->fn (&c);
	if (w16_exit)
		return;
	cpu_set_sreg (S_SS, ssel);
	cpu_set_reg (R_ESI, si);
	cpu_set_reg (R_EDI, di);
	cpu_set_reg (R_EBP, bp);
	cpu_set_sreg (S_DS, sel_valid (ds) ? ds : 0);
	cpu_set_reg (R_ESP, (esp & 0xffff0000u) | (uint16_t) (sp + 4 + (e->conv != 2 ? total : 0)));
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
	int keep_flags = 0;
	/* le gestionnaire voit les drapeaux d'avant l'INT (CF rendu par cpu_flags) */
	cpu_set_flags ((cpu_flags () & ~0xffffu) | flags);
	switch (vec)
	{
	case 0x21:
		dos_int21 ();
		break;
	case 0x2f:	/* multiplex : rien d'installé */
		break;
	case 0x25:	/* lecture et écriture absolues du disque : pas d'accès aux secteurs (« lecteur pas prêt ») ; */
	case 0x26:	/* comme le DOS, les drapeaux restent sur la pile au retour */
		cpu_set_reg (R_EAX, (cpu_reg (R_EAX) & ~0xffffu) | 0x8002);
		cpu_set_flags (cpu_flags () | 1);
		keep_flags = 1;
		break;
	case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d:
	{
		/* l'émulation flottante des compilateurs de Microsoft (sans OSFIXUP appliqué) : comme Wine, le « CD 3x » est
		   remplacé par FWAIT et l'instruction x87, puis réexécuté (INT 3Ch xy : préfixe de segment d'après les deux
		   bits hauts de xy, 00 DS, 01 SS, 10 CS, 11 ES ; INT 3Dh : FWAIT seul) */
		uint16_t rip = rd16 (ss + sp), rcs = rd16 (ss + sp + 2);
		uint32_t at = sel_base (rcs) + rip - 2;
		uint8_t *code = lin_ptr (at);
		code[0] = 0x9b;
		if (vec == 0x3d)
			code[1] = 0x90;
		else if (vec == 0x3c)
		{
			static const uint8_t pre[4] = { 0x3e, 0x36, 0x2e, 0x26 };
			code[1] = pre[code[2] >> 6];
			code[2] |= 0xc0;
		}
		else
			code[1] = (uint8_t) (0xd8 + vec - 0x34);
		wr16 (ss + sp, (uint16_t) (rip - 2));
		break;
	}
	default:
		w16_log ("INT %02xh ignorée (AX=%04x)\n", vec, cpu_reg (R_EAX) & 0xffff);
		break;
	}
	if (w16_exit)
		return;
	uint16_t ip = pop16 (), cs = pop16 ();
	uint16_t f = pop16 ();
	f = (uint16_t) ((f & ~0x8d5u) | (cpu_flags () & 0x8d5u));	/* CF, PF, AF, ZF, SF, OF rendus par le gestionnaire */
	if (keep_flags)
		push16 (f);
	cpu_jump (cs, ip);
	cpu_set_flags ((cpu_flags () & ~0xffffu) | f | 0x200);
}

/* débogage : W16_WATCH=sel:off signale chaque changement des 4 octets à cette adresse */
static int watch_on;
static uint16_t watch_sel, watch_off;
static uint32_t watch_val, watch_cs, watch_ip;

static void watch_check (void)
{
	if (!sel_valid (watch_sel))
		return;
	uint32_t v = rd32 (sel_base (watch_sel) + watch_off);
	if (v != watch_val)
	{
		fprintf (stderr, "[surveillance] %04x:%04x : %08x -> %08x après %04x:%04x\n", watch_sel, watch_off, watch_val, v,
			watch_cs, watch_ip);
		watch_val = v;
	}
	watch_cs = cpu_sreg (S_CS);
	watch_ip = cpu_eip ();
}

static int run_loop (int stop_off)
{
	for (;;)
	{
		if (w16_exit)
			return 0;
		if (watch_on)
			watch_check ();
		uint16_t cs = cpu_sreg (S_CS);
		if (cs > sel_ret || cs < sel_mod[0])	/* (les sélecteurs des points d'entrée se suivent : un seul test) */
		{
			if (w16_trace > 1)
			{
				char buf[128];
				uint32_t ip = cpu_eip ();
				cpu_disasm (buf, cs, ip);
				fprintf (stderr, "%04x:%04x %-32s AX=%04x BX=%04x CX=%04x DX=%04x SI=%04x DI=%04x BP=%04x SP=%04x DS=%04x ES=%04x\n",
					cs, ip, buf, cpu_reg (R_EAX) & 0xffff, cpu_reg (R_EBX) & 0xffff, cpu_reg (R_ECX) & 0xffff,
					cpu_reg (R_EDX) & 0xffff, cpu_reg (R_ESI) & 0xffff, cpu_reg (R_EDI) & 0xffff, cpu_reg (R_EBP) & 0xffff,
					cpu_reg (R_ESP) & 0xffff, cpu_sreg (S_DS), cpu_sreg (S_ES));
			}
			cpu_step ();
			continue;
		}
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
		if (cs == sel_native)
		{
			native_dispatch ((int) ip);
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

static uint32_t call16_in (uint32_t target, const uint16_t *args, int nwords, uint16_t ds, const uint16_t *regs);

/* appelle une fonction 16 bits (convention Pascal : les mots empilés dans l'ordre), DS = AX = ds ; rend DX:AX */
uint32_t call16 (uint32_t target, const uint16_t *args, int nwords, uint16_t ds)
{
	return call16_in (target, args, nwords, ds, 0);
}

/* sans arguments, avec DI, CX, SI, ES donnés (LibMain d'une DLL) */
uint32_t call16_regs (uint32_t target, uint16_t ds, const uint16_t *r)
{
	return call16_in (target, 0, 0, ds, r);
}

static uint32_t call16_in (uint32_t target, const uint16_t *args, int nwords, uint16_t ds, const uint16_t *init)
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
	cpu_set_sreg (S_DS, sel_valid (ds) ? ds : 0);
	cpu_set_sreg (S_ES, 0);
	cpu_set_reg (R_EAX, ds);
	if (init)
	{
		cpu_set_reg (R_EDI, init[0]);
		cpu_set_reg (R_ECX, init[1]);
		cpu_set_reg (R_ESI, init[2]);
		cpu_set_sreg (S_ES, sel_valid (init[3]) ? init[3] : 0);
	}
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
	for (nmods = 0; w16_modules[nmods].name && nmods < MAX_BUILTIN; nmods++)
		sel_mod[nmods] = sel_alloc (THUNK_LIN, 0x10000, 1);
	const char *wv = getenv ("W16_WATCH");
	unsigned ws, wo;
	if (wv && sscanf (wv, "%x:%x", &ws, &wo) == 2)
	{
		watch_on = 1;
		watch_sel = (uint16_t) ws;
		watch_off = (uint16_t) wo;
	}
	sel_native = sel_alloc (THUNK_LIN, 0x10000, 1);
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
