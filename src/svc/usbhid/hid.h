/* usbhid/hid.h - pilotes HID USB en C, communs au Pi et au PC : usbkeyboard,
 * usbmouse, usbgamepad (un même corps, trois services comme en asm).
 *
 * Repris de src/svc/inc/hiddrv.inc (corps du pilote), hid.inc (lecture du
 * descripteur de rapport et décodage) et hidpad.inc (manettes). Mêmes messages,
 * même protocole (src/inc/usb.inc avec usbd, anneau HID_OP_PILOTE de hidmanager),
 * mêmes bizarreries (« comme l'asm » dans les commentaires).
 *
 * Ce dossier n'est pas un service (pas de main) : chaque pilote le compile par
 * son svc.conf (sources = ../usbhid/hiddrv.c, ...). */
#ifndef USBHID_H
#define USBHID_H

#include "korinrt.h"

typedef int8_t i8;
typedef int16_t i16;

/* types d'événements de l'anneau (hid.inc, pad.inc) */
#define EV_KEY 1
#define EV_MOUSE_REL 2
#define EV_MOUSE_ABS 3
#define EV_WHEEL 4
#define EV_PAD_RAW 16

#define HID_MAX_USAGES 16

/* nature d'une fonction HID (HF_KIND) */
#define HID_KBD 1
#define HID_MOUSE 2
#define HID_TABLET 3
#define HID_REPORT 4 /* décodé d'après le descripteur de rapport */
#define HID_PAD 5    /* manette : hidpad.c */

/* une fonction HID (HF_* de hid.inc ; positions en bits, -1 : absent) */
struct hid_fn {
	i32 kind, ep, mps, iface, subclass;
	u8 prev[8];  /* rapport clavier précédent (format boot) */
	u32 rdlen;   /* longueur du descripteur de rapport */
	u32 has;     /* bit 0 clavier, bit 1 souris */
	u32 ids;     /* 1 : rapports numérotés (1er octet) */
	i32 k_id, k_mod, k_arr, k_arrn, k_arrs, k_bmp, k_bmpmin, k_bmpn;
	i32 m_id, m_btn, m_btnn, m_x, m_xs, m_y, m_ys, m_w, m_ws, m_abs, m_xmax, m_ymax;
};

/* hidparse.c (hid.inc) */
void hid_parse(struct hid_fn *f, const u8 *d, u32 len);
void usb_report_parsed(struct hid_fn *f, const u8 *data);
/* octets valables du rapport en cours (hiddrv.c le pose avant chaque rapport) : les positions et tailles venues du
 * descripteur ne lisent pas au-delà (P9) */
extern u32 hid_rep_len;
void usb_report_kbd(struct hid_fn *f, const u8 *rep);
void usb_report_mouse(const u8 *rep);
void usb_report_tablet(const u8 *rep);

/* hidpad.c (hidpad.inc) */
extern u32 pd_vidpid;
int pad_parse(const u8 *d, u32 len);
void pad_report(const u8 *data);

/* hiddrv.c : un événement dans l'anneau de hidmanager (perdu s'il est plein) */
void hid_push(u64 type, u64 a, u64 b, u64 c);
/* le corps du pilote : name = « usbkeyboard »..., pad = 1 pour usbgamepad */
int hid_driver_run(const char *name, int pad);

#endif
