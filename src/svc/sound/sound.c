/* sound - le son en service (docs/SON.md, docs/etude/SON-PC.md), en C pour le Pi et le PC,
 * repris pas à pas de sound.asm, mix.inc et settings.inc (à côté ; le Pi garde l'asm tant que
 * le C n'y est pas essayé). Lancé par init2 (/etc/services.txt). Service « sound »,
 * agnostique du matériel comme netd : il ne touche à aucun registre et ne charge aucun pilote.
 * Il tient :
 *  * les flux des programmes (AudioKit : ops 1-8), un anneau partagé par flux (int16_t stéréo
 *    44,1 kHz) ;
 *  * les sorties : la ou les sorties virtuelles (cadencées par l'horloge, toujours là ;
 *    sound_virtual=2 de /etc/options.txt : deux) et celles des pilotes (op 20 : pwmaudio,
 *    hdmiaudio, usbaudio, hda), un anneau par sortie à la fréquence du pilote ; sur le Pi, les
 *    canaux DMA « lite » (7-14) distribués aux pilotes qui en demandent un ;
 *  * les entrées des pilotes de micro (op 21) ;
 *  * le mélange : un thread qui, toutes les 5 ms, remplit chaque anneau de sortie par blocs de
 *    1024 trames (modèle « tiré »), gain du flux x client x sortie, saturation ; la capture ;
 *  * la sortie principale (priorité : préférée, USB, HDMI, jack, virtuelle), volumes, routage,
 *    le tampon d'état partagé, /etc/sound.txt, korin.audio.state sur le bus.
 * Ajout du C : op 20, d = volume de départ de la sortie quand sound.txt n'en retient pas
 * (0 : 100 ; l'asm l'ignore). */
#include "korinrt.h"
#include "caller.h"
#include <stdint.h>

/* src/inc/sound.inc (ce que korinrt.h n'a pas) */
#define SND_BLOCK 1024
#define SND_OP_OPEN 1
#define SND_OP_CLOSE 2
#define SND_OP_REC_OPEN 3
#define SND_OP_STATE 4
#define SND_OP_OUT_VOLUME 5
#define SND_OP_CLIENT_VOLUME 6
#define SND_OP_ROUTE 7
#define SND_OP_MAIN 8
#define SND_OUT_VIRTUAL 0
#define SND_OUT_PWM 1
#define SND_OUT_HDMI 2
#define REC_SRC_MIC 0
#define REC_SRC_LOOP 1
#define REC_SRC_LOOP_OUT 0x100
#define REC_SRC_INPUT 0x200
#define REC_FROM_LOOP 1
#define SR_VOLUME 16
#define SR_SOURCE 16
#define SR_STATE 24
#define SR_OUTPUT 32
#define SR_RATE 56
#define SR_OUT_TYPE 56
#define SR_STREAM_BYTES 65536
#define SS_NOUT 16
#define SS_NCLI 24
#define SS_NIN 32
#define SS_BYTES 4096

#define SVF_ENDS 1
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define THREAD_PRIO_HIGH 1
#define BUS_RETAIN 1
#define BUS_NAME_MAX 64
#define BUS_VALUE_MAX 256

#define MSTACK 32768 /* pile du thread de mélange */
#define STR_MAX 32
#define CLI_MAX SS_CLI_MAX
#define OUT_MAX SS_OUT_MAX
#define IN_MAX SS_IN_MAX
#define LEAD_BLOCKS 3 /* avance d'une sortie : 3 blocs (~70 ms) */
#define RATE_MAX 96000
#define APM_MAX 32 /* programmes retenus (sound.txt) */
#define OMM_MAX 16 /* sorties retenues */
#define SAVE_MAX 4096
#define INI_MAX 16384

#define R64(r, off) (*(volatile u64 *)((u8 *)(r) + (off)))
#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))

struct stream {
	u64 kind; /* 0 libre, 1 lecture, 2 capture */
	u64 pid;
	long buf;
	u8 *ring;
	u64 out; /* lecture : sortie demandée (0 : suit le client) ; capture : source (REC_SRC_*) */
	u64 cur; /* lecture : sortie où il est mélangé */
	u64 cli; /* lecture : son client */
};

struct client {
	u64 pid;
	u64 vol, mute; /* 0..100 */
	u64 route;     /* sortie choisie (0 : la principale) */
	u64 peak, nstr, cur;
	char name[32];
	char rname[32]; /* sortie choisie, par son nom (vide : la principale) */
};

struct output {
	u64 id; /* 0 : place libre */
	u64 type, rate, pid; /* pilote (0 : sortie virtuelle) */
	long buf;
	u8 *ring;
	u64 vol, mute;
	long dma;  /* canal DMA donné (-1 : aucun) */
	u64 lead;  /* avance maximale (octets) */
	u64 blk;   /* octets d'un bloc à la fréquence de la sortie (max.) */
	u64 rs[3]; /* rééchantillonneur */
	u64 vt0, vcons; /* virtuelle : départ (ms), trames consommées */
	u64 peak;
	long rank, base; /* priorité de la sortie principale ; celle de son type */
	char name[32];
};

struct input {
	u64 id, type, rate, pid;
	long buf;
	u8 *ring;
	long dma;
	u64 rs[3];
	u64 peak;
	char name[32];
};

struct appmem {
	char name[32];
	u64 vol, mute;
	char route[32];
};

struct outmem {
	char name[32];
	u64 vol, mute;
};

static long canal, bp_canal, state_buf;
static u8 *state;
static u32 tab_lock;
static u64 virt_n = 1, main_out, pref_out, next_out, next_in, dma_mask, dma_used, t_start, dirty;
static char pref_name[32], namebuf[32], notebuf[128];
static struct stream streams[STR_MAX];
static struct client clients[CLI_MAX];
static struct output outs[OUT_MAX];
static struct input inputs[IN_MAX];
static struct appmem appmem[APM_MAX];
static struct outmem outmem[OMM_MAX];
static i32 acc[SND_BLOCK * 2]; /* accumulateur i32 stéréo */
static u32 mixbuf[SND_BLOCK];  /* bloc mélangé (int16_t stéréo) */
static u32 inbuf[SND_BLOCK];
static u32 rsbuf[6144]; /* rééchantillonné (sortie <= 96 kHz, entrée >= 8 kHz) */
static u64 msg[64] __attribute__((aligned(16)));
static u8 plist[64];
static char savebuf[SAVE_MAX], ini_buf[INI_MAX];

static const char s_empty[] = "";

/* ---- outils */

static void puts_(const char *s)
{
	ak_puts(s);
}

/* str_copy_n d'applist.inc : au plus n - 1 octets, puis 0 */
static void str_copy_n(char *d, const char *s, u64 n)
{
	while (n > 1 && *s) {
		*d++ = *s++;
		n--;
	}
	*d = 0;
}

