; ============================================================================
;  gputest - essai du GPU V3D par GpuKit et le service v3d (docs/V3D.md)
;
;	gputest
;
;  Etat du GPU (gk_info), puis dans des tampons de gk_alloc (640 x 480) :
;  remplissage entier, rectangle a x impair, copie d'un motif, triangle de
;  demonstration ; chaque resultat est verifie pixel par pixel. Puis un
;  remplissage hors d'un tampon partage (le CPU doit le faire). Sans GPU
;  (QEMU, no_gpu) tout passe par le CPU et doit donner les memes pixels.
;  Puis les textures et gk_render (GPU seulement : sans GPU, GK_E_NOGPU
;  attendu) : texture 64 x 64 recopiee au texel pres, melange premultiplie
;  par-dessus la cible, rectangle remplace (gk_texture_rect), cible hors
;  d'un tampon partage (tampon d'etape), sommet hors de la bande de garde.
;  Sortie : GPU ou CPU par essai, duree du job sur le GPU,
;  « gputest : PASS / FAIL ».
;
;	gputest lists
;
;  Listes a blanc (GK_DBG_DRY, sans GPU aussi) : textures de chaque rangement
;  (hash de leurs texels) et trois images de gk_render, listes en hexadecimal
;  (lignes « V3DT ») a comparer avec tools/v3d_ref/gk_ref.cpp.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/gpukit/gpukit.inc'

W		= 640
H		= 480
PITCH		= W*4
BYTES		= PITCH*H
BG		= 0x00101828		; fond du triangle de demonstration

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/gpukit/gpukit.abi'

start:
	str	x0,[sp,-16]!
	bl	kits_open
	ldr	x0,[sp],16
	cbz	x0,.normal
	ldrb	w1,[x0]
	cmp	w1,'l'
	b.eq	lists_mode
	cmp	w1,'r'
	b.eq	render_mode
.normal:
	adr	x0,info				; etat
	bl	gk_info
	mov	x19,x0
	adr	x0,m_state
	bl	puts
	adr	x1,info
	ldr	x0,[x1,GKI_STATE]
	bl	print_dec
	adr	x0,m_rev
	bl	puts
	adr	x1,info
	ldr	x0,[x1,GKI_REV]
	bl	print_dec
	adr	x0,m_jobs
	bl	puts
	adr	x1,info
	ldr	x0,[x1,GKI_JOBS]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,m_gpu
	adr	x1,m_cpu
	cmp	x19,0
	csel	x0,x0,x1,ne
	bl	puts

	movl	x0,BYTES			; deux tampons que le GPU atteint
	bl	gk_alloc
	cbz	x0,.nomem
	mov	x20,x0
	movl	x0,BYTES
	bl	gk_alloc
	cbz	x0,.nomem
	mov	x21,x0

	; ---- 1. remplissage entier
	mov	x0,x20
	movl	x1,PITCH
	mov	x2,0
	mov	x3,0
	mov	x4,W
	mov	x5,H
	movl	w6,0x00336699
	bl	gk_fill
	mov	x22,x0
	mov	x0,x20				; tout a 0x336699
	movl	x1,BYTES/4
	movl	w2,0x00336699
	bl	all_equal
	adr	x1,t_fill
	mov	x2,x22
	bl	report

	; ---- 2. rectangle a x impair (101, 37, 200 x 100) : le reste intact
	mov	x0,x20
	movl	x1,PITCH
	mov	x2,101
	mov	x3,37
	mov	x4,200
	mov	x5,100
	movl	w6,0x00FF8000
	bl	gk_fill
	mov	x22,x0
	mov	x0,x20
	mov	x1,101
	mov	x2,37
	movl	w3,0x00FF8000			; coin haut gauche
	bl	px_is
	mov	x23,x0
	mov	x0,x20
	mov	x1,300
	mov	x2,136
	movl	w3,0x00FF8000			; coin bas droit
	bl	px_is
	and	x23,x23,x0
	mov	x0,x20
	mov	x1,100
	mov	x2,37
	movl	w3,0x00336699			; juste a gauche : intact
	bl	px_is
	and	x23,x23,x0
	mov	x0,x20
	mov	x1,301
	mov	x2,137
	movl	w3,0x00336699			; juste apres : intact
	bl	px_is
	and	x0,x23,x0
	adr	x1,t_rect
	mov	x2,x22
	bl	report

	; ---- 3. copie d'un motif (300 x 200, de (0,0) vers x21 en (0,0))
	mov	x0,x20				; motif : (x * 7 + y * 131) & 0xFFFFFF
	mov	x2,0
.my:	mov	x1,0
.mx:	mov	x3,7
	mul	x3,x1,x3
	mov	x4,131
	madd	x3,x2,x4,x3
	and	w3,w3,0xFFFFFF
	str	w3,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,W
	b.lo	.mx
	add	x0,x0,PITCH
	add	x2,x2,1
	cmp	x2,H
	b.lo	.my
	mov	x0,x20
	movl	x1,PITCH
	mov	x2,x21
	movl	x3,PITCH
	mov	x4,300
	mov	x5,200
	bl	gk_copy
	mov	x22,x0
	mov	x23,1				; comparaison
	mov	x2,0
