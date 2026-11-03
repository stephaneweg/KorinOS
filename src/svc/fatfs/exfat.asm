; ============================================================================
;  exfat.asm - systeme de fichiers exFAT (lecture, ecriture, noms longs)
;
;  Reimplementation d'apres la specification Microsoft exFAT. Meme interface
;  que fat32.asm (appels sous fs_mutex, structures V_*, DE_*, IT_*, FO_*).
;
;  Le "cluster" qu'exFAT expose au VFS (DE_CLUSTER, FO_START, V_ROOT...) est
;  une POIGNEE 64 bits :
;     bits 0-31  premier cluster
;     bits 32-61 nombre de clusters (allocation contigue NoFatChain)
;     bit 62     toujours 1 (poignee exFAT : fat_free_chain l'ignore)
;     bit 63     NoFatChain : clusters contigus, la FAT n'est pas utilisee
;  Une poignee nulle = aucune allocation.
;
;  Hypotheses : bitmap d'allocation et table de majuscules contigus (c'est le
;  cas de tous les formateurs), une seule FAT active (la premiere).
; ============================================================================

EX_HANDLE	= 1 shl 62
EX_NOFAT	= 1 shl 63
EX_EOC		= 0xFFFFFFFF

; champs exFAT du volume (dans la zone libre de VOL_SIZE)
V_EX_BMLBA	= 128			; secteur du debut du bitmap d'allocation
V_EX_UPPAGES	= 136			; table de majuscules : 64 Ki x 16 bits (V_UPCASE)

EX_TS		= 0x5D5C6000		; horodatage fixe : 28/10/2026 12:00
EX_MAX_SET	= 19			; entrees max d'un jeu (1 + 1 + 17 noms)

; ---------------------------------------------------------------------------
;  Montage
; ---------------------------------------------------------------------------

; x0 = volume, x1 = premier secteur de la partition -> x0 = 1 si exFAT reconnu
exfat_mount:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x20,[x19,V_PSTART]
	mov	x0,x20
	bl	bc_get
	cbz	x0,.bad
	adr	x1,ex_sig			; "EXFAT   " a l'octet 3
	mov	x2,0
.sig:	add	x3,x0,x2
	ldrb	w3,[x3,3]
	ldrb	w4,[x1,x2]
	cmp	w3,w4
	b.ne	.bad
	add	x2,x2,1
	cmp	x2,8
	b.lo	.sig
	ldrb	w1,[x0,108]			; octets par secteur : 2^9 seulement
	cmp	w1,9
	b.ne	.bad
	ldrb	w1,[x0,109]			; secteurs par cluster (log2)
	cmp	w1,16
	b.hi	.bad
	mov	x2,1
	lslv	x2,x2,x1
	str	x2,[x19,V_SPC]
	add	x2,x1,9
	str	x2,[x19,V_CSHIFT]
	ldr	w1,[x0,80]			; FatOffset
	add	x1,x1,x20
	str	x1,[x19,V_FAT]
	ldr	w1,[x0,84]			; FatLength
	str	x1,[x19,V_FATSZ]
	mov	x1,1
	str	x1,[x19,V_NFATS]
	ldr	w1,[x0,88]			; ClusterHeapOffset
	add	x1,x1,x20
	str	x1,[x19,V_DATA]
	ldr	w1,[x0,92]			; ClusterCount
	str	x1,[x19,V_NCLUST]
	ldr	w1,[x0,96]			; FirstClusterOfRootDirectory
	cmp	w1,2
	b.lo	.bad
	movl	x2,EX_HANDLE
	orr	x1,x1,x2
	str	x1,[x19,V_ROOT]
	mov	x1,2
	str	x1,[x19,V_HINT]
	str	xzr,[x19,V_BITMAP]
	str	xzr,[x19,V_UPCASE]

	; --- bitmap (0x81) et majuscules (0x82) dans la racine -----------------
	mov	x21,0				; index d'entree
.scan:	mov	x0,x19
	ldr	x1,[x19,V_ROOT]
	mov	x2,x21
	bl	ex_entry_at
	cbz	x0,.scanned
	ldrb	w1,[x0]
	cbz	w1,.scanned
	cmp	w1,0x81
	b.ne	.not_bm
	ldr	w1,[x0,20]
	ldr	x2,[x0,24]
	str	x1,[x19,V_BITMAP]
	str	x2,[x19,V_BITMAP_LEN]
	mov	x0,x19
	bl	fat_clus_lba
	str	x0,[x19,V_EX_BMLBA]
	b	.next
.not_bm:
	cmp	w1,0x82
	b.ne	.next
	ldr	w22,[x0,20]			; cluster de la table
	ldr	x23,[x0,24]			; taille en octets
	mov	x0,x19
	mov	x1,x22
	mov	x2,x23
	bl	ex_load_upcase
.next:	add	x21,x21,1
	b	.scan
.scanned:
	ldr	x0,[x19,V_BITMAP]
	cbz	x0,.bad				; pas de bitmap : volume inutilisable
	mov	x0,2
	str	x0,[x19,V_TYPE]
	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

ex_sig		db 'EXFAT   '
		align 4

; x0 = volume, x1 = cluster de la table compressee, x2 = taille en octets
; Decompresse la table (0xFFFF, n : n caracteres identite) dans 128 Ko.
ex_load_upcase:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,32				; 65536 x 2 octets
	bl	pmm_alloc_n
	cbz	x0,.out				; sans table : majuscules ASCII seulement
	mov	x22,x0
	mov	x1,0				; identite par defaut
