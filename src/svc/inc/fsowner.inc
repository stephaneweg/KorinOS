; fsowner.inc - services de fichiers (fatfs, ramfs) : proprietaires d'un
; fichier ouvert et tampons partages par process (docs/FICHIERS.md, 4.2)
;  * proprietaire : le pid de l'appelant (MSG_PID). vfs transmet les
;    ouvertures des programmes (SVC_FORWARD) : MSG_PID est alors le programme
;    lui-meme, d = 0. Seule V_EXEC passe encore par un appel de vfs, avec le
;    pid du programme en d ;
;  * ouverture transmise par vfs : c = attributs a ajouter (ATTR_SYS,
;    ATTR_LIB, d'apres le chemin) ; la reponse porte en a le canal du service
;    (open_extras_take, open_extras_put) ;
;  * un fichier ouvert a un ou plusieurs proprietaires (table proprios :
;    couples numero + 1, pid). Plusieurs : apres un fork (avis SVC_OP_FORK,
;    inscription SVF_NAISSANCES), le fils tient tous les fichiers du pere, aux
;    memes numeros (sa memoire recopiee, les tables d'AppKit comprises, reste
;    juste) ; ils partagent la position du fichier (gardee par le service).
;    Fermer (op 12) ou finir (SVC_OP_FIN) retire le pid ; le fichier se ferme
;    avec son dernier proprietaire (fo_own_*) ;
;  * acces (ops 10-17, 26, fermer) : un proprietaire ou le noyau (pid 0 :
;    avis, SVC_PID_NOYAU) ; sinon E_BADF ;
;  * tampon (ops 14-16) : un par process client, FS_TAMPON_TAILLE octets,
;    cree par le service (mappe chez lui) et accorde a ce pid ; rendu a
;    l'avis de fin du process (SVC_OP_FIN).
; Donnees : fsowner_data dans le segment de donnees du service, qui definit
; canal (son numero de service).

FOW_MAX	= 1024				; couples (fichier, pid) : proprios
TP_MAX	= 48				; tampons (process clients a la fois : au-dela,
					; le client passe par des messages de 464 o,
					; secteur par secteur, ~1,6 Mo/s)
PL_SIZE	= 64				; entree de SYS_PROC_INFO

; x0 = message d'ouverture -> x0 = proprietaire du fichier : MSG_PID ; d (pid
; pour qui vfs ouvre, V_EXEC) si l'appelant est vfs (son pid est appris a la
; premiere ouverture qu'il fait : SYS_PROC_INFO)
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
	mov	x0,x4				; vfs ? (nom du process)
	adr	x1,proclist
	sys	SYS_PROC_INFO
	cbnz	x0,.pid
	adr	x5,proclist
	ldr	w6,[x5,24]			; « vfs », 0
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

; x0 = message, x1 = numero du fichier -> x0 = 0 si l'appelant y a droit (un
; de ses proprietaires, ou le noyau), E_BADF sinon (ne touche que x0-x3, x8,
; x9)
acces:
	ldr	w2,[x0,MSG_PID]
	mov	x0,0
	cbz	x2,.out				; avis du noyau
	movl	x3,SVC_PID_NOYAU
	cmp	x2,x3
	b.eq	.out
	mov	x0,x1
	mov	x1,x2
	str	x30,[sp,-16]!
	bl	fo_own_find
	ldr	x30,[sp],16
	cmp	x0,0
	mov	x0,E_BADF
	csel	x0,xzr,x0,ne
.out:	ret

; --- proprietaires (table proprios : numero + 1 sur 32 bits, pid sur 32) ---

; x0 = numero, x1 = pid -> x0 = son couple (0 : aucun) ; ne touche que x0-x3,
; x8, x9
fo_own_find:
	add	w0,w0,1
	adr	x2,proprios
	mov	x3,FOW_MAX
.f:	ldp	w8,w9,[x2]
	cmp	w8,w0
	b.ne	.n
	cmp	w9,w1
	b.eq	.got
.n:	add	x2,x2,8
	subs	x3,x3,1
	b.ne	.f
	mov	x0,0
	ret
