; ============================================================================
;  arch/pi5/uart.asm - console serie du Pi 5 : UART de debogage (PL011 sur
;  le connecteur 3 broches), deja programme par le firmware (enable_uart=1)
; ============================================================================

; N'utilise que x0-x3 (appele MMU coupee)
uart_init:
	movl	x1,UART0_BASE
	ldr	w0,[x1,UART_CR]			; deja actif : on garde sa vitesse
	tbnz	w0,0,.done
	b	pl011_init			; sinon : 115200 sur une horloge de 48 MHz
.done:	ret
