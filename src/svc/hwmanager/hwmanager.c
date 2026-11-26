/* hwmanager - découverte du matériel (docs/FEUILLE-DE-ROUTE.md, section 1),
 * repris de hwmanager.asm ; lancé par init2. Il détecte le matériel et lance
 * le pilote adéquat (un service de /sys/svc) d'après la table
 * /etc/drivers.txt (« détection = pilote [arguments] ») :
 *  * bus I2C 1 (non énumérable, Pi) : il sonde les adresses de la table
 *    (« i2c1:5d = /sys/svc/gt911 ») toutes les 2 s pendant une minute ; dès
 *    qu'un pilote du bus est lancé, il ne touche plus au bus ;
 *  * USB : usbd lui annonce chaque interface (HW_OP_USB) ; il cherche, de la
 *    plus précise à la moins précise, « usb:VVVV:PPPP », « usb:cc/ss/pp »,
 *    « usb:cc/ss », « usb:cc » et lance le pilote avec « <périphérique>
 *    <interface> » ;
 *  * PCIe (PC) : pcid lui annonce chaque fonction (HW_OP_PCI) ; de même
 *    « pci:VVVV:DDDD », « pci:cc/ss/pp », « pci:cc/ss », « pci:cc », le pilote
 *    lancé avec l'adresse « SSSS:BB:DD.F » (docs/etude/PC-PILOTES.md) ; un
 *    pilote PCIe qui s'arrête sur une erreur est relancé (3 fois, comme soc:),
 *    sauf E_EXIST : la fonction est déjà servie (un second usbd confie son
 *    xHCI au premier) ;
 *  * les périphériques du SoC d'après la carte (soc:genet, soc:wifi) et le son
 *    HDMI (hdmi0:audio) : la couche du Pi (hwmanager_aarch64.c).
 * Il s'enregistre sous « hwmanager » (les annonces). Commun au Pi et au PC ; ce
 * qui touche le matériel du Pi (GPIO, boîte aux lettres, registres HDMI) est
 * dans hwmanager_aarch64.c. */
#include "hwmanager.h"
#include "pci.h"
#include "caller.h"

#define HW_OP_USB 1
#define HW_OP_USB_RETRAIT 2
#define HW_PID_TAG 0x5049442100000000ull /* (réponse de HW_OP_USB : a = pid du pilote lancé, b = cette marque) */

#define DET_MAX 8   /* lignes « i2c1:... » */
#define SONDES 30   /* essais (toutes les 2 s) */
#define NOMS_MAX 16 /* pilotes USB retenus (notifications) */
#define USBT_MAX 16 /* lignes « usb:... » */
#define PCIT_MAX 32 /* lignes « pci:... » (le ZBook a une trentaine de fonctions) */
#define PCIL_MAX 32 /* pilotes PCIe lancés (relances) */

struct det { u64 adr; u64 done; char driver[112]; };
struct key_entry { char key[16]; char driver[112]; };
struct pci_run { u64 pid; u64 tries; char line[128]; };

struct soc_entry soct[SOC_MAX];
long soc_n;
char hdmi_line[128];

static struct det table[DET_MAX];
static long det_n;
static struct key_entry usbt[USBT_MAX];
static long usbt_n;
static struct key_entry pcit[PCIT_MAX];
static long pcit_n;
static struct pci_run pcil[PCIL_MAX];
static char noms_usb[NOMS_MAX][32];
static char cle_vp[16], cle_cc_ss_pp[16], cle_cc_ss[8], cle_cc[8];
static char ligne_usb[128];
static char ligne[128];
static char optbuf[1024];
static u8 msg[512] __attribute__((aligned(16)));

static int has_prefix(const char *s, const char *p, const char **rest)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	*rest = s;
	return 1;
}

