/* pci.h : le service « pci » (pcid) et la petite bibliothèque des pilotes
 * PCIe (pcilib.c), docs/etude/PC-PILOTES.md.
 *
 * pcid énumère les bus PCIe par l'ECAM (table ACPI MCFG), tient la liste des
 * fonctions et les annonce à hwmanager (HW_OP_PCI) ; un pilote lancé pour une
 * fonction la « prend » (PCI_OP_CLAIM : décodage mémoire et maître du bus
 * allumés) puis mappe lui-même ses BAR (DEV_MAP) et ses vecteurs MSI/MSI-X
 * (IRQ_MSI). Constantes en assembleur : src/inc/pci.inc (mêmes valeurs). */
#ifndef KORIN_PCI_H
#define KORIN_PCI_H

#include <stdint.h>

/* ---- adresse d'une fonction : segment << 16 | bus << 8 | dev << 3 | fn,
 * écrite « SSSS:BB:DD.F » (argument des pilotes lancés par hwmanager) */
#define PCI_ADDR(seg, bus, dev, fn) \
	((uint32_t)(seg) << 16 | (uint32_t)(bus) << 8 | (uint32_t)(dev) << 3 | (uint32_t)(fn))

/* ---- service « pci » : message de 512 o (op en +0, a b c d, données en +48) */
#define PCI_OP_COUNT 1     /* -> nombre de fonctions */
#define PCI_OP_INFO 2      /* a = rang -> 0, struct pci_info en +48 ; E_NOENT au-delà */
#define PCI_OP_FIND 3      /* a = vendeur << 16 | appareil (0xFFFF : tous), b = classe << 16 |
			    *   sous-classe << 8 | interface, c = masque de b, d = rang de départ
			    *   -> rang de la première qui convient, E_NOENT */
#define PCI_OP_CLAIM 4     /* a = adresse -> 0, struct pci_info en +48 ; la fonction est au
			    *   pilote (rendue à sa fin) : décodage mémoire, maître du bus,
			    *   INTx coupé ; E_BUSY : à un autre */
#define PCI_OP_RELEASE 5   /* a = adresse -> 0 */
#define PCI_OP_CFG_READ 6  /* a = adresse, b = décalage, c = taille (1, 2, 4) -> valeur */
#define PCI_OP_CFG_WRITE 7 /* a = adresse, b = décalage, c = taille, d = valeur -> 0 (le pilote
			    *   qui la tient seulement) */
#define PCI_OP_RESCAN 8    /* -> nombre de fonctions (de nouveau annoncées) */

/* ---- annonce à hwmanager (src/inc/usb.inc : HW_OP_USB 1, HW_OP_USB_RETRAIT 2) */
#define HW_OP_PCI 3 /* a = adresse, b = classe << 16 | sous-classe << 8 | interface,
		     * c = vendeur << 16 | appareil, d = sous-système (vendeur << 16 | id) ;
		     * hwmanager cherche pci:VVVV:DDDD, pci:cc/ss/pp, pci:cc/ss, pci:cc dans
		     * drivers.txt et lance le pilote avec « SSSS:BB:DD.F » */

/* BAR : bits de pci_info.bar_flags[i] */
#define PCI_BAR_IO 1
#define PCI_BAR_64 2
#define PCI_BAR_PREFETCH 4

/* une fonction (PCI_OP_INFO, PCI_OP_CLAIM : 160 o, mêmes décalages sur les
 * deux machines) */
struct pci_info {
	uint32_t addr;
	uint16_t vendor, device;
	uint8_t cls, sub, prog, rev;
	uint8_t hdr;          /* type d'en-tête (0 : fonction, 1 : pont), bit 7 : multifonction */
	uint8_t irq_pin;      /* INTx (1 = INTA), 0 : aucun */
	uint8_t cap_msi;      /* décalage de la capacité MSI dans la config, 0 : absente */
	uint8_t cap_msix;     /* MSI-X */
	uint8_t cap_pcie;     /* PCI Express */
	uint8_t cap_pm;       /* gestion d'énergie */
	uint8_t msi_max;      /* vecteurs MSI possibles (1, 2, 4… 32) */
	uint8_t msi_64;       /* 1 : adresse MSI sur 64 bits */
	uint16_t msix_count;  /* entrées de la table MSI-X */
	uint16_t svid, ssid;  /* sous-système */
	uint8_t msix_table_bar, msix_pba_bar;
	uint32_t msix_table_off, msix_pba_off;
	uint8_t secondary;    /* pont : bus secondaire */
	uint8_t bar_flags[6];
	uint8_t pad;
	uint32_t pad1;
	uint64_t ecam;        /* adresse physique de sa page de configuration (4 Ko) */
	uint64_t bar[6];      /* adresse physique (BAR 64 bits : dans la première des deux) */
	uint64_t bar_size[6]; /* 0 : absente (ou non mesurée : affichage) */
	int32_t owner;        /* pid du pilote qui la tient, 0 : libre */
	uint32_t pad2;
};

/* ---- pcilib.c : côté pilote */

/* « SSSS:BB:DD.F » ou « BB:DD.F » -> adresse, -1 si illisible */
long pci_parse_addr(const char *s);
/* « 0000:00:04.0 » dans buf (13 o au moins) */
void pci_format_addr(uint32_t addr, char *buf);
/* le service « pci » (attente en ms) : 0 ou < 0 */
long pci_open(long wait_ms);
/* la fonction pour ce pilote : info remplie */
long pci_claim(uint32_t addr, struct pci_info *info);
/* la première fonction de cette classe (b, c de PCI_OP_FIND) à partir du
 * rang *from ; info remplie, *from avancé -> 0, E_NOENT */
long pci_find_class(uint32_t cls, uint32_t mask, long *from, struct pci_info *info);
long pci_cfg_read(uint32_t addr, unsigned off, unsigned size);
long pci_cfg_write(uint32_t addr, unsigned off, unsigned size, uint32_t v);
/* la BAR i mappée (registres non cachés) : adresse, ou < 0 */
long pci_map_bar(const struct pci_info *info, int i);
/* MSI-X (sinon MSI, un vecteur) : n vecteurs demandés au noyau (IRQ_MSI),
 * écrits dans la table (BAR mappée par pcilib) ou la capacité, INTx coupé ;
 * lines[] reçoit les lignes (IRQ_BIND…) -> vecteurs obtenus (>= 1), ou < 0
 * (E_NOSYS : pas de MSI, le pilote sonde) */
long pci_irq_setup(const struct pci_info *info, unsigned n, long *lines);

#endif
