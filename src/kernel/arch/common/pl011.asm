; ============================================================================
;  arch/common/pl011.asm - UART PL011 (115200 8N1, horloge 48 MHz)
;  Adresse : UART0_BASE (board.inc). Les broches sont posees par la carte
;  (arch/<carte>/uart.asm : uart_init), qui appelle pl011_init.
; ============================================================================

; Programmation du PL011 (n'utilise que x0-x3 ; utilisable MMU coupee)
pl011_init:
	movl	x1,UART0_BASE
	str	wzr,[x1,UART_CR]		; UART off pendant la config

	mov	w0,0x7FF
	str	w0,[x1,UART_ICR]		; acquitte toutes les IRQ
	mov	w0,26				; 48e6 / (16*115200) = 26.04
	str	w0,[x1,UART_IBRD]
	mov	w0,3				; 0.04*64 = 3
	str	w0,[x1,UART_FBRD]
	mov	w0,0x70				; 8 bits, FIFO on
	str	w0,[x1,UART_LCRH]
	mov	w0,0x301			; UARTEN | TXE | RXE
	str	w0,[x1,UART_CR]
	ret

; w0 = caractere
uart_putc:
	movl	x1,UART0_BASE
.wait:	ldr	w2,[x1,UART_FR]
	tst	w2,UART_FR_TXFF
	b.ne	.wait
	str	w0,[x1,UART_DR]
	ret

; x0 = chaine terminee par 0
uart_puts:
	prologue
	mov	x9,x0
.next:	ldrb	w0,[x9],1
	cbz	w0,.done
	bl	uart_putc
	b	.next
.done:	epilogue