.id:	strh	w1,[x22,x1,lsl 1]
	add	x1,x1,1
	cmp	x1,0x10000
	b.lo	.id
	mov	x0,x19
	mov	x1,x20
	bl	fat_clus_lba
	mov	x20,x0				; secteur de debut (table contigue)
	mov	x23,0				; offset en octets dans la table
	mov	x24,0				; caractere courant
.w:	cmp	x23,x21
	b.hs	.done
	mov	x0,x23
	bl	.word
	movl	w1,0xFFFF
	cmp	w0,w1
	b.ne	.map
	add	x23,x23,2			; 0xFFFF, n : saut de n caracteres
	cmp	x23,x21
	b.hs	.done
	mov	x0,x23
	bl	.word
	add	x24,x24,x0
	add	x23,x23,2
	b	.w
.map:	cmp	x24,0x10000
	b.hs	.done
	strh	w0,[x22,x24,lsl 1]
	add	x24,x24,1
	add	x23,x23,2
	b	.w
.done:	str	x22,[x19,V_UPCASE]
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; (interne) x0 = offset pair dans la table -> w0 = mot de 16 bits
.word:	stp	x29,x30,[sp,-16]!
	str	x0,[sp,-16]!
	add	x0,x20,x0,lsr 9
	bl	bc_get
	ldr	x1,[sp],16
	cbz	x0,.wz
	and	x1,x1,511
	ldrh	w0,[x0,x1]
	ldp	x29,x30,[sp],16
	ret
.wz:	movl	w0,0xFFFF
	ldp	x29,x30,[sp],16
	ret

; w0 = point de code, x1 = table de majuscules (0 : ASCII) -> w0 = majuscule
ex_upcase:
	cmp	w0,0x10000
	b.hs	.r
	cbz	x1,.ascii
	ldrh	w0,[x1,x0,lsl 1]
.r:	ret
.ascii:	sub	w2,w0,'a'
	cmp	w2,25
	b.hi	.r
	sub	w0,w0,32
	ret

; ---------------------------------------------------------------------------
;  Entrees de repertoire
; ---------------------------------------------------------------------------

; x0 = volume, x1 = poignee du repertoire, x2 = index d'entree
; -> x0 = pointeur (cache) sur l'entree de 32 o, x1 = son secteur ; 0 si hors
;    de l'allocation du repertoire
ex_entry_at:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x3,[x19,V_CSHIFT]
	sub	x3,x3,5				; log2(entrees par cluster)
	lsrv	x21,x2,x3			; rang du cluster
	mov	x4,1
	lslv	x4,x4,x3
	sub	x4,x4,1
	and	x22,x2,x4			; index dans le cluster
	mov	w0,w20				; premier cluster
	cmp	x0,2
	b.lo	.none
	tbz	x20,63,.chain
	ubfx	x1,x20,32,30			; contigu : rang < nombre de clusters
	cmp	x21,x1
	b.hs	.none
	add	x0,x0,x21
	b	.here
.chain:	cbz	x21,.here
	mov	x1,x0
	mov	x0,x19
	bl	fat_next
	movl	x1,FAT_EOC
	cmp	x0,x1
	b.hs	.none
	sub	x21,x21,1
	b	.chain
.here:	mov	x1,x0
	mov	x0,x19
	bl	fat_clus_lba
	add	x20,x0,x22,lsr 4		; 16 entrees par secteur
	mov	x0,x20
	bl	bc_get
	cbz	x0,.none
	and	x1,x22,15
	add	x0,x0,x1,lsl 5
	mov	x1,x20
	b	.out
.none:	mov	x0,0
	mov	x1,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = poignee du repertoire, x2 = index de l'entree 0x85,
; x3 = tampon (EX_MAX_SET x 32 o) -> x0 = nombre d'entrees du jeu (0 si invalide)
ex_read_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,0				; entrees lues
	mov	x24,1				; entrees attendues (mis a jour par la 1re)
.e:	cmp	x23,x24
	b.hs	.ok
	mov	x0,x19
	mov	x1,x20
	add	x2,x21,x23
	bl	ex_entry_at
	cbz	x0,.bad
	add	x1,x22,x23,lsl 5
	ldp	x2,x3,[x0]
	stp	x2,x3,[x1]
	ldp	x2,x3,[x0,16]
	stp	x2,x3,[x1,16]
	cbnz	x23,.n
	ldrb	w2,[x0]
	and	w2,w2,0x7F
	cmp	w2,0x05				; entree fichier (en service ou non)
	b.ne	.bad
	ldrb	w2,[x0,1]
	add	x24,x2,1
	cmp	x24,EX_MAX_SET
	b.hi	.bad
	cmp	x24,3
	b.lo	.bad
.n:	add	x23,x23,1
	b	.e
.ok:	mov	x0,x24
	b	.out
.bad:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = jeu d'entrees, x1 = nombre d'entrees : calcule et range SetChecksum
ex_set_checksum:
	lsl	x1,x1,5
	mov	x2,0
	mov	w3,0
.b:	cmp	x2,x1
	b.hs	.done
	cmp	x2,2				; octets 2-3 : le checksum lui-meme
	b.eq	.skip
	cmp	x2,3
	b.eq	.skip
	ubfx	w4,w3,1,15
	tst	w3,1
	b.eq	.add
	orr	w4,w4,0x8000
.add:	ldrb	w5,[x0,x2]
	add	w3,w4,w5
	and	w3,w3,0xFFFF
.skip:	add	x2,x2,1
	b	.b
.done:	strh	w3,[x0,2]
	ret

