; exec.inc - AppKit : exec (SYS_EXEC, src/kernel/exec.asm, docs/ABI.md 0x6B)
;
;  * f_exec(chemin, argv, envp, &fds[3]) : le programme du chemin (resolu par
;    vfs, V_EXEC, comme spawn) remplace celui du process, meme pid. argv et
;    l'environnement : un objet partage marque BLOB_TAG (comme f_spawn_env),
;    argv[1..] en ligne d'arguments. fds : comme f_spawn_stdio (0 : entrees
;    / sorties inchangees). Ne ferme rien.
;  * f_exec_file(reponse de V_EXEC, params EXP_* 72 o) : pour libposix, qui
;    remplit la ligne, les entrees / sorties et ses marques ; AppKit y met le
;    nom, son etat et l'image, comme f_spawn_file (image du chemin, sinon
;    construite : IMAGE_CREATE, lecture, IMAGE_SEAL ; sans images : l'ELF lu
;    dans un tampon partage). Le fichier est ferme avant SYS_EXEC (rouvert par
;    V_EXEC s'il faut le lire). Programme de SD0:/sys (ATTR_SYS) : E_PERM
;    (exec n'accorde pas les droits ; libposix lance alors le programme et
;    l'attend). DLL .NET (MZ) : E_NOEXEC (de meme).
;  * Etat d'AppKit : les descripteurs 64..127 ouverts (fichiers, repertoires,
;    bouts de tubes ; pas ceux des entrees / sorties standard, que le nouveau
;    programme redemande a vfs) vont dans un objet partage marque
;    EXEC_FC_TAG | 1. Le nouveau programme les reprend aux memes numeros, au
;    premier usage d'un descripteur (fc_exec_vu : fc_reserver, fc_desc_de,
;    f_fork), si proc_flags dit PROC_EXEC ; l'objet est ensuite ferme. Les
;    services les tiennent toujours (meme pid) ; la position suit (FCD_POS,
;    ou gardee par le service apres un fork). Les tampons d'echange sont
;    redemandes (le service rend le meme a ce pid).

FCX_MAGIC	= 0x58454346		; (FCEX)
E_NOEXEC	= -8
E_PERM_FC	= -1
FCX_N		= 4			; descripteurs (32 bits)
FCX_ENT		= 8			; {rang (8 o), entree (FC_SIZE)}
FCX_ENT_SIZE	= 8 + FC_SIZE

; Reprise : une fois par process (0 : a voir, 1 : en cours, 2 : fait) ; garde
; tous les registres (x0-x18, x30, drapeaux)
f_exec_reprise:					; (exportee : libposix, au demarrage)
fc_exec_reprise:
	stp	x29,x30,[sp,-16]!
	mov	x29,sp
	sub	sp,sp,160
	stp	x0,x1,[sp]
	stp	x2,x3,[sp,16]
	stp	x4,x5,[sp,32]
	stp	x6,x7,[sp,48]
	stp	x8,x9,[sp,64]
	stp	x10,x11,[sp,80]
	stp	x12,x13,[sp,96]
	stp	x14,x15,[sp,112]
	stp	x16,x17,[sp,128]
	mrs	x9,nzcv
	stp	x18,x9,[sp,144]
	adr	x9,fc_exec_etat
.cas:	ldaxr	w10,[x9]
	cbnz	w10,.autre
	mov	w11,1
	stxr	w12,w11,[x9]
	cbnz	w12,.cas
	mov	x8,SYS_PROC_FLAGS		; lance par exec ?
	svc	0
	tbnz	x0,63,.fait
	tst	x0,PROC_EXEC
	b.eq	.fait
	sub	sp,sp,16*256			; ses poignees : l'objet d'AppKit
	mov	x0,sp
	mov	x1,256
	mov	x2,0				; (marques gardees : libposix les lit)
	mov	x8,SYS_HANDLE_LIST
	svc	0
	mov	x1,sp
	mov	x13,0
