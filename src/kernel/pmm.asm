; ============================================================================
;  pmm.asm - allocateur de pages physiques de 4 Ko (bitmap, 1 bit = 1 page)
;
;  Couvre toute la RAM vue (memmap.asm) : une bitmap de 0 a la fin de la plus
;  haute plage, placee juste apres le noyau. Bit a 1 = page occupee (ou hors
;  RAM : trous du VideoCore, peripheriques).
;
;  Deux zones :
;   * basse : la RAM ARM sous le VideoCore (< 1 Go, ram_end). Ce qui donne une
;     adresse physique au materiel doit y rester : dma_alloc (DMA « lite » de
;     30 bits, DMA 2D d'Elegant, son, USB, SD, Ethernet), tampons du V3D,
;     piles des coeurs secondaires (lues MMU coupee) -> pmm_alloc_low(_n) ;
;   * haute : au-dela de 1 Go (Pi 4 de 2, 4 ou 8 Go). Activee par
;     pmm_enable_high une fois SD0:/etc/options.txt lu (option « no_highmem » :
;     le 1er Go seul, pour un diagnostic).
;  pmm_alloc(_n) prend d'abord en zone haute, puis en zone basse : pages des
;  process, tables, piles du noyau, images, caches.
;
;  Chaque zone garde un indice (borne basse des pages libres) : la recherche
;  repart de la, pas du debut. Protege par un spinlock irqsave : appelable
;  depuis tout coeur et tout contexte (y compris une faute de page).
; ============================================================================

PAGE_SIZE	= 4096
MEM_LOW_PAGES	= 16384			; sous 64 Mo libres : les services rendent (mem_watch)
MEM_WAIT_PAGES	= 4096			; mem_wait : 16 Mo libres lui suffisent
MEM_WAIT_MS	= 2000
MEM_WAIT_STEP	= 5
; zone : premiere page, fin (pages), indice, pages libres
Z_FIRST		= 0
Z_END		= 8
Z_HINT		= 16
Z_FREE		= 24
Z_SIZE		= 32

; x0 = fin de la RAM ARM basse (ram_end). Bitmap apres le noyau, zone basse
; libre [fin de la bitmap, ram_end[ (moins les reservations du DTB) ; zone
; haute bornee (mem_high_start, mem_high_end) mais encore occupee.
pmm_init:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	lsr	x19,x0,12			; fin de la zone basse (page)
	adr	x0,mem_high_end			; pages couvertes par la bitmap
	ldr	x0,[x0]
	lsr	x20,x0,12
	cmp	x20,x19
	csel	x20,x19,x20,lo
	add	x20,x20,63
	and	x20,x20,not 63
	adr	x0,pmm_npages
	str	x20,[x0]
	adr	x0,kernel_end			; bitmap : apres le noyau
	add	x0,x0,0xFFF
	and	x21,x0,not 0xFFF
	adr	x0,pmm_bitmap
	str	x21,[x0]
	lsr	x22,x20,3			; octets
	mov	x0,x21				; tout occupe
	mov	x1,x22
	mov	x3,-1
.fill:	str	x3,[x0],8
	subs	x1,x1,8
	b.hi	.fill
	add	x0,x21,x22			; premiere page libre : apres la bitmap
	add	x0,x0,0xFFF
	lsr	x0,x0,12
	adr	x2,pmm_zone_low
	stp	x0,x19,[x2,Z_FIRST]
	str	x0,[x2,Z_HINT]
	sub	x3,x19,x0
	str	x3,[x2,Z_FREE]
	adr	x4,pmm_free_pages
	str	x3,[x4]
	adr	x4,pmm_total_pages
	str	x3,[x4]
	mov	x1,x19
	mov	x2,0
	bl	pmm_bits
	adr	x0,mem_high_start		; zone haute : bornes seulement
	ldp	x0,x1,[x0]			; (mem_high_start, mem_high_end)
	lsr	x0,x0,12
	lsr	x1,x1,12
	adr	x2,pmm_zone_high
	stp	x0,x1,[x2,Z_FIRST]
	str	x0,[x2,Z_HINT]
	str	xzr,[x2,Z_FREE]
	bl	pmm_apply_rsv			; reservations du DTB (zone basse)
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Rend la zone haute disponible (plages de mem_banks) -> x0 = pages ajoutees
pmm_enable_high:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,pmm_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x0,pmm_high_on
	ldr	x1,[x0]
	mov	x21,0
	cbnz	x1,.unlock			; (deja fait)
	adr	x19,mem_banks
	adr	x0,mem_nbanks
	ldr	x20,[x0]
.b:	cbz	x20,.banks_done
	ldp	x0,x1,[x19],16
	lsr	x0,x0,12
	lsr	x1,x1,12
	sub	x2,x1,x0
	add	x21,x21,x2
	mov	x2,0
	bl	pmm_bits
	sub	x20,x20,1
	b	.b
.banks_done:
	adr	x0,pmm_zone_high
	str	x21,[x0,Z_FREE]
	adr	x0,pmm_free_pages
	ldr	x1,[x0]
	add	x1,x1,x21
	str	x1,[x0]
	adr	x0,pmm_total_pages
	ldr	x1,[x0]
	add	x1,x1,x21
	str	x1,[x0]
	bl	pmm_apply_rsv			; (une reservation au-dela de 1 Go)
	adr	x0,pmm_high_on
	mov	x1,1
	str	x1,[x0]
.unlock:
	adr	x0,pmm_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Reservations du DTB (/memreserve/) : pages gardees occupees
pmm_apply_rsv:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,mem_rsv
	adr	x0,mem_nrsv
	ldr	x20,[x0]
.r:	cbz	x20,.out
	ldp	x0,x1,[x19],16
	sub	x1,x1,x0
	bl	pmm_reserve
	sub	x20,x20,1
	b	.r
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = premiere page, x1 = fin (exclue), x2 = 0 libre / 1 occupe (sans
; compter : l'appelant tient les compteurs). Mots entiers quand c'est possible.
pmm_bits:
	adr	x3,pmm_bitmap
	ldr	x3,[x3]
	neg	x8,x2				; 0 ou -1 (mot entier)
.p:	cmp	x0,x1
	b.hs	.out
	tst	x0,63
	b.ne	.bit
	sub	x4,x1,x0
	cmp	x4,64
	b.lo	.bit
	lsr	x4,x0,6
	str	x8,[x3,x4,lsl 3]
	add	x0,x0,64
	b	.p
.bit:	lsr	x4,x0,6
	and	x5,x0,63
	ldr	x6,[x3,x4,lsl 3]
	mov	x7,1
	lslv	x7,x7,x5
	bic	x6,x6,x7
	cbz	x2,.st
	orr	x6,x6,x7
.st:	str	x6,[x3,x4,lsl 3]
	add	x0,x0,1
	b	.p
.out:	ret

; x0 = adresse, x1 = taille : ces pages restent occupees (modules
; d'initramfs, reservations du DTB ; au demarrage, un seul coeur)
pmm_reserve:
	add	x1,x0,x1
	add	x1,x1,4095
	lsr	x1,x1,12			; fin (page)
	lsr	x0,x0,12
	adr	x3,pmm_npages			; (hors bitmap : rien)
	ldr	x3,[x3]
	cmp	x1,x3
	csel	x1,x3,x1,hi
	adr	x3,pmm_bitmap
	ldr	x3,[x3]
.p:	cmp	x0,x1
	b.hs	.out
	lsr	x4,x0,6
	and	x5,x0,63
	ldr	x2,[x3,x4,lsl 3]
	mov	x7,1
	lslv	x7,x7,x5
	tst	x2,x7
	b.ne	.n				; (deja occupee)
	orr	x2,x2,x7
	str	x2,[x3,x4,lsl 3]
	adr	x6,pmm_free_pages
	ldr	x8,[x6]
	sub	x8,x8,1
	str	x8,[x6]
	adr	x6,pmm_zone_low			; compteur de sa zone
	movl	x8,MEM_LOW_LIMIT shr 12
	cmp	x0,x8
	b.lo	.z
	adr	x6,pmm_zone_high
.z:	ldr	x8,[x6,Z_FREE]
	sub	x8,x8,1
	str	x8,[x6,Z_FREE]
.n:	add	x0,x0,1
	b	.p
.out:	ret

; (sous pmm_lock) x0 = nombre de pages, x1 = zone -> x0 = premiere page
; (marquee occupee), 0 si aucune suite libre assez longue
pmm_zone_alloc:
	adr	x10,pmm_bitmap
	ldr	x10,[x10]
	mov	x11,x0				; n
	mov	x12,x1				; zone
	ldr	x9,[x12,Z_FREE]
	cmp	x9,x11
	b.lo	.none
	ldp	x1,x13,[x12,Z_FIRST]		; premiere page, fin
	ldr	x0,[x12,Z_HINT]
	cmp	x0,x1
	csel	x1,x0,x1,hi			; depart : l'indice
	mov	x14,-1				; premiere page libre vue
	mov	x2,0				; longueur de la sequence libre
	mov	x3,x1				; debut de la sequence
.scan:	cmp	x1,x13
	b.hs	.fail
	lsr	x4,x1,6
	ldr	x5,[x10,x4,lsl 3]
	cbnz	x2,.bit
	cmn	x5,1				; mot plein et pas de sequence : saut de 64
	b.ne	.bit
	and	x1,x1,not 63
	add	x1,x1,64
	b	.scan
.bit:	and	x6,x1,63
	lsrv	x5,x5,x6
	add	x1,x1,1
	tbnz	x5,0,.used
	cbnz	x2,.grow
	sub	x3,x1,1
	cmn	x14,1
	csel	x14,x3,x14,eq
.grow:	add	x2,x2,1
	cmp	x2,x11
	b.lo	.scan
	mov	x1,x3				; [x3, x3+n[ libre : marquer occupe
.mark:	lsr	x4,x1,6
	and	x6,x1,63
	ldr	x5,[x10,x4,lsl 3]
	mov	x7,1
	lslv	x7,x7,x6
	orr	x5,x5,x7
	str	x5,[x10,x4,lsl 3]
	add	x1,x1,1
	subs	x2,x2,1
	b.ne	.mark
	sub	x9,x9,x11
	str	x9,[x12,Z_FREE]
	add	x4,x3,x11			; indice : apres la suite prise si
	cmp	x14,x3				; elle commence a la premiere page
	csel	x4,x14,x4,ne			; libre vue, sinon cette page
	str	x4,[x12,Z_HINT]
	adr	x4,pmm_free_pages
	ldr	x5,[x4]
	sub	x5,x5,x11
	str	x5,[x4]
	mov	x0,x3
	ret
.used:	mov	x2,0
	b	.scan
.fail:	cmn	x14,1				; rien de libre apres l'indice : il
	csel	x14,x13,x14,eq			; avance jusqu'a la fin
	str	x14,[x12,Z_HINT]
.none:	mov	x0,0
	ret

; x0 = nombre de pages contigues -> x0 = adresse physique (non mise a zero),
; 0 si echec. Zone haute d'abord, puis zone basse.
pmm_alloc_n:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,pmm_lock
	bl	spin_lock_irqsave
	mov	x20,x0				; DAIF
	adr	x0,pmm_high_on
	ldr	x0,[x0]
	cbz	x0,.low
	mov	x0,x19
	adr	x1,pmm_zone_high
	bl	pmm_zone_alloc
	cbnz	x0,.got
.low:	mov	x0,x19
	adr	x1,pmm_zone_low
	bl	pmm_zone_alloc
.got:	lsl	x19,x0,12
	adr	x0,pmm_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nombre de pages contigues -> x0 = adresse physique sous 1 Go (zone
; basse : DMA, VideoCore, V3D), 0 si echec
pmm_alloc_low_n:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,pmm_lock
	bl	spin_lock_irqsave
	mov	x20,x0
	mov	x0,x19
	adr	x1,pmm_zone_low
	bl	pmm_zone_alloc
	lsl	x19,x0,12
	adr	x0,pmm_lock
	mov	x1,x20
	bl	spin_unlock_irqrestore
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = une page
pmm_alloc:
	mov	x0,1
	b	pmm_alloc_n

; -> x0 = une page sous 1 Go
pmm_alloc_low:
	mov	x0,1
	b	pmm_alloc_low_n

; -> x0 = pages libres, toutes zones (feuille)
mem_free_pages:
	adr	x0,pmm_zone_low
	ldr	x0,[x0,Z_FREE]
	adr	x1,pmm_zone_high
	ldr	x1,[x1,Z_FREE]
	add	x0,x0,x1
	ret

; (faucheur, toutes les 20 ms) peu de memoire libre : les services qui en gardent
; sont prevenus, avant qu'un process en manque
mem_watch:
	prologue
	bl	mem_free_pages
	mov	x1,MEM_LOW_PAGES
	cmp	x0,x1
	b.hs	.out
	bl	svc_low_memory
.out:	epilogue

; (thread, hors de tout verrou) plus de page : demande aux services d'en rendre
; et attend -> x0 = 1 s'il y a de nouveau MEM_WAIT_PAGES libres, 0 si rien n'est
; venu en MEM_WAIT_MS, ou s'il n'y a personne a attendre
mem_wait:
	prologue
	stp	x19,x20,[sp,-16]!
	mrs	x20,daif
	msr	daifclr,2			; (une faute d'EL0 arrive IRQ masquees)
	mov	x19,MEM_WAIT_MS / MEM_WAIT_STEP
.l:	bl	svc_low_memory
	cbnz	x0,.no
	mov	x0,MEM_WAIT_STEP
	bl	thread_sleep
	bl	mem_free_pages
	mov	x1,MEM_WAIT_PAGES
	cmp	x0,x1
	b.hs	.yes
	subs	x19,x19,1
	b.ne	.l
.no:	mov	x0,0
	b	.out
.yes:	mov	x0,1
.out:	msr	daif,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse, x1 = nombre de pages (d'une meme zone)
pmm_free_n:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	lsr	x19,x0,12
	mov	x20,x1
	cbz	x20,.ret
	adr	x0,pmm_lock
	bl	spin_lock_irqsave
	mov	x21,x0
	adr	x3,pmm_bitmap
	ldr	x3,[x3]
	mov	x2,x20
	mov	x8,x19
.clr:	lsr	x4,x8,6
	and	x6,x8,63
	ldr	x5,[x3,x4,lsl 3]
	mov	x7,1
	lslv	x7,x7,x6
	bic	x5,x5,x7
	str	x5,[x3,x4,lsl 3]
	add	x8,x8,1
	subs	x2,x2,1
	b.ne	.clr
	adr	x4,pmm_free_pages
	ldr	x5,[x4]
	add	x5,x5,x20
	str	x5,[x4]
	adr	x4,pmm_zone_low			; zone : compteur et indice
	movl	x5,MEM_LOW_LIMIT shr 12
	cmp	x19,x5
	b.lo	.z
	adr	x4,pmm_zone_high
.z:	ldr	x5,[x4,Z_FREE]
	add	x5,x5,x20
	str	x5,[x4,Z_FREE]
	ldr	x5,[x4,Z_HINT]
	cmp	x19,x5
	csel	x5,x19,x5,lo
	str	x5,[x4,Z_HINT]
	adr	x0,pmm_lock
	mov	x1,x21
	bl	spin_unlock_irqrestore
.ret:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse d'une page
pmm_free:
	mov	x1,1
	b	pmm_free_n

; x0 = adresse, x1 = taille en octets (multiple de 16) : mise a zero
mem_zero:
.z:	stp	xzr,xzr,[x0],16
	subs	x1,x1,16
	b.hi	.z
	ret

; x0 = dst, x1 = src, x2 = taille en octets (par 16 octets quand les deux
; adresses et la taille s'y pretent)
mem_copy:
	cbz	x2,.done
	orr	x3,x0,x1
	orr	x3,x3,x2
	tst	x3,15
	b.ne	.b
.q:	ldp	x3,x4,[x1],16
	stp	x3,x4,[x0],16
	subs	x2,x2,16
	b.ne	.q
	ret
.b:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.b
.done:	ret

; x0 = destination, x1 = chaine -> x0 = position du 0 final (copie <= 64 o)
str_copy_n:
	mov	x2,63
.c:	ldrb	w3,[x1],1
	cbz	x2,.end
	cbz	w3,.end
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.end:	strb	wzr,[x0]
	ret
