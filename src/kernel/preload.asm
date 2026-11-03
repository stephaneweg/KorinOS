; ============================================================================
;  preload.asm - images des executables : chargees une fois, partagees
;
;  Tout lancement passe par une image (docs/ABI.md, « Images ») : un modele,
;  espace d'adressage qui ne tourne jamais, dont les pages de code et de
;  lecture seule sont partagees par les process lances de lui (PTE_SHARED),
;  les donnees en copie sur ecriture (PTE_COW, vmspace.asm), le .bss en
;  region paresseuse. Cle : le chemin resolu par vfs (« SD0:/bin/fasmarm »),
;  compare aux majuscules pres, et la taille du fichier.
;
;  Le noyau ne lit aucun fichier. Construction d'une image (AppKit, apres
;  V_EXEC) sans tampon intermediaire ni copie :
;   1. IMAGE_CREATE (chemin, taille, drapeaux) : le noyau prend les pages
;      physiques du fichier (a zero), les mappe dans le modele (zone de
;      travail LOADER_WIN) et dans l'appelant, en ecriture, a une fenetre
;      LOADER_WIN + n x 4 Go (4 chargements a la fois par process) ;
;   2. AppKit lit le fichier directement dans la fenetre (vfs, fatfs) ;
;   3. IMAGE_SEAL (fenetre, drapeaux) : la fenetre est retiree de
;      l'appelant, les en-tetes ELF verifies, chaque page d'un segment est
;      la page du fichier elle-meme quand c'est possible (meme decalage dans
;      la page, page entiere, pas deja prise : img_build), sinon une page a
;      soi ou la partie lue est recopiee ; les pages du fichier hors segment
;      (en-tetes de sections, symboles) sont rendues. Echec de lecture ou de
;      verification (ou drapeau IMS_ABORT) : tout est rendu.
;  Lancement : SPAWN_PRELOADED (chemin, taille, params) ; E_NOENT s'il n'y a
;  pas d'image : AppKit la construit puis relance SPAWN_PRELOADED.
;
;  Deux sortes d'images :
;   * epinglee (IMG_PIN : SD0:/etc/preload.txt par init2, la commande
;     preload) : gardee tant que le fichier ne change pas ;
;   * passagere (les autres lancements) : rendue a la fin du dernier process
;     lance d'elle (image_unref). (A faire : la garder apres la fin, et
;     l'eviction sous pression memoire.)
;  Une image construite par un process qui a les droits de SD0:/sys est
;  marquee IMK_SYS ; un lancement avec droits n'accepte qu'une telle image
;  (un process sans droits ne peut pas fabriquer l'image d'un service).
;
;  Comptage : une reference pour la table (tant qu'elle est valide), une par
;  process lance d'elle (P_IMAGE, rendue par vm_destroy) ; pages du modele
;  rendues a la derniere. Fichier change : vfs appelle IMAGE_FORGET (aussi la
;  commande unload). Option no_preload : IMAGE_CREATE et IMAGE_PRELOAD
;  rendent E_NOSYS ; AppKit lit alors le fichier dans un tampon et passe par
;  SPAWN_IMAGE (64 Mo au plus), comme avant les images.
;  Au journal, un lancement : « image <chemin> : chargee en N ms » (le
;  premier, depuis IMAGE_CREATE) ou « partagee en N ms ».
; ============================================================================

IMG_MAX		= 128
IM_USED		= 0
IM_VALID	= 8
IM_REFS		= 16
IM_FSIZE	= 24			; taille du fichier
IM_TPL		= 32			; modele (PCB sans thread)
IM_ENTRY	= 40
IM_BRK		= 48
IM_PAGES	= 56			; pages du modele
IM_RWPAGES	= 64			; dont inscriptibles (copie sur ecriture)
IM_LAZY		= 72			; octets de .bss a la demande
IM_PATH		= 80
IM_PATH_MAX	= 128
IM_LOADER	= 208			; PCB qui charge (image en construction)
IM_WIN		= 216			; sa fenetre
IM_T0		= 224			; ms a IMAGE_CREATE
IM_KIND		= 232			; IMK_*
IM_NPAGES	= 240			; pages du fichier
IM_SIZE		= 256

IMK_PIN		= 1			; epinglee (preload)
IMK_PENDING	= 2			; en construction (entre CREATE et SEAL)
IMK_FRESH	= 4			; pas encore lancee (journal : « chargee »)
IMK_SYS		= 8			; construite avec les droits de SD0:/sys
IMK_PENDING_BIT	= 1

LOADER_WIN	= 0x6C00000000		; fenetres de chargement (et zone de
LOADER_SLOT	= 0x100000000		; travail du modele) : 4 x 4 Go sous le
LOADER_SLOTS	= 4			; framebuffer (0x70_0000_0000)
PTE_IMG_TAKEN	= 1 shl 57		; bit logiciel (zone de travail du modele) :
					; page du fichier deja prise par un segment

; x0 = chaine, x1 = chaine -> x0 = 1 si egales aux majuscules pres (ASCII)
str_ieq:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	sub	w4,w2,'A'
	cmp	w4,25
	b.hi	.a
	add	w2,w2,32
.a:	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.b
	add	w3,w3,32
.b:	cmp	w2,w3
	b.ne	.no
	cbnz	w2,.c
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = image : une reference de moins ; a la derniere, le modele et ses pages
; sont rendus. Une image passagere n'a plus que la reference de la table
; quand son dernier process finit : elle est rendue aussi.
image_unref:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	mov	x20,0
	ldr	x2,[x19,IM_REFS]
	sub	x2,x2,1
	str	x2,[x19,IM_REFS]
	cbz	x2,.last
	cmp	x2,1
	b.ne	.unlock
	ldr	x3,[x19,IM_VALID]		; passagere valide, plus lancee :
	cbz	x3,.unlock			; rendue
	ldr	x3,[x19,IM_KIND]
	tst	x3,IMK_PIN
	b.ne	.unlock
	str	xzr,[x19,IM_VALID]
	str	xzr,[x19,IM_REFS]
.last:	ldr	x20,[x19,IM_TPL]
	str	xzr,[x19,IM_KIND]
	str	xzr,[x19,IM_USED]
.unlock:
	adr	x0,img_lock
	bl	spin_unlock_irqrestore
	cbz	x20,.out
	mov	x0,x20
	bl	vm_destroy
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin (noyau), x1 = taille du fichier, x2 = 1 si l'image doit etre
; IMK_SYS -> x0 = image valide de ce chemin et de cette taille, avec une
; reference prise pour l'appelant (0 : aucune)
img_find_ref:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x23,x2
	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x21,img_table
	mov	x24,IMG_MAX
.s:	ldr	x0,[x21,IM_USED]
	cbz	x0,.n
	ldr	x0,[x21,IM_VALID]
	cbz	x0,.n
	ldr	x0,[x21,IM_FSIZE]
	cmp	x0,x20
	b.ne	.n
	cbz	x23,.p
	ldr	x0,[x21,IM_KIND]
	tst	x0,IMK_SYS
	b.eq	.n
.p:	mov	x0,x19
	add	x1,x21,IM_PATH
	bl	str_ieq
	cbz	x0,.n
	ldr	x0,[x21,IM_REFS]
	add	x0,x0,1
	str	x0,[x21,IM_REFS]
	b	.unlock
.n:	add	x21,x21,IM_SIZE
	subs	x24,x24,1
	b.ne	.s
	mov	x21,0
.unlock:
	adr	x0,img_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin (noyau) : les images valides de ce chemin ne sont plus prises ;
; la reference de la table est rendue -> x0 = nombre d'images oubliees
img_forget:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,IMG_MAX*8			; modeles a rendre
	mov	x19,x0
	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x21,img_table
	mov	x20,IMG_MAX
	mov	x23,0				; oubliees
	mov	x24,0				; a rendre
.s:	ldr	x0,[x21,IM_USED]
	cbz	x0,.n
	ldr	x0,[x21,IM_VALID]
	cbz	x0,.n
	mov	x0,x19
	add	x1,x21,IM_PATH
	bl	str_ieq
	cbz	x0,.n
	str	xzr,[x21,IM_VALID]
	add	x23,x23,1
	ldr	x0,[x21,IM_REFS]
	sub	x0,x0,1
	str	x0,[x21,IM_REFS]
	cbnz	x0,.n
	ldr	x0,[x21,IM_TPL]
	str	x0,[sp,x24,lsl 3]
	add	x24,x24,1
	str	xzr,[x21,IM_KIND]
	str	xzr,[x21,IM_USED]
.n:	add	x21,x21,IM_SIZE
	subs	x20,x20,1
	b.ne	.s
	adr	x0,img_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
.d:	cbz	x24,.out
	sub	x24,x24,1
	ldr	x0,[sp,x24,lsl 3]
	bl	vm_destroy
	b	.d
.out:	mov	x0,x23
	add	sp,sp,IMG_MAX*8
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB neuf, x1 = image : ses pages mappees (lecture seule et code
; partages ; donnees partagees en copie sur ecriture), ses regions
; paresseuses recopiees -> x0 = 1 si succes
img_clone:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x1,IM_TPL]
	ldr	x21,[x20,P_L1]
	movl	x26,PTE_ADDR_MASK
	mov	x22,256				; L1 de la moitie EL0
