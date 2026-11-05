; sysstat - les appels systeme des programmes (docs/OUTILS.md, d'apres sysstat.c d'Onyx) : combien
; chacun en fait, et lesquels. Un programme tourne en EL0 et entre dans le noyau par `svc` : un appel
; coute quelques centaines de nanosecondes, un programme qui en fait des dizaines de milliers par
; seconde merite un regard. Les appels de fichiers (0x80-0x8E) passent par AppKit et vfs : ils
; comptent ici en SVC_CALL. Compteurs du noyau : SYS_PROC_STATS (docs/ABI.md).
;
;   sysstat                 chaque process : pid, appels par seconde (mesures sur une seconde), en tout
;   sysstat <pid | nom>     un process : la meme ligne, puis ses 8 appels les plus frequents
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

PMAX		= 256			; process vus
PS_PLACES	= 129			; PROC_STATS : le total et 128 numeros
TOP		= 8

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x0,0				; ce noyau compte-t-il ?
	adr	x1,stats
	mov	x2,1
	sys	SYS_PROC_STATS
	tbz	x0,63,.ok
	adr	x0,m_nosys
	bl	t_err_msg
	mov	x0,1
	bl	t_fin
.ok:	mov	x28,0				; le pid ou le nom voulu (0 : tous)
	cmp	x19,2
	b.lo	.liste
	ldr	x28,[x20,8]
.liste:	bl	collect				; -> x21 = process (procs : pid, nom)
	cbnz	x28,.filtre
	mov	x22,0				; premiers totaux
.t0:	cmp	x22,x21
	b.hs	.dors
	mov	x0,x22
	bl	total
	adr	x1,t0
	str	x0,[x1,x22,lsl 3]
	add	x22,x22,1
	b	.t0
.dors:	mov	x0,1000
	sys	SYS_SLEEP
	bl	header
	mov	x22,0
.t1:	cmp	x22,x21
	b.hs	.fin
	mov	x0,x22
	bl	total
	tbnz	x0,63,.tn			; (parti entre-temps)
	adr	x1,t0
	ldr	x1,[x1,x22,lsl 3]
	mov	x2,x0
	sub	x1,x0,x1
	mov	x0,x22
	bl	line
.tn:	add	x22,x22,1
	b	.t1
.fin:	mov	x0,0
	bl	t_fin

.filtre:					; un seul : par pid, sinon par nom
	mov	x0,x28
	bl	num_pid
	mov	x23,x0
	mov	x22,0
.f:	cmp	x22,x21
	b.hs	.aucun
	adr	x0,procs
	lsl	x1,x22,6
	add	x24,x0,x1
	cbz	x23,.fn
	ldr	x0,[x24]
	cmp	x0,x23
	b.eq	.trouve
	b	.fs
.fn:	add	x0,x24,8
	mov	x1,x28
	bl	str_eq
	cbnz	x0,.trouve
.fs:	add	x22,x22,1
	b	.f
.aucun:	ldr	x0,[x20]
	bl	t_err
	adr	x0,s_t_sep
	bl	t_err
	adr	x0,m_noproc
	bl	t_derr
	mov	x0,x28
	bl	t_err
	adr	x0,s_t_nl
	bl	t_err
	mov	x0,1
	bl	t_fin
.trouve:
	mov	x0,x22
	bl	total
	mov	x25,x0
	mov	x0,1000
	sys	SYS_SLEEP
	ldr	x0,[x24]			; le tableau entier
	adr	x1,stats
	mov	x2,PS_PLACES
	sys	SYS_PROC_STATS
	tbnz	x0,63,.aucun
	mov	x26,x0				; mots ecrits
	bl	header
	adr	x0,stats
	ldr	x2,[x0]
	sub	x1,x2,x25
	mov	x0,x22
	bl	line
	adr	x0,m_top
	bl	t_dire
	adr	x0,stats
	ldr	x27,[x0]			; total, pour les %
	mov	x25,0				; rang
.top:	cmp	x25,TOP
	b.hs	.fin
	adr	x0,stats			; le plus grand qui reste
	mov	x1,1
	mov	x2,0				; meilleur indice
	mov	x3,0				; sa valeur
