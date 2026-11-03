; path.inc - vfs : chemins des programmes et dossiers courants par pid
; (docs/FICHIERS.md, etape 1, ops 30-36). Le code de chemins vient de
; src/kernel/files.asm (path_normalize, path_from_volume, path_to_volume,
; app_main_fix, spawn_name), tel quel ; le noyau garde le sien pour son
; relais (P_CWD) jusqu'a l'etape 5.
;
;  * Chemin d'un programme : « SD0:/a/b », « sd0:a/b », « SD:/a » (volume 0),
;    relatif (dossier courant du pid appelant), ou « /a » : racine du volume
;    courant (« \ » de DOS : dossier SD1:/docs, « /musique » -> SD1:/musique).
;  * Forme interne : « /sd0/a/b » (minuscules pour le volume, sans « . »,
;    « .. », ni « / » final). Le dossier courant d'un pid est garde sous
;    cette forme ; une entree vide vaut « /sd0 ».
;  * Avis de naissance (0xFFFE) : le fils recoit le dossier du pere ; avis de
;    fin (0xFFFF) : le dossier est oublie.

PATH_MAX	= 256
DOSSIERS_MAX	= 64				; pids 1..64 (MAX_PROCS)
MAP_MAX		= 16				; chemins virtuels
MAP_PHYS	= 64				; une entree : virtuel, puis physique interne
MAP_TAILLE	= 192

; x0 = dossier courant (absolu), x1 = chemin, x2 = sortie (PATH_MAX)
; -> x0 = 1 si ok. Resultat absolu, sans "." ni "..", sans '/' final.
path_normalize:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x2				; fin courante du resultat
	strb	wzr,[x20]
	ldrb	w3,[x19]
	cmp	w3,'/'
	b.eq	.parse				; absolu : on ignore le dossier courant
	mov	x22,x0
	mov	x0,x22
	bl	.walk
	cbz	x0,.fail
