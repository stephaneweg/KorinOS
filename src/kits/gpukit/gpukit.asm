; ============================================================================
;  kits/gpukit/gpukit.asm - GpuKit : le GPU V3D 4.2 cote application
;  -> SD0:/lib/gpukit.so (docs/V3D.md, docs/KITS.md)
;
;  Le service " v3d " possede le GPU et lance les jobs ; tout le reste est
;  ici, dans le process de l'application :
;  * un « tampon de travail » par process (tampon partage sous 1 Go, d'un
;    seul bloc) : etat permanent (attributs par defaut, shaders), listes de
;    binning, de rendu et indirecte, sommets, etat et allocation des tuiles ;
;  * les listes de commandes (recette d'Onyx, validee sur Pi 4 ; octets
;    identiques a ses paquets : tools/v3d_ref) avec les adresses physiques
;    (BUF_INFO) ; la cible et la source sont des tampons partages sous 1 Go ;
;  * le cache du processeur : listes nettoyees, cible nettoyee et invalidee
;    avant et apres le job (DC CVAC / CIVAC permis en EL0) ;
;  * le repli sur le CPU : pas de service (QEMU, no_gpu), GPU eteint apres
;    un blocage, cible hors d'un tampon partage ou trop grande.
;  Un job a la fois par process (verrou) ; le service les passe l'un apres
;  l'autre pour tout le systeme.
;  Variables par process (segment inscriptible du kit).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include '../../inc/v3d.inc'
include 'gpukit.inc'

; --- le tampon de travail (decalages) -------------------------------------------
WK_STATE	= 0			; attributs par defaut (+0), FS (+256), VS (+512), CS (+1024),
					; shaders du rendu (ST_*)
WK_TMAP		= 8*1024		; pages des morceaux de textures (512 o par morceau)
WK_TEX		= 16*1024		; textures : GK_MAX_TEXTURES x TX_SIZE
WK_RECS		= 80*1024		; gk_render : adresse du shader state record de chaque lot
WK_BCL		= 96*1024
WK_RCL		= 160*1024
WK_IND		= 176*1024
WK_VERTS	= 768*1024		; GK_MAX_VERTS x GKV_SIZE
WK_TSTATE	= WK_VERTS + GK_MAX_VERTS*GKV_SIZE	; 256 o par tuile : 32 x 32 tuiles
WK_TALLOC	= WK_TSTATE + 256*1024	; tuiles x 64 (arrondi a 4 Ko) + 2 Mo
WK_TALLOC_MAX	= ((GK_MAX_W/64)*(GK_MAX_H/64)*64 + 4095) and not 4095 + 2*1024*1024
WK_SIZE		= WK_TALLOC + WK_TALLOC_MAX

; etat du kit dans le process
ST_UNKNOWN	= 0			; service pas encore trouve (nouvel essai chaque seconde)
ST_GPU		= 1
ST_CPU		= 2			; plus de GPU (absent, eteint, pas de memoire)

LOW_LIMIT	= 0x40000000		; le V3D n'adresse que le premier Go (sans MMU)

F32_ONE		= 0x3F800000
F32_HALF	= 0x3F000000

; job (gk_job)
JB_CLEAR	= 0			; couleur 0x00RRGGBB
JB_TGT		= 8			; adresse PHYSIQUE de la cible (coin du rectangle)
JB_STRIDE	= 16
JB_W		= 24
JB_H		= 32
JB_SRC		= 40			; adresse physique de la source (0 : aucune)
JB_SSTRIDE	= 48
JB_N		= 56			; sommets (multiple de 3, 0 : aucun)
JB_NB		= 64			; gk_render : lots (0 : pas gk_render)
JB_BATCHES	= 72			;   leur adresse
JB_FLAGS	= 80			;   GK_F_* (GK_F_ALPHA : alpha efface, ecrit)
JB_SIZE		= 96

segment readable executable
include 'gpukit.abi'

; ---------------------------------------------------------------------------
;  Verrou du process (un job a la fois) ; garde tout sauf x9-x11
; ---------------------------------------------------------------------------
gk_lock:
	adr	x9,gk_lk
.try:	ldaxr	w10,[x9]
	cbnz	w10,.busy
	mov	w10,1
	stxr	w11,w10,[x9]
	cbnz	w11,.try
	ret
.busy:	clrex
	stp	x0,x8,[sp,-16]!
	mov	x8,SYS_YIELD
	svc	0
	ldp	x0,x8,[sp],16
	b	gk_lock
gk_unlock:
	adr	x9,gk_lk
	stlr	wzr,[x9]
	ret

