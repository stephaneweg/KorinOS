; ============================================================================
;  arch/pi5/board.inc - carte memoire du Raspberry Pi 5 (BCM2712 + RP1)
;
;  Adresses physiques sur 40 bits : peripheriques du SoC a 0x10_7C00_0000,
;  bus AXI a 0x10_0000_0000, puce RP1 (GPIO, USB, Ethernet, UART, I2C...)
;  vue par le firmware a 0x1F_0000_0000 (lien PCIe deja etabli).
;  Ref : documentation BCM2712 / RP1 ; Circle et Onyx (docs/PI5-PORT.md)
;  n'ont servi que de carte. NON TESTE SUR MATERIEL.
; ============================================================================

PERI_BASE		= 0x107C000000		; ARM_IO_BASE du BCM2712
GPU_BUS_ALIAS		= 0xC0000000		; adresses vues par le VideoCore

; --- console : UART de debogage (connecteur 3 broches), PL011 ---------------
UART0_BASE		= PERI_BASE + 0x1001000	; 0x10_7D00_1000
UART_DR			= 0x00
UART_FR			= 0x18
UART_IBRD		= 0x24
UART_FBRD		= 0x28
UART_LCRH		= 0x2C
UART_CR			= 0x30
UART_ICR		= 0x44
UART_FR_TXFF		= 1 shl 5

; --- Mailbox VideoCore ------------------------------------------------------
MBOX_BASE		= PERI_BASE + 0x13880
MBOX_READ		= 0x00
MBOX_STATUS		= 0x18
MBOX_STATUS1		= 0x38
MBOX_WRITE		= 0x20
MBOX_FULL		= 0x80000000
MBOX_EMPTY		= 0x40000000
MBOX_CH_PROP		= 8

; --- GIC-400 ----------------------------------------------------------------
GICD_BASE		= PERI_BASE + 0x3FF9000	; 0x10_7FFF_9000
GICC_BASE		= PERI_BASE + 0x3FFA000

; --- carte SD (arch/common/sdhci.asm) ----------------------------------------
SD_BASE_A		= 0x1000FFF000		; controleur SDHCI de la carte SD
SD_BASE_B		= 0
SD_CLOCK_HZ		= 200000000		; horloge de base (fixe)

; --- RP1 : GPIO de la banque 0 (broches 0-27 du connecteur) -------------------
RP1_IO_BANK0		= 0x1F000D0000		; par broche : etat (+0), controle (+4)
RP1_RIO_BANK0		= 0x1F000E0000		; sortie (+0), activation (+4), entree (+8)
RP1_PADS_BANK0		= 0x1F000F0000		; plots : +4 + 4 x broche
RP1_SET			= 0x2000		; alias atomiques des registres RIO
RP1_CLR			= 0x3000

; regions de peripheriques mappees (blocs de 1 Go, arch/pi5/mmu.asm)
PERI_REGION_AXI		= 0x1000000000		; SD, PCIe...
PERI_REGION_SOC		= 0x1040000000		; peripheriques du SoC, GIC
PERI_REGION_RP1		= 0x1F00000000

; zones que les services peuvent mapper (dev_map, hardware.asm ; non teste) :
; bus AXI et peripheriques du SoC jusqu'au GIC exclu, puis la puce RP1
DEV_ZONE1_MIN		= 0x1000000000
DEV_ZONE1_MAX		= 0x107FF00000
DEV_ZONE2_MIN		= 0x1F00000000
DEV_ZONE2_MAX		= 0x1F00400000
