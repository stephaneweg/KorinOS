; vfstest - essai des chemins et dossiers courants de vfs (docs/FICHIERS.md,
; etape 1) : ops 30-36 par SVC_CALL au service « vfs », sans le noyau :
;  * relatif, « . », « .. », « SD: » (volume 0), « sd0:a » ;
;  * « /a » : racine du volume courant (dossier RAM0:/vt, « / » -> RAM0:/) ;
;  * chdir / getcwd, E_NOTDIR, ouverture pour l'appelant (ecriture et
;    fermeture directes chez fs.ram0 : il en est le proprietaire), exec
;    (nom seul, paquet .app, ATTR_SYS sous SD0:/sys/) ;
;  * un fils lance apres un chdir herite du dossier du pere (il ecrit son
;    getcwd) ; il change de dossier puis se termine ; un second fils (le
;    plus souvent le meme pid, repris) recoit le dossier du pere et non celui
;    du premier : l'avis de fin est passe avant la naissance.
; Lignes « vfstest : PASS ... » / « vfstest : FAIL ... » ; code de sortie :
; le nombre d'echecs.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	adr	x0,s_vfs
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.sans
	adr	x1,canal
	str	x0,[x1]
	cbz	x19,pere
	ldrb	w0,[x19]
	cmp	w0,'e'
	b.eq	enfant
	b	pere
.sans:	adr	x0,m_sans
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Le fils : « enfant <dossier attendu> » : ecrit son getcwd, puis va dans
;  RAM0:/vt/sous ; code 0 si le dossier est celui attendu
; ---------------------------------------------------------------------------
enfant:
	add	x20,x19,7			; dossier attendu
	mov	x0,V_GETCWD
	adr	x1,vide
	bl	vop
	mov	x21,x0
	adr	x0,m_enfant
	bl	puts
	adr	x0,msg
	add	x0,x0,MSG_DATA
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x22,1
	tbnz	x21,63,.fin
	adr	x0,msg
	add	x0,x0,MSG_DATA
	mov	x1,x20
	bl	str_eq
	eor	x22,x0,1
	mov	x0,V_CHDIR			; un dossier a lui avant de finir
	adr	x1,p_sous_abs
	bl	vop
