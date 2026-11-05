; ============================================================================
;  console.asm - console texte sur le framebuffer + sorties noyau (UART + ecran)
;
;  Police 8x16 Latin-1. La derniere ligne de l'ecran est une barre d'etat
;  hors zone de defilement. Le framebuffer est non cacheable : le defilement
;  relit l'ecran (lent mais simple) ; un tampon arriere viendra avec la GUI.
; ============================================================================

include 'font8x16.inc'

CON_FG		= 0x00D8D8D8 or PIXEL_ALPHA	; (PIXEL_ALPHA : board.inc)
CON_BG		= 0x00101828 or PIXEL_ALPHA
STATUS_FG	= 0x00101828 or PIXEL_ALPHA
STATUS_BG	= 0x0080B0E0 or PIXEL_ALPHA

con_init:
	prologue
	adr	x0,fb_w				; colonnes, lignes (derniere : barre
	ldp	x2,x3,[x0]			; d'etat)
	lsr	x4,x2,3				; / FONT_W
	lsr	x5,x3,4				; / FONT_H
	sub	x5,x5,1
	adr	x0,con_cols
	stp	x4,x5,[x0]
	mov	x0,0
	mov	x1,0
	movl	w4,CON_BG
	bl	fill_rect
	adr	x0,con_x
	stp	xzr,xzr,[x0]			; con_x, con_y
	mov	x1,1
	adr	x0,con_ready
	str	x1,[x0]
	epilogue

; w0 = caractere, x1 = x pixel, x2 = y pixel, w3 = encre, w4 = fond
draw_glyph:
	adr	x9,font_glyphs
	and	x0,x0,0xFF
	add	x9,x9,x0,lsl 4
	adr	x10,fb_base
	ldr	x10,[x10]
	adr	x11,fb_pitch
	ldr	x11,[x11]
	madd	x12,x2,x11,x10
	add	x12,x12,x1,lsl 2
	mov	x13,FONT_H
.row:	ldrb	w14,[x9],1
	lsl	w14,w14,24
	mov	x15,x12
	mov	x16,FONT_W
.px:	tst	w14,0x80000000
	csel	w17,w3,w4,ne
	str	w17,[x15],4
	lsl	w14,w14,1
	subs	x16,x16,1
	b.ne	.px
	add	x12,x12,x11
	subs	x13,x13,1
	b.ne	.row
	ret

; w0 = octet (10 = LF, 13 = CR, 8 = BS) ; les messages sont en UTF-8 : une
; suite de 2 a 4 octets est decodee, puis dessinee si elle est dans la police
; Latin-1 (sinon '?')
con_putc:
	cmp	w0,0x80
	b.lo	con_glyph
	adr	x1,con_utf
	ldr	x2,[x1]
	lsr	x3,x2,32			; octets restants
	cmp	w0,0xC0
	b.hs	.u_lead
	cbz	x3,con_glyph			; octet isole : tel quel
	and	x4,x2,0xFFFFFFFF
	and	w0,w0,0x3F
	orr	w4,w0,w4,lsl 6
	sub	x3,x3,1
	orr	x2,x4,x3,lsl 32
	str	x2,[x1]
	cbnz	x3,.u_ret
	mov	w0,w4
	cmp	w0,0x100
	b.lo	con_glyph
	mov	w0,'?'
	b	con_glyph
.u_lead:	cmp	w0,0xE0
	b.hs	.u_3
	and	w4,w0,0x1F
	mov	x3,1
	b	.u_set
.u_3:	cmp	w0,0xF0
	b.hs	.u_4
	and	w4,w0,0x0F
	mov	x3,2
	b	.u_set
.u_4:	and	w4,w0,0x07
	mov	x3,3
.u_set:	orr	x2,x4,x3,lsl 32
	str	x2,[x1]
.u_ret:	ret

; w0 = caractere de la police (Latin-1)
con_glyph:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,con_x
	ldp	x1,x2,[x19]			; x1 = colonne, x2 = ligne
	cmp	w0,10
	b.eq	.lf
	cmp	w0,13
	b.eq	.cr
	cmp	w0,8
	b.eq	.bs
	mov	x20,x1
	lsl	x1,x1,3				; * FONT_W
	lsl	x2,x2,4				; * FONT_H
	movl	w3,CON_FG
	movl	w4,CON_BG
	bl	draw_glyph
	add	x1,x20,1
	ldr	x2,[x19,8]
	adr	x3,con_cols
	ldr	x3,[x3]
	cmp	x1,x3
	b.lo	.store
.lf:	mov	x1,0
	add	x2,x2,1
	adr	x3,con_rows
	ldr	x3,[x3]
	cmp	x2,x3
	b.lo	.store
	bl	con_scroll
	mov	x1,0
	adr	x2,con_rows
	ldr	x2,[x2]
	sub	x2,x2,1
	b	.store
.cr:	mov	x1,0
	b	.store
.bs:	cbz	x1,.store
	sub	x1,x1,1
.store:	stp	x1,x2,[x19]
	ldp	x19,x20,[sp],16
	epilogue

; fait remonter la zone de texte d'une ligne
con_scroll:
	prologue
	adr	x0,fb_base
	ldr	x0,[x0]
	adr	x1,fb_pitch
	ldr	x1,[x1]
	lsl	x2,x1,4				; une ligne de texte en octets
	add	x3,x0,x2			; source
	adr	x4,con_rows
	ldr	x4,[x4]
	sub	x4,x4,1
	mul	x4,x4,x2			; octets a deplacer (multiple de 16)
.copy:	ldp	x5,x6,[x3],16
	stp	x5,x6,[x0],16
	subs	x4,x4,16
	b.ne	.copy
	adr	x1,con_rows
	ldr	x1,[x1]
	sub	x1,x1,1
	lsl	x1,x1,4				; * FONT_H
	mov	x0,0
	adr	x2,fb_w
	ldr	x2,[x2]
	mov	x3,FONT_H
	movl	w4,CON_BG
	bl	fill_rect
	epilogue

; x0 = chaine, x1 = colonne, x2 = ligne, x3 = largeur (completee d'espaces),
; w4 = encre, w5 = fond
draw_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	w23,w4
	mov	w24,w5
.ch:	cbz	x22,.end
	ldrb	w0,[x19]
	cbz	w0,.pad
	add	x19,x19,1
	b	.draw
.pad:	mov	w0,' '
.draw:	lsl	x1,x20,3
	lsl	x2,x21,4
	mov	w3,w23
	mov	w4,w24
	bl	draw_glyph
	add	x20,x20,1
	sub	x22,x22,1
	b	.ch
.end:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine : ecrite dans la barre d'etat (derniere ligne)
con_status:
	mov	x1,0
	adr	x3,con_cols
	ldp	x3,x2,[x3]			; colonnes, lignes
	movl	w4,STATUS_FG
	movl	w5,STATUS_BG
	b	draw_text

; ---------------------------------------------------------------------------
;  Sorties noyau : UART + console (si initialisee)
; ---------------------------------------------------------------------------

; w0 = caractere (sans verrou)
kputc_raw:
	prologue
	str	x19,[sp,-16]!
	mov	w19,w0
	bl	klog_putc			; journal du noyau (appel systeme klog)
	mov	w0,w19
	cmp	w0,10
	b.ne	.raw
	mov	w0,13				; LF -> CRLF sur la liaison serie
	bl	uart_putc
.raw:	mov	w0,w19
	bl	uart_putc
	adr	x0,con_ready
	ldr	x0,[x0]
	cbz	x0,.out
	mov	w0,w19
	bl	con_putc
.out:	ldr	x19,[sp],16
	epilogue

; --- klog(tampon, max) -> octets du journal du noyau (copies : min(n, max)) ---
KLOG_SIZE	= 65536
sys_klog:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,con_lock			; les lignes commencees partent telles
	bl	spin_lock_irqsave		; quelles (un process fini sans saut de
	str	x0,[sp,-16]!			; ligne n'y reste pas)
	bl	klog_flush_all
	ldr	x1,[sp],16
	adr	x0,con_lock
	bl	spin_unlock_irqrestore
	mov	x1,x20
	adr	x2,klog_len
	ldr	x2,[x2]
	cmp	x1,x2
	csel	x20,x1,x2,lo
	cbz	x20,.done
	check_user x19,x20,.fault
	mov	x0,x19
	adr	x1,klog_buf
	mov	x2,x20
	bl	mem_copy
.done:	mov	x0,x20
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Journal : lignes d'un seul tenant, horodatees (sous con_lock)
;  Chaque ecrivain (le thread courant ; sans thread : le coeur) assemble sa
;  ligne dans une case. Au saut de ligne, elle part d'un bloc dans klog_buf,
;  precedee de l'instant de son premier caractere, "[  12.345] " : secondes
;  du compteur materiel (remis a 0 par le firmware du Pi juste avant le noyau,
;  docs/BOOT.md). La liaison serie et la console, elles, recoivent chaque
;  caractere tout de suite.
; ---------------------------------------------------------------------------
KLS_OWNER	= 0
KLS_T		= 8
KLS_LEN		= 16
KLS_BUF		= 24
KLS_SIZE	= 256				; (decalage de 8 : klog_putc)
KLS_MAX		= KLS_SIZE - KLS_BUF
KLS_N		= 8				; (puissance de 2 : tourniquet)

; -> x0 = ecrivain (jamais 0)
klog_owner:
	this_cpu x0
	adr	x1,percpu_area			; (TPIDR_EL1 n'est sur qu'une fois
	subs	x2,x0,x1			; sched_init_cpu passe)
	b.lo	.boot
	cmp	x2,PERCPU_SIZE*MAX_CPUS
	b.hs	.boot
	ldr	x1,[x0,PC_CURRENT]
	cbz	x1,.cpu
	adr	x0,klog_user			; ce qu'ecrit le process (print) ne se
	ldr	x0,[x0]				; mele pas a ce que le noyau ecrit pour
	orr	x0,x1,x0			; lui (spawn : "image ... chargee")
	ret
.cpu:	orr	x0,x0,1
	ret
.boot:	mov	x0,1
	ret

; w0 = caractere
klog_putc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w19,w0
	bl	klog_owner
	mov	x20,x0
	adr	x21,klog_slots
	mov	x22,0				; premiere case libre
	mov	x2,KLS_N
.find:	ldr	x3,[x21,KLS_OWNER]
	cmp	x3,x20
	b.eq	.have
	cbnz	x3,.next
	cbnz	x22,.next
	mov	x22,x21
.next:	add	x21,x21,KLS_SIZE
	subs	x2,x2,1
	b.ne	.find
	cbnz	x22,.claim
	adr	x0,klog_victim			; toutes prises : la suivante du
	ldr	x1,[x0]				; tourniquet part telle quelle
	add	x2,x1,1
	and	x2,x2,KLS_N-1
	str	x2,[x0]
	adr	x22,klog_slots
	add	x22,x22,x1,lsl 8
	mov	x0,x22
	bl	klog_flush
.claim:	mov	x21,x22
	str	x20,[x21,KLS_OWNER]
	bl	time_ms
	str	x0,[x21,KLS_T]
.have:	cmp	w19,10
	b.eq	.line
	ldr	x1,[x21,KLS_LEN]
	add	x2,x21,KLS_BUF
	strb	w19,[x2,x1]
	add	x1,x1,1
	str	x1,[x21,KLS_LEN]
	cmp	x1,KLS_MAX
	b.lo	.out
	mov	x0,x21				; case pleine : ce morceau part, la
	bl	klog_flush			; suite fait une autre ligne
	str	x20,[x21,KLS_OWNER]
	bl	time_ms
	str	x0,[x21,KLS_T]
	b	.out
.line:	mov	x0,x21
	bl	klog_flush
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Toutes les cases commencees partent
klog_flush_all:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,klog_slots
	mov	x20,KLS_N
.s:	ldr	x0,[x19,KLS_OWNER]
	cbz	x0,.n
	mov	x0,x19
	bl	klog_flush
.n:	add	x19,x19,KLS_SIZE
	subs	x20,x20,1
	b.ne	.s
	ldp	x19,x20,[sp],16
	epilogue

; x0 = case : "[secondes.mmm] " + la ligne + saut de ligne dans klog_buf ;
; la case est liberee
klog_flush:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	ldr	x0,[x19,KLS_T]
	mov	x1,1000
	udiv	x20,x0,x1			; secondes
	msub	x22,x20,x1,x0			; millisecondes
	mov	x0,x20
	mov	x1,sp
	bl	fmt_dec
	mov	x1,sp
	sub	x21,x0,x1			; chiffres des secondes
	mov	w0,'['
	bl	klog_byte
.pad:	cmp	x21,4
	b.hs	.sec
	mov	w0,' '
	bl	klog_byte
	add	x21,x21,1
	b	.pad
.sec:	mov	x21,sp
.sd:	ldrb	w0,[x21],1
	cbz	w0,.ms
	bl	klog_byte
	b	.sd
.ms:	mov	w0,'.'
	bl	klog_byte
	mov	x1,100
	udiv	x0,x22,x1
	msub	x22,x0,x1,x22
	add	w0,w0,'0'
	bl	klog_byte
	mov	x1,10
	udiv	x0,x22,x1
	msub	x22,x0,x1,x22
	add	w0,w0,'0'
	bl	klog_byte
	add	w0,w22,'0'
	bl	klog_byte
	mov	w0,']'
	bl	klog_byte
	mov	w0,' '
	bl	klog_byte
	add	x21,x19,KLS_BUF
	ldr	x22,[x19,KLS_LEN]
.c:	cbz	x22,.nl
	ldrb	w0,[x21],1
	bl	klog_byte
	sub	x22,x22,1
	b	.c
.nl:	mov	w0,10
	bl	klog_byte
	str	xzr,[x19,KLS_OWNER]
	str	xzr,[x19,KLS_LEN]
	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = octet, a la suite du journal (perdu une fois les 64 Ko pleins)
klog_byte:
	adr	x1,klog_len
	ldr	x2,[x1]
	movl	x3,KLOG_SIZE
	cmp	x2,x3
	b.hs	.full
	adr	x3,klog_buf
	strb	w0,[x3,x2]
	add	x2,x2,1
	str	x2,[x1]
.full:	ret

; w0 = caractere
kputc:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	adr	x0,con_lock
	bl	spin_lock_irqsave
	mov	x20,x0
	mov	w0,w19
	bl	kputc_raw
	adr	x0,con_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine terminee par 0 (ecrite d'un bloc, sous verrou)
kputs:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,con_lock
	bl	spin_lock_irqsave
	mov	x20,x0
.next:	ldrb	w0,[x19],1
	cbz	w0,.done
	bl	kputc_raw
	b	.next
.done:	adr	x0,con_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur -> "0x" + 16 chiffres hexa
kput_hex:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	w0,'0'
	bl	kputc
	mov	w0,'x'
	bl	kputc
	mov	x20,60
.digit:	lsrv	x0,x19,x20
	and	x0,x0,0xF
	cmp	x0,10
	b.lo	.num
	add	x0,x0,'A'-10-'0'
.num:	add	x0,x0,'0'
	bl	kputc
	subs	x20,x20,4
	b.pl	.digit
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur non signee, x1 = tampon -> x0 = fin de chaine (0 ecrit)
fmt_dec:
	sub	sp,sp,32
	mov	x2,sp				; chiffres a l'envers sur la pile
	mov	x6,sp
	mov	x3,10
.div:	udiv	x4,x0,x3
	msub	x5,x4,x3,x0
	add	w5,w5,'0'
	strb	w5,[x2],1
	mov	x0,x4
	cbnz	x0,.div
.rev:	ldrb	w5,[x2,-1]!
	strb	w5,[x1],1
	cmp	x2,x6
	b.ne	.rev
	strb	wzr,[x1]
	mov	x0,x1
	add	sp,sp,32
	ret

; x0 = valeur non signee, en decimal
kput_dec:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	kputs
	add	sp,sp,32
	epilogue

; x0 = destination, x1 = chaine source -> x0 = fin (sur le 0 final)
str_copy:
.c:	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.e
	add	x0,x0,1
	b	.c
.e:	ret

; (primitive de dessin directe dans le framebuffer, avant la GUI)
; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, w4 = couleur 0x00RRGGBB
fill_rect:
	cbz	x2,.done
	cbz	x3,.done
	adr	x9,fb_base
	ldr	x9,[x9]
	adr	x10,fb_pitch
	ldr	x10,[x10]
	madd	x11,x1,x10,x9			; debut de ligne = base + y*pitch
	add	x11,x11,x0,lsl 2
.row:	mov	x12,x11
	mov	x13,x2
.px:	str	w4,[x12],4
	subs	x13,x13,1
	b.ne	.px
	add	x11,x11,x10
	subs	x3,x3,1
	b.ne	.row
.done:	ret

; w0 = octet -> deux chiffres hexa sur la console
put_hex8:
	prologue
	str	x19,[sp,-16]!
	mov	w19,w0
	lsr	w0,w19,4
	bl	.dig
	and	w0,w19,0xF
	bl	.dig
	ldr	x19,[sp],16
	epilogue
.dig:	cmp	w0,10
	add	w1,w0,'0'
	add	w2,w0,'a'-10
	csel	w0,w1,w2,lo
	b	kputc

; x0 = adresse MAC (6 octets) : affichage aa:bb:cc:dd:ee:ff
put_mac:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.b:	ldrb	w0,[x19,x20]
	bl	put_hex8
	add	x20,x20,1
	cmp	x20,6
	b.hs	.e
	mov	w0,':'
	bl	kputc
	b	.b
.e:	ldp	x19,x20,[sp],16
	epilogue
