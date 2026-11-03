; gpuhang - bloque le GPU expres, pour essayer la remise a zero du service v3d
; (docs/V3D.md) : une liste de binning qui branche sur elle-meme ne finit jamais.
; Le job doit revenir en V3D_E_LOST apres le delai du service, et le GPU repartir.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/v3d.inc'

BYTES		= 0x8000
BCL_LEN		= 64
RCL_OFF		= 0x1000
TALLOC_OFF	= 0x2000
TALLOC_LEN	= 0x1000
TSTATE_OFF	= 0x4000
TSTATE_LEN	= 256
OP_BRANCH	= 16

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,s_v3d
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.absent
	mov	x21,x0
	movl	x0,BYTES
	mov	x1,1
	lsl	x1,x1,62			; BUF_LOW : sous 1 Go, le GPU n'a pas de MMU
	orr	x0,x0,x1
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.nobuf
	mov	x19,x0
	adr	x1,info
	sys	SYS_BUF_INFO
	tbnz	x0,63,.nobuf
	movl	x0,BUF_VA_BASE
	mov	x1,BUF_SLOT
	madd	x20,x19,x1,x0
	adr	x0,info
	ldr	x22,[x0,BI_PHYS]
	mov	w0,OP_BRANCH			; la liste : un saut sur elle-meme
	strb	w0,[x20]
	strb	w22,[x20,1]
	lsr	x0,x22,8
	strb	w0,[x20,2]
	lsr	x0,x22,16
	strb	w0,[x20,3]
	lsr	x0,x22,24
	strb	w0,[x20,4]
	dc	cvac,x20
	dsb	sy
	adr	x1,msg
	mov	x0,V3D_OP_RUN
	str	x0,[x1]
	str	x19,[x1,V3DR_BUF]
	str	xzr,[x1,V3DR_BCL]
	mov	x0,BCL_LEN
	str	x0,[x1,V3DR_BCL_LEN]
	mov	x0,RCL_OFF
	str	x0,[x1,V3DR_RCL]
	mov	x0,16
	str	x0,[x1,V3DR_RCL_LEN]
	mov	x0,TALLOC_OFF
	str	x0,[x1,V3DR_TALLOC]
	mov	x0,TALLOC_LEN
	str	x0,[x1,V3DR_TALLOC_LEN]
	mov	x0,TSTATE_OFF
	str	x0,[x1,V3DR_TSTATE]
	mov	x0,TSTATE_LEN
	str	x0,[x1,V3DR_TSTATE_LEN]
	adr	x0,m_go
	bl	puts
	mov	x0,x21
	adr	x1,msg
	sys	SYS_SVC_CALL
	mov	x22,x0
	adr	x0,m_res
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_want
	bl	puts
	mov	x0,x19
	sys	SYS_BUF_FREE
	mov	x0,0
	sys	SYS_EXIT
.absent:
	adr	x0,m_absent
	b	.fail
.nobuf:	adr	x0,m_nobuf
.fail:	bl	puts
	mov	x0,1
	sys	SYS_EXIT

segment readable writeable
kit_data

s_v3d		db 'v3d',0
m_go		db 'gpuhang : job sans fin envoye',10,0
m_res		db 'gpuhang : resultat ',0
m_want		db ' (-5 attendu), lancer gpuinfo',10,0
m_absent	db 'gpuhang : pas de service v3d',10,0
m_nobuf		db 'gpuhang : pas de tampon',10,0
		align 16
info		rb 64
		align 16
msg		rb 512