.fin:	mov	x0,x22
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Le pere
; ---------------------------------------------------------------------------
pere:
	mov	x0,1500				; (lance au demarrage : RAM0: monte)
	sys	SYS_SLEEP
	mov	x0,V_UNLINK			; restes d'un essai precedent
	adr	x1,p_f_abs
	bl	vop
	mov	x0,V_UNLINK
	adr	x1,p_sous_abs
	bl	vop
	mov	x0,V_UNLINK
	adr	x1,p_vt
	bl	vop

	mov	x0,V_GETCWD			; dossier de depart : SD0:/
	adr	x1,vide
	adr	x2,l_cwd0
	adr	x3,c_sd0
	bl	cwd_vaut
	mov	x0,V_MKDIR			; RAM0:/vt
	adr	x1,p_vt
	adr	x2,l_mkdir
	mov	x3,0
	bl	attendu
	mov	x0,V_CHDIR			; « ram0:vt » (minuscules, sans « / »)
	adr	x1,p_vt_min
	adr	x2,l_chdir
	mov	x3,0
	bl	attendu
	mov	x0,V_GETCWD
	adr	x1,vide
	adr	x2,l_cwd_vt
	adr	x3,c_vt
	bl	cwd_vaut
	mov	x0,V_MKDIR			; relatif
	adr	x1,p_sous
	adr	x2,l_mkdir_rel
	mov	x3,0
	bl	attendu
	mov	x0,V_STAT			; « ./sous/. » : un repertoire
	adr	x1,p_sous_pts
	bl	vop
	adr	x1,msg
	ldr	x1,[x1,MSG_C]
	and	x1,x1,ATTR_DIR
	cmp	x0,0
	ccmp	x1,ATTR_DIR,0,eq
	cset	x0,eq
	adr	x1,l_stat_rel
	adr	x2,msg
	ldr	x2,[x2,MSG_C]
	bl	verdict
	mov	x0,V_CHDIR			; relatif
	adr	x1,p_sous
	adr	x2,l_chdir_rel
	mov	x3,0
	bl	attendu
	mov	x0,V_GETCWD
	adr	x1,vide
	adr	x2,l_cwd_sous
	adr	x3,c_sous
	bl	cwd_vaut
	mov	x0,V_CHDIR			; « .. »
	adr	x1,p_pp
	adr	x2,l_chdir_pp
	mov	x3,0
	bl	attendu
	mov	x0,V_GETCWD
	adr	x1,vide
	adr	x2,l_cwd_pp
	adr	x3,c_vt
	bl	cwd_vaut
	bl	essai_fichier
	mov	x0,V_CHDIR			; un fichier : E_NOTDIR
	adr	x1,p_f
	adr	x2,l_notdir
	mov	x3,E_NOTDIR
	bl	attendu
	mov	x0,V_STAT			; volume absent : E_NOENT
	adr	x1,p_absent
	adr	x2,l_absent
	mov	x3,E_NOENT
	bl	attendu

	; « / » : racine du volume courant (RAM0:)
	mov	x0,V_STAT			; « /vt/sous/../f.txt » -> RAM0:/vt/f.txt
	adr	x1,p_racine_f
	bl	vop
	adr	x1,msg
	ldr	x2,[x1,MSG_B]
	cmp	x0,0
	ccmp	x2,5,0,eq
	cset	x0,eq
	adr	x1,l_racine_f
	bl	verdict
	mov	x0,V_CHDIR			; « / » -> RAM0:/
	adr	x1,p_racine
	adr	x2,l_chdir_racine
	mov	x3,0
	bl	attendu
	mov	x0,V_GETCWD
	adr	x1,vide
	adr	x2,l_cwd_racine
	adr	x3,c_ram0
	bl	cwd_vaut
	mov	x0,V_CHDIR			; SD: (volume 0), puis « /bin »
	adr	x1,p_sd
	adr	x2,l_chdir_sd
	mov	x3,0
	bl	attendu
	mov	x0,V_GETCWD
	adr	x1,vide
	adr	x2,l_cwd_sd
	adr	x3,c_sd_bin
	bl	cwd_vaut
	mov	x0,V_STAT			; « /bin/vfstest » depuis SD0:/bin
	adr	x1,p_racine_bin
	bl	vop
	adr	x1,msg
	ldr	x2,[x1,MSG_C]
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_racine_bin
	bl	verdict
	mov	x0,V_CHDIR			; « / » -> SD0:/
	adr	x1,p_racine
	adr	x2,l_chdir_racine
	mov	x3,0
	bl	attendu
	mov	x0,V_GETCWD
	adr	x1,vide
	adr	x2,l_cwd_racine_sd
	adr	x3,c_sd0
	bl	cwd_vaut
	mov	x0,V_CHDIR			; au-dessus de la racine : refuse
	adr	x1,p_trop_haut
	bl	vop
	mov	x2,x0
	lsr	x0,x0,63
	adr	x1,l_trop_haut
	bl	verdict
	bl	essai_exec
	bl	essai_fils
	mov	x0,V_UNLINK			; menage
	adr	x1,p_f_abs
	adr	x2,l_unlink_f
	mov	x3,0
	bl	attendu
	mov	x0,V_UNLINK
	adr	x1,p_sous_abs
	adr	x2,l_unlink_sous
	mov	x3,0
	bl	attendu
	mov	x0,V_UNLINK
	adr	x1,p_vt
	adr	x2,l_unlink_vt
	mov	x3,0
	bl	attendu
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

; ouverture pour l'appelant (op 30), relatif : ecriture et fermeture
; directes chez le service du volume (proprietaire = ce pid)
essai_fichier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,msg
	mov	x1,O_READ or O_WRITE or O_CREATE or O_TRUNC
	str	x1,[x0,MSG_A]
	mov	x0,V_OUVRIR
	adr	x1,p_f
	bl	vop_a
	mov	x2,x0
	cmp	x0,0
	cset	x0,ge
	adr	x1,l_ouvrir
	bl	verdict
	adr	x0,msg
	ldp	x19,x20,[x0,MSG_A]		; canal, numero
	ldr	x0,[x0,MSG_C]
	tst	x0,ATTR_SYS
	cset	x0,eq
	adr	x1,l_pas_sys
	mov	x2,0
	bl	verdict
	adr	x0,msg				; 5 octets, directement
	mov	x1,FS_ECRIRE
	stp	x1,x20,[x0]
	stp	xzr,xzr,[x0,MSG_B]
	mov	x1,5
	str	x1,[x0,MSG_C]
	str	w1,[x0,MSG_LEN]
	adr	x1,s_bonjour
	mov	x3,0
