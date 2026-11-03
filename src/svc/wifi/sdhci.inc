; ============================================================================
;  inc/sdhci.inc - registres et bits d'un controleur SDHCI (pour le Wi-Fi en
;  SDIO, arch/pi4/sdio.asm ; le pilote de la carte SD est un service :
;  src/svc/sd/sdhci.asm, qui definit les memes noms)
; ============================================================================

SD_ARG2		= 0x00
SD_BLKSIZECNT	= 0x04
SD_ARG1		= 0x08
SD_CMDTM	= 0x0C
SD_RESP0	= 0x10
SD_RESP1	= 0x14
SD_RESP2	= 0x18
SD_RESP3	= 0x1C
SD_DATA		= 0x20
SD_STATUS	= 0x24
SD_CONTROL0	= 0x28
SD_CONTROL1	= 0x2C
SD_INTERRUPT	= 0x30
SD_IRPT_MASK	= 0x34
SD_IRPT_EN	= 0x38
SD_CONTROL2	= 0x3C
SD_CAPS		= 0x40
SD_VERSION	= 0xFC

; CMDTM
CMD_RSP_NONE	= 0
CMD_RSP_136	= 1 shl 16
CMD_RSP_48	= 2 shl 16
CMD_RSP_48B	= 3 shl 16
CMD_CRCCHK	= 1 shl 19
CMD_IXCHK	= 1 shl 20
CMD_DATA	= 1 shl 21
CMD_READ	= 1 shl 4
CMD_MULTI	= 1 shl 5
CMD_BLKCNT	= 1 shl 1
CMD_AUTO12	= 1 shl 2

RSP_R1		= CMD_RSP_48 or CMD_CRCCHK or CMD_IXCHK
RSP_R1B		= CMD_RSP_48B or CMD_CRCCHK or CMD_IXCHK
RSP_R2		= CMD_RSP_136 or CMD_CRCCHK
RSP_R3		= CMD_RSP_48
RSP_R6		= CMD_RSP_48 or CMD_CRCCHK or CMD_IXCHK
RSP_R7		= CMD_RSP_48 or CMD_CRCCHK or CMD_IXCHK

; INTERRUPT
INT_CMD_DONE	= 1 shl 0
INT_DATA_DONE	= 1 shl 1
INT_WRITE_RDY	= 1 shl 4
INT_READ_RDY	= 1 shl 5
INT_ERR		= 1 shl 15

SD_TIMEOUT_MS	= 500