; x0 = volume, x1 = poignee du repertoire, x2 = index, x3 = jeu, x4 = nombre :
; recalcule le checksum et ecrit les entrees sur disque -> x0 = 1 si succes
ex_write_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x0,x22
	mov	x1,x23
	bl	ex_set_checksum
	mov	x24,0
.e:	cmp	x24,x23
	b.hs	.ok
	mov	x0,x19
	mov	x1,x20
	add	x2,x21,x24
	bl	ex_entry_at
	cbz	x0,.bad
	add	x2,x22,x24,lsl 5
	ldp	x3,x4,[x2]
	stp	x3,x4,[x0]
	ldp	x3,x4,[x2,16]
	stp	x3,x4,[x0,16]
	mov	x0,x1
	bl	bc_write
	add	x24,x24,1
	b	.e
.ok:	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = premier cluster, x2 = drapeaux du flux (bit 1 NoFatChain),
; x3 = DataLength -> x0 = poignee (0 si aucun cluster)
ex_make_handle:
	cmp	w1,2
	b.lo	.zero
	mov	w4,w1
	movl	x5,EX_HANDLE
	orr	x4,x4,x5
	tbz	x2,1,.done
	ldr	x5,[x0,V_CSHIFT]		; contigu : nombre de clusters de l'allocation
	mov	x6,1
	lslv	x6,x6,x5
	sub	x6,x6,1
	add	x3,x3,x6
	lsrv	x3,x3,x5
	ubfx	x3,x3,0,30
	orr	x4,x4,x3,lsl 32
	movl	x5,EX_NOFAT
	orr	x4,x4,x5
.done:	mov	x0,x4
	ret
.zero:	mov	x0,0
	ret

; x0 = iterateur (IT_*), x1 = DE a remplir -> x0 = 1 si une entree lue, 0 en fin
exfat_dir_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,EX_MAX_SET*32+16
	mov	x19,x0
	mov	x20,x1
	ldr	x21,[x19,IT_VOL]
	ldr	x0,[x19,IT_START]
	str	x0,[x20,DE_DIR]
.next:	ldr	x22,[x19,IT_INDEX]
	mov	x0,x21
	ldr	x1,[x19,IT_START]
	mov	x2,x22
	bl	ex_entry_at
	cbz	x0,.end
	ldrb	w1,[x0]
	cbz	w1,.end				; fin de repertoire
	add	x2,x22,1
	str	x2,[x19,IT_INDEX]
	cmp	w1,0x85
	b.ne	.next
	mov	x0,x21				; jeu complet
	ldr	x1,[x19,IT_START]
	mov	x2,x22
	mov	x3,sp
	bl	ex_read_set
	cbz	x0,.next
	mov	x23,x0
	add	x1,x22,x23
	str	x1,[x19,IT_INDEX]
	ldrb	w1,[sp,32]			; flux 0xC0 obligatoire
	cmp	w1,0xC0
	b.ne	.next
	str	x22,[x20,DE_INDEX]
	str	x22,[x20,DE_FIRST]
	ldrh	w1,[sp,4]
	str	x1,[x20,DE_ATTR]
	ldr	x3,[sp,32+24]			; DataLength
	str	x3,[x20,DE_FSIZE]
	ldr	w4,[sp,12]			; derniere ecriture (x3 : ex_make_handle)
	str	x4,[x20,DE_MTIME]
	mov	x0,x21
	ldr	w1,[sp,32+20]
	ldrb	w2,[sp,32+1]
	bl	ex_make_handle
	str	x0,[x20,DE_CLUSTER]
	; nom : entrees 0xC1 consecutives, 15 caracteres chacune, en place
	ldrb	w24,[sp,32+3]			; longueur du nom
	mov	x1,2				; 1re entree de nom
	mov	x2,0				; caracteres recopies
.nm:	cmp	x1,x23
	b.hs	.nm_end
	lsl	x3,x1,5
	add	x3,sp,x3
	ldrb	w4,[x3]
	cmp	w4,0xC1
	b.ne	.nm_end
	mov	x5,0
.ch:	cmp	x5,15
	b.hs	.nm_next
	cmp	x2,x24
	b.hs	.nm_end
	add	x6,x3,2
	ldrh	w7,[x6,x5,lsl 1]
	strh	w7,[sp,x2,lsl 1]		; compactage au debut du tampon
	add	x2,x2,1
	add	x5,x5,1
	b	.ch
.nm_next:
	add	x1,x1,1
	b	.nm
.nm_end:
	strh	wzr,[sp,x2,lsl 1]
	add	x0,x20,DE_NAME
	mov	x1,sp
	mov	x2,255
	bl	utf16_to_utf8
	mov	x0,1
	b	.out
.end:	mov	x0,0
.out:	add	sp,sp,EX_MAX_SET*32+16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine UTF-8 -> w0 = point de code, x1 = suite ('/' rendu comme 0)
ex_u8next:
	ldrb	w2,[x0],1
	cmp	w2,'/'
	b.ne	.nz
	mov	w2,0
.nz:	tbz	w2,7,.one
	and	w3,w2,0xE0
	cmp	w3,0xC0
	b.ne	.three
	and	w2,w2,0x1F
	ldrb	w3,[x0],1
	and	w3,w3,0x3F
	orr	w2,w3,w2,lsl 6
	b	.one
.three:	and	w2,w2,0x0F
	ldrb	w3,[x0],1
	and	w3,w3,0x3F
	orr	w2,w3,w2,lsl 6
	ldrb	w3,[x0],1
	and	w3,w3,0x3F
	orr	w2,w3,w2,lsl 6
