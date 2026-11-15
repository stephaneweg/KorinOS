/* hidrd.h - lecture d'un descripteur de rapport HID pour i2chid (souris, pavé tactile « precision
 * touchpad » de Windows), sans rien du système : essayé hors matériel par test/hidrd_test.c.
 * docs/etude/TOUCHPAD-PC.md. */
#ifndef I2CHID_HIDRD_H
#define I2CHID_HIDRD_H

#include <stdint.h>

#define HU(page, id) ((uint32_t)(page) << 16 | (uint32_t)(id))
/* usages utiles */
#define U_GD_POINTER HU(1, 1)
#define U_GD_MOUSE HU(1, 2)
#define U_GD_X HU(1, 0x30)
#define U_GD_Y HU(1, 0x31)
#define U_GD_WHEEL HU(1, 0x38)
#define U_CON_ACPAN HU(0x0C, 0x238)
#define U_BUTTON1 HU(9, 1)
#define U_DIG_TOUCHPAD HU(0x0D, 0x05)
#define U_DIG_CONFIG HU(0x0D, 0x0E)
#define U_DIG_FINGER HU(0x0D, 0x22)
#define U_DIG_TIP HU(0x0D, 0x42)
#define U_DIG_CONFIDENCE HU(0x0D, 0x47)
#define U_DIG_CONTACT_ID HU(0x0D, 0x51)
#define U_DIG_INPUT_MODE HU(0x0D, 0x52)
#define U_DIG_CONTACT_COUNT HU(0x0D, 0x54)
#define U_DIG_CONTACT_MAX HU(0x0D, 0x55)
#define U_DIG_SCAN_TIME HU(0x0D, 0x56)
#define U_DIG_SURFACE_SWITCH HU(0x0D, 0x57)
#define U_DIG_BUTTON_SWITCH HU(0x0D, 0x58)
#define U_DIG_BUTTON_TYPE HU(0x0D, 0x59)

#define HK_INPUT 0
#define HK_OUTPUT 1
#define HK_FEATURE 2

#define HF_CONST 1
#define HF_VAR 2
#define HF_REL 4

#define HID_MAX_ITEMS 640
#define HID_MAX_FINGERS 10

/* un élément d'un rapport (une valeur ; un champ « tableau » : un élément par place, usage 0) */
struct hid_item {
	uint32_t usage;       /* page << 16 | id */
	uint32_t app;         /* usage de la collection d'application qui le contient */
	uint32_t bit;         /* position dans le rapport, après l'octet du numéro */
	int32_t lmin, lmax;   /* logique */
	int32_t pmin, pmax;   /* physique (pmin = pmax = 0 : celles du logique) */
	int32_t unit_exp;     /* exposant d'unité (-4..7) */
	uint32_t unit;
	uint16_t coll;        /* rang de la collection logique (0 : aucune) : un doigt du pavé */
	uint8_t size;         /* bits */
	uint8_t kind;         /* HK_* */
	uint8_t flags;        /* HF_* */
	uint8_t id;           /* numéro du rapport (0 : sans numéro) */
};

struct hid_desc {
	struct hid_item it[HID_MAX_ITEMS];
	int n;
	int ids;              /* 1 : rapports numérotés */
	int err;              /* 0, ou ce qui a coupé la lecture */
	int overflow;         /* éléments perdus (plus de HID_MAX_ITEMS) */
	uint32_t bits[3][256]; /* longueur de chaque rapport (bits), par sorte et par numéro */
};

/* lit le descripteur -> 0, ou < 0 */
int hid_parse_desc(struct hid_desc *d, const uint8_t *p, uint32_t len);
/* l'élément de cet usage, de cette sorte, dans cette collection d'application (0 : toutes), le
 * rang-ième -> indice, ou -1 */
int hid_find(const struct hid_desc *d, int kind, uint32_t app, uint32_t usage, int rank);
/* la valeur d'un élément dans un rapport (data : après l'octet du numéro ; len : ses octets) */
int32_t hid_get(const struct hid_item *it, const uint8_t *data, uint32_t len);
/* la valeur d'un élément écrite dans un rapport */
void hid_put(const struct hid_item *it, uint8_t *data, uint32_t len, int32_t v);
/* longueur en octets du rapport (sans l'octet du numéro) */
uint32_t hid_report_bytes(const struct hid_desc *d, int kind, int id);

/* ---- ce que le pilote en fait : un modèle de l'appareil */
struct hid_finger {
	int tip, conf, cid, x, y; /* indices d'éléments, -1 : absents */
};

#define HID_MAX_MICE 4

/* une souris (rapport relatif) : le touchpad en mode souris, un pointeur du clavier (« pointstick »)… */
struct hid_mouse {
	int id, x, y, wheel, pan, btn[5];
};

struct hid_model {
	struct hid_mouse m[HID_MAX_MICE];
	int nmouse;
	/* pavé tactile (« precision touchpad ») */
	int t_id, t_count, t_scan, nfing;
	int t_btn[3];                   /* boutons 1 à 3 du rapport du pavé (-1 : absents) */
	struct hid_finger f[HID_MAX_FINGERS];
	int32_t xmin, xmax, ymin, ymax; /* logique de X et Y du premier doigt */
	int32_t xmm100, ymm100;         /* taille physique, centièmes de mm (0 : inconnue) */
	/* réglages (Feature) */
	int mode_item;                  /* Input Mode (Digitizer 0x52) */
	int surface_item, button_item;  /* Surface Switch, Button Switch */
	int max_contacts_item;
};

/* trouve souris et pavé dans le descripteur -> 1 souris, 2 pavé, 3 les deux, 0 rien */
int hid_model_build(const struct hid_desc *d, struct hid_model *m);

/* ---- un rapport lu -> ce qui s'est passé */
struct hid_contact {
	int id, x, y, conf;
};

struct hid_frame {
	int kind;      /* 1 souris, 2 pavé */
	/* souris */
	int src;       /* laquelle (hid_model.m) */
	int dx, dy, wheel, pan, buttons;
	/* pavé */
	int count, button, scan; /* button : bits 0-2, boutons 1 à 3 du pavé */ /* count : -1 si absent ; scan : -1 si absent */
	int n;                   /* contacts posés dans ce rapport (c[0..n-1]) */
	struct hid_contact c[HID_MAX_FINGERS];
};

/* rapport complet (numéro compris s'il y en a) -> 1 si reconnu, 0 sinon */
int hid_decode(const struct hid_desc *d, const struct hid_model *m, const uint8_t *rep, uint32_t len,
	       struct hid_frame *fr);

#endif