static int streq(const char *a, const char *b)
{
	return !strcmp(a, b);
}

static u64 now_ms(void)
{
	return (u64)korin_sys0(SYS_TIME_MS);
}

/* (ak_volume_curve d'AudioKit) volume 0..100 -> gain 0..65536 (carré : à l'oreille) */
static u64 volume_curve(u64 v)
{
	if (v > 100)
		v = 100;
	return ((v * v) << 16) / 10000;
}

/* (ak_resample d'AudioKit) rééchantillonnage linéaire int16_t stéréo, état {précédente, position
 * 16.16, pas 16.16} -> trames produites */
static u64 resample(const u32 *in, u64 frames, u32 *out, u64 *st)
{
	u64 k = 0;
	if (!frames)
		return 0;
	u64 pos = st[1], step = st[2];
	u64 end = frames << 16;
	while (pos < end) {
		u64 i = pos >> 16;
		i32 fr = (i32)(pos & 0xFFFF);
		u32 a = i ? in[i - 1] : (u32)st[0];
		u32 b = in[i];
		i32 al = (int16_t)a, bl = (int16_t)b;
		i32 l = al + (i32)(((i64)(i32)((u32)bl - (u32)al) * fr) >> 16);
		i32 ar = (i32)a >> 16, br = (i32)b >> 16;
		i32 r = ar + (i32)(((i64)(i32)((u32)br - (u32)ar) * fr) >> 16);
		out[k++] = ((u32)l & 0xFFFF) | ((u32)r << 16);
		pos += step;
	}
	st[1] = pos - end;
	*(u32 *)st = in[frames - 1];
	return k;
}

/* ---- verrou des tables (requêtes / mélange) */

static void lock_take(void)
{
	while (__atomic_exchange_n(&tab_lock, 1, __ATOMIC_ACQUIRE))
		korin_sys0(SYS_YIELD);
}

static void lock_drop(void)
{
	__atomic_store_n(&tab_lock, 0, __ATOMIC_RELEASE);
}

/* ---- bus, sans BusKit (src/svc/inc/buspub.inc) */

static long bp_publish(const char *topic, const char *val)
{
	if (!bp_canal) {
		long c = korin_sys2(SYS_SVC_OPEN, "bus", 0);
		if (c < 0)
			return c;
		bp_canal = c + 1;
	}
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	M64(m, 0) = 1; /* BUS_OP_PUBLISH */
	M64(m, MSG_A) = BUS_RETAIN;
	u8 *d = m + MSG_DATA;
	for (int k = BUS_NAME_MAX;;) {
		u8 c = (u8)*topic++;
		*d++ = c;
		if (!c)
			break;
		if (!--k)
			return E_INVAL;
	}
	u8 *v = d;
	for (int k = BUS_VALUE_MAX - 1; k && *val; k--)
		*d++ = (u8)*val++;
	*d = 0;
	M64(m, MSG_B) = (u64)(d - v);
	long r = korin_sys2(SYS_SVC_CALL, bp_canal - 1, m);
	if (r >= 0 || r == (long)M64(m, 0))
		return r;
	bp_canal = 0; /* busd relancé : à rouvrir */
	return r;
}

/* ---- tampon d'état (SS_*) : recopie des tables, génération + 1 */

static void audio_publish(void);

static void state_update(void)
{
	lock_take();
	u8 *s = state;
	M64(s, SS_MAIN) = main_out;
	u64 n = 0;
	for (int i = 0; i < OUT_MAX; i++) {
		struct output *o = &outs[i];
		u8 *e = s + SS_OUTS + i * 64;
		M32(e, SO_ID) = (u32)o->id;
		if (!o->id)
			continue;
		n++;
		M32(e, SO_TYPE) = (u32)o->type;
		M32(e, SO_RATE) = (u32)o->rate;
		M32(e, SO_VOLUME) = (u32)o->vol;
		M32(e, SO_MUTE) = (u32)o->mute;
		M32(e, SO_FLAGS) = (main_out == o->id) | (pref_out == o->id) << 1;
		memcpy(e + SO_NAME, o->name, 32);
	}
	M64(s, SS_NOUT) = n;
	n = 0;
	for (int i = 0; i < CLI_MAX; i++) {
		struct client *c = &clients[i];
		u8 *e = s + SS_CLIS + i * 64;
		M32(e, SC_PID) = (u32)c->pid;
		if (!c->pid)
			continue;
		n++;
		M32(e, SC_VOLUME) = (u32)c->vol;
		M32(e, SC_MUTE) = (u32)c->mute;
		M32(e, SC_ROUTE) = (u32)c->route;
		M32(e, SC_OUTPUT) = (u32)c->cur;
		M32(e, SC_STREAMS) = (u32)c->nstr;
		memcpy(e + SC_NAME, c->name, 32);
	}
	M64(s, SS_NCLI) = n;
	n = 0;
	for (int i = 0; i < IN_MAX; i++) {
		struct input *in = &inputs[i];
		u8 *e = s + SS_INS + i * 64;
		M32(e, SO_ID) = (u32)in->id;
		if (!in->id)
			continue;
		n++;
		M32(e, SO_TYPE) = (u32)in->type;
		M32(e, SO_RATE) = (u32)in->rate;
		memcpy(e + SO_NAME, in->name, 32);
	}
	M64(s, SS_NIN) = n;
	__atomic_store_n((u64 *)(s + SS_GEN), M64(s, SS_GEN) + 1, __ATOMIC_RELEASE); /* (en dernier) */
	lock_drop();
	audio_publish();
}

/* korin.audio.state (gardée) : « <génération> <volume> <muet> <sortie> » de la principale */
static void audio_publish(void)
{
	char b[128], *p = b;
	u8 *s = state;
	p = fmt_dec(M64(s, SS_GEN), p);
	u8 *e = 0;
	for (int i = 0; i < SS_OUT_MAX; i++) {
		u8 *x = s + SS_OUTS + i * 64;
		if (M32(x, SO_ID) && M32(x, SO_ID) == M64(s, SS_MAIN)) {
			e = x;
			break;
		}
	}
	*p++ = ' ';
	p = fmt_dec(e ? M32(e, SO_VOLUME) : 0, p);
	*p++ = ' ';
	*p++ = e && M32(e, SO_MUTE) ? '1' : '0';
	if (e) {
		*p++ = ' ';
		const char *nm = (const char *)e + SO_NAME;
		for (int k = 0; k < 31 && nm[k]; k++)
			*p++ = nm[k];
	}
	*p = 0;
	bp_publish("korin.audio.state", b);
}

/* ---- réglages retenus : /etc/sound.txt (settings.inc) */