/* (ini_read de drivers.txt) une ligne de la table */
static void table_line(char *key, char *val)
{
	const char *k;
	if (str_eq(key, "hdmi0:audio")) { /* « hdmi0:audio = pilote » */
		str_copy_n(hdmi_line, val, 128);
	} else if (has_prefix(key, "soc:", &k)) { /* « soc:genet », « soc:wifi » */
		if (soc_n >= SOC_MAX)
			return;
		struct soc_entry *e = &soct[soc_n++];
		str_copy_n(e->key, k, 16);
		str_copy_n(e->driver, val, 112);
	} else if (has_prefix(key, "usb:", &k)) {
		if (usbt_n >= USBT_MAX)
			return;
		struct key_entry *e = &usbt[usbt_n++];
		str_copy_n(e->key, k, 16);
		str_copy_n(e->driver, val, 112);
	} else if (has_prefix(key, "pci:", &k)) { /* (l'asm la laissait tomber dans « i2c1: ») */
		if (pcit_n >= PCIT_MAX)
			return;
		struct key_entry *e = &pcit[pcit_n++];
		str_copy_n(e->key, k, 16);
		str_copy_n(e->driver, val, 112);
	} else if (has_prefix(key, "i2c1:", &k)) { /* adresse en hexa */
		u64 a = 0;
		for (; *k; k++) {
			u64 d = (u8)(*k - '0');
			if (d > 9) {
				d = (u8)(*k - 'a');
				if (d > 5)
					return;
				d += 10;
			}
			a = (a << 4) + d;
		}
		if (!a || a > 0x7F || det_n >= DET_MAX)
			return;
		struct det *e = &table[det_n++];
		e->adr = a;
		e->done = 0;
		str_copy_n(e->driver, val, 112);
	}
}

void print_hex(u64 v)
{
	char b[18], *p = b + 17;
	*p = 0;
	do {
		u64 d = v & 15;
		*--p = (char)(d < 10 ? '0' + d : 'A' + d - 10);
		v >>= 4;
	} while (v);
	say(p);
}

long launch(const char *line)
{
	str_copy_n(ligne, line, 128); /* copie : chemin, puis arguments */
	char *args = 0;
	for (char *c = ligne; *c; c++)
		if (*c == ' ') {
			*c = 0;
			args = c + 1;
			break;
		}
	long pid = plat_spawn(ligne, args);
	say(ligne);
	say(" -> ");
	say_dec(pid);
	say("\n");
	return pid;
}

/* sonde les adresses I2C de la table ; lance le pilote de la première qui
 * répond -> 1 si un pilote a été lancé (le bus est à lui) */
static int probe_i2c(void)
{
	for (long i = 0; i < det_n; i++)
		if (plat_i2c_present(table[i].adr)) {
			say("hwmanager : i2c1 0x");
			print_hex(table[i].adr);
			say(" -> ");
			launch(table[i].driver);
			return 1;
		}
	return 0;
}

/* d = destination, v = valeur, n = chiffres -> fin (hexa minuscule) */
static char *hexa(char *d, u64 v, int n)
{
	for (int i = n - 1; i >= 0; i--, v >>= 4)
		d[i] = "0123456789abcdef"[v & 15];
	return d + n;
}

/* classe/sous-classe/protocole, vendeur/produit -> « vvvv:pppp », « cc/ss/pp »,
 * « cc/ss », « cc » */
static void make_keys(u64 cls, u64 vp)
{
	char *p = hexa(cle_vp, vp >> 16, 4);
	*p++ = ':';
	p = hexa(p, vp & 0xFFFF, 4);
	*p = 0;
	p = hexa(cle_cc_ss_pp, cls >> 16, 2);
	*p++ = '/';
	p = hexa(p, (cls >> 8) & 0xFF, 2);
	*p++ = '/';
	p = hexa(p, cls & 0xFF, 2);
	*p = 0;
	str_copy_n(cle_cc_ss, cle_cc_ss_pp, 6); /* (les débuts de la précédente) */
	str_copy_n(cle_cc, cle_cc_ss_pp, 3);
}

static struct key_entry *find_key(struct key_entry *t, long n, const char *key)
{
	for (long i = 0; i < n; i++)
		if (str_eq(t[i].key, key))
			return &t[i];
	return 0;
}

static const char *const keys[] = {cle_vp, cle_cc_ss_pp, cle_cc_ss, cle_cc, 0};

/* (msg) dev = périphérique USB, name = « chemin [arguments] » du pilote lancé :
 * une notification « Périphérique USB branché » (la première interface
 * seulement), le nom du pilote retenu pour le retrait */
static void note_plugged(u64 dev, const char *line)
{
	if (dev - 1 >= NOMS_MAX)
		return;
	char *n = noms_usb[dev - 1];
	if (n[0])
		return; /* déjà annoncé */
	const char *b = line; /* le nom : après le dernier « / », jusqu'à l'espace */
	for (const char *c = line; *c;)
		if (*c++ == '/')
			b = c;
	long k = 31;
	while ((u8)*b > ' ' && k) {
		*n++ = *b++;
		k--;
	}
	*n = 0;
	notify_send("Périphérique USB branché", noms_usb[dev - 1], 0);
}