.cp:	ldrb	w2,[x1,x3]
	add	x4,x0,x3
	strb	w2,[x4,MSG_DATA]
	add	x3,x3,1
	cmp	x3,5
	b.lo	.cp
	mov	x1,x0
	mov	x0,x19
	sys	SYS_SVC_CALL
	mov	x2,x0
	cmp	x0,5
	cset	x0,eq
	adr	x1,l_ecrire
	bl	verdict
	adr	x0,msg				; fermeture (appel du proprietaire)
	mov	x1,FS_FERMER
	stp	x1,x20,[x0]
	str	wzr,[x0,MSG_LEN]
	mov	x1,x0
	mov	x0,x19
	sys	SYS_SVC_CALL
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_fermer
	bl	verdict
	mov	x0,V_STAT			; « sous/../f.txt » : 5 octets
	adr	x1,p_f_pp
	bl	vop
	adr	x1,msg
	ldr	x2,[x1,MSG_B]
	cmp	x0,0
	ccmp	x2,5,0,eq
	cset	x0,eq
	adr	x1,l_stat_pp
	bl	verdict
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; V_EXEC : nom seul (SD0:/bin), paquet (.app/main), SD0:/sys (ATTR_SYS)
essai_exec:
	prologue
	adr	x0,p_ex_bin
	adr	x1,c_ex_bin
	mov	x2,0
	adr	x3,l_ex_bin
	bl	un_exec
	adr	x0,p_ex_app
	adr	x1,c_ex_app
	mov	x2,0
	adr	x3,l_ex_app
	bl	un_exec
	adr	x0,p_ex_pkg
	adr	x1,c_ex_app
	mov	x2,0
	adr	x3,l_ex_pkg
	bl	un_exec
	adr	x0,p_ex_sys
	adr	x1,s_vfs
	mov	x2,ATTR_SYS
	adr	x3,l_ex_sys
	bl	un_exec
	mov	x0,V_EXEC			; absent : E_NOENT
	adr	x1,p_ex_absent
	adr	x2,l_ex_absent
	mov	x3,E_NOENT
	bl	attendu
	mov	x0,V_EXEC			; un repertoire : E_ISDIR
	adr	x1,p_ex_rep
	adr	x2,l_ex_rep
	mov	x3,E_ISDIR
	bl	attendu
	epilogue

; x0 = nom ou chemin, x1 = nom du process attendu, x2 = ATTR_SYS attendu,
; x3 = libelle : V_EXEC, controle, fermeture
un_exec:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	x1,x0
	mov	x0,V_EXEC
	bl	vop
	mov	x22,x0
	tbnz	x0,63,.ko
	adr	x0,msg
	add	x0,x0,MSG_DATA
	mov	x1,x19
	bl	str_eq
	cbz	x0,.ko
	adr	x0,msg
	ldr	x1,[x0,MSG_C]
	and	x1,x1,ATTR_SYS
	cmp	x1,x20
	b.ne	.ko
	ldr	x22,[x0,MSG_D]			; (valeur : la taille)
	mov	x0,1
	b	.v
.ko:	mov	x0,0
.v:	mov	x1,x21
	mov	x2,x22
	bl	verdict
	adr	x0,msg				; fermeture (si ouvert)
	ldr	x1,[x0]
	tbnz	x1,63,.out
	ldp	x2,x3,[x0,MSG_A]
	mov	x1,FS_FERMER
	stp	x1,x3,[x0]
	str	wzr,[x0,MSG_LEN]
	mov	x1,x0
	mov	x0,x2
	sys	SYS_SVC_CALL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; un fils lance apres un chdir herite du dossier ; apres sa fin, son pid