.one:	mov	x1,x0
	mov	w0,w2
	ret

; x0 = volume, x1 = nom 1 (fin 0 ou '/'), x2 = nom 2 -> x0 = 1 si egaux
; (insensible a la casse via la table de majuscules du volume)
ex_name_equal:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x19,[x0,V_UPCASE]
	mov	x20,x1
	mov	x21,x2
.c:	mov	x0,x20
	bl	ex_u8next
	mov	x20,x1
	mov	x1,x19
	bl	ex_upcase
	mov	w22,w0
	mov	x0,x21
	bl	ex_u8next
	mov	x21,x1
	mov	x1,x19
	bl	ex_upcase
	cmp	w0,w22
	b.ne	.no
	cbnz	w0,.c
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = chemin relatif au volume, x2 = DE -> x0 = 1 si trouve
exfat_lookup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,IT_SIZE
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	x22,[x19,V_ROOT]
	str	x22,[x21,DE_CLUSTER]		; racine synthetique
	str	xzr,[x21,DE_FSIZE]
	str	xzr,[x21,DE_MTIME]
	mov	x0,ATTR_DIR
	str	x0,[x21,DE_ATTR]
	str	xzr,[x21,DE_DIR]
	strb	wzr,[x21,DE_NAME]
.comp:	ldrb	w0,[x20]
	cmp	w0,'/'
	b.ne	.nz
	add	x20,x20,1
	b	.comp
.nz:	cbz	w0,.found
	ldr	x0,[x21,DE_ATTR]
	tst	x0,ATTR_DIR
	b.eq	.missing
	mov	x0,sp				; parcours du repertoire x22
	mov	x1,x19
	mov	x2,x22
	bl	dir_open
.find:	mov	x0,sp
	mov	x1,x21
	bl	exfat_dir_read
	cbz	x0,.missing
	mov	x0,x19
	mov	x1,x20
	add	x2,x21,DE_NAME
	bl	ex_name_equal
	cbz	x0,.find
	ldr	x22,[x21,DE_CLUSTER]
.skip:	ldrb	w0,[x20],1
	cbz	w0,.found
	cmp	w0,'/'
	b.ne	.skip
	b	.comp
.found:	mov	x0,1
	b	.out
.missing:
	mov	x0,0
.out:	add	sp,sp,IT_SIZE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Allocation : bitmap + FAT
; ---------------------------------------------------------------------------

; x0 = volume, x1 = cluster -> x0 = secteur du bitmap, x1 = octet, x2 = bit
ex_bm_locate:
	sub	x1,x1,2
	and	x2,x1,7
	lsr	x1,x1,3
	ldr	x3,[x0,V_EX_BMLBA]
	add	x0,x3,x1,lsr 9
	and	x1,x1,511
	ret

; x0 = volume, x1 = cluster -> x0 = 1 si alloue
ex_bm_get:
	prologue
	bl	ex_bm_locate
	stp	x1,x2,[sp,-16]!
	bl	bc_get
	ldp	x1,x2,[sp],16
	cbz	x0,.used
	ldrb	w0,[x0,x1]
	lsrv	w0,w0,w2
	and	w0,w0,1
	epilogue
.used:	mov	x0,1				; erreur de lecture : prudence
	epilogue

; x0 = volume, x1 = cluster, x2 = 0 libre / 1 alloue
ex_bm_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x2
	bl	ex_bm_locate
	mov	x20,x0
	stp	x1,x2,[sp,-16]!
	bl	bc_get
	ldp	x1,x2,[sp],16
	cbz	x0,.out
	ldrb	w3,[x0,x1]
	mov	w4,1
	lslv	w4,w4,w2
	bic	w3,w3,w4
	cbz	x19,.st
	orr	w3,w3,w4
.st:	strb	w3,[x0,x1]
	mov	x0,x20
	bl	bc_write
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = cluster, w2 = valeur 32 bits : ecrit l'entree de FAT
ex_fat_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	w20,w2
	ldr	x0,[x0,V_FAT]
	add	x0,x0,x1,lsr 7
	str	x0,[sp,-16]!
	bl	bc_get
	ldr	x1,[sp],16
	cbz	x0,.out
	and	x2,x19,127
	str	w20,[x0,x2,lsl 2]
	mov	x0,x1
	bl	bc_write
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = cluster : contenu a zero
ex_zero_cluster:
	prologue
	str	x19,[sp,-16]!
	ldr	x19,[x0,V_SPC]
	bl	fat_clus_lba
	mov	x1,x19
	bl	blk_zero
	ldr	x19,[sp],16
	epilogue

; x0 = volume -> x0 = cluster libre, marque alloue et mis a zero (0 si plein)
ex_alloc_cluster:
	mov	x1,0
; (x1 = 1 : pas de zeros, le cluster va etre ecrit en entier)
ex_alloc_cluster_x:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x23,x1
	ldr	x20,[x19,V_HINT]
	ldr	x21,[x19,V_NCLUST]
	add	x21,x21,2
	sub	x22,x21,2			; essais
.s:	cbz	x22,.full
	sub	x22,x22,1
	cmp	x20,x21
	b.lo	.chk
	mov	x20,2
.chk:	mov	x0,x19
	mov	x1,x20
	bl	ex_bm_get
	cbz	x0,.got
	add	x20,x20,1
	b	.s
.got:	mov	x0,x19
	mov	x1,x20
	mov	x2,1
	bl	ex_bm_set
	cbnz	x23,.nz
	mov	x0,x19
	mov	x1,x20
	bl	ex_zero_cluster
