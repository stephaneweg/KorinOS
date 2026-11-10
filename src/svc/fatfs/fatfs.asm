; fatfs (FatFS) - service de fichiers FAT32 et exFAT (docs/MICRO-NOYAU.md,
; section 4) : un volume sur un peripherique bloc (blk.sd0p1, blk.sd0p2...),
; monte par vfs : « fatfs <volume> <peripherique> [ro] » ; le format est
; reconnu au montage, lecture et ecriture dans les deux formats. Il
; s'enregistre sous « fs.<volume> » et repond au protocole de inc/fs.inc.
;  * Les systemes de fichiers sont ceux du noyau (src/kernel/fat32.asm et
;    exfat.asm, inclus tels quels) ; la gestion des fichiers ouverts (fo_*)
;    est reprise de l'ancien vfs.asm du noyau (aujourd'hui files.asm).
;  * Ses secteurs : un cache de BC_N secteurs (correspondance directe,
;    ecriture immediate) DANS le tampon partage avec le peripherique bloc :
;    le service bloc y lit directement, sans copie.
;  * Une ecriture de fichier (vol_rw) : FAT et bitmap differes, ecrits une fois
;    a la fin de la requete, apres les donnees ; un cluster neuf ecrit en entier
;    n'est pas mis a zero ; les clusters contigus partent en une commande.
;    Liberer une allocation (rm, troncature) les differe de meme.
;  * Un cache de donnees sous le systeme de fichiers (cc_*, plus bas) : blocs de
;    64 Ko du peripherique, lus une fois, gardes en memoire privee ; les
;    lectures de fichier et les secteurs de bc_get y passent. Toute ecriture
;    passe par blk_requete, qui en retire les blocs touches.
;  * Un fichier ouvert : un objet de la table « ouverts » (son numero est
;    rendu a l'ouverture : droits, proprietaire, drapeaux) qui designe un
;    NOEUD de la table « noeuds », partage par toutes les ouvertures du meme
;    fichier (cle : repertoire parent + index de son entree) : taille,
;    premier cluster, curseur de chaine. Les ecritures d'un descripteur sont
;    ainsi vues des autres (taille, nouveaux clusters ; O_APPEND ecrit a la
;    fin reelle) ; stat, readdir et une nouvelle ouverture lisent la taille
;    du noeud. L'entree du repertoire est reecrite a chaque fermeture d'un
;    noeud modifie (avis du noyau, appel d'un process, ou fin de son
;    proprietaire : SVF_FINS), et tout de suite pour O_TRUNC.
;  * unlink d'un fichier encore ouvert : ses entrees sont effacees (il
;    disparait du repertoire), ses clusters liberes a la derniere fermeture.
;  * Noms longs : 255 caracteres UTF-16 (fat32.asm, exfat.asm) ; readdir en
;    rend 255 octets UTF-8 au plus (nom[256] du protocole).
;  * Proprietaire, acces et tampons partages (ops 14-16) : inc/fsowner.inc
;    (docs/FICHIERS.md, 4.2).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/blk.inc'

macro ktrace msg*, val* {}		; (traces de fat32.asm : noyau seulement)

BC_N		= 256			; secteurs en cache (128 Ko)
BLOC_N		= 128			; secteurs par transfert direct (64 Ko : FS_LIRE_T)
MC_WRITE	= 1
PARTAGE		= (BC_N + BLOC_N) * 512	; tampon partage avec le peripherique
OUV_MAX		= 512			; fichiers ouverts (.NET en garde beaucoup)
CC_BLOCK_SHIFT	= 7			; bloc du cache : 128 secteurs (BLOC_N)
CC_BLOCK_BYTES	= BLOC_N * 512
CC_BLOCKS_MAX	= 4096			; 256 Mo
CC_OTHER_MAX	= 1024			; 64 Mo : les volumes autres que SD0
CC_HASH		= 8192
CC_FILE_MAX	= 0x8000000		; un fichier plus gros n'y passe pas (video)
CC_RESERVE	= 16384			; pages libres laissees aux process (64 Mo)
CC_CHECK_MASK	= 63			; memoire libre revue tous les 64 defauts

; --- objet fichier (comme vfs.asm) --------------------------------------------
FO_TYPE		= 0			; 0 libre, 1 fichier, 2 repertoire
FO_IT		= 8
FO_VOL		= FO_IT + IT_VOL
FO_START	= FO_IT + IT_START
FO_CCLUS	= FO_IT + IT_CLUSTER
FO_CIDX		= FO_IT + IT_INDEX
FO_SIZE		= 40
FO_POS		= 48
FO_PDIR		= 56
FO_INDEX	= 64
FO_FIRST	= 72
FO_ATTR		= 80
FO_FLAGS	= 88
FO_DIRTY	= 96
FO_RANG		= 104			; repertoire : rang de la prochaine entree
FO_PROPRIO	= 112			; pid du proprietaire (SVC_PID_NOYAU : relais)
FO_NOEUD	= 120			; fichier : son noeud (0 : repertoire)
FO_SIZE_	= 128

; --- noeud : un fichier ouvert, partage par ses ouvertures --------------------
; (les 48 premiers octets ont la disposition d'une DE : vol_maj l'accepte)
NO_START	= DE_CLUSTER		; premier cluster (exFAT : poignee)
NO_SIZE		= DE_FSIZE
NO_ATTR		= DE_ATTR
NO_DIR		= DE_DIR		; cle : premier cluster du repertoire parent
NO_INDEX	= DE_INDEX		; cle : index de l'entree (8.3 ; exFAT : 0x85)
NO_FIRST	= DE_FIRST
NO_REFS		= 48			; ouvertures (0 : noeud libre)
NO_DIRTY	= 56			; entree du repertoire a reecrire
NO_CCLUS	= 64			; curseur de chaine (fo_seek_cluster)
NO_CIDX		= 72
NO_EFFACE	= 80			; efface pendant son ouverture : plus de cle
NO_SIZE_	= 96
NO_MAX		= OUV_MAX

E_NAMETOOLONG	= -36

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; AppKit (initramfs en mode services)

start:
	mov	x19,x0				; « <volume> <peripherique> [ro] »
	bl	kits_open
	cbz	x19,quit
	adr	x0,nom_svc			; « fs.<volume> »
	adr	x1,s_fs
	bl	str_cat
	mov	x1,x19
.v:	ldrb	w2,[x1],1
	cmp	w2,' '
	b.eq	.vf
	cbz	w2,quit
	strb	w2,[x0],1
	b	.v
.vf:	strb	wzr,[x0]
	adr	x0,nom_blk			; le peripherique
.b:	ldrb	w2,[x1],1
	cmp	w2,' '
	b.eq	.bf
	cbz	w2,.bf
	strb	w2,[x0],1
	b	.b
.bf:	strb	wzr,[x0]
	cbz	w2,.ouvre
	ldrb	w2,[x1]				; « ro »
	cmp	w2,'r'
	b.ne	.ouvre
	adr	x0,lecture_seule
	mov	x1,1
	str	x1,[x0]
.ouvre:	adr	x0,nom_blk
	mov	x1,3000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,quit
	adr	x1,canal_blk
	str	x0,[x1]
	mov	x0,BLK_INFO			; lecture seule ? son pid
	bl	blk_requete
	tbnz	x0,63,quit
	adr	x1,msg_blk
	ldr	x2,[x1,MSG_B]			; (secteurs : fin du cache)
	adr	x0,cc_disk
	str	x2,[x0]
	ldr	x2,[x1,MSG_A]
	cbz	x2,.rw
	adr	x0,lecture_seule
	mov	x2,1
	str	x2,[x0]
.rw:	ldr	x20,[x1,MSG_D]			; pid du service bloc
	movl	x0,PARTAGE			; le tampon partage
	sys	SYS_BUF_CREATE
	tbnz	x0,63,quit
	mov	x21,x0
	movl	x1,BUF_SLOT
	movl	x2,BUF_VA_BASE
	madd	x1,x21,x1,x2
	adr	x2,cache
	str	x1,[x2]
	mov	x0,x21
	mov	x1,x20
	sys	SYS_BUF_GRANT
	adr	x1,msg_blk			; attacher
	mov	x0,BLK_ATTACHER
	stp	x0,x21,[x1]
	movl	x0,PARTAGE
	str	x0,[x1,MSG_B]
	adr	x0,canal_blk
	ldr	x0,[x0]
	sys	SYS_SVC_CALL
	tbnz	x0,63,quit
	bl	cc_init
	adr	x0,volume			; le systeme de fichiers
	mov	x1,0
	bl	vol_monter
	cbz	x0,quit

	adr	x0,nom_svc
	mov	x1,SVF_FINS or SVF_MEMORY	; prevenu des fins de process, memoire rare
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
.loop:	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbnz	x0,.req
	bl	cc_pressure			; (au repos aussi : memoire libre)
	b	.loop
.req:	mov	x19,x0
	adr	x20,msg
	ldr	x0,[x20]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_SIGNAL
	cmp	x0,x1
	b.eq	.signal
	cmp	x0,FS_FERMER
	b.eq	.fermer
	adr	x1,ops
	mov	x2,0
.o:	add	x3,x1,x2,lsl 3
	ldr	w3,[x3]
	cbz	w3,.inval
	cmp	x3,x0
	b.eq	.go
	add	x2,x2,1
	b	.o
.go:	add	x1,x1,x2,lsl 3
	ldrsw	x3,[x1,4]
	add	x3,x1,x3
	adr	x0,req_cour
	str	x19,[x0]
	mov	x0,x20
	blr	x3
	bl	fsinfo_apres
	b	.reply
.inval:	mov	x0,E_INVAL
.reply:	str	x0,[x20]
	mov	x0,x19
	adr	x1,msg
	sys	SYS_SVC_REPLY
	b	.loop
.fermer:
	mov	x0,x20
	bl	op_fermer
	bl	fsinfo_apres
	ldr	w1,[x20,MSG_PID]		; avis du noyau (pid 0) : sans reponse
	cbz	w1,.loop
	b	.reply
.signal:
	bl	cc_give_back			; le noyau manque de pages
	b	.loop
