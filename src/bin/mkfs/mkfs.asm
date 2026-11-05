; mkfs - formater un volume : tout ce qu'il contient est efface, un systeme de fichiers FAT32
; neuf y est ecrit (docs/OUTILS.md ; d'apres mkfs d'Onyx) :
;   mkfs [-t auto|fat32] [-L etiquette] [-c octets] [-y] [-f] [--card] VOLUME:
;   -t      le systeme de fichiers : FAT32 seulement (auto aussi ; exFAT et FAT16 : pas encore)
;   -L      l'etiquette du volume (11 caracteres au plus, en majuscules)
;   -c      la taille d'un cluster : 512 a 64K (« 4096 », « 4k ») ; sinon d'apres la taille
;           du volume, comme Windows (512 o jusqu'a 64 Mo ... 32 Ko au-dela de 32 Go)
;   -y      sans question (sinon : taper le nom du volume pour confirmer, sur l'entree standard)
;   -f      meme avec des fichiers ouverts dessus (leurs appels suivants echouent)
;   --card  permettre une partition de la carte SD (SD1:...). SD0:, le volume du systeme, jamais.
; Le volume est formate sur sa source (la partition, « blk.usb0p1 » ; un volume non monte : sa
; ligne de SD0:/etc/mounts.txt, ou blk.usbNp1) : demonte (fatfs s'arrete), ecrit secteur par
; secteur par le protocole des peripheriques blocs (inc/blk.inc), puis remonte. Au moins 65 525
; clusters (FAT32 : 33 Mo au moins avec des clusters de 512 o).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../inc/blk.inc'

TAMPON		= 65536			; tampon partage avec le service bloc
PAQUET		= TAMPON / 512		; secteurs par ecriture
RSVD		= 32			; secteurs reserves (FAT32)
NFATS		= 2
CLUS_MIN	= 65525			; en dessous : FAT16 pour les autres systemes
CLUS_MAX	= 0x0FFFFFF5

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,0				; options : bit 0 -y, 1 -f, 2 --card
	mov	x22,0				; volume
	mov	x23,1
.arg:	cmp	x23,x19
	b.hs	.go
	ldr	x24,[x20,x23,lsl 3]
	add	x23,x23,1
	ldrb	w0,[x24]
	cmp	w0,'-'
	b.ne	.vol
	mov	x0,x24
	adr	x1,s_o_y
	bl	str_eq
	cbz	x0,.o1
	orr	x21,x21,1
	b	.arg
.o1:	mov	x0,x24
	adr	x1,s_o_f
	bl	str_eq
	cbz	x0,.o2
	orr	x21,x21,2
	b	.arg
.o2:	mov	x0,x24
	adr	x1,s_o_card
	bl	str_eq
	cbz	x0,.o3
	orr	x21,x21,4
	b	.arg
.o3:	cmp	x23,x19				; les options a valeur
	b.hs	.usage
	ldr	x25,[x20,x23,lsl 3]
	add	x23,x23,1
	mov	x0,x24
	adr	x1,s_o_t
	bl	str_eq
	cbz	x0,.o4
	mov	x0,x25
	bl	minuscules
	mov	x0,x25
	adr	x1,s_auto
	bl	str_eq
	cbnz	x0,.arg
	mov	x0,x25
	adr	x1,s_fat32
	bl	str_eq
	cbnz	x0,.arg
	mov	x0,x25
	adr	x1,s_vfat
	bl	str_eq
	cbnz	x0,.arg
	adr	x0,m_type
	bl	t_derr
	mov	x0,2
	bl	t_fin
.o4:	mov	x0,x24
	adr	x1,s_o_l
	bl	str_eq
	cbz	x0,.o5
	mov	x0,x25				; 11 caracteres au plus
	bl	str_len
	cmp	x0,11
	b.hi	.longue
	adr	x0,etiquette
	mov	x1,x25
	bl	str_cat
	b	.arg
.longue:
	adr	x0,m_longue
	bl	t_derr
	mov	x0,2
	bl	t_fin
.o5:	mov	x0,x24
	adr	x1,s_o_c
	bl	str_eq
	cbz	x0,.usage
	mov	x0,x25				; « 4096 », « 4k », « 32K »
	bl	parse_dec
	mov	x1,x0
	mov	x0,x25
	bl	str_len
	add	x0,x25,x0
	ldurb	w2,[x0,-1]
	orr	w2,w2,0x20
	cmp	w2,'k'
	b.ne	.c1
	lsl	x1,x1,10
.c1:	adr	x0,opt_cluster
	str	x1,[x0]
	b	.arg
.vol:	cbnz	x22,.usage
	mov	x22,x24
	b	.arg

.go:	cbz	x22,.usage
	mov	x0,x22
	bl	vu_nom
	cbz	x0,.usage
	adr	x0,vu_vol			; SD0: (ou SD:) : jamais
	adr	x1,s_sd0
	bl	str_eq
	cbnz	x0,.systeme
	adr	x0,vu_vol
	adr	x1,s_sd
	bl	str_eq
	cbnz	x0,.systeme
	adr	x0,opt_cluster			; taille de cluster demandee : valide ?
	ldr	x0,[x0]
	cbz	x0,.vsrc
	sub	x1,x0,1
	tst	x0,x1
	b.ne	.mauvais_c
	cmp	x0,512
	b.lo	.mauvais_c
	cmp	x0,65536
	b.hi	.mauvais_c
.vsrc:	adr	x0,vu_vol			; sa source : monte, sinon devinee
	bl	vu_trouver
	cbz	x0,.non_monte
	mov	x24,x0
	adr	x0,vu_src
	add	x1,x24,VU_SRC
	bl	str_cat
	adr	x0,vu_srv
	add	x1,x24,VU_FS
	bl	str_cat
	adr	x0,monte
	mov	x1,1
	str	x1,[x0]
	b	.src
.non_monte:
	adr	x0,vu_vol
	bl	vu_source
	cbz	x0,.inconnu
.src:	adr	x0,vu_src			; un peripherique bloc
	adr	x1,s_blk
	bl	prefixe
	cbz	x0,.pas_bloc
	adr	x0,vu_src			; la carte : --card
	adr	x1,s_blk_sd
	bl	prefixe
	cbz	x0,.ouvre
	tbz	x21,2,.carte
	adr	x0,vu_src			; (blk.sd0p1 : le systeme, jamais)
	adr	x1,s_blk_sd0p1
	bl	str_eq
	cbnz	x0,.systeme
.ouvre:	adr	x0,vu_src			; le service bloc
	mov	x1,2000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.sans_bloc
	adr	x1,canal
	str	x0,[x1]
	mov	x0,BLK_INFO
	mov	x1,0
	mov	x2,0
	mov	x3,0
	bl	blk
	tbnz	x0,63,.sans_bloc
	adr	x1,msg
	ldr	x0,[x1,MSG_A]
	cbnz	x0,.protege
	ldr	x0,[x1,MSG_B]
	movl	x2,0xFFFFFFFF			; (FAT32 : 2 To au plus)
	cmp	x0,x2
	csel	x0,x0,x2,ls
	adr	x2,secteurs
	str	x0,[x2]
	ldr	x0,[x1,MSG_D]
	adr	x2,pid_blk
	str	x0,[x2]
	bl	disposer			; -> x0 = 0, < 0 : trop petit
	tbnz	x0,63,.petit
	tbnz	x21,0,.confirme			; -y
	adr	x0,m_efface1			; « Tout ce qui est sur USB0: (blk.usb0p1, 1,9 Go) sera efface. »
	bl	t_dire
	adr	x0,vu_aff
	bl	t_puts
	adr	x0,m_paren
	bl	t_puts
	adr	x0,vu_src
	bl	t_puts
	adr	x0,m_virgule
	bl	t_puts
	adr	x0,secteurs
	ldr	x0,[x0]
	lsl	x0,x0,9
	bl	vu_taille
	adr	x0,m_efface2
	bl	t_dire
	adr	x0,m_confirme1
	bl	t_dire
	adr	x0,vu_aff
	bl	t_puts
	adr	x0,m_confirme2
	bl	t_dire
	bl	t_flush
	mov	x0,0				; la reponse, sur l'entree standard
	bl	t_lire_debut
	bl	t_lire_ligne
	cbz	x0,.non
	adr	x1,vu_aff			; meme nom ? (vu_nom le recrit : copie)
	ldp	x2,x3,[x1]
	adr	x1,aff_garde
	stp	x2,x3,[x1]
	bl	vu_nom
	adr	x0,vu_aff
	adr	x1,aff_garde
	bl	str_eq
	mov	x24,x0
	adr	x0,aff_garde			; (vu_vol et vu_aff rendus)
	bl	vu_nom
	cbz	x24,.non
.confirme:
	adr	x0,monte			; demonte d'abord, ses fichiers ecrits
	ldr	x0,[x0]
	cbz	x0,.formate
	adr	x0,vu_vol
	bl	vu_sync
	cmp	x0,0
	b.le	.demonte
	tbz	x21,1,.occupe
.demonte:
	adr	x0,vu_vol
	bl	vu_demonter
	tbnz	x0,63,.echec_dem
.formate:
	adr	x0,m_formatage
	bl	t_dire
	adr	x0,vu_aff
	bl	t_puts
	adr	x0,m_points
	bl	t_puts
	bl	t_flush
	bl	formater
	mov	x24,x0
	adr	x0,vu_aff			; remonte, meme en cas d'echec
	adr	x1,vu_src
	adr	x2,vu_srv
	bl	vu_monter
	mov	x25,x0
	tbnz	x24,63,.echec_fmt
	adr	x0,vu_aff
	bl	t_puts
	adr	x0,m_formate
	bl	t_dire
	tbnz	x25,63,.pas_remonte
	adr	x0,vu_vol
	bl	vu_info
	tbnz	x0,63,.fin
	adr	x24,vu_rep
	ldr	x0,[x24,MSG_D]
	bl	vu_type_nom
	bl	t_puts
	adr	x0,m_virgule
	bl	t_puts
	ldr	x0,[x24,MSG_A]
	bl	vu_taille
	adr	x0,m_cluster
	bl	t_dire
	ldr	x0,[x24,MSG_C]
	bl	t_putn
	adr	x0,m_octets
	bl	t_dire
	ldrb	w0,[x24,MSG_DATA]
	cbz	w0,.fin
	adr	x0,m_guill1
	bl	t_puts
	add	x0,x24,MSG_DATA
	strb	wzr,[x0,63]
	bl	t_puts
	adr	x0,m_guill2
	bl	t_puts
.fin:	bl	t_nl
	mov	x0,0
	bl	t_fin
.pas_remonte:
	bl	t_nl
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_pas_remonte
	bl	t_derr
	mov	x0,x25
	bl	vu_err
	adr	x0,m_nl
	bl	t_err
	mov	x0,1
	bl	t_fin
.echec_fmt:
	bl	t_nl
	mov	x0,x24
	b	.err
.echec_dem:
.err:	mov	x24,x0
	adr	x0,m_mkfs
	bl	t_derr
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_pas_formate
	bl	t_derr
	mov	x0,x24
	bl	vu_err
	adr	x0,m_nl
	bl	t_err
	mov	x0,1
	bl	t_fin
.occupe:
	mov	x0,E_BUSY_VU
	bl	.err_msg
	adr	x0,m_ajoutez_f
	bl	t_derr
	mov	x0,1
	bl	t_fin
.protege:
	adr	x0,m_protege
	b	.dire1
.petit:	adr	x0,m_petit
	b	.dire1
.mauvais_c:
	adr	x0,m_mauvais_c
	b	.dire1
.pas_bloc:
	adr	x0,m_pas_bloc
	b	.dire1
.sans_bloc:
	adr	x0,m_sans_bloc
	b	.dire1
.inconnu:
	adr	x0,m_inconnu
	b	.dire1
.carte:	adr	x0,m_carte
	b	.dire1
.systeme:
	adr	x0,m_systeme
.dire1:	mov	x24,x0
	adr	x0,m_mkfs
	bl	t_derr
	adr	x0,vu_aff
	bl	t_err
	mov	x0,x24
	bl	t_derr
	mov	x0,1
	bl	t_fin
.non:	adr	x0,m_non
	bl	t_derr
	mov	x0,1
	bl	t_fin
.usage:	adr	x0,m_usage
	bl	t_derr
	mov	x0,2
	bl	t_fin
; x0 = erreur : « mkfs : USB0: pas formate : <texte> »
.err_msg:
	prologue
	str	x0,[sp,-16]!
	adr	x0,m_mkfs
	bl	t_derr
	adr	x0,vu_aff
	bl	t_err
	adr	x0,m_pas_formate
	bl	t_derr
	ldr	x0,[sp],16
	bl	vu_err
	adr	x0,m_nl
	bl	t_err
	epilogue

; x0 = texte : en minuscules (ASCII), sur place
minuscules:
	ldrb	w1,[x0]
	cbz	w1,.r
	sub	w2,w1,'A'
	cmp	w2,25
	b.hi	.s
	add	w1,w1,32
	strb	w1,[x0]
.s:	add	x0,x0,1
	b	minuscules
.r:	ret

; x0 = texte, x1 = prefixe -> x0 = 1 si le texte commence par lui
prefixe:
	ldrb	w2,[x1],1
	cbz	w2,.oui
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	prefixe
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; --- le peripherique bloc ---------------------------------------------------------------------

; x0 = op, x1 = a, x2 = b, x3 = c : requete au service bloc -> x0 = resultat (reponse dans msg)
blk:
	prologue
	adr	x4,msg
	stp	x0,x1,[x4]
	stp	x2,x3,[x4,16]
	str	xzr,[x4,32]
	str	xzr,[x4,40]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x4
	sys	SYS_SVC_CALL
	epilogue

; x0 = premier secteur, x1 = nombre (<= PAQUET), x2 = decalage dans le tampon : ecrits -> 0, erreur
ecrire:
	prologue
	adr	x3,tampon
	ldr	x3,[x3]
	add	x3,x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,BLK_ECRIRE
	bl	blk
	epilogue

; x0 = premier secteur, x1 = nombre : mis a zero (le tampon est a zero) -> 0, erreur
zeros:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	cbz	x20,.ok
	mov	x1,PAQUET
	cmp	x20,x1
	csel	x1,x20,x1,lo
	sub	x20,x20,x1
	mov	x0,x19
	add	x19,x19,x1
	mov	x2,0
	bl	ecrire
	tbz	x0,63,.c
	b	.out
.ok:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; le tampon (TAMPON octets) a zero
tampon_zero:
	adr	x0,tampon
	ldr	x0,[x0]
	mov	x1,TAMPON
.z:	stp	xzr,xzr,[x0],16
	subs	x1,x1,16
	b.ne	.z
	ret

; --- la disposition FAT32 (fatgen103) ---------------------------------------------------------

; secteurs -> spc, fatsz, clusters ; x0 = 0, -1 si le volume est trop petit (ou trop grand)
disposer:
	prologue
	adr	x0,secteurs
	ldr	x9,[x0]
	adr	x0,opt_cluster
	ldr	x10,[x0]
	lsr	x10,x10,9			; secteurs par cluster demandes
	cbnz	x10,.calc
	mov	x10,1				; comme Windows, d'apres la taille
	movl	x11,131072			; 64 Mo
	cmp	x9,x11
	b.ls	.calc
	mov	x10,2
	lsl	x11,x11,1			; 128 Mo
	cmp	x9,x11
	b.ls	.calc
	mov	x10,4
	lsl	x11,x11,1			; 256 Mo
	cmp	x9,x11
	b.ls	.calc
	mov	x10,8
	movl	x11,16777216			; 8 Go
	cmp	x9,x11
	b.ls	.calc
	mov	x10,16
	lsl	x11,x11,1			; 16 Go
	cmp	x9,x11
	b.ls	.calc
	mov	x10,32
	lsl	x11,x11,1			; 32 Go
	cmp	x9,x11
	b.ls	.calc
	mov	x10,64
.calc:	adr	x0,spc
	str	x10,[x0]
	subs	x1,x9,RSVD			; TmpVal1 = secteurs - reserves
	b.ls	.petit
	lsl	x2,x10,7			; TmpVal2 = 128 * spc + 1 (deux FAT)
	add	x2,x2,1
	add	x1,x1,x2			; FATSz = arrondi superieur
	sub	x1,x1,1
	udiv	x1,x1,x2
	adr	x0,fatsz
	str	x1,[x0]
	mov	x3,RSVD				; debut des donnees
	add	x3,x3,x1,lsl 1
	adr	x0,donnees
	str	x3,[x0]
	subs	x4,x9,x3
	b.ls	.petit
	udiv	x4,x4,x10
	adr	x0,clusters
	str	x4,[x0]
	movl	x5,CLUS_MIN
	cmp	x4,x5
	b.lo	.petit
	movl	x5,CLUS_MAX
	cmp	x4,x5
	b.hi	.petit
	mov	x0,0
	b	.out
.petit:	mov	x0,-1
.out:	epilogue

; x0 = adresse, x1 = valeur : 16 / 32 bits petit-boutiens, octet par octet
st16:
	strb	w1,[x0]
	lsr	w1,w1,8
	strb	w1,[x0,1]
	ret
st32:
	strb	w1,[x0]
	lsr	w2,w1,8
	strb	w2,[x0,1]
	lsr	w2,w1,16
	strb	w2,[x0,2]
	lsr	w2,w1,24
	strb	w2,[x0,3]
	ret

; le formatage -> x0 = 0, erreur
formater:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	movl	x0,TAMPON			; le tampon partage avec le service bloc
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x19,x0
	movl	x1,BUF_SLOT
	movl	x2,BUF_VA_BASE
	madd	x1,x19,x1,x2
	adr	x2,tampon
	str	x1,[x2]
	mov	x0,x19
	adr	x1,pid_blk
	ldr	x1,[x1]
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.out
	mov	x0,BLK_ATTACHER
	mov	x1,x19
	movl	x2,TAMPON
	mov	x3,0
	bl	blk
	tbnz	x0,63,.out
	bl	tampon_zero
	adr	x20,tampon
	ldr	x20,[x20]
	; 1) a zero : les secteurs reserves, les deux FAT, le cluster de la racine
	mov	x0,0
	adr	x1,donnees
	ldr	x1,[x1]
	adr	x2,spc
	ldr	x2,[x2]
	add	x1,x1,x2
	bl	zeros
	tbnz	x0,63,.out
	; 2) le debut de chaque FAT : entrees 0, 1, et 2 (la racine, fin de chaine)
	mov	x0,x20
	movl	x1,0x0FFFFFF8
	bl	st32
	add	x0,x20,4
	movl	x1,0x0FFFFFFF
	bl	st32
	add	x0,x20,8
	movl	x1,0x0FFFFFFF
	bl	st32
	mov	x0,RSVD
	mov	x1,1
	mov	x2,0
	bl	ecrire
	tbnz	x0,63,.out
	adr	x0,fatsz
	ldr	x0,[x0]
	add	x0,x0,RSVD
	mov	x1,1
	mov	x2,0
	bl	ecrire
	tbnz	x0,63,.out
	bl	tampon_zero
	; 3) la racine : l'etiquette, s'il y en a une
	adr	x0,etiquette
	ldrb	w0,[x0]
	cbz	w0,.fsinfo
	mov	x0,x20
	bl	nom_etiquette
	mov	w1,0x08				; attribut : etiquette du volume
	strb	w1,[x20,11]
	adr	x0,donnees
	ldr	x0,[x0]
	mov	x1,1
	mov	x2,0
	bl	ecrire
	tbnz	x0,63,.out
	bl	tampon_zero