.m:	cmp	x1,x26
	b.hs	.mf
	ldr	x4,[x0,x1,lsl 3]
	cmp	x4,x3
	b.ls	.mn
	mov	x3,x4
	mov	x2,x1
.mn:	add	x1,x1,1
	b	.m
.mf:	cbz	x3,.fin
	str	xzr,[x0,x2,lsl 3]
	sub	x23,x2,1			; numero
	mov	x24,x3
	mov	x0,x24
	mov	x1,13
	bl	put_w
	mov	w0,' '
	bl	t_putc
	mov	w0,' '
	bl	t_putc
	mov	x0,100
	mul	x0,x24,x0
	udiv	x0,x0,x27
	mov	x1,3
	bl	put_w
	adr	x0,m_pct
	bl	t_puts
	mov	x0,x23
	bl	put_name
	bl	t_nl
	add	x25,x25,1
	b	.top

; -> x21 = process vus, procs : PMAX entrees de 64 o (+0 pid, +8 nom)
collect:
	prologue
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	mov	x21,0
	mov	x22,0				; emplacement de depart
.paquet:
	adr	x0,lot
	mov	x1,16
	mov	x2,x22
	sys	SYS_PROCLIST_FROM
	cmp	x0,0
	b.le	.out
	mov	x23,x0
	adr	x24,lot
.e:	ldr	x0,[x24]
	and	x22,x0,0xFFFF
	cmp	x21,PMAX
	b.hs	.out
	adr	x1,procs
	lsl	x2,x21,6
	add	x1,x1,x2
	str	x0,[x1]
	add	x1,x1,8
	add	x2,x24,24
	mov	x3,31
.c:	ldrb	w4,[x2],1
	strb	w4,[x1],1
	cbz	w4,.cf
	subs	x3,x3,1
	b.ne	.c
	strb	wzr,[x1]
.cf:	add	x21,x21,1
	add	x24,x24,64
	subs	x23,x23,1
	b.ne	.e
	cmp	x0,0				; (paquet plein : il y a peut-etre une suite)
	adr	x0,lot+16*64
	cmp	x24,x0
	b.eq	.paquet
.out:	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	epilogue

; x0 = indice dans procs -> x0 = appels faits (< 0 : parti)
total:
	prologue
	adr	x1,procs
	lsl	x0,x0,6
	ldr	x0,[x1,x0]
	adr	x1,one
	mov	x2,1
	sys	SYS_PROC_STATS
	tbnz	x0,63,.out
	adr	x0,one
	ldr	x0,[x0]
.out:	epilogue

header:
	adr	x0,m_head
	b	t_dire

; x0 = indice, x1 = appels pendant la seconde, x2 = en tout
line:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x3,procs
	lsl	x0,x0,6
	add	x19,x3,x0
	mov	x21,x1
	mov	x22,x2
	ldr	x0,[x19]
	mov	x1,7
	bl	put_w
	mov	x0,x21
	mov	x1,11
	bl	put_w
	mov	x0,x22
	mov	x1,14
	bl	put_w
	mov	w0,' '
	bl	t_putc
	mov	w0,' '
	bl	t_putc
	add	x0,x19,8
	bl	t_puts
	bl	t_nl
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero d'appel : son nom en minuscules (« svc_call »), ou 0x..
put_name:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,noms
.c:	ldrb	w0,[x20]			; (dh : pas forcement aligne)
	ldrb	w1,[x20,1]
	orr	w0,w0,w1,lsl 8
	movl	x1,0xFFFF
	cmp	x0,x1
	b.eq	.hex
	add	x20,x20,2
	cmp	x0,x19
	b.eq	.ecrit
.s:	ldrb	w0,[x20],1
	cbnz	w0,.s
	b	.c
.ecrit:	ldrb	w0,[x20],1
	cbz	w0,.out
	sub	w1,w0,'A'
	cmp	w1,25
	b.hi	.p
	add	w0,w0,32
.p:	bl	t_putc
	b	.ecrit
.hex:	mov	w0,'0'
	bl	t_putc
	mov	w0,'x'
	bl	t_putc
	lsr	x0,x19,4
	and	x0,x0,15
	adr	x1,s_hex
	ldrb	w0,[x1,x0]
	bl	t_putc
	and	x0,x19,15
	adr	x1,s_hex
	ldrb	w0,[x1,x0]
	bl	t_putc
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> x0 = pid s'il n'a que des chiffres, 0 sinon
num_pid:
	mov	x1,0
	ldrb	w2,[x0]
	cbz	w2,.non