.parse:	mov	x0,x19
	bl	.walk
	cbz	x0,.fail
	cmp	x21,x20				; resultat vide : "/"
	b.ne	.ok
	mov	w0,'/'
	strb	w0,[x20]
	strb	wzr,[x20,1]
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (interne) x0 = chemin a ajouter a [x20, x21[ -> x0 = 1 si ok, x21 mis a jour
.walk:
.w_comp:
	ldrb	w3,[x0]
	cbz	w3,.w_end
	cmp	w3,'/'
	b.ne	.w_name
	add	x0,x0,1
	b	.w_comp
.w_name:
	cmp	w3,'.'				; "." ou ".." ?
	b.ne	.w_copy
	ldrb	w4,[x0,1]
	cbz	w4,.w_dot
	cmp	w4,'/'
	b.eq	.w_dot
	cmp	w4,'.'
	b.ne	.w_copy
	ldrb	w4,[x0,2]
	cbz	w4,.w_up
	cmp	w4,'/'
	b.ne	.w_copy
.w_up:	add	x0,x0,2				; ".." : retirer le dernier composant
.w_pop:	cmp	x21,x20
	b.ls	.w_comp
	sub	x21,x21,1
	ldrb	w4,[x21]
	strb	wzr,[x21]
	cmp	w4,'/'
	b.ne	.w_pop
	b	.w_comp
.w_dot:	add	x0,x0,1
	b	.w_comp
.w_copy:
	add	x4,x20,PATH_MAX-2
	cmp	x21,x4
	b.hs	.w_fail
	mov	w4,'/'
	strb	w4,[x21],1
.w_c:	ldrb	w3,[x0]
	cbz	w3,.w_cend
	cmp	w3,'/'
	b.eq	.w_cend
	add	x4,x20,PATH_MAX-1
	cmp	x21,x4
	b.hs	.w_fail
	strb	w3,[x21],1
	add	x0,x0,1
	b	.w_c
.w_cend:
	strb	wzr,[x21]
	b	.w_comp
.w_end:	strb	wzr,[x21]
	mov	x0,1
	ret
.w_fail:
	mov	x0,0
	ret

; x0 = chemin d'un programme (tampon de PATH_MAX, modifie en place) :
; « SD0:/a/b », « sd0:a/b », « SD:/a » -> « /sd0/a/b » ; un chemin relatif
; (sans « : ») reste tel quel -> x0 = 0 s'il commence par « / » (traite
; avant par resoudre) ou si le volume est vide
path_from_volume:
	ldrb	w1,[x0]
	cmp	w1,'/'
	b.eq	.no
	mov	x1,0				; « NOM: » ? (lettres et chiffres, 8 au plus)
.n:	ldrb	w2,[x0,x1]
	cmp	w2,':'
	b.eq	.vol
	sub	w3,w2,'0'
	cmp	w3,9
	b.ls	.c
	orr	w3,w2,0x20
	sub	w3,w3,'a'
	cmp	w3,25
	b.hi	.rel
.c:	add	x1,x1,1
	cmp	x1,8
	b.ls	.n
.rel:	mov	x0,1				; relatif : inchange
	ret
.vol:	cbz	x1,.no				; x1 = longueur du nom
	ldrb	w2,[x0,x1]			; (sur le « : »)
	sub	x3,x1,1				; sans numero : volume 0
	ldrb	w4,[x0,x3]
	sub	w4,w4,'0'
	cmp	w4,9
	cset	x5,hi				; x5 = 1 : ajouter « 0 »
	add	x6,x0,x1			; reste : apres « : » (et « / »)
	add	x6,x6,1
	ldrb	w4,[x6]
	cmp	w4,'/'
	b.ne	.len
	add	x6,x6,1
.len:	mov	x7,0				; longueur du reste
.l:	ldrb	w4,[x6,x7]
	cbz	w4,.mv
	add	x7,x7,1
	b	.l
.mv:	add	x8,x1,x5			; nouvelle tete : « / » + nom (+ « 0 ») + « / »
	add	x8,x8,2
	add	x9,x8,x7
	cmp	x9,PATH_MAX-1
	b.hs	.no
	add	x10,x6,x7			; deplacer le reste (de la fin vers le debut,
.mvl:	ldrb	w4,[x10]			; la tete peut grandir d'un octet)
	add	x11,x0,x8
	sub	x12,x10,x6
	strb	w4,[x11,x12]
	cmp	x10,x6
	b.ls	.head
	sub	x10,x10,1
	b	.mvl
.head:	mov	x10,x1				; nom en minuscules, decale d'un cran
.h:	cbz	x10,.slash
	sub	x10,x10,1
	ldrb	w4,[x0,x10]
	sub	w11,w4,'A'
	cmp	w11,25
	b.hi	.hc
	add	w4,w4,32
.hc:	add	x11,x10,1
	strb	w4,[x0,x11]
	b	.h
.slash:	mov	w4,'/'
	strb	w4,[x0]
	add	x10,x1,1
	cbz	x5,.sep
	mov	w4,'0'
	strb	w4,[x0,x10]
	add	x10,x10,1
.sep:	mov	w4,'/'
	strb	w4,[x0,x10]
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = chemin interne normalise (« /sd0/a »), x1 = sortie -> « SD0:/a »
; (x1 rendu apres le 0 final)
path_to_volume:
	mov	x2,x0
	ldrb	w3,[x2],1			; « / »
.n:	ldrb	w3,[x2],1			; nom du volume, en majuscules
	cbz	w3,.root
	cmp	w3,'/'
	b.eq	.rest
	sub	w4,w3,'a'
	cmp	w4,25
	b.hi	.c
	sub	w3,w3,32
.c:	strb	w3,[x1],1
	b	.n
.root:	sub	x2,x2,1				; (fin : « VOL:/ »)
.rest:	mov	w3,':'
	strb	w3,[x1],1
	mov	w3,'/'
	strb	w3,[x1],1
.r:	ldrb	w3,[x2],1
	strb	w3,[x1],1
	cbnz	w3,.r
	ret

; x0 = chemin interne (PATH_MAX) : « .../<nom>.app » -> « .../<nom>.app/main »
app_main_fix:
	mov	x1,x0
.e:	ldrb	w2,[x1]
	cbz	w2,.end
	add	x1,x1,1
	b	.e
.end:	sub	x2,x1,x0
	cmp	x2,5
	b.lo	.ret
	ldur	w3,[x1,-4]
	movl	w4,0x7070612E			; « .app »
	cmp	w3,w4
	b.ne	.ret
	cmp	x2,PATH_MAX-6
	b.hs	.ret
	adr	x2,sp_slmain
.c:	ldrb	w3,[x2],1
	strb	w3,[x1],1
	cbnz	w3,.c
.ret:	ret

; x0 = chemin, x1 = son dernier composant, x2 = tampon de 32 octets
; -> x0 = nom du process : « /sd0/apps/paint.app/main » donne « paint »
; (l'executable d'un paquet .app), sinon le dernier composant
spawn_name:
	ldr	w3,[x1]				; « main » ?
	movl	w4,0x6E69616D
	cmp	w3,w4
	b.ne	.last
	ldrb	w3,[x1,4]
	cbnz	w3,.last
	sub	x4,x1,1				; sur le « / »
	sub	x5,x4,x0
	cmp	x5,5
	b.lt	.last
	ldur	w3,[x4,-4]			; « .app » avant ?
	movl	w5,0x7070612E
	cmp	w3,w5
	b.ne	.last
	sub	x4,x4,4				; fin du nom du paquet
	mov	x5,x4				; son debut : apres le « / » precedent
.back:	cmp	x5,x0
	b.ls	.cp
	ldurb	w3,[x5,-1]
	cmp	w3,'/'
	b.eq	.cp
	sub	x5,x5,1
	b	.back
.cp:	mov	x6,0
.c:	cmp	x5,x4
	b.hs	.end
	cmp	x6,31
	b.hs	.end
	ldrb	w3,[x5],1
	strb	w3,[x2,x6]
	add	x6,x6,1
	b	.c
.end:	strb	wzr,[x2,x6]
	mov	x0,x2
	ret
.last:	mov	x0,x1
	ret

; ---------------------------------------------------------------------------
;  Dossiers courants
; ---------------------------------------------------------------------------

; x0 = pid -> x0 = son entree dans la table (0 : hors table) ; ne touche que x0, x1
dossier_entree:
	sub	x1,x0,1
	mov	x0,0
	cmp	x1,DOSSIERS_MAX
	b.hs	.out
	adr	x0,dossiers
	add	x0,x0,x1,lsl 8
.out:	ret

; x0 = pid -> x0 = son dossier courant interne (« /sd0 » par defaut)
; ne touche que x0, x1
dossier_de:
	sub	x1,x0,1
	cmp	x1,DOSSIERS_MAX
	b.hs	.def
	adr	x0,dossiers
	add	x0,x0,x1,lsl 8
	ldrb	w1,[x0]
	cbz	w1,.def
	ret
.def:	adr	x0,dossier_defaut
	ret

; x0 = fils, x1 = pere (0 : le noyau) : avis de naissance, le fils recoit le
; dossier du pere (par defaut s'il n'en a pas)
naissance:
	adr	x2,dossiers
	sub	x0,x0,1
	cmp	x0,DOSSIERS_MAX
	b.hs	.out
	add	x0,x2,x0,lsl 8
	strb	wzr,[x0]
	sub	x1,x1,1				; (pere 0 : hors table)
	cmp	x1,DOSSIERS_MAX
	b.hs	.out
	add	x1,x2,x1,lsl 8
	mov	x3,0
.c:	ldrb	w4,[x1,x3]
	strb	w4,[x0,x3]
	add	x3,x3,1
	cbz	w4,.out
	cmp	x3,PATH_MAX
	b.lo	.c
	strb	wzr,[x0,PATH_MAX-1]
.out:	ret

; x0 = pid termine : son dossier est oublie
oublier_dossier:
	sub	x0,x0,1
	cmp	x0,DOSSIERS_MAX
	b.hs	.out
	adr	x1,dossiers
	add	x0,x1,x0,lsl 8
	strb	wzr,[x0]
.out:	ret

; x0 = pid, x1 = chemin du programme, x2 = sortie (PATH_MAX) : chemin
; interne absolu normalise -> x0 = 0 ou erreur (E_NOENT : volume mal ecrit,
; E_INVAL : trop long)
resoudre:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,PATH_MAX
	mov	x20,x1
	mov	x21,x2
	bl	dossier_de
	mov	x19,x0				; dossier courant
	mov	x22,0				; longueur du prefixe
	ldrb	w0,[x20]
	cmp	w0,'/'
	b.ne	.copie
	mov	x0,x20				; chemin virtuel (SD0:/etc/paths.txt) ?
	bl	chemin_virtuel
	cbz	x0,.rac
	add	x0,x0,MAP_PHYS			; son chemin physique, puis le reste
.pp:	ldrb	w2,[x0,x22]
	cbz	w2,.ppf
	strb	w2,[sp,x22]
	add	x22,x22,1
	b	.pp
.ppf:	mov	x20,x1
	b	.copie
.rac:	mov	w0,'/'				; « /a » : racine du volume courant,
	strb	w0,[sp]				; prefixe « /vol » du dossier
	mov	x22,1
.pv:	ldrb	w0,[x19,x22]
	cbz	w0,.copie
	cmp	w0,'/'
	b.eq	.copie
	strb	w0,[sp,x22]
	add	x22,x22,1
	b	.pv
.copie:	mov	x1,0
.c:	ldrb	w0,[x20,x1]
	add	x2,x22,x1
	cmp	x2,PATH_MAX-1
	b.hs	.long
	strb	w0,[sp,x2]
	add	x1,x1,1
	cbnz	w0,.c
	cbnz	x22,.norm			; deja « /vol/... »
	mov	x0,sp				; « VOL:... » -> « /vol/... »
	bl	path_from_volume
	cbz	x0,.noent
.norm:	mov	x0,x19
	mov	x1,sp
	mov	x2,x21
	bl	path_normalize
	cbz	x0,.long
	mov	x0,0
	b	.out
.long:	mov	x0,E_INVAL
	b	.out
.noent:	mov	x0,E_NOENT
.out:	add	sp,sp,PATH_MAX
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Chemins virtuels (docs/FICHIERS.md) : « /gcc » = « SD0:/gcc », quel que soit
;  le volume du dossier courant. init2 les donne (SD0:/etc/paths.txt).
; ---------------------------------------------------------------------------

; x0 = chemin « /a/b » -> x0 = son entree (0 : aucune), x1 = le reste (« /b »
; ou vide) ; ne touche que x0 a x6
chemin_virtuel:
	adr	x2,chemins_v
	mov	x3,MAP_MAX
.e:	ldrb	w4,[x2]
	cbz	w4,.non
	mov	x5,0
.c:	ldrb	w4,[x2,x5]
	cbz	w4,.fin
	ldrb	w6,[x0,x5]
	cmp	w4,w6
	b.ne	.suiv
	add	x5,x5,1
	b	.c
.fin:	ldrb	w6,[x0,x5]			; composant entier : pas « /gccx »
	cbz	w6,.oui
	cmp	w6,'/'
	b.eq	.oui
.suiv:	add	x2,x2,MAP_TAILLE
	sub	x3,x3,1
	cbnz	x3,.e
.non:	mov	x0,0
	ret
.oui:	add	x1,x0,x5
	mov	x0,x2
	ret

; (msg) VFS_PATHMAP : donnees = virtuel, 0, physique, 0 -> x0 = 0, E_INVAL,
; E_NOSPC ; un chemin virtuel deja connu est remplace
chemin_ajouter:
	prologue
	str	x19,[sp,-16]!
	adr	x0,msg
	strb	wzr,[x0,MSG_TAILLE-1]
	add	x19,x0,MSG_DATA
	ldrb	w0,[x19]
	cmp	w0,'/'
	b.ne	.inval
	ldrb	w0,[x19,1]
	cbz	w0,.inval			; (« / » : pas une entree)
	mov	x0,0
.lv:	ldrb	w1,[x19,x0]
	cbz	w1,.phy
	add	x0,x0,1
	cmp	x0,MAP_PHYS
	b.lo	.lv
	b	.inval
.phy:	add	x1,x19,x0			; physique -> forme interne
	add	x1,x1,1
	adr	x2,nom_exec
	mov	x3,0
.cp:	ldrb	w4,[x1,x3]
	strb	w4,[x2,x3]
	add	x3,x3,1
	cbz	w4,.conv
	cmp	x3,PATH_MAX-1
	b.lo	.cp
	b	.inval
.conv:	mov	x0,x2
	bl	path_from_volume
	cbz	x0,.inval
	adr	x0,dossier_defaut
	adr	x1,nom_exec
	adr	x2,resolu
	bl	path_normalize
	cbz	x0,.inval
	adr	x2,resolu
	mov	x3,0
.lp:	ldrb	w4,[x2,x3]
	cbz	w4,.place
	add	x3,x3,1
	cmp	x3,MAP_TAILLE-MAP_PHYS
	b.lo	.lp
	b	.inval
.place:	adr	x2,chemins_v			; la sienne, ou la premiere vide
	mov	x3,MAP_MAX
.e:	ldrb	w4,[x2]
	cbz	w4,.met
	mov	x5,0
.c:	ldrb	w4,[x2,x5]
	ldrb	w6,[x19,x5]
	cmp	w4,w6
	b.ne	.suiv
	add	x5,x5,1
	cbnz	w4,.c
	b	.met
.suiv:	add	x2,x2,MAP_TAILLE
	sub	x3,x3,1
	cbnz	x3,.e
	mov	x0,E_NOSPC
	b	.out
.met:	mov	x5,0
.v:	ldrb	w4,[x19,x5]
	strb	w4,[x2,x5]
	add	x5,x5,1
	cbnz	w4,.v
	adr	x1,resolu
	add	x2,x2,MAP_PHYS
.p:	ldrb	w4,[x1],1
	strb	w4,[x2],1
	cbnz	w4,.p
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = op FS_*, x1 = chemin interne (« /sd0/a », hors de msg), x2 = d
; (pid pour qui l'on ouvre, 0 sinon) ; msg : a deja rempli -> x0 = resultat
; (reponse du service dans msg ; ouverture : a = son canal)
vers_volume:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	adr	x22,msg
	ldrb	w2,[x1]
	cmp	w2,'/'
	b.ne	.noent
	add	x0,x1,1
	bl	volume_de			; -> x0 = entree, x1 = reste
	cbz	x0,.noent
	mov	x21,x0
	add	x2,x22,MSG_DATA
.cp:	ldrb	w3,[x1],1
	strb	w3,[x2],1
	cbnz	w3,.cp
	sub	x2,x2,x22
	sub	x2,x2,MSG_DATA
	str	w2,[x22,MSG_LEN]
	str	x19,[x22]
	str	x20,[x22,MSG_D]
	ldr	x0,[x21,VOL_CANAL]
	mov	x1,x22
	sys	SYS_SVC_CALL
	str	x0,[x22]
	tbnz	x0,63,.out
	cmp	x19,FS_OUVRIR
	b.ne	.out
	ldr	x1,[x21,VOL_CANAL]
	str	x1,[x22,MSG_A]
	b	.out
.noent:	mov	x0,E_NOENT
	str	x0,[x22]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = resultat d'une ouverture (garde) : reussie et sous /sd0/sys/ ->
; c |= ATTR_SYS (droits demandes au lancement, D9) ; sous /sd0/lib/ ->
; c |= ATTR_LIB (un kit, inscrit par le noyau : etape 5)
marquer_sys:
	tbnz	x0,63,.out
	adr	x2,chemin_sys
	mov	x5,ATTR_SYS
	str	x30,[sp,-16]!
	bl	.prefixe
	adr	x2,chemin_lib
	mov	x5,ATTR_LIB
	bl	.prefixe
	ldr	x30,[sp],16
.out:	ret
.prefixe:					; x2 = prefixe, x5 = attribut
	adr	x1,resolu
.c:	ldrb	w3,[x2],1
	cbz	w3,.oui
	ldrb	w4,[x1],1
	cmp	w3,w4
	b.eq	.c
	ret
.oui:	adr	x1,msg
	ldr	x2,[x1,MSG_C]
	orr	x2,x2,x5
	str	x2,[x1,MSG_C]
	ret

; (msg) ops 30-36 au nom de MSG_PID (V_EXEC : donnees = nom du process, 0,
; chemin resolu « SD0:/... », 0) -> x0 = resultat (msg pret pour la
; reponse)
ops_chemins:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x20,msg
	ldr	x19,[x20]			; op
	ldr	w21,[x20,MSG_PID]		; appelant
	strb	wzr,[x20,MSG_TAILLE-1]		; (donnees terminees)
	cmp	x19,V_GETCWD
	b.eq	.getcwd
	cmp	x19,V_EXEC
	b.eq	.exec
	cmp	x19,V_RENAME
	b.ne	.res
	mov	x0,x21
	bl	chemin_renommer
	b	.out
.res:	mov	x0,x21
	add	x1,x20,MSG_DATA
	adr	x2,resolu
	bl	resoudre
	cbnz	x0,.out
	cmp	x19,V_OUVRIR
	b.eq	.ouvrir
	cmp	x19,V_CHDIR
	b.eq	.chdir
	cmp	x19,V_UTIME
	b.eq	.utime
	cmp	x19,V_UNLINK			; un fichier supprime : son image
	b.ne	.vol				; prechargee n'est plus prise
	bl	forget_resolu
.vol:	sub	x0,x19,V_STAT-FS_STAT		; 31-33 -> FS_STAT, FS_MKDIR, FS_UNLINK
	adr	x1,resolu
	mov	x2,0
	bl	vers_volume
	b	.out
.utime:	mov	x0,FS_UTIME			; (b : la date, deja dans msg)
	adr	x1,resolu
	mov	x2,0
	bl	vers_volume
	b	.out
.ouvrir:					; a = drapeaux (deja dans msg)
	ldr	x0,[x20,MSG_A]			; ecriture, creation, troncature : le
	tst	x0,O_WRITE or O_CREATE or O_TRUNC or O_APPEND
	b.eq	.ouv				; fichier change (image prechargee)
	bl	forget_resolu
.ouv:	mov	x0,FS_OUVRIR
	adr	x1,resolu
	mov	x2,x21
	bl	vers_volume
	bl	marquer_sys
	b	.out
.chdir:	mov	x0,x21
	bl	dossier_entree
	mov	x22,x0
	mov	x0,E_INVAL
	cbz	x22,.out
	mov	x0,FS_STAT			; un repertoire ?
	adr	x1,resolu
	mov	x2,0
	bl	vers_volume
	tbnz	x0,63,.out
	ldr	x0,[x20,MSG_C]
	tst	x0,ATTR_DIR
	mov	x0,E_NOTDIR
	b.eq	.out
	adr	x1,resolu
.cd:	ldrb	w2,[x1],1
	strb	w2,[x22],1
	cbnz	w2,.cd
	str	wzr,[x20,MSG_LEN]
	mov	x0,0
	b	.out
.getcwd:
	mov	x0,x21
	bl	dossier_de
	add	x1,x20,MSG_DATA			; « /sd0/a » -> « SD0:/a »
	bl	path_to_volume
	sub	x1,x1,x20
	sub	x1,x1,MSG_DATA
	str	w1,[x20,MSG_LEN]
	mov	x0,0
	b	.out
.exec:	mov	x0,x21
	bl	exec_resoudre
	cbnz	x0,.out
	mov	x0,O_READ			; ouvert pour l'appelant
	str	x0,[x20,MSG_A]
	mov	x0,FS_OUVRIR
	adr	x1,resolu
	mov	x2,x21
	bl	vers_volume
	bl	marquer_sys
	tbnz	x0,63,.out
	mov	x22,x0
	adr	x0,resolu			; nom du process, dans les donnees
	mov	x1,x0
	mov	x2,x0
.dc:	ldrb	w3,[x2],1			; (dernier composant)
	cbz	w3,.nom
	cmp	w3,'/'
	b.ne	.dc
	mov	x1,x2
	b	.dc
.nom:	adr	x2,nom_proc
	bl	spawn_name
	add	x1,x20,MSG_DATA
	mov	x2,0
.cn:	ldrb	w3,[x0,x2]
	strb	w3,[x1,x2]
	add	x2,x2,1
	cbnz	w3,.cn
	add	x1,x1,x2			; puis le chemin resolu, « SD0:/a/b »
	adr	x0,resolu			; (etape 4 : AppKit le donne a init2
	bl	path_to_volume			; pour un programme de SD0:/sys)
	sub	x1,x1,x20
	sub	x1,x1,MSG_DATA
	str	w1,[x20,MSG_LEN]
	mov	x0,x22
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (msg) V_RENAME au nom du pid x0 : les deux chemins resolus, un seul volume
; -> x0 = resultat (E_XDEV : deux volumes)
chemin_renommer:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	adr	x20,msg
	add	x1,x20,MSG_DATA
	adr	x2,resolu
	bl	resoudre
	cbnz	x0,.out
	add	x1,x20,MSG_DATA
.f:	ldrb	w0,[x1],1
	cbnz	w0,.f
	mov	x0,x19
	adr	x2,nom_exec
	bl	resoudre
	cbnz	x0,.out
	bl	forget_resolu
	adr	x0,nom_exec
	bl	forget_chemin
	adr	x0,nom_exec
	add	x0,x0,1
	bl	volume_de
	cbz	x0,.noent
	mov	x21,x0				; (x1 : la cible, dans son volume)
	mov	x19,x1
	adr	x0,resolu
	add	x0,x0,1
	bl	volume_de
	cbz	x0,.noent
	cmp	x0,x21
	b.ne	.xdev
	add	x2,x20,MSG_DATA			; ancien, 0, nouveau, 0
.a:	ldrb	w3,[x1],1
	strb	w3,[x2],1
	cbnz	w3,.a
.b:	ldrb	w3,[x19],1
	strb	w3,[x2],1
	cbnz	w3,.b
	sub	x2,x2,x20
	sub	x2,x2,MSG_DATA
	str	w2,[x20,MSG_LEN]
	mov	x0,FS_RENAME
	str	x0,[x20]
	ldr	x0,[x21,VOL_CANAL]
	mov	x1,x20
	sys	SYS_SVC_CALL
	b	.out
.xdev:	mov	x0,E_XDEV
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; resolu (« /sd0/bin/x ») va changer : le noyau oublie l'image prechargee de
; ce chemin (« SD0:/bin/x », IMAGE_FORGET ; docs/ABI.md, preload.txt)
forget_resolu:
	adr	x0,resolu
; (x0 = un autre chemin interne)
forget_chemin:
	prologue
	sub	sp,sp,PATH_MAX+16
	mov	x1,sp
	bl	path_to_volume
	mov	x0,sp
	mov	x8,SYS_IMAGE_FORGET
	svc	0
	add	sp,sp,PATH_MAX+16
	epilogue

; (V_EXEC) x0 = pid ; msg : nom ou chemin -> resolu = l'executable, x0 = 0
; ou erreur. Nom seul (sans « / » ni « : ») : SD0:/bin/<nom>, sinon
; SD0:/apps/<nom>.app/main ; chemin : « .../<nom>.app » -> « .../main ».
; Un repertoire : E_ISDIR.
exec_resoudre:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,nom_exec			; (msg sert aux requetes au volume)
	adr	x1,msg
	add	x1,x1,MSG_DATA
	mov	x2,0
.cp:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	cbz	w3,.vu
	cmp	x2,PATH_MAX-1
	b.lo	.cp
	strb	wzr,[x0,x2]
.vu:	mov	x1,x0
.sc:	ldrb	w3,[x1],1
	cbz	w3,.nom
	cmp	w3,'/'
	b.eq	.chemin
	cmp	w3,':'
	b.eq	.chemin
	b	.sc
.nom:	ldrb	w3,[x0]
	mov	x0,E_NOENT
	cbz	w3,.out
	adr	x0,resolu			; « /sd0/bin/<nom> »
	strb	wzr,[x0]
	adr	x1,sp_bin
	bl	ajouter
	cbz	x0,.long
	adr	x1,nom_exec
	bl	ajouter
	cbz	x0,.long
	bl	stat_resolu
	cmn	x0,2				; absente : « /sd0/apps/<nom>.app/main »
	b.ne	.out
	adr	x0,resolu
	strb	wzr,[x0]
	adr	x1,sp_apps
	bl	ajouter
	cbz	x0,.long
	adr	x1,nom_exec
	bl	ajouter
	cbz	x0,.long
	adr	x1,sp_main
	bl	ajouter
	cbz	x0,.long
	bl	stat_resolu
	b	.out
.chemin:
	mov	x0,x19
	adr	x1,nom_exec
	adr	x2,resolu
	bl	resoudre
	cbnz	x0,.out
	adr	x0,resolu
	bl	app_main_fix
	bl	stat_resolu
	b	.out
.long:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 0 si resolu est un fichier (E_ISDIR : repertoire), ou erreur
stat_resolu:
	prologue
	mov	x0,FS_STAT
	adr	x1,resolu
	mov	x2,0
	bl	vers_volume
	tbnz	x0,63,.out
	adr	x1,msg
	ldr	x1,[x1,MSG_C]
	mov	x0,0
	tst	x1,ATTR_DIR
	b.eq	.out
	mov	x0,E_ISDIR
.out:	epilogue

; x0 = chaine dans resolu (PATH_MAX), x1 = a ajouter -> x0 = 1, 0 si trop long
ajouter:
	adr	x2,resolu
	mov	x3,0
.f:	ldrb	w4,[x2,x3]
	cbz	w4,.c
	add	x3,x3,1
	b	.f
.c:	ldrb	w4,[x1],1
	strb	w4,[x2,x3]
	cbz	w4,.ok
	add	x3,x3,1
	cmp	x3,PATH_MAX-1
	b.lo	.c
	strb	wzr,[x2,x3]
	mov	x0,0
	ret
.ok:	mov	x0,1
	ret

dossier_defaut	db '/sd0',0
chemin_sys	db '/sd0/sys/',0
chemin_lib	db '/sd0/lib/',0
sp_bin		db '/sd0/bin/',0
sp_apps		db '/sd0/apps/',0
sp_main		db '.app/main',0
sp_slmain	db '/main',0
		align 8

macro chemins_data {
		align 8
dossiers	rb DOSSIERS_MAX * PATH_MAX	; dossier courant par pid (interne)
chemins_v	rb MAP_MAX * MAP_TAILLE		; chemins virtuels (a zero : aucun)
resolu		rb PATH_MAX			; chemin resolu de la requete
nom_exec	rb PATH_MAX
nom_proc	rb 32
}