.fin:	ldr	x0,[x20,MSG_A]			; fin d'un process : ses fichiers
	bl	fin_pid
	bl	fsinfo_apres
	b	.loop
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; x0 = resultat d'une operation (garde) : FSInfo reecrit si le nombre de
; clusters libres a change (FAT32)
fsinfo_apres:
	stp	x0,x30,[sp,-16]!
	adr	x0,volume
	bl	fat_fsinfo_flush
	ldp	x0,x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Peripherique bloc et cache (ce que fat32.asm attend : bc_get, bc_write,
;  blk_read, blk_write, blk_zero, mem_zero, zero_sector)
; ---------------------------------------------------------------------------

; x0 = op, x1 = secteur, x2 = nombre, x3 = adresse -> x0 = resultat
blk_requete:
	cmp	x0,BLK_ECRIRE
	b.ne	.send
	sub	sp,sp,48			; le seul passage des ecritures
	stp	x0,x1,[sp]
	stp	x2,x3,[sp,16]
	str	x30,[sp,32]
	mov	x0,x1
	mov	x1,x2
	bl	cc_forget
	ldp	x0,x1,[sp]
	ldp	x2,x3,[sp,16]
	ldr	x30,[sp,32]
	add	sp,sp,48
.send:	adr	x4,msg_blk
	stp	x0,x1,[x4]
	stp	x2,x3,[x4,MSG_B]
	str	wzr,[x4,MSG_LEN]
	adr	x0,canal_blk
	ldr	x0,[x0]
	mov	x1,x4
	sys	SYS_SVC_CALL
	ret

; x0 = secteur -> x0 = ses 512 octets en cache, 0 si erreur
bc_get:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	and	x20,x19,BC_N-1
	adr	x0,bc_tags
	add	x21,x0,x20,lsl 3
	adr	x22,cache
	ldr	x22,[x22]
	add	x22,x22,x20,lsl 9
	ldr	x1,[x21]
	add	x2,x19,1			; etiquette = secteur + 1 (0 : vide)
	cmp	x1,x2
	b.eq	.hit
	adr	x3,bc_dirty			; la place d'un secteur differe
	ldrb	w4,[x3,x20]
	cbz	w4,.miss
	strb	wzr,[x3,x20]
	mov	x0,BLK_ECRIRE
	sub	x1,x1,1
	mov	x2,1
	mov	x3,x22
	bl	blk_requete
	cbnz	x0,.err
.miss:	str	xzr,[x21]
	mov	x0,x19
	mov	x1,1
	mov	x2,x22
	bl	cc_read
	cbnz	x0,.got
	mov	x0,BLK_LIRE
	mov	x1,x19
	mov	x2,1
	mov	x3,x22
	bl	blk_requete
	cbnz	x0,.err
.got:	add	x2,x19,1
	str	x2,[x21]
.hit:	mov	x0,x22
	b	.out
.err:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = secteur obtenu par bc_get et modifie en place : l'ecrit -> x0 = 1
; (pendant une ecriture de fichier, vol_rw : plus tard, une seule fois)
bc_write:
	prologue
	mov	x1,x0
	and	x3,x0,BC_N-1
	adr	x2,bc_defer
	ldr	x2,[x2]
	cbz	x2,.now
	adr	x4,bc_dirty
	strb	w2,[x4,x3]
	adr	x4,bc_any
	str	x2,[x4]
	mov	x0,1
	b	.out
.now:	adr	x2,cache
	ldr	x2,[x2]
	add	x3,x2,x3,lsl 9
	mov	x0,BLK_ECRIRE
	mov	x2,1
	bl	blk_requete
	cmp	x0,0
	cset	x0,eq
.out:	epilogue

; (fat_free_chain, ex_free_alloc) x0 et x1 gardes : bc_write differe
bc_defer_on:
	adr	x2,bc_defer
	mov	x3,1
	str	x3,[x2]
	ret

; Fin de l'attente : les secteurs differes ecrits
bc_defer_off:
	adr	x0,bc_defer
	str	xzr,[x0]
; Les secteurs differes -> x0 = 1, 0 si l'un n'a pas pu etre ecrit
bc_flush:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,1
	adr	x0,bc_any
	ldr	x1,[x0]
	cbz	x1,.out
	str	xzr,[x0]
	mov	x19,0
.s:	adr	x0,bc_dirty
	ldrb	w1,[x0,x19]
	cbz	w1,.n
	strb	wzr,[x0,x19]
	adr	x0,bc_tags
	ldr	x1,[x0,x19,lsl 3]
	cbz	x1,.n
	sub	x1,x1,1
	adr	x3,cache
	ldr	x3,[x3]
	add	x3,x3,x19,lsl 9
	mov	x0,BLK_ECRIRE
	mov	x2,1
	bl	blk_requete
	cbz	x0,.n
	mov	x20,0
.n:	add	x19,x19,1
	cmp	x19,BC_N
	b.lo	.s
.out:	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = secteur, x1 = nombre : mis a zero, BLOC_N secteurs par commande -> x0 = 1
blk_zero:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x21,cache			; zone de transfert
	ldr	x21,[x21]
	add	x21,x21,BC_N * 512
.pass:	cbz	x20,.ok
	cmp	x20,BLOC_N
	mov	x22,BLOC_N
	csel	x22,x20,x22,lo
	mov	x0,x21
	lsl	x1,x22,9
	bl	mem_zero
	mov	x0,BLK_ECRIRE
	mov	x1,x19
	mov	x2,x22
	mov	x3,x21
	bl	blk_requete
	cbnz	x0,.err
	mov	x0,x19
	mov	x1,x22
	bl	bc_oublier
	add	x19,x19,x22
	sub	x20,x20,x22
	b	.pass
.ok:	mov	x0,1
	b	.out
.err:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = secteur, x1 = nombre, x2 = destination (hors cache) -> x0 = 1
blk_read:
	mov	x3,0
	b	blk_direct
; x0 = secteur, x1 = nombre, x2 = source -> x0 = 1 (et le cache invalide)
blk_write:
	mov	x3,1
blk_direct:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	adr	x23,cache			; zone de transfert : apres le cache
	ldr	x23,[x23]
	add	x23,x23,BC_N * 512
.pass:	cbz	x20,.ok
	cmp	x20,BLOC_N
	mov	x24,BLOC_N
	csel	x24,x20,x24,lo
	cbz	x22,.lit
	mov	x0,x23				; ecriture : source -> zone
	mov	x1,x21
	lsl	x2,x24,9
	bl	mem_move
	mov	x0,BLK_ECRIRE
	mov	x1,x19
	mov	x2,x24
	mov	x3,x23
	bl	blk_requete
	cbnz	x0,.err
	mov	x0,x19				; secteurs ecrits : hors du cache
	mov	x1,x24
	bl	bc_oublier
	b	.next
.lit:	adr	x0,cc_skip
	ldr	x0,[x0]
	cbnz	x0,.nocc
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	cc_read
	cbz	x0,.nocc
	mov	x24,x0
	b	.next
.nocc:	adr	x0,direct			; dans la memoire de l'appelant ?
	ldr	x0,[x0]
	cbz	x0,.zone
	tst	x21,3
	b.ne	.zone
	mov	x0,BLK_LIRE_D
	mov	x1,x19
	mov	x2,x20
	mov	x3,x21
	bl	blk_requete
	cbnz	x0,.zone			; (pas possible : par la zone)
	mov	x24,x20
	b	.next
.zone:	mov	x0,BLK_LIRE
	mov	x1,x19
	mov	x2,x24
	mov	x3,x23
	bl	blk_requete
	cbnz	x0,.err
	mov	x0,x21
	mov	x1,x23
	lsl	x2,x24,9
	bl	mem_move
.next:	add	x19,x19,x24
	sub	x20,x20,x24
	add	x21,x21,x24,lsl 9
	b	.pass
.ok:	mov	x0,1
	b	.out
.err:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = secteur, x1 = nombre : retires du cache
bc_oublier:
	adr	x3,bc_tags
.l:	cbz	x1,.out
	and	x4,x0,BC_N-1
	ldr	x5,[x3,x4,lsl 3]
	add	x6,x0,1
	cmp	x5,x6
	b.ne	.n
	str	xzr,[x3,x4,lsl 3]
	adr	x5,bc_dirty			; (ecrit depuis : plus rien a ecrire)
	strb	wzr,[x5,x4]
.n:	add	x0,x0,1
	sub	x1,x1,1
	b	.l
.out:	ret

; ---------------------------------------------------------------------------
;  Cache de donnees : blocs de 64 Ko du peripherique (numero = secteur / 128),
;  commun a FAT32 et exFAT. Une region a la demande de ce process ; un bloc
;  neuf est peuple d'abord (MADV_WILLNEED : E_NOMEM plutot qu'une faute qui
;  tuerait le service). Quand la memoire manque, le noyau leve SVC_SIG_LOWMEM
;  (SVF_MEMORY, svc.asm) : chaque signal rend la moitie du cache, et il ne
;  regrandit qu'au-dessus de 2 x CC_RESERVE pages libres.
;  Table de hachage (cc_head, cc_next : indice + 1),
;  remplacement par horloge (cc_refs). Lecture : par la zone de transfert, qui
;  marche pour tous les pilotes, la memoire du cache etant souvent au-dessus de
;  SD_DMA_LIMIT. Rien de differe ici : une coupure n'y perd rien.
; ---------------------------------------------------------------------------

; (start) la region, a sa taille la plus grande ; permis d'abord : 1/8 de la
; memoire libre (au montage de sd0, la zone haute n'est pas encore la :
; cc_pressure le revoit)
cc_init:
	prologue
	sub	sp,sp,64
	mov	x0,0
	mov	x1,sp
	sys	SYS_VM_STATS
	tbnz	x0,63,.out
	ldp	x0,x1,[sp,VS_FREE_LOW]
	add	x0,x0,x1
	lsr	x0,x0,7				; pages / 8, en blocs de 16 pages
	mov	x1,CC_OTHER_MAX
	adr	x2,nom_svc
	ldr	w2,[x2,3]			; "fs.sd0"
	movl	w3,0x00306473
	cmp	w2,w3
	b.ne	.cap
	mov	x1,CC_BLOCKS_MAX
.cap:	adr	x2,cc_max
	str	x1,[x2]
	cmp	x0,x1
	csel	x0,x1,x0,hi
	cmp	x0,64				; trop peu de memoire : pas de cache
	b.lo	.out
	adr	x2,cc_cap
	str	x0,[x2]
	adr	x2,cc_limit
	str	x0,[x2]
	lsl	x1,x1,16
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.out
	adr	x1,cc_base
	str	x0,[x1]
