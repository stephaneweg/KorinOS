//
// taskman -- le Gestionnaire de tâches, en onglets (comme celui de Windows) :
//   Processus   les process en grille : nom, genre, état, CPU, mémoire, threads, tas, pid ; un clic sur un titre
//               trie ; Entrée ou un double-clic (Au premier plan) : sa fenêtre devant ; Suppr (Terminer) : le
//               process arrêté (confirmé pour un programme de /sys) ; R : relire.
//   Mémoire     utilisée, libre, programmes, système ; la dernière minute ; ce qui l'utilise.
//   Processeur  un panneau par cœur, en grille selon le nombre de cœurs et la fenêtre : sa charge, la dernière
//               minute, le process qui l'a le plus occupé.
//   Réseau      débits, totaux, la dernière minute, les interfaces.
// Une lecture par seconde ; la fenêtre se redimensionne, les vues suivent.
//
// Port en C++ du Task Manager d'Onyx (user/Apps/taskman/main.cpp : ses onglets, sa grille triée par insertion,
// ses vues dessinées en tuiles et en courbes d'une minute, ses formats de tailles) sur les kits de Korin en direct
// (ui_* d'UIKit, net_* de NetKit) et les appels du noyau de Korin (<armpi/syscall.h> : SYS_SYSINFO,
// SYS_CPU_LOAD (PC), SYS_PROCLIST_FROM, SYS_THREAD_LIST, SYS_VM_STATS, SYS_PROC_RIGHTS, SYS_KILL ; docs/ABI.md),
// avec tout ce que le Gestionnaire de Korin en assembleur (taskman.asm, qui remplaçait le Moniteur) a de plus et
// que l'utilisateur a validé : les threads, le tas, le CPU par process mesuré sur les temps de ses threads, les
// commutations par seconde, le temps de marche, l'état d'après les threads, la fenêtre de chaque process
// (ui_win_details), les interfaces de netd. Mêmes mesures, mêmes textes, mêmes dessins que l'asm. Un seul source pour le Pi et le PC.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include <stdint.h>
#include <string.h>
#include <armpi/syscall.h>
#include <uikit.h>
#include <netkit_abi.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

// les appels du noyau (src/inc/syscalls.inc)
enum
{
	SYS_GETUID_ = 0x90, SYS_SETUID_ = 0x91,
	SYS_VM_STATS = 0x0B, SYS_GETPID = 0x11, SYS_KILL = 0x14, SYS_SLEEP = 0x17, SYS_TIME_MS = 0x19,
	SYS_PROC_RIGHTS = 0x1D, SYS_PROCLIST_FROM = 0x1F, SYS_THREAD_LIST = 0x2A, SYS_SYSINFO = 0x62,
	SYS_CPU_LOAD = 0x7E,		// (PC ; le Pi rend E_NOSYS : SYSINFO +40, ses 4 cœurs)
};
// une entrée de SYS_THREAD_LIST (THL_*, 64 octets)
enum { THL_TID = 0, THL_STATE = 1, THL_CPU = 3, THL_RUNTIME = 4 };	// (en mots de 64 bits)
// les compteurs de netd (netd.inc : NST_*, NIF_*)
enum { NST_STACK = 64, NST_RTX = 72, NST_FRTX = 120, NST_RXB = 136, NST_TXB = 144, NST_SIZE = 152 };
enum { NIF_ENT = 64, NIF_E_NAME = 0, NIF_E_TYPE = 8, NIF_E_LINK = 12, NIF_E_IP = 16, NIF_E_SPEED = 24, NIF_E_SSID = 32 };
enum { NIF_ETHERNET = 1, NIF_WIFI = 2 };

enum
{
	WIN_W = 680, WIN_H = 520,
	TOP = 46,				// le haut : les onglets
	FOOT = 46,				// le bas : le message, les boutons
	BTN_W = 130, BTN_H = 30,
	HIST = 60,				// une minute d'échantillons
	MAXP = 256,				// process montrés (la liste du noyau n'a pas de plafond)
	MAXT = 512,				// threads suivis
	MAXIF = 6,
	MAXC = 256,				// cœurs suivis (MAX_CPUS du noyau du PC)
	KEY_R = 0x15,
};
static const unsigned C_SENT = 0x00D08A30;	// (l'émission : une couleur de contenu, comme l'asm)
static const unsigned SEG_COLS[6] = { 0x005E6FB8, 0x003D86DA, 0x004A9FB0, 0x005E9F58, 0x00B5A040, 0x00A0A0A8 };

static struct ui_vars ui;
static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }
static long sysc (long n, long a = 0, long b = 0, long c = 0) { return armpi_syscall (n, a, b, c, 0, 0, 0); }
static unsigned long time_ms (void) { return (unsigned long) sysc (SYS_TIME_MS); }