; repris recoit le dossier du pere, pas le sien
essai_fils:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,V_CHDIR
	adr	x1,p_vt
	bl	vop
	adr	x0,p_moi
	adr	x1,a_enfant_vt
	sys	SYS_SPAWN
	mov	x19,x0
	tbnz	x0,63,.ko1
	sys	SYS_WAIT
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_fils
	bl	verdict
	b	.second
.ko1:	mov	x2,x0
	mov	x0,0
	adr	x1,l_fils
	bl	verdict
.second:
	mov	x0,V_CHDIR			; pere : SD0:/bin
	adr	x1,p_sd
	bl	vop
	adr	x0,p_moi
	adr	x1,a_enfant_bin
	sys	SYS_SPAWN
	mov	x20,x0
	tbnz	x0,63,.ko2
	sys	SYS_WAIT
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_fils2
	bl	verdict
	adr	x0,m_pids			; (pids des deux fils)
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_et
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	b	.out
.ko2:	mov	x2,x0
	mov	x0,0
	adr	x1,l_fils2
	bl	verdict
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Outils
; ---------------------------------------------------------------------------

; x0 = op, x1 = chemin (donnees) -> x0 = resultat (reponse dans msg) ;
; vop_a garde le champ a deja ecrit dans msg
vop:
	adr	x4,msg
	str	xzr,[x4,MSG_A]
vop_a:
	adr	x4,msg
	str	x0,[x4]
	stp	xzr,xzr,[x4,MSG_B]
	str	xzr,[x4,MSG_D]
	add	x5,x4,MSG_DATA
	mov	x6,0
.c:	ldrb	w7,[x1,x6]
	strb	w7,[x5,x6]
	add	x6,x6,1
	cbnz	w7,.c
	str	w6,[x4,MSG_LEN]
	adr	x0,canal
	ldr	x0,[x0]
	mov	x1,x4
	sys	SYS_SVC_CALL
	ret

; x0 = op, x1 = chemin, x2 = libelle, x3 = resultat attendu
attendu:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x2
	mov	x20,x3
	str	x30,[sp,-16]!
	bl	vop
	ldr	x30,[sp],16
	mov	x2,x0
	cmp	x0,x20
	cset	x0,eq
	mov	x1,x19
	bl	verdict
	ldp	x19,x20,[sp],16
	epilogue

; x0 = V_GETCWD, x1 = vide, x2 = libelle, x3 = dossier attendu
cwd_vaut:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x2
	mov	x20,x3
	bl	vop
	mov	x21,0
	tbnz	x0,63,.v
	adr	x0,msg
	add	x0,x0,MSG_DATA
	mov	x1,x20
	bl	str_eq
	mov	x21,x0
.v:	mov	x0,x21
	mov	x1,x19
	mov	x2,0
	bl	verdict
	adr	x0,m_cwd			; (le dossier rendu)
	bl	puts
	adr	x0,msg
	add	x0,x0,MSG_DATA
	bl	puts
	adr	x0,m_nl
	bl	puts
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = 1 reussi / 0 echec, x1 = libelle, x2 = valeur :
; « vfstest : PASS libelle -> valeur »
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