.out:	add	sp,sp,64
	epilogue

; x0 = bloc -> x0 = son indice, -1 s'il n'est pas la (ne touche que x0-x5)
cc_find:
	add	x5,x0,1
	adr	x1,cc_head
	and	x2,x0,CC_HASH-1
	ldr	w3,[x1,x2,lsl 2]
	adr	x1,cc_tags
	adr	x2,cc_next
.l:	cbz	w3,.no
	sub	x3,x3,1
	ldr	x4,[x1,x3,lsl 3]
	cmp	x4,x5
	b.eq	.yes
	ldr	w3,[x2,x3,lsl 2]
	b	.l
.yes:	mov	x0,x3
	ret
.no:	mov	x0,-1
	ret

; x0 = indice : retire de la table (ne touche que x1-x4)
cc_unlink:
	adr	x1,cc_tags
	ldr	x2,[x1,x0,lsl 3]
	cbz	x2,.out
	str	xzr,[x1,x0,lsl 3]
	sub	x2,x2,1
	and	x2,x2,CC_HASH-1
	adr	x1,cc_head
	add	x1,x1,x2,lsl 2			; le lien qui le designe
	adr	x3,cc_next
	add	x4,x0,1
.l:	ldr	w2,[x1]
	cbz	w2,.out
	cmp	w2,w4
	b.eq	.hit
	sub	x2,x2,1
	add	x1,x3,x2,lsl 2
	b	.l
.hit:	ldr	w2,[x3,x0,lsl 2]
	str	w2,[x1]
.out:	ret

; x0 = secteur, x1 = nombre : les blocs touches quittent le cache
cc_forget:
	cbz	x1,.ret
	adr	x2,cc_used
	ldr	x2,[x2]
	cbz	x2,.ret
	prologue
	stp	x19,x20,[sp,-16]!
	add	x20,x0,x1
	sub	x20,x20,1
	lsr	x20,x20,CC_BLOCK_SHIFT
	lsr	x19,x0,CC_BLOCK_SHIFT
.b:	mov	x0,x19
	bl	cc_find
	tbnz	x0,63,.n
	bl	cc_unlink
.n:	add	x19,x19,1
	cmp	x19,x20
	b.ls	.b
	ldp	x19,x20,[sp],16
	epilogue
.ret:	ret

; Tout est rendu (commande fscache)
cc_drop:
	mov	x0,0
; x0 = blocs gardes : ceux d'au-dela quittent la table, leurs pages sont rendues
cc_shrink:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,cc_used
	ldr	x20,[x1]
	cmp	x19,x20
	b.hs	.out
	str	x19,[x1]
	adr	x1,cc_hand
	str	xzr,[x1]
.u:	bl	cc_unlink
	add	x0,x0,1
	cmp	x0,x20
	b.lo	.u
	adr	x0,cc_base
	ldr	x0,[x0]
	add	x0,x0,x19,lsl 16
	sub	x1,x20,x19
	lsl	x1,x1,16
	mov	x2,MADV_DONTNEED
	sys	SYS_VM_ADVISE
.out:	ldp	x19,x20,[sp],16
	epilogue

; (SVC_SIG_LOWMEM, memoire libre trop basse) la moitie du cache est rendue, et
; il ne regrandit pas tant que la memoire reste rare
cc_give_back:
	prologue
	adr	x1,cc_used
	ldr	x0,[x1]
	lsr	x0,x0,1
	cmp	x0,64				; (moins de 4 Mo : ne sert plus a rien)
	csel	x0,xzr,x0,lo
	adr	x1,cc_limit
	str	x0,[x1]
	bl	cc_shrink
	epilogue

; (tous les 64 defauts, au repos) la memoire libre : moins de CC_RESERVE pages,
; le cache rend ; plus du double, il peut regrandir
cc_pressure:
	prologue
	adr	x0,cc_base
	ldr	x0,[x0]
	cbz	x0,.out
	sub	sp,sp,64
	mov	x0,0
	mov	x1,sp
	sys	SYS_VM_STATS
	ldp	x1,x2,[sp,VS_FREE_LOW]
	add	sp,sp,64
	tbnz	x0,63,.out
	add	x1,x1,x2
	movl	x2,CC_RESERVE
	cmp	x1,x2
	b.hs	.ok
	bl	cc_give_back
	b	.out
.ok:	lsr	x3,x1,7				; 1/8, en blocs de 16 pages
	adr	x0,cc_max
	ldr	x0,[x0]
	cmp	x3,x0
	csel	x3,x0,x3,hi
	adr	x0,cc_cap
	ldr	x4,[x0]
	cmp	x3,x4
	csel	x3,x3,x4,hi
	str	x3,[x0]
	cmp	x1,x2,lsl 1
	b.lo	.out
	adr	x1,cc_limit
	str	x3,[x1]
.out:	epilogue

; -> x0 = indice d'un bloc libre (hors table), -1 si aucun
cc_take:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,cc_used
	ldr	x20,[x19]
	adr	x0,cc_limit
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.sweep
	adr	x0,cc_base			; un bloc neuf : peuple tout de suite
	ldr	x0,[x0]
	add	x0,x0,x20,lsl 16
	mov	x1,CC_BLOCK_BYTES
	mov	x2,MADV_WILLNEED
	sys	SYS_VM_ADVISE
	cbnz	x0,.full
	add	x0,x20,1
	str	x0,[x19]
	mov	x0,x20
	b	.out
.full:	adr	x0,cc_limit			; plus de memoire : on s'en tient la
	str	x20,[x0]
.sweep:	cbz	x20,.none
	adr	x1,cc_hand
	ldr	x2,[x1]
	adr	x3,cc_tags
	adr	x4,cc_refs
	lsl	x5,x20,1			; (deux tours au plus)
.s:	cmp	x2,x20
	csel	x2,xzr,x2,hs
	add	x6,x2,1
	ldr	x0,[x3,x2,lsl 3]
	cbz	x0,.got
	ldrb	w0,[x4,x2]
	cbz	w0,.old
	strb	wzr,[x4,x2]
	mov	x2,x6
	subs	x5,x5,1
	b.ne	.s
	b	.none
.old:	adr	x0,cc_evicts
	ldr	x7,[x0]
	add	x7,x7,1
	str	x7,[x0]
.got:	str	x6,[x1]
	mov	x0,x2
	str	x0,[sp,-16]!
	bl	cc_unlink
	ldr	x0,[sp],16
	b	.out
.none:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = bloc -> x0 = son adresse dans le cache (lu au besoin), 0 si pas possible
cc_block:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	cc_find
	tbnz	x0,63,.miss
	mov	x20,x0
	adr	x1,cc_refs
	mov	w2,1
	strb	w2,[x1,x20]
	adr	x1,cc_hits
	b	.count
.miss:	add	x0,x19,1			; (dernier bloc incomplet : hors cache)
	lsl	x0,x0,CC_BLOCK_SHIFT
	adr	x1,cc_disk
	ldr	x1,[x1]
	cmp	x0,x1
	b.hi	.none
	adr	x20,cc_misses
	ldr	x0,[x20]
	add	x0,x0,1
	str	x0,[x20]
	tst	x0,CC_CHECK_MASK
	b.ne	.take
	bl	cc_pressure
.take:	bl	cc_take
	tbnz	x0,63,.none
	mov	x20,x0
	adr	x21,cache			; zone de transfert
	ldr	x21,[x21]
	add	x21,x21,BC_N * 512
	mov	x0,BLK_LIRE
	lsl	x1,x19,CC_BLOCK_SHIFT
	mov	x2,BLOC_N
	mov	x3,x21
	bl	blk_requete
	cbnz	x0,.none
	adr	x0,cc_base
	ldr	x0,[x0]
	add	x0,x0,x20,lsl 16
	mov	x1,x21
	mov	x2,CC_BLOCK_BYTES
	bl	mem_move
	adr	x0,cc_refs
	mov	w1,1
	strb	w1,[x0,x20]
	adr	x0,cc_tags			; dans la table
	add	x1,x19,1
	str	x1,[x0,x20,lsl 3]
	adr	x0,cc_head
	and	x1,x19,CC_HASH-1
	ldr	w2,[x0,x1,lsl 2]
	adr	x3,cc_next
	str	w2,[x3,x20,lsl 2]
	add	x2,x20,1
	str	w2,[x0,x1,lsl 2]
	b	.addr
.count:	ldr	x0,[x1]
	add	x0,x0,1
	str	x0,[x1]
.addr:	adr	x0,cc_base
	ldr	x0,[x0]
	add	x0,x0,x20,lsl 16
	b	.out
.none:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = secteur, x1 = nombre, x2 = destination -> x0 = secteurs lus, 0 : passer
; par le chemin ordinaire
cc_read:
	adr	x3,cc_base
	ldr	x3,[x3]
	cbz	x3,.none
	adr	x3,cc_off
	ldr	x3,[x3]
	cbnz	x3,.none
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0
.b:	cbz	x20,.fin
	lsr	x0,x19,CC_BLOCK_SHIFT
	bl	cc_block
	cbz	x0,.fin
	and	x1,x19,BLOC_N-1
	add	x1,x0,x1,lsl 9
	mov	x2,BLOC_N
	and	x3,x19,BLOC_N-1
	sub	x2,x2,x3
	cmp	x2,x20
	csel	x2,x20,x2,hi
	add	x19,x19,x2
	sub	x20,x20,x2
	add	x22,x22,x2
	lsl	x2,x2,9
	mov	x0,x21
	add	x21,x21,x2
	bl	mem_move
	b	.b
.fin:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.none:	mov	x0,0
	ret

; 18 cache(a : 0 lire, FS_CC_ON, FS_CC_OFF, FS_CC_DROP) -> a succes, b defauts,
; c blocs en memoire, d blocs remplaces
op_cache:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,MSG_A]
	cbz	x0,.read
	cmp	x0,FS_CC_ON
	b.eq	.on
	bl	cc_drop				; (FS_CC_DROP, FS_CC_OFF)
	ldr	x0,[x19,MSG_A]
	cmp	x0,FS_CC_OFF
	b.ne	.read
	mov	x0,1
	b	.set
.on:	mov	x0,0
.set:	adr	x1,cc_off
	str	x0,[x1]
