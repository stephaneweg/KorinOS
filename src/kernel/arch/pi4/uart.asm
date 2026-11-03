; ============================================================================
;  arch/pi4/uart.asm - console serie du Pi 4 : UART0 (PL011) sur GPIO 14/15
; ============================================================================

; Appele tout au debut (MMU coupee) : registres poses directement, puis
; programmation du PL011. N'utilise que x0-x3.
uart_init:
	movl	x2,GPFSEL1			; GPIO14/15 -> ALT0 (TXD0/RXD0)
	ldr	w0,[x2]
	and	w0,w0,0xFFFC0FFF
	movl	w3,(4 shl 12) or (4 shl 15)
	orr	w0,w0,w3
	str	w0,[x2]
	movl	x2,GPIO_PUP_PDN_CNTRL0		; pas de pull-up/down sur 14/15
	ldr	w0,[x2]
	and	w0,w0,0x0FFFFFFF
	str	w0,[x2]
	b	pl011_init
