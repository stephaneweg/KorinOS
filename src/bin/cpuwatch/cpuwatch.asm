; ============================================================================
;  cpuwatch - charge des coeurs, puis chaque thread du systeme, une fois :
;    load 3 0 12 1			(% par coeur)
;    13 42 2 18250 wifi			(pid, tid, coeur, ms consommees, nom)
;  Deux passages a quelques secondes d'ecart donnent qui consomme, et ou,
;  sans occuper de coeur.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

TH_MAX		= 64
THL_SIZE	= 64

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	bl	kits_open
	adr	x0,info
	sys	SYS_SYSINFO
	adr	x0,m_load
	bl	puts
	mov	x19,0
.cpu:	adr	x1,info+40
	ldr	x0,[x1,x19,lsl 3]
	bl	field
	add	x19,x19,1
	cmp	x19,4
	b.lo	.cpu
	adr	x0,m_nl
	bl	puts
	mov	x22,0				; chaque process (SYS_PROCLIST_FROM,
.pid:	adr	x0,proc1			;  un a la fois : pas de plafond)
	mov	x1,1
	mov	x2,x22
	sys	SYS_PROCLIST_FROM
	cmp	x0,0
	b.le	.fin
	adr	x0,proc1
	ldr	x19,[x0]
	and	x22,x19,0xFFFF
	mov	x0,x19
	adr	x1,list
	mov	x2,TH_MAX
	sys	SYS_THREAD_LIST
	cmp	x0,0
	b.le	.next
	mov	x20,x0
	adr	x21,list
.th:	mov	x0,x19
	bl	field
	ldr	x0,[x21]			; tid
	bl	field
	ldr	x0,[x21,24]			; coeur
	bl	field
	ldr	x0,[x21,32]			; ms
	bl	field
	add	x0,x21,40			; nom
	bl	puts
	adr	x0,m_nl
	bl	puts
	add	x21,x21,THL_SIZE
	subs	x20,x20,1
	b.ne	.th
.next:	b	.pid
.fin:	mov	x0,0
	sys	SYS_EXIT

; x0 = valeur : affichee, suivie d'une espace
field:
	prologue
	bl	print_dec
	adr	x0,m_sp
	bl	puts
	epilogue

m_load		db 'load ',0
m_sp		db ' ',0
m_nl		db 10,0

segment readable writeable
kit_data
		align 8
info		rb 80
proc1		rb 64
list		rb TH_MAX*THL_SIZE