.c:	ldrb	w2,[x0],1
	cbz	w2,.f
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.non
	mov	x3,10
	mul	x1,x1,x3
	add	x1,x1,x2
	b	.c
.f:	mov	x0,x1
	ret
.non:	mov	x0,0
	ret

; x0 = nombre, x1 = largeur : aligne a droite
put_w:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x1
	mov	x1,sp
	bl	fmt_dec
	mov	x1,sp
	sub	x0,x0,x1
	sub	x19,x19,x0
.b:	cmp	x19,0
	b.le	.t
	mov	w0,' '
	bl	t_putc
	sub	x19,x19,1
	b	.b
.t:	mov	x0,sp
	bl	t_puts
	add	sp,sp,32
	ldr	x19,[sp],16
	epilogue

m_nosys		db 'ce noyau ne compte pas les appels (SYS_PROC_STATS)',0
m_noproc	db 'pas de process ',0
m_head		db '    PID   APPELS/s  APPELS EN TOUT  NOM',10,0
m_top		db 10,'  les appels les plus fr',0xC3,0xA9,'quents :',10,0
m_pct		db ' %  ',0
s_hex		db '0123456789abcdef'

; les noms des appels (src/inc/syscalls.inc : un nom absent n'assemble pas)
macro sn [nom] {
 forward
	dh SYS_#nom
	db `nom,0
}
noms:
	sn SBRK,BUF_CREATE,BUF_GRANT,BUF_MAP,BUF_FREE,DMA_ALLOC,VM_MAP,VM_UNMAP,VM_PROTECT,VM_ADVISE
	sn VM_QUERY,VM_STATS,VM_PAGEINFO,BUF_INFO,EXIT,GETPID,SPAWN_IMAGE,WAIT,KILL,PROCLIST,PROC_FLAGS
	sn SLEEP,YIELD,TIME_MS,CLOCK_GET,CLOCK_SET,SPAWN_PRELOADED,PROC_RIGHTS,PROC_INFO,PROCLIST_FROM
	sn THREAD_CREATE,THREAD_EXIT,FUTEX_WAIT,FUTEX_WAKE,THREAD_CREATE_EX,THREAD_JOIN,THREAD_SELF
	sn THREAD_SET_PRIORITY,THREAD_SET_NAME,THREAD_KILL,THREAD_LIST,SIG_ACTION,SIG_SEND,SIG_RETURN
	sn SIG_MASK,SIG_ALTSTACK,SVC_REGISTER,SVC_OPEN,SVC_CALL,SVC_NEXT,SVC_REPLY,SVC_MAP_CALLER
	sn SVC_UNMAP_CALLER,SVC_CALLER_PHYS,SVC_SIGNAL,SVC_FORWARD,DEV_MAP,FW_CALL,MOD_LANCER,MOD_LIRE
	sn DEMARRAGE_PRET,BOOT_FILE,KIT_REGISTER,IMAGE_PRELOAD,IMAGE_FORGET,IRQ_BIND,IRQ_ACK,IRQ_WAIT
	sn IRQ_UNBIND,WS_REGISTER,WS_CALL,WS_NEXT,WS_REPLY,WS_POST,WS_DISPLAY,EV_WAIT,SCREEN_INFO
	sn CLIP_SET,CLIP_GET,WS_SIGNAL,WS_MODE,PRINT,KLOG,SYSINFO,GETRANDOM,LIB_OPEN,IMAGE_LIST
	sn IMAGE_CREATE,IMAGE_SEAL,MEMBARRIER,PROC_STATS,HANDLE_CLOSE,HANDLE_INFO,HANDLE_WAIT
	sn HANDLE_LIST,HANDLE_INHERIT,CHAN_CREATE,CHAN_SEND,CHAN_RECV,CHAN_SHUTDOWN,SHM_CREATE
	sn SHM_OPEN,SHM_UNLINK,SHM_CTL,SHM_MAP
	dh 0xFFFF
	align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
	align 8
one	rb 8
t0	rb PMAX*8
stats	rb PS_PLACES*8
lot	rb 16*64
procs	rb PMAX*64
