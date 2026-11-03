; ============================================================================
;  fat32.asm - systeme de fichiers FAT32 (lecture, ecriture, noms longs)
;
;  Reimplementation d'apres la specification Microsoft FAT (fatgen103).
;  Toutes les fonctions s'appellent sous fs_mutex (vfs.asm).
;  Noms : UTF-8 cote OS <-> UTF-16 (LFN) sur disque ; comparaison insensible
;  a la casse pour l'ASCII. Les entrees 8.3 sont generees en NOM~n.EXT.
; ============================================================================

; --- volume (VOL_SIZE octets) ------------------------------------------------
V_TYPE		= 0			; 0 aucun, 1 FAT32, 2 exFAT
V_PSTART	= 8			; premier secteur de la partition
V_SPC		= 16			; secteurs par cluster
V_CSHIFT	= 24			; log2(octets par cluster)
V_FAT		= 32			; secteur de la 1re FAT
V_FATSZ		= 40			; secteurs par FAT
V_NFATS		= 48
V_DATA		= 56			; secteur du cluster 2
V_ROOT		= 64			; cluster de la racine
V_NCLUST	= 72			; nombre de clusters de donnees
V_HINT		= 80			; recherche du prochain cluster libre
V_FREE		= 88			; FAT32 : clusters libres (-1 : inconnu), FSInfo
V_BITMAP	= 96			; exFAT : cluster du bitmap d'allocation
V_BITMAP_LEN	= 104
V_UPCASE	= 112			; exFAT : table de majuscules
V_NAME		= 120			; nom de montage (8 octets)
V_FSINFO	= 144			; FAT32 : secteur FSInfo (0 : aucun)
V_FSI_DIRTY	= 152			; FAT32 : V_FREE a reecrire dans FSInfo
VOL_SIZE	= 256

FAT_EOC		= 0x0FFFFFF8
FAT_MASK	= 0x0FFFFFFF

ATTR_RO		= 0x01
ATTR_HIDDEN	= 0x02
ATTR_SYSTEM	= 0x04
ATTR_VOLUME	= 0x08
ATTR_DIR	= 0x10
ATTR_ARCHIVE	= 0x20
ATTR_LFN	= 0x0F

; --- description d'une entree de repertoire (DE_SIZE octets) -----------------
DE_CLUSTER	= 0			; premier cluster du fichier
DE_FSIZE	= 8
DE_ATTR		= 16
DE_DIR		= 24			; premier cluster du repertoire parent
DE_INDEX	= 32			; index (entrees de 32 o) de l'entree 8.3
DE_MTIME	= 48			; date << 16 | heure de la derniere ecriture (dostime.inc)
DE_FIRST	= 40			; index de la 1re entree LFN (= DE_INDEX si aucune)
DE_NAME		= 64			; nom UTF-8, NAME_BYTES octets
NAME_BYTES	= 768			; 255 caracteres UTF-16 -> 765 octets UTF-8 au plus
DE_LFN		= DE_NAME + NAME_BYTES	; nom long UTF-16 en cours de lecture (dir_read)
DE_LFN_SIZE	= 528			; 20 entrees x 13 caracteres x 2 o (+ 8)
DE_SIZE		= DE_LFN + DE_LFN_SIZE	; (multiple de 16 : reserve sur la pile)
LFN_MAX		= 20			; entrees LFN d'un nom (255 caracteres)
NAME_UNITS	= 255			; caracteres UTF-16 d'un nom

; --- iterateur de repertoire -------------------------------------------------
IT_VOL		= 0
IT_START	= 8			; premier cluster du repertoire
IT_CLUSTER	= 16			; cluster courant
IT_INDEX	= 24			; index de la prochaine entree
IT_SIZE		= 32

; ---------------------------------------------------------------------------
;  Montage
; ---------------------------------------------------------------------------

; x0 = volume, x1 = premier secteur de la partition -> x0 = 1 si FAT32 reconnu
fat_mount:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	str	x20,[x19,V_PSTART]
	mov	x0,x20
	bl	bc_get
	cbz	x0,.bad
	ldrh	w1,[x0,510]
	movl	w2,0xAA55
	cmp	w1,w2
	b.ne	.bad
	ldrb	w1,[x0,11]			; octets par secteur (non aligne)
	ldrb	w2,[x0,12]
	orr	w1,w1,w2,lsl 8
	cmp	w1,512
	b.ne	.bad
	ldrb	w1,[x0,13]			; secteurs par cluster
	cbz	w1,.bad
	str	x1,[x19,V_SPC]
	clz	w2,w1
	mov	w3,31
	sub	w2,w3,w2
	add	w2,w2,9
	str	x2,[x19,V_CSHIFT]
	ldrh	w2,[x0,14]			; secteurs reserves
	add	x3,x20,x2
	str	x3,[x19,V_FAT]
	ldrb	w4,[x0,16]			; nombre de FAT
	str	x4,[x19,V_NFATS]
	ldr	w5,[x0,36]			; secteurs par FAT
	cbz	w5,.bad				; 0 : FAT12/16, non gere
	str	x5,[x19,V_FATSZ]
	madd	x6,x4,x5,x3
	str	x6,[x19,V_DATA]
	ldr	w7,[x0,44]
	str	x7,[x19,V_ROOT]
	ldr	w8,[x0,32]			; secteurs totaux
	sub	x9,x6,x20
	sub	x8,x8,x9
	ldr	x1,[x19,V_SPC]
	udiv	x8,x8,x1
	str	x8,[x19,V_NCLUST]
	mov	x0,2
	str	x0,[x19,V_HINT]
	mov	x0,x19
	bl	fat_fsinfo_load
	mov	x0,1
	str	x0,[x19,V_TYPE]
	mov	x0,x19
	bl	fat_agrandir
	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume FAT32 monte : la partition est plus grande que le systeme de