.got:	mov	x0,x2
	ret

; x0 = numero, x1 = pid : un proprietaire de plus (rien s'il l'est deja)
; -> x0 = 1, 0 si la table est pleine ; ne touche que x0-x9
fo_own_add:
	mov	x6,x0
	mov	x7,x1
	str	x30,[sp,-16]!
	bl	fo_own_find
	ldr	x30,[sp],16
	cbnz	x0,.oui
	adr	x2,proprios
	mov	x3,FOW_MAX
.f:	ldr	w4,[x2]
	cbz	w4,.put
	add	x2,x2,8
	subs	x3,x3,1
	b.ne	.f
	mov	x0,0
	ret
.put:	add	w4,w6,1
	stp	w4,w7,[x2]
.oui:	mov	x0,1
	ret

; x0 = numero -> x0 = ses proprietaires (ne touche que x0-x4)
fo_own_count:
	add	w1,w0,1
	adr	x2,proprios
	mov	x3,FOW_MAX
	mov	x0,0
.f:	ldr	w4,[x2],8
	cmp	w4,w1
	cinc	x0,x0,eq
	subs	x3,x3,1
	b.ne	.f
	ret

; x0 = numero : plus aucun proprietaire (ne touche que x0-x3)
fo_own_clear:
	add	w0,w0,1
	adr	x1,proprios
	mov	x2,FOW_MAX
.f:	ldr	w3,[x1]
	cmp	w3,w0
	b.ne	.n
	str	xzr,[x1]
.n:	add	x1,x1,8
	subs	x2,x2,1
	b.ne	.f
	ret

; x0 = message d'une fermeture (12), x1 = numero (acces deja verifie) -> x0 = 1
; si le fichier doit etre ferme : le noyau (pid 0, SVC_PID_NOYAU) le ferme
; pour tous, un process le quitte (ferme s'il etait le dernier)
fo_own_close:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	ldr	w1,[x0,MSG_PID]
	cbz	w1,.tous
	movl	x2,SVC_PID_NOYAU
	cmp	x1,x2
	b.eq	.tous
	mov	x0,x19
	bl	fo_own_find
	cbz	x0,.compte
	str	xzr,[x0]
.compte:
	mov	x0,x19
	bl	fo_own_count
	cmp	x0,0
	cset	x0,eq
	b	.out
.tous:	mov	x0,x19
	bl	fo_own_clear
	mov	x0,1
.out:	ldr	x19,[sp],16
	epilogue

; x0 = pid termine, x1 = fonction (x0 = numero : ferme le fichier) : le pid
; quitte ses fichiers ; ceux dont il etait le dernier proprietaire sont fermes
fo_own_fin:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x19,.out
	adr	x21,proprios
	mov	x22,FOW_MAX
.f:	ldp	w0,w1,[x21]
	cbz	w0,.n
	cmp	w1,w19
	b.ne	.n
	str	xzr,[x21]
	sub	w0,w0,1
	str	x0,[sp,-16]!
	bl	fo_own_count
	mov	x1,x0
	ldr	x0,[sp],16
	cbnz	x1,.n
	blr	x20
.n:	add	x21,x21,8
	subs	x22,x22,1
	b.ne	.f
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fils, x1 = pere : avis de fork (SVC_OP_FORK) ; le fils devient
; proprietaire de chaque fichier du pere (memes numeros ; tampon : le sien,
; demande au premier besoin) -> x0 = couples que la table pleine a refuses
fo_own_fork:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x23,0
	cbz	x19,.out
	cbz	x20,.out
	adr	x21,proprios
	mov	x22,FOW_MAX
.f:	ldp	w0,w1,[x21]
	cbz	w0,.n
	cmp	w1,w20
	b.ne	.n
	sub	w0,w0,1
	mov	x1,x19
	bl	fo_own_add
	cbnz	x0,.n
	add	x23,x23,1
.n:	add	x21,x21,8
	subs	x22,x22,1
	b.ne	.f
.out:	mov	x0,x23
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

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
proprios	rb FOW_MAX * 8			; numero + 1 (0 : libre), pid
proclist	rb PL_SIZE
}