.nz:	add	x0,x20,1
	str	x0,[x19,V_HINT]
	mov	x0,x20
	b	.out
.full:	mov	x0,0
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = poignee, x2 = DataLength : libere toute l'allocation (bitmap
; et FAT ecrits une fois par secteur, a la fin)
ex_free_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x22,x2
	bl	bc_defer_on
	mov	x2,x22
	mov	w21,w20				; premier cluster
	cmp	x21,2
	b.lo	.out
	tbz	x20,63,.chain
	ubfx	x22,x20,32,30			; contigu
	cbnz	x22,.cont
	ldr	x3,[x19,V_CSHIFT]		; nombre inconnu : d'apres la taille
	mov	x4,1
	lslv	x4,x4,x3
	sub	x4,x4,1
	add	x22,x2,x4
	lsrv	x22,x22,x3
.cont:	cbz	x22,.out
	mov	x0,x19
	mov	x1,x21
	mov	x2,0
	bl	ex_bm_set
	add	x21,x21,1
	sub	x22,x22,1
	b	.cont
.chain:	ldr	x0,[x19,V_NCLUST]
	add	x22,x0,2			; garde-fou contre les boucles
.c:	cmp	x21,2
	b.lo	.out
	movl	x0,FAT_EOC
	cmp	x21,x0
	b.hs	.out
	cbz	x22,.out
	sub	x22,x22,1
	mov	x0,x19
	mov	x1,x21
	bl	fat_next
	mov	x20,x0
	mov	x0,x19
	mov	x1,x21
	mov	x2,0
	bl	ex_bm_set
	mov	x0,x19
	mov	x1,x21
	mov	w2,0
	bl	ex_fat_set
	mov	x21,x20
	b	.c
.out:	bl	bc_defer_off
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Fichiers
; ---------------------------------------------------------------------------

; x0 = objet fichier, x1 = rang de cluster, x2 = 1 pour allouer si absent,
; x3 = 1 : ce cluster sera ecrit en entier, sans zeros s'il est neuf
; -> x0 = cluster (0 si hors fichier / disque plein). Met a jour FO_START
; (poignee) et FO_DIRTY quand l'allocation change.
ex_cluster_at:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x25,x3
	ldr	x22,[x19,FO_VOL]
	ldr	x23,[x19,FO_START]
	mov	w0,w23
	cmp	x0,2
	b.hs	.has
	cbz	x21,.fail			; fichier vide : premier cluster, contigu
	mov	x0,x22
	cmp	x20,0
	csel	x1,x25,xzr,eq
	bl	ex_alloc_cluster_x
	cbz	x0,.fail
	movl	x1,EX_HANDLE or EX_NOFAT or (1 shl 32)
	orr	x23,x0,x1
	str	x23,[x19,FO_START]
	str	xzr,[x19,FO_CCLUS]
	mov	x0,1
	str	x0,[x19,FO_DIRTY]
.has:	tbz	x23,63,.chain
	; --- allocation contigue (NoFatChain) --------------------------------
.cont:	ubfx	x24,x23,32,30			; nombre de clusters
	mov	w0,w23
	cmp	x20,x24
	b.lo	.cont_ok
	cbz	x21,.fail
	add	x1,x0,x24			; le cluster suivant est-il libre ?
	ldr	x2,[x22,V_NCLUST]
	add	x2,x2,2
	cmp	x1,x2
	b.hs	.to_chain
	str	x1,[sp,-16]!
	mov	x0,x22
	bl	ex_bm_get
	ldr	x1,[sp],16
	cbnz	x0,.to_chain
	mov	x0,x22
	mov	x2,1
	str	x1,[sp,-16]!
	bl	ex_bm_set
	ldr	x1,[sp],16
	cmp	x20,x24
	b.ne	.zc
	cbnz	x25,.nzc
.zc:	mov	x0,x22
	bl	ex_zero_cluster
.nzc:	movl	x1,1 shl 32			; un cluster de plus
	add	x23,x23,x1
	str	x23,[x19,FO_START]
	mov	x0,1
	str	x0,[x19,FO_DIRTY]
	b	.cont
.cont_ok:
	add	x0,x0,x20
	b	.out
.to_chain:					; plus de place contigue : chaine FAT
	mov	w0,w23
	mov	x1,0
.link:	add	x2,x1,1
	cmp	x2,x24
	b.hs	.last
	stp	x0,x1,[sp,-16]!
	add	x1,x0,x1
	add	w2,w1,1
	mov	x0,x22
	bl	ex_fat_set
	ldp	x0,x1,[sp],16
	add	x1,x1,1
	b	.link
.last:	stp	x0,x1,[sp,-16]!
	add	x1,x0,x1
	movl	w2,EX_EOC
	mov	x0,x22
	bl	ex_fat_set
	ldp	x0,x1,[sp],16
	mov	w23,w23				; poignee chainee : cluster | EX_HANDLE
	movl	x1,EX_HANDLE
	orr	x23,x23,x1
	str	x23,[x19,FO_START]
	str	xzr,[x19,FO_CCLUS]
	mov	x0,1
	str	x0,[x19,FO_DIRTY]
	; --- chaine FAT ---------------------------------------------------------
.chain:	ldr	x0,[x19,FO_CCLUS]
	ldr	x1,[x19,FO_CIDX]
	cbz	x0,.rewind
	cmp	x1,x20
	b.ls	.walk
.rewind:
	mov	w0,w23
	mov	x1,0
