; ============================================================================
;  gpio.inc - le service " gpio " (SD0:/sys/svc/gpio) et GpioKit : protocole
;  (docs/KITS.md, docs/BASIC.md). Message de 512 o : +0 op puis resultat, +8,
;  +16, +24, +32 arguments, +44 pid de l'appelant (noyau), +48 donnees.
; ============================================================================

GPIO_OP_INFO	= 1		; -> 0 ; +8 : 1 emule (QEMU) ; +48 : GPIO_PINS x PIW_SIZE
GPIO_OP_MODE	= 2		; a broche, b mode
GPIO_OP_WRITE	= 3		; a broche, b niveau
GPIO_OP_READ	= 4		; a broche -> niveau
GPIO_OP_READALL	= 5		; -> les niveaux (bit n : GPIO n)
GPIO_OP_PWM	= 6		; a broche, b Hz, c duree haute /10000
GPIO_OP_EDGES	= 7		; a broche, b GP_RISING | GP_FALLING
GPIO_OP_EVENTS	= 8		; a places (GPIO_MSG_EVENTS au plus) -> n ; +48 : GPE_SIZE x n
GPIO_OP_RELEASE	= 9		; tout ce que l'appelant tient est rendu
GPIO_OP_I2C_OPEN = 10		; a Hz
GPIO_OP_I2C_CLOSE = 11
GPIO_OP_I2C_XFER = 12		; a adresse, b octets ecrits (+48), c octets lus (+48) -> c, ou b
GPIO_OP_SPI_OPEN = 13		; a Hz, b mode 0..3
GPIO_OP_SPI_CLOSE = 14
GPIO_OP_SPI_XFER = 15		; a CE, b octets (+48, dans les deux sens) -> b
GPIO_OP_MAX	= 15

GPIO_PINS	= 28		; GPIO 0..27 : le connecteur a 40 broches
GPIO_MSG_DATA	= 48
GPIO_MSG_BYTES	= 460		; donnees d'un message
GPIO_MSG_EVENTS	= 28

; une broche vue par le service (GPIO_OP_INFO)
PIW_MODE	= 0		; octet
PIW_FLAGS	= 1		; octet GP_F_*
PIW_REASON	= 2		; octet : 0 aucune, 1 EEPROM, 2 console, 3 tactile
PIW_LEVEL	= 3		; octet
PIW_OWNER	= 4
PIW_FREQ	= 8
PIW_DUTY	= 12
PIW_SIZE	= 16