// ---- un process : l'entrée de SYS_PROCLIST (64 octets), puis ce qu'on en déduit ----
struct Proc
{
	uint64_t pid, threads, heap;		// heap : Ko
	char name[33];
	uint64_t state;				// 0 vivant, sinon en fin de vie
	uint64_t mem;				// Ko à lui
	uint64_t cpu;				// ms de CPU sur la dernière période
	uint64_t kind;				// 0 application, 1 programme, 2 système
	uint64_t win;				// sa fenêtre (0 : aucune)
	uint64_t run;				// 0 actif, 1 en veille, 2 en attente, 3 fin de vie
};
static Proc g_p[MAXP];
static int  g_np;
static uint16_t g_order[MAXP];			// les lignes de la grille -> g_p, triées
static int  g_sort = 3, g_desc = 1;		// (le CPU, le plus gros d'abord)
static uint64_t g_sel;				// le pid choisi (0 : aucun)
static uint64_t g_self;
static int  g_tab;
static uint64_t g_info[10];			// SYS_SYSINFO : coeurs, Ko libres, Ko total, ms, process, charges, commutations
static uint64_t g_last, g_dt;			// la dernière lecture, la période (ms)
static uint64_t g_apps;				// Ko des process
static uint64_t g_lastsw, g_swrate;
static bool g_netok, g_netkit;
static uint64_t g_lastrx, g_lasttx, g_rxrate, g_txrate;
static long g_nif;
static unsigned char g_nst[NST_SIZE], g_ifs[(int) NIF_ENT * (int) MAXIF];
static struct { uint64_t tid, rt; } g_tprev[MAXT], g_tcur[MAXT];	// les temps des threads : d'avant, de maintenant
static int  g_tprevn, g_tcount;
static int  g_ncores = 1;			// cœurs en marche (SYSINFO +0, CPU_LOAD)
static int16_t g_owner[MAXC];			// pour chaque cœur, celui qui l'a le plus occupé (g_p ; -1 : aucun)
static uint32_t g_obest[MAXC];			// (ses ms)
static uint32_t g_pcore[MAXC];			// les ms du process lu, par cœur
static uint16_t g_ptouch[MAXC];			// (les cœurs qu'il a touchés)
static int  g_ntouch;
static uint64_t g_load[MAXC * 2];		// CPU_LOAD : {%, ms occupées} par cœur
// les courbes : une minute
struct Hist { uint32_t v[HIST]; int n; };
static Hist h_mem, h_rx, h_tx, h_core[MAXC];
// les widgets
static long g_seg, g_list, g_view, g_info_w, g_braise, g_bkill;

