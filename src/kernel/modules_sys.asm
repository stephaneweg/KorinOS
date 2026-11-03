; ============================================================================
;  modules_sys.asm - appels des modules de demarrage (modules.asm) : init
;  (SD0:/sys/svc/init, lance par le noyau) les lit et les lance.
; ============================================================================

; --- mod_lancer(nom, arguments) -> pid : lance un module de initramfs ------
; (un service de demarrage : init, vfs, sd, fat32 ; SD0:/sys seulement)
sys_mod_lancer:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	mov	x20,x1
	bl	mat_droits
	cbz	x0,.perm
	mov	x0,x19
	mov	x1,sp
	mov	x2,48
	bl	copy_str_from_user
	tbnz	x0,63,.out
	mov	x21,0				; arguments : une page du noyau
	cbz	x20,.find
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x21,x0
	mov	x0,x20
	mov	x1,x21
	mov	x2,ARGS_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.free
.find:	mov	x0,sp
	bl	module_find
	cbz	x0,.noent
	ldp	x0,x1,[x0,ME_ADR]		; image, taille
	mov	x2,sp
	mov	x3,x21
	mov	x4,0
	mov	x5,0
	mov	x6,1				; droits de SD0:/sys
	mov	x7,0
	bl	proc_spawn_elf
	cmp	x0,0
	mov	x1,E_INVAL
	csel	x0,x1,x0,eq
	b	.free
.noent:	mov	x0,E_NOENT
.free:	cbz	x21,.out
	mov	x19,x0
	mov	x0,x21
	bl	pmm_free
	mov	x0,x19
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.perm:	mov	x0,E_PERM
.out:	add	sp,sp,64
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- mod_lire(nom, tampon, max) -> octets : contenu d'un module ----------
; (un module texte : init.txt ; SD0:/sys seulement)
sys_mod_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x1
	mov	x20,x2
	mov	x1,sp				; le nom d'abord (mat_droits touche x1)
	mov	x2,48
	bl	copy_str_from_user
	tbnz	x0,63,.out
	bl	mat_droits
	cbz	x0,.perm
	check_user x19,x20,.fault
	mov	x0,sp
	bl	module_find
	cbz	x0,.noent
	ldp	x1,x2,[x0,ME_ADR]
	cmp	x2,x20
	csel	x2,x20,x2,hi
	mov	x20,x2
	mov	x0,x19
	bl	mem_copy
	mov	x0,x20
	b	.out
.noent:	mov	x0,E_NOENT
	b	.out
.fault:	mov	x0,E_FAULT
	b	.out
.perm:	mov	x0,E_PERM
.out:	add	sp,sp,64
	ldp	x19,x20,[sp],16
	epilogue

