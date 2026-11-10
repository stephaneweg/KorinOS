; imgblk - peripherique bloc sur un fichier image (docs/MICRO-NOYAU.md,
; section 4) : blk.<nom> et ses partitions, en lecture et ecriture. Sert a
; essayer les services de fichiers sans toucher a la carte (« img0
; SD0:/test.img »), et plus tard a monter une image disque.
; Arguments : « <nom> <fichier> ».
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/blk.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,quit
	mov	x0,x19				; « <nom> <fichier> »
.c:	ldrb	w1,[x0]
	cbz	w1,quit
	cmp	w1,' '
	b.eq	.cut
	add	x0,x0,1
	b	.c
.cut:	strb	wzr,[x0],1
	mov	x20,x0
	adr	x1,infos			; taille de l'image
	sys	SYS_STAT
	tbnz	x0,63,quit
	mov	x0,x20
	mov	x1,O_READ or O_WRITE
	sys	SYS_OPEN
	tbnz	x0,63,quit
	adr	x1,fd
	str	x0,[x1]
	adr	x0,lire				; le pilote
	adr	x1,blk_lire_fn
	str	x0,[x1]
	adr	x0,ecrire
	adr	x1,blk_ecrire_fn
	str	x0,[x1]
	adr	x2,infos
	ldr	x2,[x2]
	lsr	x2,x2,9				; secteurs
	mov	x0,x19
	mov	x1,0				; lecture et ecriture
	b	blk_demarrer
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; x0 = secteur, x1 = nombre, x2 = adresse -> 0 ou erreur
lire:
	mov	x3,SYS_READ
	b	transfert
ecrire:
	mov	x3,SYS_WRITE
transfert:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	lsl	x1,x0,9				; position
	adr	x0,fd
	ldr	x0,[x0]
	mov	x2,0
	sys	SYS_SEEK
	tbnz	x0,63,.out
	adr	x0,fd
	ldr	x0,[x0]
	mov	x1,x20
	lsl	x2,x19,9
	mov	x8,x21				; lire ou ecrire
  if defined FICHIERS_APPKIT
   if FICHIERS_APPKIT
	bl	fichier_sys			; client d'AppKit (x30 : prologue)
   else
	svc	0
   end if
  else
	svc	0
  end if
	tbnz	x0,63,.out
	lsl	x1,x19,9
	cmp	x0,x1
	mov	x0,0
	b.eq	.out
	mov	x0,E_IO
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include '../inc/blkserv.inc'

segment readable writeable
kit_data
blkserv_data
	align 8
fd	dd 0
infos	rb 16
