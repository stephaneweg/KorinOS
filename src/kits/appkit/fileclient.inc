; ============================================================================
;  kits/appkit/fileclient.inc - client de fichiers d'AppKit (docs/FICHIERS.md,
;  etape 2) : les appels de fichiers d'un programme ne passent plus par le
;  noyau.
;
;  * Descripteurs 64..127 (FC_BASE..) : table dans les variables du kit
;    (recopiees par process : propre a chaque process, partagee par ses
;    threads) ; type, canal du service FS, numero chez lui, position,
;    taille, drapeaux. Un verrou (fc_verrou) protege l'allocation.
;  * Chemins (open, stat, mkdir, unlink, chdir, getcwd) : au service vfs
;    (ops 30-35, dossier courant par pid) ; read / write / readdir / close :
;    directement au service FS (ops 10-13) ; seek : sans message.
;  * Gros transferts (> 464 o) : le tampon partage du couple (process,
;    service FS), 64 Ko (FS_TAMPON, buf_map), demande au premier besoin ;
;    sans lui (refus, table pleine), les messages de 464 o.
;  * Descripteurs 0..2 : entree, sortie, erreur standard (etape 4) : un bout
;    de tube de vfs (type 4) si le pere en a donne un (proc_flags, V_STDIO :
;    demandes au premier usage), sinon la console du noyau (print).
;    Descripteurs 3..63 : aucun (E_BADF ; le noyau n'en a plus, etape 5).
;  * Tubes (etape 4) : servis par vfs (ops 37-41), descripteur de type 4
;    (canal de vfs, poignee du bout) ; lecture : attend des donnees, sauf
;    O_NONBLOCK (E_AGAIN) ; ecriture : jusqu'au bout (tube plein : on
;    reessaie). print (appel 1) ecrit sur la sortie standard.
;  * spawn, spawn_pipe (etape 4) : V_EXEC a vfs, lecture de l'executable
;    (tampon partage temporaire), spawn_image du noyau ; droits demandes
;    pour un fichier de SD0:/sys (ATTR_SYS) ; sans droits soi-meme : init2
;    le lance (INIT_OP_LAUNCH). spawn_pipe : sortie et erreur du fils dans
;    un tube neuf, lu par le pere (O_NONBLOCK, comme le tube du noyau).
;  * chdir : vfs seul (le noyau n'a plus de dossier courant, etape 5).
;  * Kits (etape 5) : f_kit_load lit SD0:/lib/<nom>.so (ATTR_LIB de vfs)
;    dans un tampon partage temporaire et le fait inscrire par le noyau
;    (kit_register, droits de SD0:/sys) ; sans droits : par init2
;    (INIT_OP_KIT_LOAD). kits_open (kit.inc) l'appelle pour un kit absent.
;
;  fichier_sys : x8 = numero, x0-x5 arguments (comme svc 0) ; rend x0 et
;  garde tout le reste (x1-x15, x17, x18, x29, NZCV ; x30 : sauve par la
;  macro sys). Ecart avec svc 0 : x16 (relais du kit). Le code derriere suit
;  l'AAPCS et n'utilise pas les registres FP/SIMD.
;  f_* : les memes appels en fonctions AAPCS (x0-x18 non gardes).
; ============================================================================

FC_BASE		= 64			; premier descripteur d'AppKit
FC_MAX		= 64			; descripteurs par process (64..127)
FC_SIZE		= 48
FCD_TYPE	= 0			; 0 libre, 1 fichier, 2 repertoire, 3 en ouverture,
					; 4 bout de tube (vfs)
FCD_CANAL	= 8			; service FS
FCD_NUM		= 16			; numero chez lui
FCD_POS		= 24
FCD_TAILLE	= 32
FCD_DRAP	= 40			; O_*
FC_CANAUX	= 32			; canaux des services (SVC_MAX du noyau)
FC_CHEMIN	= 255			; longueur maximale d'un chemin (+ 0 final)
FC_E_MFILE	= -24
FC_E_FAULT	= -14

; --- fichier_sys : x8 = numero, x0-x5 arguments -> x0 ------------------------
fichier_sys:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	sub	sp,sp,144
	stp	x1,x2,[sp]
	stp	x3,x4,[sp,16]
	stp	x5,x6,[sp,32]
	stp	x7,x8,[sp,48]
	stp	x9,x10,[sp,64]
	stp	x11,x12,[sp,80]
	stp	x13,x14,[sp,96]
	stp	x15,x17,[sp,112]
	mrs	x9,nzcv
	stp	x18,x9,[sp,128]
	bl	fc_aiguille
	ldp	x18,x9,[sp,128]
	msr	nzcv,x9
	ldp	x15,x17,[sp,112]
	ldp	x13,x14,[sp,96]
	ldp	x11,x12,[sp,80]
	ldp	x9,x10,[sp,64]
	ldp	x7,x8,[sp,48]
	ldp	x5,x6,[sp,32]
	ldp	x3,x4,[sp,16]
	ldp	x1,x2,[sp]
	add	sp,sp,144
	ldp	x29,x30,[sp],16
	ret

; x8 = numero : vers la fonction (appel terminal : x30 = retour a fichier_sys)
fc_aiguille:
	cmp	x8,SYS_PRINT
	b.eq	f_print
	cmp	x8,SYS_SPAWN
	b.eq	f_spawn
	cmp	x8,SYS_SPAWN_PIPE
	b.eq	f_spawn_pipe
	cmp	x8,SYS_PRELOAD
	b.eq	f_preload
	cmp	x8,SYS_UNLOAD
	b.eq	f_unload
	cmp	x8,SYS_OPEN
	b.eq	f_ouvrir
	cmp	x8,SYS_STAT
	b.eq	f_stat
	cmp	x8,SYS_MKDIR
	b.eq	f_mkdir
	cmp	x8,SYS_UNLINK
	b.eq	f_unlink
	cmp	x8,SYS_CHDIR
	b.eq	f_chdir
	cmp	x8,SYS_GETCWD
	b.eq	f_getcwd
	cmp	x0,3				; entree, sortie, erreur standard
	b.lo	fc_std
	sub	x9,x0,FC_BASE			; un descripteur d'AppKit ?
	cmp	x9,FC_MAX
	b.hs	fc_noyau			; (aucun autre descripteur)
	cmp	x8,SYS_READ
	b.eq	f_lire
	cmp	x8,SYS_WRITE
	b.eq	f_ecrire
	cmp	x8,SYS_SEEK
	b.eq	f_position
	cmp	x8,SYS_CLOSE
	b.eq	f_fermer
	cmp	x8,SYS_READDIR
	b.eq	f_lire_rep
fc_noyau:					; descripteur inconnu (ceux du
	mov	x0,E_BADF			; noyau : partis a l'etape 5)
	ret

; --- verrous (attente : yield) -------------------------------------------------
; x0 = verrou (32 bits) ; ne touche que x0-x3, x8
fc_prendre:
	mov	x2,x0
	mov	w3,1
.t:	ldaxr	w1,[x2]
	cbnz	w1,.att
	stxr	w1,w3,[x2]
	cbnz	w1,.t
	ret
.att:	mov	x8,SYS_YIELD
	svc	0
	b	.t

; x0 = verrou
fc_rendre:
	stlr	wzr,[x0]
	ret

; --- descripteurs ----------------------------------------------------------------
; -> x0 = entree reservee (type 3), x1 = son rang ; x0 = 0 : table pleine
fc_reserver:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,fc_verrou
	bl	fc_prendre
	adr	x19,fc_desc
	mov	x20,0
.c:	ldr	x0,[x19,FCD_TYPE]
	cbz	x0,.libre
	add	x19,x19,FC_SIZE
	add	x20,x20,1
	cmp	x20,FC_MAX
	b.lo	.c
	mov	x19,0
	b	.fin
.libre:	mov	x0,3
	str	x0,[x19,FCD_TYPE]
.fin:	adr	x0,fc_verrou
	bl	fc_rendre
	mov	x0,x19
	mov	x1,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = descripteur -> x0 = son entree (fichier ou repertoire ouvert), 0 sinon
; (ne touche que x0-x2)
fc_desc_de:
	sub	x1,x0,FC_BASE
	cmp	x1,FC_MAX
	b.hs	.non
	adr	x0,fc_desc
	mov	x2,FC_SIZE
	madd	x0,x1,x2,x0
	ldr	x2,[x0,FCD_TYPE]
	cmp	x2,4				; (bout de tube)
	b.eq	.oui
	sub	x2,x2,1
	cmp	x2,1
	b.hi	.non
.oui:	ret
.non:	mov	x0,0
	ret

; x0 = message, x1 = op, x2 = a, x3 = chemin -> x0 = 0, E_INVAL si trop long
; (ne touche que x0-x6)
fc_msg_chemin:
	stp	x1,x2,[x0]
	str	xzr,[x0,MSG_D]
	add	x4,x0,MSG_DATA
	mov	x5,0
.c:	cmp	x5,FC_CHEMIN
	b.hs	.long
	ldrb	w6,[x3,x5]
	strb	w6,[x4,x5]
	add	x5,x5,1
	cbnz	w6,.c
	str	w5,[x0,MSG_LEN]
	mov	x0,0
	ret
.long:	mov	x0,E_INVAL
	ret

; x0 = message rempli : au service vfs -> resultat (E_NOENT sans vfs)
fc_vfs:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,fc_nom_vfs
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.absent
	mov	x1,x19
	sys	SYS_SVC_CALL
	b	.out
.absent:
	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; x0 = op V_*, x1 = chemin : requete de chemin sans reponse a garder
fc_chemin_seul:
	prologue
	sub	sp,sp,MSG_TAILLE
	mov	x3,x1
	mov	x1,x0
	mov	x2,0
	mov	x0,sp
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
.out:	add	sp,sp,MSG_TAILLE
	epilogue

; x0 = destination, x1 = source, x2 = octets (sans recouvrement)
fc_copie:
.p:	cmp	x2,16
	b.lo	.o
	ldp	x3,x4,[x1],16
	stp	x3,x4,[x0],16
	sub	x2,x2,16
	b	.p
.o:	cbz	x2,.f
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.o
.f:	ret

; x0 = canal d'un service FS -> x0 = son tampon partage mappe (0 : aucun),
; x1 = le verrou de ce tampon. Demande au service au premier besoin
; (FS_TAMPON, buf_map) ; un refus est garde (messages de 464 o ensuite).
fc_tampon:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	mov	x0,0
	cmp	x19,FC_CANAUX
	b.hs	.out
	adr	x20,fc_tampons
	ldr	x0,[x20,x19,lsl 3]
	cmn	x0,1
	b.eq	.aucun
	cbnz	x0,.pret
	adr	x0,fc_verrou
	bl	fc_prendre
	ldr	x0,[x20,x19,lsl 3]
	cbnz	x0,.deja
	mov	x0,FS_TAMPON
	str	x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,x19
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.refus
	ldr	x21,[sp,MSG_A]
	mov	x0,x21
	sys	SYS_BUF_MAP
	tbnz	x0,63,.refus
	cbz	x0,.refus
	adr	x1,fc_tnum
	str	x21,[x1,x19,lsl 3]
	str	x0,[x20,x19,lsl 3]
	b	.deja
.refus:	mov	x0,-1
	str	x0,[x20,x19,lsl 3]
.deja:	adr	x0,fc_verrou
	bl	fc_rendre
	ldr	x0,[x20,x19,lsl 3]
	cmn	x0,1
	b.ne	.pret
.aucun:	mov	x0,0
	b	.out
.pret:	adr	x1,fc_tverrous
	add	x1,x1,x19,lsl 2
.out:	add	sp,sp,MSG_TAILLE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = canal : son tampon n'est plus reconnu (service relance) : demappe,
; redemande au prochain gros transfert
fc_tampon_oublier:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cmp	x19,FC_CANAUX
	b.hs	.out
	adr	x0,fc_verrou
	bl	fc_prendre
	adr	x20,fc_tampons
	ldr	x0,[x20,x19,lsl 3]
	cbz	x0,.rien
	cmn	x0,1
	b.eq	.rien
	str	xzr,[x20,x19,lsl 3]
	adr	x0,fc_tnum
	ldr	x0,[x0,x19,lsl 3]
	sys	SYS_BUF_FREE
.rien:	adr	x0,fc_verrou
	bl	fc_rendre
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 12 open(chemin, drapeaux) -> descripteur ----------------------------------
f_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	mov	x20,x1
	bl	fc_reserver
	cbz	x0,.mfile
	mov	x21,x0
	mov	x22,x1
	mov	x0,sp
	mov	x1,V_OUVRIR
	mov	x2,x20
	mov	x3,x19
	bl	fc_msg_chemin
	cbnz	x0,.libere
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.libere
	ldp	x0,x1,[sp,MSG_A]		; canal, numero
	stp	x0,x1,[x21,FCD_CANAL]
	ldp	x0,x1,[sp,MSG_C]		; attributs, taille
	str	x1,[x21,FCD_TAILLE]
	str	x20,[x21,FCD_DRAP]
	mov	x2,0				; O_APPEND : a la fin
	tst	x20,O_APPEND
	csel	x2,x1,x2,ne
	str	x2,[x21,FCD_POS]
	mov	x2,1
	mov	x3,2
	tst	x0,ATTR_DIR
	csel	x2,x3,x2,ne
	str	x2,[x21,FCD_TYPE]		; (pret)
	add	x0,x22,FC_BASE
	b	.out
.libere:
	str	xzr,[x21,FCD_TYPE]
	b	.out
.mfile:	mov	x0,FC_E_MFILE
.out:	add	sp,sp,MSG_TAILLE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 13 read(fd, tampon, n) / 14 write(fd, tampon, n) -> octets ---------------
; Comme le relais du noyau : une lecture s'arrete au premier morceau court
; (fin du fichier), une ecriture va jusqu'au bout ; une erreur apres des
; octets rend ces octets.
f_lire:
	mov	x3,0
	b	fc_rw
f_ecrire:
	mov	x3,1
fc_rw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	str	x27,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	fc_desc_de
	cbz	x0,.badf
	mov	x19,x0
	ldr	x0,[x19,FCD_TYPE]
	cmp	x0,2
	b.eq	.isdir
	ldr	x0,[x19,FCD_DRAP]		; droits d'acces
	mov	x1,O_READ
	cmp	x22,0
	mov	x2,O_WRITE
	csel	x1,x2,x1,ne
	tst	x0,x1
	b.eq	.badf
	mov	x23,0				; transferes
	ldr	x0,[x19,FCD_TYPE]
	cmp	x0,4
	b.eq	.p_morceau			; un tube : a vfs
	cmp	x21,FS_DONNEES
	b.ls	.m_morceau
	cbnz	x22,.tampon			; grosse lecture : directement ici (le
	movl	x0,FS_DIRECT_MIN		; service mappe cette memoire) ; sur
	cmp	x21,x0				; un refus, le tampon partage
	b.lo	.tampon
	tst	x20,3
	b.ne	.tampon
.d_morceau:
	sub	x26,x21,x23			; reste
	cbz	x26,.d_fin
	movl	x0,FS_DIRECT_MAX
	cmp	x26,x0
	csel	x26,x0,x26,hi
	mov	x0,FS_LIRE_D
	ldr	x1,[x19,FCD_NUM]
	stp	x0,x1,[sp]
	ldr	x0,[x19,FCD_POS]
	stp	x0,x26,[sp,MSG_B]
	add	x0,x20,x23
	str	x0,[sp,MSG_D]
	str	wzr,[sp,MSG_LEN]
	ldr	x0,[x19,FCD_CANAL]
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.tampon
	cbz	x0,.d_fin			; fin du fichier
	cmp	x0,x26
	csel	x27,x26,x0,hi
	ldr	x0,[x19,FCD_POS]
	add	x0,x0,x27
	str	x0,[x19,FCD_POS]
	add	x23,x23,x27
	cmp	x27,x26
	b.hs	.d_morceau
.d_fin:	mov	x0,x23
	b	.out
.tampon:
	ldr	x0,[x19,FCD_CANAL]		; gros transfert : le tampon partage
	bl	fc_tampon
	cbz	x0,.m_morceau
	mov	x24,x0
	mov	x25,x1
	mov	x0,x25
	bl	fc_prendre
.t_morceau:
	sub	x26,x21,x23			; reste
	cbz	x26,.t_fin
	mov	x0,FS_TAMPON_TAILLE
	cmp	x26,x0
	csel	x26,x0,x26,hi
	cbz	x22,.t_op
	mov	x0,x24				; donnees a ecrire -> tampon
	add	x1,x20,x23
	mov	x2,x26
	bl	fc_copie
.t_op:	mov	x0,FS_LIRE_T
	cbz	x22,.t_o2
	mov	x0,FS_ECRIRE_T
.t_o2:	ldr	x1,[x19,FCD_NUM]
	stp	x0,x1,[sp]
	ldr	x0,[x19,FCD_POS]
	stp	x0,x26,[sp,MSG_B]
	str	wzr,[sp,MSG_LEN]
	ldr	x0,[x19,FCD_CANAL]
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.t_err
	cbz	x0,.t_fin			; fin du fichier
	cmp	x0,x26				; (pas plus que demande)
	csel	x27,x26,x0,hi
	cbnz	x22,.t_ecrit
	add	x0,x20,x23			; lu -> programme
	mov	x1,x24
	mov	x2,x27
	bl	fc_copie
	b	.t_av
.t_ecrit:
	ldr	x0,[sp,MSG_B]			; nouvelle taille
	str	x0,[x19,FCD_TAILLE]
.t_av:	ldr	x0,[x19,FCD_POS]
	add	x0,x0,x27
	str	x0,[x19,FCD_POS]
	add	x23,x23,x27
	cmp	x27,x26
	b.hs	.t_morceau
	cbnz	x22,.t_morceau			; ecriture : tout
.t_fin:	mov	x0,x25
	bl	fc_rendre
	mov	x0,x23
	b	.out
.t_err:	mov	x26,x0
	mov	x0,x25
	bl	fc_rendre
	cbnz	x23,.partiel
	cmn	x26,22				; E_INVAL : tampon inconnu du service
	b.ne	.t_errr				; (relance) : par les messages
	ldr	x0,[x19,FCD_CANAL]
	bl	fc_tampon_oublier
	b	.m_morceau
.t_errr:
	mov	x0,x26
	b	.out
.partiel:
	mov	x0,x23
	b	.out
.m_morceau:					; par messages de 464 o
	sub	x26,x21,x23
	cbz	x26,.m_fin
	cmp	x26,FS_DONNEES
	mov	x0,FS_DONNEES
	csel	x26,x0,x26,hi
	cbz	x22,.m_op
	add	x0,sp,MSG_DATA
	add	x1,x20,x23
	mov	x2,x26
	bl	fc_copie
.m_op:	mov	x0,FS_LIRE
	cbz	x22,.m_o2
	mov	x0,FS_ECRIRE
.m_o2:	ldr	x1,[x19,FCD_NUM]
	stp	x0,x1,[sp]
	ldr	x0,[x19,FCD_POS]
	stp	x0,x26,[sp,MSG_B]
	cmp	x22,0
	csel	x0,x26,xzr,ne
	str	w0,[sp,MSG_LEN]
	ldr	x0,[x19,FCD_CANAL]
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.m_err
	cbz	x0,.m_fin
	cmp	x0,x26
	csel	x27,x26,x0,hi
	cbnz	x22,.m_ecrit
	add	x0,x20,x23
	add	x1,sp,MSG_DATA
	mov	x2,x27
	bl	fc_copie
	b	.m_av
.m_ecrit:
	ldr	x0,[sp,MSG_B]
	str	x0,[x19,FCD_TAILLE]
.m_av:	ldr	x0,[x19,FCD_POS]
	add	x0,x0,x27
	str	x0,[x19,FCD_POS]
	add	x23,x23,x27
	cmp	x27,FS_DONNEES
	b.hs	.m_morceau
	cbnz	x22,.m_morceau
.m_fin:	mov	x0,x23
	b	.out
.m_err:	cmp	x23,0
	csel	x0,x23,x0,ne
	b	.out
.p_morceau:					; tube : lecture, un message
	cbnz	x22,.p_ecr
	cbz	x21,.p_fin
.p_lire:
	mov	x0,V_PIPE_READ
	ldr	x1,[x19,FCD_NUM]
	stp	x0,x1,[sp]
	mov	x0,FS_DONNEES
	cmp	x21,x0
	csel	x0,x21,x0,lo
	str	x0,[sp,MSG_B]
	ldr	x1,[x19,FCD_DRAP]
	tst	x1,O_NONBLOCK
	cset	x1,ne
	str	x1,[sp,MSG_C]
	str	wzr,[sp,MSG_LEN]
	ldr	x0,[x19,FCD_CANAL]
	mov	x1,sp
	sys	SYS_SVC_CALL
	cmn	x0,11				; E_AGAIN : vide (ou attente echue)
	b.ne	.p_lu
	ldr	x1,[x19,FCD_DRAP]
	tst	x1,O_NONBLOCK
	b.ne	.out
	b	.p_lire				; bloquant : on redemande
.p_lu:	cmp	x0,0
	b.le	.out
	cmp	x0,x21
	csel	x27,x21,x0,hi
	mov	x0,x20
	add	x1,sp,MSG_DATA
	mov	x2,x27
	bl	fc_copie
	mov	x0,x27
	b	.out
.p_ecr:	sub	x26,x21,x23			; ecriture : tout
	cbz	x26,.p_fin
	cmp	x26,FS_DONNEES
	mov	x0,FS_DONNEES
	csel	x26,x0,x26,hi
	add	x0,sp,MSG_DATA
	add	x1,x20,x23
	mov	x2,x26
	bl	fc_copie
	mov	x0,V_PIPE_WRITE
	ldr	x1,[x19,FCD_NUM]
	stp	x0,x1,[sp]
	str	w26,[sp,MSG_LEN]
	ldr	x0,[x19,FCD_CANAL]
	mov	x1,sp
	sys	SYS_SVC_CALL
	tbnz	x0,63,.m_err
	cbnz	x0,.p_av
	ldr	x1,[x19,FCD_DRAP]		; plein
	tst	x1,O_NONBLOCK
	mov	x0,E_AGAIN
	b.ne	.m_err
	mov	x0,2				; laisser le lecteur vider
	sys	SYS_SLEEP
	b	.p_ecr
.p_av:	add	x23,x23,x0
	b	.p_ecr
.p_fin:	mov	x0,x23
	b	.out
.isdir:	mov	x0,E_ISDIR
	b	.out
.badf:	mov	x0,E_BADF
.out:	add	sp,sp,MSG_TAILLE
	ldr	x27,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 15 seek(fd, position, origine 0/1/2) -> nouvelle position ----------------
f_position:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	bl	fc_desc_de
	cbz	x0,.badf
	ldr	x1,[x0,FCD_TYPE]		; (pas dans un tube)
	cmp	x1,4
	b.eq	.inval
	ldr	x1,[x0,FCD_POS]
	ldr	x2,[x0,FCD_TAILLE]
	cmp	x20,1
	csel	x3,x1,xzr,eq
	cmp	x20,2
	csel	x3,x2,x3,eq
	adds	x3,x3,x19
	b.mi	.inval
	str	x3,[x0,FCD_POS]
	mov	x0,x3
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 16 close(fd) ------------------------------------------------------------------
f_fermer:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	adr	x1,fc_stdio			; plus une entree standard
	mov	x2,0
.s:	ldr	x3,[x1,x2,lsl 3]
	cmp	x3,x0
	b.ne	.sn
	str	xzr,[x1,x2,lsl 3]
.sn:	add	x2,x2,1
	cmp	x2,3
	b.lo	.s
	bl	fc_desc_de
	cbz	x0,.badf
	mov	x19,x0
	ldr	x2,[x19,FCD_TYPE]
	mov	x0,FS_FERMER			; (un appel : reponse 0)
	mov	x3,V_PIPE_CLOSE			; (tube : a vfs)
	cmp	x2,4
	csel	x0,x3,x0,eq
	ldr	x1,[x19,FCD_NUM]
	stp	x0,x1,[sp]
	str	wzr,[sp,MSG_LEN]
	ldr	x0,[x19,FCD_CANAL]
	mov	x1,sp
	sys	SYS_SVC_CALL
	str	xzr,[x19,FCD_TYPE]
	mov	x0,0
	b	.out
.badf:	mov	x0,E_BADF
.out:	add	sp,sp,MSG_TAILLE
	ldr	x19,[sp],16
	epilogue

; --- 17 stat(chemin, tampon{taille, attributs}) ---------------------------------
f_stat:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x1
	cbz	x19,.fault
	mov	x3,x0
	mov	x0,sp
	mov	x1,V_STAT
	mov	x2,0
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldp	x1,x2,[sp,MSG_B]		; taille, attributs
	stp	x1,x2,[x19]
	b	.out
.fault:	mov	x0,FC_E_FAULT
.out:	add	sp,sp,MSG_TAILLE
	ldr	x19,[sp],16
	epilogue

; --- 18 readdir(fd, tampon{nom[256], taille, attributs}) -> 1, 0 en fin ---------
f_lire_rep:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x20,x1
	bl	fc_desc_de
	cbz	x0,.badf
	mov	x19,x0
	ldr	x0,[x19,FCD_TYPE]
	cmp	x0,2
	b.ne	.notdir
	mov	x0,FS_LIRE_REP
	ldr	x1,[x19,FCD_NUM]
	stp	x0,x1,[sp]
	ldr	x0,[x19,FCD_POS]
	str	x0,[sp,MSG_B]
	str	wzr,[sp,MSG_LEN]
	ldr	x0,[x19,FCD_CANAL]
	mov	x1,sp
	sys	SYS_SVC_CALL
	cmp	x0,1
	b.ne	.out
	mov	x0,x20
	add	x1,sp,MSG_DATA
	mov	x2,DIRENT_BYTES
	bl	fc_copie
	ldr	x0,[x19,FCD_POS]
	add	x0,x0,1
	str	x0,[x19,FCD_POS]
	mov	x0,1
	b	.out
.notdir:
	mov	x0,E_NOTDIR
	b	.out
.badf:	mov	x0,E_BADF
.out:	add	sp,sp,MSG_TAILLE
	ldp	x19,x20,[sp],16
	epilogue

; --- 19 mkdir(chemin) / 20 unlink(chemin) -----------------------------------------
f_mkdir:
	mov	x1,x0
	mov	x0,V_MKDIR
	b	fc_chemin_seul
f_unlink:
	mov	x1,x0
	mov	x0,V_UNLINK
	b	fc_chemin_seul

; --- 21 chdir(chemin) : vfs (dossier courant par pid) -----------------------------
f_chdir:
	mov	x1,x0
	mov	x0,V_CHDIR
	b	fc_chemin_seul

; --- 22 getcwd(tampon, taille >= 256) ----------------------------------------------
f_getcwd:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	cmp	x1,256
	b.lo	.inval
	cbz	x19,.fault
	mov	x0,V_GETCWD
	str	x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldr	w2,[sp,MSG_LEN]
	cmp	x2,256
	mov	x0,256
	csel	x2,x0,x2,hi
	cbz	x2,.vide
	mov	x0,x19
	add	x1,sp,MSG_DATA
	bl	fc_copie
	strb	wzr,[x19,255]
	mov	x0,0
	b	.out
.vide:	strb	wzr,[x19]
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,FC_E_FAULT
.out:	add	sp,sp,MSG_TAILLE
	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------------
;  Etape 4 : entrees / sorties standard, tubes de vfs, lancement des programmes
; ---------------------------------------------------------------------------------
FC_IMAGE_MAX	= 0x1000000		; executable : un tampon partage (16 Mo au plus)

; Entrees / sorties standard, au premier besoin : proc_flags (noyau) dit
; lesquelles sont des tubes ; leurs poignees viennent de vfs (V_STDIO) et
; deviennent des descripteurs de type 4 (fc_stdio : 0 = console). Un process
; sans tube (vfs, les services...) n'envoie aucune requete.
fc_stdio_init:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,MSG_TAILLE+32		; (+ poignees, fichiers)
	adr	x0,fc_stdio_ok
	ldar	w0,[x0]
	cbnz	w0,.out
	adr	x0,fc_sverrou
	bl	fc_prendre
	adr	x0,fc_stdio_ok
	ldr	w0,[x0]
	cbnz	w0,.rend
	mov	x8,SYS_PROC_FLAGS
	svc	0
	tbnz	x0,63,.fait			; (noyau sans l'appel 98)
	ands	x0,x0,7
	b.eq	.fait				; tout a la console
	mov	x0,V_STDIO
	str	x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.fait
	add	x2,sp,MSG_TAILLE			; (stp : pas si loin de sp)
	ldp	x0,x1,[sp,MSG_A]
	stp	x0,x1,[x2]
	ldp	x0,x1,[sp,MSG_C]
	stp	x0,x1,[x2,16]
	mov	x19,0
.i:	add	x0,sp,MSG_TAILLE
	ldr	x0,[x0,x19,lsl 3]
	cbz	x0,.n
	ldr	x1,[sp,MSG_TAILLE+24]		; un fichier (redirection) : ouvert ici
	lsr	x1,x1,x19
	tbz	x1,0,.tube
	mov	x1,V_END_FILE
	stp	x1,x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	fc_vfs
	cmp	x0,1
	b.ne	.n
	add	x0,sp,MSG_DATA
	adr	x1,fc_devnull
	bl	str_ends
	mov	x2,x0
	mov	x0,-1				; (/dev/null : ni lecture ni ecriture)
	cbnz	x2,.place
	ldr	x1,[sp,MSG_B]
	cmp	x1,1
	mov	x1,O_READ
	mov	x2,O_WRITE or O_APPEND
	csel	x1,x1,x2,eq
	add	x0,sp,MSG_DATA
	bl	f_ouvrir
	cmn	x0,1
	b.eq	.n
	b	.place
.tube:	cmp	x19,0				; entree : lecture, sinon ecriture
	mov	x1,O_WRITE
	mov	x2,O_READ
	csel	x1,x2,x1,eq
	bl	fc_pipe_desc
.place:	cmn	x0,1
	b.eq	.pose
	tbnz	x0,63,.n
.pose:
	adr	x1,fc_stdio
	str	x0,[x1,x19,lsl 3]
.n:	add	x19,x19,1
	cmp	x19,3
	b.lo	.i
.fait:	mov	w0,1
	adr	x1,fc_stdio_ok
	stlr	w0,[x1]
.rend:	adr	x0,fc_sverrou
	bl	fc_rendre
.out:	add	sp,sp,MSG_TAILLE+32
	ldp	x19,x20,[sp],16
	epilogue

fc_devnull	db '/dev/null',0
		align 4

; --- f_redir(chemin, O_READ ou O_WRITE) -> descripteur a donner a f_spawn_stdio :
; le fils ouvrira ce fichier lui-meme, en ajout pour l'ecriture (docs/FICHIERS.md)
f_redir:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x1
	mov	x3,x0
	mov	x0,sp
	mov	x1,V_FILE_END
	mov	x2,2
	tst	x19,O_WRITE
	mov	x4,1
	csel	x2,x2,x4,ne
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldr	x0,[sp,MSG_A]
	mov	x1,x19
	bl	fc_pipe_desc
.out:	add	sp,sp,MSG_TAILLE
	ldr	x19,[sp],16
	epilogue

; x0 = poignee d'un bout de tube (vfs), x1 = drapeaux (O_READ / O_WRITE,
; O_NONBLOCK) -> x0 = descripteur (type 4) ou erreur (le bout reste ouvert)
fc_pipe_desc:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,fc_nom_vfs
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x21,x0
	bl	fc_reserver
	cbz	x0,.mfile
	stp	x21,x19,[x0,FCD_CANAL]
	stp	xzr,xzr,[x0,FCD_POS]
	str	x20,[x0,FCD_DRAP]
	mov	x2,4
	str	x2,[x0,FCD_TYPE]		; (pret)
	add	x0,x1,FC_BASE
	b	.out
.mfile:	mov	x0,FC_E_MFILE
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = poignee d'un bout de tube : fermee chez vfs (sans descripteur)
fc_end_close:
	prologue
	sub	sp,sp,MSG_TAILLE
	mov	x1,x0
	mov	x0,V_PIPE_CLOSE
	stp	x0,x1,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	fc_vfs
	add	sp,sp,MSG_TAILLE
	epilogue

; x0 = canal d'un service FS, x1 = numero : fichier ferme (sans descripteur)
fc_fs_close:
	prologue
	sub	sp,sp,MSG_TAILLE
	mov	x2,FS_FERMER
	stp	x2,x1,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x1,sp
	sys	SYS_SVC_CALL
	add	sp,sp,MSG_TAILLE
	epilogue

; (fichier_sys) x0 = 0, 1, 2 : read / write / close sur l'entree, la sortie
; ou l'erreur standard ; x8 = appel, x1, x2 = tampon, octets
fc_std:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x8
	bl	fc_stdio_init
	adr	x0,fc_stdio
	ldr	x0,[x0,x19,lsl 3]
	cbz	x0,.console
	cmn	x0,1
	b.eq	.nul
	mov	x1,x20
	mov	x2,x21
	cmp	x22,SYS_READ
	b.eq	.lire
	cmp	x22,SYS_WRITE
	b.eq	.ecrire
	cmp	x22,SYS_CLOSE
	b.eq	.fermer
	mov	x0,E_INVAL
	b	.out
.lire:	bl	f_lire
	b	.out
.ecrire:
	bl	f_ecrire
	b	.out
.fermer:
	bl	f_fermer
	b	.out
.console:
	cmp	x22,SYS_WRITE
	b.ne	.c2
	cbz	x19,.badf			; (l'entree ne s'ecrit pas)
	mov	x0,x20
	mov	x1,x21
	bl	fc_console
	tbnz	x0,63,.out
	mov	x0,x21
	b	.out
.nul:	cmp	x22,SYS_WRITE			; /dev/null : tout est accepte, rien a lire
	csel	x0,x21,xzr,eq
	b	.out
.c2:	mov	x0,0				; lire la console : fin ; fermer : 0
	cmp	x22,SYS_READ
	b.eq	.out
	cmp	x22,SYS_CLOSE
	b.eq	.out
	mov	x0,E_INVAL
	b	.out
.badf:	mov	x0,E_BADF
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = tampon, x1 = octets : a la console du noyau (print, morceaux de 4096)
; -> 0 ou erreur
fc_console:
	mov	x2,x0
	mov	x3,x1
.m:	mov	x0,0
	cbz	x3,.f
	mov	x0,x2
	mov	x1,4096
	cmp	x3,x1
	csel	x1,x3,x1,lo
	add	x2,x2,x1
	sub	x3,x3,x1
	mov	x8,SYS_PRINT
	svc	0
	tbz	x0,63,.m
.f:	ret

; --- 1 print(tampon, octets) : sur la sortie standard -> 0 ---------------------------
f_print:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	fc_stdio_init
	adr	x0,fc_stdio
	ldr	x0,[x0,8]
	cbz	x0,.console
	cmn	x0,1
	b.eq	.rien
	mov	x1,x19
	mov	x2,x20
	bl	f_ecrire			; (lecteur parti : sortie perdue)
.rien:
	mov	x0,0
	b	.out
.console:
	mov	x0,x19				; comme le noyau : <= 4096 o
	mov	x1,x20
	mov	x8,SYS_PRINT
	svc	0
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- f_pipe(&fds[2]) -> 0 : un tube neuf, fds[0] lecture, fds[1] ecriture
; (descripteurs de 8 o ; lecture bloquante)
f_pipe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	mov	x0,V_PIPE
	str	x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldp	x20,x21,[sp,MSG_A]
	mov	x0,x20
	mov	x1,O_READ
	bl	fc_pipe_desc
	tbnz	x0,63,.err2
	mov	x22,x0
	mov	x0,x21
	mov	x1,O_WRITE
	bl	fc_pipe_desc
	tbnz	x0,63,.err1
	stp	x22,x0,[x19]
	mov	x0,0
	b	.out
.err1:	mov	x20,x0
	mov	x0,x22				; (ferme aussi le bout de lecture)
	bl	f_fermer
	mov	x0,x21
	bl	fc_end_close
	mov	x0,x20
	b	.out
.err2:	mov	x22,x0
	mov	x0,x20
	bl	fc_end_close
	mov	x0,x21
	bl	fc_end_close
	mov	x0,x22
.out:	add	sp,sp,MSG_TAILLE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- f_spawn_file(reponse, arguments, stdio) -> pid ---------------------------
; x0 = reponse de V_EXEC (a canal, b numero, c attributs, d taille ;
; donnees : nom du process, 0, chemin resolu, 0), x1 = arguments (0 : aucun),
; x2 = entree, sortie, erreur du fils (3 x 8 o, poignees de vfs : 0 = la
; sienne, -1 = aucune ; x2 = 0 : toutes heritees). Tout lancement passe par
; l'image de ce chemin et de cette taille (spawn_preloaded : rien a lire si
; elle existe) ; sinon le fichier est lu directement dans les pages d'une
; image neuve (fc_image_load : IMAGE_CREATE, lecture, IMAGE_SEAL), puis
; spawn_preloaded. Sans images (option no_preload, table pleine, ancien
; noyau) : le fichier lu entier dans un tampon partage temporaire (16 Mo au
; plus), puis spawn_image (droits demandes si ATTR_SYS). Un fichier qui
; commence par MZ est une DLL .NET : /dotnet/dotnet la lance (fc_spawn_dotnet).
; Sert aussi a vfs (resolution locale, sans requete a lui-meme).
f_spawn_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,64			; params (48 o), image
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	fc_reserver			; descripteur temporaire
	cbz	x0,.mfile
	mov	x22,x0
	add	x23,x1,FC_BASE
	ldp	x0,x1,[x19,MSG_A]
	stp	x0,x1,[x22,FCD_CANAL]
	ldr	x24,[x19,MSG_D]			; taille
	str	x24,[x22,FCD_TAILLE]
	str	xzr,[x22,FCD_POS]
	mov	x0,O_READ
	str	x0,[x22,FCD_DRAP]
	mov	x0,1
	str	x0,[x22,FCD_TYPE]		; (pret)
	mov	x25,E_INVAL
	cbz	x24,.ferme
	add	x0,x19,MSG_DATA			; params : nom, arguments, options
	stp	x0,x20,[sp]
	ldr	x0,[x19,MSG_C]
	tst	x0,ATTR_SYS
	cset	x0,ne				; droits demandes (le noyau verifie
	str	x0,[sp,16]			; que l'appelant les a)
	stp	xzr,xzr,[sp,24]
	str	xzr,[sp,40]
	cbz	x21,.pre
	ldp	x0,x1,[x21]
	stp	x0,x1,[sp,24]
	ldr	x0,[x21,16]
	str	x0,[sp,40]
.pre:	add	x0,x19,MSG_DATA			; l'image de ce programme ?
.pn:	ldrb	w1,[x0],1			; (chemin resolu : apres le nom)
	cbnz	w1,.pn
	mov	x26,x0
	mov	x1,x24
	mov	x2,sp
	mov	x8,SYS_SPAWN_PRELOADED
	svc	0
	mov	x25,x0
	cmn	x0,2				; E_NOENT : pas d'image, on la construit
	b.eq	.magic
	cmn	x0,38				; (E_NOSYS : noyau d'avant : on lit)
	b.ne	.ferme
.magic:	strh	wzr,[sp,56]			; ses deux premiers octets
	mov	x0,x23
	add	x1,sp,56
	mov	x2,2
	bl	f_lire
	str	xzr,[x22,FCD_POS]
	ldrh	w0,[sp,56]
	movl	w1,'M' or ('Z' shl 8)
	cmp	w0,w1
	b.ne	.elf
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	fc_spawn_dotnet
	mov	x25,x0
	b	.ferme
.elf:	cmn	x25,38
	b.eq	.lire
.image:	mov	x0,x26				; le fichier lu dans les pages de
	mov	x1,x24				; l'image (IMAGE_CREATE, IMAGE_SEAL)
	mov	x2,x23
	mov	x3,0
	bl	fc_image_load
	mov	x25,x0
	cmn	x0,38				; E_NOSYS : pas d'images (option
	b.eq	.lire				; no_preload) : par un tampon
	cmn	x0,28				; E_NOSPC : table des images pleine
	b.eq	.lire
	cbnz	x0,.ferme
	mov	x0,x26
	mov	x1,x24
	mov	x2,sp
	mov	x8,SYS_SPAWN_PRELOADED
	svc	0
	mov	x25,x0
	b	.ferme
.lire:	mov	x25,E_INVAL
	movl	x0,FC_IMAGE_MAX
	cmp	x24,x0
	b.hi	.ferme
	mov	x0,x24				; l'image : un tampon partage, rendu apres
	sys	SYS_BUF_CREATE
	mov	x25,x0
	tbnz	x0,63,.ferme
	mov	x26,x0
	movl	x0,BUF_VA_BASE
	add	x0,x0,x26,lsl 26		; (BUF_SLOT = 64 Mo)
	str	x0,[sp,48]
	mov	x1,x0
	mov	x0,x23
	mov	x2,x24
	bl	f_lire
	mov	x25,x0
	mov	x0,x23
	bl	f_fermer
	cmp	x25,x24
	b.eq	.lu
	tbnz	x25,63,.rend
	mov	x25,E_INVAL			; (fichier plus court)
	b	.rend
.lu:	ldr	x0,[sp,48]			; (params faits plus haut)
	mov	x1,x24
	mov	x2,sp
	mov	x8,SYS_SPAWN_IMAGE
	svc	0
	mov	x25,x0
.rend:	mov	x0,x26
	sys	SYS_BUF_FREE
	mov	x0,x25
	b	.out
.ferme:	mov	x0,x23
	bl	f_fermer
	mov	x0,x25
	b	.out
.mfile:	ldp	x0,x1,[x19,MSG_A]
	bl	fc_fs_close
	mov	x0,FC_E_MFILE
.out:	add	sp,sp,64
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = reponse de V_EXEC d'une DLL .NET, x1 = arguments, x2 = stdio -> pid :
; /dotnet/dotnet sous le nom du programme. Sa ligne : 1, le chemin de la DLL, 1,
; puis les arguments ; libposix en fait argv[1] (env.c), que les arguments
; viennent de la ligne ou de l'objet de spawn_env. Un fichier <dll>.skia a cote
; de la DLL demande l'hote qui contient Skia, /dotnet/dotnet-skia (docs/AVALONIA.md).
fc_spawn_dotnet:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,(MSG_TAILLE+LIGNE_MAX+15) and not 15
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	add	x2,x19,MSG_DATA
.n:	ldrb	w1,[x2],1			; le chemin, apres le nom
	cbnz	w1,.n
	add	x0,sp,MSG_TAILLE
	add	x3,x0,LIGNE_MAX-2		; limite
	mov	w1,1
	strb	w1,[x0],1
.c:	ldrb	w1,[x2],1
	cbz	w1,.cf
	cmp	x0,x3
	b.hs	.long
	strb	w1,[x0],1
	b	.c
.cf:	mov	w1,1
	strb	w1,[x0],1
	cbz	x20,.fl
	mov	w1,' '
	strb	w1,[x0],1
.a:	ldrb	w1,[x20],1
	cbz	w1,.fl
	cmp	x0,x3
	b.hs	.long
	strb	w1,[x0],1
	b	.a
.fl:	strb	wzr,[x0]
	adr	x22,fc_p_dotnet			; l'hote : <dll>.skia existe-t-il ?
	add	x3,x19,MSG_DATA
.m:	ldrb	w1,[x3],1
	cbnz	w1,.m
	mov	x0,sp
	mov	x1,V_STAT
	mov	x2,0
	bl	fc_msg_chemin
	cbnz	x0,.host
	ldr	w1,[sp,MSG_LEN]
	cmp	w1,FC_CHEMIN-6
	b.hs	.host
	add	x0,sp,MSG_DATA
	add	x0,x0,x1
	sub	x0,x0,1
	adr	x2,fc_e_skia
.e:	ldrb	w3,[x2],1
	strb	w3,[x0],1
	cbnz	w3,.e
	add	w1,w1,5
	str	w1,[sp,MSG_LEN]
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.host
	adr	x22,fc_p_skia
.host:	mov	x0,sp
	mov	x1,V_EXEC
	mov	x2,0
	mov	x3,x22
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	add	x0,sp,MSG_DATA			; ses donnees : le nom du programme,
	add	x1,x19,MSG_DATA			; puis son propre chemin
.d1:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.d1
	mov	x1,x22
.d2:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.d2
	mov	x0,sp
	add	x1,sp,MSG_TAILLE
	mov	x2,x21
	bl	f_spawn_file
	b	.out
.long:	mov	x0,E_INVAL
.out:	add	sp,sp,(MSG_TAILLE+LIGNE_MAX+15) and not 15
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

fc_p_dotnet	db 'SD0:/dotnet/dotnet',0
fc_p_skia	db 'SD0:/dotnet/dotnet-skia',0
fc_e_skia	db '.skia',0
		align 4

; x0 = chemin, x1 = arguments, x2 = stdio (poignees, comme f_spawn_file)
; -> x0 = pid, x1 = 1 s'il est detache : V_EXEC, puis f_spawn_file ; un
; programme de SD0:/sys (ATTR_SYS) demande par un process sans droits :
; lance par init2 (INIT_OP_LAUNCH, chemin resolu par vfs ; le fils a alors
; les entrees / sorties - le journal - et le dossier d'init2 : detache)
fc_spawn:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x0,sp
	mov	x1,V_EXEC
	mov	x2,0
	mov	x3,x19
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldr	x0,[sp,MSG_C]
	tst	x0,ATTR_SYS
	b.eq	.direct
	mov	x8,SYS_PROC_FLAGS
	svc	0
	tbnz	x0,63,.direct
	tst	x0,PROC_RIGHTS
	b.ne	.direct
	ldp	x0,x1,[sp,MSG_A]		; sans droits : par init2
	bl	fc_fs_close
	mov	x0,sp
	mov	x1,x20
	bl	fc_launch_init2
	mov	x1,1				; (detache)
	b	.ret
.direct:
	mov	x0,sp
	mov	x1,x20
	mov	x2,x21
	bl	f_spawn_file
.out:	mov	x1,0
.ret:	add	sp,sp,MSG_TAILLE
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = reponse de V_EXEC (donnees : nom, 0, chemin resolu, 0), x1 = arguments
; -> pid : init2 lance le programme (INIT_OP_LAUNCH ; donnees : chemin, 0,
; arguments, 0 ; il n'accepte que SD0:/sys et les droits viennent de vfs)
fc_launch_init2:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	add	x0,x19,MSG_DATA			; le chemin, apres le nom
.n:	ldrb	w1,[x0],1
	cbnz	w1,.n
	add	x1,x19,MSG_DATA			; -> debut des donnees
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	strb	w3,[x1,x2]
	add	x2,x2,1
	cbnz	w3,.c
	add	x1,x1,x2			; puis les arguments
	mov	x3,x20
	cbnz	x3,.a
	adr	x3,fc_vide
.a:	ldrb	w4,[x3],1
	add	x5,x19,MSG_TAILLE
	cmp	x1,x5
	b.hs	.long
	strb	w4,[x1],1
	cbnz	w4,.a
	sub	x1,x1,x19
	sub	x1,x1,MSG_DATA
	str	w1,[x19,MSG_LEN]
	mov	x0,INIT_OP_LAUNCH
	str	x0,[x19]
	adr	x0,fc_nom_init2
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.perm
	mov	x1,x19
	sys	SYS_SVC_CALL
	b	.out
.perm:	mov	x0,-1				; E_PERM (pas d'init2)
	b	.out
.long:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_PRELOAD preload(chemin) -> 0 (init2 : SD0:/etc/preload.txt ; la
; commande preload) -------------------------------------------------------------
; Le chemin est resolu comme pour spawn (V_EXEC de vfs) et le fichier lu dans
; une image epinglee (fc_image_load, IMG_PIN), cle des lancements suivants
; (f_spawn_file). chemin = 0 : 0 si le noyau a les images (E_NOSYS : option
; no_preload ; droits de SD0:/sys).
f_preload:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	cbnz	x19,.chemin
	mov	x1,0				; question : actif ?
	mov	x2,0
	mov	x8,SYS_IMAGE_PRELOAD
	svc	0
	b	.out
.chemin:
	mov	x0,sp
	mov	x1,V_EXEC
	mov	x2,0
	mov	x3,x19
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldp	x20,x21,[sp,MSG_A]		; canal, numero
	ldr	x24,[sp,MSG_D]			; taille
	mov	x25,E_INVAL
	cmp	x24,64
	b.lo	.ferme
	bl	fc_reserver			; descripteur temporaire
	mov	x25,FC_E_MFILE
	cbz	x0,.ferme
	mov	x22,x0
	add	x23,x1,FC_BASE
	stp	x20,x21,[x22,FCD_CANAL]
	str	x24,[x22,FCD_TAILLE]
	str	xzr,[x22,FCD_POS]
	mov	x0,O_READ
	str	x0,[x22,FCD_DRAP]
	mov	x0,1
	str	x0,[x22,FCD_TYPE]		; (pret)
	add	x0,sp,MSG_DATA			; chemin resolu : apres le nom
.pn:	ldrb	w1,[x0],1
	cbnz	w1,.pn
	mov	x1,x24
	mov	x2,x23
	mov	x3,IMG_PIN
	bl	fc_image_load
	mov	x25,x0
	mov	x0,x23				; (ferme aussi le fichier)
	bl	f_fermer
	mov	x0,x25
	b	.out
.ferme:	ldp	x0,x1,[sp,MSG_A]
	bl	fc_fs_close
	mov	x0,x25
.out:	add	sp,sp,MSG_TAILLE
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin resolu, x1 = taille du fichier, x2 = descripteur ouvert en
; lecture (a sa position 0), x3 = drapeaux IMG_* -> x0 = 0 : l'image de ce
; programme est construite (IMAGE_CREATE : le noyau mappe ses pages ici, le
; fichier y est lu directement, IMAGE_SEAL : verifiee, mise en place) ;
; E_NOSYS : pas d'images (option no_preload) ; sinon l'erreur
fc_image_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x2,x3
	mov	x1,x19
	mov	x8,SYS_IMAGE_CREATE
	svc	0
	tbnz	x0,63,.out
	mov	x21,x0				; la fenetre
	mov	x0,x20
	mov	x1,x21
	mov	x2,x19
	bl	f_lire
	mov	x22,x0
	cmp	x22,x19
	b.eq	.seal
	mov	x0,x21				; lecture courte ou echouee : rendue
	mov	x1,IMS_ABORT
	mov	x8,SYS_IMAGE_SEAL
	svc	0
	mov	x0,E_INVAL
	tbz	x22,63,.out
	mov	x0,x22
	b	.out
.seal:	mov	x0,x21
	mov	x1,0
	mov	x8,SYS_IMAGE_SEAL
	svc	0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_UNLOAD unload(chemin) -> nombre ----------------------------------------
; le chemin resolu (V_EXEC de vfs), ses images oubliees (IMAGE_FORGET) : le
; prochain lancement relit le fichier
f_unload:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	mov	x0,sp
	mov	x1,V_EXEC
	mov	x2,0
	mov	x3,x19
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldp	x0,x1,[sp,MSG_A]
	bl	fc_fs_close
	add	x0,sp,MSG_DATA			; chemin resolu : apres le nom
.pn:	ldrb	w1,[x0],1
	cbnz	w1,.pn
	mov	x8,SYS_IMAGE_FORGET
	svc	0
.out:	add	sp,sp,MSG_TAILLE
	ldr	x19,[sp],16
	epilogue

; --- 23 spawn(chemin, arguments) -> pid : entrees / sorties du pere ------------
f_spawn:
	mov	x2,0
	b	fc_spawn

; --- f_spawn_stdio(chemin, arguments, &fds[3]) -> pid : fds = descripteurs
; (8 o) de l'entree, la sortie, l'erreur du fils : un tube (type 4), 0-2 :
; l'entree / sortie standard correspondante du pere, < 0 : la console ;
; fds = 0 : comme spawn
f_spawn_stdio:
	cbz	x2,f_spawn
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	fc_stdio_init
	mov	x22,0
.i:	ldr	x0,[x21,x22,lsl 3]
	mov	x1,-1
	tbnz	x0,63,.mis			; console
	cmp	x0,3
	b.hs	.tube
	adr	x1,fc_stdio			; une entree standard du pere
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,-1
	cbz	x0,.mis
	mov	x1,0				; (/dev/null : 0, celle du pere)
	cmn	x0,1
	b.eq	.mis
.tube:	bl	fc_desc_de
	cbz	x0,.badf
	ldr	x1,[x0,FCD_TYPE]
	cmp	x1,4
	b.eq	.bout
	cmp	x1,1				; (redirigee vers un fichier : 0, celle
	mov	x1,0				; du pere, que vfs connait)
	b.eq	.mis
	b	.badf
.bout:	ldr	x1,[x0,FCD_NUM]
.mis:	str	x1,[sp,x22,lsl 3]
	add	x22,x22,1
	cmp	x22,3
	b.lo	.i
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	bl	fc_spawn
	b	.out
.badf:	mov	x0,E_BADF
.out:	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 26 spawn_pipe(chemin, arguments, &fd) -> pid : sortie et erreur du fils
; dans un tube neuf ; *fd = son bout de lecture (O_NONBLOCK : E_AGAIN si vide,
; 0 a la fin, comme le tube du noyau d'avant). Programme detache (lance par
; init2 : SD0:/sys sans droits) : *fd = -1, pas de tube (sortie au journal,
; ne pas l'attendre)
f_spawn_pipe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbz	x21,.fault
	mov	x0,V_PIPE
	str	x0,[sp]
	str	wzr,[sp,MSG_LEN]
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldp	x22,x23,[sp,MSG_A]		; lecture, ecriture
	mov	x0,x22
	mov	x1,O_READ or O_NONBLOCK
	bl	fc_pipe_desc
	tbnz	x0,63,.err
	mov	x24,x0
	mov	x0,0				; entree : la sienne ; sortie, erreur :
	stp	x0,x23,[sp]			; le tube
	str	x23,[sp,16]
	mov	x0,x19
	mov	x1,x20
	mov	x2,sp
	bl	fc_spawn
	mov	x19,x0
	mov	x22,x1
	mov	x0,x23				; le pere n'ecrit pas : fin du tube
	bl	fc_end_close			; quand le fils s'arrete
	tbnz	x19,63,.echec
	cbnz	x22,.detache
	str	x24,[x21]
	mov	x0,x19
	b	.out
.detache:
	mov	x0,-1				; detache : pas de tube
	str	x0,[x21]
	mov	x0,x24
	bl	f_fermer
	mov	x0,x19
	b	.out
.echec:	mov	x0,x24
	bl	f_fermer
	mov	x0,x19
	b	.out
.err:	mov	x19,x0
	mov	x0,x22
	bl	fc_end_close
	mov	x0,x23
	bl	fc_end_close
	mov	x0,x19
	b	.out
.fault:	mov	x0,FC_E_FAULT
.out:	add	sp,sp,MSG_TAILLE
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- f_kit_load(nom) -> 0 ou erreur : le kit SD0:/lib/<nom>.so est lu par vfs
; et inscrit par le noyau (kit_register, droits de SD0:/sys : le fichier doit
; etre marque ATTR_LIB par vfs) ; sans droits : init2 le fait
; (INIT_OP_KIT_LOAD, donnees : le nom). Image dans un tampon partage
; temporaire, rendu tout de suite apres (rien ne reste chez l'appelant).
; Ensuite : lib_open (kits_open le fait).
FC_KIT_NOM	= 23
f_kit_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,MSG_TAILLE+64		; message, chemin
	mov	x19,x0
	mov	x1,0				; nom : 1-23 caracteres a-z 0-9 _
.v:	ldrb	w2,[x19,x1]
	cbz	w2,.vf
	cmp	x1,FC_KIT_NOM
	b.hs	.inval
	cmp	w2,'_'
	b.eq	.vok
	sub	w3,w2,'a'
	cmp	w3,25
	b.ls	.vok
	sub	w3,w2,'0'
	cmp	w3,9
	b.hi	.inval
.vok:	add	x1,x1,1
	b	.v
.vf:	cbz	x1,.inval
	mov	x8,SYS_PROC_FLAGS		; sans droits : par init2
	svc	0
	tbnz	x0,63,.init2
	tst	x0,PROC_RIGHTS
	b.eq	.init2
	add	x0,sp,MSG_TAILLE		; « SD0:/lib/<nom>.so »
	adr	x1,fc_lib_dir
	bl	fc_cat
	mov	x1,x19
	bl	fc_cat
	adr	x1,fc_lib_ext
	bl	fc_cat
	mov	x0,sp
	mov	x1,V_OUVRIR
	mov	x2,O_READ
	add	x3,sp,MSG_TAILLE
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,sp
	bl	fc_vfs
	tbnz	x0,63,.out
	ldp	x20,x21,[sp,MSG_A]		; canal, numero
	ldp	x0,x24,[sp,MSG_C]		; attributs, taille
	mov	x25,-1				; E_PERM : pas un kit du systeme
	tst	x0,ATTR_DIR
	b.ne	.ferme
	tst	x0,ATTR_LIB
	b.eq	.ferme
	mov	x25,E_INVAL
	cmp	x24,64
	b.lo	.ferme
	movl	x0,FC_IMAGE_MAX
	cmp	x24,x0
	b.hi	.ferme
	bl	fc_reserver			; descripteur temporaire
	mov	x25,FC_E_MFILE
	cbz	x0,.ferme
	mov	x22,x0
	add	x23,x1,FC_BASE
	stp	x20,x21,[x22,FCD_CANAL]
	str	x24,[x22,FCD_TAILLE]
	str	xzr,[x22,FCD_POS]
	mov	x0,O_READ
	str	x0,[x22,FCD_DRAP]
	mov	x0,1
	str	x0,[x22,FCD_TYPE]		; (pret)
	mov	x0,x24				; l'image : un tampon partage, rendu apres
	sys	SYS_BUF_CREATE
	mov	x25,x0
	tbnz	x0,63,.desc
	mov	x26,x0
	movl	x20,BUF_VA_BASE
	add	x20,x20,x26,lsl 26		; (BUF_SLOT = 64 Mo)
	mov	x0,x23
	mov	x1,x20
	mov	x2,x24
	bl	f_lire
	mov	x25,x0
	mov	x0,x23
	bl	f_fermer
	cmp	x25,x24
	b.eq	.lu
	tbnz	x25,63,.rend
	mov	x25,E_INVAL			; (fichier plus court)
	b	.rend
.lu:	mov	x0,x19
	mov	x1,x20
	mov	x2,x24
	mov	x8,SYS_KIT_REGISTER
	svc	0
	mov	x25,x0
	tbnz	x0,63,.rend
	mov	x25,0
.rend:	mov	x0,x26				; tampon rendu (demappe)
	sys	SYS_BUF_FREE
	mov	x0,x25
	b	.out
.desc:	mov	x0,x23
	bl	f_fermer
	mov	x0,x25
	b	.out
.ferme:	mov	x0,x20
	mov	x1,x21
	bl	fc_fs_close
	mov	x0,x25
	b	.out
.init2:	mov	x0,INIT_OP_KIT_LOAD		; donnees : le nom
	str	x0,[sp]
	add	x0,sp,MSG_DATA
	mov	x1,x19
	bl	fc_cat
	mov	x1,sp
	sub	x0,x0,x1
	sub	x0,x0,MSG_DATA-1
	str	w0,[sp,MSG_LEN]
	adr	x0,fc_nom_init2
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.perm
	mov	x1,sp
	sys	SYS_SVC_CALL
	b	.out
.perm:	mov	x0,-1				; E_PERM (pas d'init2)
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,MSG_TAILLE+64
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fin d'une chaine, x1 = chaine a ajouter -> x0 = nouvelle fin (0 ecrit)
fc_cat:
	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.f
	add	x0,x0,1
	b	fc_cat
.f:	ret

fc_lib_dir	db 'SD0:/lib/',0
fc_lib_ext	db '.so',0
fc_nom_init2	db 'init2',0
fc_vide		db 0
fc_nom_vfs	db 'vfs',0
		align 4

; variables du client (segment inscriptible du kit : une copie par process)
macro fileclient_data {
		align 8
fc_verrou	dw 0, 0				; allocation des descripteurs, tampons
fc_desc		rb FC_MAX * FC_SIZE
fc_tampons	rd FC_CANAUX			; adresse mappee (0 : pas encore, -1 : refus)
fc_tnum		rd FC_CANAUX			; numero du tampon (buf_free)
fc_tverrous	rb FC_CANAUX * 4		; un transfert a la fois par tampon
		align 8
fc_stdio	rd 3				; entree, sortie, erreur : descripteur (0 : console)
fc_stdio_ok	dw 0				; (fc_stdio rempli)
fc_sverrou	dw 0
}

include 'spawnenv.inc'