static void note_removed(u64 dev)
{
	if (dev - 1 >= NOMS_MAX)
		return;
	char *n = noms_usb[dev - 1];
	if (!n[0])
		return; /* (sans pilote : rien) */
	notify_send("Périphérique USB retiré", n, 0);
	n[0] = 0;
}

/* (msg) annonce d'usbd : a = périphérique, b = interface, c = classe/
 * sous-classe/protocole, d = vendeur/produit -> le pilote est lancé ; 0 */
static long announce_usb(void)
{
	say("hwmanager : usb ");
	say_dec((long)M64(msg, 8));
	say(".");
	say_dec((long)M64(msg, 16));
	say(" (");
	make_keys(M64(msg, 24), M64(msg, 32));
	say(cle_cc_ss_pp);
	say(") -> ");
	for (int i = 0; keys[i]; i++) { /* de la plus précise à la moins précise */
		struct key_entry *e = find_key(usbt, usbt_n, keys[i]);
		if (!e)
			continue;
		str_copy_n(ligne_usb, e->driver, 96); /* pilote, « <périphérique> <interface> » */
		char *p = ligne_usb + str_len(ligne_usb);
		*p++ = ' ';
		p = fmt_dec(M64(msg, 8), p);
		*p++ = ' ';
		p = fmt_dec(M64(msg, 16), p);
		*p = 0;
		long pid = launch(ligne_usb);
		note_plugged(M64(msg, 8), ligne_usb); /* (une par périphérique) */
		M64(msg, 8) = pid > 0 ? (u64)pid : 0; /* E49 : usbd réserve ce périphérique à ce pilote */
		M64(msg, 16) = HW_PID_TAG;
		return 0;
	}
	say("aucun pilote\n");
	M64(msg, 8) = 0;
	M64(msg, 16) = HW_PID_TAG;
	return 0;
}

/* (msg) annonce de pcid (docs/etude/PC-PILOTES.md) : a = adresse, b = classe <<
 * 16 | sous-classe << 8 | interface, c = vendeur << 16 | appareil, d = sous-
 * système -> le pilote est lancé avec l'adresse « SSSS:BB:DD.F » ; 0 tout de
 * suite (pcid annonce une fonction après l'autre) */
static long announce_pci(void)
{
	char addr[16];
	pci_format_addr((u32)M64(msg, 8), addr);
	make_keys(M64(msg, 16), M64(msg, 24));
	say("hwmanager : pci ");
	say(addr);
	say(" (");
	say(cle_cc_ss_pp);
	say(" ");
	say(cle_vp);
	say(") -> ");
	for (int i = 0; keys[i]; i++) {
		struct key_entry *e = find_key(pcit, pcit_n, keys[i]);
		if (!e)
			continue;
		struct pci_run *r = 0;
		for (int j = 0; j < PCIL_MAX && !r; j++)
			if (!pcil[j].pid)
				r = &pcil[j];
		char *l = r ? r->line : ligne_usb;
		str_copy_n(l, e->driver, 96);
		char *p = l + str_len(l);
		*p++ = ' ';
		str_copy_n(p, addr, 16);
		long pid = launch(l);
		if (r && pid > 0) {
			r->pid = (u64)pid;
			r->tries = 0;
		}
		return 0;
	}
	say("aucun pilote\n");
	return 0;
}

void soc_launch(struct soc_entry *e)
{
	say("hwmanager : soc:");
	say(e->key);
	say(" -> ");
	long pid = launch(e->driver);
	e->pid = pid < 0 ? 0 : (u64)pid;
}

/* pid terminé, code de sortie : un pilote du SoC (ou du PCIe) en erreur est
 * relancé (3 fois) */