.l1:	ldr	x0,[x21,x22,lsl 3]
	tbz	x0,0,.n1
	and	x23,x0,x26			; table L2 du modele
	mov	x24,0
.l2:	ldr	x0,[x23,x24,lsl 3]
	tbz	x0,0,.n2
	and	x25,x0,x26			; table L3 du modele
	mov	x1,0				; (zone de travail videe : une table
.e:	ldr	x2,[x25,x1,lsl 3]		; sans page n'est pas recopiee)
	tbnz	x2,0,.has
	add	x1,x1,1
	cmp	x1,512
	b.lo	.e
	b	.n2
.has:	mov	x0,x19				; la meme chez le process
	lsl	x1,x22,30
	orr	x1,x1,x24,lsl 21
	bl	vm_pte				; (entree 0 de sa table L3)
	cbz	x0,.fail
	mov	x1,0
.l3:	ldr	x2,[x25,x1,lsl 3]
	tbz	x2,0,.n3
	ubfx	x3,x2,6,2
	cmp	x3,1				; inscriptible : lecture seule +
	b.ne	.ro				; copie sur ecriture
	orr	x2,x2,0xC0
	orr	x2,x2,PTE_COW
.ro:	orr	x2,x2,PTE_SHARED		; jamais a lui : pas rendue par vm_destroy
	str	x2,[x0,x1,lsl 3]
.n3:	add	x1,x1,1
	cmp	x1,512
	b.lo	.l3
.n2:	add	x24,x24,1
	cmp	x24,512
	b.lo	.l2
.n1:	add	x22,x22,1
	cmp	x22,512
	b.lo	.l1
	dsb	ishst
	mov	x22,0				; regions paresseuses (.bss)
.v:	ldr	x0,[x20,P_VMA_N]
	cmp	x22,x0
	b.hs	.ok
	ldr	x0,[x20,P_VMA]
	add	x0,x0,x22,lsl 5
	ldp	x2,x3,[x0]
	ldr	x4,[x0,VMA_PROT]
	mov	x0,x19
	mov	x1,x22
	bl	vma_insert_at
	cbz	x0,.fail
	add	x22,x22,1
	b	.v
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB (modele) -> x0 = pages presentes, x1 = dont inscriptibles
img_count:
	ldr	x1,[x0,P_L1]
	movl	x7,PTE_ADDR_MASK
	mov	x0,0
	mov	x8,0
	mov	x2,256
.l1:	ldr	x3,[x1,x2,lsl 3]
	tbz	x3,0,.n1
	and	x3,x3,x7
	mov	x4,0
.l2:	ldr	x5,[x3,x4,lsl 3]
	tbz	x5,0,.n2
	and	x5,x5,x7
	mov	x6,0
.l3:	ldr	x9,[x5,x6,lsl 3]
	tbz	x9,0,.n3
	add	x0,x0,1
	ubfx	x9,x9,6,2
	cmp	x9,1
	b.ne	.n3
	add	x8,x8,1
.n3:	add	x6,x6,1
	cmp	x6,512
	b.lo	.l3
.n2:	add	x4,x4,1
	cmp	x4,512
	b.lo	.l2
.n1:	add	x2,x2,1
	cmp	x2,512
	b.lo	.l1
	mov	x1,x8
	ret

; x0 = modele -> x0 = octets de ses regions paresseuses (.bss)
img_lazy_bytes:
	ldr	x1,[x0,P_VMA_N]
	ldr	x0,[x0,P_VMA]
	mov	x2,0
.r:	cbz	x1,.rd
	ldp	x3,x4,[x0],VMA_SIZE
	sub	x4,x4,x3
	add	x2,x2,x4
	sub	x1,x1,1
	b	.r
.rd:	mov	x0,x2
	ret

; x0 = modele, x1 = decalage dans le fichier -> x0 = adresse physique de cet
; octet (dans la zone de travail), 0 si absente
img_file_pa:
	prologue
	str	x19,[sp,-16]!
	and	x19,x1,0xFFF
	movl	x2,LOADER_WIN
	add	x1,x1,x2
	bl	vm_lookup
	cbz	x0,.out
	ldr	x0,[x0]
	tbz	x0,0,.none
	movl	x1,PTE_ADDR_MASK
	and	x0,x0,x1
	add	x0,x0,x19
	b	.out
.none:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = modele, x1 = destination (noyau), x2 = decalage dans le fichier,
; x3 = octets : copie depuis les pages du fichier -> x0 = 1 si succes
img_copy_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
.c:	cbz	x22,.ok
	mov	x0,x19
	mov	x1,x21
	bl	img_file_pa
	cbz	x0,.fail
	mov	x1,x0
	and	x2,x21,0xFFF			; jusqu'a la fin de la page du fichier
	mov	x3,PAGE_SIZE
	sub	x2,x3,x2
	cmp	x2,x22
	csel	x2,x2,x22,lo
	add	x21,x21,x2
	sub	x22,x22,x2
	mov	x0,x20
	add	x20,x20,x2
	bl	mem_copy
	b	.c
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = modele, x1 = taille du fichier : la zone de travail videe (pages du
; fichier prises par un segment : entree effacee ; les autres : rendues)
img_scratch_free:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	add	x20,x1,0xFFF
	lsr	x20,x20,12
	movl	x21,LOADER_WIN
.p:	cbz	x20,.out
	mov	x0,x19
	mov	x1,x21
	bl	vm_lookup
	cbz	x0,.n
	ldr	x1,[x0]
	str	xzr,[x0]
	tbz	x1,0,.n
	tbnz	x1,57,.n			; PTE_IMG_TAKEN : a un segment
	movl	x2,PTE_ADDR_MASK
	and	x0,x1,x2
	bl	pmm_free
.n:	add	x21,x21,PAGE_SIZE
	sub	x20,x20,1
	b	.p
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = modele (fichier dans sa zone de travail), x1 = taille du fichier :
; segments PT_LOAD mis en place (pages du fichier reprises ou partie lue
; recopiee, .bss en region paresseuse), zone de travail videe dans tous les
; cas -> x0 = 1 si succes, x1 = fin du plus haut segment, x2 = e_entry
img_build:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,32			; +0 e_entry, +8 attributs, +16 resultat
	mov	x19,x0
	mov	x20,x1
	str	xzr,[sp,16]
	mov	x0,x19				; en-tete : page 0 du fichier ; la
	mov	x1,0				; table des segments doit y tenir
	bl	img_file_pa
	cbz	x0,.fin
	mov	x21,x0
	mov	x1,PAGE_SIZE
	cmp	x20,x1
	csel	x1,x20,x1,lo
	bl	elf_check
	cbz	x0,.fin
	ldr	x0,[x21,24]			; e_entry
	str	x0,[sp]
	ldr	x23,[x21,32]			; e_phoff
	ldrh	w24,[x21,56]			; e_phnum
	add	x23,x21,x23
	movl	x25,USER_BASE			; plus haute adresse chargee (-> brk)
.ph:	cbz	w24,.good
	ldr	w0,[x23]			; p_type
	cmp	w0,PT_LOAD
	b.ne	.ph_next
	ldr	x1,[x23,16]			; p_vaddr
	ldr	x2,[x23,40]			; p_memsz
	ldr	x3,[x23,32]			; p_filesz
	cmp	x3,x2
	b.hi	.fin
	movl	x4,USER_BASE			; dans la zone EL0, sous la pile
	cmp	x1,x4
	b.lo	.fin
	adds	x4,x1,x2
	b.cs	.fin
	movl	x5,USER_STACK_TOP - USER_STACK_SIZE
	cmp	x4,x5
	b.hi	.fin
	cmp	x4,x25
	csel	x25,x4,x25,hi
	ldr	x5,[x23,8]			; p_offset + p_filesz dans le fichier
	adds	x5,x5,x3
	b.cs	.fin
	cmp	x5,x20
	b.hi	.fin
	ldr	w5,[x23,4]			; p_flags -> PROT_*
	mov	x26,PROT_READ
	tbz	w5,1,.nw			; PF_W
	orr	x26,x26,PROT_WRITE
.nw:	tbz	w5,0,.nx			; PF_X
	orr	x26,x26,PROT_EXEC
.nx:	mov	x0,x26
	bl	vm_prot_attrs
	str	x0,[sp,8]
	ldr	x1,[x23,16]
	ldr	x2,[x23,40]
	ldr	x3,[x23,32]
	add	x4,x1,x2
	add	x27,x1,x3			; fin de la partie lue, a la page
	add	x27,x27,0xFFF
	and	x27,x27,not 0xFFF
	add	x28,x4,0xFFF			; fin du segment, a la page
	and	x28,x28,not 0xFFF
	cmp	x27,x28
	csel	x27,x28,x27,hi
	and	x22,x1,not 0xFFF		; pages de la partie lue
	ldr	x0,[x23,32]			; rien du fichier (p_filesz = 0) : tout
	cmp	x0,0				; est paresseux, sa premiere page aussi
	csel	x27,x22,x27,eq
.pg:	cmp	x22,x27
	b.hs	.lazy
	mov	x0,x19
	mov	x1,x22
	bl	vm_pte
	cbz	x0,.fin
	mov	x21,x0				; entree du segment chez le modele
	ldr	x0,[x21]
	tbnz	x0,0,.exist
	; la page du fichier elle-meme ? meme decalage dans la page, page
	; entiere dans la partie lue, pas deja prise
	ldr	x1,[x23,16]			; p_vaddr
	ldr	x2,[x23,8]			; p_offset
	eor	x3,x1,x2
	tst	x3,0xFFF
	b.ne	.copy
	ldr	x3,[x23,32]
	add	x3,x1,x3			; fin de la partie lue
	add	x4,x22,PAGE_SIZE
	cmp	x4,x3
	b.hi	.copy
	sub	x2,x2,x1			; decalage = p_offset - p_vaddr + va
	add	x1,x2,x22
	movl	x2,LOADER_WIN
	add	x1,x1,x2
	mov	x0,x19
	bl	vm_lookup
	cbz	x0,.copy
	ldr	x1,[x0]
	tbz	x1,0,.copy
	tbnz	x1,57,.copy			; deja prise
	movl	x2,PTE_IMG_TAKEN
	orr	x2,x1,x2
	str	x2,[x0]
	movl	x2,PTE_ADDR_MASK
	and	x0,x1,x2
	ldr	x1,[sp,8]
	orr	x1,x0,x1
	str	x1,[x21]
	tst	x26,PROT_EXEC
	b.eq	.pg_next
	mov	x1,PAGE_SIZE
	bl	dcache_clean_pou
	b	.pg_next
.copy:	bl	pmm_alloc			; une page a soi
	cbz	x0,.fin
	str	x0,[sp,24]
	mov	x1,PAGE_SIZE
	bl	mem_zero
	ldr	x0,[sp,24]
	ldr	x1,[sp,8]
	orr	x1,x0,x1
	str	x1,[x21]
	b	.fill
.exist:	ldr	x2,[sp,8]			; page deja la (deux segments) :
	tbnz	x2,7,.keep_ap			; droits fusionnes, comme
	and	x0,x0,not 0x80			; vm_alloc_range
.keep_ap:
	tbnz	x2,54,.st
	and	x0,x0,not PTE_UXN
.st:	str	x0,[x21]
	movl	x1,PTE_ADDR_MASK
	and	x0,x0,x1
	str	x0,[sp,24]
.fill:	ldr	x1,[x23,16]			; [max(va, p_vaddr), min(va + 4 Ko,
	cmp	x22,x1				;  p_vaddr + p_filesz)[ recopie
	csel	x4,x22,x1,hi
	ldr	x3,[x23,32]
	add	x3,x1,x3
	add	x5,x22,PAGE_SIZE
	cmp	x5,x3
	csel	x5,x5,x3,lo
	cmp	x4,x5
	b.hs	.flushc
	ldr	x2,[x23,8]			; decalage dans le fichier
	sub	x6,x4,x1
	add	x2,x2,x6
	sub	x3,x5,x4			; octets
	ldr	x1,[sp,24]
	sub	x6,x4,x22
	add	x1,x1,x6			; destination
	mov	x0,x19
	bl	img_copy_file
	cbz	x0,.fin
.flushc:
	tst	x26,PROT_EXEC
	b.eq	.pg_next
	ldr	x0,[sp,24]
	mov	x1,PAGE_SIZE
	bl	dcache_clean_pou
.pg_next:
	add	x22,x22,PAGE_SIZE
	b	.pg
.lazy:	cmp	x28,x27				; .bss : region paresseuse
	b.ls	.ph_next
	mov	x0,x19
	mov	x1,x27
	mov	x2,x28
	bl	vma_overlaps
	cbnz	x0,.eager
	mov	x0,x19
	mov	x1,x27
	bl	vma_lower
	mov	x1,x0
	mov	x0,x19
	mov	x2,x27
	mov	x3,x28
	mov	x4,VMK_BSS
	lsl	x4,x4,32
	orr	x4,x4,x26
	bl	vma_insert_at
	cbnz	x0,.ph_next
.eager:	ldr	x3,[sp,8]			; (chevauchement : peuplee d'avance)
	mov	x0,x19
	mov	x1,x27
	sub	x2,x28,x27
	bl	vm_alloc_range
	cbz	x0,.fin
.ph_next:
	add	x23,x23,56
	sub	w24,w24,1
	b	.ph
.good:	mov	x0,1
	str	x0,[sp,16]
.fin:	mov	x0,x19				; zone de travail videe
	mov	x1,x20
	bl	img_scratch_free
	ldr	x0,[sp,16]
	mov	x1,x25
	ldr	x2,[sp]
	add	sp,sp,32
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = image en construction (la place est a l'appelant, plus trouvable par
; IMAGE_SEAL) : fenetre retiree du chargeur s'il en a encore une (IM_LOADER),
; modele et pages du fichier rendus, place liberee
img_discard:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,IM_LOADER]
	cbz	x0,.noloader
	ldr	x1,[x19,IM_WIN]
	ldr	x2,[x19,IM_NPAGES]
	lsl	x2,x2,12
	mov	x3,0
	bl	vm_unmap_range
.noloader:
	ldr	x0,[x19,IM_TPL]
	cbz	x0,.free
	bl	vm_destroy
.free:	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	str	xzr,[x19,IM_LOADER]
	str	xzr,[x19,IM_TPL]
	str	xzr,[x19,IM_KIND]
	str	xzr,[x19,IM_USED]
	adr	x0,img_lock
	bl	spin_unlock_irqrestore
	ldr	x19,[sp],16
	epilogue

; x0 = PCB qui disparait (vm_destroy) : ses chargements en cours sont rendus
; (ses tables partent avec lui : la fenetre n'est pas retiree)
img_loader_gone:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
.again:	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x21,img_table
	mov	x20,IMG_MAX
.s:	ldr	x0,[x21,IM_USED]
	cbz	x0,.n
	ldr	x0,[x21,IM_KIND]
	tbz	x0,IMK_PENDING_BIT,.n
	ldr	x0,[x21,IM_LOADER]
	cmp	x0,x19
	b.ne	.n
	ldr	x0,[x21,IM_KIND]		; a nous : plus trouvable
	bic	x0,x0,IMK_PENDING
	str	x0,[x21,IM_KIND]
	str	xzr,[x21,IM_LOADER]
	adr	x0,img_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	bl	img_discard
	b	.again
.n:	add	x21,x21,IM_SIZE
	subs	x20,x20,1
	b.ne	.s
	adr	x0,img_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_IMAGE_CREATE image_create(chemin, taille, drapeaux) -> fenetre -----
; chemin = chemin resolu par vfs (cle de l'image), taille = celle du fichier
; (64 o a 4 Go), drapeaux : IMG_PIN (epinglee). Les pages du fichier (a zero)
; sont mappees en ecriture chez l'appelant a la fenetre rendue ; il y lit le
; fichier puis appelle IMAGE_SEAL. E_NOSYS : option no_preload ; E_NOSPC :
; table pleine ou 4 chargements deja en cours dans ce process.
sys_image_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,IM_PATH_MAX
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,preload_off
	ldr	x0,[x0]
	cbnz	x0,.nosys
	cmp	x20,64
	b.lo	.inval
	movl	x0,LOADER_SLOT
	cmp	x20,x0
	b.hi	.inval
	mov	x0,x19
	mov	x1,sp
	mov	x2,IM_PATH_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.out
	current_proc
	mov	x22,x1
	mov	x26,IMK_PENDING			; sorte
	tst	x21,IMG_PIN
	b.eq	.np
	orr	x26,x26,IMK_PIN
.np:	ldr	x0,[x22,P_DROITS]
	cbz	x0,.ns
	orr	x26,x26,IMK_SYS
.ns:	bl	vm_create			; le modele
	cbz	x0,.nomem
	mov	x23,x0
	adr	x0,img_lock			; une place, une fenetre
	bl	spin_lock_irqsave
	mov	x27,x0
	adr	x24,img_table
	mov	x3,IMG_MAX
	mov	x4,0				; fenetres prises (bits)
	mov	x5,0				; place libre
.s:	ldr	x0,[x24,IM_USED]
	cbz	x0,.libre
	ldr	x0,[x24,IM_KIND]
	tbz	x0,IMK_PENDING_BIT,.nx
	ldr	x0,[x24,IM_LOADER]
	cmp	x0,x22
	b.ne	.nx
	ldr	x0,[x24,IM_WIN]
	movl	x6,LOADER_WIN
	sub	x0,x0,x6
	lsr	x0,x0,32
	mov	x6,1
	lsl	x6,x6,x0
	orr	x4,x4,x6
	b	.nx
.libre:	cbnz	x5,.nx
	mov	x5,x24
.nx:	add	x24,x24,IM_SIZE
	subs	x3,x3,1
	b.ne	.s
	cbz	x5,.full
	mvn	x6,x4				; premiere fenetre libre
	rbit	x6,x6
	clz	x6,x6
	cmp	x6,LOADER_SLOTS
	b.hs	.full
	mov	x24,x5
	movl	x0,LOADER_WIN
	add	x25,x0,x6,lsl 32
	mov	x0,1
	str	x0,[x24,IM_USED]
	str	xzr,[x24,IM_VALID]
	str	xzr,[x24,IM_REFS]
	str	x26,[x24,IM_KIND]
	str	x22,[x24,IM_LOADER]
	str	x25,[x24,IM_WIN]
	str	x23,[x24,IM_TPL]
	str	x20,[x24,IM_FSIZE]
	str	xzr,[x24,IM_NPAGES]
	adr	x0,img_lock
	mov	x1,x27
	bl	spin_unlock_irqrestore
	add	x0,x24,IM_PATH			; (place a nous : pas valide, en
	mov	x1,sp				; construction)
	mov	x2,IM_PATH_MAX-1
	bl	str_ncopy
	bl	time_ms
	str	x0,[x24,IM_T0]
	add	x28,x20,0xFFF			; pages du fichier
	lsr	x28,x28,12
	mov	x27,0
.pg:	cmp	x27,x28
	b.hs	.done
	bl	pmm_alloc
	cbz	x0,.fail
	mov	x19,x0
	mov	x1,PAGE_SIZE
	bl	mem_zero
	mov	x0,x23				; au modele (zone de travail) : a lui
	movl	x1,LOADER_WIN
	add	x1,x1,x27,lsl 12
	bl	vm_pte
	cbz	x0,.fail_pg
	movl	x1,VM_RW
	orr	x1,x19,x1
	str	x1,[x0]
	add	x0,x27,1
	str	x0,[x24,IM_NPAGES]
	mov	x0,x22				; chez l'appelant : partagee
	add	x1,x25,x27,lsl 12
	bl	vm_pte
	cbz	x0,.fail
	movl	x1,VM_RW or PTE_SHARED
	orr	x1,x19,x1
	str	x1,[x0]
	add	x27,x27,1
	b	.pg
.done:	dsb	ishst
	mov	x0,x25
	b	.out
.fail_pg:
	mov	x0,x19
	bl	pmm_free
.fail:	adr	x0,img_lock			; plus trouvable par IMAGE_SEAL
	bl	spin_lock_irqsave
	mov	x1,x0
	ldr	x2,[x24,IM_KIND]
	bic	x2,x2,IMK_PENDING
	str	x2,[x24,IM_KIND]
	adr	x0,img_lock
	bl	spin_unlock_irqrestore
	mov	x0,x24
	bl	img_discard
	mov	x0,E_NOMEM
	b	.out
.full:	adr	x0,img_lock
	mov	x1,x27
	bl	spin_unlock_irqrestore
	mov	x0,x23
	bl	vm_destroy
	mov	x0,E_NOSPC
	b	.out
.nosys:	mov	x0,E_NOSYS
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,IM_PATH_MAX
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_IMAGE_SEAL image_seal(fenetre, drapeaux) -> 0 ----------------------
; Le fichier est lu dans la fenetre d'IMAGE_CREATE : elle est retiree de
; l'appelant, l'image verifiee et mise en place. drapeaux : IMS_ABORT (la
; lecture a echoue : tout est rendu). Une image passagere identique deja la
; (un lancement concurrent) : la neuve est rendue, l'autre sert.
; E_INVAL : pas de chargement a cette fenetre, ou executable refuse.
sys_image_seal:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	current_proc
	mov	x22,x1
	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x23,x0
	adr	x21,img_table
	mov	x24,IMG_MAX
.s:	ldr	x0,[x21,IM_USED]
	cbz	x0,.n
	ldr	x0,[x21,IM_KIND]
	tbz	x0,IMK_PENDING_BIT,.n
	ldr	x0,[x21,IM_LOADER]
	cmp	x0,x22
	b.ne	.n
	ldr	x0,[x21,IM_WIN]
	cmp	x0,x19
	b.eq	.found
.n:	add	x21,x21,IM_SIZE
	subs	x24,x24,1
	b.ne	.s
	adr	x0,img_lock
	mov	x1,x23
	bl	spin_unlock_irqrestore
	mov	x0,E_INVAL
	b	.out
.found:	ldr	x0,[x21,IM_KIND]		; a nous
	bic	x0,x0,IMK_PENDING
	str	x0,[x21,IM_KIND]
	adr	x0,img_lock
	mov	x1,x23
	bl	spin_unlock_irqrestore
	mov	x0,x22				; fenetre retiree (TLB de l'ASID)
	mov	x1,x19
	ldr	x2,[x21,IM_NPAGES]
	lsl	x2,x2,12
	mov	x3,0
	bl	vm_unmap_range
	str	xzr,[x21,IM_LOADER]
	tbnz	x20,0,.abort			; IMS_ABORT
	ldr	x0,[x21,IM_TPL]
	ldr	x1,[x21,IM_FSIZE]
	bl	img_build
	cbz	x0,.bad
	str	x1,[x21,IM_BRK]
	str	x2,[x21,IM_ENTRY]
	ic	ialluis				; code neuf
	dsb	ish
	isb
	ldr	x0,[x21,IM_TPL]
	bl	img_count
	stp	x0,x1,[x21,IM_PAGES]
	ldr	x0,[x21,IM_TPL]
	bl	img_lazy_bytes
	str	x0,[x21,IM_LAZY]
	ldr	x0,[x21,IM_KIND]
	tst	x0,IMK_PIN
	b.eq	.transient
	add	x0,x21,IM_PATH			; epinglee : remplace les images de
	bl	img_forget			; ce chemin
	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x1,x0
	mov	x0,1
	str	x0,[x21,IM_REFS]
	dmb	ish
	str	x0,[x21,IM_VALID]
	adr	x0,img_lock
	bl	spin_unlock_irqrestore
	mov	x0,x21
	bl	img_log
	mov	x0,0
	b	.out
.transient:
	adr	x0,img_lock			; une image identique deja la ?
	bl	spin_lock_irqsave
	mov	x23,x0
	adr	x19,img_table
	mov	x24,IMG_MAX
.t:	cmp	x19,x21
	b.eq	.tn
	ldr	x0,[x19,IM_USED]
	cbz	x0,.tn
	ldr	x0,[x19,IM_VALID]
	cbz	x0,.tn
	ldr	x0,[x19,IM_FSIZE]
	ldr	x1,[x21,IM_FSIZE]
	cmp	x0,x1
	b.ne	.tn
	ldr	x0,[x21,IM_KIND]		; (la notre avec droits, l'autre
	tst	x0,IMK_SYS			; sans : la notre est gardee)
	b.eq	.tp
	ldr	x0,[x19,IM_KIND]
	tst	x0,IMK_SYS
	b.eq	.tn
.tp:	add	x0,x19,IM_PATH
	add	x1,x21,IM_PATH
	bl	str_ieq
	cbnz	x0,.dup
.tn:	add	x19,x19,IM_SIZE
	subs	x24,x24,1
	b.ne	.t
	mov	x0,1
	str	x0,[x21,IM_REFS]
	ldr	x0,[x21,IM_KIND]
	orr	x0,x0,IMK_FRESH
	str	x0,[x21,IM_KIND]
	dmb	ish
	mov	x0,1
	str	x0,[x21,IM_VALID]
	adr	x0,img_lock
	mov	x1,x23
	bl	spin_unlock_irqrestore
	mov	x0,0
	b	.out
.dup:	adr	x0,img_lock
	mov	x1,x23
	bl	spin_unlock_irqrestore
.abort:	mov	x0,x21
	bl	img_discard
	mov	x0,0
	b	.out
.bad:	mov	x0,x21
	bl	img_discard
	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_IMAGE_PRELOAD image_preload(chemin, image, taille) -> 0 ------------
; (ancien chemin, droits de SD0:/sys) l'executable lu dans un tampon de
; l'appelant ; AppKit passe maintenant par IMAGE_CREATE / IMAGE_SEAL. Garde
; pour un AppKit d'avant et pour la question : image = 0 -> 0 si les images
; sont actives (E_NOSYS avec l'option no_preload).
sys_image_preload:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,IM_PATH_MAX
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	mat_droits
	cbz	x0,.perm
	adr	x0,preload_off
	ldr	x0,[x0]
	cbnz	x0,.nosys
	mov	x0,0
	cbz	x20,.out			; (question : actif)
	cmp	x21,64
	b.lo	.inval
	movl	x0,SPAWN_IMAGE_MAX
	cmp	x21,x0
	b.hi	.inval
	mov	x0,x19
	mov	x1,sp
	mov	x2,IM_PATH_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.out
	check_user x20,x21,.fault
	add	x0,x21,0xFFF			; copie dans des pages du noyau
	lsr	x26,x0,12
	mov	x0,x26
	bl	pmm_alloc_n
	cbz	x0,.nomem
	mov	x22,x0
	mov	x1,x20
	mov	x2,x21
	bl	mem_copy
	mov	x0,x22
	mov	x1,x21
	bl	elf_check
	mov	x24,E_INVAL
	cbz	x0,.free
	bl	vm_create			; le modele
	mov	x24,E_NOMEM
	cbz	x0,.free
	mov	x23,x0
	mov	x1,x22
	mov	x2,x21
	bl	elf_load
	mov	x24,E_INVAL
	cbz	x0,.tpl_bad
	mov	x25,x1				; fin des segments (-> brk)
	ldr	x20,[x22,24]			; e_entry
	ic	ialluis
	dsb	ish
	isb
	mov	x0,x22				; copie du fichier rendue
	mov	x1,x26
	bl	pmm_free_n
	mov	x0,sp				; l'ancienne image de ce chemin
	bl	img_forget
	adr	x0,img_lock			; une place
	bl	spin_lock_irqsave
	mov	x26,x0
	adr	x22,img_table
	mov	x1,IMG_MAX
.slot:	ldr	x0,[x22,IM_USED]
	cbz	x0,.got
	add	x22,x22,IM_SIZE
	subs	x1,x1,1
	b.ne	.slot
	adr	x0,img_lock
	mov	x1,x26
	bl	spin_unlock_irqrestore
	mov	x0,x23
	bl	vm_destroy
	mov	x0,E_NOSPC
	b	.out
.got:	mov	x0,1
	str	x0,[x22,IM_USED]
	str	x0,[x22,IM_REFS]
	str	x21,[x22,IM_FSIZE]
	str	x23,[x22,IM_TPL]
	str	x20,[x22,IM_ENTRY]
	str	x25,[x22,IM_BRK]
	mov	x0,IMK_PIN or IMK_SYS
	str	x0,[x22,IM_KIND]
	str	xzr,[x22,IM_LOADER]
	add	x0,x22,IM_PATH
	mov	x1,sp
	mov	x2,IM_PATH_MAX-1
	bl	str_ncopy
	mov	x0,x23
	bl	img_count
	stp	x0,x1,[x22,IM_PAGES]
	mov	x0,x23
	bl	img_lazy_bytes
	str	x0,[x22,IM_LAZY]
	dmb	ish
	mov	x0,1				; prise des lancements a partir d'ici
	str	x0,[x22,IM_VALID]
	adr	x0,img_lock
	mov	x1,x26
	bl	spin_unlock_irqrestore
	mov	x0,x22
	bl	img_log
	mov	x0,0
	b	.out
.tpl_bad:
	mov	x0,x23
	bl	vm_destroy
.free:	mov	x0,x22
	mov	x1,x26
	bl	pmm_free_n
	mov	x0,x24
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.nosys:	mov	x0,E_NOSYS
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,IM_PATH_MAX
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = image : « Prechargé : chemin (N Ko : P pages partagees, dont W en
; copie sur ecriture ; .bss B Ko a la demande) »
img_log:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,msg_pl
	bl	kputs
	add	x0,x19,IM_PATH
	bl	kputs
	adr	x0,msg_pl2
	bl	kputs
	ldr	x0,[x19,IM_FSIZE]
	lsr	x0,x0,10
	bl	kput_dec
	adr	x0,msg_pl3
	bl	kputs
	ldr	x0,[x19,IM_PAGES]
	bl	kput_dec
	adr	x0,msg_pl4
	bl	kputs
	ldr	x0,[x19,IM_RWPAGES]
	bl	kput_dec
	adr	x0,msg_pl5
	bl	kputs
	ldr	x0,[x19,IM_LAZY]
	lsr	x0,x0,10
	bl	kput_dec
	adr	x0,msg_pl6
	bl	kputs
	ldr	x19,[sp],16
	epilogue

; --- SYS_IMAGE_FORGET image_forget(chemin) -> nombre ------------------------
; le fichier de ce chemin change (vfs), ou la commande unload : ses images ne
; sont plus prises pour un lancement (les process lances gardent la leur).
; Ouvert a tous : oublier une image ne fait que la recharger au lancement
; suivant.
sys_image_forget:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,IM_PATH_MAX
	mov	x19,x0
	mov	x0,x19
	mov	x1,sp
	mov	x2,IM_PATH_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.out
	mov	x0,sp
	bl	img_forget
	cbz	x0,.out
	str	x0,[sp,-16]!			; au journal
	adr	x0,msg_pl_forget
	bl	kputs
	add	x0,sp,16
	bl	kputs
	mov	w0,10
	bl	kputc
	ldr	x0,[sp],16
.out:	add	sp,sp,IM_PATH_MAX
	ldr	x19,[sp],16
	epilogue

; --- SYS_SPAWN_PRELOADED spawn_preloaded(chemin, taille, params) -> pid ----
; Comme SPAWN_IMAGE (memes params de 48 o, memes droits), mais depuis l'image
; de ce chemin resolu et de cette taille de fichier ; E_NOENT s'il n'y en a
; pas (AppKit la construit : IMAGE_CREATE, lecture, IMAGE_SEAL). Droits
; demandes : seulement une image IMK_SYS. Au journal : « image <chemin> :
; chargee en N ms » (premier lancement d'une image neuve, depuis
; IMAGE_CREATE) ou « partagee en N ms » (ce lancement).
sys_spawn_preloaded:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,64+IM_PATH_MAX		; nom (32 o), entrees / sorties (24
	mov	x19,x0				; o), chemin
	mov	x20,x1
	mov	x21,x2
	bl	time_ms
	mov	x26,x0
	check_user x21,SPAWN_PARAMS,.fault
	ldp	x0,x1,[x21,24]			; entree, sortie, erreur
	stp	x0,x1,[sp,32]
	ldr	x0,[x21,40]
	str	x0,[sp,48]
	mov	x25,0				; droits
	ldr	x0,[x21,16]
	tbz	x0,0,.nodroits
	current_proc
	ldr	x0,[x1,P_DROITS]
	cbz	x0,.perm
	mov	x25,1
.nodroits:
	mov	x0,x19				; chemin
	add	x1,sp,64
	mov	x2,IM_PATH_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.out
	strb	wzr,[sp]			; nom
	ldr	x0,[x21]
	cbz	x0,.nonom
	mov	x1,sp
	mov	x2,32
	bl	copy_str_from_user
	tbnz	x0,63,.out
.nonom:	mov	x23,0				; arguments (une page)
	ldr	x22,[x21,8]
	cbz	x22,.noargs
	bl	pmm_alloc
	cbz	x0,.nomem
	mov	x23,x0
	mov	x0,x22
	mov	x1,x23
	mov	x2,ARGS_MAX
	bl	copy_str_from_user
	mov	x24,x0
	tbnz	x0,63,.libre
.noargs:
	add	x0,sp,64			; l'image (une reference pour lui)
	mov	x1,x20
	mov	x2,x25
	bl	img_find_ref
	mov	x24,E_NOENT
	cbz	x0,.libre
	mov	x22,x0
	mov	x4,x0
	mov	x0,0
	mov	x1,0
	mov	x2,sp
	mov	x3,x23
	mov	x5,0
	mov	x6,x25
	add	x7,sp,32
	bl	proc_spawn_elf			; (rend la reference s'il echoue)
	mov	x24,x0
	cbz	x24,.echec
	mov	x0,x22				; au journal
	mov	x1,x26
	bl	img_log_spawn
	b	.libre
.echec:	mov	x24,E_NOMEM
.libre:	cbz	x23,.ret
	mov	x0,x23
	bl	pmm_free
.ret:	mov	x0,x24
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.fault:	mov	x0,E_FAULT
.out:	add	sp,sp,64+IM_PATH_MAX
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = image (le process lance en tient une reference), x1 = ms au debut du
; lancement : « image <chemin> : chargee|partagee en N ms »
img_log_spawn:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,img_lock			; premier lancement d'une image neuve ?
	bl	spin_lock_irqsave
	mov	x1,x0
	ldr	x21,[x19,IM_KIND]
	bic	x2,x21,IMK_FRESH
	str	x2,[x19,IM_KIND]
	adr	x0,img_lock
	bl	spin_unlock_irqrestore
	tst	x21,IMK_FRESH
	b.eq	.sh
	ldr	x20,[x19,IM_T0]
.sh:	adr	x0,msg_img
	bl	kputs
	add	x0,x19,IM_PATH
	bl	kputs
	adr	x0,msg_img_sh
	tst	x21,IMK_FRESH
	b.eq	.m
	adr	x0,msg_img_ld
.m:	bl	kputs
	bl	time_ms
	sub	x0,x0,x20
	bl	kput_dec
	adr	x0,msg_img_ms
	bl	kputs
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_IMAGE_LIST image_list(tampon, max) -> nombre -----------------------
; Images valides, entrees de 128 o (IML_*) : +0 taille du fichier, +8 pages
; du modele, +16 dont inscriptibles (copie sur ecriture), +24 references (1
; pour la table + 1 par process lance d'elle), +32 octets de .bss a la
; demande, +40 sorte (IML_PIN : epinglee, IML_SYS : droits de SD0:/sys),
; +48 chemin (80 o, tronque). (IML_* : src/inc/syscalls.inc)
sys_image_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,IMG_MAX
	mov	x0,IMG_MAX
	csel	x20,x0,x20,hi
	lsl	x1,x20,7
	cbz	x20,.vide
	check_user x19,x1,.fault
.vide:	adr	x0,img_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x10,0				; entrees ecrites
	mov	x15,IMK_PIN			; d'abord les epinglees, puis les
.pass:	adr	x21,img_table			; passageres
	mov	x9,IMG_MAX
.s:	cmp	x10,x20
	b.hs	.done
	ldr	x0,[x21,IM_USED]
	cbz	x0,.n
	ldr	x0,[x21,IM_VALID]
	cbz	x0,.n
	ldr	x0,[x21,IM_KIND]
	and	x0,x0,IMK_PIN
	cmp	x0,x15
	b.ne	.n
	add	x11,x19,x10,lsl 7
	ldr	x0,[x21,IM_FSIZE]
	ldp	x1,x2,[x21,IM_PAGES]
	stp	x0,x1,[x11]
	ldr	x0,[x21,IM_REFS]
	stp	x2,x0,[x11,16]
	ldr	x0,[x21,IM_LAZY]
	ldr	x1,[x21,IM_KIND]
	mov	x2,IMK_PIN or IMK_SYS
	and	x1,x1,x2
	stp	x0,x1,[x11,32]
	add	x12,x11,IML_PATH
	add	x13,x21,IM_PATH
	mov	x14,IML_SIZE-IML_PATH-1
.c:	ldrb	w0,[x13],1
	cbz	w0,.ce
	strb	w0,[x12],1
	subs	x14,x14,1
	b.ne	.c
.ce:	strb	wzr,[x12]
	add	x10,x10,1
.n:	add	x21,x21,IM_SIZE
	subs	x9,x9,1
	b.ne	.s
	cbz	x15,.done
	mov	x15,0
	b	.pass
.done:	adr	x0,img_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x10
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (option no_preload) les images sont coupees
preload_disable:
	adr	x0,preload_off
	mov	x1,1
	str	x1,[x0]
	ret

msg_pl		db 'Pr',0xC3,0xA9,'charg',0xC3,0xA9,'       : ',0
msg_pl2		db ' (',0
msg_pl3		db ' Ko : ',0
msg_pl4		db ' pages partag',0xC3,0xA9,'es, dont ',0
msg_pl5		db ' en copie sur ',0xC3,0xA9,'criture ; .bss ',0
msg_pl6		db ' Ko ',0xC3,0xA0,' la demande)',10,0
msg_pl_forget	db 'Image oubli',0xC3,0xA9,'e (fichier chang',0xC3,0xA9,' ou unload) : ',0
msg_img		db 'image ',0
msg_img_ld	db ' : charg',0xC3,0xA9,'e en ',0
msg_img_sh	db ' : partag',0xC3,0xA9,'e en ',0
msg_img_ms	db ' ms',10,0
		align 4
