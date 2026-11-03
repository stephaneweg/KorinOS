; tee - copie l'entree vers la sortie et des fichiers : tee [-a] fichiers (docs/OUTILS.md)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,1
	mov	x22,O_WRITE or O_CREATE or O_TRUNC
	ldr	x0,[x20,8]
	cbz	x0,.ouv
	bl	t_est_opt
	cbz	x0,.ouv
	mov	x21,2
	mov	x22,O_WRITE or O_CREATE or O_APPEND
.ouv:	mov	x23,0				; descripteurs
.o:	cmp	x21,x19
	b.hs	.c
	cmp	x23,16
	b.hs	.c
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x24
	mov	x1,x22
	sys	SYS_OPEN
	tbz	x0,63,.ok
	mov	x0,x24
	bl	t_err_fic
	b	.o
.ok:	adr	x1,fds
	str	x0,[x1,x23,lsl 3]
	add	x23,x23,1
	b	.o
.c:	mov	x0,0
	adr	x1,t_lu
	mov	x2,T_LIRE
	sys	SYS_READ
	cmp	x0,0
	b.le	.fin
	mov	x24,x0
	mov	x2,x24
	mov	x0,1
	adr	x1,t_lu
	sys	SYS_WRITE
	mov	x25,0
.w:	cmp	x25,x23
	b.hs	.c
	adr	x1,fds
	ldr	x0,[x1,x25,lsl 3]
	adr	x1,t_lu
	mov	x2,x24
	sys	SYS_WRITE
	add	x25,x25,1
	b	.w
.fin:	mov	x25,0
.z:	cmp	x25,x23
	b.hs	.q
	adr	x1,fds
	ldr	x0,[x1,x25,lsl 3]
	sys	SYS_CLOSE
	add	x25,x25,1
	b	.z
.q:	mov	x0,0
	bl	t_fin

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
fds	rb 16*8