; ---------------------------------------------------------------------------
;  Le GPU est-il la ? (sous le verrou) -> x0 = 1 GPU, 0 CPU
;  Premier appel : service ouvert (sans attendre), etat demande, tampon de
;  travail cree et rempli. Service absent : nouvel essai une seconde plus tard
;  (il peut demarrer apres l'application).
; ---------------------------------------------------------------------------
gk_ready:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,gk_state
	ldr	x0,[x19]
	cmp	x0,ST_GPU
	b.eq	.yes
	cmp	x0,ST_CPU
	b.eq	.no
	sys	SYS_TIME_MS			; ST_UNKNOWN : pas plus d'un essai par seconde
	adr	x1,gk_retry
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.no
	add	x0,x0,1000
	str	x0,[x1]
	adr	x0,s_svc
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.nosvc
	adr	x1,gk_chan
	str	x0,[x1]
	bl	gk_query			; V3D_OP_INFO
	cmp	x0,1
	b.eq	.wk
	adr	x0,gk_dry
	ldr	x0,[x0]
	cbz	x0,.cpu
	b	.wk
.nosvc:	adr	x0,gk_dry			; essais sans GPU : listes a blanc
	ldr	x0,[x0]
	cbz	x0,.no
.wk:	movl	x0,WK_SIZE or BUF_LOW		; le tampon de travail
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.cpu
	mov	x20,x0
	adr	x1,gk_binfo
	sys	SYS_BUF_INFO
	tbnz	x0,63,.cpu
	adr	x1,gk_binfo
	ldr	x2,[x1,BI_PHYS]
	ldr	x3,[x1,BI_BYTES]
	add	x3,x3,x2
	movl	x4,LOW_LIMIT
	cmp	x3,x4
	b.hi	.cpu
	movl	x0,BUF_VA_BASE
	mov	x1,BUF_SLOT
	madd	x0,x20,x1,x0
	adr	x1,gk_wk
	stp	x20,x0,[x1]			; numero, adresse
	str	x2,[x1,16]			; adresse physique
	bl	gk_load_state
	mov	x0,ST_GPU
	str	x0,[x19]
.yes:	mov	x0,1
	b	.out
.cpu:	mov	x0,ST_CPU
	str	x0,[x19]
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; V3D_OP_INFO -> x0 = 1 pret (reponse dans gk_msg)
gk_query:
	prologue
	adr	x1,gk_msg
	mov	x0,V3D_OP_INFO
	str	x0,[x1]
	adr	x0,gk_chan
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	epilogue

; etat permanent : 16 x (0, 0, 0, 1), puis les 3 shaders ; nettoye vers la RAM
gk_load_state:
	prologue
	adr	x0,gk_wk
	ldr	x0,[x0,8]			; WK_STATE = 0
	mov	x1,0
.def:	and	x2,x1,3
	cmp	x2,3
	movl	w3,F32_ONE
	csel	w3,w3,wzr,eq
	str	w3,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,64
	b.lo	.def
	add	x2,x0,256
	adr	x3,fs_code
	mov	x4,fs_end - fs_code
	bl	.copy
	add	x2,x0,512
	adr	x3,vs_code
	mov	x4,vs_end - vs_code
	bl	.copy
	add	x2,x0,1024
	adr	x3,cs_code
	mov	x4,cs_end - cs_code
	bl	.copy
	add	x2,x0,ST_VS_CLIP		; ceux de gk_render
	adr	x3,vs_clip
	mov	x4,vs_clip_end - vs_clip
	bl	.copy
	add	x2,x0,ST_CS_CLIP
	adr	x3,cs_clip
	mov	x4,cs_clip_end - cs_clip
	bl	.copy
	mov	x2,ST_FS_COLOR
	add	x2,x0,x2
	adr	x3,fs_color
	mov	x4,fs_color_end - fs_color
	bl	.copy
	mov	x2,ST_FS_TEX
	add	x2,x0,x2
	adr	x3,fs_tex
	mov	x4,fs_tex_end - fs_tex
	bl	.copy
	mov	x2,ST_FS_COLOR_AT
	add	x2,x0,x2
	adr	x3,fs_color_at
	mov	x4,fs_color_at_end - fs_color_at
	bl	.copy
	mov	x2,ST_FS_TEX_AT
	add	x2,x0,x2
	adr	x3,fs_tex_at
	mov	x4,fs_tex_at_end - fs_tex_at
	bl	.copy
	mov	x1,ST_END
	bl	dc_clean
	epilogue
.copy:	ldr	x5,[x3],8
	str	x5,[x2],8
	subs	x4,x4,8
	b.ne	.copy
	ret

; ---------------------------------------------------------------------------
;  Cache du processeur (lignes de CTR_EL0.DminLine)
; ---------------------------------------------------------------------------
; x0 = adresse, x1 = octets : nettoie vers la RAM (le GPU va lire) ; garde x0
dc_clean:
	cbz	x1,.r
	mrs	x3,ctr_el0
	ubfx	x3,x3,16,4
	mov	x4,4
	lsl	x4,x4,x3			; taille de ligne
	sub	x5,x4,1
	bic	x2,x0,x5
	add	x6,x0,x1
.l:	dc	cvac,x2
	add	x2,x2,x4
	cmp	x2,x6
	b.lo	.l
	dsb	sy
.r:	ret
; x0 = adresse, x1 = octets : nettoie et invalide (le GPU ecrit) ; garde x0
dc_flush:
	cbz	x1,.r
	mrs	x3,ctr_el0
	ubfx	x3,x3,16,4
	mov	x4,4
	lsl	x4,x4,x3
	sub	x5,x4,1
	bic	x2,x0,x5
	add	x6,x0,x1
.l:	dc	civac,x2
	add	x2,x2,x4
	cmp	x2,x6
	b.lo	.l
	dsb	sy
.r:	ret

; ---------------------------------------------------------------------------
;  Adresse physique d'une zone : x0 = adresse, x1 = octets -> x0 = adresse
;  physique (0 : pas dans un tampon partage du premier Go)
; ---------------------------------------------------------------------------
gk_phys:
	prologue
	stp	x19,x20,[sp,-16]!
	movl	x2,BUF_VA_BASE
	subs	x19,x0,x2			; decalage dans la zone des tampons
	b.lo	.no
	lsr	x20,x19,26			; numero (BUF_SLOT = 64 Mo)
	cmp	x20,128
	b.hs	.no
	and	x19,x19,BUF_SLOT-1		; decalage dans le tampon
	add	x2,x19,x1
	str	x2,[sp,-16]!
	mov	x0,x20
	adr	x1,gk_binfo
	sys	SYS_BUF_INFO
	ldr	x2,[sp],16
	tbnz	x0,63,.no
	adr	x1,gk_binfo
	ldr	x3,[x1,BI_BYTES]
	cmp	x2,x3
	b.hi	.no
	ldr	x0,[x1,BI_PHYS]
	add	x0,x0,x19
	add	x2,x0,x3
	movl	x4,LOW_LIMIT
	cmp	x2,x4
	b.hi	.no
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  API
; ---------------------------------------------------------------------------

; x0 = tampon GKI_SIZE ou 0 -> 1 GPU, 0 CPU
gk_info:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	gk_lock
	bl	gk_ready
	mov	x20,x0
	cbz	x19,.out
	str	x20,[x19,GKI_GPU]
	stp	xzr,xzr,[x19,GKI_STATE]
	stp	xzr,xzr,[x19,GKI_REV]
	stp	xzr,xzr,[x19,GKI_BUSY_US]
	adr	x0,gk_chan
	ldr	x0,[x0]
	cbz	x0,.out
	adr	x0,gk_state
	ldr	x0,[x0]
	cmp	x0,ST_UNKNOWN
	b.eq	.out
	bl	gk_query
	adr	x1,gk_msg
	ldr	x0,[x1,V3DI_STATE]
	str	x0,[x19,GKI_STATE]
	ldr	x0,[x1,V3DI_VERSION]
	str	x0,[x19,GKI_VERSION]
	ldr	x0,[x1,V3DI_REV]
	str	x0,[x19,GKI_REV]
	ldp	x2,x3,[x1,V3DI_JOBS]
	stp	x2,x3,[x19,GKI_JOBS]
	adr	x0,gk_last_us
	ldr	x0,[x0]
	str	x0,[x19,GKI_LAST_US]
.out:	bl	gk_unlock
	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = octets -> adresse d'un tampon partage sous 1 Go (0 : rien)
gk_alloc:
	prologue
	cbz	x0,.no
	movl	x1,BUF_SLOT
	cmp	x0,x1
	b.hi	.no
	movl	x1,BUF_LOW
	orr	x0,x0,x1
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.no
	movl	x1,BUF_VA_BASE
	mov	x2,BUF_SLOT
	madd	x0,x0,x2,x1
	b	.out
.no:	mov	x0,0
.out:	epilogue

; x0 = adresse de gk_alloc -> 0
gk_free:
	prologue
	movl	x1,BUF_VA_BASE
	subs	x0,x0,x1
	b.lo	.out
	lsr	x0,x0,26			; (BUF_SLOT = 64 Mo)
	sys	SYS_BUF_FREE
.out:	mov	x0,0
	epilogue

; x0 = pixels, x1 = pas, x2 = x, x3 = y, x4 = l, x5 = h, w6 = couleur
gk_fill:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	madd	x0,x3,x1,x0			; coin du rectangle
	add	x19,x0,x2,lsl 2
	mov	x20,x1
	mov	x21,x4
	mov	x22,x5
	and	x23,x6,0xFFFFFF
	cmp	x21,0
	ccmp	x22,0,4,gt
	b.le	.none
	bl	gk_lock
	bl	gk_fits
	cbz	x0,.cpu
	bl	gk_ready
	cbz	x0,.cpu
	mov	x0,x19				; cible
	bl	gk_extent
	bl	gk_phys
	cbz	x0,.cpu
	adr	x24,gk_job
	stp	x23,x0,[x24,JB_CLEAR]
	stp	x20,x21,[x24,JB_STRIDE]
	str	x22,[x24,JB_H]
	stp	xzr,xzr,[x24,JB_SRC]
	str	xzr,[x24,JB_N]
	stp	xzr,xzr,[x24,JB_NB]
	str	xzr,[x24,JB_FLAGS]
	mov	x0,x19
	mov	x1,0
	bl	gk_submit
	cbnz	x0,.done
.cpu:	mov	x0,x19				; sur le CPU
.row:	mov	x1,0
.px:	str	w23,[x0,x1,lsl 2]
	add	x1,x1,1
	cmp	x1,x21
	b.lo	.px
	add	x0,x0,x20
	subs	x22,x22,1
	b.ne	.row
	mov	x0,0
.done:	mov	x19,x0
	bl	gk_unlock
	mov	x0,x19
	b	.out
.none:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = source, x1 = pas, x2 = destination, x3 = pas, x4 = l, x5 = h
gk_copy:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x25,x0
	mov	x26,x1
	mov	x19,x2
	mov	x20,x3
	mov	x21,x4
	mov	x22,x5
	cmp	x21,0
	ccmp	x22,0,4,gt
	b.le	.none
	bl	gk_lock
	bl	gk_fits
	cbz	x0,.cpu
	bl	gk_ready
	cbz	x0,.cpu
	mov	x0,x19
	bl	gk_extent
	bl	gk_phys
	cbz	x0,.cpu
	mov	x23,x0
	mov	x0,x25				; source : etendue avec son pas
	mul	x1,x22,x26
	sub	x1,x1,x26
	add	x1,x1,x21,lsl 2
	bl	gk_phys
	cbz	x0,.cpu
	adr	x24,gk_job
	stp	xzr,x23,[x24,JB_CLEAR]
	stp	x20,x21,[x24,JB_STRIDE]
	str	x22,[x24,JB_H]
	stp	x0,x26,[x24,JB_SRC]
	str	xzr,[x24,JB_N]
	stp	xzr,xzr,[x24,JB_NB]
	str	xzr,[x24,JB_FLAGS]
	mov	x0,x19
	mov	x1,x25
	bl	gk_submit
	cbnz	x0,.done
.cpu:	mov	x0,x25				; sur le CPU (lignes de haut en bas)
	mov	x1,x19
	mov	x23,x22
.row:	mov	x2,0
.px:	ldr	w3,[x0,x2,lsl 2]
	str	w3,[x1,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,x21
	b.lo	.px
	add	x0,x0,x26
	add	x1,x1,x20
	subs	x23,x23,1
	b.ne	.row
	mov	x0,0
.done:	mov	x19,x0
	bl	gk_unlock
	mov	x0,x19
	b	.out
.none:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pixels, x1 = pas, x2 = l, x3 = h
gk_demo_triangle:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	gk_lock
	mov	x23,0
	cmp	x21,0
	ccmp	x22,0,4,gt
	b.le	.done
	bl	gk_fits
	cbz	x0,.done
	bl	gk_ready
	cbz	x0,.done
	mov	x0,x19
	bl	gk_extent
	bl	gk_phys
	cbz	x0,.done
	adr	x9,gk_job
	movl	x4,0x00101828
	stp	x4,x0,[x9,JB_CLEAR]
	stp	x20,x21,[x9,JB_STRIDE]
	str	x22,[x9,JB_H]
	stp	xzr,xzr,[x9,JB_SRC]
	mov	x4,3
	str	x4,[x9,JB_N]
	stp	xzr,xzr,[x9,JB_NB]
	str	xzr,[x9,JB_FLAGS]
	adr	x0,gk_wk			; les sommets
	ldr	x0,[x0,8]
	add	x0,x0,WK_VERTS
	adr	x4,tri_verts
	mov	x5,0
.cp:	ldr	x6,[x4,x5]
	str	x6,[x0,x5]
	add	x5,x5,8
	cmp	x5,48
	b.lo	.cp
	mov	x1,48
	bl	dc_clean
	mov	x0,x19
	mov	x1,0
	bl	gk_submit
	mov	x23,x0
.done:	bl	gk_unlock
	mov	x0,x23
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x21 = largeur, x22 = hauteur -> x0 = 1 si le GPU les accepte
gk_fits:
	mov	x0,0
	cmp	x21,GK_MAX_W
	b.hi	.r
	cmp	x22,GK_MAX_H
	cset	x0,ls
.r:	ret

; x20 = pas, x21 = largeur, x22 = hauteur -> x1 = etendue en octets ; garde x0
gk_extent:
	sub	x1,x22,1
	mul	x1,x1,x20
	add	x1,x1,x21,lsl 4-2
	ret

; ---------------------------------------------------------------------------
;  Le job de gk_job au service : x0 = cible (adresse virtuelle), x1 = source
;  (virtuelle, 0 : aucune) -> x0 = 1 fait par le GPU, 0 a faire sur le CPU
;  (sous le verrou ; GPU pret)
; ---------------------------------------------------------------------------
gk_submit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	gk_build
	adr	x21,gk_job
	ldr	x22,[x21,JB_STRIDE]		; etendue de la cible
	ldr	x1,[x21,JB_H]
	sub	x1,x1,1
	mul	x1,x1,x22
	ldr	x2,[x21,JB_W]
	add	x22,x1,x2,lsl 2
	mov	x0,x19				; cible : rien de sale ne doit
	mov	x1,x22				; retomber sur ce que le GPU ecrit
	bl	dc_flush
	cbz	x20,.nosrc
	ldr	x1,[x21,JB_H]			; source : en RAM
	sub	x1,x1,1
	ldr	x2,[x21,JB_SSTRIDE]
	mul	x1,x1,x2
	ldr	x2,[x21,JB_W]
	add	x1,x1,x2,lsl 2
	mov	x0,x20
	bl	dc_clean
.nosrc:	adr	x9,gk_wk			; listes : en RAM
	ldr	x9,[x9,8]
	adr	x10,gk_sizes
	add	x0,x9,WK_BCL
	ldr	x1,[x10]
	bl	dc_clean
	add	x0,x9,WK_RCL
	ldr	x1,[x10,8]
	bl	dc_clean
	add	x0,x9,WK_IND
	ldr	x1,[x10,16]
	bl	dc_clean
	adr	x1,gk_msg			; le message
	mov	x0,V3D_OP_RUN
	str	x0,[x1]
	adr	x9,gk_wk
	ldr	x0,[x9]
	str	x0,[x1,V3DR_BUF]
	adr	x10,gk_sizes
	movl	x0,WK_BCL
	ldr	x2,[x10]
	stp	x0,x2,[x1,V3DR_BCL]
	movl	x0,WK_RCL
	ldr	x2,[x10,8]
	stp	x0,x2,[x1,V3DR_RCL]
	ldr	x2,[x21,JB_W]			; tuiles
	add	x2,x2,63
	lsr	x2,x2,6
	ldr	x3,[x21,JB_H]
	add	x3,x3,63
	lsr	x3,x3,6
	mul	x2,x2,x3
	lsl	x3,x2,8				; etat : 256 o par tuile
	lsl	x2,x2,6				; allocation : 64 o par tuile,
	add	x2,x2,4095			; arrondi, + 2 Mo
	and	x2,x2,not 4095
	add	x2,x2,2*1024*1024
	movl	x0,WK_TALLOC
	stp	x0,x2,[x1,V3DR_TALLOC]
	movl	x0,WK_TSTATE
	stp	x0,x3,[x1,V3DR_TSTATE]
	adr	x0,gk_dry			; essais : rien au GPU
	ldr	x0,[x0]
	cbz	x0,.call
	str	xzr,[x1,V3DR_US]
	mov	x0,0
	b	.done
.call:	adr	x0,gk_chan
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
.done:	mov	x9,x0
	mov	x0,x19				; cible : relue depuis la RAM
	mov	x1,x22
	bl	dc_flush
	cbz	x9,.ok
	cmp	x9,V3D_E_INVAL			; refuse : ce job sur le CPU
	b.eq	.cpu
	adr	x0,gk_state			; GPU perdu (ou service parti) :
	mov	x1,ST_CPU			; le CPU desormais
	str	x1,[x0]
.cpu:	mov	x0,0
	b	.out
.ok:	adr	x1,gk_msg
	ldr	x0,[x1,V3DR_US]
	adr	x1,gk_last_us
	str	x0,[x1]
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Construction des listes (recette du pilote du noyau d'avant, octets
;  verifies contre les paquets d'Onyx : tools/v3d_ref) ; x27 = curseur
;  d'ecriture (adresse virtuelle) ; les adresses ecrites dans les listes sont
;  physiques (cl_addr pour celles du tampon de travail)
; ---------------------------------------------------------------------------

macro cl8 v* {
	mov	w0,v
	strb	w0,[x27],1
}

; w0 -> 2 / 4 / 3 octets (petit-boutiste) au curseur x27
cl_u16:
	strb	w0,[x27],1
	lsr	w1,w0,8
	strb	w1,[x27],1
	ret
cl_u24:
	strb	w0,[x27],1
	lsr	w1,w0,8
	strb	w1,[x27],1
	lsr	w1,w0,16
	strb	w1,[x27],1
	ret
cl_u32:
	strb	w0,[x27],1
	lsr	w1,w0,8
	strb	w1,[x27],1
	lsr	w1,w0,16
	strb	w1,[x27],1
	lsr	w1,w0,24
	strb	w1,[x27],1
	ret
; x0 = adresse virtuelle dans le tampon de travail -> son adresse physique
; (32 bits) au curseur
cl_addr:
	adr	x1,gk_wk
	ldr	x2,[x1,8]
	ldr	x1,[x1,16]
	sub	x0,x0,x2
	add	x0,x0,x1
	b	cl_u32
; x0 = adresse virtuelle dans le tampon de travail -> x0 = physique
wk_phys:
	adr	x1,gk_wk
	ldr	x2,[x1,8]
	ldr	x1,[x1,16]
	sub	x0,x0,x2
	add	x0,x0,x1
	ret
; x0 = nombre d'octets nuls
cl_zero:
	cbz	x0,.r
.z:	strb	wzr,[x27],1
	subs	x0,x0,1
	b.ne	.z
.r:	ret

; x0 = entier signe (|x0| < 2^24) -> w0 = flottant simple precision exact
int_to_f32:
	cbz	x0,.zero
	mov	x3,0
	tbz	x0,63,.pos
	mov	x3,1
	neg	x0,x0
.pos:	clz	x1,x0
	mov	x2,63
	sub	x2,x2,x1			; position du bit de poids fort
	mov	x4,23
	sub	x4,x4,x2
	lslv	x0,x0,x4			; mantisse sur 24 bits (exact : < 2^24)
	and	x0,x0,0x7FFFFF
	add	x2,x2,127
	orr	x0,x0,x2,lsl 23
	orr	x0,x0,x3,lsl 31
	ret
.zero:	mov	w0,0
	ret

; Paquet STORE_TILE_BUFFER_GENERAL (raster RGBA8, R/B echanges : memoire
; 0x00RRGGBB) ou LOAD_TILE_BUFFER_GENERAL : w0 = opcode, w2 = pas, w3 = adresse
cl_tile_io:
	str	x30,[sp,-16]!
	strb	w0,[x27],1
	strb	wzr,[x27],1			; tampon 0, raster, sans retournement
	mov	w0,0x11B0			; format RGBA8 (27 << 4) | r_b_swap (bit 12)
	bl	cl_u16
	lsl	w0,w2,4				; pas sur 20 bits a partir du bit 4
	bl	cl_u24
	mov	w0,0				; hauteur (inutile en raster)
	bl	cl_u16
	mov	w0,w3
	bl	cl_u32
	ldr	x30,[sp],16
	ret

; STORE_TILE_BUFFER_GENERAL "aucun tampon" (ecriture factice)
cl_store_none:
	str	x30,[sp,-16]!
	cl8	29
	cl8	8
	mov	x0,11
	bl	cl_zero
	ldr	x30,[sp],16
	ret

; BCL, liste indirecte et RCL de gk_job ; tailles dans gk_sizes
gk_build:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	adr	x28,gk_job
	ldr	x19,[x28,JB_W]
	ldr	x20,[x28,JB_H]
	add	x21,x19,63			; tuiles
	lsr	x21,x21,6
	add	x22,x20,63
	lsr	x22,x22,6
	lsr	x23,x19,1			; w/2 et h/2 entiers (comme Onyx)
	lsr	x24,x20,1

	; ---- liste indirecte : uniforms, shader state, attributs -------------
	adr	x0,gk_wk
	ldr	x27,[x0,8]
	add	x27,x27,WK_IND
	mov	x25,0				; adresse du shader state record
	ldr	x0,[x28,JB_NB]
	cbz	x0,.not_render
	bl	gk_ind_batches			; gk_render : un etat par lot
	b	.no_shader
.not_render:
	ldr	x0,[x28,JB_N]
	cbz	x0,.no_shader
	mov	x26,x27				; uniforms fragment = vertex
	movl	w0,F32_ONE
	bl	cl_u32
	lsl	x0,x23,8
	bl	int_to_f32
	bl	cl_u32
	lsl	x0,x24,8
	neg	x0,x0
	bl	int_to_f32
	bl	cl_u32
	movl	w0,F32_HALF
	bl	cl_u32
	movl	w0,F32_HALF
	bl	cl_u32
	mov	x9,x27				; uniforms coordonnees
	str	x9,[sp,-16]!
	movl	w0,F32_ONE
	bl	cl_u32
	lsl	x0,x23,8
	bl	int_to_f32
	bl	cl_u32
	lsl	x0,x24,8
	neg	x0,x0
	bl	int_to_f32
	bl	cl_u32
.al:	tst	x27,31				; alignement 32
	b.eq	.aligned
	strb	wzr,[x27],1
	b	.al
.aligned:
	mov	x25,x27
	cl8	0x02				; clipping
	cl8	0x10				; centre de pixel reel (w2)
	cl8	0x04				; pas de varyings implicites point/ligne
	cl8	4				; 4 varyings
	cl8	1
	cl8	1
	cl8	1
	cl8	1				; segments VPM de taille 1
	adr	x0,gk_wk
	ldr	x10,[x0,16]			; etat permanent : physique (WK_STATE = 0)
	mov	w0,w10				; valeurs d'attributs par defaut
	bl	cl_u32
	add	w0,w10,256+5			; FS : 4 voies (bit 0), pas section finale, NaN (bit 2)
	bl	cl_u32
	mov	x0,x26
	bl	cl_addr
	adr	x0,gk_wk
	ldr	x10,[x0,16]
	add	w0,w10,512			; VS : 4 voies, section finale, NaN
	orr	w0,w0,7
	bl	cl_u32
	mov	x0,x26
	bl	cl_addr
	adr	x0,gk_wk
	ldr	x10,[x0,16]
	add	w0,w10,1024			; CS
	orr	w0,w0,7
	bl	cl_u32
	ldr	x0,[sp],16
	bl	cl_addr
	adr	x0,gk_wk			; attribut position : 3 flottants
	ldr	x10,[x0,16]
	add	x10,x10,WK_VERTS
	mov	w0,w10
	bl	cl_u32
	cl8	0x0B				; vec_size 3, type float
	cl8	0x33				; 3 valeurs lues par CS et VS
	mov	w0,0
	bl	cl_u16
	mov	w0,16				; pas = taille d'un sommet
	bl	cl_u32
	movl	w0,0xFFFFFF
	bl	cl_u32
	adr	x0,gk_wk
	ldr	x10,[x0,16]
	add	x10,x10,WK_VERTS
	add	w0,w10,12			; attribut couleur : 4 octets normalises
	bl	cl_u32
	cl8	0x50
	cl8	0x40				; 4 valeurs lues par le VS seulement
	mov	w0,0
	bl	cl_u16
	mov	w0,16
	bl	cl_u32
	movl	w0,0xFFFFFF
	bl	cl_u32
.no_shader:
	mov	x26,x27				; suite de la liste indirecte

	; ---- liste de binning ----------------------------------------------------
	adr	x0,gk_wk
	ldr	x27,[x0,8]
	add	x27,x27,WK_BCL
	cl8	120				; TILE_BINNING_MODE_CFG
	mov	x0,4
	bl	cl_zero
	sub	w0,w19,1
	bl	cl_u16
	sub	w0,w20,1
	bl	cl_u16
	cl8	19				; FLUSH_VCD_CACHE
	cl8	92				; OCCLUSION_QUERY_COUNTER (0)
	mov	w0,0
	bl	cl_u32
	cl8	6				; START_TILE_BINNING
	cl8	107				; CLIP_WINDOW
	mov	w0,0
	bl	cl_u32
	mov	w0,w19
	bl	cl_u16
	mov	w0,w20
	bl	cl_u16
	ldr	x9,[x28,JB_NB]			; (gk_render : par lot)
	cbnz	x9,.no_cfg
	cl8	96				; CFG_BITS : deux faces, sens horaire,
	cl8	0x07				;   profondeur LESS + ecriture
	cl8	0x90
	cl8	0
.no_cfg:
	cl8	104				; POINT_SIZE 1.0
	movl	w0,F32_ONE
	bl	cl_u32
	cl8	105				; LINE_WIDTH 1.0
	movl	w0,F32_ONE
	bl	cl_u32
	cl8	110				; CLIPPER_XY_SCALING (1/256 px)
	lsl	x0,x23,8
	bl	int_to_f32
	bl	cl_u32
	lsl	x0,x24,8
	neg	x0,x0
	bl	int_to_f32
	bl	cl_u32
	cl8	111				; CLIPPER_Z_SCALE_AND_OFFSET (0.5, 0.5)
	movl	w0,F32_HALF
	bl	cl_u32
	movl	w0,F32_HALF
	bl	cl_u32
	cl8	109				; Z min/max (0, 1)
	mov	w0,0
	bl	cl_u32
	movl	w0,F32_ONE
	bl	cl_u32
	cl8	108				; VIEWPORT_OFFSET (centre, 14.8)
	lsl	w0,w23,8
	and	w0,w0,0x3FFFFF
	bl	cl_u32
	lsl	w0,w24,8
	and	w0,w0,0x3FFFFF
	bl	cl_u32
	cl8	87				; COLOR_WRITE_MASKS : alpha non ecrit
	ldr	x9,[x28,JB_FLAGS]		;   (sauf cible GK_F_ALPHA)
	tst	x9,GK_F_ALPHA
	mov	w0,8
	csel	w0,wzr,w0,ne
	bl	cl_u32
	cl8	86				; BLEND_CONSTANT_COLOR 0
	mov	x0,8
	bl	cl_zero
	cl8	97				; ZERO_ALL_FLAT_SHADE_FLAGS
	cl8	99				; ZERO_ALL_NON_PERSPECTIVE_FLAGS
	cl8	88				; ZERO_ALL_CENTROID_FLAGS
	cl8	74				; TRANSFORM_FEEDBACK_SPECS (0)
	cl8	0
	cl8	92				; OCCLUSION_QUERY_COUNTER (0)
	mov	w0,0
	bl	cl_u32
	cl8	91				; SAMPLE_STATE (0xF, 1.0)
	cl8	0x0F
	cl8	0
	mov	w0,0x3F80
	bl	cl_u16
	cl8	71				; VCM_CACHE_SIZE (4, 4)
	cl8	0x44
	ldr	x9,[x28,JB_NB]
	cbz	x9,.not_batches
	bl	gk_bcl_batches			; gk_render : les lots
	b	.no_draw
.not_batches:
	ldr	x9,[x28,JB_N]
	cbz	x9,.no_draw			; rien a dessiner : pas de primitive
	cl8	64				; GL_SHADER_STATE (record, 2 attributs)
	mov	x0,x25
	bl	wk_phys
	orr	w0,w0,2
	bl	cl_u32
	cl8	36				; VERTEX_ARRAY_PRIMS (triangles, n, 0)
	cl8	4
	ldr	x0,[x28,JB_N]
	bl	cl_u32
	mov	w0,0
	bl	cl_u32
.no_draw:
	cl8	4				; FLUSH
	adr	x0,gk_wk
	ldr	x0,[x0,8]
	add	x0,x0,WK_BCL
	sub	x0,x27,x0
	adr	x1,gk_sizes
	str	x0,[x1]				; taille BCL

	; ---- liste de rendu --------------------------------------------------------
	adr	x0,gk_wk
	ldr	x27,[x0,8]
	add	x27,x27,WK_RCL
	cl8	121				; TILE_RENDERING_MODE_CFG_COMMON
	cl8	0
	mov	w0,w19
	bl	cl_u16
	mov	w0,w20
	bl	cl_u16
	mov	w0,0x0100			; profondeur interne : 16 bits
	bl	cl_u16
	cl8	0
	cl8	121				; ... CLEAR_COLORS_PART1 (RGBA, R/B
	cl8	3				;     echanges, alpha 0 : cible directe)
	ldr	x0,[x28,JB_CLEAR]
	and	w1,w0,0xFF00
	and	w2,w0,0xFF
	orr	w1,w1,w2,lsl 16
	ubfx	w2,w0,16,8
	ldr	x9,[x28,JB_FLAGS]		; GK_F_ALPHA : l'alpha de clear
	tst	x9,GK_F_ALPHA
	and	w3,w0,0xFF000000
	csel	w3,w3,wzr,ne
	orr	w0,w1,w2
	orr	w0,w0,w3
	bl	cl_u32
	mov	x0,3
	bl	cl_zero
	cl8	121				; ... COLOR (RT0 : 32 bpp, type 8 bits)
	cl8	0x81
	mov	x0,7
	bl	cl_zero
	cl8	121				; ... ZS_CLEAR_VALUES (0, 1.0)
	cl8	2
	cl8	0
	movl	w0,F32_ONE
	bl	cl_u32
	cl8	0
	cl8	0
	cl8	126				; TILE_LIST_INITIAL_BLOCK_SIZE (auto)
	cl8	4
	; supertuiles : agrandir jusqu'a moins de 256 trames
	mov	x9,1				; stW
	mov	x10,1				; stH
.st:	add	x11,x21,x9
	sub	x11,x11,1
	udiv	x11,x11,x9			; fW
	add	x12,x22,x10
	sub	x12,x12,1
	udiv	x12,x12,x10			; fH
	mul	x13,x11,x12
	cmp	x13,256
	b.lo	.st_ok
	cmp	x9,x10
	cinc	x9,x9,lo
	cinc	x10,x10,hs
	b	.st
.st_ok:	stp	x9,x10,[sp,-32]!
	stp	x11,x12,[sp,16]
	cl8	123				; TILE_LIST_SET_BASE
	adr	x0,gk_wk
	ldr	x0,[x0,16]
	movl	x1,WK_TALLOC
	add	x0,x0,x1
	bl	cl_u32
	cl8	122				; SUPERTILE_CFG
	ldp	x9,x10,[sp]
	sub	w0,w9,1
	strb	w0,[x27],1
	sub	w0,w10,1
	strb	w0,[x27],1
	ldp	x11,x12,[sp,16]
	strb	w11,[x27],1
	strb	w12,[x27],1
	orr	w0,w21,w22,lsl 12
	bl	cl_u32
	mov	x25,2				; tuile (0,0) : clear puis ecriture
.dummy:	cl8	124				;   factice, deux fois (course materielle)
	mov	x0,3
	bl	cl_zero
	cl8	26				; END_OF_LOADS
	bl	cl_store_none
	cmp	x25,2
	b.ne	.dummy2
	cl8	25				; CLEAR_TILE_BUFFERS (couleur, Z)
	cl8	3
.dummy2:
	cl8	27				; END_OF_TILE_MARKER
	subs	x25,x25,1
	b.ne	.dummy
	cl8	19				; FLUSH_VCD_CACHE
	; liste generique par tuile, ecrite dans la liste indirecte
	mov	x24,x27				; (RCL de cote)
	mov	x27,x26
	mov	x23,x26				; debut de la liste generique
	cl8	125				; TILE_COORDINATES_IMPLICIT
	ldr	x3,[x28,JB_SRC]
	cbz	x3,.no_load
	mov	w0,30				; LOAD_TILE_BUFFER_GENERAL : la source
	ldr	x2,[x28,JB_SSTRIDE]
	bl	cl_tile_io
.no_load:
	cl8	26				; END_OF_LOADS
	cl8	56				; PRIM_LIST_FORMAT (triangles)
	cl8	2
	cl8	21				; BRANCH_TO_IMPLICIT_TILE_LIST (0)
	cl8	0
	mov	w0,29				; STORE_TILE_BUFFER_GENERAL : la cible
	ldr	x2,[x28,JB_STRIDE]
	ldr	x3,[x28,JB_TGT]
	bl	cl_tile_io
	cl8	25				; CLEAR_TILE_BUFFERS
	cl8	3
	cl8	27				; END_OF_TILE_MARKER
	cl8	18				; RETURN_FROM_SUB_LIST
	mov	x25,x27				; fin de la liste generique
	adr	x0,gk_wk
	ldr	x0,[x0,8]
	add	x0,x0,WK_IND
	sub	x0,x25,x0
	adr	x1,gk_sizes
	str	x0,[x1,16]			; taille de la liste indirecte
	mov	x27,x24
	cl8	20				; START_ADDRESS_OF_GENERIC_TILE_LIST
	mov	x0,x23
	bl	cl_addr
	mov	x0,x25
	bl	cl_addr
	ldp	x9,x10,[sp],32			; supertuiles a parcourir
	mov	x0,64
	mul	x11,x9,x0			; largeur d'une supertuile (px)
	mul	x12,x10,x0
	sub	x13,x20,1
	udiv	x13,x13,x12			; derniere ligne
	sub	x14,x19,1
	udiv	x14,x14,x11			; derniere colonne
	mov	x15,0				; y
.sy:	mov	x16,0				; x
.sx:	cl8	23				; SUPERTILE_COORDINATES
	strb	w16,[x27],1
	strb	w15,[x27],1
	add	x16,x16,1
	cmp	x16,x14
	b.ls	.sx
	add	x15,x15,1
	cmp	x15,x13
	b.ls	.sy
	cl8	13				; END_OF_RENDERING
	adr	x0,gk_wk
	ldr	x0,[x0,8]
	add	x0,x0,WK_RCL
	sub	x0,x27,x0
	adr	x1,gk_sizes
	str	x0,[x1,8]			; taille RCL
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include 'render.asm'
include 'draw.asm'

s_svc		db V3D_SVC_NAME,0
		align 8

; triangle de demonstration : x, y, z (NDC) + r, g, b, a
tri_verts:	dw 0x00000000, 0x3F333333, 0x3F000000	; ( 0.0,  0.7, 0.5)
		db 255, 40, 40, 255
		dw 0xBF333333, 0xBF19999A, 0x3F000000	; (-0.7, -0.6, 0.5)
		db 40, 220, 60, 255
		dw 0x3F333333, 0xBF19999A, 0x3F000000	; ( 0.7, -0.6, 0.5)
		db 40, 90, 255, 255

; Shaders QPU (V3D 4.2) compiles par Mesa depuis GLSL, repris de
; rpi4-gpu-bare-metal-examples (Random06457, licence MIT) via Onyx.
		align 8
fs_code:	; fragment : outColor = v_color
		dd 0x3D103186BB800000, 0x54003046BBC00000, 0x3D10A18605829000, 0x540030C6BBC80000
		dd 0x3D1121850582B000, 0x54003006BBD00000, 0x3D30618405828000, 0x54203086BBC40000
		dd 0x3C0021830582A000, 0x3C2031873583E185, 0x3C0031873583E103, 0x3C003186BB800000
fs_end:
vs_code:	; vertex : gl_Position = vec4(position, 1.0) ; v_color = color
		dd 0x3DE02187BC807000, 0x3DE02189BC807001, 0x3DE0218ABC807002, 0x3DE02183BC807003
		dd 0x3D823186BB800000, 0x3DE02184BC807004, 0x3C403186BB800000, 0x55E02005BCB871C5
		dd 0x3DE02186BC807006, 0x3C403181F5818000, 0x3DE02180F88370C4, 0x54403086BBB80240
		dd 0x3DE02180F8837105, 0x3DE02180F8837146, 0x54403003F5B9A280, 0x3DE02180F8837187
		dd 0x3DE02180F880F000, 0x3C00318405828000, 0x3DE02180F881F001, 0x3DE02180F8827002
		dd 0x3DE02180F8837203, 0x3C003186BB816000, 0x3C203186BB800000, 0x3C003186BB800000
		dd 0x3C003186BB800000
vs_end:
cs_code:	; coordonnees (copie du vertex shader pour le binner : position seule)
		dd 0x3DE02184BC807000, 0x3DE02185BC807001, 0x3DE02183BC807002, 0x3DE02180F8837100
		dd 0x3DE02180F8837141, 0x3C403186BB800000, 0x3DE02180F88370C2, 0x3DE02180F882F003
		dd 0x3C403186BB800000, 0x54403006BBB80100, 0x54003081F5B98140, 0x3DE02180F880F004
		dd 0x3C003183F581A000, 0x3DE02180F881F005, 0x3C003186BB816000, 0x3C203186BB800000
		dd 0x3C003186BB800000, 0x3C003186BB800000
cs_end:
; Shaders de gk_render (QPU V3D 4.2), ecrits a la main pour Onyx
; (kernel/sys/v3d_shaders.qasm, assembles par tools/qpu/qpuasm) : VS_CLIP /
; CS_CLIP (la matrice du lot, la division par w), FS_COLOR (couleur +
; couleur 2), FS_TEX (texel * couleur + couleur 2) ; _AT : test d'alpha.
vs_clip:
		dd 0x3DE02181BC807000, 0x3DE02182BC807001, 0x3DE02183BC807002, 0x3DE02184BC807003
		dd 0x3DE02185BC807004, 0x3DE02186BC807005, 0x3DE02187BC807006, 0x3DE02188BC807007
		dd 0x3DE02189BC807008, 0x3DE0218ABC807009, 0x3DE0218FBC80700A, 0x3DE02190BC80700B
		dd 0x3DE02191BC80700C, 0x3DE02192BC80700D, 0x3C403186BB800000, 0x54403006BBB80040
		dd 0x54403046BBB80080, 0x5440304005B880C0, 0x5400304005B88100, 0x3C00219405808000
		dd 0x3C403186BB800000, 0x54403006BBB80040, 0x54403046BBB80080, 0x5440304005B880C0
		dd 0x5400304005B88100, 0x3C00219505808000, 0x3C403186BB800000, 0x54403006BBB80040
		dd 0x54403046BBB80080, 0x5440304005B880C0, 0x5400304005B88100, 0x3C00219605808000
		dd 0x3C403186BB800000, 0x54403006BBB80040, 0x54403046BBB80080, 0x5440304005B880C0
		dd 0x5400304005B88100, 0x3C00219705808000, 0x3D82F186BB800000, 0x3D8334C6BBF805C0
		dd 0x3D837186BB800000, 0x3D83B186BB800000, 0x54003006BB980500, 0x54003046BB980540
		dd 0x54003086BB980580, 0x54003006BBC002C0, 0x54003040F5C58300, 0x54003081F5C99340
		dd 0x3C00318205832380, 0x3DE02180F8807000, 0x3DE02180F880F001, 0x3DE02180F8817002
		dd 0x3DE02180F8827003, 0x3DE02180F8837144, 0x3DE02180F8837185, 0x3DE02180F88371C6
		dd 0x3DE02180F8837207, 0x3DE02180F8837248, 0x3DE02180F8837289, 0x3DE02180F88373CA
		dd 0x3DE02180F883740B, 0x3DE02180F883744C, 0x3DE02180F883748D, 0x3C003186BB816000
		dd 0x3C203186BB800000, 0x3C003186BB800000, 0x3C003186BB800000
vs_clip_end:
cs_clip:
		dd 0x3DE02181BC807000, 0x3DE02182BC807001, 0x3DE02183BC807002, 0x3DE02184BC807003
		dd 0x3C403186BB800000, 0x54403006BBB80040, 0x54403046BBB80080, 0x5440304005B880C0
		dd 0x5400304005B88100, 0x3C00219405808000, 0x3C403186BB800000, 0x54403006BBB80040
		dd 0x54403046BBB80080, 0x5440304005B880C0, 0x5400304005B88100, 0x3C00219505808000
		dd 0x3C403186BB800000, 0x54403006BBB80040, 0x54403046BBB80080, 0x5440304005B880C0
		dd 0x5400304005B88100, 0x3C00219605808000, 0x3C403186BB800000, 0x54403006BBB80040
		dd 0x54403046BBB80080, 0x5440304005B880C0, 0x5400304005B88100, 0x3C00219705808000
		dd 0x3D82F186BB800000, 0x3D8334C6BBF805C0, 0x3DE02180F8837500, 0x3DE02180F8837541
		dd 0x3DE02180F8837582, 0x3DE02180F88375C3, 0x54003006BB980500, 0x54003046BB980540
		dd 0x54003006BBC002C0, 0x54003040F5C58300, 0x3C003181F5819000, 0x3DE02180F8807004
		dd 0x3DE02180F880F005, 0x3C003186BB816000, 0x3C203186BB800000, 0x3C003186BB800000
		dd 0x3C003186BB800000
cs_clip_end:
fs_color:
		dd 0x3D103186BB800000, 0x3C003186BB800000, 0x3D103186BB800000, 0x3C003186BB800000
		dd 0x3D103186BB800000, 0x54003046BBC00000, 0x3D10218305829000, 0x54003046BBC00000
		dd 0x3D10218405829000, 0x54003046BBC00000, 0x3D10218505829000, 0x54003046BBC00000
		dd 0x3D10218605829000, 0x54003046BBC00000, 0x3D10218705829000, 0x54003046BBC00000
		dd 0x3D10218805829000, 0x54003046BBC00000, 0x3D30218905829000, 0x54203046BBC00000
		dd 0x3C00218A05829000, 0x3C0031800583E0C7, 0x3C0031810583E108, 0x3C0031820583E149
		dd 0x3C0031830583E18A, 0x3DE0318085838028, 0x3DE0318185839028, 0x3DE031828583A028
		dd 0x3DE031838583B028, 0x3C20318735808000, 0x3C0031873581A000, 0x3C003186BB800000
fs_color_end:
fs_color_at:
		dd 0x3D103186BB800000, 0x3D82F186BB800000, 0x3D103186BB800000, 0x3C003186BB800000
		dd 0x3D103186BB800000, 0x54003046BBC00000, 0x3D10218305829000, 0x54003046BBC00000
		dd 0x3D10218405829000, 0x54003046BBC00000, 0x3D10218505829000, 0x54003046BBC00000
		dd 0x3D10218605829000, 0x54003046BBC00000, 0x3D10218705829000, 0x54003046BBC00000
		dd 0x3D10218805829000, 0x54003046BBC00000, 0x3D30218905829000, 0x54203046BBC00000
		dd 0x3C00218A05829000, 0x3C0031800583E0C7, 0x3C0031810583E108, 0x3C0031820583E149
		dd 0x3C0031830583E18A, 0x3DE0318085838028, 0x3DE0318185839028, 0x3DE031828583A028
		dd 0x3DE031838583B028, 0x3C00B186458332C0, 0x3DE83186BA837000, 0x3C20318735808000
		dd 0x3C0031873581A000, 0x3C003186BB800000
fs_color_at_end:
fs_tex:
		dd 0x3D103186BB800000, 0x54003046BBC00000, 0x3D10318105829000, 0x54003086BBC00000
		dd 0x3D1031820582A000, 0x3C003886BBE80000, 0x564030C6BBC00000, 0x3E9021830582B000
		dd 0x3C003846BBE40000, 0x540030C6BBC00000, 0x3D1021840582B000, 0x540030C6BBC00000
		dd 0x3D1021850582B000, 0x540030C6BBC00000, 0x3D1021860582B000, 0x540030C6BBC00000
		dd 0x3D1021870582B000, 0x540030C6BBC00000, 0x3D1021880582B000, 0x540030C6BBC00000
		dd 0x3D1021890582B000, 0x542030C6BBC00000, 0x3C20218A0582B000, 0x3C003186BB800000
		dd 0x3C903186BB800000, 0x6490B046BBC000C0, 0x7400300105E311C4, 0x640030C005EB0205
		dd 0x7400308305EB3246, 0x3C00318205832280, 0x3DE0318185839028, 0x3DE0318085838028
		dd 0x3DE031838583B028, 0x3DE031828583A028, 0x3C20318735801000, 0x3C00318735813000
		dd 0x3C003186BB800000
fs_tex_end:
fs_tex_at:
		dd 0x3D103186BB800000, 0x54003046BBC00000, 0x3D10318105829000, 0x54003086BBC00000
		dd 0x3D1031820582A000, 0x3C003886BBE80000, 0x564030C6BBC00000, 0x3E9021830582B000
		dd 0x3C003846BBE40000, 0x5582F0C6BBC00000, 0x3D1021840582B000, 0x540030C6BBC00000
		dd 0x3D1021850582B000, 0x540030C6BBC00000, 0x3D1021860582B000, 0x540030C6BBC00000
		dd 0x3D1021870582B000, 0x540030C6BBC00000, 0x3D1021880582B000, 0x540030C6BBC00000
		dd 0x3D1021890582B000, 0x542030C6BBC00000, 0x3C20218A0582B000, 0x3C003186BB800000
		dd 0x3C903186BB800000, 0x6490B046BBC000C0, 0x7400300105E311C4, 0x640030C005EB0205
		dd 0x7400308305EB3246, 0x3C00318205832280, 0x3DE0318185839028, 0x3DE0318085838028
		dd 0x3DE031838583B028, 0x3DE031828583A028, 0x3C00B186458322C0, 0x3DE83186BA837000
		dd 0x3C20318735801000, 0x3C00318735813000, 0x3C003186BB800000
fs_tex_at_end:

segment readable writeable
		align 8
gk_lk		dw 0, 0			; verrou du process
gk_state	dd 0			; ST_*
gk_retry	dd 0			; prochain essai d'ouverture du service (ms)
gk_chan		dd 0			; canal du service v3d
gk_wk		dd 0, 0, 0		; tampon de travail : numero, adresse, adresse physique
gk_last_us	dd 0
gk_sizes	dd 0, 0, 0		; BCL, RCL, liste indirecte
gk_dry		dd 0			; GK_DBG_DRY : listes a blanc
gk_stage	dd 0, 0, 0		; gk_render, cible hors d'atteinte : tampon, octets, physique
gd_lk		dw 0, 0			; gk_draw : verrou du process
gd_verts	dd 0			; ses sommets decoupes
gd_bat		dd 0			; ses lots
gd_zb		dd 0, 0			; tampon de profondeur du CPU, octets
gd_tex		dd 0			; fiche de texture du lot en cours
gk_chunks	rb GK_CHUNKS*CK_SIZE	; morceaux de textures
gk_binfo	rb BI_SIZE
gk_job		rb JB_SIZE
gk_msg		rb 512