static struct appmem *appmem_find(const char *name, int create)
{
	for (int i = 0; i < APM_MAX; i++)
		if (appmem[i].name[0] && streq(appmem[i].name, name))
			return &appmem[i];
	if (!create)
		return 0;
	for (int i = 0; i < APM_MAX; i++)
		if (!appmem[i].name[0]) {
			struct appmem *a = &appmem[i];
			a->vol = 100;
			a->mute = 0;
			a->route[0] = 0;
			str_copy_n(a->name, name, 32);
			return a;
		}
	return 0;
}

static struct outmem *outmem_find(const char *name, int create)
{
	for (int i = 0; i < OMM_MAX; i++)
		if (outmem[i].name[0] && streq(outmem[i].name, name))
			return &outmem[i];
	if (!create)
		return 0;
	for (int i = 0; i < OMM_MAX; i++)
		if (!outmem[i].name[0]) {
			struct outmem *o = &outmem[i];
			o->vol = 100;
			o->mute = 0;
			str_copy_n(o->name, name, 32);
			return o;
		}
	return 0;
}

/* ini_trim d'applist.inc : espaces et tabulations autour, sur place */
static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t' || *s == '\r')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
		*--e = 0;
	return s;
}

/* ini_field : le champ jusqu'à la virgule (coupé), *rest = la suite (0 : dernier champ) */
static char *field(char *s, char **rest)
{
	char *c = s;
	while (*c && *c != ',')
		c++;
	if (*c) {
		*c = 0;
		*rest = c + 1;
	} else
		*rest = 0;
	return trim(s);
}

static void settings_line(const char *key, char *val)
{
	char *rest;
	if (streq(key, "main")) {
		str_copy_n(pref_name, val, 32);
	} else if (streq(key, "output")) { /* volume, muet, nom */
		u64 vol = parse_dec(field(val, &rest));
		if (!rest)
			return;
		u64 mute = parse_dec(field(rest, &rest));
		if (!rest)
			return;
		char *nm = field(rest, &rest);
		if (!*nm)
			return;
		struct outmem *o = outmem_find(nm, 1);
		if (!o)
			return;
		o->vol = vol > 100 ? 100 : vol;
		o->mute = mute != 0;
	} else if (streq(key, "app")) { /* nom, volume, muet, sortie */
		char *nm = field(val, &rest);
		if (!*nm)
			return;
		struct appmem *a = appmem_find(nm, 1);
		if (!a || !rest)
			return;
		u64 v = parse_dec(field(rest, &rest));
		a->vol = v > 100 ? 100 : v;
		if (!rest)
			return;
		a->mute = parse_dec(field(rest, &rest)) != 0;
		if (!rest)
			return;
		str_copy_n(a->route, field(rest, &rest), 32);
	}
}

/* ini_read d'applist.inc : lignes « clé = valeur » (clé en minuscules), « # » : commentaire */
static void settings_load(void)
{
	long fd = sys_open("/etc/sound.txt", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, ini_buf, INI_MAX - 1);
	sys_close(fd);
	if (n < 0)
		n = 0;
	ini_buf[n] = 0;
	char *l = ini_buf;
	while (*l) {
		char *e = l;
		while (*e && *e != '\n') {
			if (*e == '#')
				*e = 0; /* (la suite de la ligne est ignorée jusqu'au saut) */
			e++;
		}
		char next = *e;
		*e = 0;
		char *eq = l;
		while (*eq && *eq != '=')
			eq++;
		if (*eq == '=') {
			*eq = 0;
			char *k = trim(l);
			for (char *c = k; *c; c++)
				if (*c >= 'A' && *c <= 'Z')
					*c += 32;
			settings_line(k, trim(eq + 1));
		}
		if (!next)
			break;
		l = e + 1;
	}
}

static char *sv(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	return d;
}

static void settings_save(void)
{
	char *p = savebuf;
	p = sv(p, "# /etc/sound.txt : réglages du son, écrits par sound (docs/SON.md 4)\n"
		  "# main = sortie préférée ; output = volume, muet, sortie ;\n"
		  "# app = programme, volume, muet, sortie choisie (vide : la principale)\n");
	if (pref_name[0]) {
		p = sv(p, "main = ");
		p = sv(p, pref_name);
		*p++ = '\n';
	}
	for (int i = 0; i < OMM_MAX; i++) {
		struct outmem *o = &outmem[i];
		if (!o->name[0])
			continue;
		p = sv(p, "output = ");
		p = fmt_dec(o->vol, p);
		p = sv(p, ", ");
		p = fmt_dec(o->mute, p);
		p = sv(p, ", ");
		p = sv(p, o->name);
		*p++ = '\n';
	}
	for (int i = 0; i < APM_MAX; i++) {
		struct appmem *a = &appmem[i];
		if (!a->name[0])
			continue;
		p = sv(p, "app = ");
		p = sv(p, a->name);
		p = sv(p, ", ");
		p = fmt_dec(a->vol, p);
		p = sv(p, ", ");
		p = fmt_dec(a->mute, p);
		p = sv(p, ", ");
		p = sv(p, a->route);
		*p++ = '\n';
	}
	long fd = sys_open("/etc/sound.txt", O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0) {
		puts_("sound : /etc/sound.txt non écrit\n");
		return;
	}
	sys_write(fd, savebuf, (u64)(p - savebuf));
	sys_close(fd);
}

static void mark_dirty(void)
{
	u64 t = now_ms();
	dirty = t ? t : 1;
}

/* réglages à écrire une seconde après le dernier changement */
static void idle_work(void)
{
	if (!dirty || now_ms() - dirty < 1000)
		return;
	dirty = 0;
	settings_save();
}

/* ---- options (/etc/options.txt) */

/* clé (« sound_virtual= ») -> valeur décimale de la ligne qui commence par la clé, -1 sinon */
static long option_value(const char *key)
{
	char b[1024];
	long fd = sys_open("/etc/options.txt", O_READ);
	if (fd < 0)
		return -1;
	long n = sys_read(fd, b, sizeof b - 1);
	sys_close(fd);
	if (n <= 0)
		return -1;
	b[n] = 0;
	u64 kl = strlen(key);
	for (char *l = b; *l;) {
		if (!strncmp(l, key, kl))
			return (long)parse_dec(l + kl);
		while (*l && *l != '\n')
			l++;
		if (*l)
			l++;
	}
	return -1;
}

/* ---- canaux DMA « lite » (7-14) du Pi, distribués aux pilotes (docs/SON.md 2.1) */

#ifdef __aarch64__
static u32 fw_dma[8] __attribute__((aligned(16))) = {28, 0, 0x00060001, 4, 0, 0, 0}; /* GET_DMA_CHANNELS */

static void dma_mask_read(void)
{
	long r = korin_sys1(SYS_FW_CALL, fw_dma);
	u32 m = fw_dma[5];
	dma_mask = r < 0 || !m ? 0x7F35 : m;
}

/* demande (1 : n'importe lequel ; 0x100 + n : le canal n s'il est libre) -> canal, -1 si
 * aucun ; d'abord un canal seul sur sa ligne d'interruption (7-8 et 9-10 la partagent) */