// ---- les textes ----
static char *cat (char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *dec (char *d, uint64_t v)
{
	char t[24]; int k = 0;
	do t[k++] = (char) ('0' + v % 10); while (v /= 10);
	while (k) *d++ = t[--k];
	*d = 0;
	return d;
}
static char *two (char *d, uint64_t v) { *d++ = (char) ('0' + v / 10); *d++ = (char) ('0' + v % 10); *d = 0; return d; }
static char *tab (char *d) { return cat (d, "\t"); }

// pour mille -> « 12,3 % »
static char *per1000 (char *d, uint64_t v)
{
	d = dec (d, v / 10);
	*d++ = ',';
	*d++ = (char) ('0' + v % 10);
	return cat (d, " %");
}
// ms de CPU sur la période -> « 12,3 % » de la machine
static char *cpu_text (char *d, uint64_t ms)
{
	uint64_t span = g_dt * (uint64_t) g_ncores;		// (le temps de tous les cœurs)
	return per1000 (d, span ? (ms * 1000 + (span >> 1)) / span : 0);
}
// Ko -> « 812 Ko », « 34,2 Mo », « 118 Mo », « 2,31 Go » (bytes_text d'Onyx, en Ko)
static char *fmt_kb (char *d, uint64_t kb)
{
	if (kb < 1024) return cat (dec (d, kb), TR (" Ko"));
	if (kb < 102400)
	{
		d = dec (d, kb >> 10);
		*d++ = ',';
		*d++ = (char) ('0' + ((kb & 1023) * 10 >> 10));
		return cat (d, TR (" Mo"));
	}
	if (kb < 1048576) return cat (dec (d, (kb + 512) >> 10), TR (" Mo"));
	d = dec (d, kb >> 20);
	*d++ = ',';
	d = two (d, (kb & 0xFFFFF) * 100 >> 20);
	return cat (d, TR (" Go"));
}
// « 12 345 » (groupes de trois)
static char *fmt_group (char *d, uint64_t v)
{
	char t[24];
	int n = (int) (dec (t, v) - t);
	for (int i = 0; i < n; i++)
	{
		*d++ = t[i];
		int left = n - 1 - i;
		if (left && left % 3 == 0) *d++ = ' ';
	}
	*d = 0;
	return d;
}
// octets -> « 850 o », sinon comme fmt_kb
static char *fmt_bytes (char *d, uint64_t b) { return b < 1024 ? cat (dec (d, b), " o") : fmt_kb (d, b >> 10); }
static char *fmt_bps (char *d, uint64_t b) { return cat (fmt_bytes (d, b), "/s"); }
// ms -> « 00:03:12 » (ou « 2 j 00:03:12 »)
static char *fmt_uptime (char *d, uint64_t ms)
{
	uint64_t s = ms / 1000, days = s / 86400;
	s %= 86400;
	if (days) d = cat (dec (d, days), " j ");
	d = two (d, s / 3600);
	*d++ = ':';
	d = two (d, s % 3600 / 60);
	*d++ = ':';
	return two (d, s % 60);
}

// ===========================================================================
//  Les mesures
// ===========================================================================

static void push (Hist &h, uint32_t v)
{
	if (h.n >= HIST)
	{
		memmove (h.v, h.v + 1, (HIST - 1) * sizeof h.v[0]);
		h.n = HIST - 1;
	}
	h.v[h.n++] = v;
}

// la fenêtre de chaque process (la dernière des siennes dans la liste)
static void read_windows (void)
{
	static unsigned char wins[WE_PER_MSG * WE_SIZE];
	long first = 0, n;
	do
	{
		n = ui_win_details (first, (long) wins);
		if (n <= 0) return;
		long k = n - first;
		if ((unsigned long) k > WE_PER_MSG) k = WE_PER_MSG;
		for (long e = 0; e < k; e++)
		{
			uint32_t pid, id;
			memcpy (&pid, wins + e * WE_SIZE + WE_PID, 4);
			memcpy (&id, wins + e * WE_SIZE + WE_ID, 4);
			for (int i = 0; i < g_np; i++)
				if (g_p[i].pid == pid) { g_p[i].win = id; break; }
		}
		first += WE_PER_MSG;
	} while (first < n);
}

// ses threads -> cpu, run ; ses ms par cœur -> g_owner
static void read_threads (Proc &p, int idx)
{
	g_ntouch = 0;
	static uint64_t tb[64 * 8];
	long n = sysc (SYS_THREAD_LIST, (long) p.pid, (long) tb, 64);
	if (n < 0) n = 0;
	uint64_t best = p.state ? 3 : 2;		// le meilleur état vu (0 prêt, 1 endormi, 2 bloqué)
	for (long i = 0; i < n; i++)
	{
		const uint64_t *t = tb + i * 8;
		if (t[THL_STATE] <= 2 && !p.state && t[THL_STATE] < best) best = t[THL_STATE];
		uint64_t rt = t[THL_RUNTIME], tid = t[THL_TID], delta;
		if (g_tcount < MAXT) { g_tcur[g_tcount].tid = tid; g_tcur[g_tcount].rt = rt; g_tcount++; }	// retenu
		int k = 0;
		while (k < g_tprevn && g_tprev[k].tid != tid) k++;
		if (k < g_tprevn) delta = rt >= g_tprev[k].rt ? rt - g_tprev[k].rt : 0;	// celui d'il y a une période
		else delta = g_dt ? (rt > g_dt ? g_dt : rt) : 0;	// (nouveau : compté à sa première période)
		p.cpu += delta;
		uint64_t c = t[THL_CPU];
		if (c < (uint64_t) g_ncores && delta)
		{
			if (!g_pcore[c]) g_ptouch[g_ntouch++] = (uint16_t) c;
			g_pcore[c] += (uint32_t) delta;
		}
	}
	p.run = best;
	for (int k = 0; k < g_ntouch; k++)		// le plus gros de chaque cœur jusqu'ici
	{
		int c = g_ptouch[k];
		if (g_pcore[c] > g_obest[c]) { g_obest[c] = g_pcore[c]; g_owner[c] = (int16_t) idx; }
		g_pcore[c] = 0;
	}
}

// les charges des cœurs : CPU_LOAD (PC), sinon SYSINFO +40 (le Pi : ses 4 cœurs)
static void read_loads (void)
{
	long n = sysc (SYS_CPU_LOAD, (long) g_load, MAXC);
	if (n > 0)
	{
		g_ncores = n < MAXC ? (int) n : MAXC;
		for (int c = 0; c < g_ncores; c++) push (h_core[c], (uint32_t) (g_load[2 * c] > 100 ? 100 : g_load[2 * c]));
		return;
	}
	uint64_t n0 = g_info[0];
	g_ncores = n0 < 1 ? 1 : n0 > 4 ? 4 : (int) n0;
	for (int c = 0; c < g_ncores; c++) push (h_core[c], (uint32_t) (g_info[5 + c] > 100 ? 100 : g_info[5 + c]));
}

// débit d'un compteur d'octets sur la période (le premier : 0)
static uint64_t rate_of (uint64_t now, uint64_t &last)
{
	uint64_t was = last;
	last = now;
	return was && g_dt ? (now - was) * 1000 / g_dt : 0;
}

// le réseau : compteurs de netd, interfaces
static void read_net (void)
{
	g_netok = g_netkit && net_stats ((long) g_nst) == 0;
	if (!g_netok) return;
	uint64_t rx, tx;
	memcpy (&rx, g_nst + NST_RXB, 8);
	memcpy (&tx, g_nst + NST_TXB, 8);
	g_rxrate = rate_of (rx, g_lastrx);
	push (h_rx, (uint32_t) g_rxrate);
	g_txrate = rate_of (tx, g_lasttx);
	push (h_tx, (uint32_t) g_txrate);
	g_nif = net_ifaces ((long) g_ifs, MAXIF);
	if (g_nif < 0) g_nif = 0;
}

// deux fiches -> > 0 : la seconde doit passer devant (cmp d'Onyx, champs de l'asm)
static long cmp_rows (const Proc &a, const Proc &b)
{
	if (g_sort > 0 && g_sort < 8)			// (aucune colonne, -1 : par nom ; l'asm lisait hors de sa table)
	{
		static uint64_t Proc::*const field[8] = { 0, &Proc::kind, &Proc::run, &Proc::cpu, &Proc::mem, &Proc::threads,
							  &Proc::heap, &Proc::pid };
		uint64_t x = a.*field[g_sort], y = b.*field[g_sort];
		if (x != y) return (x > y) == !g_desc ? 1 : -1;
	}
	for (int i = 0; ; i++)				// par nom, sans la casse
	{
		int x = (unsigned char) a.name[i], y = (unsigned char) b.name[i];
		if ((unsigned) (x - 'A') < 26) x += 32;
		if ((unsigned) (y - 'A') < 26) y += 32;
		if (x != y)
		{
			long d = x - y;
			return !g_sort && g_desc ? -d : d;	// (le nom trié à rebours : seulement sa colonne)
		}
		if (!x) return 0;
	}
}

// g_order selon g_sort, g_desc (insertion : la liste est courte)
static void sort_rows (void)
{
	for (int i = 0; i < g_np; i++) g_order[i] = (uint16_t) i;
	for (int i = 1; i < g_np; i++)
		for (int j = i; j > 0 && cmp_rows (g_p[g_order[j - 1]], g_p[g_order[j]]) > 0; j--)
		{
			uint16_t t = g_order[j]; g_order[j] = g_order[j - 1]; g_order[j - 1] = t;
		}
}

// lit l'état du système
static void sample (void)
{
	sysc (SYS_SYSINFO, (long) g_info);
	uint64_t now = time_ms ();
	g_dt = now - g_last;				// la période
	if (g_dt >= 1000000) g_dt = 0;
	g_last = now;
	uint64_t total = g_info[2], used = total >= g_info[1] ? total - g_info[1] : 0;	// mémoire utilisée, pour mille
	push (h_mem, (uint32_t) (total ? used * 1000 / total : 0));
	read_loads ();
	uint64_t sw = g_info[9];			// commutations par seconde
	g_swrate = g_lastsw ? sw - g_lastsw : 0;
	g_lastsw = sw;
	static uint64_t raw[MAXP * 8];			// les process, par morceaux
	long n = 0, from = 0;
	for (;;)
	{
		long want = MAXP - n;
		if (!want) break;
		long r = sysc (SYS_PROCLIST_FROM, (long) (raw + n * 8), want, from);
		if (r <= 0) break;
		n += r;
		from = (long) (raw[(n - 1) * 8] & 0xFFFF);	// la suite : après le dernier
		if (r != want) break;
	}
	g_np = (int) n;
	for (int i = 0; i < g_np; i++)			// fiche : l'entrée du noyau, le reste à zéro
	{
		const uint64_t *e = raw + i * 8;
		Proc &p = g_p[i];
		memset (&p, 0, sizeof p);
		p.pid = e[0];
		p.threads = e[1];
		p.heap = e[2];
		memcpy (p.name, e + 3, 32);
		p.state = e[7];
	}
	read_windows ();
	memcpy (g_tprev, g_tcur, sizeof g_tcur[0] * (unsigned) g_tcount);	// les temps d'avant, de côté
	g_tprevn = g_tcount;
	g_tcount = 0;
	g_apps = 0;
	for (int c = 0; c < g_ncores; c++) { g_owner[c] = -1; g_obest[c] = 0; }
	for (int i = 0; i < g_np; i++)			// chaque process : ses threads, sa mémoire, ses droits
	{
		Proc &p = g_p[i];
		uint64_t vm[8];
		if (sysc (SYS_VM_STATS, (long) p.pid, (long) vm) >= 0)
		{
			p.mem = vm[0] << 2;		// (pages de 4 Ko)
			g_apps += p.mem;
		}
		p.kind = sysc (SYS_PROC_RIGHTS, (long) p.pid) == 1 ? 2 : p.win == 0;
		read_threads (p, i);
	}
	read_net ();
	sort_rows ();
}

// ===========================================================================
//  La grille, les boutons
// ===========================================================================

static const char *const KINDS[3] = { "Application", "Programme", "Système" };
static const char *const RUNS[4] = { "Actif", "En veille", "En attente", "Fin de vie" };

// (ui_list_virtual) liste, ligne -> texte de la ligne (colonnes : tabulations)
static long row_text (long, long row)
{
	static char t[256];
	const Proc &p = g_p[g_order[row]];
	char *d = tab (cat (t, p.name));
	d = tab (cat (d, TR (KINDS[p.kind < 3 ? p.kind : 0])));
	d = tab (cat (d, TR (RUNS[p.run < 4 ? p.run : 0])));
	d = tab (cpu_text (d, p.cpu));
	d = tab (fmt_kb (d, p.mem));
	d = tab (dec (d, p.threads));
	d = tab (fmt_kb (d, p.heap));
	dec (d, p.pid);
	return (long) t;
}

// la fiche du process choisi (0 : aucun)
static Proc *sel_rec (void)
{
	if (!g_sel) return 0;
	for (int i = 0; i < g_np; i++) if (g_p[i].pid == g_sel) return &g_p[i];
	return 0;
}

// les boutons selon le process choisi (Terminer : pas soi-même)
static void buttons (void)
{
	Proc *p = sel_rec ();
	ui_w_enable (g_braise, p && p->win);
	ui_w_enable (g_bkill, p && p->pid != g_self);
}

// la ligne choisie a changé
static void sel_changed (void)
{
	long r = ui_w_get_value (g_list);
	g_sel = r >= 0 && r < g_np ? g_p[g_order[r]].pid : 0;
	buttons ();
}

// la grille : ses lignes, la sélection gardée par pid
static void show_rows (void)
{
	ui_list_virtual (g_list, g_np, (long) row_text);
	long row = -1;
	for (int i = 0; g_sel && i < g_np; i++) if (g_p[g_order[i]].pid == g_sel) { row = i; break; }
	if (row < 0) g_sel = 0;
	if (ui_w_get_value (g_list) != row) ui_w_set_value (g_list, row);
	buttons ();
	ui_w_draw (g_list);
}

// le message du bas selon l'onglet
static void foot_text (void)
{
	static char t[160];
	char *d = t;
	switch (g_tab)
	{
	case 1:		// « Mémoire gérée : 960 Mo  -  pages de 4 Ko »
		cat (fmt_kb (cat (d, TR ("Mémoire gérée : ")), g_info[2]), TR ("  -  pages de 4 Ko"));
		break;
	case 2:		// temps de marche, commutations...
		d = fmt_uptime (cat (d, TR ("En marche depuis ")), g_info[3]);
		d = cat (fmt_group (cat (d, "  -  "), g_swrate), TR (" commutations/s  -  "));
		d = cat (dec (d, (uint64_t) g_np), TR (" process  -  "));
		cat (dec (d, g_info[0]), TR (" coeurs"));
		break;
	case 3:		// « Trames traitées : 1 234  -  retransmissions TCP : 3 »
		if (!g_nif) { cat (d, TR ("Pas d'interface réseau")); break; }
		{
			uint64_t st, rtx, frtx;
			memcpy (&st, g_nst + NST_STACK, 8);
			memcpy (&rtx, g_nst + NST_RTX, 8);
			memcpy (&frtx, g_nst + NST_FRTX, 8);
			d = cat (fmt_group (cat (d, TR ("Trames traitées : ")), st), TR ("  -  retransmissions TCP : "));
			fmt_group (d, rtx + frtx);
		}
		break;
	default:	// « 28 process »
		cat (dec (d, (uint64_t) g_np), TR (" process"));
	}
	ui_w_set_text (g_info_w, (long) t);
}

// relit tout et montre l'onglet ouvert
static void refresh (void)
{
	sample ();
	show_rows ();
	ui_view_dirty (g_view);
	ui_view_flush ();
	foot_text ();
}

static void do_raise (void)
{
	Proc *p = sel_rec ();
	if (p && p->win) ui_raise ((long) p->win);
}

static void do_kill (void)
{
	Proc *p = sel_rec ();
	if (!p || p->pid == g_self) return;
	if (p->kind == 2 && ui_message ((long) "Terminer",
		(long) TR ("Ce programme fait partie du système : le terminer peut bloquer la machine. Le terminer quand même ?"),
		UM_YESNO) != 1)
		return;
	long r = sysc (SYS_KILL, (long) p->pid);
	if (r == -1)	// E_PERM : un process d'un autre compte ou du système (décision E34 du 25/11/2026) : la carte
	{		// d'élévation, le Gestionnaire passe en uid 0 le temps de terminer, puis reprend son uid
		long me = sysc (SYS_GETUID_, 0);
		if (ui_elevate ((long) TR ("Le Gestionnaire des tâches veut terminer un programme d'un autre compte ou du système.")) == 0)
		{
			sysc (SYS_KILL, (long) p->pid);
			if (me > 0)
				sysc (SYS_SETUID_, me);
		}
	}
	sysc (SYS_SLEEP, 100);
	refresh ();
}

// ===========================================================================
//  Les vues dessinées
// ===========================================================================

// x, y, largeur, libellé, valeur, dessous (0), marque (0)
static void tile (long x, long y, long w, const char *label, const char *value, const char *sub, unsigned mark)
{
	ui_rbox (x, y, w, 62, 6, tok (TJ_LINE), tok (TJ_LINE));
	ui_rbox (x + 1, y + 1, w - 2, 60, 5, tok (TJ_SURFACE), tok (TJ_SURFACE));
	ui_text_fit ((long) label, x + 10, y + 17, tok (TJ_TEXT2), 12, (long) ui.font, w - 16);
	ui_text_fit ((long) value, x + 10, y + 37, tok (TJ_TEXT1), 15, (long) ui.font_b, w - 16);
	if (sub) ui_text_fit ((long) sub, x + 10, y + 53, tok (TJ_TEXT3), 12, (long) ui.font, w - 16);
	if (mark) ui_rbox (x + w - 20, y + 9, 10, 10, 2, mark, mark);
}

// un fond de courbe, quatre traits
static void plot_box (long x, long y, long w, long h)
{
	ui_rbox (x, y, w, h, 5, tok (TJ_LINE), tok (TJ_LINE));
	ui_rbox (x + 1, y + 1, w - 2, h - 2, 4, tok (TJ_SURFACE), tok (TJ_SURFACE));
	for (long i = 1; i < 4; i++) ui_fill (x + 4, (long) ((unsigned long) (h * i) >> 2) + y, w - 8, 1, tok (TJ_LINE));
}

// boîte, historique, haut, couleur, aire sous la courbe
static void plot (long x, long y, long w, long h, const Hist &hs, long top, unsigned color, bool area)
{
	if (hs.n < 2 || top <= 0) return;
	unsigned long iw = (unsigned long) (w - 12), ih = (unsigned long) (h - 12), t = (unsigned long) top;
	unsigned fillc = (unsigned) ui_mix (tok (TJ_SURFACE), color, 46);
	long px = 0, py = 0;
	for (int i = 0; i < hs.n; i++)
	{
		unsigned long v = hs.v[i] > t ? t : hs.v[i];
		long yy = (long) (y + 6 + ih - v * ih / t);
		long xx = (long) ((unsigned long) (HIST - hs.n + i) * iw / (HIST - 1) + (unsigned long) x + 6);
		if (area) ui_fill (xx - 1, yy, (long) (iw / (HIST - 1) + 1), y + 6 + (long) ih - yy, fillc);
		if (i) ui_vp_line (px << 4, py << 4, xx << 4, yy << 4, 2 * 16, color);
		px = xx;
		py = yy;
	}
}

// texte, bord droit, ligne de base, couleur, px, police
static void text_r (const char *s, long right, long y, unsigned c, long px, const unsigned char *font)
{
	ui_text ((long) s, right - ui_text_width ((long) s, px, (long) font), y, c, px, (long) font, 0);
}

// un titre de section
static void sect (const char *s, long x, long y) { ui_text ((long) s, x, y, tok (TJ_TEXT2), 12, (long) ui.font_b, 0); }

static void paint_mem (long w, long h)
{
	char a[96], b[96];
	uint64_t total = g_info[2], free = g_info[1], used = total >= free ? total - free : 0;
	uint64_t apps = g_apps, sys = used >= apps ? used - apps : 0;
	long tw = (long) ((unsigned long) (w - 30) >> 2);		// largeur d'une tuile
	fmt_kb (cat (dec (a, total ? used * 100 / total : 0), TR (" % de ")), total);	// « 37 % de 960 Mo »
	fmt_kb (b, used);
	tile (0, 0, tw, TR ("Utilisée"), b, a, 0);
	fmt_kb (b, free);
	tile (tw + 10, 0, tw, TR ("Libre"), b, 0, 0);
	fmt_kb (b, apps);
	tile ((tw + 10) * 2, 0, tw, TR ("Programmes"), b, 0, 0);
	fmt_kb (b, sys);
	tile ((tw + 10) * 3, 0, w - (tw + 10) * 3, TR ("Système"), b, TR ("noyau et caches"), 0);
	sect (TR ("MÉMOIRE UTILISÉE, LA DERNIÈRE MINUTE"), 2, 86);	// la dernière minute
	long gh = h - 96 - 100;						// hauteur du graphe
	if (gh < 50) gh = 50;
	plot_box (0, 96, w, gh);
	plot (0, 96, w, gh, h_mem, 1000, tok (TJ_ACCENT), true);
	long y = gh + 96 + 26;						// ce qui l'utilise
	sect (TR ("CE QUI L'UTILISE"), 2, y - 8);
	// le système, les quatre plus gros process qui ne sont pas du système, le reste
	uint64_t kb[7];
	const char *nm[7];
	kb[0] = sys;
	nm[0] = TR ("Système");
	int nseg = 1;
	uint64_t shown = 0;
	static bool taken[MAXP];
	memset (taken, 0, sizeof taken);
	while (nseg < 5)
	{
		int big = -1;
		uint64_t best = 0;
		for (int i = 0; i < g_np; i++)
			if (!taken[i] && g_p[i].kind != 2 && g_p[i].mem > best) { best = g_p[i].mem; big = i; }
		if (big < 0) break;
		taken[big] = true;
		kb[nseg] = best;
		nm[nseg++] = g_p[big].name;
		shown += best;
	}
	if (apps > shown) { kb[nseg] = apps - shown; nm[nseg++] = TR ("les autres"); }
	ui_rbox (0, y, w, 18, 4, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
	long bx = 1;							// x de la bande
	for (int i = 0; i < nseg && total; i++)
	{
		long sw = (long) (kb[i] * (uint64_t) (w - 2) / total);
		if (!sw && kb[i]) sw = 1;
		if (sw > w - 1 - bx) sw = w - 1 - bx;
		if (sw <= 0) continue;
		ui_fill (bx, y + 1, sw, 16, SEG_COLS[i]);
		bx += sw;
	}
	y += 26;							// la légende
	long lx = 2;
	for (int i = 0; i < nseg; i++)
	{
		fmt_kb (cat (cat (a, nm[i]), "  "), kb[i]);
		long ew = ui_text_width ((long) a, 12, (long) ui.font) + 30;	// place de l'entrée
		if ((unsigned long) (lx + ew) > (unsigned long) w && lx > 2) { lx = 2; y += 20; }
		ui_rbox (lx, y + 4, 10, 10, 2, SEG_COLS[i], SEG_COLS[i]);
		ui_text ((long) a, lx + 16, y + 14, tok (TJ_TEXT1), 12, (long) ui.font, 0);
		lx += ew;
	}
}

// la couleur du cœur c : les teintes en cycle (angle d'or, depuis le bleu de l'accent d'Onyx), saturation et
// clarté fixes ; rien d'autre en dur
static unsigned core_color (int c)
{
	unsigned hue = (210 + (unsigned) c * 137) % 360;		// degrés
	const unsigned v = 224, m = 224 * 35 / 100;		// clarté, minimum (saturation 65 %)
	unsigned f = hue % 60 * (v - m) / 60, r, g, b;
	switch (hue / 60)
	{
	case 0:  r = v; g = m + f; b = m; break;
	case 1:  r = v - f; g = v; b = m; break;
	case 2:  r = m; g = v; b = m + f; break;
	case 3:  r = m; g = v - f; b = v; break;
	case 4:  r = m + f; g = m; b = v; break;
	default: r = v; g = m; b = v - f; break;
	}
	return r << 16 | g << 8 | b;
}

// la grille : colonnes pour n panneaux dans w x h, avec gap entre eux (les plus proches de 16:10, au moins 1)
static int grid_cols (int n, long w, long h, long gap)
{
	int best = 1;
	long bestd = -1;
	for (int cols = 1; cols <= n; cols++)
	{
		int rows = (n + cols - 1) / cols;
		long pw = (w - gap * (cols - 1)) / cols, ph = (h - gap * (rows - 1)) / rows;
		if (pw <= 0 || ph <= 0) break;
		long d = pw * 10 - ph * 16;				// écart à 16:10
		if (d < 0) d = -d;
		d = d * 1000 / (pw + ph);
		if (bestd < 0 || d < bestd) { bestd = d; best = cols; }
	}
	return best;
}

static void paint_cpu (long w, long h)
{
	char t[64];
	int n = g_ncores;
	long gap = n <= 4 ? 10 : n <= 16 ? 8 : 6;
	int cols = grid_cols (n, w, h, gap), rows = (n + cols - 1) / cols;
	long pw = (w - gap * (cols - 1)) / cols, ph = (h - gap * (rows - 1)) / rows;	// un panneau
	// le texte selon la place : 14 px comme avant, plus petit dans les petits panneaux
	long px = ph >= 120 && pw >= 220 ? 14 : ph >= 70 && pw >= 140 ? 12 : 11;
	long head = px + 10;						// la ligne du titre
	bool word = pw >= 110;						// « Coeur 12 » ou « 12 »
	for (int c = 0; c < n; c++)
	{
		long x = (c % cols) * (pw + gap), y = (c / cols) * (ph + gap);
		char *e = word ? cat (t, TR ("Coeur ")) : t;			// « Coeur 0 »
		dec (e, (uint64_t) c);
		long base = y + px + 1;
		ui_text ((long) t, x + 2, base, tok (TJ_TEXT1), px, (long) ui.font_b, 0);
		long ox = ui_text_width ((long) t, px, (long) ui.font_b) + x + 2 + px * 2 / 3;	// ce qui l'occupe
		const Hist &hc = h_core[c];					// « 37 % »
		char pc[16];
		cat (dec (pc, hc.n ? hc.v[hc.n - 1] : 0), " %");
		long pcw = ui_text_width ((long) pc, px, (long) ui.font_b);
		text_r (pc, x + pw - 2, base, tok (TJ_TEXT1), px, ui.font_b);
		long room = x + pw - 2 - pcw - px / 2 - ox;
		if (room >= 24)
		{
			const char *who = g_owner[c] >= 0 ? g_p[g_owner[c]].name : TR ("libre");
			ui_text_fit ((long) who, ox, base, tok (TJ_TEXT2), px - 2, (long) ui.font, room);
		}
		if (ph - head >= 16)
		{
			plot_box (x, y + head, pw, ph - head);
			plot (x, y + head, pw, ph - head, hc, 100, core_color (c), true);
		}
	}
}

static void paint_net (long w, long h)
{
	char t[128], b[64];
	if (!g_netok)
	{
		ui_text ((long) TR ("Le service réseau ne répond pas."), 2, 24, tok (TJ_TEXT2), 14, (long) ui.font, 0);
		return;
	}
	long tw = (long) ((unsigned long) (w - 30) >> 2);		// largeur d'une tuile
	uint64_t rx, tx;
	memcpy (&rx, g_nst + NST_RXB, 8);
	memcpy (&tx, g_nst + NST_TXB, 8);
	fmt_bps (t, g_rxrate);
	tile (0, 0, tw, TR ("Réception"), t, 0, tok (TJ_ACCENT));
	fmt_bps (t, g_txrate);
	tile (tw + 10, 0, tw, TR ("Émission"), t, 0, C_SENT);
	fmt_bytes (t, rx);
	tile ((tw + 10) * 2, 0, tw, TR ("Reçues"), t, TR ("depuis le démarrage"), 0);
	fmt_bytes (t, tx);
	tile ((tw + 10) * 3, 0, w - (tw + 10) * 3, TR ("Émises"), t, TR ("depuis le démarrage"), 0);
	uint64_t top = 65536;						// le haut : le plus grand débit de la minute
	for (int i = 0; i < h_rx.n; i++)
	{
		if (h_rx.v[i] > top) top = h_rx.v[i];
		if (h_tx.v[i] > top) top = h_tx.v[i];
	}
	sect (TR ("LA DERNIÈRE MINUTE"), 2, 86);
	fmt_bps (b, top);
	cat (cat (t, TR ("haut : ")), b);
	text_r (t, w - 2, 86, tok (TJ_TEXT3), 12, ui.font);
	long gh = h - 96 - (g_nif * 22 + 44);				// sous le graphe : les interfaces
	if (gh < 70) gh = 70;
	plot_box (0, 96, w, gh);
	plot (0, 96, w, gh, h_rx, (long) top, tok (TJ_ACCENT), true);
	plot (0, 96, w, gh, h_tx, (long) top, C_SENT, false);
	long y = gh + 96 + 24;
	sect (TR ("INTERFACES"), 2, y);
	y += 20;
	for (long i = 0; i < g_nif && (unsigned long) y < (unsigned long) h; i++, y += 22)
	{
		const unsigned char *f = g_ifs + i * NIF_ENT;
		char name[9];
		memcpy (name, f + NIF_E_NAME, 8);
		name[8] = 0;
		ui_text_fit ((long) name, 2, y, tok (TJ_TEXT1), 13, (long) ui.font_b, 70);
		uint32_t type, link, ip, speed;				// type, lien, débit, adresse, SSID
		memcpy (&type, f + NIF_E_TYPE, 4);
		memcpy (&link, f + NIF_E_LINK, 4);
		memcpy (&ip, f + NIF_E_IP, 4);
		memcpy (&speed, f + NIF_E_SPEED, 4);
		char *d = cat (t, type == NIF_ETHERNET ? "Ethernet" : type == NIF_WIFI ? "Wi-Fi" : "USB");
		d = cat (d, TR (link ? ", reliée, " : ", non reliée"));
		if (speed) d = cat (dec (d, speed), " Mb/s");
		if (ip) d = (char *) fmt_ip (ip, (long) cat (d, "  "));
		char ssid[33];
		memcpy (ssid, f + NIF_E_SSID, 32);
		ssid[32] = 0;
		if (ssid[0]) cat (cat (d, "  "), ssid);
		ui_text_fit ((long) t, 76, y, tok (TJ_TEXT2), 12, (long) ui.font, w - 80);
	}
}

// (ui_view_on) vue, largeur, hauteur
static long paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, tok (TJ_FACE));
	if (g_tab == 1) paint_mem (w, h);
	else if (g_tab == 2) paint_cpu (w, h);
	else if (g_tab == 3) paint_net (w, h);
	return 0;
}