s_vfs		db 'vfs',0
s_bonjour	db 'salut',0
vide		db 0
p_moi		db 'SD0:/bin/vfstest',0
a_enfant_vt	db 'enfant RAM0:/vt',0
a_enfant_bin	db 'enfant SD0:/bin',0
p_vt		db 'RAM0:/vt',0
p_vt_min	db 'ram0:vt',0
p_sous		db 'sous',0
p_sous_pts	db './sous/.',0
p_sous_abs	db 'RAM0:/vt/sous',0
p_pp		db '..',0
p_f		db 'f.txt',0
p_f_pp		db 'sous/../f.txt',0
p_f_abs		db 'RAM0:/vt/f.txt',0
p_absent	db 'XYZ9:/rien',0
p_racine_f	db '/vt/sous/../f.txt',0
p_racine	db '/',0
p_sd		db 'SD:bin',0
p_racine_bin	db '/bin/vfstest',0
p_trop_haut	db 'SD0:/..',0
p_ex_bin	db 'vfstest',0
p_ex_app	db 'welcome',0
p_ex_pkg	db 'SD0:/apps/welcome.app',0
p_ex_sys	db 'SD0:/sys/svc/vfs',0
p_ex_absent	db 'pas_un_programme',0
p_ex_rep	db 'SD0:/bin',0
c_sd0		db 'SD0:/',0
c_vt		db 'RAM0:/vt',0
c_sous		db 'RAM0:/vt/sous',0
c_ram0		db 'RAM0:/',0
c_sd_bin	db 'SD0:/bin',0
c_ex_bin	db 'vfstest',0
c_ex_app	db 'welcome',0
m_sans		db 'vfstest : FAIL service vfs absent',10,0
m_enfant	db 'vfstest : fils, dossier courant : ',0
m_cwd		db '          getcwd : ',0
m_pids		db 'vfstest : pids des fils : ',0
m_et		db ' et ',0
m_pass		db 'vfstest : PASS ',0
m_fail		db 'vfstest : FAIL ',0
m_val		db ' -> ',0
m_nl		db 10,0
m_fin		db 'vfstest : termine, echecs : ',0
l_cwd0		db 'V_GETCWD au depart = SD0:/',0
l_mkdir		db 'V_MKDIR RAM0:/vt',0
l_chdir		db 'V_CHDIR ram0:vt',0
l_cwd_vt	db 'V_GETCWD = RAM0:/vt',0
l_mkdir_rel	db 'V_MKDIR sous (relatif)',0
l_stat_rel	db 'V_STAT ./sous/. : repertoire (attributs)',0
l_chdir_rel	db 'V_CHDIR sous (relatif)',0
l_cwd_sous	db 'V_GETCWD = RAM0:/vt/sous',0
l_chdir_pp	db 'V_CHDIR ..',0
l_cwd_pp	db 'V_GETCWD apres .. = RAM0:/vt',0
l_ouvrir	db 'V_OUVRIR f.txt (relatif, creation)',0
l_pas_sys	db 'V_OUVRIR RAM0: sans ATTR_SYS',0
l_ecrire	db 'FS_ECRIRE direct (proprietaire = ce pid)',0
l_fermer	db 'FS_FERMER direct (appel du proprietaire)',0
l_stat_pp	db 'V_STAT sous/../f.txt : taille 5',0
l_notdir	db 'V_CHDIR f.txt : E_NOTDIR',0
l_absent	db 'V_STAT XYZ9:/rien : E_NOENT',0
l_racine_f	db 'V_STAT /vt/sous/../f.txt (racine de RAM0:) : taille 5',0
l_chdir_racine	db 'V_CHDIR /',0
l_cwd_racine	db 'V_GETCWD apres / = RAM0:/',0
l_chdir_sd	db 'V_CHDIR SD:bin',0
l_cwd_sd	db 'V_GETCWD = SD0:/bin',0
l_racine_bin	db 'V_STAT /bin/vfstest (racine de SD0:) : attributs',0
l_cwd_racine_sd	db 'V_GETCWD apres / = SD0:/',0
l_trop_haut	db 'V_CHDIR SD0:/.. refuse',0
l_ex_bin	db 'V_EXEC vfstest (SD0:/bin) : nom, taille',0
l_ex_app	db 'V_EXEC welcome (SD0:/apps/welcome.app/main) : nom, taille',0
l_ex_pkg	db 'V_EXEC SD0:/apps/welcome.app : nom, taille',0
l_ex_sys	db 'V_EXEC SD0:/sys/svc/vfs : ATTR_SYS, taille',0
l_ex_absent	db 'V_EXEC pas_un_programme : E_NOENT',0
l_ex_rep	db 'V_EXEC SD0:/bin : E_ISDIR',0
l_fils		db 'fils lance dans RAM0:/vt : meme dossier (code du fils)',0
l_fils2		db 'second fils lance dans SD0:/bin (fin du premier oubliee)',0
l_unlink_f	db 'V_UNLINK RAM0:/vt/f.txt',0
l_unlink_sous	db 'V_UNLINK RAM0:/vt/sous',0
l_unlink_vt	db 'V_UNLINK RAM0:/vt',0
		align 8

segment readable writeable
kit_data
		align 8
canal		dd 0
echecs		dd 0
msg		rb MSG_TAILLE
