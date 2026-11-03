; ============================================================================
;  kits/uikit/uikit_clip.inc - le presse-papiers des programmes (clipboard.h
;  d'Onyx) : par le service clipd (src/sys/clipd, inc/clip.inc), qui garde les
;  10 dernieres copies ; ui_clip_set / ui_clip_get repassent par Elegant
;  (uk_ws_clip_*, uikit_text.inc) s'il ne tourne pas : un seul texte.
;
;  Un format est un nom : « text », « image » (l, h en 32 bits puis pixels
;  0x00RRGGBB), « files », « files-cut » (chemins separes par \n), « url »,
;  « rtf », ou celui d'une application (« x-sheet »...).
; ============================================================================

include '../../inc/clip.inc'

; x0 = message : au service clipboard -> x0 = resultat (< 0 : pas de service)
uk_cd_call:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_cs_svc
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x1,x19
	sys	SYS_SVC_CALL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = format, x1 = donnees, x2 = octets, x3 = copie a completer (0 : nouvelle),
; x4 = entete de 8 octets (0 : aucun) -> x0 = id de la copie (< 0 : refus)
uk_cd_put:
	prologue
	sub	sp,sp,WS_MSG
	mov	x5,CLIP_PUT
	str	x5,[sp,WM_OP]
	stp	x1,x2,[sp,WM_A]
	stp	x3,x4,[sp,WM_C]
	add	x1,sp,WM_DATA
	mov	x2,0
.f:	ldrb	w3,[x0,x2]
	strb	w3,[x1,x2]
	cbz	w3,.go
	add	x2,x2,1
	cmp	x2,CLIP_FMT-1
	b.lo	.f
	strb	wzr,[x1,x2]
.go:	mov	x0,sp
	bl	uk_cd_call
	add	sp,sp,WS_MSG
	epilogue

; x0 = formats (« a\0b\0\0 »), x1 = destination (0 : la taille seule), x2 =
; place, x3 = octets a sauter -> x0 = octets ecrits (< 0 : pas de service),
; x1 = octets de la representation (0 : rien ne convient), x2 = rang du format
uk_cd_get:
	prologue
	sub	sp,sp,WS_MSG
	mov	x5,CLIP_GET
	str	x5,[sp,WM_OP]
	stp	x1,x2,[sp,WM_A]
	str	x3,[sp,WM_C]
	add	x1,sp,WM_DATA
	mov	x2,0
.f:	ldrb	w3,[x0,x2]
	strb	w3,[x1,x2]
	add	x2,x2,1
	cmp	x2,400
	b.hs	.end
	cbnz	w3,.f
	ldrb	w3,[x0,x2]			; (une chaine finie : la suivante ?)
	cbnz	w3,.f
.end:	strb	wzr,[x1,x2]
	mov	x0,sp
	bl	uk_cd_call
	ldp	x1,x2,[sp,WM_A]
	add	sp,sp,WS_MSG
	epilogue

; x0 = texte, x1 = octets (-1 : jusqu'au NUL) -> x0 = 0 (< 0 : erreur)
ui_clip_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmn	x20,1
	b.ne	.len
	bl	uk_strlen
	mov	x20,x0
.len:	adr	x0,uk_cs_text
	mov	x1,x19
	mov	x2,x20
	mov	x3,0
	mov	x4,0
	bl	uk_cd_put
	mov	x1,x0
	mov	x0,0
	tbz	x1,63,.out
	mov	x0,x19
	mov	x1,x20
	bl	uk_ws_clip_set
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = taille du tampon -> x0 = octets lus (texte termine par
; NUL dans le tampon ; < 0 : erreur)
ui_clip_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,1
	b.lt	.ws
	adr	x0,uk_cs_text
	mov	x1,x19
	sub	x2,x20,1
	mov	x3,0
	bl	uk_cd_get
	tbnz	x0,63,.ws
	strb	wzr,[x19,x0]
	b	.out
.ws:	mov	x0,x19
	mov	x1,x20
	bl	uk_ws_clip_get
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = format, x1 = donnees, x2 = octets, x3 = copie a completer (0 : nouvelle)
; -> x0 = id de la copie, 0 : refusee ou pas de service. Une copie a plusieurs
; representations (« rtf » puis « text ») : l'id de la premiere dans x3.
ui_clip_put:
	prologue
	mov	x4,0
	bl	uk_cd_put
	cmp	x0,0
	csel	x0,xzr,x0,lt
	epilogue

; x0 = formats (« a\0b\0\0 », le prefere d'abord), x1 = tampon (0 : la taille
; seule), x2 = taille -> x0 = octets de la representation (0 : rien ne convient,
; ou pas de service ; le tampon en recoit au plus sa taille, sans NUL), x1 = rang
; du format. L'element du curseur, sinon la copie la plus recente qui convient.
ui_clip_take:
	prologue
	mov	x3,0
	bl	uk_cd_get
	tbnz	x0,63,.none
	mov	x0,x1
	mov	x1,x2
	b	.out
.none:	mov	x0,0
	mov	x1,0
.out:	epilogue

; x0 = pixels 0x00RRGGBB, x1 = largeur, x2 = hauteur -> x0 = 1 (0 : refusee, ou
; pas de service)
ui_clip_set_image:
	prologue
	mul	x3,x1,x2
	lsl	x3,x3,2
	orr	x4,x1,x2,lsl 32
	mov	x1,x0
	mov	x2,x3
	adr	x0,uk_cs_image
	mov	x3,0
	bl	uk_cd_put
	cmp	x0,0
	cset	x0,gt
	epilogue

; x0 = tampon (0 : la taille seule), x1 = taille -> x0 = largeur, x1 = hauteur
; (0 : pas d'image), x2 = octets des pixels (largeur * hauteur * 4)
ui_clip_get_image:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x20,x1
	str	xzr,[sp]
	adr	x0,uk_cs_image
	mov	x1,sp
	mov	x2,8
	mov	x3,0
	bl	uk_cd_get
	cmp	x0,8
	b.ne	.none
	cbz	x19,.size
	adr	x0,uk_cs_image
	mov	x1,x19
	mov	x2,x20
	mov	x3,8
	bl	uk_cd_get
.size:	ldp	w0,w1,[sp]
	umull	x2,w0,w1
	lsl	x2,x2,2
	b	.out
.none:	mov	x0,0
	mov	x1,0
	mov	x2,0
.out:	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemins (separes par \n), x1 = 1 : coupes (le collage les deplace) ->
; x0 = 1 (0 : refusee, ou pas de service)
ui_clip_set_files:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	uk_strlen
	mov	x2,x0
	adr	x0,uk_cs_files
	cbz	x20,.go
	adr	x0,uk_cs_cut
.go:	mov	x1,x19
	mov	x3,0
	mov	x4,0
	bl	uk_cd_put
	cmp	x0,0
	cset	x0,gt
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = taille -> x0 = octets des chemins (texte termine par NUL ;
; 0 : aucun), x1 = 1 s'ils sont coupes
ui_clip_get_files:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x2,x1
	sub	x2,x2,1
	adr	x0,uk_cs_flist
	mov	x1,x19
	mov	x3,0
	bl	uk_cd_get
	tbnz	x0,63,.none
	strb	wzr,[x19,x0]
	mov	x1,x2
	cbz	x0,.none
	b	.out
.none:	mov	x0,0
	mov	x1,0
.out:	ldr	x19,[sp],16
	epilogue

; des fichiers coupes ont ete deplaces par le collage : la copie s'en va
ui_clip_done_cut:
	prologue
	sub	sp,sp,WS_MSG
	mov	x0,CLIP_DROPCUT
	str	x0,[sp,WM_OP]
	mov	x0,sp
	bl	uk_cd_call
	add	sp,sp,WS_MSG
	epilogue

uk_cs_svc	db 'clipboard',0
uk_cs_text	db 'text',0,0
uk_cs_image	db 'image',0,0
uk_cs_flist	db 'files',0			; (deux formats : « files », puis...)
uk_cs_cut	db 'files-cut',0,0		; (...« files-cut »)
uk_cs_files	db 'files',0
		align 4