.e:	cmp	x0,0
	b.le	.lu
	ldr	w2,[x1,4]
	lsr	w3,w2,30			; (EXEC_FC_TAG : bits 31-30 = 01)
	cmp	w3,EXEC_FC_TAG shr 30
	b.eq	.got
	add	x1,x1,16
	sub	x0,x0,1
	b	.e
.got:	ldr	w13,[x1]			; poignee
.lu:	add	sp,sp,16*256
	cbz	x13,.fait
	mov	x0,x13
	mov	x1,0
	mov	x2,4096
	mov	x3,PROT_READ
	mov	x4,0
	mov	x5,0
	mov	x8,SYS_SHM_MAP
	svc	0
	cmn	x0,4096
	b.hs	.ferme
	mov	x14,x0
	ldr	w1,[x14]
	movl	w2,FCX_MAGIC
	cmp	w1,w2
	b.ne	.demappe
	ldr	w15,[x14,FCX_N]
	cmp	w15,FC_MAX
	b.hi	.demappe
	add	x10,x14,FCX_ENT
.d:	cbz	w15,.demappe
	ldr	x11,[x10]			; rang
	cmp	x11,FC_MAX
	b.hs	.dn
	adr	x0,fc_desc
	mov	x2,FC_SIZE
	madd	x0,x11,x2,x0
	add	x1,x10,8
	mov	x2,FC_SIZE
	bl	fc_copie
.dn:	add	x10,x10,FCX_ENT_SIZE
	sub	w15,w15,1
	b	.d
.demappe:
	mov	x0,x14
	mov	x1,4096
	mov	x8,SYS_VM_UNMAP
	svc	0
.ferme:	mov	x0,x13
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
.fait:	mov	w0,2
	adr	x1,fc_exec_etat
	stlr	w0,[x1]
	b	.out
.autre:	clrex
	cmp	w10,2
	b.eq	.out
	mov	x8,SYS_YIELD			; un autre thread la fait
	svc	0
	b	.cas
.out:	ldp	x18,x9,[sp,144]
	msr	nzcv,x9
	ldp	x16,x17,[sp,128]
	ldp	x14,x15,[sp,112]
	ldp	x12,x13,[sp,96]
	ldp	x10,x11,[sp,80]
	ldp	x8,x9,[sp,64]
	ldp	x6,x7,[sp,48]
	ldp	x4,x5,[sp,32]
	ldp	x2,x3,[sp,16]
	ldp	x0,x1,[sp]
	add	sp,sp,160
	ldp	x29,x30,[sp],16
	ret

; -> x0 = poignee de l'objet de l'etat d'AppKit (rempli), < 0 erreur
fc_exec_etat_faire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x0,4096
	mov	x1,0
	mov	x8,SYS_SHM_CREATE
	svc	0
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x1,0
	mov	x2,4096
	mov	x3,PROT_READ or PROT_WRITE
	mov	x4,0
	mov	x5,0
	mov	x8,SYS_SHM_MAP
	svc	0
	cmn	x0,4096
	b.hs	.err
	mov	x20,x0
	movl	w1,FCX_MAGIC
	str	w1,[x20]
	adr	x0,fc_verrou
	bl	fc_prendre
	add	x21,x20,FCX_ENT
	mov	x22,0				; rang
.d:	adr	x0,fc_desc
	mov	x1,FC_SIZE
	madd	x1,x22,x1,x0
	ldr	x2,[x1,FCD_TYPE]
	cmp	x2,3				; ouverts : 1, 2, 4
	b.eq	.dn
	cbz	x2,.dn
	add	x2,x22,FC_BASE			; pas une entree standard
	adr	x3,fc_stdio
	ldp	x4,x5,[x3]
	ldr	x6,[x3,16]
	cmp	x4,x2
	b.eq	.dn
	cmp	x5,x2
	b.eq	.dn
	cmp	x6,x2
	b.eq	.dn
	str	x22,[x21]
	add	x0,x21,8
	mov	x2,FC_SIZE
	bl	fc_copie
	add	x21,x21,FCX_ENT_SIZE
	ldr	w0,[x20,FCX_N]
	add	w0,w0,1
	str	w0,[x20,FCX_N]
