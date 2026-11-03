; ============================================================================
;  memmap.asm - carte de la RAM : plages annoncees par le firmware, acces du
;  noyau a toute la RAM
;
;  Deux sources, toutes deux du firmware (rien n'est devine) :
;   * la mailbox (GET_ARM_MEMORY, ram_end) : la RAM ARM sous la memoire du
;     VideoCore, dans le 1er Go seulement ;
;   * le DTB, dont l'adresse est dans x0 a l'entree du noyau (boot_dtb) :
;     noeuds « memory » (toute la RAM : plusieurs plages sur un Pi 4 de 4 ou
;     8 Go) et reservations (/memreserve/).
;  Sous 1 Go, la mailbox fait foi (elle exclut le VideoCore ; sous QEMU, le
;  noeud memory couvre tout le 1er Go). Au-dela de 1 Go : les plages du DTB.
;  Sans DTB lisible : la taille de RAM de la revision de la carte (mailbox,
;  bits 20-22) et la disposition du BCM2711 (1 Go - 0xFC000000, puis 4 Go -
;  fin) ; sur un autre SoC, rien au-dela du 1er Go.
;
;  Le noyau voit toute la RAM en identite (meme adresse physique et virtuelle,
;  entrees L1[0..255] recopiees dans chaque process) : une page de la zone
;  haute s'adresse comme une page basse. Blocs de 1 Go quand une plage couvre
;  un Go entier, sinon blocs de 2 Mo (une table L2 de reserve). Les
;  peripheriques et la memoire du VideoCore gardent leurs attributs (mmu.asm).
; ============================================================================

MEM_LOW_LIMIT	= 0x40000000		; zone basse : sous 1 Go (DMA 30 bits, VideoCore)
MEM_BANKS_MAX	= 8
MEM_RSV_MAX	= 8
MEM_SRC_NONE	= 0
MEM_SRC_DTB	= 1
MEM_SRC_REV	= 2			; revision (pas de DTB)
MEM_SRC_REV_DTB	= 3			; revision (le DTB en annonce moins : QEMU)
FDT_MAGIC	= 0xEDFE0DD0		; 0xD00DFEED lu en petit-boutiste
PI4_PERI_LOW	= 0xFC000000		; BCM2711 : fenetre des peripheriques (cache la RAM)

; Detecte la RAM et la mappe pour le noyau (MMU active, avant pmm_init).
; Remplit mem_banks (plages au-dela de 1 Go, a 2 Mo pres), mem_high_start,
; mem_high_end, mem_high_bytes, mem_source.
memmap_detect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	memmap_from_revision		; -> mem_rev_banks (BCM2711)
	adr	x0,boot_dtb			; DTB plausible : dans la RAM basse
	ldr	x19,[x0]			; (mappee), aligne, bonne signature
	cbz	x19,.rev
	tst	x19,7
	b.ne	.rev
	adr	x0,ram_end
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.rev
	ldr	w0,[x19]
	movl	w1,FDT_MAGIC
	cmp	w0,w1
	b.ne	.rev
	mov	x0,x19
	bl	fdt_parse
	adr	x0,mem_source
	mov	x1,MEM_SRC_DTB
	str	x1,[x0]
.rev:	; la revision annonce plus de RAM au-dela de 1 Go que le DTB (DTB
	; absent, ou celui de QEMU raspi4b : un seul noeud de 960 Mo pour une
	; carte de 2 Go) : la disposition du BCM2711 pour cette taille
	adr	x0,mem_dtb_banks
	adr	x1,mem_dtb_n
	ldr	x1,[x1]
	bl	banks_high
	mov	x19,x0
	adr	x0,mem_rev_banks
	adr	x1,mem_rev_n
	ldr	x1,[x1]
	bl	banks_high
	cmp	x0,x19
	b.ls	.map
	adr	x0,mem_rev_n
	ldr	x1,[x0]
	adr	x0,mem_dtb_n
	str	x1,[x0]
	adr	x0,mem_rev_banks
	adr	x2,mem_dtb_banks
.cp:	cbz	x1,.cpd
	ldp	x3,x4,[x0],16
	stp	x3,x4,[x2],16
	sub	x1,x1,1
	b	.cp
.cpd:	adr	x0,mem_source
	ldr	x1,[x0]
	cmp	x1,MEM_SRC_DTB
	mov	x1,MEM_SRC_REV
	mov	x2,MEM_SRC_REV_DTB
	csel	x1,x2,x1,eq
	str	x1,[x0]
.map:	; plages au-dela de 1 Go -> mappage du noyau, bornes de la zone haute
	adr	x19,mem_dtb_banks
	adr	x0,mem_dtb_n
	ldr	x20,[x0]
	mov	x21,0				; banques retenues
.b:	cbz	x20,.done
	ldp	x0,x1,[x19],16
	sub	x20,x20,1
	movl	x2,MEM_LOW_LIMIT
	cmp	x0,x2
	csel	x0,x2,x0,lo			; debut >= 1 Go
	movl	x2,0x1FFFFF			; a 2 Mo pres, vers l'interieur
	add	x0,x0,x2
	and	x0,x0,not 0x1FFFFF
	and	x1,x1,not 0x1FFFFF
	cmp	x1,x0
	b.ls	.b
	cmp	x21,MEM_BANKS_MAX
	b.hs	.b
	adr	x2,mem_banks
	add	x2,x2,x21,lsl 4
	stp	x0,x1,[x2]
	add	x21,x21,1
	adr	x2,mem_high_bytes
	ldr	x3,[x2]
	sub	x4,x1,x0
	add	x3,x3,x4
	str	x3,[x2]
	adr	x2,mem_high_start		; bornes
	ldr	x3,[x2]
	cbz	x3,.first
	cmp	x0,x3
	b.hs	.endb
.first:	str	x0,[x2]
.endb:	adr	x2,mem_high_end
	ldr	x3,[x2]
	cmp	x1,x3
	b.ls	.mapb
	str	x1,[x2]
.mapb:	bl	mmu_map_normal
	b	.b
.done:	adr	x0,mem_nbanks
	str	x21,[x0]
	dsb	ish
	isb
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = plages (debut, fin), x1 = nombre -> x0 = octets au-dela de 1 Go
banks_high:
	mov	x2,0
	movl	x3,MEM_LOW_LIMIT
.b:	cbz	x1,.out
	ldp	x4,x5,[x0],16
	sub	x1,x1,1
	cmp	x4,x3
	csel	x4,x3,x4,lo
	cmp	x5,x4
	b.ls	.b
	sub	x5,x5,x4
	add	x2,x2,x5
	b	.b
.out:	mov	x0,x2
	ret

; Revision de la carte (mailbox) : taille de la RAM -> mem_rev_banks, la
; disposition du BCM2711 pour cette taille (rien pour un autre SoC)
memmap_from_revision:
	prologue
	adr	x0,rev_msg
	bl	mbox_call
	cbz	x0,.out
	adr	x0,rev_val
	ldr	w0,[x0]
	adr	x1,board_rev
	str	x0,[x1]
	tbz	w0,23,.out			; ancien format : pas de taille
	ubfx	w1,w0,12,4			; processeur : 3 = BCM2711
	cmp	w1,3
	b.ne	.out
	ubfx	w1,w0,20,3			; 256 Mo << n
	movl	x2,0x10000000
	lslv	x2,x2,x1			; taille totale
	movl	x3,MEM_LOW_LIMIT
	cmp	x2,x3
	b.ls	.out				; 1 Go ou moins : rien en haut
	adr	x4,mem_rev_banks
	movl	x5,PI4_PERI_LOW			; [1 Go, min(total, 0xFC000000)[
	cmp	x2,x5
	csel	x5,x2,x5,lo
	stp	x3,x5,[x4]
	mov	x6,1
	movl	x3,0x100000000			; [4 Go, total[
	cmp	x2,x3
	b.ls	.n
	stp	x3,x2,[x4,16]
	mov	x6,2
.n:	adr	x4,mem_rev_n
	str	x6,[x4]
.out:	epilogue

		align 16
rev_msg:	dw rev_msg_end - rev_msg, 0
		dw 0x00010002, 4, 0		; GET_BOARD_REVISION
rev_val		dw 0
		dw 0
rev_msg_end:

; x0 = debut, x1 = fin (alignes sur 2 Mo, au-dela de 1 Go) : RAM normale
; cacheable dans pt_l1 (blocs de 1 Go, sinon de 2 Mo dans une table L2 ;
; une entree deja faite - peripheriques - est gardee)
mmu_map_normal:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x21,pt_l1
.gb:	cmp	x19,x20
	b.hs	.out
	lsr	x22,x19,30			; Go courant
	cmp	x22,256				; (moitie noyau seulement)
	b.hs	.out
	lsl	x2,x22,30
	movl	x3,0x40000000
	add	x3,x2,x3			; fin du Go
	ldr	x4,[x21,x22,lsl 3]
	tbnz	x4,0,.table			; entree deja faite
	cmp	x19,x2				; Go entier : un bloc
	b.ne	.new_l2
	cmp	x20,x3
	b.lo	.new_l2
	movl	x4,BLK_NORMAL
	orr	x4,x4,x2
	str	x4,[x21,x22,lsl 3]
	mov	x19,x3
	b	.gb
.new_l2:					; table de reserve
	adr	x0,pt_l2_spare_n
	ldr	x1,[x0]
	cmp	x1,2
	b.hs	.skip
	add	x4,x1,1
	str	x4,[x0]
	adr	x0,pt_l2_spare
	add	x0,x0,x1,lsl 12
	mov	x5,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	orr	x4,x5,PTE_TABLE
	str	x4,[x21,x22,lsl 3]
	lsl	x2,x22,30
	movl	x3,0x40000000
	add	x3,x2,x3
.table:	and	x4,x4,3
	cmp	x4,PTE_TABLE			; un bloc de 1 Go : rien a faire
	b.ne	.skip
	ldr	x4,[x21,x22,lsl 3]
	movl	x5,PTE_ADDR_MASK
	and	x4,x4,x5			; table L2
	movl	x6,BLK_NORMAL
.mb:	cmp	x19,x3
	b.hs	.gb
	cmp	x19,x20
	b.hs	.out
	ubfx	x7,x19,21,9
	ldr	x8,[x4,x7,lsl 3]
	cbnz	x8,.mbn				; (peripheriques : gardes)
	orr	x8,x6,x19
	str	x8,[x4,x7,lsl 3]
.mbn:	add	x19,x19,0x200000
	b	.mb
.skip:	mov	x19,x3				; Go suivant
	b	.gb
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  DTB (Flattened Device Tree, gros-boutiste) : #address-cells / #size-cells
;  de la racine, « reg » des noeuds « memory » de premier niveau, table des
;  reservations
; ---------------------------------------------------------------------------

; x0 = DTB
fdt_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldr	w0,[x19,8]			; off_dt_struct
	rev	w0,w0
	add	x20,x19,x0
	ldr	w0,[x19,12]			; off_dt_strings
	rev	w0,w0
	add	x21,x19,x0
	ldr	w0,[x19,4]			; totalsize (borne)
	rev	w0,w0
	add	x22,x19,x0
	mov	x23,0				; profondeur
	mov	x24,0				; 1 : dans un noeud memory
	mov	x25,2				; #address-cells, #size-cells
	mov	x26,1				; (valeurs par defaut)
	; --- reservations : paires (adresse, taille) de 64 bits, fin 0, 0 ---
	ldr	w0,[x19,16]			; off_mem_rsvmap
	rev	w0,w0
	add	x0,x19,x0
	mov	x3,0
	adr	x4,mem_rsv
.rsv:	ldp	x1,x2,[x0],16
	rev	x1,x1
	rev	x2,x2
	orr	x5,x1,x2
	cbz	x5,.rsv_done
	cmp	x3,MEM_RSV_MAX
	b.hs	.rsv_done
	add	x2,x1,x2
	stp	x1,x2,[x4],16
	add	x3,x3,1
	b	.rsv
.rsv_done:
	adr	x0,mem_nrsv
	str	x3,[x0]
	; --- structure ---
.tok:	cmp	x20,x22
	b.hs	.done
	ldr	w0,[x20],4
	rev	w0,w0
	cmp	w0,1				; FDT_BEGIN_NODE
	b.eq	.begin
	cmp	w0,2				; FDT_END_NODE
	b.eq	.end
	cmp	w0,3				; FDT_PROP
	b.eq	.prop
	cmp	w0,9				; FDT_END
	b.eq	.done
	b	.tok				; FDT_NOP
.begin:	add	x23,x23,1
	cmp	x23,2				; noeud sous la racine : « memory »
	b.ne	.skipname			; ou « memory@... » ?
	mov	x24,0
	adr	x1,fdt_s_memory
	mov	x2,0
.mn:	ldrb	w3,[x1,x2]
	cbz	w3,.mend
	ldrb	w4,[x20,x2]
	cmp	w3,w4
	b.ne	.skipname
	add	x2,x2,1
	b	.mn
.mend:	ldrb	w4,[x20,x2]
	mov	w5,'@'
	cmp	w4,0
	ccmp	w4,w5,4,ne
	b.ne	.skipname
	mov	x24,1
.skipname:					; nom, 0, alignement sur 4
	ldrb	w0,[x20],1
	cbnz	w0,.skipname
	add	x20,x20,3
	and	x20,x20,not 3
	b	.tok
.end:	cmp	x23,2
	b.ne	.end2
	mov	x24,0
.end2:	sub	x23,x23,1
	b	.tok
.prop:	ldp	w0,w1,[x20],8			; longueur, decalage du nom
	rev	w0,w0
	rev	w1,w1
	mov	x2,x20				; valeur
	add	x20,x20,x0
	add	x20,x20,3
	and	x20,x20,not 3
	stp	x0,x2,[sp,-16]!
	add	x0,x21,x1			; nom
	cmp	x23,1
	b.eq	.rootprop
	cmp	x23,2
	b.ne	.pdone
	cbz	x24,.pdone
	adr	x1,fdt_s_reg
	bl	str_equal
	cbz	x0,.pdone
	ldp	x0,x2,[sp]
	bl	fdt_reg
	b	.pdone
.rootprop:
	str	x0,[sp,-16]!
	adr	x1,fdt_s_acells
	bl	str_equal
	cbz	x0,.sc
	ldr	x2,[sp,24]
	ldr	w25,[x2]
	rev	w25,w25
.sc:	ldr	x0,[sp],16
	adr	x1,fdt_s_scells
	bl	str_equal
	cbz	x0,.pdone
	ldr	x2,[sp,8]
	ldr	w26,[x2]
	rev	w26,w26
.pdone:	add	sp,sp,16
	b	.tok
.done:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (fdt_parse) x0 = longueur de « reg », x2 = valeur ; x25, x26 = cellules
; d'adresse et de taille -> plages ajoutees a mem_dtb_banks
fdt_reg:
	add	x7,x25,x26
	lsl	x7,x7,2				; octets par plage
	cbz	x7,.out
	cmp	x25,2
	b.hi	.out
	cmp	x26,2
	b.hi	.out
	adr	x8,mem_dtb_n
	ldr	x9,[x8]
	adr	x10,mem_dtb_banks
.e:	cmp	x0,x7
	b.lo	.end
	mov	x3,0				; adresse
	mov	x4,x25
.a:	cbz	x4,.adone
	ldr	w5,[x2],4
	rev	w5,w5
	orr	x3,x5,x3,lsl 32
	sub	x4,x4,1
	b	.a
.adone:	mov	x6,0				; taille
	mov	x4,x26
.s:	cbz	x4,.sdone
	ldr	w5,[x2],4
	rev	w5,w5
	orr	x6,x5,x6,lsl 32
	sub	x4,x4,1
	b	.s
.sdone:	sub	x0,x0,x7
	cbz	x6,.e
	cmp	x9,MEM_BANKS_MAX
	b.hs	.end
	add	x6,x3,x6
	add	x11,x10,x9,lsl 4
	stp	x3,x6,[x11]
	add	x9,x9,1
	b	.e
.end:	str	x9,[x8]
.out:	ret

fdt_s_memory	db 'memory',0
fdt_s_reg	db 'reg',0
fdt_s_acells	db '#address-cells',0
fdt_s_scells	db '#size-cells',0
		align 4

; Journal : « RAM vue : N Mo (source), zone basse, zone haute »
memmap_log:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,msg_mm_ram
	bl	kputs
	adr	x0,ram_end
	ldr	x19,[x0]
	adr	x0,mem_high_bytes
	ldr	x20,[x0]
	add	x0,x19,x20
	lsr	x0,x0,20
	bl	kput_dec
	adr	x0,msg_mm_mo
	bl	kputs
	adr	x0,mem_source
	ldr	x0,[x0]
	adr	x1,msg_mm_dtb
	cmp	x0,MEM_SRC_DTB
	b.eq	.src
	adr	x1,msg_mm_rev
	cmp	x0,MEM_SRC_REV
	b.eq	.src
	adr	x1,msg_mm_revdtb
	cmp	x0,MEM_SRC_REV_DTB
	b.eq	.src
	adr	x1,msg_mm_none
.src:	mov	x0,x1
	bl	kputs
	adr	x0,msg_mm_low
	bl	kputs
	lsr	x0,x19,20
	bl	kput_dec
	adr	x0,msg_mm_high
	bl	kputs
	lsr	x0,x20,20
	bl	kput_dec
	adr	x0,msg_mm_banks
	bl	kputs
	adr	x0,mem_nbanks
	ldr	x0,[x0]
	bl	kput_dec
	adr	x0,msg_mm_end
	bl	kputs
	adr	x0,msg_mm_list			; plages annoncees (DTB ou revision)
	bl	kputs
	adr	x19,mem_dtb_banks
	adr	x0,mem_dtb_n
	ldr	x20,[x0]
.l:	cbz	x20,.le
	mov	w0,' '
	bl	kputc
	ldr	x0,[x19],8
	bl	kput_hex
	mov	w0,'-'
	bl	kputc
	ldr	x0,[x19],8
	bl	kput_hex
	sub	x20,x20,1
	b	.l
.le:	adr	x0,msg_mm_rv			; revision de la carte
	bl	kputs
	adr	x0,board_rev
	ldr	x0,[x0]
	bl	kput_hex
	mov	w0,10
	bl	kputc
	ldp	x19,x20,[sp],16
	epilogue

msg_mm_ram	db 'RAM vue          : ',0
msg_mm_mo	db ' Mo (',0
msg_mm_dtb	db 'DTB',0
msg_mm_rev	db 'r',0xC3,0xA9,'vision de la carte, sans DTB',0
msg_mm_revdtb	db 'r',0xC3,0xA9,'vision de la carte : le DTB en annonce moins',0
msg_mm_none	db 'mailbox seule',0
msg_mm_low	db '), zone basse ',0
msg_mm_high	db ' Mo (< 1 Go), zone haute ',0
msg_mm_banks	db ' Mo (',0
msg_mm_end	db ' plage(s))',10,0
msg_mm_list	db 'Plages de RAM    :',0
msg_mm_rv	db ' ; r',0xC3,0xA9,'vision ',0
		align 4