.read:	adr	x0,cc_hits
	ldr	x0,[x0]
	adr	x1,cc_misses
	ldr	x1,[x1]
	stp	x0,x1,[x19,MSG_A]
	adr	x0,cc_used
	ldr	x0,[x0]
	adr	x1,cc_evicts
	ldr	x1,[x1]
	stp	x0,x1,[x19,MSG_C]
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; 19 info -> a octets du volume, b octets libres (-1 : inconnu), c octets par
; cluster, d FS_T_* ; donnees : etiquette (UTF-8, vide : aucune)
op_info:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	adr	x20,volume
	ldr	x0,[x20,V_NCLUST]
	ldr	x1,[x20,V_CSHIFT]
	lslv	x0,x0,x1
	str	x0,[x19,MSG_A]
	mov	x0,1
	lslv	x0,x0,x1
	str	x0,[x19,MSG_C]
	ldr	x0,[x20,V_TYPE]
	str	x0,[x19,MSG_D]
	strb	wzr,[x19,MSG_DATA]
	cmp	x0,FS_T_EXFAT
	b.eq	.ex
	ldr	x0,[x20,V_FREE]			; FAT32 : FSInfo, tenu a jour
	tbnz	x0,63,.f1
	ldr	x1,[x20,V_CSHIFT]
	lslv	x0,x0,x1
.f1:	str	x0,[x19,MSG_B]
	ldr	x0,[x20,V_PSTART]		; etiquette du secteur de demarrage
	bl	bc_get
	cbz	x0,.ok
	add	x0,x0,71
	add	x1,x19,MSG_DATA
	mov	x2,0
	mov	x3,0				; longueur sans les blancs de la fin
.fl:	ldrb	w4,[x0,x2]
	strb	w4,[x1,x2]
	add	x2,x2,1
	cmp	w4,' '
	csel	x3,x2,x3,ne
	cmp	x2,11
	b.lo	.fl
	strb	wzr,[x1,x3]
	ldr	x0,[x1]				; (NO NAME : aucune)
	movl	x2,0x20454D414E204F4E
	cmp	x0,x2
	b.ne	.ok
	strb	wzr,[x1]
	b	.ok
.ex:	ldr	x22,[x20,V_NCLUST]		; exFAT : les bits du bitmap
	mov	x21,0				; clusters pris
	ldr	x23,[x20,V_EX_BMLBA]
.sec:	cbz	x22,.exd
	mov	x0,x23
	bl	bc_get
	cbz	x0,.exu
	add	x23,x23,1
	mov	x1,0
.oct:	cbz	x22,.exd
	ldrb	w2,[x0,x1]
	add	x1,x1,1
	cmp	x22,8
	b.hs	.pop
	mov	w3,1
	lslv	w3,w3,w22
	sub	w3,w3,1
	and	w2,w2,w3
	mov	x22,8
.pop:	sub	x22,x22,8
.b:	cbz	w2,.nb
	sub	w3,w2,1
	and	w2,w2,w3
	add	x21,x21,1
	b	.b
.nb:	cmp	x1,512
	b.lo	.oct
	b	.sec
.exu:	mov	x0,-1
	b	.exs
.exd:	ldr	x0,[x20,V_NCLUST]
	sub	x0,x0,x21
	ldr	x1,[x20,V_CSHIFT]
	lslv	x0,x0,x1
.exs:	str	x0,[x19,MSG_B]
	mov	x21,0				; etiquette : entree 0x83 de la racine
.l:	mov	x0,x20
	ldr	x1,[x20,V_ROOT]
	mov	x2,x21
	bl	ex_entry_at
	cbz	x0,.ok
	ldrb	w1,[x0]
	cbz	w1,.ok
	add	x21,x21,1
	cmp	w1,0x83
	b.ne	.l
	ldrb	w1,[x0,1]			; caracteres (11 au plus), UTF-16
	cmp	w1,11
	mov	w2,11
	csel	w1,w2,w1,hi
	add	x2,x19,MSG_DATA
	mov	x3,0
.c:	cmp	w3,w1
	b.hs	.cf
	add	x4,x0,2
	ldrh	w5,[x4,x3,lsl 1]
	add	x3,x3,1
	cmp	w5,0x80
	b.hs	.c2
	strb	w5,[x2],1
	b	.c
.c2:	cmp	w5,0x800
	b.hs	.c3
	lsr	w6,w5,6
	orr	w6,w6,0xC0
	strb	w6,[x2],1
	b	.cl
.c3:	lsr	w6,w5,12
	orr	w6,w6,0xE0
	strb	w6,[x2],1
	ubfx	w6,w5,6,6
	orr	w6,w6,0x80
	strb	w6,[x2],1
.cl:	and	w6,w5,0x3F
	orr	w6,w6,0x80
	strb	w6,[x2],1
	b	.c
.cf:	strb	wzr,[x2]
.ok:	mov	x0,0
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = adresse, x1 = taille (multiple de 16) : mise a zero
mem_zero:
.z:	stp	xzr,xzr,[x0],16
	subs	x1,x1,16
	b.hi	.z
	ret

; x0 = destination, x1 = source, x2 = octets (fo_rw, fat32.asm)
mem_copy:
	b	mem_move

; ---------------------------------------------------------------------------
;  Objets fichiers : repris tels quels de src/kernel/vfs.asm
; ---------------------------------------------------------------------------

; x0 = objet fichier, x1 = DE : copie la description de l'entree
fo_from_de:
	ldr	x2,[x1,DE_CLUSTER]
	str	x2,[x0,FO_START]
	str	xzr,[x0,FO_CCLUS]
	str	xzr,[x0,FO_CIDX]
	ldr	x2,[x1,DE_FSIZE]
	str	x2,[x0,FO_SIZE]
	ldr	x2,[x1,DE_DIR]
	str	x2,[x0,FO_PDIR]
	ldr	x2,[x1,DE_INDEX]
	str	x2,[x0,FO_INDEX]
	ldr	x2,[x1,DE_FIRST]
	str	x2,[x0,FO_FIRST]
	ldr	x2,[x1,DE_ATTR]
	str	x2,[x0,FO_ATTR]
	ret

; x0 = objet fichier : place FO_CCLUS/FO_CIDX sur le cluster de rang x1,
; en allouant la chaine si x2 = 1 ; x3 = 1 : ce cluster de rang x1 sera ecrit
; en entier, sans zeros s'il est neuf -> x0 = cluster (0 si fin / disque plein)
fo_seek_cluster:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x3
	ldr	x22,[x19,FO_VOL]
	ldr	x0,[x19,FO_START]
	cbnz	x0,.has_start
	cbz	x21,.fail
	mov	x0,x22				; fichier vide : premier cluster
	mov	x1,0
	cmp	x20,0
	csel	x2,x23,xzr,eq
	bl	fat_alloc_x
	cbz	x0,.fail
	str	x0,[x19,FO_START]
	mov	x1,1
	str	x1,[x19,FO_DIRTY]
	str	xzr,[x19,FO_CCLUS]
.has_start:
	ldr	x0,[x19,FO_CCLUS]
	ldr	x1,[x19,FO_CIDX]
	cbz	x0,.rewind
	cmp	x1,x20
	b.ls	.walk
.rewind:
	ldr	x0,[x19,FO_START]
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
	stp	x1,xzr,[sp,-16]!
	add	x3,x1,1
	cmp	x3,x20
	csel	x3,x23,xzr,eq
	mov	x0,x22
	mov	x1,x2
	mov	x2,x3
	bl	fat_alloc_x
	ldp	x1,x2,[sp],16
	cbz	x0,.fail
.step:	add	x1,x1,1
	b	.walk
.here:	str	x0,[x19,FO_CCLUS]
	str	x1,[x19,FO_CIDX]
	b	.out
.fail:	mov	x0,0
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = objet fichier, x1 = tampon, x2 = octets, x3 = 0 lire / 1 ecrire
; -> x0 = octets transferes (ou erreur)
fo_rw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x26,0				; total transfere
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
	lsrv	x1,x24,x0			; rang du cluster
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
	bl	fo_seek_cluster
	cbz	x0,.done
	mov	x1,x0
	mov	x0,x23
	bl	fat_clus_lba			; x0 = secteur du cluster, x1 = cshift
	mov	x2,1
	lslv	x2,x2,x1
	sub	x2,x2,1
	and	x3,x24,x2			; offset dans le cluster
	add	x25,x0,x3,lsr 9			; secteur vise
	and	x4,x24,511			; offset dans le secteur
	cbnz	x4,.partial
	cmp	x21,512
	b.lo	.partial
	; secteurs entiers jusqu'a la fin du cluster
	sub	x5,x2,x3
	add	x5,x5,1
	lsr	x5,x5,9				; secteurs restant dans le cluster
	lsr	x6,x21,9
	cmp	x6,x5
	csel	x6,x5,x6,hi
.more:	lsr	x7,x21,9			; les clusters contigus avec
	cmp	x6,x7				; (une seule commande de la carte)
	b.hs	.run
	stp	x5,x6,[sp,-16]!
	mov	x0,x23
	ldr	x1,[x19,FO_CCLUS]
	bl	fat_next
	ldp	x5,x6,[sp],16
	movl	x1,FAT_EOC
	cmp	x0,x1
	b.lo	.adj
	cbz	x22,.run
	lsr	x7,x21,9			; fin de chaine : un cluster de plus,
	sub	x7,x7,x5			; s'il sera ecrit en entier
	ldr	x1,[x23,V_SPC]
	cmp	x7,x1
	b.lo	.run
	stp	x5,x6,[sp,-16]!
	mov	x0,x23
	ldr	x1,[x19,FO_CCLUS]
	mov	x2,1
	bl	fat_alloc_x
	ldp	x5,x6,[sp],16
	cbz	x0,.run
