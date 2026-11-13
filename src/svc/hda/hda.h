/* hda.h : pilote Intel High Definition Audio (docs/etude/SON-PC.md), d'après la norme HDA 1.0a.
 * hda.c : contrôleur (registres de BAR0, CORB/RIRB, flux DMA par BDL, boucle) ;
 * codec.c : codecs (widgets, chemins, prise casque, particularités des ALC285 de HP) ;
 * plat_x86_64.c : la fonction PCIe de pcid (BAR0, MSI, config PCI d'Intel et de NVIDIA). */
#ifndef HDA_H
#define HDA_H

#include "korinrt.h"
#include <stdint.h>

#define E_NODEV -19
#define E_BUSY -16
#define E_TIMEDOUT -110

/* ---- registres du contrôleur (BAR0) */
#define R_GCAP 0x00 /* 16 : OSS 15:12, ISS 11:8, BSS 7:3, 64OK 0 */
#define R_VMIN 0x02
#define R_VMAJ 0x03
#define R_GCTL 0x08
#define GCTL_CRST 0x1
#define GCTL_UNSOL 0x100
#define R_STATESTS 0x0E /* 16 : codecs présents */
#define R_LLCH 0x14     /* 16 : première capacité étendue (cAVS d'Intel) */
#define R_INTCTL 0x20
#define INTCTL_GIE 0x80000000u
#define INTCTL_CIE 0x40000000u
#define R_INTSTS 0x24
#define R_CORBLBASE 0x40
#define R_CORBUBASE 0x44
#define R_CORBWP 0x48 /* 16 */
#define R_CORBRP 0x4A /* 16, bit 15 : remise à zéro */
#define R_CORBCTL 0x4C /* 8 : bit 1 marche */
#define R_CORBSTS 0x4D
#define R_CORBSIZE 0x4E
#define R_RIRBLBASE 0x50
#define R_RIRBUBASE 0x54
#define R_RIRBWP 0x58 /* 16, bit 15 : remise à zéro */
#define R_RINTCNT 0x5A
#define R_RIRBCTL 0x5C /* 8 : bit 0 interruption, bit 1 DMA */
#define R_RIRBSTS 0x5D /* bit 0 réponse, bit 2 débordement */
#define R_RIRBSIZE 0x5E
#define R_ICOI 0x60 /* interface de commande immédiate */
#define R_ICII 0x64
#define R_ICIS 0x68 /* 16 : bit 0 occupé, bit 1 réponse valide */
#define R_SD0 0x80  /* descripteurs de flux : 0x20 chacun */
#define SD_CTL 0x00 /* 8 : SRST 0, RUN 1, IOCE 2 ; +2 : étiquette 7:4 */
#define SD_STS 0x03 /* 8 : BCIS 2, FIFOE 3, DESE 4 */
#define SD_LPIB 0x04
#define SD_CBL 0x08
#define SD_LVI 0x0C /* 16 */
#define SD_FMT 0x12 /* 16 */
#define SD_BDPL 0x18
#define SD_BDPU 0x1C

/* capacités étendues (cAVS) : en-tête : version 31:28, id 27:16, suivant 15:0 */
#define CAP_ML 2
#define CAP_PP 3
#define PP_PPCTL 0x04 /* GPROCEN 30, PIE 31, PROCEN(n) bit n */
#define ML_LCAP 0x40
#define ML_LCTL 0x44 /* SPA 16, CPA 23, SCF 3:0 */

/* ---- verbes */
#define V_GET_PARAM 0xF00
#define V_GET_CONN_SEL 0xF01
#define V_SET_CONN_SEL 0x701
#define V_GET_CONN_LIST 0xF02
#define V_SET_POWER 0x705
#define V_GET_POWER 0xF05
#define V_SET_STREAM 0x706
#define V_SET_PIN_CTL 0x707
#define V_GET_PIN_CTL 0xF07
#define V_SET_UNSOL 0x708
#define V_EXEC_SENSE 0x709
#define V_GET_SENSE 0xF09
#define V_SET_EAPD 0x70C
#define V_GET_EAPD 0xF0C
#define V_SET_GPIO_DATA 0x715
#define V_SET_GPIO_MASK 0x716
#define V_SET_GPIO_DIR 0x717
#define V_GET_CONFIG 0xF1C
#define V_GET_SUBSYS 0xF20
#define V_RESET 0x7FF
/* verbes de 4 bits (charge de 16 bits) */
#define V4_FORMAT 0x2
#define V4_AMP 0x3
#define V4_SET_COEF 0x4
#define V4_COEF_INDEX 0x5
#define V4_GET_COEF 0xC

