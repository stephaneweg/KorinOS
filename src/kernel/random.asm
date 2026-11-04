; ============================================================================
;  random.asm - generateur aleatoire du noyau (docs/RESEAU.md 5.1, etape 13 ;
;  decision de l'utilisateur, 29/10/2026) : il ne depend d'aucune valeur
;  venue d'un service (ni fichier de graine, ni paquets ni touches transmis),
;  seulement de ce que le noyau voit lui-meme :
;   * la gigue du compteur generique (au demarrage : 4096 mesures d'une
;     boucle de travail) ;
;   * l'instant de chaque interruption recue (le noyau les recoit toutes, y
;     compris celles des pilotes en service) et l'adresse interrompue ;
;   * chaque commutation (schedule) ;
;   * le RNG200 du BCM2711, sauf avec l'option no_rng200 de
;     SD0:/etc/options.txt (dans Onyx, un acces au RNG200 figeait le bus ;
;     sur le PocketTerm, il repond : actif par defaut depuis le 29/10/2026) ;
;     une ligne au journal dit le resultat. Le Cortex-A72 n'a pas RNDR.
;  Melange : 4 mots de 64 bits (multiplication, rotation : rapide, sans
;  verrou, appele depuis les interruptions). Tirage : ChaCha20 (RFC 8439)
;  dont la cle recoit le melange a chaque appel puis est remplacee par un
;  bloc de sortie (secret en avant).
;  Appel 101 getrandom(tampon, octets) -> octets (65536 au plus par appel).
; ============================================================================

; RNG200_BASE : board.inc (Pi 4 et Pi 5 different)
RNG_CTRL	= 0x00
RNG_SOFT_RESET	= 0x04
RBG_SOFT_RESET	= 0x08
RNG_INT_STATUS	= 0x18
RNG_FIFO_DATA	= 0x20
RNG_FIFO_COUNT	= 0x24
GETRANDOM_MAX	= 65536

; x0 = echantillon : melange dans le reservoir (x0-x3 seulement ; sans verrou :
; une course ne fait que perdre un peu de melange)
rng_mix:
	adr	x1,rng_idx
	ldr	x2,[x1]
	add	x3,x2,1
	str	x3,[x1]
	and	x2,x2,3
	adr	x1,rng_pool
	add	x1,x1,x2,lsl 3
	ldr	x2,[x1]
	eor	x2,x2,x0
	movl	x3,0x9E3779B97F4A7C15
	mul	x2,x2,x3
	ror	x2,x2,29
	ldr	x3,[x1,8]			; (un peu du mot voisin)
	eor	x2,x2,x3,lsr 7
	str	x2,[x1]
	ret

; (interruption : x0-x3 libres) instant et adresse interrompue
rng_irq:
	mrs	x0,cntpct_el0
	mrs	x1,elr_el1
	eor	x0,x0,x1,lsl 20
	b	rng_mix

; Au demarrage (avant les process) : gigue du compteur
rng_init:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,4096
	mov	x20,0x1F3
.j:	mrs	x0,cntpct_el0
	mov	x1,x19				; un peu de travail variable
	mov	x2,x20
.w:	mul	x2,x2,x1
	add	x2,x2,x19
	udiv	x3,x2,x20
	subs	x1,x1,1
	and	x1,x1,15
	b.ne	.w
	add	x20,x20,x3
	mrs	x1,cntpct_el0
	sub	x0,x1,x0
	eor	x0,x0,x1,lsl 32
	eor	x0,x0,x20,lsl 13
	bl	rng_mix
	subs	x19,x19,1
	b.ne	.j
	ldp	x19,x20,[sp],16
	epilogue

; (demarrage_suite, options lues) RNG200 : sauf avec l'option no_rng200 ;
; une ligne au journal dans tous les cas
rng200_probe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,rng_opt
	bl	opt_present
	cbz	x0,.essai
	adr	x0,msg_rng_off
	bl	kputs
	b	.out
.essai:	bl	board_is_emulated		; QEMU : pas de RNG200 emule
	cbnz	x0,.emul
	adr	x0,msg_rng_try			; (si le Pi fige ici : option no_rng200)
	bl	kputs
	movl	x19,RNG200_BASE
	ldr	w0,[x19,RNG_CTRL]		; arret, remise a zero, mise en route
	and	w0,w0,0xFFFFE000
	str	w0,[x19,RNG_CTRL]
	mov	w0,-1
	str	w0,[x19,RNG_INT_STATUS]
	mov	w0,1
	str	w0,[x19,RNG_SOFT_RESET]
	str	w0,[x19,RBG_SOFT_RESET]
	str	wzr,[x19,RNG_SOFT_RESET]
	str	wzr,[x19,RBG_SOFT_RESET]
	ldr	w0,[x19,RNG_CTRL]
	and	w0,w0,0xFFFFE000
	orr	w0,w0,1
	str	w0,[x19,RNG_CTRL]
	bl	time_ms				; 200 ms pour des mots dans la FIFO
	add	x20,x0,200
	mov	x21,0				; mots lus