.walk:	cmp	x1,x20
	b.hs	.here
	stp	x0,x1,[sp,-16]!
	mov	x1,x0
	mov	x0,x22
	bl	fat_next
	ldp	x2,x1,[sp],16
	movl	x3,FAT_EOC
	cmp	x0,x3
	b.lo	.step
	cbz	x21,.fail			; fin de chaine en lecture
	stp	x1,x2,[sp,-16]!			; rang, cluster precedent
	add	x3,x1,1
	cmp	x3,x20
	csel	x1,x25,xzr,eq
	mov	x0,x22
	bl	ex_alloc_cluster_x
	cbz	x0,.fail_pop
	str	x0,[sp,-16]!
	mov	x1,x0				; nouveau = fin de chaine
	movl	w2,EX_EOC
	mov	x0,x22
	bl	ex_fat_set
	ldr	x2,[sp]				; precedent -> nouveau
	ldr	x1,[sp,24]
	mov	x0,x22
	bl	ex_fat_set
	ldr	x0,[sp],16
	ldp	x1,x2,[sp],16
.step:	add	x1,x1,1
	b	.walk
.here:	str	x0,[x19,FO_CCLUS]
	str	x1,[x19,FO_CIDX]
	b	.out
.fail_pop:
	add	sp,sp,16
.fail:	mov	x0,0
.out:	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = objet fichier, x1 = tampon, x2 = octets, x3 = 0 lire / 1 ecrire
; -> x0 = octets transferes
exfat_rw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x26,0
	ldr	x23,[x19,FO_VOL]
	cbnz	x22,.loop
	ldr	x0,[x19,FO_SIZE]		; lecture : bornee par la taille
	ldr	x1,[x19,FO_POS]
	subs	x0,x0,x1
	csel	x0,xzr,x0,lo
	cmp	x21,x0
	csel	x21,x0,x21,hi
.loop:	cbz	x21,.done
	ldr	x24,[x19,FO_POS]
	ldr	x0,[x23,V_CSHIFT]
	lsrv	x1,x24,x0
	mov	x3,0				; ecrit en entier ?
	cbz	x22,.seek
	mov	x2,1
	lslv	x2,x2,x0
	cmp	x21,x2
	b.lo	.seek
	sub	x2,x2,1
	tst	x24,x2
	b.ne	.seek
	mov	x3,1
.seek:	mov	x0,x19
	mov	x2,x22
	bl	ex_cluster_at
	cbz	x0,.done
	mov	x1,x0
	mov	x0,x23
	bl	fat_clus_lba
	mov	x2,1
	lslv	x2,x2,x1
	sub	x2,x2,1
	and	x3,x24,x2
	add	x25,x0,x3,lsr 9
	and	x4,x24,511
	cbnz	x4,.partial
	cmp	x21,512
	b.lo	.partial
	sub	x5,x2,x3			; secteurs entiers jusqu'a la fin du cluster
	add	x5,x5,1
	lsr	x5,x5,9
	lsr	x6,x21,9
	cmp	x6,x5
	csel	x6,x5,x6,hi
.more:	lsr	x7,x21,9			; les clusters contigus avec
	cmp	x6,x7				; (une seule commande de la carte)
	b.hs	.run
	add	x1,x24,x5,lsl 9			; le suivant
	ldr	x2,[x23,V_CSHIFT]
	lsrv	x1,x1,x2
	mov	x2,0				; alloue s'il sera ecrit en entier
	cbz	x22,.next
	sub	x7,x7,x5
	ldr	x3,[x23,V_SPC]
	cmp	x7,x3
	cset	x2,hs
.next:	mov	x3,x2
	stp	x5,x6,[sp,-16]!
	mov	x0,x19
	bl	ex_cluster_at
	ldp	x5,x6,[sp],16
	cbz	x0,.run
	mov	x1,x0
	mov	x0,x23
	bl	fat_clus_lba
	add	x1,x25,x5
	cmp	x0,x1
	b.ne	.run
	ldr	x1,[x23,V_SPC]
	add	x5,x5,x1
	lsr	x6,x21,9
	cmp	x6,x5
	csel	x6,x5,x6,hi
	b	.more
.run:	mov	x0,x25
	mov	x1,x6
	mov	x2,x20
	lsl	x24,x6,9
	cbnz	x22,.wfull
	bl	blk_read
	b	.full_done
.wfull:	bl	blk_write
.full_done:
	cbz	x0,.done
	mov	x4,x24
	b	.advance
.partial:
	mov	x0,x25
	str	x4,[sp,-16]!
	bl	bc_get
	ldr	x4,[sp],16
	cbz	x0,.done
	mov	x5,512
	sub	x5,x5,x4
	cmp	x5,x21
	csel	x5,x21,x5,hi
	add	x6,x0,x4
	mov	x7,0
	cbnz	x22,.wpart
.rp:	ldrb	w8,[x6,x7]
	strb	w8,[x20,x7]
	add	x7,x7,1
	cmp	x7,x5
	b.lo	.rp
	mov	x4,x5
	b	.advance
.wpart:	ldrb	w8,[x20,x7]
	strb	w8,[x6,x7]
	add	x7,x7,1
	cmp	x7,x5
	b.lo	.wpart
	str	x5,[sp,-16]!
	mov	x0,x25
	bl	bc_write
	ldr	x4,[sp],16
.advance:
	add	x20,x20,x4
	sub	x21,x21,x4
	add	x26,x26,x4
	ldr	x0,[x19,FO_POS]
	add	x0,x0,x4
	str	x0,[x19,FO_POS]
	ldr	x1,[x19,FO_SIZE]
	cmp	x0,x1
	b.ls	.loop
	str	x0,[x19,FO_SIZE]
	mov	x0,1
	str	x0,[x19,FO_DIRTY]
	b	.loop