.adj:	ldr	x1,[x19,FO_CCLUS]
	add	x1,x1,1
	cmp	x0,x1
	b.ne	.run
	str	x0,[x19,FO_CCLUS]		; (fo_seek_cluster repart d'ici)
	ldr	x1,[x19,FO_CIDX]
	add	x1,x1,1
	str	x1,[x19,FO_CIDX]
	ldr	x0,[x23,V_CSHIFT]
	sub	x0,x0,9
	mov	x1,1
	lslv	x1,x1,x0			; secteurs par cluster
	add	x5,x5,x1
	lsr	x6,x21,9
	cmp	x6,x5
	csel	x6,x5,x6,hi
	b	.more
.run:	mov	x0,x25
	mov	x1,x6
	mov	x2,x20
	lsl	x24,x6,9			; octets
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
	stp	x4,xzr,[sp,-16]!
	bl	bc_get
	ldp	x4,x5,[sp],16
	cbz	x0,.done
	mov	x5,512
	sub	x5,x5,x4
	cmp	x5,x21
	csel	x5,x21,x5,hi			; octets dans ce secteur
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
	str	x0,[x19,FO_SIZE]		; le fichier grandit
	mov	x0,1
	str	x0,[x19,FO_DIRTY]
	b	.loop
.done:	mov	x0,x26
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue


; ---------------------------------------------------------------------------
;  Fichiers ouverts
; ---------------------------------------------------------------------------

E_MFILE		= -24

; -> x0 = objet libre (mis a zero), 0 si aucun
fo_libre:
	adr	x0,ouverts
	mov	x1,OUV_MAX
.f:	ldr	x2,[x0,FO_TYPE]
	cbz	x2,.got
	add	x0,x0,FO_SIZE_
	subs	x1,x1,1
	b.ne	.f
	mov	x0,0
	ret
.got:	mov	x1,0
.z:	str	xzr,[x0,x1]
	add	x1,x1,8
	cmp	x1,FO_SIZE_
	b.lo	.z
	ret

; x0 = numero -> x0 = objet ouvert, 0 si invalide
fo_de:
	cmp	x0,OUV_MAX
	b.hs	.bad
	adr	x1,ouverts
	mov	x2,FO_SIZE_
	madd	x0,x0,x2,x1
	ldr	x1,[x0,FO_TYPE]
	cbz	x1,.bad
	ret
.bad:	mov	x0,0
	ret

; x0 = message -> x0 = objet ouvert (numero en a) si l'appelant y a droit,
; E_INVAL (numero libre) ou E_BADF (autre proprietaire)
fo_acces:
	str	x30,[sp,-16]!
	mov	x4,x0
	ldr	x0,[x4,MSG_A]
	bl	fo_de
	cbz	x0,.inval
	mov	x5,x0
	ldr	x1,[x5,FO_PROPRIO]
	mov	x0,x4
	bl	acces
	cbnz	x0,.out
	mov	x0,x5
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Noeuds : l'etat partage d'un fichier ouvert
; ---------------------------------------------------------------------------

; x0 = DE (DE_DIR, DE_INDEX) -> x0 = noeud ouvert de ce fichier, 0 si aucun
; (ne touche que x0-x5)
no_trouver:
	adr	x1,noeuds
	mov	x2,NO_MAX
	ldr	w3,[x0,DE_DIR]			; (exFAT : poignee, 32 bits bas)
	ldr	x4,[x0,DE_INDEX]
.f:	ldr	x5,[x1,NO_REFS]
	cbz	x5,.n
	ldr	x5,[x1,NO_EFFACE]
	cbnz	x5,.n
	ldr	w5,[x1,NO_DIR]
	cmp	w5,w3
	b.ne	.n
	ldr	x5,[x1,NO_INDEX]
	cmp	x5,x4
	b.ne	.n
	mov	x0,x1
	ret
.n:	add	x1,x1,NO_SIZE_
	subs	x2,x2,1
	b.ne	.f
	mov	x0,0
	ret

; x0 = DE d'un fichier -> x0 = son noeud, une ouverture de plus (cree
; d'apres la DE s'il n'etait pas ouvert), 0 si la table est pleine
no_prendre:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	no_trouver
	cbnz	x0,.plus
	adr	x0,noeuds
	mov	x1,NO_MAX
.f:	ldr	x2,[x0,NO_REFS]
	cbz	x2,.got
	add	x0,x0,NO_SIZE_
	subs	x1,x1,1
	b.ne	.f
	mov	x0,0
	b	.out
.got:	mov	x1,0				; a zero, puis les champs de la DE
.z:	str	xzr,[x0,x1]
	add	x1,x1,8
	cmp	x1,NO_SIZE_
	b.lo	.z
	mov	x1,0
.c:	ldr	x2,[x19,x1]
	str	x2,[x0,x1]
	add	x1,x1,8
	cmp	x1,NO_REFS
	b.lo	.c
.plus:	ldr	x1,[x0,NO_REFS]
	add	x1,x1,1
	str	x1,[x0,NO_REFS]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = objet fichier : son etat <- celui du noeud (avant vol_rw ; ne touche
; que x1, x2)
fo_charger:
	ldr	x1,[x0,FO_NOEUD]
	cbz	x1,.r
	ldr	x2,[x1,NO_START]
	str	x2,[x0,FO_START]
	ldr	x2,[x1,NO_SIZE]
	str	x2,[x0,FO_SIZE]
	ldr	x2,[x1,NO_CCLUS]
	str	x2,[x0,FO_CCLUS]
	ldr	x2,[x1,NO_CIDX]
	str	x2,[x0,FO_CIDX]
	str	xzr,[x0,FO_DIRTY]
.r:	ret

; x0 = objet fichier : son etat -> le noeud (apres vol_rw ; ne touche que
; x1, x2)
fo_ranger:
	ldr	x1,[x0,FO_NOEUD]
	cbz	x1,.r
	ldr	x2,[x0,FO_START]
	str	x2,[x1,NO_START]
	ldr	x2,[x0,FO_SIZE]
	str	x2,[x1,NO_SIZE]
	ldr	x2,[x0,FO_CCLUS]
	str	x2,[x1,NO_CCLUS]
	ldr	x2,[x0,FO_CIDX]
	str	x2,[x1,NO_CIDX]
	ldr	x2,[x0,FO_DIRTY]
	cbz	x2,.r
	str	x2,[x1,NO_DIRTY]
.r:	ret

; x0 = volume, x1 = noeud : O_TRUNC, le fichier est vide pour toutes ses
; ouvertures ; chaine liberee, entree reecrite tout de suite
no_tronquer:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x20,NO_DIRTY]		; disque a jour d'abord (exFAT :
	cbz	x0,.vide			; vol_maj libere l'allocation ecrite)
	mov	x0,x19
	mov	x1,x20
	bl	vol_maj
.vide:	ldr	x21,[x20,NO_START]
	str	xzr,[x20,NO_START]
	str	xzr,[x20,NO_SIZE]
	str	xzr,[x20,NO_CCLUS]
	str	xzr,[x20,NO_CIDX]
	mov	x0,x19				; FAT32 : la chaine (une poignee
	mov	x1,x21				; exFAT est ignoree)
	bl	fat_free_chain
	mov	x0,x19
	mov	x1,x20
	bl	vol_maj
	str	xzr,[x20,NO_DIRTY]
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 25 sync -> a (et resultat) : fichiers ouverts ; l'entree de repertoire de
; chaque fichier ouvert modifie est reecrite (taille, cluster) : le volume est
; coherent meme si le service s'arrete ensuite (eject, mkfs ; FSInfo : apres
; chaque requete)
op_sync:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x22,x0
	adr	x19,noeuds
	mov	x20,NO_MAX
.n:	ldr	x0,[x19,NO_REFS]
	cbz	x0,.nn
	ldr	x0,[x19,NO_EFFACE]
	cbnz	x0,.nn
	ldr	x0,[x19,NO_DIRTY]
	cbz	x0,.nn
	adr	x0,volume
	mov	x1,x19
	bl	vol_maj
	str	xzr,[x19,NO_DIRTY]
.nn:	add	x19,x19,NO_SIZE_
	subs	x20,x20,1
	b.ne	.n
	adr	x19,ouverts			; fichiers ouverts (pas les repertoires)
	mov	x20,OUV_MAX
	mov	x21,0
.o:	ldr	x0,[x19,FO_TYPE]
	cmp	x0,1
	cinc	x21,x21,eq
	add	x19,x19,FO_SIZE_
	subs	x20,x20,1
	b.ne	.o
	str	x21,[x22,MSG_A]
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid termine : ses fichiers fermes, son tampon rendu
fin_pid:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x19,ouverts
	mov	x20,OUV_MAX
.f:	ldr	x0,[x19,FO_TYPE]
	cbz	x0,.n
	ldr	x0,[x19,FO_PROPRIO]
	cmp	x0,x21
	b.ne	.n
	mov	x0,x19
	bl	fo_fermer
.n:	add	x19,x19,FO_SIZE_
	subs	x20,x20,1
	b.ne	.f
	mov	x0,x21
	bl	tampons_fin
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si le volume est en lecture seule
ro:
	adr	x0,lecture_seule
	ldr	x0,[x0]
	ret

; ---------------------------------------------------------------------------
;  Operations (x0 = message -> x0 = resultat)
; ---------------------------------------------------------------------------

; 1 ouvrir(a drapeaux, chemin) -> b numero, c attributs, d taille
op_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,DE_SIZE
	mov	x19,x0
	bl	open_extras_take		; (attributs demandes par vfs)
	ldr	x20,[x19,MSG_A]			; drapeaux
	tst	x20,O_WRITE or O_CREATE or O_TRUNC or O_APPEND
	b.eq	.lu
	bl	ro
	cbnz	x0,.perm
.lu:	adr	x21,volume
	add	x22,x19,MSG_DATA		; chemin
	mov	x0,x21
	mov	x1,x22
	mov	x2,sp
	bl	vol_chercher
	cbnz	x0,.existe
	tst	x20,O_CREATE			; creation : le parent doit exister
	b.eq	.noent
	mov	x0,x22
	mov	x23,x22
.last:	ldrb	w1,[x0],1
	cbz	w1,.split
	cmp	w1,'/'
	b.ne	.last
	mov	x23,x0				; debut du dernier composant
	b	.last
.split:	cmp	x23,x22
	b.eq	.racine
	sub	x24,x23,1
	strb	wzr,[x24]			; « parent/nom » -> « parent »
	mov	x0,x21
	mov	x1,x22
	mov	x2,sp
	bl	vol_chercher
	mov	w1,'/'
	strb	w1,[x24]
	cbz	x0,.noent
	ldr	x1,[sp,DE_ATTR]
	tst	x1,ATTR_DIR
	b.eq	.notdir
	ldr	x1,[sp,DE_CLUSTER]
	b	.creer
.racine:
	ldr	x1,[x21,V_ROOT]