static long dma_take(u64 req)
{
	if (req >= 0x100) {
		u64 n = req - 0x100;
		if (n >= 7 && n <= 14 && (dma_mask >> n & 1) && !(dma_used >> n & 1)) {
			dma_used |= 1UL << n;
			return (long)n;
		}
	}
	for (int pass = 1; pass >= 0; pass--)
		for (u64 n = 7; n <= 14; n++) {
			if (!(dma_mask >> n & 1) || (dma_used >> n & 1))
				continue;
			if (pass && n <= 10 && (dma_used >> (((n - 7) ^ 1) + 7) & 1))
				continue;
			dma_used |= 1UL << n;
			return (long)n;
		}
	return -1;
}
#else
static void dma_mask_read(void)
{
	dma_mask = 0; /* PC : pas de canaux à distribuer */
}

static long dma_take(u64 req)
{
	(void)req;
	return -1;
}
#endif

static void dma_give(long n)
{
	if (n >= 0)
		dma_used &= ~(1UL << n);
}

/* ---- sorties, entrées, clients */

static struct output *out_find(u64 id)
{
	if (!id)
		return 0;
	for (int i = 0; i < OUT_MAX; i++)
		if (outs[i].id == id)
			return &outs[i];
	return 0;
}

static struct output *out_by_name(const char *name)
{
	if (!*name)
		return 0;
	for (int i = 0; i < OUT_MAX; i++)
		if (outs[i].id && streq(outs[i].name, name))
			return &outs[i];
	return 0;
}

static struct input *in_find(u64 id)
{
	if (!id)
		return 0;
	for (int i = 0; i < IN_MAX; i++)
		if (inputs[i].id == id)
			return &inputs[i];
	return 0;
}

/* le micro par défaut : l'entrée USB s'il y en a une, sinon la première */
static struct input *in_first(void)
{
	struct input *f = 0;
	for (int i = 0; i < IN_MAX; i++) {
		struct input *in = &inputs[i];
		if (!in->id)
			continue;
		if (!f)
			f = in;
		if (in->type == SND_IN_USB)
			return in;
	}
	return f;
}

static struct client *client_by_pid(u64 pid)
{
	if (!pid)
		return 0;
	for (int i = 0; i < CLI_MAX; i++)
		if (clients[i].pid == pid)
			return &clients[i];
	return 0;
}

/* (sous le verrou) rank = base, + 10 pour la préférée ; pref_out : son numéro si présente */
static void ranks_update(void)
{
	pref_out = 0;
	for (int i = 0; i < OUT_MAX; i++) {
		struct output *o = &outs[i];
		if (!o->id)
			continue;
		o->rank = o->base;
		if (pref_name[0] && streq(pref_name, o->name)) {
			o->rank += 10;
			pref_out = o->id;
		}
	}
}

/* (sous le verrou) la principale : la plus haute priorité présente ; au journal si elle change */
static void main_choose(void)
{
	u64 best = 0;
	long br = -1;
	for (int i = 0; i < OUT_MAX; i++)
		if (outs[i].id && outs[i].rank > br) {
			br = outs[i].rank;
			best = outs[i].id;
		}
	if (best == main_out)
		return;
	main_out = best;
	char b[96];
	struct output *o = out_find(best);
	if (o)
		snprintf(b, sizeof b, "sound : sortie principale : %lu « %s »\n", (unsigned long)best, o->name);
	else
		snprintf(b, sizeof b, "sound : sortie principale : %lu\n", (unsigned long)best);
	puts_(b);
}

/* (sous le verrou) chaque flux de lecture va sur sa sortie : celle demandée si elle est là,
 * sinon celle du client (routage), sinon la principale ; demandée et partie : « perdue » */
static void reroute(void)
{
	for (int i = 0; i < STR_MAX; i++) {
		struct stream *s = &streams[i];
		if (s->kind != 1)
			continue;
		u8 *r = s->ring;
		u64 st = 0;
		struct output *o = 0;
		if (s->out) {
			o = out_find(s->out);
			if (!o)
				st = 1;
		}
		struct client *c = &clients[s->cli];
		if (!o) {
			c->route = 0;
			o = out_by_name(c->rname);
			if (o)
				c->route = o->id;
			else
				o = out_find(main_out);
		}
		if (o) {
			s->cur = o->id;
			R64(r, SR_OUTPUT) = o->id;
			R64(r, SR_OUT_TYPE) = o->type;
			c->cur = o->id;
		} else {
			s->cur = 0;
			R64(r, SR_OUTPUT) = 0;
		}
		R64(r, SR_STATE) = st;
	}
}

/* taille des données -> tampon partagé créé (en-tête à zéro), *addr ; < 0 si échec */
static long ring_new(u64 bytes, u8 **addr)
{
	long b = korin_sys1(SYS_BUF_CREATE, bytes + SR_DATA);
	if (b < 0)
		return b;
	long a = korin_sys1(SYS_BUF_MAP, b);
	if (a < 0) {
		korin_sys1(SYS_BUF_FREE, b);
		return a;
	}
	memset((void *)a, 0, SR_DATA);
	*addr = (u8 *)a;
	return b;
}

/* type, fréquence, pid du pilote (0 : virtuelle), nom, volume de départ (0 : 100) -> sortie */
static struct output *out_add(u64 type, u64 rate, u64 pid, const char *name, u64 vol0, long *err)
{
	u8 *ring;
	long b = ring_new(SR_OUT_BYTES, &ring);
	if (b < 0) {
		*err = b;
		return 0;
	}
	R64(ring, SR_RATE) = rate;
	if (pid) {
		long r = korin_sys2(SYS_BUF_GRANT, b, pid);
		if (r < 0) {
			korin_sys1(SYS_BUF_FREE, b);
			*err = r;
			return 0;
		}
	}
	lock_take();
	struct output *o = 0;
	for (int i = 0; i < OUT_MAX; i++)
		if (!outs[i].id) {
			o = &outs[i];
			break;
		}
	if (!o) {
		lock_drop();
		korin_sys1(SYS_BUF_FREE, b);
		*err = E_NOMEM;
		return 0;
	}
	memset(o, 0, sizeof *o);
	u64 id = ++next_out; /* numéro de sortie : jamais repris */
	o->type = type;
	o->rate = rate;
	o->pid = pid;
	o->buf = b;
	o->ring = ring;
	o->vol = vol0 && vol0 <= 100 ? vol0 : 100;
	o->dma = -1;
	o->rs[2] = ((u64)SND_RATE << 16) / rate; /* pas : 44 100 / fréquence (16.16) */
	o->blk = ((u64)SND_BLOCK * rate / SND_RATE + 2) << 2;
	o->lead = o->blk * LEAD_BLOCKS;
	long rk = type == SND_OUT_PWM ? 2 : type == SND_OUT_HDMI ? 3 : type == SND_OUT_USB ? 4 : 1;
	if (type == SND_OUT_VIRTUAL && id != 1)
		rk = 0; /* seconde virtuelle : après la première */
	o->rank = o->base = rk;
	o->vt0 = now_ms();
	str_copy_n(o->name, name, 32);
	struct outmem *m = outmem_find(o->name, 0); /* volume retenu (sound.txt) */
	if (m) {
		o->vol = m->vol;
		o->mute = m->mute;
	}
	__atomic_store_n(&o->id, id, __ATOMIC_RELEASE); /* (en dernier : visible du mélange) */
	ranks_update();
	lock_drop();
	return o;
}

