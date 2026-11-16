/* stest : essais des services du système écrits en C (src/svc : echo, mapsvc,
 * busd, hwmanager ; docs/etude/PC-PILOTES.md, « Services du système en C »),
 * sur PC, sans disque : les services sont pris dans l'initrd (MOD_LAUNCH). Sans
 * bibliothèque ; à lancer par « run stest » (droits de /sys). Les services
 * restent en place ensuite (« run pcid » les annonce à hwmanager...). */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint8_t u8;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c, u64 d)
{
	register u64 r10 __asm__("r10") = d;
	i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
	return ret;
}
#define S0(n) sc(n, 0, 0, 0, 0)
#define S1(n, a) sc(n, (u64)(a), 0, 0, 0)
#define S2(n, a, b) sc(n, (u64)(a), (u64)(b), 0, 0)
#define S3(n, a, b, c) sc(n, (u64)(a), (u64)(b), (u64)(c), 0)
#define S4(n, a, b, c, d) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d))

static void put(const char *s)
{
	u64 n = 0;
	while (s[n])
		n++;
	S2(SYS_PRINT, s, n);
}

static void putn(i64 v)
{
	char b[24];
	int i = 23, neg = v < 0;
	u64 u = neg ? (u64)-v : (u64)v;
	b[i] = 0;
	do
		b[--i] = (char)('0' + u % 10);
	while (u /= 10);
	if (neg)
		b[--i] = '-';
	put(&b[i]);
}

static int fails, passes;