.creer:	str	x1,[sp,-16]!
	mov	x0,x23				; nom de plus de 255 caracteres ?
	bl	nom_trop_long
	ldr	x1,[sp],16
	cbnz	x0,.toolong
	mov	x0,x21
	mov	x2,x23
	mov	x3,ATTR_ARCHIVE
	mov	x4,sp
	bl	vol_creer
	cbz	x0,.nospc
.existe:
	bl	fo_libre
	cbz	x0,.mfile
	mov	x24,x0
	str	x21,[x24,FO_VOL]
	mov	x1,sp
	bl	fo_from_de
	mov	x0,x19
	bl	proprietaire
	str	x0,[x24,FO_PROPRIO]
	ldr	x0,[x24,FO_ATTR]
	mov	x1,1				; fichier
	tst	x0,ATTR_DIR
	b.eq	.typed
	tst	x20,O_WRITE
	b.ne	.isdir
	mov	x1,2				; repertoire : iterateur au debut
	ldr	x2,[x24,FO_START]
	str	x2,[x24,FO_CCLUS]
.typed:	str	x1,[x24,FO_TYPE]
	str	x20,[x24,FO_FLAGS]
	cmp	x1,2
	b.eq	.rend
	mov	x0,sp				; fichier : son noeud (partage)
	bl	no_prendre
	cbz	x0,.mfile_l
	str	x0,[x24,FO_NOEUD]
	tst	x20,O_TRUNC
	b.eq	.charge
	mov	x1,x0				; troncature : pour toutes les ouvertures
	mov	x0,x21
	bl	no_tronquer
.charge:
	mov	x0,x24				; taille et chaine du noeud
	bl	fo_charger
.rend:	adr	x0,ouverts			; numero, attributs, taille
	sub	x0,x24,x0
	mov	x1,FO_SIZE_
	udiv	x0,x0,x1
	str	x0,[x19,MSG_B]
	ldr	x0,[x24,FO_ATTR]
	ldr	x1,[x24,FO_SIZE]
	stp	x0,x1,[x19,MSG_C]
	mov	x0,x19				; a : notre canal (vfs transmet)
	bl	open_extras_put
	mov	x0,0
	b	.out
.isdir:	str	xzr,[x24,FO_TYPE]
	mov	x0,E_ISDIR
	b	.out
.mfile_l:
	str	xzr,[x24,FO_TYPE]
.mfile:	mov	x0,E_MFILE
	b	.out
.notdir:
	mov	x0,E_NOTDIR
	b	.out
.nospc:	mov	x0,E_NOSPC
	b	.out
.toolong:
	mov	x0,E_NAMETOOLONG
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.noent:	mov	x0,E_NOENT
.out:	add	sp,sp,DE_SIZE
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom UTF-8 (fin 0 ou '/') -> x0 = 1 s'il depasse 255 caracteres UTF-16
; (4 octets UTF-8 : deux) ou NAME_BYTES-3 octets
nom_trop_long:
	mov	x1,0				; caracteres
	mov	x2,0				; octets
.c:	ldrb	w3,[x0,x2]
	cbz	w3,.fin
	cmp	w3,'/'
	b.eq	.fin
	add	x2,x2,1
	and	w4,w3,0xC0			; octet de suite : pas un caractere
	cmp	w4,0x80
	b.eq	.c
	add	x1,x1,1
	cmp	w3,0xF0
	cinc	x1,x1,hs
	b	.c
.fin:	cmp	x1,NAME_UNITS
	cset	x0,hi
	cmp	x2,NAME_BYTES-3
	csinc	x0,x0,xzr,ls
	ret

; 2 stat(chemin) -> b taille, c attributs
op_stat:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,DE_SIZE
	mov	x19,x0
	adr	x0,volume
	add	x1,x19,MSG_DATA
	mov	x2,sp
	bl	vol_chercher
	cbz	x0,.noent
	mov	x0,sp				; ouvert : la taille de son noeud
	bl	no_taille
	ldr	x1,[sp,DE_ATTR]
	stp	x0,x1,[x19,MSG_B]
	ldr	w0,[sp,DE_MTIME]
	bl	dos_vers_utc
	str	x0,[x19,MSG_D]
	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
.out:	add	sp,sp,DE_SIZE
	ldr	x19,[sp],16
	epilogue

; 5 utime(b secondes UTC ; donnees : chemin) : la date de l'entree du repertoire
op_utime:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,DE_SIZE
	mov	x19,x0
	bl	ro
	cbnz	x0,.perm
	adr	x0,volume
	add	x1,x19,MSG_DATA
	mov	x2,sp
	bl	vol_chercher
	cbz	x0,.noent
	ldr	x0,[x19,MSG_B]
	bl	utc_vers_dos
	adr	x1,dos_date_forcee
	str	x0,[x1]
	adr	x0,volume
	mov	x1,sp
	bl	vol_maj
	adr	x1,dos_date_forcee
	str	xzr,[x1]
	mov	x0,0
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.noent:	mov	x0,E_NOENT
.out:	add	sp,sp,DE_SIZE
	ldr	x19,[sp],16
	epilogue

; 6 rename(donnees : ancien, 0, nouveau, 0) : une entree de repertoire neuve,
; memes clusters et meme date, puis l'ancienne effacee ; un fichier a l'arrivee
; est remplace ; le noeud d'un fichier ouvert suit (docs/FICHIERS.md)
op_rename:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	bl	ro
	cbnz	x0,.perm
	adr	x19,volume
	add	x21,x20,MSG_DATA		; ancien
	mov	x22,x21
.f:	ldrb	w0,[x22],1			; nouveau
	cbnz	w0,.f
	ldrb	w0,[x21]
	cbz	w0,.inval
	ldrb	w0,[x22]
	cbz	w0,.inval
	mov	x0,x19
	mov	x1,x21
	adr	x2,ren_src
	bl	vol_chercher
	cbz	x0,.noent
	adr	x1,ren_src
	ldr	x0,[x1,DE_ATTR]
	tst	x0,ATTR_DIR
	b.eq	.parent
	mov	x0,0				; un repertoire : pas dans lui-meme
.s:	ldrb	w1,[x21,x0]
	cbz	w1,.sf
	ldrb	w2,[x22,x0]
	cmp	w1,w2
	b.ne	.parent
	add	x0,x0,1
	b	.s
.sf:	ldrb	w2,[x22,x0]
	cmp	w2,'/'
	b.eq	.inval
.parent:
	mov	x23,x22				; le dernier composant, son repertoire
	mov	x0,x22
.l:	ldrb	w1,[x0],1
	cbz	w1,.sp
	cmp	w1,'/'
	b.ne	.l
	mov	x23,x0
	b	.l
.sp:	ldr	x24,[x19,V_ROOT]
	cmp	x23,x22
	b.eq	.cible
	strb	wzr,[x23,-1]
	mov	x0,x19
	mov	x1,x22
	adr	x2,ren_dst
	bl	vol_chercher
	mov	w1,'/'
	strb	w1,[x23,-1]
	cbz	x0,.noent
	adr	x1,ren_dst
	ldr	x0,[x1,DE_ATTR]
	tst	x0,ATTR_DIR
	b.eq	.noent
	ldr	x24,[x1,DE_CLUSTER]
.cible:	mov	x0,x19
	mov	x1,x22
	adr	x2,ren_dst
	bl	vol_chercher
	cbz	x0,.creer
	adr	x0,ren_src
	adr	x1,ren_dst
	ldr	w2,[x0,DE_DIR]
	ldr	w3,[x1,DE_DIR]
	cmp	w2,w3
	b.ne	.autre
	ldr	x2,[x0,DE_INDEX]
	ldr	x3,[x1,DE_INDEX]
	cmp	x2,x3
	b.ne	.autre
	mov	x0,x19				; la meme entree (la casse change) :
	adr	x1,ren_src			; effacee d'abord
	bl	vol_effacer_entree
	mov	x21,0
	b	.creer
.autre:	ldr	x2,[x1,DE_ATTR]
	tst	x2,ATTR_DIR
	b.ne	.exist
	ldr	x2,[x0,DE_ATTR]
	tst	x2,ATTR_DIR
	b.ne	.notdir
	mov	x0,x1				; le fichier remplace (comme unlink)
	bl	no_trouver
	cbz	x0,.eff
	mov	x1,1
	str	x1,[x0,NO_EFFACE]
	mov	x0,x19
	adr	x1,ren_dst
	bl	vol_effacer_entree
	b	.creer
.eff:	mov	x0,x19
	adr	x1,ren_dst
	bl	vol_effacer
.creer:	mov	x0,x23
	bl	nom_trop_long
	cbnz	x0,.toolong
	mov	x0,x19
	mov	x1,x24
	mov	x2,x23
	mov	x3,ATTR_ARCHIVE
	adr	x4,ren_new
	bl	vol_creer
	cbz	x0,.nospc
	adr	x0,ren_src
	adr	x1,ren_new
	ldr	x2,[x0,DE_CLUSTER]
	str	x2,[x1,DE_CLUSTER]
	ldr	x2,[x0,DE_FSIZE]
	str	x2,[x1,DE_FSIZE]
	ldr	x2,[x0,DE_ATTR]
	str	x2,[x1,DE_ATTR]
	ldr	x2,[x0,DE_MTIME]
	adr	x3,dos_date_forcee
	str	x2,[x3]
	mov	x0,x19
	bl	vol_maj
	adr	x3,dos_date_forcee
	str	xzr,[x3]
	adr	x0,ren_src			; ouvert : son noeud suit
	bl	no_trouver
	cbz	x0,.old
	adr	x1,ren_new
	ldr	x2,[x1,DE_DIR]
	str	x2,[x0,NO_DIR]
	ldr	x2,[x1,DE_INDEX]
	str	x2,[x0,NO_INDEX]
	ldr	x2,[x1,DE_FIRST]
	str	x2,[x0,NO_FIRST]
.old:	cbz	x21,.dd
	mov	x0,x19
	adr	x1,ren_src
	bl	vol_effacer_entree