// ===========================================================================
//  La fenêtre
// ===========================================================================

// largeur, hauteur : tout à sa place
static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, tok (TJ_FACE));
	ui_w_move (g_list, 10, TOP + 4, w - 20, h - (TOP + FOOT + 4));
	ui_w_move (g_view, 10, TOP + 4, w - 20, h - (TOP + FOOT + 4));
	ui_view_dirty (g_view);
	ui_w_move (g_info_w, 12, h - (FOOT - 12), w - 24 - (g_tab ? 0 : 2 * BTN_W + 12), 22);
	ui_w_move (g_braise, w - (10 + 2 * BTN_W + 6), h - (FOOT - 8), BTN_W, BTN_H);
	ui_w_move (g_bkill, w - (10 + BTN_W), h - (FOOT - 8), BTN_W, BTN_H);
	ui_w_draw (0);
	ui_update_all ();
	return 0;
}

// l'onglet g_tab : ce qui se montre
static void show_tab (void)
{
	bool grid = g_tab == 0;
	ui_w_show (g_list, grid);
	ui_w_show (g_braise, grid);
	ui_w_show (g_bkill, grid);
	ui_w_show (g_view, !grid);
	ui_view_dirty (g_view);
	on_resize (ui.w, ui.h);
	foot_text ();
	sel_changed ();
}