.dn:	add	x22,x22,1
	cmp	x22,FC_MAX
	b.lo	.d
	adr	x0,fc_verrou
	bl	fc_rendre
	mov	x0,x20
	mov	x1,4096
	mov	x8,SYS_VM_UNMAP
	svc	0
	mov	x0,x19
	b	.out
.err:	mov	x20,x0
	mov	x0,x19
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- f_exec_file(reponse, params) -> erreur (ne revient pas sinon) --------------
; x0 = reponse de V_EXEC (a canal, b numero, c attributs, d taille ; donnees :
; nom du process, 0, chemin resolu, 0 ; le fichier est ferme ici), x1 = params
; EXP_* (72 o, src/inc/syscalls.inc) : EXP_ARGS, EXP_STDIN.., EXP_MARQUES /
; EXP_NMARQUES de l'appelant ; EXP_NOM = 0 : le nom de la reponse.
; Rendus intacts en cas d'erreur.
EXF_MARQ	= 0			; marques : celles de l'appelant + la sienne
EXF_MSG		= (IPC_INHERIT_MAX + 1) * 8	; reponse d'un second V_EXEC
EXF_NOM		= EXF_MSG + MSG_TAILLE		; 32 o
EXF_SAUVE	= EXF_NOM + 32			; EXP_NOM, EXP_MARQUES, EXP_NMARQUES, EXP_IMAGE
EXF_SIZE	= EXF_SAUVE + 32
f_exec_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,EXF_SIZE
	mov	x19,x0
	mov	x20,x1
	fc_exec_vu				; (son etat repris s'il vient d'un exec)
	ldr	x0,[x20,EXP_NOM]		; parametres de l'appelant : gardes
	ldr	x1,[x20,EXP_MARQUES]
	add	x2,sp,EXF_SAUVE
	stp	x0,x1,[x2]
	ldr	x0,[x20,EXP_NMARQUES]
	ldr	x1,[x20,EXP_IMAGE]
	stp	x0,x1,[x2,16]
	ldp	x21,x22,[x19,MSG_A]		; fichier : canal, numero
	ldr	x23,[x19,MSG_D]			; taille
	ldr	x0,[x19,MSG_C]
	mov	x26,E_PERM_FC			; SD0:/sys : exec n'accorde pas les droits
	tst	x0,ATTR_SYS
	b.ne	.ferme_f
	mov	x26,E_INVAL
	cmp	x23,64
	b.lo	.ferme_f
	add	x0,sp,EXF_NOM			; le nom (la reponse peut servir encore)
	add	x1,x19,MSG_DATA
	mov	x2,31
.nm:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	cbz	w3,.nmf
	subs	x2,x2,1
	b.ne	.nm
	strb	wzr,[x0]
.nmf:	add	x0,x19,MSG_DATA			; chemin resolu (apres le nom) : dans
	add	x1,sp,EXF_MSG+MSG_DATA		; le message de travail
.pn:	ldrb	w2,[x0],1
	cbnz	w2,.pn
.pc:	ldrb	w2,[x0],1
	strb	w2,[x1],1
	cbnz	w2,.pc
	add	x24,sp,EXF_MSG+MSG_DATA		; (x24 : le chemin resolu)
	ldr	x0,[x20,EXP_NOM]
	cbnz	x0,.nomok
	add	x0,sp,EXF_NOM
	str	x0,[x20,EXP_NOM]