.dd:	ldr	x0,[x19,V_TYPE]			; FAT32 : � .. � d'un repertoire deplace
	cmp	x0,2
	b.eq	.ok
	adr	x1,ren_src
	ldr	x0,[x1,DE_ATTR]
	tst	x0,ATTR_DIR
	b.eq	.ok
	ldr	x0,[x1,DE_DIR]
	cmp	x0,x24
	b.eq	.ok
	ldr	x1,[x1,DE_CLUSTER]
	mov	x0,x19
	mov	x2,1
	bl	dir_entry_at
	cbz	x0,.ok
	ldr	x2,[x19,V_ROOT]
	cmp	x24,x2
	csel	x2,xzr,x24,eq
	lsr	x3,x2,16
	strh	w3,[x0,20]
	strh	w2,[x0,26]
	mov	x0,x1
	bl	bc_write
.ok:	mov	x0,0
	b	.out
.toolong:
	mov	x0,E_NAMETOOLONG
	b	.out
.nospc:	mov	x0,E_NOSPC
	b	.out
.notdir:
	mov	x0,E_NOTDIR
	b	.out
.exist:	mov	x0,E_EXIST
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = DE lue sur le disque -> x0 = taille du fichier (celle de son noeud
; s'il est ouvert : ecritures pas encore reportees dans le repertoire)
no_taille:
	ldr	x1,[x0,DE_ATTR]
	tst	x1,ATTR_DIR
	b.ne	.disque
	str	x0,[sp,-16]!
	str	x30,[sp,8]
	bl	no_trouver
	mov	x1,x0
	ldr	x30,[sp,8]
	ldr	x0,[sp],16
	cbz	x1,.disque
	ldr	x0,[x1,NO_SIZE]
	ret
.disque:
	ldr	x0,[x0,DE_FSIZE]
	ret

; 3 mkdir(chemin)
op_mkdir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,DE_SIZE
	mov	x20,x0
	bl	ro
	cbnz	x0,.perm
	adr	x19,volume
	add	x20,x20,MSG_DATA		; chemin
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	bl	vol_chercher
	cbnz	x0,.exist
	mov	x0,x20				; parent
	mov	x21,x20
.last:	ldrb	w1,[x0],1
	cbz	w1,.split
	cmp	w1,'/'
	b.ne	.last
	mov	x21,x0
	b	.last
.split:	ldr	x1,[x19,V_ROOT]
	cmp	x21,x20
	b.eq	.create
	sub	x22,x21,1
	strb	wzr,[x22]
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	bl	vol_chercher
	mov	w1,'/'
	strb	w1,[x22]
	cbz	x0,.noent
	ldr	x1,[sp,DE_ATTR]
	tst	x1,ATTR_DIR
	b.eq	.noent
	ldr	x1,[sp,DE_CLUSTER]
.create:
	str	x1,[sp,-16]!
	mov	x0,x21				; nom de plus de 255 caracteres ?
	bl	nom_trop_long
	ldr	x1,[sp],16
	cbnz	x0,.toolong
	mov	x0,x19
	mov	x2,x21
	mov	x3,ATTR_DIR
	mov	x4,sp
	bl	vol_creer
	cmp	x0,1
	mov	x0,E_NOSPC
	csel	x0,xzr,x0,eq
	b	.out
.toolong:
	mov	x0,E_NAMETOOLONG
	b	.out
.exist:	mov	x0,E_EXIST
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.noent:	mov	x0,E_NOENT
.out:	add	sp,sp,DE_SIZE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 4 unlink(chemin) : fichier, ou repertoire vide
op_unlink:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,DE_SIZE*2+IT_SIZE
	mov	x20,x0
	bl	ro
	cbnz	x0,.perm
	add	x1,x20,MSG_DATA
	ldrb	w2,[x1]
	cbz	w2,.inval			; racine du volume
	adr	x19,volume
	mov	x0,x19
	mov	x2,sp
	bl	vol_chercher
	cbz	x0,.noent
	ldr	x0,[sp,DE_ATTR]
	tst	x0,ATTR_DIR
	b.ne	.dir
	mov	x0,sp				; fichier encore ouvert ?
	bl	no_trouver
	cbz	x0,.erase
	mov	x1,1				; son noeud perd sa cle ; ses clusters
	str	x1,[x0,NO_EFFACE]		; a sa derniere fermeture
	mov	x0,x19
	mov	x1,sp
	bl	vol_effacer_entree
	mov	x0,0
	b	.out
.dir:	add	x0,sp,DE_SIZE*2			; repertoire : doit etre vide
	mov	x1,x19
	ldr	x2,[sp,DE_CLUSTER]
	bl	dir_open
	add	x0,sp,DE_SIZE*2
	add	x1,sp,DE_SIZE
	bl	vol_lire_rep
	cbnz	x0,.notempty
.erase:	mov	x0,x19
	mov	x1,sp
	bl	vol_effacer
	mov	x0,0
	b	.out
.notempty:
	mov	x0,E_NOTEMPTY
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.noent:	mov	x0,E_NOENT
.out:	add	sp,sp,DE_SIZE*2+IT_SIZE
	ldp	x19,x20,[sp],16
	epilogue

; 10 lire(a numero, b position, c octets) -> octets (donnees)
op_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	fo_acces
	tbnz	x0,63,.out
	mov	x20,x0
	ldr	x1,[x20,FO_TYPE]
	cmp	x1,1
	b.ne	.inval
	bl	fo_charger
	ldr	x1,[x19,MSG_B]
	str	x1,[x20,FO_POS]
	ldr	x2,[x19,MSG_C]
	cmp	x2,FS_DONNEES
	mov	x3,FS_DONNEES
	csel	x2,x3,x2,hi
	mov	x0,x20
	add	x1,x19,MSG_DATA
	mov	x3,0
	bl	vol_rw
	str	w0,[x19,MSG_LEN]
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	fo_ranger
	ldr	x0,[sp],16
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; 11 ecrire(a numero, b position, c octets, donnees) -> octets, b taille
op_ecrire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	ro
	cbnz	x0,.perm
	mov	x0,x19
	bl	fo_acces
	tbnz	x0,63,.out
	mov	x20,x0
	ldr	x1,[x20,FO_TYPE]
	cmp	x1,1
	b.ne	.inval
	ldr	x1,[x20,FO_FLAGS]
	tst	x1,O_WRITE
	b.eq	.perm
	ldr	x2,[x19,MSG_C]
	cmp	x2,FS_DONNEES
	b.hi	.inval
	mov	x0,x20				; etat du noeud, position (O_APPEND)
	bl	fo_position
	mov	x0,x20
	add	x1,x19,MSG_DATA
	ldr	x2,[x19,MSG_C]
	mov	x3,1
	bl	vol_rw
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	fo_ranger
	ldr	x0,[sp],16
	ldr	x1,[x20,FO_SIZE]
	str	x1,[x19,MSG_B]
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = objet fichier, x19 = message d'ecriture : etat du noeud charge,
; FO_POS = la position demandee (b), ou la fin du fichier avec O_APPEND
fo_position:
	str	x30,[sp,-16]!
	str	x0,[sp,8]
	bl	fo_charger
	ldr	x0,[sp,8]
	ldr	x1,[x19,MSG_B]
	ldr	x2,[x0,FO_FLAGS]
	tst	x2,O_APPEND
	b.eq	.pos
	ldr	x1,[x0,FO_SIZE]
.pos:	str	x1,[x0,FO_POS]
	ldr	x30,[sp],16
	ret

; 12 fermer(a numero) -> 0 (avis du noyau, ou appel du proprietaire)
op_fermer:
	str	x30,[sp,-16]!
	bl	fo_acces
	tbnz	x0,63,.out
	bl	fo_fermer
	mov	x0,0
.out:	ldr	x30,[sp],16
	ret

; x0 = objet ouvert : libere ; son noeud : taille et cluster reecrits dans
; le repertoire s'ils ont change ; efface pendant l'ouverture : ses
; clusters liberes a la derniere fermeture
fo_fermer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,FO_NOEUD]
	cbz	x20,.libre			; (repertoire)
	ldr	x0,[x20,NO_EFFACE]
	cbnz	x0,.efface
	ldr	x0,[x20,NO_DIRTY]
	cbz	x0,.ref
	ldr	x0,[x19,FO_VOL]
	mov	x1,x20
	bl	vol_maj
	str	xzr,[x20,NO_DIRTY]
	b	.ref
.efface:
	ldr	x0,[x20,NO_REFS]
	cmp	x0,1
	b.ne	.ref
	ldr	x0,[x19,FO_VOL]
	mov	x1,x20
	bl	vol_liberer
.ref:	ldr	x0,[x20,NO_REFS]
	sub	x0,x0,1
	str	x0,[x20,NO_REFS]
.libre:	str	xzr,[x19,FO_TYPE]
	str	xzr,[x19,FO_NOEUD]
	ldp	x19,x20,[sp],16
	epilogue

; 13 lire_rep(a numero, b rang) -> 1 (donnees : nom[256], taille, attributs)
op_lire_rep:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,DE_SIZE
	mov	x19,x0
	bl	fo_acces
	tbnz	x0,63,.out
	mov	x20,x0
	ldr	x1,[x20,FO_TYPE]
	cmp	x1,2
	b.ne	.inval
	ldr	x21,[x19,MSG_B]			; rang voulu
	ldr	x0,[x20,FO_RANG]
	cmp	x0,x21
	b.ls	.avance
	add	x0,x20,FO_IT			; plus haut : depuis le debut
	ldr	x1,[x20,FO_VOL]
	ldr	x2,[x20,FO_START]
	bl	dir_open
	str	xzr,[x20,FO_RANG]
.avance:
	add	x0,x20,FO_IT
	mov	x1,sp
	bl	vol_lire_rep
	cbz	x0,.out				; fin : 0
	ldr	x0,[x20,FO_RANG]
	add	x1,x0,1
	str	x1,[x20,FO_RANG]
	cmp	x0,x21
	b.lo	.avance
	add	x0,x19,MSG_DATA			; nom[256], taille, attributs
	add	x1,sp,DE_NAME
	bl	nom_256
	mov	x0,sp
	bl	no_taille
	ldr	x1,[sp,DE_ATTR]
	add	x2,x19,MSG_DATA+256
	stp	x0,x1,[x2]
	mov	x0,272
	str	w0,[x19,MSG_LEN]
	mov	x0,1
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,DE_SIZE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination (256 o), x1 = nom UTF-8 : copie de 255 octets au plus
; (un nom plus long, possible en UTF-8 au-dela de 85 caracteres, est coupe
; avant un caractere entier)
nom_256:
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	cbz	w3,.fin
	cmp	x2,255
	b.hs	.coupe
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.c
.coupe:	ldrb	w3,[x1,x2]			; pas au milieu d'un caractere
	and	w3,w3,0xC0
	cmp	w3,0x80
	b.ne	.fin
	sub	x2,x2,1
	b	.coupe
