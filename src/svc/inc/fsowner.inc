; fsowner.inc - services de fichiers (fatfs, ramfs) : proprietaire d'un
; fichier ouvert et tampons partages par process (docs/FICHIERS.md, 4.2)
;  * proprietaire : le pid de l'appelant (MSG_PID). vfs transmet les
;    ouvertures des programmes (SVC_FORWARD) : MSG_PID est alors le programme
;    lui-meme, d = 0. Seule V_EXEC passe encore par un appel de vfs, avec le
;    pid du programme en d ;
;  * ouverture transmise par vfs : c = attributs a ajouter (ATTR_SYS,
;    ATTR_LIB, d'apres le chemin) ; la reponse porte en a le canal du service
;    (open_extras_take, open_extras_put) ;
;  * acces (ops 10-16, fermer) : le proprietaire ou le noyau (pid 0 : avis,
;    SVC_PID_NOYAU) ; sinon E_BADF ;
;  * tampon (ops 14-16) : un par process client, FS_TAMPON_TAILLE octets,
;    cree par le service (mappe chez lui) et accorde a ce pid ; rendu a
;    l'avis de fin du process (SVC_OP_FIN).
; Donnees : fsowner_data dans le segment de donnees du service, qui definit
; canal (son numero de service).

TP_MAX	= 48				; tampons (process clients a la fois : au-dela,
					; le client passe par des messages de 464 o,
					; secteur par secteur, ~1,6 Mo/s)
PL_MAX	= 64				; entrees de proclist (MAX_PROCS)
PL_SIZE	= 64

; x0 = message d'ouverture -> x0 = proprietaire du fichier : MSG_PID ; d (pid
; pour qui vfs ouvre, V_EXEC) si l'appelant est vfs (son pid est appris a la
; premiere ouverture qu'il fait : SYS_PROCLIST)
; (ne touche que x0-x7, x9)
proprietaire:
	ldr	w4,[x0,MSG_PID]
	ldr	x9,[x0,MSG_D]			; (pid pour qui vfs ouvre)
	cbz	x9,.pid
	adr	x2,pid_vfs
	ldr	x3,[x2]
	cmp	x4,x3
	b.eq	.vfs
	movl	x1,SVC_PID_NOYAU
	cmp	x4,x1
	b.eq	.pid
	adr	x0,proclist			; vfs ? (nom du process)
	mov	x1,PL_MAX
	sys	SYS_PROCLIST
	adr	x5,proclist
.p:	cmp	x0,0
	b.le	.pid
	ldr	x6,[x5]
	cmp	x6,x4
	b.eq	.nom
	add	x5,x5,PL_SIZE
	sub	x0,x0,1
	b	.p
.nom:	ldr	w6,[x5,24]			; « vfs », 0
	movl	w7,0x00736676
	cmp	w6,w7
	b.ne	.pid
	adr	x2,pid_vfs
	str	x4,[x2]
.vfs:	mov	x0,x9
	ret
.pid:	mov	x0,x4
	ret

; x0 = message d'ouverture (au debut de l'op) : les attributs que vfs demande
; d'ajouter (c : ATTR_SYS, ATTR_LIB seulement) (ne touche que x0, x1)
open_extras_take:
	ldr	x0,[x0,MSG_C]
	and	x0,x0,ATTR_SYS or ATTR_LIB
	adr	x1,open_extras
	str	x0,[x1]
	ret

; x0 = message d'une ouverture reussie (c : attributs deja ecrits) : c recoit
; les attributs de vfs, a le canal du service (ne touche que x0-x2)
open_extras_put:
	adr	x1,open_extras
	ldr	x1,[x1]
	ldr	x2,[x0,MSG_C]
	orr	x2,x2,x1
	str	x2,[x0,MSG_C]
	adr	x1,canal
	ldr	w1,[x1]
	str	x1,[x0,MSG_A]
	ret

; x0 = message, x1 = proprietaire du fichier -> x0 = 0 si l'appelant y a
; droit, E_BADF sinon (ne touche que x0, x2, x3)
acces:
	ldr	w2,[x0,MSG_PID]
	mov	x0,0
	cbz	x2,.out				; avis du noyau
	movl	x3,SVC_PID_NOYAU
	cmp	x2,x3
	b.eq	.out
	cmp	x1,x2
	b.eq	.out
	mov	x0,E_BADF
.out:	ret

; x0 = pid -> x0 = entree de son tampon (0 : aucune)
tampon_entree:
	adr	x1,tampons
	mov	x2,TP_MAX
.t:	ldr	x3,[x1]
	cmp	x3,x0
	b.eq	.got
	add	x1,x1,16
	subs	x2,x2,1
	b.ne	.t
	mov	x0,0
	ret
.got:	mov	x0,x1
	ret

; x0 = message -> x0 = adresse du tampon de l'appelant (0 : aucun)
tampon_de:
	ldr	w0,[x0,MSG_PID]
	cbz	x0,.none
	str	x30,[sp,-16]!
	bl	tampon_entree
	ldr	x30,[sp],16
	cbz	x0,.none
	ldr	x0,[x0,8]
	movl	x1,BUF_SLOT
	movl	x2,BUF_VA_BASE
	madd	x0,x0,x1,x2
.none:	ret

; 14 tampon() -> a numero du tampon partage de l'appelant
op_tampon:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,MSG_PID]
	cbz	x20,.inval
	movl	x0,SVC_PID_NOYAU
	cmp	x20,x0
	b.eq	.inval
	mov	x0,x20				; deja le sien ?
	bl	tampon_entree
	cbnz	x0,.rend
	mov	x0,0				; une place libre
	bl	tampon_entree
	cbz	x0,.nomem
	mov	x1,x0
	movl	x0,FS_TAMPON_TAILLE
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	stp	x20,x0,[x1]
	mov	x2,x1
	mov	x1,x20
	sys	SYS_BUF_GRANT
	mov	x0,x2
.rend:	ldr	x0,[x0,8]
	str	x0,[x19,MSG_A]
	mov	x0,0
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid termine : son tampon rendu ; vfs oublie si c'etait lui
tampons_fin:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,pid_vfs
	ldr	x2,[x1]
	cmp	x2,x19
	b.ne	.t
	str	xzr,[x1]
.t:	cbz	x19,.out
	mov	x0,x19
	bl	tampon_entree
	cbz	x0,.out
	mov	x1,x0
	ldr	x0,[x1,8]
	stp	xzr,xzr,[x1]
	sys	SYS_BUF_FREE			; demappe ici : libere a la derniere
.out:	ldr	x19,[sp],16			; reference (le client est parti)
	epilogue

macro fsowner_data {
		align 8
pid_vfs		rb 8				; pid du service vfs (connu a sa
						; premiere ouverture)
open_extras	rb 8				; attributs demandes par vfs (ouverture en cours)
tampons		rb TP_MAX * 16			; pid, numero du tampon
proclist	rb PL_MAX * PL_SIZE
}