/* paramètres */
#define P_VENDOR 0x00
#define P_REVISION 0x02
#define P_NODES 0x04
#define P_FG_TYPE 0x05
#define P_WCAPS 0x09
#define P_PCM 0x0A
#define P_PINCAP 0x0C
#define P_AMP_IN 0x0D
#define P_CONN_LEN 0x0E
#define P_GPIO 0x11
#define P_AMP_OUT 0x12

/* widgets : type (AUDIO_WIDGET_CAP 23:20) */
#define W_OUT 0
#define W_IN 1
#define W_MIXER 2
#define W_SEL 3
#define W_PIN 4
#define WC_STEREO 0x1
#define WC_IN_AMP 0x2
#define WC_OUT_AMP 0x4
#define WC_AMP_OVR 0x8
#define WC_CONN 0x100
#define WC_DIGITAL 0x200
#define WC_POWER 0x400
#define PC_TRIGGER 0x2 /* PIN_CAP */
#define PC_PRESENCE 0x4
#define PC_HP 0x8
#define PC_OUT 0x10
#define PC_IN 0x20
#define PC_EAPD 0x10000
#define PINCTL_VREF80 0x04
#define PINCTL_IN 0x20
#define PINCTL_OUT 0x40
#define PINCTL_HP 0x80
/* CONFIG_DEFAULT : appareil 23:20 */
#define DEV_LINE_OUT 0x0
#define DEV_SPEAKER 0x1
#define DEV_HP 0x2
#define DEV_SPDIF 0x4
#define DEV_DIG_OUT 0x5
#define DEV_LINE_IN 0x8
#define DEV_MIC 0xA

#define MAX_WIDGETS 96
#define MAX_CONN 16
#define MAX_PATH 6

struct widget {
	u8 nid, type, nconn, used;
	u32 caps, pincap, config, amp_in, amp_out, pcm;
	u8 conn[MAX_CONN];
};

/* un chemin : nid[0] (pin ou ADC) ... nid[n-1] (DAC ou pin) ; idx[i] : entrée de nid[i] vers nid[i+1] */
struct path {
	int n;
	u8 nid[MAX_PATH], idx[MAX_PATH];
};

/* une sortie du codec (pin et son chemin) */
struct opin {
	u8 nid, dev, jack, sense; /* appareil (DEV_*), 1 : en prise, 1 : détection de présence */
	u8 present, on;
	struct path p;
};

#define MAX_OPINS 6

struct codec {
	int addr;
	u32 vendor, subsys, rev;
	u8 afg, wstart, nw;
	u32 afg_pcm, afg_amp_in, afg_amp_out, gpio;
	struct widget w[MAX_WIDGETS];
	struct opin op[MAX_OPINS];
	int nop;
	struct path cap; /* entrée : ADC ... pin (cap.n = 0 : aucune) */
	u8 cap_pin, cap_dev, cap_jack;
	u32 rate_mask;   /* fréquences communes des DAC du chemin (bits de P_PCM) */
	u32 cap_rate_mask;
	char name[24];
};

/* ---- plat_x86_64.c */
struct hda_plat {
	volatile u8 *regs;
	u64 regs_size;
	u16 vendor, device;
	char where[24];  /* « 00:1f.3 » */
	long irq_line;   /* -1 : sondage */
};
int hda_plat_init(const char *arg, struct hda_plat *p);
long hda_plat_irq(struct hda_plat *p);
/* config PCI avant / après la remise à zéro (Intel : CGCTL) ; à la fin (rien) */
void hda_plat_before_reset(struct hda_plat *p);
void hda_plat_after_reset(struct hda_plat *p);

/* ---- hda.c */
extern struct hda_plat plat;
void hd_log(const char *fmt, ...);
u32 hd_cmd(int addr, int nid, u32 verb, u32 payload); /* verbe de 12 bits (charge 8) ou de 4 bits (charge 16) ;
						      * 0xFFFFFFFF : pas de réponse */
int hd_cmd_failed(void);
void hd_sleep(long ms);
extern int opt_quirks;
extern unsigned opt_gpio;

/* ---- codec.c */
int codec_probe(struct codec *c, int addr); /* 1 : groupe audio trouvé */
int codec_setup(struct codec *c);           /* chemins ; 1 : une sortie analogique au moins */
void codec_start_out(struct codec *c, int tag, u16 fmt);
void codec_start_in(struct codec *c, int tag, u16 fmt);
int codec_jacks(struct codec *c, int force); /* détection de présence ; 1 si un changement */
void codec_unsol(struct codec *c, u32 resp);

#endif