/* « sound : sortie N « nom » (pilote P), F Hz[, canal DMA C] » (une ligne d'un coup) */
static void drv_log(const char *what, u64 id, const char *name, u64 pid, u64 rate, long dma)
{
	char b[160], d[24] = "";
	if (dma >= 0)
		snprintf(d, sizeof d, ", canal DMA %ld", dma);
	snprintf(b, sizeof b, "sound : %s %lu « %s » (pilote %lu), %lu Hz%s\n", what, (unsigned long)id, name,
		 (unsigned long)pid, (unsigned long)rate, d);
	puts_(b);
}

static void out_log(struct output *o)
{
	drv_log("sortie", o->id, o->name, o->pid, o->rate, o->dma);
}

/* avis à l'utilisateur (notifyd) pour le HDMI et l'USB, après les 15 premières secondes */
static void out_notify(struct output *o, int plugged)
{
	if (o->type < SND_OUT_HDMI || now_ms() - t_start < 15000)
		return;
	char *p = notebuf;
	*p = 0;
	p = str_cat(p, o->name);
	p = str_cat(p, plugged ? " : branché" : " : retiré");
	struct output *m = out_find(main_out);
	if (m) {
		p = str_cat(p, ", son sur ");
		str_cat(p, m->name);
	}
	notify_send("Son", notebuf, 0);
}

/* pid, destination (32 o) : nom du process */
static void proc_name(u64 pid, char *d)
{
	d[0] = 0;
	if (korin_sys2(SYS_PROC_INFO, pid, plist))
		return;
	str_copy_n(d, (const char *)plist + 24, 32);
}

/* pid -> numéro de son client (créé au besoin), < 0 si la table est pleine */
static long client_get(u64 pid)
{
	lock_take();
	long freei = -1;
	for (int i = 0; i < CLI_MAX; i++) {
		if (clients[i].pid == pid) {
			lock_drop();
			return i;
		}
		if (!clients[i].pid && freei < 0)
			freei = i;
	}
	if (freei < 0) {
		lock_drop();
		return E_NOMEM;
	}
	struct client *c = &clients[freei];
	memset(c, 0, sizeof *c);
	c->pid = pid;
	c->vol = 100;
	lock_drop();
	proc_name(pid, c->name);
	struct appmem *a = appmem_find(c->name, 0); /* volume et sortie retenus (sound.txt) */
	if (a) {
		lock_take();
		c->vol = a->vol;
		c->mute = a->mute;
		str_copy_n(c->rname, a->route, 32);
		lock_drop();
	}
	return freei;
}

/* fermeture d'un flux (retiré sous le verrou, tampon rendu, client oublié s'il n'a plus de flux) */
static void stream_close(struct stream *s)
{
	lock_take();
	u64 kind = s->kind;
	s->kind = 0;
	long b = s->buf;
	if (kind == 1) {
		struct client *c = &clients[s->cli];
		if (c->nstr)
			c->nstr--;
		if (!c->nstr)
			c->pid = 0;
	}
	lock_drop();
	korin_sys1(SYS_BUF_FREE, b);
	state_update();
}

/* ---- requêtes */

/* 1 OPEN : a sortie (0 : celle du client) -> flux ; b tampon */
static long op_open(u64 *m)
{
	u64 pid = M32(m, WM_PID);
	if (m[1] && !out_find(m[1]))
		return E_INVAL;
	u8 *ring;
	long b = ring_new(SR_STREAM_BYTES, &ring);
	if (b < 0)
		return b;
	R64(ring, SR_VOLUME) = 256;
	long r = korin_sys2(SYS_BUF_GRANT, b, pid);
	if (r < 0)
		goto free;
	long ci = client_get(pid);
	if (ci < 0) {
		r = ci;
		goto free;
	}
	lock_take();
	for (int i = 0; i < STR_MAX; i++) {
		struct stream *s = &streams[i];
		if (s->kind)
			continue;
		s->kind = 1;
		s->pid = pid;
		s->buf = b;
		s->ring = ring;
		s->out = m[1];
		s->cur = 0;
		s->cli = (u64)ci;
		clients[ci].nstr++;
		reroute();
		lock_drop();
		state_update();
		m[2] = (u64)b;
		return i;
	}
	lock_drop();
	r = E_NOMEM;
free:
	korin_sys1(SYS_BUF_FREE, b);
	return r;
}

/* 2 CLOSE : a flux */
static long op_close(u64 *m)
{
	if (m[1] >= STR_MAX)
		return E_INVAL;
	struct stream *s = &streams[m[1]];
	if (!s->kind || s->pid != M32(m, WM_PID))
		return E_INVAL;
	stream_close(s);
	return 0;
}

/* source -> source effective (REC_FROM_*), E_NOSYS sans micro, E_INVAL si inconnue */
static long source_check(u64 src)
{
	if (src == REC_SRC_LOOP)
		return REC_FROM_LOOP;
	if (!src) {
		struct input *in = in_first();
		return in ? (long)in->type : E_NOSYS;
	}
	u64 hi = src & 0xFF00, lo = src & 0xFF;
	if (hi == REC_SRC_LOOP_OUT)
		return out_find(lo) ? REC_FROM_LOOP : E_INVAL;
	if (hi != REC_SRC_INPUT)
		return E_INVAL;
	struct input *in = in_find(lo);
	return in ? (long)in->type : E_NOSYS;
}

/* 3 REC_OPEN : a source -> flux de capture ; b tampon */
static long op_rec_open(u64 *m)
{
	u64 pid = M32(m, WM_PID);
	long from = source_check(m[1]);
	if (from < 0)
		return from;
	u8 *ring;
	long b = ring_new(SR_STREAM_BYTES, &ring);
	if (b < 0)
		return b;
	R64(ring, SR_SOURCE) = (u64)from;
	long r = korin_sys2(SYS_BUF_GRANT, b, pid);
	if (r < 0)
		goto free;
	lock_take();
	for (int i = 0; i < STR_MAX; i++) {
		struct stream *s = &streams[i];
		if (s->kind)
			continue;
		s->kind = 2;
		s->pid = pid;
		s->buf = b;
		s->ring = ring;
		s->out = m[1];
		s->cur = 0;
		lock_drop();
		m[2] = (u64)b;
		return i;
	}
	lock_drop();
	r = E_NOMEM;
free:
	korin_sys1(SYS_BUF_FREE, b);
	return r;
}

