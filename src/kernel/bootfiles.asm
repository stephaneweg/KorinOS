; ============================================================================
;  bootfiles.asm - les quelques fichiers dont le noyau a encore besoin
;  (docs/FICHIERS.md, etape 5) : il n'en lit plus aucun lui-meme.
;
;  init (initramfs, droits de SD0:/sys) les lit par AppKit et vfs avant de
;  lever le verrou du demarrage, et les donne au noyau par l'appel 100
;  boot_file : aujourd'hui SD0:/etc/options.txt seulement (les fichiers du
;  Wi-Fi sont partis avec le Wi-Fi du noyau : le service wifi les lit
;  lui-meme, docs/RESEAU.md etape 6). Le mecanisme reste (decision de
;  l'utilisateur) : un futur besoin = une ligne dans bf_noms. Le noyau les
;  prend une fois (boot_file_take : opts_load), puis les pages sont a lui.
; ============================================================================

BF_N		= 1			; fichiers attendus (bf_noms)
BF_REEL		= BF_N			; a partir de ce rang : seulement sur le vrai Pi
BF_TAILLE_MAX	= 4*1024*1024
BF_NOM_MAX	= 64

; x0 = nom (noyau) -> x0 = rang dans bf_noms, -1 s'il n'est pas attendu
bf_rang:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,bf_noms
	mov	x21,0
.n:	cmp	x21,BF_N
	b.hs	.non
	mov	x0,x19
	mov	x1,x20
	bl	str_equal
	cbnz	x0,.oui
.s:	ldrb	w0,[x20],1			; nom suivant
	cbnz	w0,.s
	add	x21,x21,1
	b	.n
.oui:	mov	x0,x21
	b	.out
.non:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- boot_file(nom, image, taille) -> 0 ; nom = 0 : la liste des
; fichiers encore attendus (noms termines par 0, puis un 0) ecrite dans
; image (taille au plus) -> octets. Donner un fichier : droits de SD0:/sys
; (init), nom de la liste, taille 1..4 Mo ; une seconde fois : remplace.
sys_boot_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,BF_NOM_MAX
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x19,.liste
	current_proc
	ldr	x0,[x1,P_DROITS]
	cbz	x0,.perm
	mov	x0,x19
	mov	x1,sp
	mov	x2,BF_NOM_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.out
	mov	x0,sp
	bl	bf_rang
	tbnz	x0,63,.inval
	mov	x22,x0
	cbz	x21,.inval
	movl	x0,BF_TAILLE_MAX
	cmp	x21,x0
	b.hi	.inval
	check_user x20,x21,.fault
	add	x0,x21,0xFFF			; copie dans des pages du noyau
	lsr	x23,x0,12
	mov	x0,x23
	bl	pmm_alloc_n
	cbz	x0,.nomem
	mov	x24,x0
	mov	x1,x20
	mov	x2,x21
	bl	mem_copy
	adr	x0,bf_verrou
	bl	mutex_lock
	adr	x1,bf_pages			; (deja donne : l'ancien est rendu)
	ldr	x0,[x1,x22,lsl 3]
	str	x24,[x1,x22,lsl 3]
	adr	x1,bf_tailles
	ldr	x24,[x1,x22,lsl 3]
	str	x21,[x1,x22,lsl 3]
	cbz	x0,.libre
	add	x1,x24,0xFFF
	lsr	x1,x1,12
	bl	pmm_free_n
.libre:	adr	x0,bf_verrou
	bl	mutex_unlock
	mov	x0,0
	b	.out
.liste:	mov	x22,0				; ecrits
	bl	board_is_emulated		; (QEMU : rien a partir de BF_REEL)
	mov	x23,BF_N
	cbz	x0,.l0
	mov	x23,BF_REEL
.l0:	adr	x24,bf_noms
	mov	x19,0				; rang
.l:	cmp	x19,x23
	b.hs	.lfin
	adr	x0,bf_pages
	ldr	x0,[x0,x19,lsl 3]
	cbnz	x0,.lsaut			; deja donne
	mov	x0,0				; longueur + 0
.ln:	ldrb	w1,[x24,x0]
	add	x0,x0,1
	cbnz	w1,.ln
	add	x1,x22,x0
	add	x1,x1,1				; (place du 0 final)
	cmp	x1,x21
	b.hi	.inval
	mov	x2,x0
	add	x0,x20,x22
	check_user x0,x2,.fault
	mov	x1,x24
	add	x22,x22,x2
	bl	mem_copy
.lsaut:	ldrb	w0,[x24],1
	cbnz	w0,.lsaut
	add	x19,x19,1
	b	.l
.lfin:	cbz	x21,.inval
	add	x0,x20,x22
	check_user x0,1,.fault
	strb	wzr,[x0]
	add	x0,x22,1
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,BF_NOM_MAX
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom (noyau, celui de bf_noms) -> x0 = pages (0 : pas donne), x1 =
; taille ; le fichier est retire de la table : les pages sont a l'appelant
; (pmm_free_n) - remplace l'ancien kload_file
boot_file_take:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	bf_rang
	tbnz	x0,63,.non
	mov	x19,x0
	adr	x0,bf_verrou
	bl	mutex_lock
	adr	x1,bf_pages
	ldr	x20,[x1,x19,lsl 3]
	str	xzr,[x1,x19,lsl 3]
	adr	x1,bf_tailles
	ldr	x19,[x1,x19,lsl 3]
	adr	x0,bf_verrou
	bl	mutex_unlock
	mov	x0,x20
	mov	x1,x19
	cbnz	x0,.out
.non:	mov	x0,0
	mov	x1,0
.out:	ldp	x19,x20,[sp],16
	epilogue

bf_noms		db 'SD0:/etc/options.txt',0
		align 8
bf_verrou	dw 0, 0
bf_pages	rd BF_N
bf_tailles	rd BF_N