static void make_widgets (void)
{
	g_seg = ui_segmented (10, 8, 0, 30, (long) TR ("Processus|Mémoire|Processeur|Réseau"));
	g_list = ui_list (10, TOP + 4, WIN_W - 20, WIN_H - TOP - FOOT - 4, UL_HEADER | UL_USERSORT | UL_HSCROLL | UL_STRIPES);
	static const struct { long w, flags; const char *title; } COLS[] = {
		{ 130, 0, "Tâche" }, { 84, 0, "Genre" }, { 76, 0, "État" }, { 62, ULC_RIGHT, "CPU" },
		{ 88, ULC_RIGHT, "Mémoire" }, { 84, ULC_RIGHT, "Threads" }, { 72, ULC_RIGHT, "Tas" }, { 52, ULC_RIGHT, "PID" } };
	for (const auto &c : COLS) ui_list_column (g_list, (long) TR (c.title), c.w, c.flags);
	ui_list_sort (g_list, g_sort, g_desc);
	g_view = ui_view (10, TOP + 4, WIN_W - 20, WIN_H - TOP - FOOT - 4, 0);
	ui_view_on (g_view, (long) paint, 0, 0, 0);
	g_info_w = ui_label (12, WIN_H - FOOT + 12, WIN_W - 24, 22, 0, ULB_DIM);
	g_braise = ui_pushbutton (0, 0, BTN_W, BTN_H, (long) "Au premier plan", 0);
	g_bkill = ui_pushbutton (0, 0, BTN_W, BTN_H, (long) "Terminer", 0);
}

