; bench - cout de la composition d'Elegant sur le materiel (SD0:/bin/bench)
;   mesure [secondes]   attend (20 s par defaut : bureau ouvert depuis
;                       startup.txt), puis fait recomposer tout l'ecran
;                       20 fois avec les transparences (ombres, coins, dock),
;                       puis 20 fois sans ; affiche le temps par image.
; Le resultat va aussi dans le journal du noyau (SD0:/journal.txt via journal).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

IMAGES	= 20

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	mov	x0,20000
	cbz	x19,.wait
	ldrb	w1,[x19]
	cbz	w1,.wait
	mov	x0,x19
	bl	parse_dec
	mov	x1,1000
	mul	x0,x0,x1
.wait:	sys	SYS_SLEEP
	mov	x0,0				; avec transparences
	bl	run
	tbnz	x0,63,.fail
	mov	x20,x0
	mov	x0,1				; sans
	bl	run
	tbnz	x0,63,.fail
	mov	x21,x0
	adr	x0,m_avec
	bl	puts
	mov	x0,x20
	bl	per_image
	adr	x0,m_sans
	bl	puts
	mov	x0,x21
	bl	per_image
	mov	x0,0
	sys	SYS_EXIT
.fail:	mov	x19,x0
	adr	x0,m_fail
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = 1 sans transparence -> x0 = ms pour IMAGES recompositions
run:
	adr	x1,msg
	mov	x2,WS_OP_MESURE
	str	x2,[x1,WM_OP]
	mov	x2,IMAGES
	str	x2,[x1,WM_A]
	str	x0,[x1,WM_B]
	mov	x0,x1
	sys	SYS_WS_CALL
	ret

; x0 = ms pour IMAGES images : affiche « x,y ms par image (z images/s) »
per_image:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x1,10				; dixiemes de ms par image
	mul	x0,x0,x1
	mov	x1,IMAGES
	udiv	x20,x0,x1
	mov	x1,10
	udiv	x0,x20,x1
	bl	print_dec
	adr	x0,m_virg
	bl	puts
	mov	x1,10
	udiv	x0,x20,x1
	msub	x0,x0,x1,x20
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,IMAGES*1000			; images par seconde
	cmp	x19,0
	csinc	x19,x19,xzr,ne
	udiv	x0,x0,x19
	bl	print_dec
	adr	x0,m_ips
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

m_avec	db 'mesure : composition de tout l''ecran avec transparences : ',0
m_sans	db 'mesure : sans transparence (contour opaque) : ',0
m_virg	db ',',0
m_ms	db ' ms par image (',0
m_ips	db ' images/s)',10,0
m_fail	db 'mesure : pas de serveur graphique, erreur ',0
m_nl	db 10,0
	align 4

segment readable writeable
kit_data
	align 8
msg	rb WS_MSG