static void driver_end(u64 pid, u32 code)
{
	if (!pid)
		return;
	if (plat_hdmi_end((long)pid)) /* le pilote HDMI s'est arrêté */
		return;
	for (long i = 0; i < soc_n; i++) {
		struct soc_entry *e = &soct[i];
		if (e->pid != pid)
			continue;
		e->pid = 0;
		if (!code)
			return; /* fin normale (matériel absent...) */
		if (e->tries >= 3) {
			say("hwmanager : pilote abandonné (3 essais) : soc:");
			say(e->key);
			say("\n");
			return;
		}
		e->tries++;
		say("hwmanager : pilote du SoC en erreur, relance\n");
		sys_sleep(1000); /* un instant avant de relancer */
		soc_launch(e);
		return;
	}
	for (int i = 0; i < PCIL_MAX; i++) {
		struct pci_run *r = &pcil[i];
		if (r->pid != pid)
			continue;
		r->pid = 0;
		if (!code)
			return;
		if (code == (u32)E_EXIST) { /* (usbd : le contrôleur est déjà servi) */
			say("hwmanager : déjà servi, pas de relance : ");
			say(r->line);
			say("\n");
			return;
		}
		if (r->tries >= 3) {
			say("hwmanager : pilote abandonné (3 essais) : ");
			say(r->line);
			say("\n");
			return;
		}
		say("hwmanager : pilote PCIe en erreur, relance\n");
		sys_sleep(1000);
		u64 t = r->tries + 1;
		long np = launch(r->line);
		if (np > 0) {
			r->pid = (u64)np;
			r->tries = t;
		}
		return;
	}
}

int option_word(const char *word)
{
	long fd = sys_open("/etc/options.txt", O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, optbuf, 1023);
	sys_close(fd);
	if (n <= 0)
		return 0;
	optbuf[n] = 0;
	const char *l = optbuf;
	for (;;) {
		long i = 0; /* le mot en début de ligne, suivi d'une fin de ligne */
		while (word[i] && word[i] == l[i])
			i++;
		if (!word[i] && (u8)l[i] <= ' ')
			return 1;
		for (;;) { /* ligne suivante */
			char c = *l++;
			if (!c)
				return 0;
			if (c == 10)
				break;
		}
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!ini_read("/etc/drivers.txt", table_line)) /* la table */
		plat_drivers_missing(table_line);
	long chan = korin_sys2(SYS_SVC_REGISTER, "hwmanager", SVF_ENDS); /* (pilotes à relancer) */
	if (chan < 0)
		return 1; /* (sur une erreur : init2 relance) */
	say("hwmanager : en service\n");
	plat_soc_start(); /* pilotes du SoC d'après la carte */
	int i2c = 0;      /* I2C 1 : 0 à ouvrir, 1 ouvert, 2 fini */
	long probes = SONDES;
	for (;;) {
		long wait;
		if (i2c != 2) { /* détection sur l'I2C 1 */
			if (!i2c) {
				if (!det_n || plat_i2c_open() < 0) { /* (rien à chercher sur ce bus) */
					i2c = 2;
					goto idle;
				}
				i2c = 1;
			}
			if (probe_i2c() || !--probes) { /* un pilote a le bus */
				i2c = 2;
				goto idle;
			}
			wait = 2000;
			goto next;
		}
idle:
		wait = plat_hdmi_watched() ? 2000 : 60000; /* (son HDMI surveillé : toutes les 2 s) */
next:;
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, wait);
		if (req < 0)
			return 1;
		plat_hdmi_poll();
		if (!req)
			continue;
		u64 op = M64(msg, 0);
		long r;
		if (op == SVC_OP_END) { /* (avis) un process s'est arrêté */
			if (svc_from_kernel(msg))
				driver_end(M64(msg, 8), (u32)M64(msg, 16));
			continue;
		}
		/* annonces et retraits : usbd et pcid (droits de /sys) ; une app ne fait lancer aucun pilote */
		if ((op == HW_OP_USB_RETRAIT || op == HW_OP_USB || op == HW_OP_PCI) && !caller_rights(M32(msg, 44)))
			r = E_PERM;
		else if (op == HW_OP_USB_RETRAIT) {
			say("hwmanager : usb "); /* (le pilote s'arrête de lui-même) */
			say_dec((long)M64(msg, 8));
			say(" retire\n");
			note_removed(M64(msg, 8));
			r = 0;
		} else if (op == HW_OP_USB)
			r = announce_usb();
		else if (op == HW_OP_PCI)
			r = announce_pci();
		else
			r = E_INVAL; /* (un SVC_OP_SIGNAL aussi reçoit une réponse, comme l'asm) */
		M64(msg, 0) = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
