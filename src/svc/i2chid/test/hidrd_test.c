/* Essai hors matériel de hidrd.c et gesture.c (i2chid) : des descripteurs de rapport d'exemple (souris
 * simple, pavé « precision touchpad » à la Synaptics, à la ELAN, un rapport par doigt à la Microsoft),
 * des rapports fabriqués, et les événements qui en sortent.
 *
 *	wsl sh src/svc/i2chid/test/run.sh
 */
#include <stdio.h>
#include <string.h>

#include "../gesture.h"
#include "../hidrd.h"

static int fails;
#define CHECK(c, ...)                                   \
	do {                                            \
		if (!(c)) {                             \
			printf("ÉCHEC %s:%d : ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);            \
			printf("\n");                   \
			fails++;                        \
		}                                       \
	} while (0)

/* ---- 1. souris de démarrage avec molette (descripteur classique) */
static const uint8_t mouse_desc[] = {
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
	0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
	0x81, 0x06, 0xC0, 0xC0,
};

/* ---- 2. pavé à la Synaptics (HID over I2C, Windows PTP) : rapport 2 souris, 3 pavé (5 doigts de
 * 1+1+... bits), 4 Input Mode, 5 sélection, 6 nombre de contacts */
#define FINGER_SYNA                                                                                    \
	0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02,      \
	0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x25, 0x05, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01,    \
	0x95, 0x03, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00, 0x26, 0xF8, 0x04, 0x75, 0x10, 0x55, 0x0E, 0x65,    \
	0x11, 0x09, 0x30, 0x35, 0x00, 0x46, 0x24, 0x04, 0x95, 0x01, 0x81, 0x02, 0x46, 0x30, 0x02, 0x26,    \
	0xA0, 0x02, 0x09, 0x31, 0x81, 0x02, 0xC0
static const uint8_t syna_desc[] = {
	/* souris, rapport 2 */
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x95, 0x06, 0x81, 0x03,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
	0x81, 0x06, 0xC0, 0xC0,
	/* pavé, rapport 3 */
	0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x03, FINGER_SYNA, FINGER_SYNA, FINGER_SYNA, FINGER_SYNA,
	FINGER_SYNA, 0x05, 0x0D, 0x55, 0x0C, 0x66, 0x01, 0x10, 0x47, 0xFF, 0xFF, 0x00, 0x00, 0x27, 0xFF,
	0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x09, 0x56, 0x81, 0x02, 0x09, 0x54, 0x25, 0x7F, 0x95,
	0x01, 0x75, 0x08, 0x81, 0x02, 0x05, 0x09, 0x09, 0x01, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81,
	0x02, 0x95, 0x07, 0x81, 0x03,
	/* nombre de contacts (Feature, rapport 6) */
	0x05, 0x0D, 0x85, 0x06, 0x09, 0x55, 0x09, 0x59, 0x75, 0x04, 0x95, 0x02, 0x25, 0x0F, 0xB1, 0x02,
	0xC0,
	/* configuration : Input Mode (rapport 4), sélection (rapport 5) */
	0x05, 0x0D, 0x09, 0x0E, 0xA1, 0x01, 0x85, 0x04, 0x09, 0x22, 0xA1, 0x02, 0x09, 0x52, 0x15, 0x00,
	0x25, 0x0A, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02, 0xC0, 0x09, 0x22, 0xA1, 0x00, 0x85, 0x05, 0x09,
	0x57, 0x09, 0x58, 0x75, 0x01, 0x95, 0x02, 0x25, 0x01, 0xB1, 0x02, 0x95, 0x06, 0xB1, 0x03, 0xC0,
	0xC0,
};

/* ---- 3. pavé à la ELAN (comme ELAN0750 du ZBook vu de Windows) : rapport 4 pavé, doigts avec Contact
 * ID de 4 bits, X/Y de 12 bits en pouces (unité 0x13, exposant -2), 3 boutons sous le pavé ; le pavé en
 * souris (rapport 1) et le pointeur du clavier (rapport 5) ; Input Mode au rapport 3 avec un octet de
 * plus (Device Index) */
#define FINGER_ELAN                                                                                    \
	0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x09, 0x47, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,    \
	0x95, 0x02, 0x81, 0x02, 0x75, 0x02, 0x95, 0x01, 0x81, 0x03, 0x09, 0x51, 0x25, 0x0F, 0x75, 0x04,    \
	0x81, 0x02, 0x05, 0x01, 0x26, 0x00, 0x0C, 0x75, 0x0C, 0x55, 0x0E, 0x65, 0x13, 0x09, 0x30, 0x35,    \
	0x00, 0x46, 0xA5, 0x01, 0x81, 0x02, 0x26, 0x00, 0x08, 0x46, 0x16, 0x01, 0x09, 0x31, 0x81, 0x02,    \
	0xC0
static const uint8_t elan_desc[] = {
	0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x04, FINGER_ELAN, FINGER_ELAN, FINGER_ELAN,
	0x55, 0x0C, 0x66, 0x01, 0x10, 0x47, 0xFF, 0xFF, 0x00, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10,
	0x95, 0x01, 0x05, 0x0D, 0x09, 0x56, 0x81, 0x02, 0x09, 0x54, 0x25, 0x7F, 0x75, 0x08, 0x81, 0x02,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x03,
	0xC0,
	/* le touchpad en souris (rapport 1) et le pointeur du clavier (rapport 5), 3 boutons chacun */
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x03,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
	0xC0, 0xC0,
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x05, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x03,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
	0xC0, 0xC0,
	0x05, 0x0D, 0x09, 0x0E, 0xA1, 0x01, 0x85, 0x03, 0x09, 0x22, 0xA1, 0x00, 0x09, 0x52, 0x09, 0x53,
	0x15, 0x00, 0x25, 0x0A, 0x75, 0x08, 0x95, 0x02, 0xB1, 0x02, 0xC0, 0xC0,
};

/* ---- outils */
struct ev {
	int type;
	long a, b, c;
};
static struct ev evs[256];
static int nev;

static void emit(void *ctx, int type, long a, long b, long c)
{
	(void)ctx;
	if (nev < 256)
		evs[nev++] = (struct ev){type, a, b, c};
}

static long sum_type(int type, int which)
{
	long s = 0;
	for (int i = 0; i < nev; i++)
		if (evs[i].type == type)
			s += which == 0 ? evs[i].a : evs[i].b;
	return s;
}

static int clicks(int button) /* appuis de ce bouton (fronts montants) */
{
	int n = 0, prev = 0;
	for (int i = 0; i < nev; i++)
		if (evs[i].type == GEV_MOUSE_REL) {
			int now = (int)(evs[i].c >> button) & 1;
			n += now && !prev;
			prev = now;
		}
	return n;
}

static struct hid_desc D;
static struct hid_model M;

/* un rapport du pavé : contacts (id, x, y), bouton, nombre annoncé, temps de balayage */
static uint32_t make_pad(uint8_t *rep, const int (*c)[3], int n, int button, int count, int scan)
{
	uint32_t len = hid_report_bytes(&D, HK_INPUT, M.t_id);
	memset(rep, 0, len + 1);
	rep[0] = (uint8_t)M.t_id;
	uint8_t *p = rep + 1;
	for (int i = 0; i < n && i < M.nfing; i++) {
		const struct hid_finger *f = &M.f[i];
		hid_put(&D.it[f->tip], p, len, 1);
		if (f->conf >= 0)
			hid_put(&D.it[f->conf], p, len, 1);
		if (f->cid >= 0)
			hid_put(&D.it[f->cid], p, len, c[i][0]);
		hid_put(&D.it[f->x], p, len, c[i][1]);
		hid_put(&D.it[f->y], p, len, c[i][2]);
	}
	if (M.t_count >= 0)
		hid_put(&D.it[M.t_count], p, len, count);
	for (int b = 0; b < 3; b++)
		if (M.t_btn[b] >= 0)
			hid_put(&D.it[M.t_btn[b]], p, len, (button >> b) & 1);
	if (M.t_scan >= 0)
		hid_put(&D.it[M.t_scan], p, len, scan);
	return len + 1;
}

static struct gest G;

static void feed(const int (*c)[3], int n, int button, long t)
{
	uint8_t rep[128];
	uint32_t len = make_pad(rep, c, n, button, n, (int)(t * 10));
	struct hid_frame fr;
	CHECK(hid_decode(&D, &M, rep, len, &fr) == 1 && fr.kind == 2, "rapport du pavé non reconnu");
	CHECK(fr.n == n, "%d contacts lus sur %d", fr.n, n);
	gest_report(&G, &fr, M.nfing, t, emit, 0);
}

/* les gestes, sur le modèle chargé */
static void gestures(const char *name)
{
	struct gest_cfg cfg = {100, 1, 1, 300};
	gest_init(&G, &M, &cfg);
	int32_t xr = M.xmax - M.xmin, yr = M.ymax - M.ymin;
	int cx = M.xmin + xr / 3, cy = M.ymin + yr / 3;
	long t = 1000;

	/* un doigt glisse de 20 % vers la droite en 100 ms */
	nev = 0;
	for (int k = 0; k <= 10; k++, t += 10) {
		int c[1][3] = {{1, cx + xr * k / 50, cy}};
		feed(c, 1, 0, t);
	}
	feed(0, 0, 0, t += 10);
	long dx = sum_type(GEV_MOUSE_REL, 0), dy = sum_type(GEV_MOUSE_REL, 1);
	printf("  %s : glissé à droite -> dx %ld dy %ld, clics %d\n", name, dx, dy, clicks(0));
	CHECK(dx > 50 && dy == 0 && clicks(0) == 0, "glissé : dx %ld dy %ld", dx, dy);

	/* toucher bref : clic gauche */
	nev = 0;
	t += 500;
	int tap[1][3] = {{2, cx, cy}};
	feed(tap, 1, 0, t);
	feed(tap, 1, 0, t += 8);
	feed(0, 0, 0, t += 60);
	printf("  %s : toucher -> clics gauches %d\n", name, clicks(0));
	CHECK(clicks(0) == 1 && clicks(1) == 0, "toucher : %d", clicks(0));

	/* toucher à deux doigts : clic droit */
	nev = 0;
	t += 500;
	int tap2[2][3] = {{3, cx, cy}, {4, cx + xr / 10, cy}};
	feed(tap2, 2, 0, t);
	feed(0, 0, 0, t += 80);
	printf("  %s : toucher à deux doigts -> clics droits %d\n", name, clicks(1));
	CHECK(clicks(1) == 1 && clicks(0) == 0, "toucher à deux : droit %d gauche %d", clicks(1), clicks(0));

	/* deux doigts vers le bas de 40 % : molette (naturel : vers le haut, > 0) */
	nev = 0;
	t += 500;
	for (int k = 0; k <= 20; k++, t += 10) {
		int c[2][3] = {{5, cx, cy + yr * k / 50}, {6, cx + xr / 8, cy + yr * k / 50}};
		feed(c, 2, 0, t);
	}
	feed(0, 0, 0, t += 10);
	long w = sum_type(GEV_WHEEL, 0);
	printf("  %s : deux doigts vers le bas -> molette %+ld, pointeur %ld\n", name, w,
	       sum_type(GEV_MOUSE_REL, 0) + sum_type(GEV_MOUSE_REL, 1));
	CHECK(w > 0 && clicks(0) + clicks(1) == 0, "défilement : %ld", w);

	/* double toucher : un toucher, puis le doigt reposé aussitôt et glissé : glisser bouton appuyé */
	nev = 0;
	t += 500;
	feed(tap, 1, 0, t);
	feed(0, 0, 0, t += 60);
	for (int k = 0; k <= 10; k++) {
		int c[1][3] = {{12, cx + xr * k / 40, cy + 10}};
		feed(c, 1, 0, t += (k ? 10 : 150));
	}
	int held = 0, moved_held = 0;
	for (int i = 0; i < nev; i++)
		if (evs[i].type == GEV_MOUSE_REL && (evs[i].c & 1)) {
			held = 1;
			moved_held += evs[i].a != 0;
		}
	feed(0, 0, 0, t += 10);
	printf("  %s : double toucher et glissé -> clics %d, glissé bouton appuyé %d, relâché à la fin %d\n", name,
	       clicks(0), moved_held, (int)!(evs[nev - 1].c & 1));
	CHECK(clicks(0) == 2 && held && moved_held > 3 && !(evs[nev - 1].c & 1), "glisser");
	/* double toucher sans bouger : double clic */
	nev = 0;
	t += 500;
	feed(tap, 1, 0, t);
	feed(0, 0, 0, t += 60);
	feed(tap, 1, 0, t += 150);
	feed(0, 0, 0, t += 60);
	printf("  %s : double toucher -> clics %d\n", name, clicks(0));
	CHECK(clicks(0) == 2 && !(evs[nev - 1].c & 1), "double toucher : %d", clicks(0));

	/* clic du pavé, un doigt au milieu : gauche ; dans le coin bas droit : droit (clickpad), ou les
	   boutons 1 puis 2 (pavé à boutons) */
	int btn2 = M.t_btn[1] >= 0 ? 2 : 1;
	nev = 0;
	t += 500;
	int mid[1][3] = {{7, M.xmin + xr / 2 - xr / 10, M.ymin + yr / 2}};
	feed(mid, 1, 0, t);
	feed(mid, 1, 1, t += 10);
	feed(mid, 1, 0, t += 300);
	feed(0, 0, 0, t += 10);
	int corner[1][3] = {{8, M.xmin + xr * 9 / 10, M.ymin + yr * 9 / 10}};
	feed(corner, 1, 0, t += 500);
	feed(corner, 1, btn2, t += 10);
	feed(corner, 1, 0, t += 300);
	feed(0, 0, 0, t += 10);
	printf("  %s : clic du pavé au milieu puis au coin -> gauche %d, droit %d\n", name, clicks(0), clicks(1));
	CHECK(clicks(0) == 1 && clicks(1) == 1, "clic du pavé : %d %d", clicks(0), clicks(1));

	/* mode hybride : 3 contacts annoncés, rapports de 2 places (si le pavé en a moins de 3) */
	if (M.nfing >= 2) {
		nev = 0;
		uint8_t rep[128];
		struct hid_frame fr;
		int a[2][3] = {{9, cx, cy}, {10, cx + 100, cy}};
		uint32_t len = make_pad(rep, a, 2, 0, 3, 0);
		hid_decode(&D, &M, rep, len, &fr);
		gest_report(&G, &fr, 2, t += 500, emit, 0);
		int before = G.nprev;
		int b[1][3] = {{11, cx + 200, cy}};
		len = make_pad(rep, b, 1, 0, 0, 0);
		hid_decode(&D, &M, rep, len, &fr);
		gest_report(&G, &fr, 2, t, emit, 0);
		printf("  %s : mode hybride (3 contacts en 2 rapports) -> %d puis %d contacts\n", name, before, G.nprev);
		CHECK(G.nprev == 3, "hybride : %d", G.nprev);
		feed(0, 0, 0, t += 10);
	}
}

static void model(const char *name, const uint8_t *d, uint32_t len, int want)
{
	int r = hid_parse_desc(&D, d, len);
	int k = hid_model_build(&D, &M);
	printf("%s : %u octets, %d éléments, rapports numérotés %d, err %d -> souris %d, pavé %d (%d doigts)\n",
	       name, len, D.n, D.ids, r, k & 1, k >> 1 & 1, M.nfing);
	CHECK(r == 0 && k == want, "%s : modèle %d au lieu de %d", name, k, want);
	if (k & 2) {
		uint32_t fl = M.mode_item >= 0 ? hid_report_bytes(&D, HK_FEATURE, D.it[M.mode_item].id) : 0;
		printf("  X %d..%d (%d,%02d mm), Y %d..%d (%d,%02d mm), nombre %s, bouton %s, balayage %s, Input Mode %s "
		       "(rapport %d, %u octet(s))\n",
		       M.xmin, M.xmax, M.xmm100 / 100, M.xmm100 % 100, M.ymin, M.ymax, M.ymm100 / 100, M.ymm100 % 100,
		       M.t_count >= 0 ? "oui" : "non", M.t_btn[0] < 0 ? "non" : M.t_btn[1] < 0 ? "1 (clickpad)" : "1 à 3", M.t_scan >= 0 ? "oui" : "non",
		       M.mode_item >= 0 ? "oui" : "non", M.mode_item >= 0 ? D.it[M.mode_item].id : -1, fl);
	}
}

int main(void)
{
	/* souris */
	model("souris", mouse_desc, sizeof mouse_desc, 1);
	{
		uint8_t rep[4] = {0x01, 0x05, 0xFD, 0x01}; /* gauche, dx 5, dy -3, molette +1 */
		struct hid_frame fr;
		CHECK(hid_decode(&D, &M, rep, 4, &fr) == 1 && fr.kind == 1, "souris non reconnue");
		CHECK(fr.dx == 5 && fr.dy == -3 && fr.wheel == 1 && fr.buttons == 1, "souris : %d %d %d %d", fr.dx,
		      fr.dy, fr.wheel, fr.buttons);
		struct gest_cfg cfg = {100, 1, 1, 300};
		gest_init(&G, &M, &cfg);
		nev = 0;
		gest_mouse(&G, &fr, emit, 0);
		CHECK(nev == 2 && evs[0].a == 5 && evs[0].b == -3 && evs[0].c == 1 && evs[1].type == GEV_WHEEL,
		      "souris : événements");
		printf("  souris : rapport 01 05 FD 01 -> dx %ld dy %ld boutons %ld, molette %ld\n", evs[0].a, evs[0].b,
		       evs[0].c, evs[1].a);
	}

	/* Synaptics : souris (mode de départ) et pavé */
	model("synaptics", syna_desc, sizeof syna_desc, 3);
	CHECK(M.nfing == 5 && M.nmouse == 1 && M.m[0].id == 2 && M.t_id == 3, "synaptics : doigts %d, souris %d, pavé %d", M.nfing, M.nmouse,
	      M.t_id);
	CHECK(M.mode_item >= 0 && D.it[M.mode_item].id == 4 && hid_report_bytes(&D, HK_FEATURE, 4) == 1,
	      "synaptics : Input Mode");
	CHECK(M.surface_item >= 0 && M.button_item >= 0 && D.it[M.surface_item].id == 5, "synaptics : sélection");
	CHECK(M.xmm100 == 10600 && M.ymm100 == 5600, "synaptics : taille %d %d", M.xmm100, M.ymm100);
	{
		/* Input Mode = 3 dans le rapport 4 */
		uint8_t f[8] = {0};
		hid_put(&D.it[M.mode_item], f, 1, 3);
		CHECK(f[0] == 3, "Input Mode écrit : %02x", f[0]);
		uint8_t rep[5] = {0x02, 0x02, 0x10, 0xF0, 0x00}; /* souris : droit, dx 16, dy -16 */
		struct hid_frame fr;
		CHECK(hid_decode(&D, &M, rep, 5, &fr) == 1 && fr.kind == 1 && fr.dx == 16 && fr.dy == -16 &&
			      fr.buttons == 2,
		      "synaptics souris");
	}
	gestures("synaptics");

	/* ELAN : pavé seul, pouces, Input Mode avec Device Index */
	model("elan", elan_desc, sizeof elan_desc, 3);
	CHECK(M.nfing == 3 && M.t_id == 4 && M.nmouse == 2 && M.m[1].id == 5, "elan : %d doigts, %d souris", M.nfing,
	      M.nmouse);
	CHECK(M.t_btn[0] >= 0 && M.t_btn[1] >= 0 && M.t_btn[2] >= 0, "elan : 3 boutons");
	CHECK(M.mode_item >= 0 && hid_report_bytes(&D, HK_FEATURE, 3) == 2, "elan : Input Mode sur 2 octets");
	CHECK(M.xmm100 == 10693 && M.ymm100 == 7061, "elan : taille %d %d", M.xmm100, M.ymm100);
	gestures("elan");
	{
		/* le pointeur du clavier (rapport 5) : bouton gauche, dx 3, dy -2, à côté du pavé */
		uint8_t rep[4] = {0x05, 0x01, 0x03, 0xFE};
		struct hid_frame fr;
		CHECK(hid_decode(&D, &M, rep, 4, &fr) == 1 && fr.kind == 1 && fr.src == 1 && fr.dx == 3 && fr.dy == -2 &&
			      fr.buttons == 1,
		      "pointeur du clavier");
		nev = 0;
		gest_mouse(&G, &fr, emit, 0);
		uint8_t up[4] = {0x05, 0x00, 0x00, 0x00};
		hid_decode(&D, &M, up, 4, &fr);
		gest_mouse(&G, &fr, emit, 0);
		printf("  elan : pointeur du clavier (rapport 5) -> dx %ld dy %ld boutons %ld, puis boutons %ld\n", evs[0].a,
		       evs[0].b, evs[0].c, evs[1].c);
		CHECK(nev == 2 && evs[0].c == 1 && evs[1].c == 0, "pointeur du clavier : événements");
	}

	printf(fails ? "%d ÉCHEC(S)\n" : "tout est juste\n", fails);
	return fails != 0;
}