.done:	mov	x0,x26
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = DE : ecrit premier cluster (poignee), taille, attributs.
; Si l'allocation a change (troncature), l'ancienne est liberee.
exfat_update_entry:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,EX_MAX_SET*32
	mov	x19,x0
	mov	x20,x1
	ldr	x1,[x20,DE_DIR]
	ldr	x2,[x20,DE_INDEX]
	mov	x3,sp
	bl	ex_read_set
	cbz	x0,.out
	mov	x21,x0
	ldr	w1,[sp,32+20]			; ancienne allocation
	ldr	x22,[x20,DE_CLUSTER]
	mov	w2,w22
	cmp	w1,w2
	b.eq	.same
	cbz	w1,.same
	mov	x0,x19
	ldrb	w2,[sp,32+1]
	ldr	x3,[sp,32+24]
	bl	ex_make_handle
	mov	x1,x0
	mov	x0,x19
	ldr	x2,[sp,32+24]
	bl	ex_free_alloc
.same:	mov	w1,1				; AllocationPossible
	tbz	x22,63,.fl
	orr	w1,w1,2				; NoFatChain
.fl:	strb	w1,[sp,32+1]
	str	w22,[sp,32+20]
	ldr	x1,[x20,DE_FSIZE]
	ldr	x2,[x20,DE_ATTR]
	tst	x2,ATTR_DIR
	b.eq	.taille
	ldr	x3,[sp,32+24]
	cbnz	x3,.dir				; repertoire : taille = allocation
.taille:				; (sauf une entree neuve : rename)
	str	x1,[sp,32+8]			; ValidDataLength
	str	x1,[sp,32+24]			; DataLength
.dir:	strh	w2,[sp,4]
	bl	dos_maintenant
	str	w0,[sp,12]			; derniere ecriture, dernier acces
	str	w0,[sp,16]
	strb	wzr,[sp,21]
	strb	wzr,[sp,23]
	mov	x0,x19
	ldr	x1,[x20,DE_DIR]
	ldr	x2,[x20,DE_INDEX]
	mov	x3,sp
	mov	x4,x21
	bl	ex_write_set
.out:	add	sp,sp,EX_MAX_SET*32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = DE : supprime l'entree et libere son allocation
exfat_erase:
	mov	x2,1
	b	ex_erase
; x0 = volume, x1 = DE : supprime l'entree seulement (fichier encore ouvert :
; son allocation est liberee a sa derniere fermeture, fatfs.asm)
exfat_erase_entries:
	mov	x2,0
; x2 = 1 : liberer aussi l'allocation
ex_erase:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,EX_MAX_SET*32
	mov	x19,x0
	mov	x20,x1
	mov	x23,x2
	ldr	x1,[x20,DE_DIR]
	ldr	x2,[x20,DE_INDEX]
	mov	x3,sp
	bl	ex_read_set
	cbz	x0,.out
	mov	x21,x0
	cbz	x23,.entries
	mov	x0,x19				; liberer l'allocation
	ldr	w1,[sp,32+20]
	ldrb	w2,[sp,32+1]
	ldr	x3,[sp,32+24]
	bl	ex_make_handle
	mov	x1,x0
	mov	x0,x19
	ldr	x2,[sp,32+24]
	bl	ex_free_alloc
.entries:
	mov	x22,0				; InUse = 0 sur chaque entree
.e:	cmp	x22,x21
	b.hs	.out
	mov	x0,x19
	ldr	x1,[x20,DE_DIR]
	ldr	x2,[x20,DE_INDEX]
	add	x2,x2,x22
	bl	ex_entry_at
	cbz	x0,.out
	ldrb	w2,[x0]
	and	w2,w2,0x7F
	strb	w2,[x0]
	mov	x0,x1
	bl	bc_write
	add	x22,x22,1
	b	.e
