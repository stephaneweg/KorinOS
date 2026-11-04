; proctest - essai du nombre de process (le noyau n'a plus de plafond :
; src/kernel/proc.asm) :
;   proctest [N]	lance N fois « sleep 60 » (100 par defaut, 2000 au plus),
;			compte les process (SYS_PROCLIST_FROM), les arrete (kill),
;			attend chacun (wait), puis verifie qu'un pid neuf n'est
;			aucun des anciens.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

N_MAX		= 2000
CHUNK		= 64

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	mov	x19,x0
	bl	kits_open
	mov	x20,100
	cbz	x19,.n
	ldrb	w0,[x19]
	cbz	w0,.n
	mov	x0,x19
	bl	parse_dec
	cmp	x0,0
	csel	x20,x0,x20,gt
	mov	x0,N_MAX
	cmp	x20,x0
	csel	x20,x0,x20,hi
.n:	mov	x21,0				; lances
.sp:	cmp	x21,x20
	b.hs	.lances
	adr	x0,p_sleep
	adr	x1,a_sleep
	sys	SYS_SPAWN
	cmp	x0,0
	b.le	.refus
	adr	x1,pids
	str	x0,[x1,x21,lsl 3]
	add	x21,x21,1
	b	.sp
.refus:	mov	x22,x0
	adr	x0,m_refus
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.lances:
	adr	x0,m_lances
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	bl	compter
	mov	x22,x0
	mov	x24,x1
	adr	x0,m_vivants
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_pidmax
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x22,0				; arretes
.k:	cmp	x22,x21
	b.hs	.w0
	adr	x1,pids
	ldr	x0,[x1,x22,lsl 3]
	sys	SYS_KILL
	add	x22,x22,1
	b	.k
.w0:	mov	x22,0				; attendus, codes de fin (-9 : kill)
	mov	x23,0
.w:	cmp	x22,x21
	b.hs	.wd
	adr	x1,pids
	ldr	x0,[x1,x22,lsl 3]
	sys	SYS_WAIT
	cmn	x0,9
	cinc	x23,x23,eq
	add	x22,x22,1
	b	.w
.wd:	adr	x0,m_attendus
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_sur
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,p_true			; un pid neuf : aucun des anciens ?
	mov	x1,0
	sys	SYS_SPAWN
	mov	x23,x0
	adr	x0,m_neuf
	bl	puts
	mov	x0,x23
	bl	print_dec
	mov	x22,0
.r:	cmp	x22,x21
	b.hs	.jamais
	adr	x1,pids
	ldr	x0,[x1,x22,lsl 3]
	cmp	x0,x23
	b.eq	.repris
	add	x22,x22,1
	b	.r
.repris:
	adr	x0,m_repris
	bl	puts
	b	.fin
.jamais:
	adr	x0,m_jamais
	bl	puts
.fin:	mov	x0,x23
	sys	SYS_WAIT
	mov	x0,0
	sys	SYS_EXIT

; -> x0 = process vivants, x1 = plus grand pid vu (SYS_PROCLIST_FROM)
compter:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0				; vivants
	mov	x20,0				; emplacement de depart
	mov	x22,0				; pid max
.paq:	adr	x0,procs
	mov	x1,CHUNK
	mov	x2,x20
	sys	SYS_PROCLIST_FROM
	cmp	x0,0
	b.le	.out
	add	x19,x19,x0
	mov	x21,x0
	adr	x1,procs
.e:	ldr	x2,[x1]
	cmp	x2,x22
	csel	x22,x2,x22,hi
	and	x20,x2,0xFFFF
	add	x1,x1,64
	subs	x0,x0,1
	b.ne	.e
	cmp	x21,CHUNK
	b.eq	.paq
.out:	mov	x0,x19
	mov	x1,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

p_sleep		db 'SD0:/bin/sleep',0
a_sleep		db '60',0
p_true		db 'SD0:/bin/true',0
m_refus		db 'proctest : lancement refuse, code ',0
m_lances	db 'proctest : lances ',0
m_vivants	db 'proctest : process vivants ',0
m_pidmax	db ', plus grand pid ',0
m_attendus	db 'proctest : arretes et attendus (code -9) ',0
m_sur		db ' sur ',0
m_neuf		db 'proctest : pid neuf ',0
m_jamais	db ', jamais donne avant : bon',10,0
m_repris	db ', DEJA DONNE : un pid a resservi',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
pids		rd N_MAX
procs		rb CHUNK * 64