/* 4 STATE -> b tampon d'état */
static long op_state(u64 *m)
{
	korin_sys2(SYS_BUF_GRANT, state_buf, M32(m, WM_PID)); /* (déjà accordé : sans effet) */
	m[2] = (u64)state_buf;
	return 0;
}

/* 5 OUT_VOLUME : a sortie, b volume, c muet */
static long op_out_volume(u64 *m)
{
	struct output *o = out_find(m[1]);
	if (!o)
		return E_INVAL;
	o->vol = m[2] > 100 ? 100 : m[2];
	o->mute = m[3] != 0;
	struct outmem *om = outmem_find(o->name, 1); /* retenu par son nom */
	if (om) {
		om->vol = o->vol;
		om->mute = o->mute;
		mark_dirty();
	}
	state_update();
	return 0;
}

static u64 msg_pid(u64 *m)
{
	return m[1] ? m[1] : M32(m, WM_PID);
}

/* 6 CLIENT_VOLUME : a pid (0 : soi), b volume, c muet (sans flux : retenu pour son nom) */
static long op_client_volume(u64 *m)
{
	u64 pid = msg_pid(m);
	u64 vol = m[2] > 100 ? 100 : m[2], mute = m[3] != 0;
	struct client *c = client_by_pid(pid);
	const char *nm;
	if (c) {
		c->vol = vol;
		c->mute = mute;
		nm = c->name;
	} else {
		proc_name(pid, namebuf);
		nm = namebuf;
	}
	if (*nm) {
		struct appmem *a = appmem_find(nm, 1);
		if (a) {
			a->vol = vol;
			a->mute = mute;
			mark_dirty();
		}
	}
	state_update();
	return 0;
}

/* 7 ROUTE : a pid (0 : soi), b sortie (0 : la principale) */
static long op_route(u64 *m)
{
	u64 pid = msg_pid(m);
	const char *rn = s_empty;
	if (m[2]) {
		struct output *o = out_find(m[2]);
		if (!o)
			return E_INVAL;
		rn = o->name;
	}
	struct client *c = client_by_pid(pid);
	const char *nm;
	if (c) {
		lock_take();
		str_copy_n(c->rname, rn, 32);
		reroute();
		lock_drop();
		nm = c->name;
	} else {
		proc_name(pid, namebuf);
		nm = namebuf;
	}
	if (*nm) {
		struct appmem *a = appmem_find(nm, 1);
		if (a) {
			str_copy_n(a->route, rn, 32);
			mark_dirty();
		}
	}
	state_update();
	return 0;
}

/* 8 MAIN : a sortie (0 : plus de préférée, priorité des types) */
static long op_main(u64 *m)
{
	const char *nm = s_empty;
	if (m[1]) {
		struct output *o = out_find(m[1]);
		if (!o)
			return E_INVAL;
		nm = o->name;
	}
	lock_take();
	str_copy_n(pref_name, nm, 32);
	ranks_update();
	main_choose();
	reroute();
	lock_drop();
	mark_dirty();
	state_update();
	return 0;
}

/* 20 DRV_OUTPUT : a type, b fréquence, c DMA, d volume de départ ; données : nom */
static long op_drv_output(u64 *m)
{
	if (m[1] > SND_OUT_USB || m[2] < 8000 || m[2] > RATE_MAX)
		return E_INVAL;
	u8 *data = (u8 *)m + MSG_DATA;
	data[31] = 0;
	long err = 0;
	struct output *o = out_add(m[1], m[2], M32(m, WM_PID), (const char *)data, m[4], &err);
	if (!o)
		return err;
	long d = m[3] ? dma_take(m[3]) : -1;
	o->dma = d;
	m[3] = (u64)d;
	m[2] = (u64)o->buf;
	lock_take();
	main_choose();
	reroute();
	lock_drop();
	state_update();
	out_log(o);
	out_notify(o, 1);
	return (long)o->id;
}

/* 21 DRV_INPUT : a type, b fréquence, c DMA ; données : nom */
static long op_drv_input(u64 *m)
{
	if (m[2] < 8000 || m[2] > RATE_MAX)
		return E_INVAL;
	u8 *ring;
	long b = ring_new(SR_IN_BYTES, &ring);
	if (b < 0)
		return b;
	R64(ring, SR_RATE) = m[2];
	long r = korin_sys2(SYS_BUF_GRANT, b, M32(m, WM_PID));
	if (r < 0) {
		korin_sys1(SYS_BUF_FREE, b);
		return r;
	}
	lock_take();
	struct input *in = 0;
	for (int i = 0; i < IN_MAX; i++)
		if (!inputs[i].id) {
			in = &inputs[i];
			break;
		}
	if (!in) {
		lock_drop();
		korin_sys1(SYS_BUF_FREE, b);
		return E_NOMEM;
	}
	in->id = ++next_in; /* numéro d'entrée : jamais repris */
	in->type = m[1];
	in->rate = m[2];
	in->pid = M32(m, WM_PID);
	in->buf = b;
	in->ring = ring;
	in->rs[0] = in->rs[1] = 0;
	in->rs[2] = (m[2] << 16) / SND_RATE; /* pas : fréquence / 44 100 (16.16) */
	str_copy_n(in->name, (const char *)m + MSG_DATA, 32);
	lock_drop();
	long d = m[3] ? dma_take(m[3]) : -1;
	in->dma = d;
	m[3] = (u64)d;
	m[2] = (u64)b;
	state_update();
	drv_log("entrée", in->id, in->name, in->pid, in->rate, in->dma);
	return (long)in->id;
}

/* 22 DRV_ALIVE : a sortie (ou entrée | SND_ID_INPUT) -> 0, E_INVAL */
static long op_drv_alive(u64 *m)
{
	u64 pid = M32(m, WM_PID), a = m[1];
	if (a & SND_ID_INPUT) {
		struct input *in = in_find(a & (SND_ID_INPUT - 1));
		return in && in->pid == pid ? 0 : E_INVAL;
	}
	struct output *o = out_find(a);
	return o && o->pid == pid ? 0 : E_INVAL;
}

