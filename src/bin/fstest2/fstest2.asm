; fstest2 - essai des services de fichiers en direct (docs/FICHIERS.md, etape
; 0) : sans le relais du noyau, par SVC_CALL au service du volume
; (« fs.ram0 », « fs.img0 » s'il est monte) :
;  * ops 14-16 : tampon partage (FS_TAMPON, buf_map), ecriture et relecture
;    par le tampon de 1 octet, 4 Ko, 64 Ko, a cheval sur 64 Ko, fin de
;    fichier, refus au-dela de 64 Ko, fermeture (appel) ;
;  * proprietaire : un fils (« fstest2 enfant <numero> ») essaie le fichier
;    du pere (E_BADF attendu), ouvre N_ENF fichiers et se termine sans les
;    fermer ; le service doit les avoir fermes (avis de fin) : le pere les
;    rouvre tous.
;  * fatfs (« fs.sd0 », FAT32 ; « fs.sd1 », exFAT, s'il est monte), fichier
;    tmp/fstest2.nd ouvert plusieurs fois : les ecritures d'un numero vues
;    des autres (donnees, taille, stat avant fermeture), O_APPEND a la fin
;    reelle, O_TRUNC pour toutes les ouvertures, unlink d'un fichier ouvert
;    (encore lisible, absent du repertoire, nouveau fichier du meme nom
;    distinct ; clusters liberes a la derniere fermeture : fsck).
; Lignes « fstest2 : PASS ... » / « fstest2 : FAIL ... » ; code de sortie :
; le nombre d'echecs.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

N_ENF	= 60				; fichiers laisses ouverts par le fils
T64	= FS_TAMPON_TAILLE
CHEV	= 64536				; seconde ecriture (a cheval sur 64 Ko)

; x5 = indice, x3 = graine -> w7 = octet du motif (x6 ecrase)
macro motif {
	lsr	x6,x5,8
	add	x7,x5,x6
	add	x7,x7,x6,lsl 1
	lsr	x6,x5,16
	add	x7,x7,x6
	add	x7,x7,x6,lsl 2
	mov	x6,29
	madd	x7,x3,x6,x7
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,.pere
	ldrb	w0,[x19]
	cmp	w0,'e'
	b.eq	enfant
.pere:	mov	x0,2000				; (lance au demarrage : le temps que
	sys	SYS_SLEEP			;  vfs monte RAM0: et IMG0:)
	adr	x0,s_ram0
	mov	x1,1				; obligatoire
	bl	essai_volume
	adr	x0,s_img0
	mov	x1,0				; s'il est monte
	bl	essai_volume
	adr	x0,s_sd0			; fatfs : fichier partage
	mov	x1,1
	bl	essai_noeuds
	adr	x0,s_sd1
	mov	x1,0
	bl	essai_noeuds
	bl	essai_fins
	bl	essai_registres
	adr	x0,m_fin
	bl	puts
	adr	x0,echecs
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,echecs
	ldr	x0,[x0]
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Le fils : « enfant <numero d'un fichier du pere> »
; ---------------------------------------------------------------------------
enfant:
	add	x0,x19,7
	bl	parse_dec
	mov	x20,x0				; le fichier du pere
	adr	x0,s_enfant
	adr	x1,vol_cour
	str	x0,[x1]
	adr	x0,s_ram0
	mov	x1,3000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.fin
	adr	x1,canal
	str	x0,[x1]
	mov	x0,FS_LIRE			; le fichier d'un autre : E_BADF
	mov	x1,x20
	mov	x2,0
	mov	x3,10
	bl	appel
	adr	x1,l_e_lire
	bl	attendu_badf
	mov	x0,FS_TAMPON			; (son tampon : rendu a sa fin)
	bl	appel0
	adr	x1,msg
	ldr	x0,[x1,MSG_A]
	sys	SYS_BUF_MAP
	mov	x0,FS_LIRE_T
	mov	x1,x20
	mov	x2,0
	mov	x3,10
	bl	appel
	adr	x1,l_e_lire_t
	bl	attendu_badf
	mov	x0,FS_ECRIRE_T
	mov	x1,x20
	mov	x2,0
	mov	x3,10
	bl	appel
	adr	x1,l_e_ecrire_t
	bl	attendu_badf
	mov	x0,FS_FERMER
	mov	x1,x20
	mov	x2,0
	mov	x3,0
	bl	appel
	adr	x1,l_e_fermer
	bl	attendu_badf
	bl	ouvrir_n			; N_ENF fichiers, jamais fermes
	mov	x2,x0
	cmp	x0,N_ENF
	cset	x0,eq
	adr	x1,l_e_ouvrir
	bl	verdict
.fin:	adr	x0,echecs
	ldr	x0,[x0]
	sys	SYS_EXIT

; x0 = resultat, x1 = libelle : PASS si E_BADF
attendu_badf:
	mov	x2,x0
	cmn	x0,-E_BADF
	cset	x0,eq
	b	verdict

; ---------------------------------------------------------------------------
;  Un volume : ops 14-16
; ---------------------------------------------------------------------------

; x0 = nom du service (« fs.ram0 »), x1 = 1 s'il doit exister
essai_volume:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x1,vol_cour
	str	x19,[x1]
	mov	x0,x19
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbz	x0,63,.ouvert
	mov	x2,x0
	cbz	x20,.absent
	mov	x0,0
	adr	x1,l_svc
	bl	verdict
	b	.out
.absent:
	adr	x0,m_skip
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_absent
	bl	puts
	b	.out
.ouvert:
	adr	x1,canal
	str	x0,[x1]
	mov	x0,FS_TAMPON			; 14 : le tampon partage
	bl	appel0
	mov	x21,x0
	adr	x1,msg
	ldr	x22,[x1,MSG_A]
	cmp	x21,0
	cset	x0,eq
	adr	x1,l_tampon
	mov	x2,x22
	bl	verdict
	tbnz	x21,63,.out
	mov	x0,x22				; mappe ici
	sys	SYS_BUF_MAP
	mov	x23,x0
	mov	x2,x0
	cmp	x0,0
	cset	x0,gt
	adr	x1,l_map
	bl	verdict
	tbnz	x23,63,.out
	mov	x0,FS_TAMPON			; une seconde fois : le meme
	bl	appel0
	adr	x1,msg
	ldr	x2,[x1,MSG_A]
	cmp	x0,0
	ccmp	x2,x22,0,eq
	cset	x0,eq
	adr	x1,l_meme
	bl	verdict
	mov	x0,FS_OUVRIR
	mov	x1,O_READ or O_WRITE or O_CREATE or O_TRUNC
	adr	x2,p_bin
	bl	appel_chemin
	mov	x21,x0
	adr	x1,msg
	ldr	x24,[x1,MSG_B]			; numero du fichier
	mov	x2,x21
	cmp	x21,0
	cset	x0,eq
	adr	x1,l_ouvrir
	bl	verdict
	tbnz	x21,63,.out
	mov	x0,1				; 1 octet, 4 Ko, 64 Ko
	mov	x1,x23
	mov	x2,x24
	bl	essai_taille
	mov	x0,4096
	mov	x1,x23
	mov	x2,x24
	bl	essai_taille
	movl	x0,T64
	mov	x1,x23
	mov	x2,x24
	bl	essai_taille
	mov	x0,x23				; a cheval sur 64 Ko, fin de fichier
	mov	x1,x24
	bl	essai_chevauche
	mov	x0,FS_LIRE_T			; plus que le tampon : refuse
	mov	x1,x24
	mov	x2,0
	movl	x3,T64+1
	bl	appel
	mov	x2,x0
	cmp	x0,0
	cset	x0,lt
	adr	x1,l_trop
	bl	verdict
	mov	x0,FS_FERMER			; 12 : appel du proprietaire
	mov	x1,x24
	mov	x2,0
	mov	x3,0
	bl	appel
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_fermer
	bl	verdict
	mov	x0,FS_LIRE_T			; ferme : plus de lecture
	mov	x1,x24
	mov	x2,0
	mov	x3,1
	bl	appel
	mov	x2,x0
	cmp	x0,0
	cset	x0,lt
	adr	x1,l_ferme
	bl	verdict
	mov	x0,FS_UNLINK
	mov	x1,0
	adr	x2,p_bin
	bl	appel_chemin
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_unlink
	bl	verdict
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  fatfs : un fichier ouvert plusieurs fois (noeud partage)
; ---------------------------------------------------------------------------

; x0 = resultat, x1 = libelle, x2 = valeur attendue : PASS si egaux (valeur
; affichee : le resultat)
attendu:
	cmp	x0,x2
	mov	x2,x0
	cset	x0,eq
	b	verdict

; x0 = drapeaux -> x0 = numero du fichier tmp/fstest2.nd ouvert (resultat
; < 0 sinon), x1 = taille rendue
ouvrir_nd:
	prologue
	mov	x1,x0
	mov	x0,FS_OUVRIR
	adr	x2,p_nd
	bl	appel_chemin
	adr	x1,msg
	tbnz	x0,63,.out
	ldr	x0,[x1,MSG_B]
.out:	ldr	x1,[x1,MSG_D]
	epilogue

; x0 = numero, x1 = position, x2 = octets (<= 464), x3 = graine, x4 = indice
; du motif -> x0 = resultat de FS_ECRIRE, x1 = nouvelle taille
ecrire_m:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,msg
	add	x0,x0,MSG_DATA
	mov	x1,x21
	mov	x2,x4
	bl	remplir
	adr	x4,msg
	mov	x0,FS_ECRIRE
	stp	x0,x19,[x4]
	stp	x20,x21,[x4,MSG_B]
	str	w21,[x4,MSG_LEN]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x4
	sys	SYS_SVC_CALL
	adr	x1,msg
	ldr	x1,[x1,MSG_B]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero, x1 = position, x2 = octets (<= 464), x3 = graine
; -> x0 = octets lus (FS_LIRE), x1 = octets differents du motif
lire_m:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,FS_LIRE
	bl	appel
	mov	x1,0
	cmp	x0,0
	b.le	.out
	str	x0,[sp,-16]!
	mov	x1,x0
	adr	x0,msg
	add	x0,x0,MSG_DATA
	mov	x2,x19
	mov	x3,x20
	bl	verifier
	mov	x1,x0
	ldr	x0,[sp],16
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = octets lus, x1 = differences, x2 = octets attendus, x3 = libelle
lu_bien:
	cmp	x0,x2
	ccmp	x1,0,0,eq
	mov	x2,x0
	cset	x0,eq
	mov	x1,x3
	b	verdict

; x0 = nom du service (« fs.sd0 »), x1 = 1 s'il doit exister
essai_noeuds:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x1
	adr	x1,vol_cour
	str	x0,[x1]
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbz	x0,63,.ouvert
	mov	x2,x0
	cbz	x19,.absent
	mov	x0,0
	adr	x1,l_svc
	bl	verdict
	b	.out
.absent:
	adr	x0,m_skip
	bl	puts
	adr	x0,vol_cour
	ldr	x0,[x0]
	bl	puts
	adr	x0,m_absent
	bl	puts
	b	.out
.ouvert:
	adr	x1,canal
	str	x0,[x1]
	mov	x0,FS_MKDIR			; tmp (deja la : E_EXIST)
	mov	x1,0
	adr	x2,p_tmp
	bl	appel_chemin
	mov	x0,O_READ or O_WRITE or O_CREATE or O_TRUNC
	bl	ouvrir_nd			; A
	mov	x20,x0
	mov	x2,x0
	cmp	x0,0
	cset	x0,ge
	adr	x1,l_n_a
	bl	verdict
	tbnz	x20,63,.out
	mov	x0,O_READ or O_WRITE		; B
	bl	ouvrir_nd
	mov	x21,x0
	mov	x2,x0
	cmp	x0,0
	cset	x0,ge
	adr	x1,l_n_b
	bl	verdict
	tbnz	x21,63,.out
	mov	x0,x20				; A ecrit 300 o
	mov	x1,0
	mov	x2,300
	mov	x3,5
	mov	x4,0
	bl	ecrire_m
	mov	x2,300
	adr	x1,l_n_ecr
	bl	attendu
	mov	x0,x21				; B les relit
	mov	x1,0
	mov	x2,300
	mov	x3,5
	bl	lire_m
	mov	x2,300
	adr	x3,l_n_lire
	bl	lu_bien
	mov	x0,FS_STAT			; stat avant toute fermeture
	mov	x1,0
	adr	x2,p_nd
	bl	appel_chemin
	adr	x1,msg
	ldr	x0,[x1,MSG_B]
	mov	x2,300
	adr	x1,l_n_stat
	bl	attendu
	mov	x0,O_WRITE or O_APPEND		; C : O_APPEND
	bl	ouvrir_nd
	mov	x22,x0
	mov	x0,x1				; taille vue a l'ouverture
	mov	x2,300
	adr	x1,l_n_c
	bl	attendu
	tbnz	x22,63,.out
	mov	x0,x22				; 50 o demandes a la position 0
	mov	x1,0
	mov	x2,50
	mov	x3,7
	mov	x4,300
	bl	ecrire_m
	mov	x0,x1				; -> a la fin : taille 350
	mov	x2,350
	adr	x1,l_n_app
	bl	attendu
	mov	x0,x20				; A relit la fin
	mov	x1,300
	mov	x2,50
	mov	x3,7
	bl	lire_m
	mov	x2,50
	adr	x3,l_n_app_lu
	bl	lu_bien
	mov	x0,x20				; et le debut, intact
	mov	x1,0
	mov	x2,300
	mov	x3,5
	bl	lire_m
	mov	x2,300
	adr	x3,l_n_debut
	bl	lu_bien
	mov	x0,O_WRITE or O_TRUNC		; D : O_TRUNC
	bl	ouvrir_nd
	mov	x23,x0
	mov	x0,x1
	mov	x2,0
	adr	x1,l_n_d
	bl	attendu
	tbnz	x23,63,.out
	mov	x0,FS_LIRE			; vide aussi pour B
	mov	x1,x21
	mov	x2,0
	mov	x3,100
	bl	appel
	mov	x2,0
	adr	x1,l_n_vide
	bl	attendu
	mov	x0,x20				; A reecrit 200 o
	mov	x1,0
	mov	x2,200
	mov	x3,9
	mov	x4,0
	bl	ecrire_m
	mov	x0,x1
	mov	x2,200
	adr	x1,l_n_ecr2
	bl	attendu
	mov	x0,FS_UNLINK			; unlink pendant l'ouverture
	mov	x1,0
	adr	x2,p_nd
	bl	appel_chemin
	mov	x2,0
	adr	x1,l_n_unlink
	bl	attendu
	mov	x0,FS_STAT
	mov	x1,0
	adr	x2,p_nd
	bl	appel_chemin
	mov	x2,E_NOENT
	adr	x1,l_n_parti
	bl	attendu
	mov	x0,x21				; encore lisible par B
	mov	x1,0
	mov	x2,200
	mov	x3,9
	bl	lire_m
	mov	x2,200
	adr	x3,l_n_lisible
	bl	lu_bien
	mov	x0,O_READ or O_WRITE or O_CREATE or O_TRUNC
	bl	ouvrir_nd			; E : nouveau fichier du meme nom
	mov	x24,x0
	mov	x0,x1
	mov	x2,0
	adr	x1,l_n_e
	bl	attendu
	tbnz	x24,63,.out
	mov	x0,x24
	mov	x1,0
	mov	x2,20
	mov	x3,11
	mov	x4,0
	bl	ecrire_m
	mov	x2,20
	adr	x1,l_n_ecr3
	bl	attendu
	mov	x0,x21				; l'ancien ne change pas
	mov	x1,0
	mov	x2,200
	mov	x3,9
	bl	lire_m
	mov	x2,200
	adr	x3,l_n_ancien
	bl	lu_bien
	mov	x0,x20				; fermetures
	bl	fermer_nd
	mov	x0,x21
	bl	fermer_nd
	mov	x0,x22
	bl	fermer_nd
	mov	x0,x23
	bl	fermer_nd
	mov	x0,x24
	bl	fermer_nd
	mov	x0,FS_STAT			; le nouveau : 20 o
	mov	x1,0
	adr	x2,p_nd
	bl	appel_chemin
	adr	x1,msg
	ldr	x0,[x1,MSG_B]
	mov	x2,20
	adr	x1,l_n_stat2
	bl	attendu
	mov	x0,FS_UNLINK
	mov	x1,0
	adr	x2,p_nd
	bl	appel_chemin
	mov	x2,0
	adr	x1,l_n_unlink2
	bl	attendu
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero : FS_FERMER (appel)
fermer_nd:
	mov	x1,x0
	mov	x0,FS_FERMER
	mov	x2,0
	mov	x3,0
	b	appel

; x0 = octets, x1 = tampon, x2 = fichier : ecrit au debut par le tampon,
; relit, compare
essai_taille:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,x20				; motif (graine = octets)
	mov	x1,x19
	mov	x2,0
	mov	x3,x19
	bl	remplir
	mov	x0,FS_ECRIRE_T
	mov	x1,x21
	mov	x2,0
	mov	x3,x19
	bl	appel
	mov	x22,x0
	adr	x1,msg
	ldr	x1,[x1,MSG_B]			; nouvelle taille >= octets
	cmp	x22,x19
	ccmp	x1,x19,0,eq			; (sinon : lo)
	cset	x0,hs
	adr	x1,l_ecrire_t
	mov	x2,x22
	bl	verdict_n
	mov	x0,x20				; tampon brouille, puis relu
	movl	x1,T64
	bl	brouiller
	mov	x0,FS_LIRE_T
	mov	x1,x21
	mov	x2,0
	mov	x3,x19
	bl	appel
	mov	x22,x0
	cmp	x0,x19
	cset	x0,eq
	adr	x1,l_lire_t
	mov	x2,x22
	bl	verdict_n
	mov	x0,x20
	mov	x1,x19
	mov	x2,0
	mov	x3,x19
	bl	verifier
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_diff
	bl	verdict_n
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = fichier : 64 Ko (graine 1) a 0, 64 Ko (graine 2) a
; CHEV ; relecture de 64 Ko a 30000 (les deux motifs), fin de fichier
essai_chevauche:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,x19
	movl	x1,T64
	mov	x2,0
	mov	x3,1
	bl	remplir
	mov	x0,FS_ECRIRE_T
	mov	x1,x20
	mov	x2,0
	movl	x3,T64
	bl	appel
	mov	x0,x19
	movl	x1,T64
	mov	x2,0
	mov	x3,2
	bl	remplir
	mov	x0,FS_ECRIRE_T
	mov	x1,x20
	movl	x2,CHEV
	movl	x3,T64
	bl	appel
	mov	x21,x0
	adr	x1,msg
	ldr	x22,[x1,MSG_B]
	movl	x1,T64
	movl	x3,CHEV+T64
	cmp	x21,x1
	ccmp	x22,x3,0,eq
	cset	x0,eq
	adr	x1,l_chev_ecr
	mov	x2,x22
	bl	verdict
	mov	x0,x19
	movl	x1,T64
	bl	brouiller
	mov	x0,FS_LIRE_T			; 30000 .. 30000 + 64 Ko
	mov	x1,x20
	mov	x2,30000
	movl	x3,T64
	bl	appel
	mov	x21,x0
	mov	x0,x19				; graine 1 jusqu'a CHEV
	movl	x1,CHEV-30000
	mov	x2,30000
	mov	x3,1
	bl	verifier
	mov	x22,x0
	movl	x0,CHEV-30000			; graine 2 ensuite
	add	x0,x19,x0
	movl	x1,T64-(CHEV-30000)
	mov	x2,0
	mov	x3,2
	bl	verifier
	add	x22,x22,x0
	movl	x1,T64
	cmp	x21,x1
	ccmp	x22,0,0,eq
	cset	x0,eq
	adr	x1,l_chev_lu
	mov	x2,x22
	bl	verdict
	mov	x0,FS_LIRE_T			; fin de fichier : 100 octets
	mov	x1,x20
	movl	x2,CHEV+T64-100
	movl	x3,T64
	bl	appel
	mov	x2,x0
	cmp	x0,100
	cset	x0,eq
	adr	x1,l_fin_fic
	bl	verdict
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Fin d'un process : ses fichiers fermes par le service
; ---------------------------------------------------------------------------
essai_fins:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,s_ram0
	adr	x1,vol_cour
	str	x0,[x1]
	mov	x1,3000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	adr	x1,canal
	str	x0,[x1]
	mov	x0,FS_OUVRIR			; le fichier du pere
	mov	x1,O_READ or O_WRITE or O_CREATE or O_TRUNC
	adr	x2,p_own
	bl	appel_chemin
	tbnz	x0,63,.out
	adr	x1,msg
	ldr	x19,[x1,MSG_B]
	mov	x0,FS_ECRIRE			; 10 octets (message)
	mov	x1,x19
	mov	x2,0
	mov	x3,10
	bl	appel
	adr	x0,argument			; « enfant <numero> »
	adr	x1,s_arg
	bl	str_cat
	mov	x1,x0
	mov	x0,x19
	bl	fmt_dec
	adr	x0,p_moi
	adr	x1,argument
	sys	SYS_SPAWN
	mov	x20,x0
	mov	x2,x0
	cmp	x0,0
	cset	x0,gt
	adr	x1,l_spawn
	bl	verdict
	tbnz	x20,63,.ferme
	mov	x0,x20
	sys	SYS_WAIT
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_enfant
	bl	verdict
	adr	x0,s_ram0
	adr	x1,vol_cour
	str	x0,[x1]
	mov	x0,FS_LIRE			; le sien : toujours ouvert
	mov	x1,x19
	mov	x2,0
	mov	x3,10
	bl	appel
	mov	x2,x0
	cmp	x0,10
	cset	x0,eq
	adr	x1,l_intact
	bl	verdict
	mov	x21,0				; ceux du fils : fermes ? (l'avis de
.essai:	bl	ouvrir_n			; fin suit wait de peu)
	mov	x22,x0
	bl	fermer_n
	cmp	x22,N_ENF
	b.eq	.vu
	add	x21,x21,1
	cmp	x21,30
	b.hs	.vu
	mov	x0,100
	sys	SYS_SLEEP
	b	.essai
.vu:	mov	x2,x22
	cmp	x22,N_ENF
	cset	x0,eq
	adr	x1,l_fuite
	bl	verdict
.ferme:	mov	x0,FS_FERMER
	mov	x1,x19
	mov	x2,0
	mov	x3,0
	bl	appel
	mov	x0,FS_UNLINK
	mov	x1,0
	adr	x2,p_own
	bl	appel_chemin
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = fichiers ouverts (p_own, lecture : N_ENF au plus, dans « ids »)
ouvrir_n:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.o:	cmp	x19,N_ENF
	b.hs	.f
	mov	x0,FS_OUVRIR
	mov	x1,O_READ
	adr	x2,p_own
	bl	appel_chemin
	tbnz	x0,63,.f
	adr	x1,msg
	ldr	x0,[x1,MSG_B]
	adr	x1,ids
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	b	.o
.f:	adr	x0,nids
	str	x19,[x0]
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; ferme les fichiers de « ids »
fermer_n:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0
.c:	adr	x0,nids
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.f
	adr	x1,ids
	ldr	x1,[x1,x19,lsl 3]
	mov	x0,FS_FERMER
	mov	x2,0
	mov	x3,0
	bl	appel
	add	x19,x19,1
	b	.c
.f:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Outils
; ---------------------------------------------------------------------------

; x0 = op (sans argument) -> resultat
appel0:
	mov	x1,0
	mov	x2,0
	mov	x3,0
; x0 = op, x1 = a, x2 = b, x3 = c -> x0 = resultat (reponse dans msg)
appel:
	adr	x4,msg
	stp	x0,x1,[x4]
	stp	x2,x3,[x4,MSG_B]
	str	wzr,[x4,MSG_LEN]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x4
	sys	SYS_SVC_CALL
	ret

; x0 = op, x1 = a, x2 = chemin (relatif au volume) -> x0 = resultat
appel_chemin:
	adr	x4,msg
	stp	x0,x1,[x4]
	add	x5,x4,MSG_DATA
	mov	x6,0
.c:	ldrb	w7,[x2,x6]
	strb	w7,[x5,x6]
	add	x6,x6,1
	cbnz	w7,.c
	str	w6,[x4,MSG_LEN]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x4
	sys	SYS_SVC_CALL
	ret

; x0 = tampon, x1 = octets, x2 = indice de depart, x3 = graine : motif
remplir:
	mov	x4,0
.l:	cmp	x4,x1
	b.hs	.f
	add	x5,x2,x4
	motif
	strb	w7,[x0,x4]
	add	x4,x4,1
	b	.l
.f:	ret

; (memes arguments) -> x0 = octets differents du motif
verifier:
	mov	x4,0
	mov	x8,0
.l:	cmp	x4,x1
	b.hs	.f
	add	x5,x2,x4
	motif
	ldrb	w9,[x0,x4]
	and	w7,w7,0xFF
	cmp	w7,w9
	cinc	x8,x8,ne
	add	x4,x4,1
	b	.l
.f:	mov	x0,x8
	ret

; x0 = tampon, x1 = octets : 0xEE partout
brouiller:
	mov	w2,0xEE
.l:	cbz	x1,.f
	sub	x1,x1,1
	strb	w2,[x0,x1]
	b	.l
.f:	ret


; ---------------------------------------------------------------------------
;  Registres a travers un appel de fichier (etape 2 : fichier_sys d'AppKit) :
;  x1-x15, x17, x18 et NZCV rendus comme par svc 0 (stat, puis read de
;  4098 octets par le tampon partage)
; ---------------------------------------------------------------------------
macro regs_poser {
	irp r, 3,4,5,6,7,9,10,11,12,13,14,15,17,18 \{ mov x\#r,0x100+r \}
	cmp	x3,x4				; N = 1, Z = 0, C = 0, V = 0
}
; x19 = x1 attendu, x20 = x2 attendu, x21 = x8 attendu -> x0 = ecarts
macro regs_compter {
	local	lf, lg
	mov	x16,0
	b.pl	lf
	b.eq	lf
	b.hs	lf
	b.vs	lf
	b	lg
lf:	add	x16,x16,1
lg:
	irp r, 3,4,5,6,7,9,10,11,12,13,14,15,17,18 \{
	cmp	x\#r,0x100+r
	cinc	x16,x16,ne \}
	cmp	x1,x19
	cinc	x16,x16,ne
	cmp	x2,x20
	cinc	x16,x16,ne
	cmp	x8,x21
	cinc	x16,x16,ne
}
essai_registres:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,8192
	adr	x0,s_regs
	adr	x1,vol_cour
	str	x0,[x1]
	mov	x19,sp				; stat
	mov	x20,0x1002
	mov	x21,SYS_STAT
	adr	x0,p_moi
	mov	x1,x19
	mov	x2,x20
	regs_poser
	sys	SYS_STAT
	mov	x22,x0
	regs_compter
	cmp	x16,0
	cset	x0,eq
	adr	x1,l_regs_stat
	mov	x2,x16
	bl	verdict
	adr	x0,p_moi			; read de 4098 octets (tampon partage)
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x22,x0
	mov	x21,SYS_READ
	mov	x0,x22
	mov	x1,x19
	mov	x2,x20
	regs_poser
	sys	SYS_READ
	str	x0,[sp,4104]
	regs_compter
	ldr	x0,[sp,4104]
	cmp	x0,x20
	cinc	x16,x16,ne
	cmp	x16,0
	cset	x0,eq
	adr	x1,l_regs_read
	mov	x2,x16
	bl	verdict
	mov	x0,x22
	sys	SYS_CLOSE
	add	sp,sp,8192
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_regs		db 'fichier_sys',0
l_regs_stat	db 'stat : x1-x15, x17, x18, NZCV gardes (ecarts)',0
l_regs_read	db 'read 4098 o : x1-x15, x17, x18, NZCV gardes (ecarts)',0
		align 4

; x0 = 1 reussi / 0 echec, x1 = libelle, x2 = valeur, x19 = octets (dans
; le libelle : « ecrire_t 4096 »)
verdict_n:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x20,x0
	mov	x22,x2
	adr	x0,libelle
	bl	str_cat
	mov	x1,x0
	mov	x0,x19
	bl	fmt_dec
	mov	x0,x20
	adr	x1,libelle
	mov	x2,x22
	bl	verdict
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x0 = 1 reussi / 0 echec, x1 = libelle, x2 = valeur :
; « fstest2 : PASS fs.ram0 : libelle -> valeur »
verdict:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,m_pass
	cbnz	x19,.p
	adr	x1,echecs
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
	adr	x0,m_fail
.p:	bl	puts
	adr	x0,vol_cour
	ldr	x0,[x0]
	bl	puts
	adr	x0,m_sep
	bl	puts
	mov	x0,x20
	bl	puts
	adr	x0,m_val
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_ram0	db 'fs.ram0',0
s_img0	db 'fs.img0',0
s_sd0	db 'fs.sd0',0
s_sd1	db 'fs.sd1',0
p_tmp	db 'tmp',0
p_nd	db 'tmp/fstest2.nd',0
l_n_a	db 'FS_OUVRIR tmp/fstest2.nd (A, creation)',0
l_n_b	db 'FS_OUVRIR du meme fichier (B)',0
l_n_ecr	db 'A : FS_ECRIRE 300 o',0
l_n_lire db 'B : FS_LIRE des 300 o ecrits par A',0
l_n_stat db 'FS_STAT avant fermeture : taille 300',0
l_n_c	db 'FS_OUVRIR O_APPEND (C) : taille 300',0
l_n_app	db 'C : FS_ECRIRE 50 o en 0 avec O_APPEND : taille 350',0
l_n_app_lu db 'A : FS_LIRE des 50 o ajoutes a la fin par C',0
l_n_debut db 'A : FS_LIRE des 300 premiers octets (intacts)',0
l_n_d	db 'FS_OUVRIR O_TRUNC (D) : taille 0',0
l_n_vide db 'B : FS_LIRE apres O_TRUNC de D : 0 octet',0
l_n_ecr2 db 'A : FS_ECRIRE 200 o : taille 200',0
l_n_unlink db 'FS_UNLINK du fichier ouvert',0
l_n_parti db 'FS_STAT apres unlink : E_NOENT',0
l_n_lisible db 'B : FS_LIRE apres unlink : encore lisible',0
l_n_e	db 'FS_OUVRIR creation du meme nom (E) : taille 0',0
l_n_ecr3 db 'E : FS_ECRIRE 20 o',0
l_n_ancien db 'B : FS_LIRE de l ancien fichier (inchange par E)',0
l_n_stat2 db 'FS_STAT apres les fermetures : 20 (E)',0
l_n_unlink2 db 'FS_UNLINK du nouveau fichier',0
s_enfant db 'fs.ram0 (fils)',0
s_arg	db 'enfant ',0
p_moi	db 'SD0:/bin/fstest2',0
p_bin	db 'fstest2.bin',0
p_own	db 'fstest2.own',0
m_pass	db 'fstest2 : PASS ',0
m_fail	db 'fstest2 : FAIL ',0
m_skip	db 'fstest2 : SKIP ',0
m_absent db ' absent',10,0
m_sep	db ' : ',0
m_val	db ' -> ',0
m_nl	db 10,0
m_fin	db 'fstest2 : termine, echecs : ',0
l_svc	db 'svc_open',0
l_tampon db 'FS_TAMPON (numero)',0
l_map	db 'buf_map (adresse)',0
l_meme	db 'FS_TAMPON une seconde fois : meme numero',0
l_ouvrir db 'FS_OUVRIR fstest2.bin (creation)',0
l_ecrire_t db 'FS_ECRIRE_T ',0
l_lire_t db 'FS_LIRE_T ',0
l_diff	db 'octets differents apres relecture de ',0
l_chev_ecr db 'FS_ECRIRE_T 64 Ko a 64536 (a cheval sur 64 Ko) : taille',0
l_chev_lu db 'FS_LIRE_T 64 Ko a 30000 (deux ecritures) : octets differents',0
l_fin_fic db 'FS_LIRE_T 64 Ko a 100 octets de la fin',0
l_trop	db 'FS_LIRE_T de 64 Ko + 1 refuse',0
l_fermer db 'FS_FERMER (appel du proprietaire)',0
l_ferme	db 'FS_LIRE_T apres fermeture refuse',0
l_unlink db 'FS_UNLINK fstest2.bin',0
l_spawn	db 'spawn du fils',0
l_enfant db 'fils : code de sortie (ses echecs)',0
l_intact db 'fichier du pere encore ouvert apres le fils (FS_LIRE)',0
l_fuite	db 'fichiers laisses ouverts par le fils fermes a sa fin (rouverts)',0
l_e_lire db 'FS_LIRE du fichier du pere : E_BADF',0
l_e_lire_t db 'FS_LIRE_T du fichier du pere : E_BADF',0
l_e_ecrire_t db 'FS_ECRIRE_T du fichier du pere : E_BADF',0
l_e_fermer db 'FS_FERMER du fichier du pere : E_BADF',0
l_e_ouvrir db 'ouvertures laissees ouvertes',0
	align 8

segment readable writeable
kit_data
		align 8
echecs		rb 8
canal		rb 8
vol_cour	rb 8
nids		rb 8
ids		rb N_ENF * 8
libelle		rb 128
argument	rb 64
msg		rb MSG_TAILLE