.out:	add	sp,sp,EX_MAX_SET*32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = poignee du repertoire parent, x2 = nom UTF-8 (fin 0 ou '/'),
; x3 = attributs, x4 = DE a remplir -> x0 = 1 si cree (fichier vide, ou
; repertoire d'un cluster)
exfat_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,EX_MAX_SET*32+528		; [sp] jeu, [sp+608] nom UTF-16
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	add	x0,x23,DE_NAME			; composant -> DE_NAME
	mov	x1,x21
	mov	x2,NAME_BYTES-2
.cp:	ldrb	w3,[x1],1
	cmp	w3,'/'
	csel	w3,wzr,w3,eq
	strb	w3,[x0],1
	cbz	w3,.cp_done
	subs	x2,x2,1
	b.ne	.cp
	b	.fail				; nom trop long
.cp_done:
	add	x0,x23,DE_NAME
	ldrb	w1,[x0]
	cbz	w1,.fail
	add	x1,sp,EX_MAX_SET*32
	mov	x2,NAME_UNITS+1
	bl	utf8_to_utf16
	mov	x24,x0				; longueur du nom
	cmp	x24,NAME_UNITS			; 255 caracteres au plus
	b.hi	.fail
	add	x0,x24,14
	mov	x1,15
	udiv	x25,x0,x1			; entrees de nom
	add	x26,x25,2			; taille du jeu

	; --- jeu d'entrees -----------------------------------------------------
	mov	x0,sp
	mov	x1,EX_MAX_SET*32
	bl	mem_zero_small
	mov	w0,0x85
	strb	w0,[sp]
	add	w0,w25,1
	strb	w0,[sp,1]
	strh	w22,[sp,4]			; attributs
	movl	w0,EX_TS
	str	w0,[sp,8]
	str	w0,[sp,12]
	str	w0,[sp,16]
	mov	w0,0xC0
	strb	w0,[sp,32]
	mov	w0,1				; AllocationPossible
	strb	w0,[sp,33]
	strb	w24,[sp,35]
	mov	x2,0				; entrees de nom
.ne:	cmp	x2,x25
	b.hs	.ne_done
	add	x3,x2,2
	lsl	x3,x3,5
	add	x3,sp,x3
	mov	w4,0xC1
	strb	w4,[x3]
	mov	x5,0
.nc:	cmp	x5,15
	b.hs	.ne_next
	mov	x6,15
	madd	x6,x2,x6,x5			; caractere absolu
	cmp	x6,x24
	b.hs	.ne_next
	add	x7,sp,EX_MAX_SET*32
	ldrh	w7,[x7,x6,lsl 1]
	add	x8,x3,2
	strh	w7,[x8,x5,lsl 1]
	add	x5,x5,1
	b	.nc
.ne_next:
	add	x2,x2,1
	b	.ne
.ne_done:
	; NameHash sur les caracteres en majuscules
	ldr	x9,[x19,V_UPCASE]
	mov	x6,0
	mov	w10,0
.h:	cmp	x6,x24
	b.hs	.h_done
	add	x7,sp,EX_MAX_SET*32
	ldrh	w0,[x7,x6,lsl 1]
	mov	x1,x9
	bl	ex_upcase
	and	w2,w0,0xFF
	bl	.hash_byte
	lsr	w2,w0,8
	bl	.hash_byte
	add	x6,x6,1
	b	.h
.h_done:
	strh	w10,[sp,36]

	; --- repertoire : un cluster vide, chaine ------------------------------
	mov	x21,0				; poignee du nouvel element
	tst	x22,ATTR_DIR
	b.eq	.place
	mov	x0,x19
	bl	ex_alloc_cluster
	cbz	x0,.fail
	mov	x21,x0
	mov	x0,x19
	mov	x1,x21
	movl	w2,EX_EOC
	bl	ex_fat_set
	str	w21,[sp,32+20]
	ldr	x0,[x19,V_CSHIFT]
	mov	x1,1
	lslv	x1,x1,x0
	str	x1,[sp,32+8]
	str	x1,[sp,32+24]
	movl	x0,EX_HANDLE
	orr	x21,x21,x0

	; --- place libre : x26 entrees consecutives -----------------------------
.place:	mov	x24,0				; index examine
	mov	x25,0				; longueur de la suite libre
	mov	x22,0				; debut de la suite
.fs:	mov	x0,x19
	mov	x1,x20
	mov	x2,x24
	bl	ex_entry_at
	cbz	x0,.grow
	ldrb	w0,[x0]
	tbnz	w0,7,.busy
	cbnz	x25,.inc
	mov	x22,x24
.inc:	add	x25,x25,1
	cmp	x25,x26
	b.hs	.write
	add	x24,x24,1
	b	.fs
.busy:	mov	x25,0
	add	x24,x24,1
	b	.fs
.grow:	; seul un repertoire chaine sans taille (la racine) peut grandir ici
	ldr	x0,[x19,V_ROOT]
	cmp	x0,x20
	b.ne	.fail_free
	mov	w0,w20				; dernier cluster de la chaine
.last:	mov	x1,x0
	str	x1,[sp,-16]!
	mov	x0,x19
	bl	fat_next
	ldr	x1,[sp],16
	movl	x2,FAT_EOC
	cmp	x0,x2
	b.lo	.last
	str	x1,[sp,-16]!
	mov	x0,x19
	bl	ex_alloc_cluster
	ldr	x1,[sp],16
	cbz	x0,.fail_free
	stp	x0,x1,[sp,-16]!
	mov	x1,x0
	movl	w2,EX_EOC
	mov	x0,x19
	bl	ex_fat_set
	ldp	x2,x1,[sp],16
	mov	x0,x19
	bl	ex_fat_set			; precedent -> nouveau (w2)
	b	.fs

.write:	bl	dos_maintenant			; creation, derniere ecriture, dernier acces
	str	w0,[sp,8]
	str	w0,[sp,12]
	str	w0,[sp,16]
	mov	x0,x19
	mov	x1,x20
	mov	x2,x22
	mov	x3,sp
	mov	x4,x26
	bl	ex_write_set
	cbz	x0,.fail_free
	str	x20,[x23,DE_DIR]
	str	x22,[x23,DE_INDEX]
	str	x22,[x23,DE_FIRST]
	str	x21,[x23,DE_CLUSTER]
	ldr	x0,[sp,32+24]
	str	x0,[x23,DE_FSIZE]
	ldrh	w0,[sp,4]
	str	x0,[x23,DE_ATTR]
	mov	x0,1
	b	.out
.fail_free:
	cbz	x21,.fail
	mov	x0,x19
	mov	x1,x21
	mov	x2,0
	bl	ex_free_alloc
.fail:	mov	x0,0
.out:	add	sp,sp,EX_MAX_SET*32+528
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
; (interne) w10 = hash, w2 = octet : hash = ror16(hash) + octet
.hash_byte:
	ubfx	w11,w10,1,15
	tst	w10,1
	b.eq	.hb
	orr	w11,w11,0x8000
.hb:	add	w10,w11,w2
	and	w10,w10,0xFFFF
	ret