.fsinfo:					; 4) FSInfo (1, copie en 7)
	mov	x0,x20
	movl	x1,0x41615252
	bl	st32
	add	x0,x20,484
	movl	x1,0x61417272
	bl	st32
	add	x0,x20,488			; clusters libres (la racine en prend un)
	adr	x1,clusters
	ldr	x1,[x1]
	sub	x1,x1,1
	bl	st32
	add	x0,x20,492			; prochain libre
	mov	x1,3
	bl	st32
	add	x0,x20,508
	movl	x1,0xAA550000
	bl	st32
	mov	x0,1
	mov	x1,1
	mov	x2,0
	bl	ecrire
	tbnz	x0,63,.out
	mov	x0,7
	mov	x1,1
	mov	x2,0
	bl	ecrire
	tbnz	x0,63,.out
	bl	tampon_zero
	bl	secteur_boot			; 5) le secteur de demarrage, en dernier
	mov	x0,6				; (copie en 6 d'abord)
	mov	x1,1
	mov	x2,0
	bl	ecrire
	tbnz	x0,63,.out
	mov	x0,0
	mov	x1,1
	mov	x2,0
	bl	ecrire
	tbnz	x0,63,.out
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination (11 octets) : l'etiquette en majuscules, completee d'espaces
nom_etiquette:
	adr	x1,etiquette
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	cbz	w3,.esp
	sub	w4,w3,'a'
	cmp	w4,25
	b.hi	.m
	sub	w3,w3,32
.m:	strb	w3,[x0,x2]
	add	x2,x2,1
	cmp	x2,11
	b.lo	.c
	ret
.esp:	mov	w3,' '
.e:	cmp	x2,11
	b.hs	.r
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.e
.r:	ret

; le secteur de demarrage FAT32 dans le tampon (x20) ; BPB d'apres fatgen103
secteur_boot:
	prologue
	str	x19,[sp,-16]!
	mov	w0,0xEB				; saut, OEM
	strb	w0,[x20]
	mov	w0,0x58
	strb	w0,[x20,1]
	mov	w0,0x90
	strb	w0,[x20,2]
	add	x0,x20,3
	adr	x1,s_oem
	mov	x2,8
	bl	mem_move
	add	x0,x20,11			; octets par secteur
	mov	x1,512
	bl	st16
	adr	x0,spc
	ldr	x0,[x0]
	strb	w0,[x20,13]
	add	x0,x20,14
	mov	x1,RSVD
	bl	st16
	mov	w0,NFATS
	strb	w0,[x20,16]
	mov	w0,0xF8				; support fixe (comme les cles formatees par Windows)
	strb	w0,[x20,21]
	add	x0,x20,24			; geometrie (sans usage)
	mov	x1,63
	bl	st16
	add	x0,x20,26
	mov	x1,255
	bl	st16
	add	x0,x20,32			; secteurs
	adr	x1,secteurs
	ldr	x1,[x1]
	bl	st32
	add	x0,x20,36			; secteurs par FAT
	adr	x1,fatsz
	ldr	x1,[x1]
	bl	st32
	add	x0,x20,44			; cluster de la racine
	mov	x1,2
	bl	st32
	add	x0,x20,48			; FSInfo, copie du secteur de demarrage
	mov	x1,1
	bl	st16
	add	x0,x20,50
	mov	x1,6
	bl	st16
	mov	w0,0x80				; lecteur
	strb	w0,[x20,64]
	mov	w0,0x29				; signature etendue
	strb	w0,[x20,66]
	sys	SYS_TIME_MS			; numero de serie
	mov	x19,x0
	mov	x0,0
	sys	SYS_CLOCK_GET
	eor	x1,x19,x0
	add	x0,x20,67
	bl	st32
	add	x0,x20,71			; etiquette
	adr	x1,etiquette
	ldrb	w1,[x1]
	cbz	w1,.sans
	bl	nom_etiquette
	b	.type
.sans:	adr	x1,s_no_name
	mov	x2,11
	bl	mem_move
.type:	add	x0,x20,82
	adr	x1,s_fat32_bs
	mov	x2,8
	bl	mem_move
	mov	w0,0xEB				; code : une boucle (« jmp $ »)
	strb	w0,[x20,90]
	mov	w0,0xFE
	strb	w0,[x20,91]
	add	x0,x20,510
	movl	x1,0xAA55
	bl	st16
	ldr	x19,[sp],16
	epilogue

include '../../inc/tool.inc'
include '../../inc/volutil.inc'

s_o_y		db '-y',0
s_o_f		db '-f',0
s_o_card	db '--card',0
s_o_t		db '-t',0
s_o_l		db '-L',0
s_o_c		db '-c',0
s_auto		db 'auto',0
s_fat32		db 'fat32',0
s_vfat		db 'vfat',0
s_sd0		db 'sd0',0
s_sd		db 'sd',0
s_blk		db 'blk.',0
s_blk_sd	db 'blk.sd',0
s_blk_sd0p1	db 'blk.sd0p1',0
s_oem		db 'KORIN OS'
s_no_name	db 'NO NAME    '
s_fat32_bs	db 'FAT32   '
m_usage		db 'usage : mkfs [-t auto|fat32] [-L ',0xC3,0xA9,'tiquette] [-c octets] [-y] [-f] [--card] VOLUME:',10,0
m_type		db 'mkfs : seul FAT32 est pris en charge (-t auto ou fat32)',10,0
m_mkfs		db 'mkfs : ',0
m_longue	db 'mkfs : ',0xC3,0xA9,'tiquette trop longue (11 caract',0xC3,0xA8,'res au plus)',10,0
m_systeme	db ' est le volume du syst',0xC3,0xA8,'me : il n',0x27,'est jamais format',0xC3,0xA9,10,0
m_carte		db ' est une partition de la carte SD : ajoutez --card',10,0
m_inconnu	db ' : volume inconnu (ni mont',0xC3,0xA9,', ni dans SD0:/etc/mounts.txt, ni une cl',0xC3,0xA9,' USB)',10,0
m_pas_bloc	db ' n',0x27,'est pas sur un p',0xC3,0xA9,'riph',0xC3,0xA9,'rique bloc',10,0
m_sans_bloc	db ' : son p',0xC3,0xA9,'riph',0xC3,0xA9,'rique ne r',0xC3,0xA9,'pond pas',10,0
m_protege	db ' : le p',0xC3,0xA9,'riph',0xC3,0xA9,'rique est prot',0xC3,0xA9,'g',0xC3,0xA9,' en ',0xC3,0xA9,'criture',10,0
m_petit		db ' : trop petit ou trop grand pour FAT32 avec ces clusters (65 525 clusters au moins : 33 Mo)',10,0
m_mauvais_c	db ' : taille de cluster invalide (512 ',0xC3,0xA0,' 65536, une puissance de 2)',10,0
m_efface1	db 'Tout ce qui est sur ',0
m_paren		db ' (',0
m_virgule	db ', ',0
m_efface2	db ') sera effac',0xC3,0xA9,'.',10,0
m_confirme1	db 'Tapez le nom du volume (',0
m_confirme2	db ') pour le formater : ',0
m_non		db 'mkfs : pas format',0xC3,0xA9,10,0
m_ajoutez_f	db '(fermez ses fichiers, ou ajoutez -f)',10,0
m_formatage	db 'Formatage de ',0
m_points	db '...',10,0
m_formate	db ' format',0xC3,0xA9,' : ',0
m_cluster	db ', clusters de ',0
m_octets	db ' octets',0
m_guill1	db ', « ',0
m_guill2	db ' »',0
m_pas_formate	db ' pas format',0xC3,0xA9,' : ',0
m_pas_remonte	db ' : format',0xC3,0xA9,', mais pas remont',0xC3,0xA9,' : ',0
m_nl		db 10,0
		align 4

segment readable writeable
kit_data
tool_data
volutil_data
		align 8
canal		rb 8
pid_blk		rb 8
tampon		rb 8
monte		rb 8
opt_cluster	rb 8
secteurs	rb 8
spc		rb 8
fatsz		rb 8
donnees		rb 8
clusters	rb 8
aff_garde	rb 16
etiquette	rb 64
msg		rb MSG_TAILLE