.fin:	strb	wzr,[x0,x2]
	ret

; 15 lire_t(a numero, b position, c octets) -> octets, au debut du tampon
op_lire_t:
	mov	x4,0
	b	rw_t
; 16 ecrire_t(a numero, b position, c octets du tampon) -> octets, b taille
op_ecrire_t:
	mov	x4,1
rw_t:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x22,x4
	cbz	x22,.acc
	bl	ro
	cbnz	x0,.perm
.acc:	mov	x0,x19
	bl	fo_acces
	tbnz	x0,63,.out
	mov	x20,x0
	ldr	x1,[x20,FO_TYPE]
	cmp	x1,1
	b.ne	.inval
	cbz	x22,.tp
	ldr	x1,[x20,FO_FLAGS]
	tst	x1,O_WRITE
	b.eq	.perm
.tp:	mov	x0,x19				; le tampon de l'appelant
	bl	tampon_de
	cbz	x0,.inval
	mov	x21,x0
	ldr	x2,[x19,MSG_C]
	movl	x3,FS_TAMPON_TAILLE
	cmp	x2,x3
	b.hi	.inval
	mov	x0,x20				; etat du noeud, position (O_APPEND)
	bl	fo_position
	cbnz	x22,.rw
	ldr	x1,[x19,MSG_B]			; lecture : la position demandee
	str	x1,[x20,FO_POS]
.rw:	mov	x0,x20
	mov	x1,x21
	ldr	x2,[x19,MSG_C]
	mov	x3,x22
	bl	vol_rw
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	fo_ranger
	ldr	x0,[sp],16
	cbz	x22,.out
	ldr	x1,[x20,FO_SIZE]
	str	x1,[x19,MSG_B]
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 17 lire_d(a numero, b position, c octets, d adresse) -> octets : la zone de
; l'appelant, mappee, sert de destination (SVC_MAP_CALLER ; le noyau la rend
; avec la reponse) ; les secteurs entiers y vont sans copie (blk_direct)
op_lire_d:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	fo_acces
	tbnz	x0,63,.out
	mov	x20,x0
	ldr	x1,[x20,FO_TYPE]
	cmp	x1,1
	b.ne	.inval
	ldr	x2,[x19,MSG_C]
	movl	x3,FS_DIRECT_MAX
	cmp	x2,x3
	b.hi	.inval
	adr	x0,req_cour
	ldr	x0,[x0]
	ldr	x1,[x19,MSG_D]
	mov	x3,MC_WRITE
	sys	SYS_SVC_MAP_CALLER
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x0,x20
	bl	fo_position
	ldr	x1,[x19,MSG_B]
	str	x1,[x20,FO_POS]
	adr	x0,direct
	mov	x1,1
	str	x1,[x0]
	mov	x0,x20
	mov	x1,x21
	ldr	x2,[x19,MSG_C]
	mov	x3,0
	bl	vol_rw
	adr	x1,direct
	str	xzr,[x1]
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	fo_ranger
	ldr	x0,[sp],16
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

include '../inc/fsowner.inc'
include '../../inc/dostime.inc'

		align 8
ops:	dw FS_OUVRIR, op_ouvrir - ops - 0
	dw FS_STAT, op_stat - ops - 8
	dw FS_MKDIR, op_mkdir - ops - 16
	dw FS_UNLINK, op_unlink - ops - 24
	dw FS_LIRE, op_lire - ops - 32
	dw FS_ECRIRE, op_ecrire - ops - 40
	dw FS_LIRE_REP, op_lire_rep - ops - 48
	dw FS_TAMPON, op_tampon - ops - 56
	dw FS_LIRE_T, op_lire_t - ops - 64
	dw FS_ECRIRE_T, op_ecrire_t - ops - 72
	dw FS_UTIME, op_utime - ops - 80
	dw FS_RENAME, op_rename - ops - 88
	dw FS_LIRE_D, op_lire_d - ops - 96
	dw FS_CACHE, op_cache - ops - 104
	dw FS_INFO, op_info - ops - 112
	dw FS_SYNC, op_sync - ops - 120
	dw 0, 0
s_fs	db 'fs.',0
	align 8

; ---------------------------------------------------------------------------
;  Le systeme de fichiers FAT32 du noyau (meme source)
; ---------------------------------------------------------------------------
include 'fat32.asm'

; ---------------------------------------------------------------------------
;  FAT32 ou exFAT : reconnu au montage ; chaque operation appelle les
;  fonctions du format du volume (src/kernel/exfat.asm inclus tel quel)
; ---------------------------------------------------------------------------
include 'exfat.asm'

; x0 = volume, x1 = premier secteur -> x0 = 1 si FAT32 ou exFAT reconnu
vol_monter:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	fat_mount
	cbnz	x0,.out
	mov	x0,x19
	mov	x1,x20
	bl	exfat_mount
.out:	ldp	x19,x20,[sp],16
	epilogue

; (comme fat_lookup) x0 = volume
vol_chercher:
	ldr	x3,[x0,V_TYPE]
	cmp	x3,2
	b.eq	exfat_lookup
	b	fat_lookup
; (comme dir_read) x0 = iterateur
vol_lire_rep:
	ldr	x2,[x0,IT_VOL]
	ldr	x2,[x2,V_TYPE]
	cmp	x2,2
	b.eq	exfat_dir_read
	b	dir_read
; (comme fo_rw) x0 = objet fichier ; en ecriture, FAT et bitmap ecrits une
; seule fois, une fois les donnees sur la carte
vol_rw:
	ldr	x4,[x0,FO_SIZE]			; un tres gros fichier : hors du cache
	movl	x5,CC_FILE_MAX
	cmp	x4,x5
	cset	x4,hi
	adr	x5,cc_skip
	str	x4,[x5]
	ldr	x4,[x0,FO_VOL]
	ldr	x4,[x4,V_TYPE]
	cbz	x3,.rw
	prologue
	str	x19,[sp,-16]!
	adr	x5,bc_defer
	mov	x6,1
	str	x6,[x5]
	cmp	x4,2
	b.eq	.ex
	bl	fo_rw
	b	.sync
.ex:	bl	exfat_rw
.sync:	mov	x19,x0
	adr	x5,bc_defer
	str	xzr,[x5]
	bl	bc_flush
	cmp	x0,0
	mov	x0,E_IO
	csel	x0,x19,x0,ne
	ldr	x19,[sp],16
	epilogue
.rw:	cmp	x4,2
	b.eq	exfat_rw
	b	fo_rw
; (comme fat_create) x0 = volume
vol_creer:
	ldr	x5,[x0,V_TYPE]
	cmp	x5,2
	b.eq	exfat_create
	b	fat_create
; (comme fat_update_entry) x0 = volume, x1 = DE
vol_maj:
	ldr	x3,[x0,V_TYPE]
	cmp	x3,2
	b.eq	exfat_update_entry
	b	fat_update_entry
; x0 = volume, x1 = DE : supprime l'entree et libere son allocation
vol_effacer:
	ldr	x3,[x0,V_TYPE]
	cmp	x3,2
	b.eq	exfat_erase
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	fat_erase_entry
	mov	x0,x19
	ldr	x1,[x20,DE_CLUSTER]
	bl	fat_free_chain
	ldp	x19,x20,[sp],16
	epilogue

; x0 = volume, x1 = DE : supprime l'entree seulement (fichier encore ouvert)
vol_effacer_entree:
	ldr	x3,[x0,V_TYPE]
	cmp	x3,2
	b.eq	exfat_erase_entries
	b	fat_erase_entry
; x0 = volume, x1 = noeud efface : libere ses clusters (derniere fermeture)
vol_liberer:
	ldr	x3,[x0,V_TYPE]
	ldr	x2,[x1,NO_SIZE]
	ldr	x1,[x1,NO_START]
	cmp	x3,2
	b.eq	ex_free_alloc
	b	fat_free_chain

; x0 = pages -> x0 = memoire (table de majuscules d'exFAT ; jamais rendue)
pmm_alloc_n:
	lsl	x0,x0,12
	b	malloc

segment readable writeable
canal		dd 0
canal_blk	dd 0
req_cour	dd 0				; numero de la requete en cours
direct		dd 0				; 1 : la destination est chez l'appelant (op_lire_d)
cc_base		dd 0				; la region du cache (0 : pas de cache)
cc_max		dd 0				; blocs au plus (taille de la region)
cc_cap		dd 0				; blocs au plus, d'apres la memoire
cc_limit	dd 0				; blocs permis pour l'instant (memoire rare : moins)
cc_used		dd 0				; blocs peuples
cc_hand		dd 0				; aiguille de l'horloge
cc_disk		dd 0				; secteurs du peripherique
cc_off		dd 0				; 1 : coupe (fscache off)
cc_skip		dd 0				; 1 : cette requete n'y passe pas
cc_hits		dd 0
cc_misses	dd 0
cc_evicts	dd 0
lecture_seule	dd 0
cache		dd 0				; adresse du tampon partage
kit_data
		align 8
nom_svc		rb 32
nom_blk		rb 32
msg		rb MSG_TAILLE
msg_blk		rb MSG_TAILLE
dos_date_forcee	rb 8			; (dostime.inc : un utime en cours)
		align 8
ren_src		rb DE_SIZE		; op_rename
ren_dst		rb DE_SIZE
ren_new		rb DE_SIZE
volume		rb VOL_SIZE
bc_tags		rb BC_N * 8
bc_defer	rd 1			; 1 : bc_write attend bc_flush (vol_rw)
bc_any		rd 1			; 1 : au moins un secteur differe
bc_dirty	rb BC_N			; 1 : ce secteur du cache reste a ecrire
	align 8
ouverts		rb OUV_MAX * FO_SIZE_
noeuds		rb NO_MAX * NO_SIZE_
cc_tags		rb CC_BLOCKS_MAX * 8		; bloc + 1 (0 : libre)
cc_next		rb CC_BLOCKS_MAX * 4
cc_head		rb CC_HASH * 4
cc_refs		rb CC_BLOCKS_MAX
		align 8
fsowner_data
		align 16
zero_sector	rb 512