/* pid terminé : ses flux, sa capture, ses sorties et entrées (pilote), son client */
static void proc_gone(u64 pid)
{
	for (int i = 0; i < STR_MAX; i++)
		if (streams[i].kind && streams[i].pid == pid)
			stream_close(&streams[i]);
	for (int i = 0; i < OUT_MAX; i++) {
		struct output *o = &outs[i];
		if (!o->id || o->pid != pid)
			continue;
		lock_take();
		u64 id = o->id;
		o->id = 0;
		main_choose();
		reroute();
		lock_drop();
		korin_sys1(SYS_BUF_FREE, o->buf);
		dma_give(o->dma);
		char b[48];
		snprintf(b, sizeof b, "sound : sortie %lu partie\n", (unsigned long)id);
		puts_(b);
		state_update();
		out_notify(o, 0); /* (le nom reste dans l'entrée) */
	}
	for (int i = 0; i < IN_MAX; i++) {
		struct input *in = &inputs[i];
		if (!in->id || in->pid != pid)
			continue;
		lock_take();
		u64 id = in->id;
		in->id = 0;
		lock_drop();
		korin_sys1(SYS_BUF_FREE, in->buf);
		dma_give(in->dma);
		char b[48];
		snprintf(b, sizeof b, "sound : entrée %lu partie\n", (unsigned long)id);
		puts_(b);
		state_update();
	}
	lock_take();
	for (int i = 0; i < CLI_MAX; i++)
		if (clients[i].pid == pid)
			clients[i].pid = 0;
	lock_drop();
	state_update();
}

/* message -> 1 : répondre (résultat en +0) */
static int dispatch(u64 *m)
{
	M32(m, WM_LEN) = 0;
	u64 op = m[0];
	if (op >= 0xFFF0) { /* les avis : du noyau seulement (pid 0) */
		if (op == SVC_OP_END && svc_from_kernel(m))
			proc_gone(m[1]);
		return 0;
	}
	/* s'annoncer comme sortie ou entrée de son : les pilotes (hda, usbaudio, hdmiaudio : droits de
	 * /sys), ou l'uid 0 (sontest par sudo) ; une app ne détourne ni n'écoute le son des autres */
	if ((op == SND_OP_DRV_OUTPUT || op == SND_OP_DRV_INPUT || op == SND_OP_DRV_ALIVE) &&
	    !caller_admin(M32(m, 44), 0)) {
		m[0] = (u64)E_PERM;
		return 1;
	}
	long r;
	switch (op) {
	case SND_OP_OPEN: r = op_open(m); break;
	case SND_OP_CLOSE: r = op_close(m); break;
	case SND_OP_REC_OPEN: r = op_rec_open(m); break;
	case SND_OP_STATE: r = op_state(m); break;
	case SND_OP_OUT_VOLUME: r = op_out_volume(m); break;
	case SND_OP_CLIENT_VOLUME: r = op_client_volume(m); break;
	case SND_OP_ROUTE: r = op_route(m); break;
	case SND_OP_MAIN: r = op_main(m); break;
	case SND_OP_DRV_OUTPUT: r = op_drv_output(m); break;
	case SND_OP_DRV_INPUT: r = op_drv_input(m); break;
	case SND_OP_DRV_ALIVE: r = op_drv_alive(m); break;
	default: r = E_INVAL;
	}
	m[0] = (u64)r;
	return 1;
}

/* ---- le thread de mélange (mix.inc) */

/* sortie virtuelle : son anneau se vide au rythme de l'horloge */
static void virt_consume(struct output *o)
{
	u8 *r = o->ring;
	u64 due = (now_ms() - o->vt0) * o->rate / 1000; /* trames dues depuis le départ */
	if (due <= o->vcons)
		return;
	u64 n = due - o->vcons;
	o->vcons = due;
	u64 head = R64(r, SR_HEAD), tail = R64(r, SR_TAIL);
	u64 avail = (head - tail) >> 2;
	u64 k = n < avail ? n : avail;
	__atomic_store_n((u64 *)(r + SR_TAIL), tail + (k << 2), __ATOMIC_RELEASE);
	R64(r, SR_PLAYED) += k;
	if (n > k)
		R64(r, SR_LOST) += n - k;
}

/* un bloc (SND_BLOCK trames, 44,1 kHz) des flux qui jouent sur cette sortie -> mixbuf */
static void mix_block(struct output *o)
{
	memset(acc, 0, sizeof acc);
	for (int i = 0; i < STR_MAX; i++) {
		struct stream *s = &streams[i];
		if (s->kind != 1 || s->cur != o->id)
			continue;
		u8 *r = s->ring;
		u64 tail = R64(r, SR_TAIL);
		u64 avail = (__atomic_load_n((u64 *)(r + SR_HEAD), __ATOMIC_ACQUIRE) - tail) >> 2;
		u64 n = avail < SND_BLOCK ? avail : SND_BLOCK;
		struct client *c = &clients[s->cli];
		i32 peak = 0;
		if (n) {
			u64 g = c->mute ? 0 : volume_curve(c->vol);
			u64 sv = R64(r, SR_VOLUME);
			if (sv > 256)
				sv = 256;
			i32 gain = (i32)((g * sv) >> 8); /* 0..65536 */
			for (u64 f = 0; f < n; f++, tail += 4) {
				u32 w = *(volatile u32 *)(r + SR_DATA + (tail & (SR_STREAM_BYTES - 1)));
				i32 L = (i32)((int16_t)w * gain) >> 16;
				i32 R = (i32)(((i32)w >> 16) * gain) >> 16;
				acc[f * 2] += L;
				acc[f * 2 + 1] += R;
				i32 aL = L < 0 ? -L : L, aR = R < 0 ? -R : R;
				if (aL > peak)
					peak = aL;
				if (aR > peak)
					peak = aR;
			}
			__atomic_store_n((u64 *)(r + SR_TAIL), tail, __ATOMIC_RELEASE);
			R64(r, SR_PLAYED) += n;
			if (n < SND_BLOCK) /* flux en retard : manque */
				R64(r, SR_LOST) += SND_BLOCK - n;
		}
		c->peak = (u64)peak; /* crête du client (vumètre) */
		M32(state, SS_CLIS + s->cli * 64 + SC_PEAK) = (u32)peak;
	}
	int16_t *mb = (int16_t *)mixbuf; /* saturation 16 bits */
	for (int k = 0; k < SND_BLOCK * 2; k++) {
		i32 v = acc[k];
		mb[k] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
	}
}

/* anneau de capture, trames int16_t stéréo : ajoutées (le reste est perdu et compté) */
static void cap_push(u8 *r, const u32 *src, u64 n)
{
	u64 head = R64(r, SR_HEAD);
	u64 tail = __atomic_load_n((u64 *)(r + SR_TAIL), __ATOMIC_ACQUIRE);
	u64 room = (SR_STREAM_BYTES - (head - tail)) >> 2;
	u64 k = n < room ? n : room;
	if (n > k)
		R64(r, SR_LOST) += n - k;
	for (u64 i = 0; i < k; i++, head += 4)
		*(volatile u32 *)(r + SR_DATA + (head & (SR_STREAM_BYTES - 1))) = src[i];
	__atomic_store_n((u64 *)(r + SR_HEAD), head, __ATOMIC_RELEASE);
}

