/* usbd.h : le service USB en C (usbd.c, cœur commun), son contrôleur xHCI
 * (xhci.c, commun au Pi et au PC) et la couche propre à chaque machine
 * (plat_x86_64.c : pcid, MSI-X ; plat_aarch64.c : pont PCIe du Pi 4, RP1 du
 * Pi 5). Le protocole avec les pilotes de classe est celui de l'asm
 * (src/inc/usb.inc, docs/USB.md) ; voir docs/etude/PC-PILOTES.md. */
#ifndef KORIN_USBD_H
#define KORIN_USBD_H

#include "korinrt.h"

/* ---- protocole du service « usb » (src/inc/usb.inc) */
#define USB_OP_DESCRIPTORS 1
#define USB_OP_CONTROL 2
#define USB_OP_INTR_OPEN 3
#define USB_OP_INTR_READ 4
#define USB_OP_BULK_BUFFER 5
#define USB_OP_BULK 6
#define USB_OP_BULK_NB 7
#define USB_OP_ISO_OPEN 8
#define USB_OP_ISO_CLOSE 9

#define HW_OP_USB 1
#define HW_OP_USB_REMOVED 2

#define USB_FS 0
#define USB_LS 1
#define USB_HS 2
#define USB_SS 3

#define USB_DT_INTERFACE 4
#define CFG_MAX 464 /* descripteur de configuration gardé (comme l'asm) */
#define BULK_MAX 65536

#define E_IO -5
#define E_BUSY -16
#define E_NODEV -19
#define E_TIMEDOUT -110

#define SVC_OP_SIGNAL 0xFFFD
#define SVC_OP_END 0xFFFF
#define SVF_ENDS 1

/* ---- usbd.c, pour le contrôleur */
void say(const char *fmt, ...);
/* un périphérique configuré (handle : son slot) : chaque interface est annoncée
 * à hwmanager -> numéro usbd (1..), < 0 si la table est pleine */
long usbd_attach(int handle, const uint8_t *dev_desc, const uint8_t *cfg, unsigned cfg_len, int speed);
/* retiré (le contrôleur a déjà rendu ses ressources) */
void usbd_detach(long dev);

/* ---- xhci.c */
int xh_init(const char *arg);
void xh_poll(int irq);                 /* événements ; ports toutes les XH_SCAN_MS */
long xh_irq_line(void);                /* ligne liée (canal du service, bit 1), -1 : sondage */
long xh_control(int slot, uint64_t setup, void *data, unsigned len);
long xh_intr_open(int slot, unsigned ep, unsigned mps, unsigned interval_ms);
long xh_intr_read(int pipe, const uint8_t **data); /* > 0 octets, 0 rien encore, < 0 erreur */
void xh_intr_close(int pipe);
long xh_bulk(int slot, unsigned ep, unsigned mps, void *data, unsigned len);
long xh_bulk_nb(int slot, unsigned ep, unsigned mps, void *data, unsigned len);
void xh_bulk_nb_stop(void);
long xh_bulk_reset(int slot, unsigned ep);

/* ---- couche de la machine (plat_<arch>.c) */
struct xh_plat {
	volatile uint8_t *regs; /* BAR du xHCI, mappée */
	uint64_t regs_size;
	uint64_t dma_offset;    /* adresse vue du contrôleur = physique + dma_offset */
	uint64_t dma_limit;     /* fin de la fenêtre DMA vue du contrôleur (0 : aucune) */
	const char *name;
	/* interruptions : pci_irq_setup ; -1 : sondage (Pi) */
	long (*irq_setup)(void);
};
int plat_xhci_init(const char *arg, struct xh_plat *p);
void plat_udelay(unsigned us);

/* barrières : DMA cohérent et stores ordonnés sur x86 ; mémoire non cachée
 * et barrières explicites sur le Pi (comme l'asm : dmb oshst, dsb sy) */
#if defined(__aarch64__)
#define dma_wmb() __asm__ volatile("dmb oshst" ::: "memory")
#define dma_rmb() __asm__ volatile("dmb oshld" ::: "memory")
#define dma_mb() __asm__ volatile("dsb sy" ::: "memory")
#else
#define dma_wmb() __asm__ volatile("" ::: "memory")
#define dma_rmb() __asm__ volatile("" ::: "memory")
#define dma_mb() __asm__ volatile("mfence" ::: "memory")
#endif

#endif