.w:	ldr	w0,[x19,RNG_FIFO_COUNT]
	and	w0,w0,0xFF
	cbz	w0,.att
	ldr	w0,[x19,RNG_FIFO_DATA]
	mov	w22,w0
	bl	rng_mix
	add	x21,x21,1
	cmp	x21,64
	b.hs	.fin
	b	.w
.att:	bl	time_ms
	cmp	x0,x20
	b.lo	.w
.fin:	cbz	x21,.muet
	adr	x0,rng200_ok
	mov	w1,1
	str	w1,[x0]
	adr	x0,msg_rng_ok			; "RNG200 : 64 mots, dernier 0x..."
	bl	kputs
	mov	x0,x21
	bl	kput_dec
	adr	x0,msg_rng_ok2
	bl	kputs
	mov	w0,w22
	bl	kput_hex
	mov	w0,10
	bl	kputc
	b	.out
.muet:	adr	x0,msg_rng_muet
	bl	kputs
	b	.out
.emul:	adr	x0,msg_rng_emul
	bl	kputs
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = mot -> x0 = 1 s'il figure dans options.txt (sans rien annoncer)
opt_present:
	adr	x1,opts_buf
.start:	ldrb	w2,[x1]
	cbz	w2,.no
	mov	x3,0
.cmp:	ldrb	w4,[x0,x3]
	cbz	w4,.yes
	ldrb	w5,[x1,x3]
	cmp	w4,w5
	b.ne	.next
	add	x3,x3,1
	b	.cmp
.next:	add	x1,x1,1
	b	.start
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = 64 octets (sous rng_lock) : un bloc de sortie ; la cle recoit le
; reservoir avant, et un bloc neuf apres (secret en avant)
rng_block:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	adr	x0,rng_key			; cle ^= reservoir
	adr	x1,rng_pool
	mov	x2,0
.k:	ldr	x3,[x0,x2]
	ldr	x4,[x1,x2]
	eor	x3,x3,x4
	str	x3,[x0,x2]
	add	x2,x2,8
	cmp	x2,32
	b.lo	.k
	mrs	x0,cntpct_el0			; (et l'instant)
	adr	x1,rng_key
	ldr	x2,[x1]
	eor	x2,x2,x0
	str	x2,[x1]
	adr	x20,rng_ctr			; le bloc demande
	ldr	x1,[x20]
	add	x2,x1,1
	str	x2,[x20]
	adr	x0,rng_key
	adr	x2,rng_nonce
	mov	x3,x19
	bl	chacha20_block
	ldr	x1,[x20]			; une cle neuve
	add	x2,x1,1
	str	x2,[x20]
	adr	x0,rng_key
	adr	x2,rng_nonce
	mov	x3,sp
	bl	chacha20_block
	adr	x0,rng_key
	ldp	x1,x2,[sp]
	stp	x1,x2,[x0]
	ldp	x1,x2,[sp,16]
	stp	x1,x2,[x0,16]
	add	sp,sp,64
	ldp	x19,x20,[sp],16
	epilogue

; --- getrandom(tampon, octets) -> octets ----------------------------------
sys_getrandom:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	mov	x20,x1
	movl	x0,GETRANDOM_MAX
	cmp	x20,x0
	csel	x20,x20,x0,ls
	check_user x19,x20,.fault
	mov	x21,0				; fait
.b:	cmp	x21,x20
	b.hs	.fini
	adr	x0,rng_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,sp
	bl	rng_block
	adr	x0,rng_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	sub	x2,x20,x21
	cmp	x2,64
	mov	x3,64
	csel	x2,x2,x3,ls
	add	x0,x19,x21
	mov	x1,sp
	add	x21,x21,x2
	bl	mem_copy
	b	.b
.fini:	mov	x0,x20
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,64
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include '../inc/chacha20.inc'

rng_opt		db 'no_rng200',0
msg_rng_off	db 'Al',0xC3,0xA9,'a             : gigue, interruptions, commutations ; RNG200 non essay',0xC3,0xA9
		db ' (option no_rng200)',10,0
msg_rng_try	db 'Al',0xC3,0xA9,'a             : essai du RNG200 (no_rng200 pour l''',0xC3,0xA9,'viter)...',10,0
msg_rng_ok	db 'Al',0xC3,0xA9,'a             : RNG200 r',0xC3,0xA9,'pond, ',0
msg_rng_ok2	db ' mots lus, dernier ',0		; (kput_hex ecrit 0x)
msg_rng_muet	db 'Al',0xC3,0xA9,'a             : RNG200 muet (FIFO vide apr',0xC3,0xA8,'s 200 ms)',10,0
msg_rng_emul	db 'Al',0xC3,0xA9,'a             : RNG200 absent (',0xC3,0xA9,'mulateur)',10,0
		align 8
rng_pool	dd 0x243F6A8885A308D3, 0x13198A2E03707344, 0xA4093822299F31D0, 0x082EFA98EC4E6C89
rng_idx		dd 0
rng_ctr		dd 0
rng_key		rb 32
rng_nonce	dw 0x4D525041, 0x2D4D5241, 0x444E4152		; "ARMPI-ARMRAND"
rng_lock	dw 0
rng200_ok	dw 0
		align 8
