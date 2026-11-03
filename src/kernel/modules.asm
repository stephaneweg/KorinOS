; ============================================================================
;  modules.asm - les modules de demarrage (docs/NOYAU-HYBRIDE.md, section 4) :
;  des fichiers que le firmware charge avant le noyau, avec la ligne
;  « initramfs a.mod,b.mod 0x02000000 » de config.txt (QEMU : --modules du
;  pilote d'essai). Le firmware les met bout a bout a partir de MOD_BASE ;
;  chacun se decrit lui-meme (src/svc/inc/module.inc) :
;	+0  « ARMPIMOD »
;	+8  taille du fichier, en-tete compris (64 bits)
;	+16 nom (48 octets, termine par 0)
;	+64 contenu (un ELF : un service de SD0:/sys/svc)
;  modules_init les repere, les garde (pages reservees) et efface leurs
;  signatures : un redemarrage a chaud sans initramfs n'en trouve plus.
; ============================================================================

MOD_BASE	= 0x02000000
MOD_MAGIC	= 0x444F4D49504D5241	; « ARMPIMOD »
MOD_HDR		= 64
MOD_MAX		= 16
MOD_TAILLE_MAX	= 0x4000000		; 64 Mo par module au plus
MOD_JEU		= 4096			; (bourrage eventuel entre deux fichiers)
; table : contenu (8), taille du contenu (8), nom (48)
ME_ADR		= 0
ME_TAILLE	= 8
ME_NOM		= 16
ME_SIZE		= 64

; x0 = adresse -> x0 = 64 bits lus octet par octet (en-tetes non alignes)
mod_rd64:
	mov	x1,7
	mov	x2,0
.l:	ldrb	w3,[x0,x1]
	orr	x2,x3,x2,lsl 8
	subs	x1,x1,1
	b.ge	.l
	mov	x0,x2
	ret

; x0 = adresse -> x0 = 1 si un en-tete de module y commence
mod_est:
	prologue
	bl	mod_rd64
	movl	x1,MOD_MAGIC
	cmp	x0,x1
	cset	x0,eq
	epilogue

; apres pmm_init : repere les modules charges par le firmware
modules_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	movl	x19,MOD_BASE			; position
	mov	x20,0				; nombre
.next:	cmp	x20,MOD_MAX
	b.hs	.fin
	mov	x0,x19
	bl	mod_est
	cbnz	x0,.mod
	cbz	x20,.fin			; le premier : a MOD_BASE, ou aucun
	mov	x21,MOD_JEU			; les suivants : un peu plus loin ?
.cher:	add	x19,x19,1
	mov	x0,x19
	bl	mod_est
	cbnz	x0,.mod
	subs	x21,x21,1
	b.ne	.cher
	b	.fin
.mod:	add	x0,x19,8			; taille
	bl	mod_rd64
	cmp	x0,MOD_HDR
	b.ls	.fin
	movl	x1,MOD_TAILLE_MAX
	cmp	x0,x1
	b.hi	.fin
	mov	x21,x0
	adr	x0,mod_table			; l'entree
	mov	x1,ME_SIZE
	madd	x22,x20,x1,x0
	add	x0,x19,MOD_HDR
	sub	x1,x21,MOD_HDR
	stp	x0,x1,[x22,ME_ADR]
	add	x0,x22,ME_NOM			; nom (47 octets au plus)
	add	x1,x19,16
	mov	x2,47
.nom:	ldrb	w3,[x1],1
	cbz	w3,.nf
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.nom
.nf:	strb	wzr,[x0]
	strb	wzr,[x19]			; signature effacee
	add	x19,x19,x21
	add	x20,x20,1
	b	.next
.fin:	adr	x0,mod_n
	str	x20,[x0]
	cbz	x20,.out
	movl	x0,MOD_BASE			; pages gardees
	sub	x1,x19,x0
	bl	pmm_reserve
	adr	x0,msg_mod			; « modules : n »
	bl	kputs
	mov	x0,x20
	bl	kput_dec
	adr	x0,msg_mod2
	bl	kputs
	mov	x19,0
.liste:	cmp	x19,x20
	b.hs	.nl
	adr	x0,mod_table
	mov	x1,ME_SIZE
	madd	x21,x19,x1,x0
	mov	w0,' '
	bl	kputc
	add	x0,x21,ME_NOM
	bl	kputs
	mov	w0,' '
	bl	kputc
	mov	w0,'('
	bl	kputc
	ldr	x0,[x21,ME_TAILLE]
	add	x0,x0,1023
	lsr	x0,x0,10
	bl	kput_dec
	adr	x0,msg_ko
	bl	kputs
	add	x19,x19,1
	b	.liste
.nl:	mov	w0,10
	bl	kputc
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom -> x0 = entree du module (0 : absent)
module_find:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.l:	adr	x0,mod_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.none
	adr	x0,mod_table
	mov	x1,ME_SIZE
	madd	x0,x20,x1,x0
	str	x0,[sp,-16]!
	add	x0,x0,ME_NOM
	mov	x1,x19
	bl	str_equal
	mov	x1,x0
	ldr	x0,[sp],16
	cbnz	x1,.out
	add	x20,x20,1
	b	.l
.none:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

msg_mod		db 'Modules          : ',0
msg_mod2	db ' (initramfs) :',0
msg_ko		db ' Ko)',0
		align 4

; (pmm_reserve : pmm.asm)
