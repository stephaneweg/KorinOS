; fputest - 4 threads EL0 qui calculent en NEON double precision et
; verifient leurs registres q pendant 3 s (preemptions entre threads
; et entre process). Le thread principal attend la fin par futex.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

NB_THREADS	= 4
STACK_SIZE	= 16384

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,msg_start
	bl	puts
	mov	x19,1
.spawn:	mov	x0,STACK_SIZE			; pile du thread sur le tas
	bl	malloc
	cbz	x0,.fail
	add	x1,x0,STACK_SIZE
	adr	x0,worker
	mov	x2,x19
	sys	SYS_THREAD_CREATE
	tbnz	x0,63,.fail
	add	x19,x19,1
	cmp	x19,NB_THREADS
	b.lo	.spawn
	mov	x0,0				; le thread principal travaille aussi
	bl	worker_body

	adr	x19,done_count			; attente des autres (futex)
.wait:	ldr	w1,[x19]
	cmp	w1,NB_THREADS
	b.hs	.all
	mov	x0,x19
	mov	x2,0				; (sans delai)
	sys	SYS_FUTEX_WAIT
	b	.wait
.all:	adr	x0,msg_errors
	bl	puts
	adr	x0,total_errors
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,msg_nl
	bl	puts
	adr	x0,total_errors
	ldr	x0,[x0]
	sys	SYS_EXIT
.fail:	adr	x0,msg_fail
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = index du thread
worker:
	bl	worker_body
	sys	SYS_THREAD_EXIT

; x0 = index : 3 s de calcul + verification de q0-q31
worker_body:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x20,x19,1			; motif propre au thread
	movl	x1,0x1111111111111111
	mul	x20,x20,x1
	dup	v8.2d,x20			; registres "temoins"
	mov	v9.16b,v8.16b
	mov	v15.16b,v8.16b
	mov	v31.16b,v8.16b
	sys	SYS_TIME_MS
	add	x21,x0,3000			; echeance
	mov	x22,0				; erreurs
	fmov	d0,1.0				; somme de 1/i^2 (-> pi^2/6)
	fmov	d1,1.0
	fmov	d2,1.0				; terme i=1
.loop:	fadd	d1,d1,d0			; i += 1
	fmul	d3,d1,d1
	fdiv	d3,d0,d3
	fadd	d2,d2,d3
	umov	x0,v8.d[1]
	umov	x1,v15.d[0]
	umov	x2,v31.d[1]
	cmp	x0,x20
	ccmp	x1,x20,0,eq
	ccmp	x2,x20,0,eq
	b.eq	.ok
	add	x22,x22,1
	dup	v8.2d,x20
	mov	v15.16b,v8.16b
	mov	v31.16b,v8.16b
.ok:	fcvtzs	x0,d1
	tst	x0,0xFFFF
	b.ne	.loop
	sys	SYS_TIME_MS
	cmp	x0,x21
	b.lo	.loop

	; resultat : "fputest : thread n  somme*1e6 = ...  erreurs e"
	sub	sp,sp,128
	mov	x0,sp
	adr	x1,msg_res
	bl	str_cat
	add	w1,w19,'0'
	strb	w1,[x0],1
	adr	x1,msg_sum
	bl	str_cat
	mov	x1,x0
	movl	x2,1000000
	scvtf	d4,x2
	fmul	d4,d2,d4
	fcvtzs	x0,d4
	bl	fmt_dec
	adr	x1,msg_err
	bl	str_cat
	mov	x1,x0
	mov	x0,x22
	bl	fmt_dec
	adr	x1,msg_nl
	bl	str_cat
	mov	x0,sp
	bl	puts
	add	sp,sp,128

	adr	x0,total_errors			; cumul + signalement de fin
.add:	ldaxr	x1,[x0]
	add	x1,x1,x22
	stlxr	w2,x1,[x0]
	cbnz	w2,.add
	adr	x0,done_count
.inc:	ldaxr	w1,[x0]
	add	w1,w1,1
	stlxr	w2,w1,[x0]
	cbnz	w2,.inc
	mov	x1,NB_THREADS
	sys	SYS_FUTEX_WAKE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue


segment readable writeable
kit_data

done_count	dw 0
		dw 0
total_errors	dd 0
msg_start	db 'fputest : 4 threads NEON pendant 3 s...',10,0
msg_res		db 'fputest : thread ',0
msg_sum		db '  somme 1/i',0xB2,' x1e6 = ',0
msg_err		db '  erreurs registres = ',0
msg_errors	db 'fputest : total erreurs = ',0
msg_fail	db 'fputest : echec creation de thread',10,0
msg_nl		db 10,0