// widget, code (ui_w_event)
static void on_widget (long w, long code)
{
	if (!w || !code) return;
	if (w == g_seg)
	{
		if (code != UN_CHANGE) return;
		long v = ui_w_get_value (w);
		if (v < 0) return;
		g_tab = (int) v;
		show_tab ();
	}
	else if (w == g_braise) { if (code == UN_ACTIVATE) do_raise (); }
	else if (w == g_bkill) { if (code == UN_ACTIVATE) do_kill (); }
	else if (w == g_list)
	{
		if (code == UN_ACTIVATE) do_raise ();
		else if (code == UN_SORT)
		{
			long desc = 0;
			g_sort = (int) uikit_list_sorted (g_list, &desc);
			g_desc = (int) desc;
			sort_rows ();
			show_rows ();
		}
		else if (code == UN_CHANGE) sel_changed ();
	}
}

// un clavier non pris par un widget (onglet Processus)
static void on_key (void)
{
	if (!ui_ev_field (&ui, EV_KIND) || g_tab) return;
	int k = ui_ev_field (&ui, EV_CODE);
	if (k == KEY_DELETE) do_kill ();
	else if (k == KEY_ENTER) do_raise ();
	else if (k == KEY_R) refresh ();
}

int main ()
{
	if (!uikit_open (&ui)) return 1;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) TR ("Gestionnaire de tâches"), WF_RESIZABLE) < 0) return 0;
	g_netkit = netkit_kit_open () != 0;
	ui_min_size (480, 400);
	ui_on_resize ((long) on_resize, 0);
	g_self = (uint64_t) sysc (SYS_GETPID);
	make_widgets ();
	sample ();
	show_rows ();
	on_resize (WIN_W, WIN_H);
	show_tab ();
	unsigned long next = time_ms () + 1000;
	for (;;)
	{
		long left = (long) (next - time_ms ());
		if (left <= 0)
		{
			next = time_ms () + 1000;
			refresh ();
			continue;
		}
		long t = ui_event (left);
		if (!t) { ui_w_idle (); continue; }
		if (t == WEV_CLOSE) break;
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);
		on_widget (w, code);
		if (!taken && t == WEV_KEY) on_key ();
		ui_view_flush ();
	}
	return 0;
}