.nomok:	bl	fc_exec_etat_faire		; son etat
	mov	x26,x0
	tbnz	x0,63,.ferme_f
	mov	x25,x0				; (poignee de l'etat)
	ldr	x2,[x20,EXP_NMARQUES]		; marques : les siennes, puis l'etat
	mov	x26,E_INVAL
	cmp	x2,IPC_INHERIT_MAX
	b.hs	.rend_e
	ldr	x1,[x20,EXP_MARQUES]
	add	x0,sp,EXF_MARQ
	lsl	x2,x2,3
	cbz	x2,.m0
	bl	fc_copie
.m0:	ldr	x2,[x20,EXP_NMARQUES]
	add	x0,sp,EXF_MARQ
	add	x0,x0,x2,lsl 3
	movl	w1,EXEC_FC_TAG or 1
	stp	w25,w1,[x0]
	add	x2,x2,1
	str	x2,[x20,EXP_NMARQUES]
	add	x0,sp,EXF_MARQ
	str	x0,[x20,EXP_MARQUES]
	str	xzr,[x20,EXP_IMAGE]
	mov	x0,x21				; le fichier : ferme (l'image suffit)
	mov	x1,x22
	bl	fc_fs_close
	mov	x0,x24				; l'image de ce programme ?
	mov	x1,x23
	mov	x2,x20
	mov	x8,SYS_EXEC
	svc	0
	mov	x26,x0
	cmn	x0,2				; E_NOENT : pas d'image, on la construit
	b.ne	.rend_e
	mov	x0,V_EXEC			; le fichier, rouvert
	str	x0,[sp,EXF_MSG]
	str	xzr,[sp,EXF_MSG+MSG_A]
	str	xzr,[sp,EXF_MSG+MSG_D]
	mov	x0,x24
	mov	x1,0
.l:	ldrb	w2,[x0,x1]
	add	x1,x1,1
	cbnz	w2,.l
	str	w1,[sp,EXF_MSG+MSG_LEN]
	add	x0,sp,EXF_MSG
	bl	fc_vfs
	mov	x26,x0
	tbnz	x0,63,.rend_e
	add	x0,sp,EXF_MSG			; (son chemin resolu, a nouveau : apres
	add	x1,x0,MSG_DATA			;  le nom)
.pn2:	ldrb	w2,[x1],1
	cbnz	w2,.pn2
	mov	x24,x1
	add	x0,sp,EXF_MSG
	ldp	x21,x22,[x0,MSG_A]
	ldr	x23,[x0,MSG_D]
	mov	x0,x21
	mov	x1,x22
	mov	x2,x23
	bl	fc_exec_lire			; image construite, ou ELF en tampon
	mov	x26,x0
	tbnz	x0,63,.rend_e
	mov	x25,x0				; (x25 libere : poignee dans la liste)
	cmp	x0,1
	b.lo	.img
	movl	x1,BUF_VA_BASE			; un tampon : l'ELF de lui
	sub	x2,x0,1
	add	x1,x1,x2,lsl 26
	str	x1,[x20,EXP_IMAGE]
.img:	mov	x0,x24
	mov	x1,x23
	mov	x2,x20
	mov	x8,SYS_EXEC
	svc	0
	mov	x26,x0
	cbz	x25,.rend_e2
	sub	x0,x25,1			; (echec : tampon rendu)
	mov	x8,SYS_BUF_FREE
	svc	0
.rend_e2:
	ldr	x0,[x20,EXP_NMARQUES]		; l'etat : sa poignee est la derniere
	add	x1,sp,EXF_MARQ
	sub	x0,x0,1
	add	x1,x1,x0,lsl 3
	ldr	w25,[x1]
.rend_e:
	mov	x0,x25				; echec : l'objet de l'etat rendu
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
	b	.restaure
.ferme_f:
	mov	x0,x21
	mov	x1,x22
	bl	fc_fs_close
.restaure:
	add	x2,sp,EXF_SAUVE
	ldp	x0,x1,[x2]
	str	x0,[x20,EXP_NOM]
	str	x1,[x20,EXP_MARQUES]
	ldp	x0,x1,[x2,16]
	str	x0,[x20,EXP_NMARQUES]
	str	x1,[x20,EXP_IMAGE]
	mov	x0,x26
	add	sp,sp,EXF_SIZE
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = canal, x1 = numero du fichier (ouvert par V_EXEC ; ferme ici), x2 =
; taille, x24 de l'appelant = chemin resolu -> x0 = 0 : image construite ;
; n + 1 : l'ELF dans le tampon partage n (a rendre) ; < 0 : erreur (E_NOEXEC
; pour une DLL .NET)
fc_exec_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	fc_reserver			; descripteur temporaire
	cbz	x0,.mfile
	mov	x22,x0
	add	x23,x1,FC_BASE
	stp	x19,x20,[x22,FCD_CANAL]
	str	x21,[x22,FCD_TAILLE]
	str	xzr,[x22,FCD_POS]
	mov	x0,O_READ
	str	x0,[x22,FCD_DRAP]
	mov	x0,1
	str	x0,[x22,FCD_TYPE]		; (pret)
	strh	wzr,[sp]			; ses deux premiers octets
	mov	x0,x23
	mov	x1,sp
	mov	x2,2
	bl	f_lire
	str	xzr,[x22,FCD_POS]
	ldrh	w0,[sp]
	movl	w1,'M' or ('Z' shl 8)
	cmp	w0,w1
	mov	x19,E_NOEXEC
	b.eq	.ferme
	mov	x0,x24				; le fichier dans les pages de l'image
	mov	x1,x21
	mov	x2,x23
	mov	x3,0
	bl	fc_image_load
	mov	x19,x0
	cmn	x0,22				; E_INVAL (IMAGE_SEAL) : pas un ELF
	b.ne	.pas_inval
	mov	x19,E_NOEXEC
	b	.ferme
.pas_inval:
	cmn	x0,38				; E_NOSYS : pas d'images (no_preload)
	b.eq	.tampon
	cmn	x0,28				; E_NOSPC : table des images pleine
	b.ne	.ferme
.tampon:
	mov	x19,E_INVAL
	movl	x0,FC_IMAGE_MAX
	cmp	x21,x0
	b.hi	.ferme
	mov	x0,x21
	sys	SYS_BUF_CREATE
	mov	x19,x0
	tbnz	x0,63,.ferme
	mov	x24,x0
	movl	x1,BUF_VA_BASE
	add	x1,x1,x24,lsl 26		; (BUF_SLOT = 64 Mo)
	mov	x0,x23
	mov	x2,x21
	bl	f_lire
	cmp	x0,x21
	add	x19,x24,1
	b.eq	.ferme
	mov	x19,x0
	tbnz	x0,63,.rend
	mov	x19,E_INVAL			; (fichier plus court)
.rend:	mov	x0,x24
	sys	SYS_BUF_FREE
.ferme:	mov	x0,x23
	bl	f_fermer
	mov	x0,x19
	b	.out
.mfile:	mov	x0,x19
	mov	x1,x20
	bl	fc_fs_close
	mov	x0,FC_E_MFILE
.out:	add	sp,sp,16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- f_exec(chemin, argv, envp, &fds[3]) -> erreur (ne revient pas sinon) -------
; argv[0] compris ; envp = 0 : aucun environnement ; fds : comme f_spawn_stdio
; (0 : entrees / sorties inchangees)
EXE_PARAMS	= 0			; EXP_SIZE
EXE_MARQ	= 80			; {objet, marque}
EXE_MSG		= 96			; reponse de V_EXEC
EXE_LIGNE	= EXE_MSG + MSG_TAILLE
EXE_SIZE	= EXE_LIGNE + LIGNE_MAX
f_exec:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,EXE_SIZE
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,sp				; params a zero
	mov	x1,0
.z:	str	xzr,[x0,x1]
	add	x1,x1,8
	cmp	x1,EXP_SIZE
	b.lo	.z
	cbz	x22,.stdok			; entrees / sorties : poignees
	bl	fc_stdio_init
	mov	x23,0
.i:	ldr	x0,[x22,x23,lsl 3]		; (comme f_spawn_stdio)
	mov	x1,-1
	tbnz	x0,63,.mis
	tst	x0,VT_TAG
	b.ne	.vt
	cmp	x0,3
	b.hs	.tube
	adr	x1,fc_stdio
	ldr	x0,[x1,x0,lsl 3]
	mov	x1,-1
	cbz	x0,.mis
	mov	x1,0
	cmn	x0,1
	b.eq	.mis
	tst	x0,VT_TAG
	b.eq	.tube
.vt:	mov	x1,x0
	b	.mis
.tube:	bl	fc_desc_de
	cbz	x0,.badf
	ldr	x1,[x0,FCD_TYPE]
	cmp	x1,4
	b.eq	.bout
	cmp	x1,1
	mov	x1,0
	b.eq	.mis
	b	.badf
.bout:	ldr	x1,[x0,FCD_NUM]
.mis:	add	x0,sp,EXP_STDIN
	str	x1,[x0,x23,lsl 3]
	add	x23,x23,1
	cmp	x23,3
	b.lo	.i
.stdok:	mov	x0,EXE_MSG			; le chemin (vfs : V_EXEC)
	add	x0,sp,x0
	mov	x1,V_EXEC
	mov	x2,0
	mov	x3,x19
	bl	fc_msg_chemin
	cbnz	x0,.out
	mov	x0,EXE_MSG
	add	x0,sp,x0
	bl	fc_vfs
	tbnz	x0,63,.out
	mov	x0,x20				; argv et environnement : un objet
	bl	env_compter
	mov	x23,x0				; argc
	mov	x25,x1
	mov	x0,x21
	bl	env_compter
	mov	x24,x0				; envc
	add	x25,x25,x1
	add	x25,x25,BLOB_CHAINES
	add	x25,x25,0xFFF
	and	x25,x25,not 0xFFF		; octets
	mov	x0,x25
	mov	x1,0
	mov	x8,SYS_SHM_CREATE
	svc	0
	tbnz	x0,63,.ferme_f
	mov	x26,x0				; poignee
	mov	x1,0
	mov	x2,x25
	mov	x3,PROT_READ or PROT_WRITE
	mov	x4,0
	mov	x5,0
	mov	x8,SYS_SHM_MAP
	svc	0
	cmn	x0,4096
	b.hs	.ferme_o
	mov	x4,x0
	movl	w1,BLOB_MAGIC
	str	w1,[x4]
	stp	w23,w24,[x4,4]
	str	w25,[x4,12]
	str	x4,[sp,EXE_MARQ]
	mov	x0,x20
	mov	x1,x23
	add	x2,x4,BLOB_CHAINES
	bl	env_copier
	mov	x2,x0
	mov	x0,x21
	mov	x1,x24
	bl	env_copier
	ldr	x0,[sp,EXE_MARQ]
	mov	x1,x25
	mov	x8,SYS_VM_UNMAP
	svc	0
	lsr	x1,x25,12			; { poignee, marque }
	movl	w2,BLOB_TAG
	orr	w1,w1,w2
	stp	w26,w1,[sp,EXE_MARQ]
	add	x0,sp,EXE_MARQ
	str	x0,[sp,EXP_MARQUES]
	mov	x0,1
	str	x0,[sp,EXP_NMARQUES]
	mov	x0,x20				; argv[1..] : la ligne
	mov	x1,EXE_LIGNE
	add	x1,sp,x1
	bl	env_ligne
	cbz	x0,.lance			; (trop longue : vide)
	mov	x1,EXE_LIGNE
	add	x1,sp,x1
	ldrb	w0,[x1]
	cbz	w0,.lance
	str	x1,[sp,EXP_ARGS]
.lance:	mov	x0,EXE_MSG
	add	x0,sp,x0
	mov	x1,sp
	bl	f_exec_file			; (ferme le fichier)
	mov	x19,x0
	mov	x0,x26				; echec : l'objet rendu
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
	mov	x0,x19
	b	.out
.ferme_o:
	mov	x19,x0
	mov	x0,x26
	mov	x8,SYS_HANDLE_CLOSE
	svc	0
	b	.ferme_e
.ferme_f:
	mov	x19,x0
.ferme_e:
	ldr	x0,[sp,EXE_MSG+MSG_A]
	ldr	x1,[sp,EXE_MSG+MSG_B]
	bl	fc_fs_close
	mov	x0,x19
	b	.out
.badf:	mov	x0,E_BADF
.out:	add	sp,sp,EXE_SIZE
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