static void check(const char *what, int ok, i64 got)
{
	if (ok) {
		passes++;
		return;
	}
	fails++;
	put("stest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

static u64 msg[64];

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static void scopy(char *d, const char *s)
{
	while ((*d++ = *s++))
		;
}

static i64 open_svc(const char *mod, const char *name)
{
	i64 c = S2(SYS_SVC_OPEN, name, 0);
	if (c >= 0)
		return c; /* (déjà lancé) */
	i64 pid = S2(SYS_MOD_LAUNCH, mod, 0);
	check(mod, pid > 0, pid);
	c = S2(SYS_SVC_OPEN, name, 3000);
	check(name, c >= 0, c);
	return c;
}

static void t_echo(void)
{
	i64 ch = open_svc("echo.svc", "echo"); /* (« echo » de l'initrd : la commande) */
	if (ch < 0)
		return;
	u64 t0 = (u64)S0(SYS_TIME_MS);
	int bad = 0;
	for (u64 i = 0; i < 10000; i++) {
		msg[0] = 7;
		msg[1] = i;
		if ((u64)S2(SYS_SVC_CALL, ch, msg) != i + 7)
			bad++;
	}
	check("echo : op + a", !bad, bad);
	put("stest : echo : 10000 allers-retours en ");
	putn((i64)((u64)S0(SYS_TIME_MS) - t0));
	put(" ms\n");
}

static u8 buf[16384];

static i64 mcall(i64 ch, u64 op, u64 a, u64 b, u64 c)
{
	msg[0] = op;
	msg[1] = a;
	msg[2] = b;
	msg[3] = c;
	return S2(SYS_SVC_CALL, ch, msg);
}

static void t_mapsvc(void)
{
	i64 ch = open_svc("mapsvc", "mapsvc");
	if (ch < 0)
		return;
	volatile u8 *p = buf + 3000;
	i64 sum = 0;
	for (int i = 0; i < 6000; i++) {
		p[i] = (u8)i;
		sum += i & 255;
	}
	i64 r = mcall(ch, 1, (u64)p, 6000, 0);
	check("mapsvc : lecture", r == sum, r);
	volatile u8 *z = (volatile u8 *)S4(SYS_VM_MAP, 0, 65536, 3, 0);
	r = mcall(ch, 2, (u64)z, 65536, 0x33);
	check("mapsvc : écriture", r == 65536 && z[65535] == 0x33 && z[0] == 0x33, r);
	r = mcall(ch, 4, (u64)z + 2048, 4097, 0);
	check("mapsvc : pages", r == 2, r);
	r = mcall(ch, 3, (u64)z, 100, 10); /* (rend la taille, comme l'asm) */
	check("mapsvc : tenir", r == 100 && z[0] == 0x5A, r);
	r = mcall(ch, 1, 0x6F00000000ull, 4096, 0);
	check("mapsvc : adresse sans page", r < 0, r);
}

/* src/inc/bus.inc */
static i64 bus(i64 ch, u64 op, u64 a, u64 b, const char *name, const char *val, u64 vlen)
{
	for (int i = 0; i < 64; i++)
		msg[i] = 0;
	msg[0] = op;
	msg[1] = a;
	msg[2] = b;
	char *d = (char *)msg + 48;
	if (name) {
		scopy(d, name);
		while (*d)
			d++;
		d++;
		for (u64 i = 0; i < vlen; i++)
			*d++ = val[i];
	}
	return S2(SYS_SVC_CALL, ch, msg);
}

static void t_busd(void)
{
	i64 ch = open_svc("busd", "bus");
	if (ch < 0)
		return;
	i64 r = bus(ch, 1, 1, 2, "st.a", "12", 2); /* PUBLISH, BUS_RETAIN */
	check("bus : publication", r == 0, r);
	r = bus(ch, 4, 0, 0, "st.a", 0, 0); /* GET par le nom */
	i64 topic = (i64)msg[1];
	check("bus : lecture", r == 2 && ((char *)msg)[48] == '1' && ((char *)msg)[49] == '2' && msg[2] == 1, r);
	r = bus(ch, 4, (u64)topic, 0, 0, 0, 0); /* GET par le numéro */
	check("bus : lecture par numéro", r == 2, r);
	int seen = 0;
	for (u64 t = 0;;) { /* LIST */
		r = bus(ch, 5, t, 0, 0, 0, 0);
		if (r <= 0)
			break;
		if (streq((char *)msg + 48, "st.a"))
			seen = msg[2] == 2 && msg[3] == 1;
		t = (u64)r;
	}
	check("bus : liste", seen, r);
	i64 sub = bus(ch, 2, 1, 0, "st.*", 0, 0); /* SUBSCRIBE, BUS_CURRENT */
	check("bus : abonnement", sub > 0, sub);
	u64 ev[4];
	r = S2(SYS_EV_WAIT, ev, 2000); /* la valeur gardée, tout de suite */
	check("bus : valeur gardée (WEV_BUS)", r == 13 && (uint32_t)(ev[0] >> 32) == (u64)sub &&
		  (uint32_t)ev[1] == (u64)topic && (uint32_t)(ev[1] >> 32) == 2 && ((char *)ev)[16] == '1', r);
	r = bus(ch, 1, 0, 3, "st.b", "xyz", 3);
	check("bus : publication à un abonné", r == 1, r);
	r = S2(SYS_EV_WAIT, ev, 2000);
	check("bus : événement", r == 13 && (uint32_t)(ev[1] >> 32) == 3 && ((char *)ev)[18] == 'z', r);
	r = bus(ch, 1, 2, 0, "st.b", 0, 0); /* BUS_DELETE */
	check("bus : retrait", r == 1, r);
	r = S2(SYS_EV_WAIT, ev, 2000);
	check("bus : BUS_DELETED", r == 13 && (uint32_t)(ev[1] >> 32) == 0xFFFFFFFF, r);
	r = bus(ch, 4, 0, 0, "st.b", 0, 0);
	check("bus : sujet retiré", r == -2, r);
	r = bus(ch, 1, 0, 0, "st a", 0, 0);
	check("bus : nom faux", r == -22, r);
	r = bus(ch, 2, 0, 0, "st*.x", 0, 0);
	check("bus : motif faux", r == -22, r);
	r = bus(ch, 3, (u64)sub, 0, 0, 0, 0);
	check("bus : désabonnement", r == 0, r);
	r = bus(ch, 3, (u64)sub, 0, 0, 0, 0);
	check("bus : désabonnement absent", r == -2, r);
	r = bus(ch, 9, 0, 0, 0, 0, 0);
	check("bus : op inconnue", r == -22, r);
}

/* hwmanager (src/inc/usb.inc, src/svc/pcid/pci.h) : annonces sans pilote */
static void t_hwmanager(void)
{
	i64 ch = open_svc("hwmanager", "hwmanager");
	if (ch < 0)
		return;
	for (int i = 0; i < 64; i++)
		msg[i] = 0;
	msg[0] = 3; /* HW_OP_PCI : une fonction sans pilote (ff/ff/ff) */
	msg[1] = 0x00FF;
	msg[2] = 0xFFFFFF;
	msg[3] = 0x12345678;
	i64 r = S2(SYS_SVC_CALL, ch, msg);
	check("hwmanager : annonce PCIe sans pilote", r == 0, r);
	msg[0] = 1; /* HW_OP_USB : 7.0, classe 99 */
	msg[1] = 7;
	msg[2] = 0;
	msg[3] = 0x990000;
	msg[4] = 0x12345678;
	r = S2(SYS_SVC_CALL, ch, msg);
	check("hwmanager : annonce USB sans pilote", r == 0, r);
	msg[0] = 2; /* HW_OP_USB_RETRAIT */
	r = S2(SYS_SVC_CALL, ch, msg);
	check("hwmanager : retrait", r == 0, r);
	msg[0] = 77;
	r = S2(SYS_SVC_CALL, ch, msg);
	check("hwmanager : op inconnue", r == -22, r);
}

/* « run stest hid [NOM] » : abonné de hidmanager (HID_OP_ABONNER), les touches reçues
 * pendant 40 s (code HID, caractère de la disposition, appui) ; hidmanager et un
 * pilote (usbkeyboard) doivent tourner. NOM : la disposition /res/keyb/<NOM>.txt
 * d'abord (HID_OP_KEYMAP, / monté) */
static void t_hid(const char *layout)
{
	i64 ch = S2(SYS_SVC_OPEN, "hid", 2000);
	check("hid", ch >= 0, ch);
	if (ch < 0)
		return;
	if (layout && *layout) {
		for (int i = 0; i < 64; i++)
			msg[i] = 0;
		msg[0] = 2; /* HID_OP_KEYMAP, le nom en +48 */
		for (int i = 0; i < 15 && layout[i] && layout[i] != ' '; i++)
			((char *)msg)[48 + i] = layout[i];
		i64 r = S2(SYS_SVC_CALL, ch, msg);
		check("hid : disposition", r == 0 && msg[0] == 0, (i64)msg[0]);
	}
	msg[0] = 1;
	i64 b = S2(SYS_SVC_CALL, ch, msg);
	check("hid : abonnement", b >= 0, b);
	if (b < 0)
		return;
	volatile u64 *ring = (volatile u64 *)S1(SYS_BUF_MAP, b);
	u64 t0 = (u64)S0(SYS_TIME_MS);
	int keys = 0;
	while ((u64)S0(SYS_TIME_MS) - t0 < 40000) {
		while (ring[1] != ring[0]) {
			volatile u64 *e = ring + (64 + (ring[1] & 63) * 32) / 8;
			if ((uint32_t)e[0] == 1) {
				put("stest : hid : touche ");
				putn((i64)e[1]);
				put(" caractère ");
				putn((i64)(e[0] >> 32));
				put(e[2] ? " appui\n" : " relâche\n");
				keys++;
			}
			ring[1] = ring[1] + 1;
		}
		S1(SYS_SLEEP, 20);
	}
	check("hid : touches reçues", keys > 0, keys);
}

void _start(const char *line)
{
	if (line && line[0] == 'h') {
		const char *l = line;
		while (*l && *l != ' ')
			l++;
		while (*l == ' ')
			l++;
		t_hid(l);
		put("stest : hid : ");
		putn(passes);
		put(" réussis, ");
		putn(fails);
		put(" échecs\n");
		S1(SYS_EXIT, fails ? 1 : 0);
	}
	put("stest : services du système en C\n");
	t_echo();
	t_mapsvc();
	t_busd();
	t_hwmanager();
	put("stest : ");
	putn(passes);
	put(" réussis, ");
	putn(fails);
	put(" échecs\n");
	S1(SYS_EXIT, fails ? 1 : 0);
	for (;;)
		;
}
