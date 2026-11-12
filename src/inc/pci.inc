; ============================================================================
;  inc/pci.inc - service "pci" (pcid, PC) : memes valeurs que src/svc/pcid/pci.h
;  (docs/etude/PC-PILOTES.md). Annonce a hwmanager : HW_OP_PCI (inc/usb.inc).
; ============================================================================

PCI_OP_COUNT		= 1		; -> nombre de fonctions
PCI_OP_INFO		= 2		; a = rang -> 0, struct pci_info (160 o) en +48
PCI_OP_FIND		= 3		; a = vendeur << 16 | appareil (0xFFFF : tous), b = classe,
					;   c = masque de b, d = rang de depart -> rang, E_NOENT
PCI_OP_CLAIM		= 4		; a = adresse -> 0, pci_info en +48 ; E_BUSY
PCI_OP_RELEASE		= 5		; a = adresse -> 0
PCI_OP_CFG_READ		= 6		; a = adresse, b = decalage, c = taille -> valeur
PCI_OP_CFG_WRITE	= 7		; a, b, c, d = valeur -> 0 (le pilote qui la tient)
PCI_OP_RESCAN		= 8		; -> nombre de fonctions (annoncees de nouveau)