; fichiers (Setup l'a etendue, src/sys/setup/disque.h) -> il grandit jusqu'a
; elle, dans la limite de sa FAT (les images de Korin OS en ont une pour
; 128 Go, tools/mkimages.py ; ses entrees au-dela sont a zero) : secteurs
; totaux du BPB et de sa copie, clusters libres de FSInfo
fat_agrandir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,lecture_seule
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,cc_disk			; secteurs de la partition
	ldr	x20,[x0]
	cbz	x20,.out
	cmn	x20,1
	b.eq	.out
	ldr	x3,[x19,V_DATA]			; x3 : debut des donnees (relatif)
	ldr	x4,[x19,V_PSTART]
	sub	x3,x3,x4
	cmp	x20,x3
	b.ls	.out
	sub	x5,x20,x3			; clusters que la partition tient
	ldr	x6,[x19,V_SPC]
	udiv	x5,x5,x6
	ldr	x1,[x19,V_FATSZ]		; ceux que la FAT decrit
	lsl	x1,x1,7
	sub	x1,x1,2
	cmp	x5,x1
	csel	x5,x1,x5,hi
	ldr	x2,[x19,V_NCLUST]
	cmp	x5,x2
	b.ls	.out
	sub	x21,x5,x2			; x21 : clusters de plus
	str	x5,[x19,V_NCLUST]
	madd	x22,x5,x6,x3			; x22 : secteurs totaux
	mov	x20,x4
	mov	x0,x20				; le BPB, puis sa copie (secteur 6)
	bl	bc_get
	cbz	x0,.out
	str	w22,[x0,32]
	mov	x0,x20
	bl	bc_write
	add	x0,x20,6
	bl	bc_get
	cbz	x0,.fsi
	ldrh	w1,[x0,510]
	movl	w2,0xAA55
	cmp	w1,w2
	b.ne	.fsi
	str	w22,[x0,32]
	add	x0,x20,6
	bl	bc_write
.fsi:	ldr	x0,[x19,V_FREE]			; (inconnu : fatfs les comptera)
	cmn	x0,1
	b.eq	.out
	add	x0,x0,x21
	str	x0,[x19,V_FREE]
	mov	x0,1
	str	x0,[x19,V_FSI_DIRTY]
	mov	x0,x19
	bl	fat_fsinfo_flush
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume FAT32 : lit FSInfo (clusters libres : V_FREE, tenu a jour par
; fat_alloc et fat_free_chain, reecrit par fat_fsinfo_flush)
fat_fsinfo_load:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,-1
	str	x0,[x19,V_FREE]
	str	xzr,[x19,V_FSINFO]
	str	xzr,[x19,V_FSI_DIRTY]
	ldr	x0,[x19,V_PSTART]		; secteur de demarrage : FSInfo (+48)
	bl	bc_get
	cbz	x0,.out
	ldrh	w1,[x0,48]
	cbz	w1,.out
	ldr	x2,[x19,V_PSTART]
	add	x20,x2,x1
	mov	x0,x20
	bl	fat_fsinfo_get
	cbz	x0,.out
	str	x20,[x19,V_FSINFO]
	ldr	w1,[x0,488]			; 0xFFFFFFFF : inconnu (garde -1)
	ldr	x2,[x19,V_NCLUST]
	cmp	x1,x2
	b.hi	.out
	str	x1,[x19,V_FREE]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = secteur FSInfo -> x0 = son contenu en cache, 0 si signatures fausses
fat_fsinfo_get:
	prologue
	bl	bc_get
	cbz	x0,.out
	ldr	w1,[x0]
	movl	w2,0x41615252
	cmp	w1,w2
	b.ne	.bad
	ldr	w1,[x0,484]
	movl	w2,0x61417272
	cmp	w1,w2
	b.eq	.out
.bad:	mov	x0,0
.out:	epilogue

; x0 = volume : reecrit le nombre de clusters libres (et le prochain libre)
; dans FSInfo s'il a change (FAT32 ; apres chaque operation de fatfs)
fat_fsinfo_flush:
	ldr	x1,[x0,V_TYPE]
	cmp	x1,1
	b.ne	.r
	ldr	x1,[x0,V_FSI_DIRTY]
	cbz	x1,.r
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	str	xzr,[x19,V_FSI_DIRTY]
	ldr	x0,[x19,V_FSINFO]
	cbz	x0,.out
	bl	fat_fsinfo_get
	cbz	x0,.out
	ldr	x1,[x19,V_FREE]
	str	w1,[x0,488]
	ldr	x1,[x19,V_HINT]
	str	w1,[x0,492]
	ldr	x0,[x19,V_FSINFO]
	bl	bc_write
.out:	ldr	x19,[sp],16
	epilogue
.r:	ret

; ---------------------------------------------------------------------------
;  Table d'allocation
; ---------------------------------------------------------------------------

; x0 = volume, x1 = cluster -> x0 = secteur, x1 = octets par cluster
fat_clus_lba:
	ldr	x2,[x0,V_DATA]
	ldr	x3,[x0,V_SPC]
	ldr	x4,[x0,V_CSHIFT]
	sub	x1,x1,2
	madd	x0,x1,x3,x2
	mov	x1,x4
	ret

; x0 = volume, x1 = cluster -> x0 = cluster suivant (>= FAT_EOC : fin, 0 erreur)
fat_next:
	prologue
	ldr	x2,[x0,V_FAT]
	lsr	x3,x1,7				; 128 entrees par secteur
	and	x4,x1,127
	str	x4,[sp,-16]!
	add	x0,x2,x3
	bl	bc_get
	ldr	x4,[sp],16
	cbz	x0,.out
	ldr	w0,[x0,x4,lsl 2]
	and	w0,w0,FAT_MASK
	cmp	w0,2				; libre/reserve rencontre dans une chaine : fin
	b.hs	.out
	movl	w0,FAT_EOC
.out:	epilogue

; x0 = volume, x1 = cluster, x2 = valeur : ecrit dans toutes les FAT
fat_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0				; numero de FAT
.fat:	ldr	x0,[x19,V_NFATS]
	cmp	x22,x0
	b.hs	.done
	ldr	x0,[x19,V_FAT]
	ldr	x1,[x19,V_FATSZ]
	madd	x0,x1,x22,x0
	lsr	x1,x20,7
	add	x0,x0,x1
	str	x0,[sp,-16]!
	bl	bc_get
	ldr	x1,[sp],16
	cbz	x0,.done
	and	x2,x20,127
	ldr	w3,[x0,x2,lsl 2]
	and	w3,w3,0xF0000000		; les 4 bits hauts sont reserves
	orr	w3,w3,w21
	str	w3,[x0,x2,lsl 2]
	mov	x0,x1
	bl	bc_write
	add	x22,x22,1
	b	.fat
.done:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = cluster precedent (0 : debut de chaine)
; -> x0 = nouveau cluster, mis a zero et marque fin de chaine (0 si disque plein)
fat_alloc:
	mov	x2,0
; (x2 = 1 : pas de zeros, le cluster va etre ecrit en entier)
fat_alloc_x:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x24,x2
	ldr	x21,[x19,V_HINT]
	ldr	x22,[x19,V_NCLUST]
	add	x22,x22,2			; dernier cluster + 1
	mov	x23,x22				; essais restants
.scan:	cbz	x23,.full
	sub	x23,x23,1
	cmp	x21,x22
	b.lo	.chk
	mov	x21,2
.chk:	ldr	x0,[x19,V_FAT]
	lsr	x1,x21,7
	add	x0,x0,x1
	bl	bc_get
	cbz	x0,.full
	and	x1,x21,127
	ldr	w2,[x0,x1,lsl 2]
	and	w2,w2,FAT_MASK
	cbz	w2,.found
	add	x21,x21,1
	b	.scan
.found:	mov	x0,x19
	mov	x1,x21
	movl	x2,FAT_MASK			; fin de chaine
	bl	fat_set
	cbz	x20,.zero
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	fat_set
.zero:	cbnz	x24,.zdone
	mov	x0,x19				; contenu a zero
	mov	x1,x21
	bl	fat_clus_lba
	ldr	x1,[x19,V_SPC]
	bl	blk_zero
.zdone:	add	x0,x21,1
	str	x0,[x19,V_HINT]
	ldr	x0,[x19,V_FREE]			; un libre de moins (s'il est connu)
	cmn	x0,1
	b.eq	.nf
	sub	x0,x0,1
	str	x0,[x19,V_FREE]
	mov	x0,1
	str	x0,[x19,V_FSI_DIRTY]
.nf:	mov	x0,x21
	b	.out
.full:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = premier cluster : libere toute la chaine (chaque secteur de
; FAT ecrit une fois, a la fin)
fat_free_chain:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	bc_defer_on
.c:	cmp	x20,2
	b.lo	.done
	movl	x0,FAT_EOC
	cmp	x20,x0
	b.hs	.done
	mov	x0,x19
	mov	x1,x20
	bl	fat_next
	str	x0,[sp,-16]!
	mov	x0,x19
	mov	x1,x20
	mov	x2,0
	bl	fat_set
	ldr	x0,[sp],16
	ldr	x1,[x19,V_FREE]			; un libre de plus (s'il est connu)
	cmn	x1,1
	b.eq	.nf
	add	x1,x1,1
	str	x1,[x19,V_FREE]
	mov	x1,1
	str	x1,[x19,V_FSI_DIRTY]
.nf:	ldr	x1,[x19,V_HINT]			; le cluster libere redevient candidat
	cmp	x20,x1
	b.hs	.keep
	str	x20,[x19,V_HINT]
.keep:	mov	x20,x0
	b	.c
.done:	bl	bc_defer_off
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Repertoires
; ---------------------------------------------------------------------------

; x0 = iterateur, x1 = volume, x2 = premier cluster du repertoire
dir_open:
	str	x1,[x0,IT_VOL]
	str	x2,[x0,IT_START]
	str	x2,[x0,IT_CLUSTER]
	str	xzr,[x0,IT_INDEX]
	ret

; x0 = iterateur -> x0 = pointeur (cache) sur l'entree de 32 o suivante,
; 0 en fin de chaine. Le secteur de l'entree est dans x1 (pour bc_write).
dir_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,IT_VOL]
	ldr	x21,[x19,IT_INDEX]
	ldr	x0,[x20,V_CSHIFT]
	sub	x0,x0,5				; entrees par cluster = 2^(cshift-5)
	mov	x1,1
	lslv	x22,x1,x0
	sub	x22,x22,1			; masque d'index dans le cluster
	and	x0,x21,x22
	cbnz	x0,.same
	cbz	x21,.same			; premiere entree : cluster de depart
	mov	x0,x20				; changement de cluster
	ldr	x1,[x19,IT_CLUSTER]
	ktrace 'dir: cluster suivant de',x1
	bl	fat_next
	ktrace 'dir: -> ',x0
	movl	x1,FAT_EOC
	cmp	x0,x1
	b.hs	.end
	cbz	x0,.end
	str	x0,[x19,IT_CLUSTER]
.same:	mov	x0,x20
	ldr	x1,[x19,IT_CLUSTER]
	cmp	x1,2
	b.lo	.end
	bl	fat_clus_lba
	and	x1,x21,x22			; index dans le cluster
	add	x0,x0,x1,lsr 4			; 16 entrees par secteur
	str	x0,[sp,-16]!
	str	x1,[sp,8]
	bl	bc_get
	ldr	x1,[sp,8]
	ldr	x2,[sp],16
	cbz	x0,.end
	and	x1,x1,15
	add	x0,x0,x1,lsl 5
	mov	x1,x2
	add	x21,x21,1
	str	x21,[x19,IT_INDEX]
	b	.out
.end:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = iterateur, x1 = description (DE) a remplir
; -> x0 = 1 si une entree (fichier ou repertoire) a ete lue, 0 en fin
; Saute ".", "..", les entrees supprimees et l'etiquette de volume.
dir_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; checksum LFN attendu (+0x100 si LFN valide)
	ldr	x0,[x19,IT_START]
	str	x0,[x20,DE_DIR]
.next:	ldr	x24,[x19,IT_INDEX]		; index de l'entree lue
	mov	x0,x19
	bl	dir_next
	cbz	x0,.end
	mov	x22,x0
	ldr	x0,[x22]
	ktrace 'dir: entree',x0
	ldrb	w0,[x22,11]
	ktrace 'dir:   attributs',x0
	ldrb	w0,[x22]
	cbz	w0,.end				; 0 : plus aucune entree
	cmp	w0,0xE5
	b.eq	.reset
	ldrb	w1,[x22,11]			; attributs
	cmp	w1,ATTR_LFN
	b.eq	.lfn
	tst	w1,ATTR_VOLUME
	b.ne	.reset
	cmp	w0,'.'
	b.eq	.reset
	; --- entree 8.3 -------------------------------------------------------
	str	x24,[x20,DE_INDEX]
	tbnz	x21,8,.have_lfn
	str	x24,[x20,DE_FIRST]
	b	.short
.have_lfn:
	ubfx	x0,x21,16,8			; suite LFN incomplete : nom 8.3
	cbnz	x0,.short_first
	mov	x0,x22				; checksum des 11 octets
	bl	sfn_checksum
	and	x1,x21,0xFF
	cmp	x0,x1
	b.ne	.short_first
	b	.fields
.short_first:
	str	x24,[x20,DE_FIRST]
.short:	mov	x0,x22				; nom 8.3 -> "nom.ext"
	add	x1,x20,DE_NAME
	bl	sfn_to_name
.fields:
	ldrb	w0,[x22,11]
	str	x0,[x20,DE_ATTR]
	ldrh	w0,[x22,20]			; cluster haut
	ldrh	w1,[x22,26]			; cluster bas
	orr	w0,w1,w0,lsl 16
	str	x0,[x20,DE_CLUSTER]
	ldr	w0,[x22,28]
	str	x0,[x20,DE_FSIZE]
	ldr	w0,[x22,22]			; heure, date
	str	x0,[x20,DE_MTIME]
	mov	x0,1
	b	.out
.lfn:	; --- entree de nom long ----------------------------------------------
	tbz	w0,6,.lfn_mid			; 0x40 : derniere (= premiere lue)
	and	w0,w0,0x1F
	cbz	w0,.reset			; numero 0 ou > 20 : entree orpheline
	cmp	w0,LFN_MAX
	b.hi	.reset
	ldrb	w1,[x22,13]
	orr	x21,x1,0x100
	orr	x21,x21,x0,lsl 16		; numero de sequence attendu
	str	x24,[x20,DE_FIRST]
	add	x0,x20,DE_LFN			; zone UTF-16 (255 caracteres) : effacer
	mov	x1,DE_LFN_SIZE
	bl	mem_zero
	ldrb	w0,[x22]
	and	w0,w0,0x1F
.lfn_mid:
	and	w0,w0,0x1F
	cbz	w0,.reset
	ubfx	x1,x21,16,8
	cmp	w0,w1
	b.ne	.reset				; sequence cassee
	ldrb	w2,[x22,13]			; meme checksum que la premiere
	and	x3,x21,0xFF
	cmp	x2,x3
	b.ne	.reset
	sub	x1,x1,1
	bfi	x21,x1,16,8
	sub	w0,w0,1				; 13 caracteres UTF-16 par entree
	mov	w1,26
	mul	w0,w0,w1
	add	x1,x20,DE_LFN
	add	x1,x1,x0
	add	x2,x22,1
	mov	x3,10
	bl	mem_copy_n
	add	x2,x22,14
	add	x1,x1,10
	mov	x3,12
	bl	mem_copy_n
	add	x2,x22,28
	add	x1,x1,12
	mov	x3,4
	bl	mem_copy_n
	ubfx	x1,x21,16,8
	cbnz	x1,.next
	; derniere partie recue : UTF-16 -> UTF-8 dans DE_NAME
	add	x0,x20,DE_NAME
	add	x1,x20,DE_LFN
	mov	x2,LFN_MAX*13
	bl	utf16_to_utf8
	b	.next
.reset:	mov	x21,0
	b	.next
.end:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = destination, x2 = source, x3 = octets (x1/x2 non modifies)
mem_copy_n:
	cbz	x3,.d
	mov	x4,0
.c:	ldrb	w5,[x2,x4]
	strb	w5,[x1,x4]
	add	x4,x4,1
	cmp	x4,x3
	b.lo	.c
.d:	ret

; x0 = entree 8.3 -> x0 = checksum LFN
sfn_checksum:
	mov	w1,0
	mov	x2,11
.c:	ubfx	w3,w1,1,7
	orr	w1,w3,w1,lsl 7
	and	w1,w1,0xFF
	ldrb	w3,[x0],1
	add	w1,w1,w3
	and	w1,w1,0xFF
	subs	x2,x2,1
	b.ne	.c
	mov	w0,w1
	ret

; x0 = entree 8.3, x1 = destination UTF-8 (minuscules si demande par l'octet 12)
sfn_to_name:
	ldrb	w5,[x0,12]			; 0x08 : base en minuscules, 0x10 : extension
	mov	x2,0
.base:	ldrb	w3,[x0,x2]
	cmp	w3,' '
	b.eq	.ext
	tbz	w5,3,.b_store
	sub	w4,w3,'A'			; minuscule (fonction feuille : pas de bl,
	cmp	w4,25				; x30 serait perdu)
	b.hi	.b_store
	add	w3,w3,32
.b_store:
	strb	w3,[x1],1
	add	x2,x2,1
	cmp	x2,8
	b.lo	.base
.ext:	ldrb	w3,[x0,8]
	cmp	w3,' '
	b.eq	.end
	mov	w4,'.'
	strb	w4,[x1],1
	mov	x2,8
.e:	ldrb	w3,[x0,x2]
	cmp	w3,' '
	b.eq	.end
	tbz	w5,4,.e_store
	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.e_store
	add	w3,w3,32
.e_store:
	strb	w3,[x1],1
	add	x2,x2,1
	cmp	x2,11
	b.lo	.e
.end:	strb	wzr,[x1]
	ret

; x0 = destination UTF-8 (NAME_BYTES o), x1 = source UTF-16LE, x2 = unites
; max ; les paires de substitution (D800-DBFF, DC00-DFFF) -> 4 octets
utf16_to_utf8:
	add	x6,x0,NAME_BYTES-5		; garde la place du plus long + 0
.c:	cbz	x2,.end
	cmp	x0,x6
	b.hs	.end
	ldrh	w3,[x1],2
	cbz	w3,.end
	movl	w4,0xFFFF
	cmp	w3,w4
	b.eq	.end
	sub	x2,x2,1
	cmp	w3,0x80
	b.hs	.two
	strb	w3,[x0],1
	b	.c
.two:	cmp	w3,0x800
	b.hs	.three
	lsr	w4,w3,6
	orr	w4,w4,0xC0
	strb	w4,[x0],1
	and	w4,w3,0x3F
	orr	w4,w4,0x80
	strb	w4,[x0],1
	b	.c
.three:	and	w4,w3,0xFC00			; substitution haute suivie d'une basse ?
	movl	w7,0xD800
	cmp	w4,w7
	b.ne	.bmp
	cbz	x2,.bmp
	ldrh	w5,[x1]
	and	w4,w5,0xFC00
	movl	w7,0xDC00
	cmp	w4,w7
	b.ne	.bmp
	add	x1,x1,2
	sub	x2,x2,1
	and	w3,w3,0x3FF
	and	w5,w5,0x3FF
	orr	w3,w5,w3,lsl 10
	add	w3,w3,0x10000
	lsr	w4,w3,18
	orr	w4,w4,0xF0
	strb	w4,[x0],1
	ubfx	w4,w3,12,6
	orr	w4,w4,0x80
	strb	w4,[x0],1
	ubfx	w4,w3,6,6
	orr	w4,w4,0x80
	strb	w4,[x0],1
	and	w4,w3,0x3F
	orr	w4,w4,0x80
	strb	w4,[x0],1
	b	.c
.bmp:	lsr	w4,w3,12
	orr	w4,w4,0xE0
	strb	w4,[x0],1
	ubfx	w4,w3,6,6
	orr	w4,w4,0x80
	strb	w4,[x0],1
	and	w4,w3,0x3F
	orr	w4,w4,0x80
	strb	w4,[x0],1
	b	.c
.end:	strb	wzr,[x0]
	ret

; x0 = chaine UTF-8, x1 = destination UTF-16 (mots de 16 bits), x2 = max
; -> x0 = nombre d'unites ecrites (4 octets UTF-8 : une paire de substitution)
utf8_to_utf16:
	mov	x6,0
.c:	cmp	x6,x2
	b.hs	.end
	ldrb	w3,[x0],1
	cbz	w3,.end
	tbz	w3,7,.store
	and	w4,w3,0xE0
	cmp	w4,0xC0
	b.ne	.three
	and	w3,w3,0x1F
	ldrb	w4,[x0],1
	and	w4,w4,0x3F
	orr	w3,w4,w3,lsl 6
	b	.store
.three:	and	w4,w3,0xF0
	cmp	w4,0xF0
	b.eq	.four
	and	w3,w3,0x0F
	ldrb	w4,[x0],1
	and	w4,w4,0x3F
	orr	w3,w4,w3,lsl 6
	ldrb	w4,[x0],1
	and	w4,w4,0x3F
	orr	w3,w4,w3,lsl 6
.store:	strh	w3,[x1],2
	add	x6,x6,1
	b	.c
.end:	mov	x0,x6
	ret
.four:	add	x4,x6,1				; 4 octets : deux unites
	cmp	x4,x2
	b.hs	.end
	and	w3,w3,0x07
	mov	x5,3
.f:	ldrb	w4,[x0],1
	and	w4,w4,0x3F
	orr	w3,w4,w3,lsl 6
	subs	x5,x5,1
	b.ne	.f
	sub	w3,w3,0x10000
	lsr	w4,w3,10
	movl	w5,0xD800
	add	w4,w4,w5
	strh	w4,[x1],2
	and	w4,w3,0x3FF
	movl	w5,0xDC00
	add	w4,w4,w5
	strh	w4,[x1],2
	add	x6,x6,2
	b	.c

; x0 = chaine 1 (terminee par 0 ou '/'), x1 = chaine 2 (terminee par 0)
; -> x0 = 1 si egales (ASCII insensible a la casse)
name_equal:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,'/'
	csel	w2,wzr,w2,eq
	sub	w4,w2,'A'
	cmp	w4,25
	b.hi	.l2
	add	w2,w2,32
.l2:	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.cmp
	add	w3,w3,32
.cmp:	cmp	w2,w3
	b.ne	.no
	cbnz	w2,.c
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = volume, x1 = premier cluster du repertoire, x2 = nom (fin : 0 ou '/'),
; x3 = description (DE) a remplir -> x0 = 1 si trouve
dir_find:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,IT_SIZE
	mov	x19,x2
	mov	x20,x3
	mov	x2,x1
	mov	x1,x0
	mov	x0,sp
	bl	dir_open
.next:	mov	x0,sp
	mov	x1,x20
	bl	dir_read
	cbz	x0,.out
	mov	x0,x19
	add	x1,x20,DE_NAME
	bl	name_equal
	cbz	x0,.next
.out:	add	sp,sp,IT_SIZE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = chemin relatif au volume ("a/b/c", sans '/' initial),
; x2 = description (DE) -> x0 = 1 si trouve. Chemin vide : la racine.
fat_lookup:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	x22,[x19,V_ROOT]		; repertoire courant du parcours
	; racine : description synthetique
	str	x22,[x21,DE_CLUSTER]
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
	ldr	x0,[x21,DE_ATTR]		; il faut descendre dans un repertoire
	tst	x0,ATTR_DIR
	b.eq	.missing
	mov	x0,x19
	mov	x1,x22
	mov	x2,x20
	mov	x3,x21
	bl	dir_find
	cbz	x0,.missing
	ldr	x22,[x21,DE_CLUSTER]
	cbnz	x22,.skip
	ldr	x22,[x19,V_ROOT]		; ".." vers la racine : cluster 0
.skip:	ldrb	w0,[x20],1			; composant suivant
	cbz	w0,.found_back
	cmp	w0,'/'
	b.ne	.skip
	b	.comp
.found_back:
.found:	mov	x0,1
	b	.out
.missing:
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = cluster du repertoire, x2 = index d'entree
; -> x0 = pointeur en cache sur l'entree, x1 = son secteur (0 si hors chaine)
dir_entry_at:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,IT_SIZE+16
	mov	x20,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,sp
	bl	dir_open
	ldr	x19,[sp,IT_VOL]			; avancer de cluster en cluster
	ldr	x0,[x19,V_CSHIFT]
	sub	x0,x0,5
	lsrv	x3,x20,x0			; nombre de clusters a sauter
.skip:	cbz	x3,.here
	str	x3,[sp,IT_SIZE]
	mov	x0,x19
	ldr	x1,[sp,IT_CLUSTER]
	bl	fat_next
	ldr	x3,[sp,IT_SIZE]
	movl	x1,FAT_EOC
	cmp	x0,x1
	b.hs	.none
	str	x0,[sp,IT_CLUSTER]
	sub	x3,x3,1
	b	.skip
.here:	ldr	x0,[x19,V_CSHIFT]		; index dans le cluster
	sub	x0,x0,5
	mov	x1,1
	lslv	x1,x1,x0
	sub	x1,x1,1
	and	x20,x20,x1
	mov	x0,x19
	ldr	x1,[sp,IT_CLUSTER]
	bl	fat_clus_lba
	add	x19,x0,x20,lsr 4
	mov	x0,x19
	bl	bc_get
	cbz	x0,.none
	and	x1,x20,15
	add	x0,x0,x1,lsl 5
	mov	x1,x19
	b	.out
.none:	mov	x0,0
	mov	x1,0
.out:	add	sp,sp,IT_SIZE+16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = DE : ecrit premier cluster, taille et attributs dans
; l'entree 8.3 sur disque
fat_update_entry:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	dos_maintenant
	mov	x21,x0
	mov	x0,x19
	ldr	x1,[x20,DE_DIR]
	ldr	x2,[x20,DE_INDEX]
	bl	dir_entry_at
	cbz	x0,.out
	ldr	x2,[x20,DE_CLUSTER]
	lsr	x3,x2,16
	strh	w3,[x0,20]
	strh	w2,[x0,26]
	ldr	x2,[x20,DE_FSIZE]
	str	w2,[x0,28]
	ldr	x2,[x20,DE_ATTR]
	strb	w2,[x0,11]
	strh	w21,[x0,22]			; derniere ecriture, dernier acces
	lsr	w2,w21,16
	strh	w2,[x0,24]
	strh	w2,[x0,18]
	mov	x0,x1
	bl	bc_write
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = DE : marque supprimees l'entree 8.3 et ses entrees LFN
fat_erase_entry:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x21,[x20,DE_FIRST]
.e:	ldr	x0,[x20,DE_INDEX]
	cmp	x21,x0
	b.hi	.done
	mov	x0,x19
	ldr	x1,[x20,DE_DIR]
	mov	x2,x21
	bl	dir_entry_at
	cbz	x0,.done
	mov	w2,0xE5
	strb	w2,[x0]
	mov	x0,x1
	bl	bc_write
	add	x21,x21,1
	b	.e
.done:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Creation d'une entree
; ---------------------------------------------------------------------------

; x0 = volume, x1 = cluster du repertoire parent, x2 = nom UTF-8 (fin 0 ou '/'),
; x3 = attributs, x4 = DE a remplir -> x0 = 1 si cree (fichier vide ou
; repertoire avec "." et "..")
fat_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,576			; [sp] nom UTF-16 (528), [sp+528] nom 8.3
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4

	; nom en UTF-16 (copie du composant jusqu'a '/')
	add	x0,x23,DE_NAME			; composant -> DE_NAME (UTF-8)
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
	cbz	w1,.fail			; nom vide
	mov	x1,sp
	mov	x2,NAME_UNITS+1
	bl	utf8_to_utf16
	mov	x24,x0				; longueur UTF-16
	cmp	x24,NAME_UNITS			; 255 caracteres au plus
	b.hi	.fail
	add	x0,x24,12			; entrees LFN = ceil(n/13)
	mov	x1,13
	udiv	x25,x0,x1
	mul	x2,x25,x1			; fin du nom : 0 puis 0xFFFF
	mov	x1,x24
	strh	wzr,[sp,x1,lsl 1]
	mov	w3,0xFFFF
.pad:	add	x1,x1,1
	cmp	x1,x2
	b.hs	.pad_done
	strh	w3,[sp,x1,lsl 1]
	b	.pad
.pad_done:

	; nom 8.3 unique
	mov	x0,x19
	mov	x1,x20
	add	x2,x23,DE_NAME
	add	x3,sp,528
	bl	fat_make_sfn
	cbz	x0,.fail

	; trouver ncount = x25+1 entrees libres consecutives
	mov	x0,x19
	mov	x1,x20
	add	x2,x25,1
	bl	dir_find_free
	tbnz	x0,63,.fail
	mov	x26,x0				; index de la premiere entree

	; ecrire les entrees LFN (ordre inverse) puis l'entree 8.3
	add	x0,sp,528
	bl	sfn_checksum
	mov	x21,x0				; checksum
	mov	x24,x25				; numero de sequence courant (n..1)
	mov	x22,x22				; attributs conserves
.lfn:	cbz	x24,.sfn
	mov	x0,x19
	mov	x1,x20
	sub	x2,x25,x24
	add	x2,x26,x2
	bl	dir_entry_at
	cbz	x0,.fail
	str	x1,[sp,-16]!
	mov	w2,w24				; octet de sequence
	cmp	x24,x25
	b.ne	.seq
	orr	w2,w2,0x40
.seq:	strb	w2,[x0]
	mov	w2,ATTR_LFN
	strb	w2,[x0,11]
	strb	wzr,[x0,12]
	strb	w21,[x0,13]
	strh	wzr,[x0,26]
	sub	x3,x24,1			; caracteres (x24-1)*13 .. +12
	mov	x4,26
	mul	x3,x3,x4
	add	x3,sp,x3
	add	x3,x3,16			; (+16 : la sauvegarde de x1 sur la pile)
	; 5 + 6 + 2 caracteres ; au-dela de la fin : 0 puis 0xFFFF
	mov	x5,0				; index caractere 0..12
.ch:	cmp	x5,13
	b.hs	.ch_done
	ldrh	w6,[x3,x5,lsl 1]
	; offset dans l'entree : 1+2i (i<5), 14+2(i-5) (i<11), 28+2(i-11)
	cmp	x5,5
	b.hs	.p2
	lsl	x8,x5,1
	add	x8,x8,1
	b	.st
.p2:	cmp	x5,11
	b.hs	.p3
	sub	x8,x5,5
	lsl	x8,x8,1
	add	x8,x8,14
	b	.st
.p3:	sub	x8,x5,11
	lsl	x8,x8,1
	add	x8,x8,28
.st:	strb	w6,[x0,x8]
	add	x8,x8,1
	lsr	w6,w6,8
	strb	w6,[x0,x8]
	add	x5,x5,1
	b	.ch
.ch_done:
	ldr	x1,[sp],16
	mov	x0,x1
	bl	bc_write
	sub	x24,x24,1
	b	.lfn

.sfn:	mov	x0,x19
	mov	x1,x20
	add	x2,x26,x25
	bl	dir_entry_at
	cbz	x0,.fail
	mov	x24,x1				; secteur
	mov	x21,x0
	mov	x1,32
	bl	mem_zero_small
	add	x1,sp,528			; nom 8.3
	mov	x2,0
.sn:	ldrb	w3,[x1,x2]
	strb	w3,[x21,x2]
	add	x2,x2,1
	cmp	x2,11
	b.lo	.sn
	strb	w22,[x21,11]			; attributs
	bl	dos_maintenant			; creation, acces, derniere ecriture
	strh	w0,[x21,14]
	strh	w0,[x21,22]
	lsr	w0,w0,16
	strh	w0,[x21,16]
	strh	w0,[x21,18]
	strh	w0,[x21,24]
	mov	x0,x24
	bl	bc_write

	; description de l'entree creee
	str	x20,[x23,DE_DIR]
	str	x26,[x23,DE_FIRST]
	add	x0,x26,x25
	str	x0,[x23,DE_INDEX]
	str	xzr,[x23,DE_CLUSTER]
	str	xzr,[x23,DE_FSIZE]
	str	x22,[x23,DE_ATTR]

	tst	x22,ATTR_DIR			; repertoire : un cluster avec "." et ".."
	b.eq	.ok
	mov	x0,x19
	mov	x1,0
	bl	fat_alloc
	cbz	x0,.fail
	str	x0,[x23,DE_CLUSTER]
	mov	x21,x0
	mov	x0,x19
	mov	x1,x23
	bl	fat_update_entry
	mov	x0,x19
	mov	x1,x21
	bl	fat_clus_lba
	mov	x24,x0
	bl	bc_get
	cbz	x0,.fail
	mov	x1,0x2020			; "." et ".." remplis d'espaces
	mov	x2,0
.sp:	strb	w1,[x0,x2]
	add	x3,x2,32
	strb	w1,[x0,x3]
	add	x2,x2,1
	cmp	x2,11
	b.lo	.sp
	mov	w1,'.'
	strb	w1,[x0]
	strb	w1,[x0,32]
	strb	w1,[x0,33]
	mov	w1,ATTR_DIR
	strb	w1,[x0,11]
	strb	w1,[x0,32+11]
	lsr	x1,x21,16			; "." -> lui-meme
	strh	w1,[x0,20]
	strh	w21,[x0,26]
	ldr	x1,[x19,V_ROOT]			; ".." -> parent (0 si racine)
	mov	x2,x20
	cmp	x2,x1
	csel	x2,xzr,x2,eq
	lsr	x1,x2,16
	strh	w1,[x0,32+20]
	strh	w2,[x0,32+26]
	mov	x0,x24
	bl	bc_write
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	add	sp,sp,576
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse, x1 = taille (multiple de 8)
mem_zero_small:
.z:	str	xzr,[x0],8
	subs	x1,x1,8
	b.hi	.z
	ret

; x0 = volume, x1 = cluster du repertoire, x2 = nombre d'entrees voulues
; -> x0 = index de la premiere entree d'une suite libre (le repertoire est
; agrandi si besoin), -1 si echec
dir_find_free:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,IT_SIZE
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,sp
	mov	x1,x19
	mov	x2,x20
	bl	dir_open
	mov	x22,0				; longueur de la suite libre
	mov	x23,0				; debut de la suite
.next:	ldr	x24,[sp,IT_INDEX]
	mov	x0,sp
	bl	dir_next
	cbz	x0,.grow
	ldrb	w0,[x0]
	cbz	w0,.free
	cmp	w0,0xE5
	b.eq	.free
	mov	x22,0
	b	.next
.free:	cbnz	x22,.inc
	mov	x23,x24
.inc:	add	x22,x22,1
	cmp	x22,x21
	b.lo	.next
	mov	x0,x23
	b	.out
.grow:	; fin de chaine : ajouter un cluster (vide) au repertoire
	mov	x0,x19
	ldr	x1,[sp,IT_CLUSTER]
	bl	fat_alloc
	cbz	x0,.fail
	cbnz	x22,.keep			; la suite continue dans le nouveau cluster
	mov	x23,x24
.keep:	mov	x0,x23
	b	.out
.fail:	mov	x0,-1
.out:	add	sp,sp,IT_SIZE
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = cluster du repertoire, x2 = nom UTF-8, x3 = sortie (11 o)
; Fabrique "BASE~n  EXT" absent du repertoire -> x0 = 1 si ok
fat_make_sfn:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,IT_SIZE+DE_SIZE
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,x22				; espaces
	mov	w1,' '
	mov	x2,0
.blank:	strb	w1,[x0,x2]
	add	x2,x2,1
	cmp	x2,11
	b.lo	.blank
	; extension : apres le dernier '.'
	mov	x23,0
	mov	x0,x21
.dot:	ldrb	w1,[x0],1
	cbz	w1,.dot_end
	cmp	w1,'.'
	b.ne	.dot
	mov	x23,x0				; debut de l'extension
	b	.dot
.dot_end:
	mov	x0,x21				; base : 6 caracteres valides
	mov	x2,0
.b:	cmp	x2,6
	b.hs	.b_end
	ldrb	w1,[x0],1
	cbz	w1,.b_end
	cmp	x0,x23
	b.eq	.b_end
	bl	sfn_char
	cbz	w1,.b
	strb	w1,[x22,x2]
	add	x2,x2,1
	b	.b
.b_end:	cbnz	x2,.b_ok
	mov	w1,'F'
	strb	w1,[x22]
	mov	x2,1
.b_ok:	mov	x24,x2				; position du '~'
	cbz	x23,.ext_end
	mov	x0,x23
	mov	x2,8
.e:	cmp	x2,11
	b.hs	.ext_end
	ldrb	w1,[x0],1
	cbz	w1,.ext_end
	bl	sfn_char
	cbz	w1,.e
	strb	w1,[x22,x2]
	add	x2,x2,1
	b	.e
.ext_end:
	mov	x21,1				; numero ~n (plusieurs chiffres : la
.try:	mov	x3,x21				; base raccourcit, dix fichiers au meme
	mov	x2,0				; debut de nom ne doivent pas echouer)
	mov	x4,10
.nd:	udiv	x3,x3,x4
	add	x2,x2,1
	cbnz	x3,.nd
	mov	x3,7
	sub	x3,x3,x2
	cmp	x24,x3
	csel	x3,x24,x3,lo
	mov	w1,'~'
	strb	w1,[x22,x3]
	add	x3,x3,x2
	mov	x5,x21
.dg:	udiv	x6,x5,x4
	msub	x7,x6,x4,x5
	add	w7,w7,'0'
	strb	w7,[x22,x3]
	sub	x3,x3,1
	mov	x5,x6
	subs	x2,x2,1
	b.ne	.dg
	; collision ?
	mov	x0,sp
	mov	x1,x19
	mov	x2,x20
	bl	dir_open
.scan:	mov	x0,sp
	bl	dir_next
	cbz	x0,.unique
	ldrb	w1,[x0]
	cbz	w1,.unique
	mov	x2,0
.cmp:	ldrb	w3,[x0,x2]
	ldrb	w4,[x22,x2]
	cmp	w3,w4
	b.ne	.scan
	add	x2,x2,1
	cmp	x2,11
	b.lo	.cmp
	add	x21,x21,1			; deja pris : numero suivant
	movl	x2,999999
	cmp	x21,x2
	b.ls	.try
	mov	x0,0
	b	.out
.unique:
	mov	x0,1
.out:	add	sp,sp,IT_SIZE+DE_SIZE
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w1 = caractere -> w1 = caractere 8.3 valide en majuscule, 0 a ignorer
sfn_char:
	sub	w3,w1,'a'
	cmp	w3,25
	b.hi	.up
	sub	w1,w1,32
	ret
.up:	sub	w3,w1,'A'
	cmp	w3,25
	b.ls	.ok
	sub	w3,w1,'0'
	cmp	w3,9
	b.ls	.ok
	cmp	w1,'_'
	b.eq	.ok
	cmp	w1,'-'
	b.eq	.ok
	mov	w1,0
.ok:	ret