.cy:	mov	x1,0
.cx:	movl	x5,PITCH
	mul	x6,x2,x5
	add	x6,x6,x1,lsl 2
	ldr	w3,[x20,x6]
	ldr	w4,[x21,x6]
	and	w4,w4,0xFFFFFF
	cmp	w3,w4
	csel	x23,xzr,x23,ne
	add	x1,x1,1
	cmp	x1,300
	b.lo	.cx
	add	x2,x2,1
	cmp	x2,200
	b.lo	.cy
	mov	x0,x23
	adr	x1,t_copy
	mov	x2,x22
	bl	report

	; ---- 4. triangle de demonstration (GPU seulement)
	mov	x0,x21
	movl	x1,PITCH
	mov	x2,W
	mov	x3,H
	bl	gk_demo_triangle
	mov	x22,x0
	cbz	x22,.notri
	mov	x0,x21				; un coin : le fond
	mov	x1,2
	mov	x2,2
	movl	w3,BG
	bl	px_is
	mov	x23,x0
	mov	x0,x21				; le centre : dans le triangle
	mov	x1,W/2
	mov	x2,H/2
	movl	w3,BG
	bl	px_is
	eor	x0,x0,1
	and	x0,x23,x0
	adr	x1,t_tri
	mov	x2,x22
	bl	report
	b	.cpu
.notri:	adr	x0,m_notri
	bl	puts

	; ---- 5. hors d'un tampon partage : le CPU
.cpu:	adr	x0,plain
	mov	x1,64*4
	mov	x2,0
	mov	x3,0
	mov	x4,64
	mov	x5,64
	movl	w6,0x00ABCDEF
	bl	gk_fill
	mov	x22,x0
	adr	x0,plain
	mov	x1,64*64
	movl	w2,0x00ABCDEF
	bl	all_equal
	cmp	x22,0				; et c'est bien le CPU
	csel	x0,x0,xzr,eq
	adr	x1,t_plain
	mov	x2,x22
	bl	report

	bl	tex_tests

	mov	x0,x20
	bl	gk_free
	mov	x0,x21
	bl	gk_free
	adr	x0,m_end
	bl	puts
	adr	x0,fails
	ldr	x0,[x0]
	adr	x1,m_pass
	adr	x2,m_fail
	cmp	x0,0
	csel	x0,x1,x2,eq
	bl	puts
	adr	x0,fails
	ldr	x0,[x0]
	cmp	x0,0
	cset	x0,ne
	sys	SYS_EXIT
.nomem:	adr	x0,m_nomem
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = pixels, x1 = nombre, w2 = valeur -> x0 = 1 si tous valent w2
; (octet haut ignore)
all_equal:
	mov	x3,0
.l:	ldr	w4,[x0,x3,lsl 2]
	and	w4,w4,0xFFFFFF
	cmp	w4,w2
	b.ne	.no
	add	x3,x3,1
	cmp	x3,x1
	b.lo	.l
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = pixels (pas PITCH), x1 = x, x2 = y, w3 = valeur -> x0 = 1 si egal
px_is:
	movl	x4,PITCH
	mul	x2,x2,x4
	add	x2,x2,x1,lsl 2
	ldr	w4,[x0,x2]
	and	w4,w4,0xFFFFFF
	cmp	w4,w3
	cset	x0,eq
	ret

; x0 = 1 reussi, x1 = nom de l'essai, x2 = 1 fait par le GPU
report:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,m_ok
	adr	x1,m_ko
	cmp	x19,0
	csel	x0,x0,x1,ne
	bl	puts
	mov	x0,x20
	bl	puts
	adr	x0,m_by_gpu
	adr	x1,m_by_cpu
	cmp	x21,0
	csel	x0,x0,x1,ne
	bl	puts
	cbz	x21,.nl
	adr	x0,info				; duree du job
	bl	gk_info
	adr	x1,info
	ldr	x0,[x1,GKI_LAST_US]
	bl	print_dec
	adr	x0,m_us
	bl	puts
.nl:	adr	x0,m_nl
	bl	puts
	cbnz	x19,.out
	adr	x0,fails
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include 'textest.asm'

m_state		db 'gputest : service v3d : ',0
m_rev		db ' (1 pr',0xC3,0xAA,'t, 0 absent, -1 ',0xC3,0xA9,'teint), r',0xC3,0xA9,'vision ',0
m_jobs		db ', jobs ',0
m_gpu		db 'gputest : rendu par le GPU',10,0
m_cpu		db 'gputest : rendu par le CPU (pas de GPU)',10,0
m_ok		db 'gputest : PASS ',0
m_ko		db 'gputest : FAIL ',0
t_fill		db 'remplissage 640 x 480',0
t_rect		db 'rectangle 200 x 100 en x = 101',0
t_copy		db 'copie 300 x 200',0
t_tri		db 'triangle de d',0xC3,0xA9,'monstration',0
t_plain		db 'remplissage hors tampon partag',0xC3,0xA9,' (CPU)',0
m_by_gpu	db ' (GPU, ',0
m_by_cpu	db ' (CPU)',0
m_us		db ' us)',0
m_notri		db 'gputest : triangle : pas de GPU',10,0
m_nomem		db 'gputest : gk_alloc : pas de m',0xC3,0xA9,'moire',10,0
m_end		db 'gputest : ',0
m_pass		db 'PASS',10,0
m_fail		db 'FAIL',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
		align 8
fails		dd 0
info		rb GKI_SIZE
plain		rb 64*64*4
frame		rb GKF_SIZE
batches		rb 8*GKB_SIZE
dbg_out		rb GKD_SIZE
hexbuf		rb 600
blue8		rb 64*4
pattern		rb 128*300*4