/* mixbuf aux captures « boucle » de cette sortie */
static void loopback(struct output *o)
{
	for (int i = 0; i < STR_MAX; i++) {
		struct stream *s = &streams[i];
		if (s->kind != 2)
			continue;
		if (s->out == REC_SRC_LOOP ? o->id == main_out : s->out - REC_SRC_LOOP_OUT == o->id)
			cap_push(s->ring, mixbuf, SND_BLOCK);
	}
}

/* mixbuf -> anneau de la sortie (rééchantillonné, gain de la sortie) ; crête de la sortie */
static void out_put(struct output *o)
{
	i32 gain = o->mute ? 0 : (i32)volume_curve(o->vol);
	const u32 *src = mixbuf;
	u64 n = SND_BLOCK;
	if (o->rate != SND_RATE) {
		n = resample(mixbuf, SND_BLOCK, rsbuf, o->rs);
		src = rsbuf;
	}
	u8 *r = o->ring;
	u64 head = R64(r, SR_HEAD);
	i32 peak = 0;
	for (u64 i = 0; i < n; i++, head += 4) {
		u32 w = src[i];
		i32 L = (i32)((int16_t)w * gain) >> 16;
		i32 R = (i32)(((i32)w >> 16) * gain) >> 16;
		*(volatile u32 *)(r + SR_DATA + (head & (SR_OUT_BYTES - 1))) = ((u32)L & 0xFFFF) | (u32)R << 16;
		i32 a = L < 0 ? -L : L; /* (la gauche seulement, comme l'asm) */
		if (a > peak)
			peak = a;
	}
	__atomic_store_n((u64 *)(r + SR_HEAD), head, __ATOMIC_RELEASE);
	o->peak = (u64)peak;
	M32(state, SS_OUTS + (o - outs) * 64 + SO_PEAK) = (u32)peak;
}

/* -> blocs mélangés */
static int out_service(struct output *o)
{
	u8 *r = o->ring;
	if (!o->pid)
		virt_consume(o);
	int b = 0;
	for (; b < LEAD_BLOCKS; b++) {
		u64 ahead = R64(r, SR_HEAD) - __atomic_load_n((u64 *)(r + SR_TAIL), __ATOMIC_ACQUIRE);
		if (ahead + o->blk > o->lead)
			break;
		mix_block(o);
		loopback(o);
		out_put(o);
	}
	return b;
}

/* source d'une capture, entrée -> 1 si la capture l'écoute */
static int cap_hears(u64 src, struct input *in)
{
	if (!src)
		return in_first() == in;
	if ((src & 0xFF00) != REC_SRC_INPUT)
		return 0;
	return (src & 0xFF) == in->id;
}

/* -> 1 si des trames ont été prises */
static int in_service(struct input *in)
{
	u8 *r = in->ring;
	int ears = 0;
	for (int i = 0; i < STR_MAX; i++)
		if (streams[i].kind == 2)
			ears += cap_hears(streams[i].out, in);
	R64(r, SR_WANTED) = ears != 0;
	u64 tail = R64(r, SR_TAIL);
	u64 avail = (__atomic_load_n((u64 *)(r + SR_HEAD), __ATOMIC_ACQUIRE) - tail) >> 2;
	u64 n = avail < SND_BLOCK ? avail : SND_BLOCK;
	if (!n)
		return 0;
	for (u64 i = 0; i < n; i++, tail += 4)
		inbuf[i] = *(volatile u32 *)(r + SR_DATA + (tail & (SR_IN_BYTES - 1)));
	__atomic_store_n((u64 *)(r + SR_TAIL), tail, __ATOMIC_RELEASE);
	const u32 *src = inbuf;
	if (in->rate != SND_RATE) {
		n = resample(inbuf, n, rsbuf, in->rs);
		src = rsbuf;
	}
	for (int i = 0; i < STR_MAX; i++)
		if (streams[i].kind == 2 && cap_hears(streams[i].out, in))
			cap_push(streams[i].ring, src, n);
	return 1;
}

static void mix_thread(void)
{
	for (;;) {
		int done = 0;
		lock_take();
		for (int i = 0; i < OUT_MAX; i++)
			if (outs[i].id)
				done += out_service(&outs[i]);
		for (int i = 0; i < IN_MAX; i++)
			if (inputs[i].id)
				done += in_service(&inputs[i]);
		lock_drop();
		if (!done)
			korin_sys1(SYS_SLEEP, 5);
	}
}

/* un thread avec sa pile (MSTACK), en priorité haute -> tid, < 0 si échec */
static long spawn_thread(void (*entry)(void))
{
	long r = korin_sys1(SYS_SBRK, MSTACK + 16);
	if (r < 0)
		return r;
	u64 top = ((u64)r + MSTACK) & ~15UL;
#ifdef __aarch64__
	long tls = 0;
#else
	long tls = -1; /* (base de FS de l'appelant) */
#endif
	return korin_sys6(SYS_THREAD_CREATE_EX, (long)entry, (long)top, 0, tls, THREAD_PRIO_HIGH, (long)"sound-mix");
}

static void quit(void)
{
	puts_("sound : démarrage impossible\n");
	korin_sys1(SYS_EXIT, 1); /* (init2 relance) */
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (option_value("sound_virtual=") == 2)
		virt_n = 2;
	dma_mask_read();
	settings_load();
	t_start = now_ms();
	state_buf = korin_sys1(SYS_BUF_CREATE, SS_BYTES); /* tampon d'état (lecture seule ailleurs) */
	if (state_buf < 0)
		quit();
	long a = korin_sys1(SYS_BUF_MAP, state_buf);
	if (a < 0)
		quit();
	state = (u8 *)a;
	memset(state, 0, SS_BYTES);
	canal = korin_sys2(SYS_SVC_REGISTER, "sound", SVF_ENDS); /* (clients et pilotes qui s'arrêtent) */
	if (canal < 0)
		quit();
	for (u64 i = 0; i < virt_n; i++) {
		long err;
		out_add(SND_OUT_VIRTUAL, SND_RATE, 0, i ? "Sortie virtuelle 2" : "Sortie virtuelle", 0, &err);
	}
	lock_take();
	main_choose();
	lock_drop();
	state_update();
	char b[160];
#ifdef __aarch64__
	snprintf(b, sizeof b, "sound : en service (sorties virtuelles : %lu, blocs de 1024 trames, canaux DMA du "
		 "firmware 0x%lX : lite 7-14 aux pilotes)\n", (unsigned long)virt_n, (unsigned long)dma_mask);
#else
	snprintf(b, sizeof b, "sound : en service (sorties virtuelles : %lu, blocs de 1024 trames)\n",
		 (unsigned long)virt_n);
#endif
	puts_(b);
	if (spawn_thread(mix_thread) < 0)
		quit();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 1000);
		if (req < 0) {
			korin_sys1(SYS_SLEEP, 100);
			continue;
		}
		if (req && dispatch(msg))
			korin_sys2(SYS_SVC_REPLY, req, msg);
		idle_work();
	}
}
