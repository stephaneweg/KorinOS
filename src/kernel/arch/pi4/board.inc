; ============================================================================
;  arch/pi4/board.inc - carte memoire du SoC BCM2711 (Raspberry Pi 4)
;  Ref : Circle include/circle/bcm2711.h, BCM2711 ARM Peripherals datasheet
; ============================================================================

PERI_BASE		= 0xFE000000		; vue ARM des peripheriques (bus 0x7E000000)
GPU_BUS_ALIAS		= 0xC0000000		; alias bus "L2 non cache" pour le VC

; --- GPIO -------------------------------------------------------------------
GPIO_BASE		= PERI_BASE + 0x200000
GPFSEL1			= GPIO_BASE + 0x04
GPIO_PUP_PDN_CNTRL0	= GPIO_BASE + 0xE4	; specifique BCM2711 (remplace GPPUD)

; --- UART0 PL011 ------------------------------------------------------------
UART0_BASE		= PERI_BASE + 0x201000
UART_DR			= 0x00
UART_FR			= 0x18
UART_IBRD		= 0x24
UART_FBRD		= 0x28
UART_LCRH		= 0x2C
UART_CR			= 0x30
UART_ICR		= 0x44
UART_FR_TXFF		= 1 shl 5

; --- Mailbox VideoCore ------------------------------------------------------
MBOX_BASE		= PERI_BASE + 0xB880
MBOX_READ		= 0x00
MBOX_STATUS		= 0x18			; statut boite 0 (lecture ARM)
MBOX_STATUS1		= 0x38			; statut boite 1 (ecriture ARM)
MBOX_WRITE		= 0x20
MBOX_FULL		= 0x80000000
MBOX_EMPTY		= 0x40000000
MBOX_CH_PROP		= 8

; --- GIC-400 ----------------------------------------------------------------
GICD_BASE		= 0xFF841000
GICC_BASE		= 0xFF842000

; --- carte SD (arch/common/sdhci.asm) ----------------------------------------
SD_BASE_A		= PERI_BASE + 0x340000	; EMMC2 (vrai Pi 4)
SD_BASE_B		= PERI_BASE + 0x300000	; Arasan (QEMU y branche la carte ;
						; sur le vrai Pi : WiFi en SDIO)

; --- zones que les services peuvent mapper (dev_map, hardware.asm) ----------
; peripheriques (PCIe, GENET, EMMC...) jusqu'aux registres locaux ARM et au
; GIC exclus ; fenetre PCIe sortante (xHCI)
DEV_ZONE1_MIN		= 0xFC000000
DEV_ZONE1_MAX		= 0xFF800000
DEV_ZONE2_MIN		= 0x600000000
DEV_ZONE2_MAX		= 0x640000000
