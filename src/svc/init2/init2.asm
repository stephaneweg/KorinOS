; init2 - etape 2 et 3 du demarrage (docs/FEUILLE-DE-ROUTE.md, section 1),
; lance par init une fois SD0: servi. Le noyau ne lance plus rien lui-meme.
;  * etape 2 : busd (le bus d'evenements, docs/BUS.md), hwmanager
;    (decouverte du materiel), hidmanager (les entrees), usbd (le bus USB), le shell (Elegant :
;    SD0:/sys/elegant, qui lit shell= de SD0:/etc/system.ini), puis les
;    services de SD0:/etc/services.txt ;
;  * etape 3 : quand Elegant repond (le service « ws »), les programmes de
;    SD0:/etc/startup.txt (sans ce fichier : Bienvenue) ;
;  * avant tout cela, s'il y a des paquets etages (SD0:/var/pkg/stage,
;    docs/PAQUETS.md) : SD0:/bin/pkg commit, qu'il attend en servant ses
;    demandes (ses kits, le redemarrage si le noyau a change) ;
;  * ensuite il veille : un service qu'il a lance et qui s'arrete sur une
;    erreur (code de sortie non nul) est relance, RELANCES_MAX fois au plus.
; Il s'enregistre sous « init2 » (prevenu des fins de process : SVF_FINS).
;  * op INIT_OP_LAUNCH (docs/FICHIERS.md, etape 4) : lance un programme de
;    SD0:/sys pour un process sans droits (svctest, le terminal : AppKit y
;    passe tout seul) ; donnees : chemin resolu par vfs (« SD0:/sys/... »),
;    0, arguments, 0 -> pid. Refus : chemin hors de SD0:/sys (E_PERM).
;    Plusieurs instances permises (fatfs par volume...) : un second service
;    du meme nom echoue de lui-meme (svc_register : E_BUSY). Le fils a init2
;    pour pere : ses entrees / sorties sont la console (journal du noyau).
;    Chaque demande est notee au journal (pid et nom du demandeur, chemin,
;    resultat). Pas encore de liste blanche ni d'authentification (fin du
;    projet).
;  * apres startup.txt : init2 se relance avec l'argument « preload », qui
;    precharge les executables de SD0:/etc/preload.txt (un chemin par ligne,
;    « # » : commentaire ; SYS_PRELOAD d'AppKit, puis le noyau :
;    src/kernel/preload.asm) et s'arrete. Dans un process a part : init2
;    continue de servir pendant ce temps (un gros executable prend du temps
;    a lire). Option « no_preload » : rien n'est precharge.
;  * op INIT_OP_KIT_LOAD (docs/FICHIERS.md, etape 5) : inscrit un kit pour
;    un process sans droits (kits_open d'AppKit y passe tout seul) ;
;    donnees : le nom (« netkit ») -> 0 ou erreur. init2 le lit lui-meme
;    (f_kit_load : SD0:/lib/<nom>.so, marque ATTR_LIB par vfs) et le noyau
;    l'inscrit (kit_register : init2 a les droits) ; le demandeur fait
;    ensuite lib_open. Chaque demande est notee au journal.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

BLK_DMA		= 5			; (inc/blk.inc)
BLK_UHS		= 8
include '../../inc/fs.inc'

SURV_MAX	= 24			; services surveilles
SV_PID		= 0
SV_RELANCES	= 8
SV_LIGNE	= 16			; « chemin [arguments] »
SV_TAILLE	= 16 + 128
RELANCES_MAX	= 3
LISTE_MAX	= 4096
PLIST_MAX	= 64			; proclist : entrees de 64 o
ATTENTE_WS	= 20000			; ms : le shell doit repondre
HEURE_TXT	= 48
HEURE_MINUTES	= 30			; sauvegarde de l'heure
COMMIT_MAX	= 300000		; ms : pkg commit, au demarrage

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; « preload » : le prechargement
	bl	kits_open
	cbz	x19,.init2
	mov	x0,x19
	adr	x1,s_preload
	bl	str_egal
	cbz	x0,.init2
	bl	precharger
	b	quit
.init2:	adr	x0,s_init2			; prevenu des fins de process
	mov	x1,SVF_FINS
	sys	SYS_SVC_REGISTER
	adr	x1,canal
	str	x0,[x1]
	bl	charger_heure			; l'horloge, puis les chemins virtuels,
	bl	charger_chemins			; avant tout le reste
	bl	sd_dma_request
	bl	commit_boot			; les paquets etages, avant tout le reste
	adr	x0,l_bus			; le bus d'evenements, avant ceux qui y
	mov	x1,1				; publient (docs/BUS.md)
	bl	lancer_ligne
	adr	x0,l_hw				; etape 2 : hwmanager, le shell
	mov	x1,1
	bl	lancer_ligne
	adr	x0,l_hid
	mov	x1,1
	bl	lancer_ligne
	adr	x0,l_usbd			; le bus USB (ses pilotes : hwmanager)
	mov	x1,1
	bl	lancer_ligne
	adr	x0,l_shell
	mov	x1,1
	bl	lancer_ligne
	adr	x0,p_services			; les services de services.txt
	mov	x1,1
	bl	lancer_fichier
	adr	x0,s_ws				; etape 3 : le shell repond ?
	mov	x1,ATTENTE_WS
	sys	SYS_SVC_OPEN
	tbz	x0,63,.ws
	adr	x0,m_sans_ws
	bl	puts
.ws:	adr	x0,p_startup			; les programmes de startup.txt
	mov	x1,0
	bl	lancer_fichier
	cbnz	x0,.pre
	adr	x0,l_bienvenue			; (sans le fichier : Bienvenue)
	mov	x1,0
	bl	lancer_ligne
.pre:	adr	x0,l_preload			; executables precharges : dans un
	adr	x1,s_preload			; process a part (init2 preload)
	sys	SYS_SPAWN
.veille:					; relancer ce qui tombe
	adr	x0,canal
	ldr	x0,[x0]
	tbnz	x0,63,quit
	adr	x1,msg
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbnz	x0,.req
	adr	x1,veilles			; toutes les 30 min : l'heure sur la carte
	ldr	x0,[x1]
	add	x0,x0,1
	cmp	x0,HEURE_MINUTES
	b.hs	.sauve
	str	x0,[x1]
	b	.veille
.sauve:	str	xzr,[x1]
	bl	sauver_heure
	b	.veille
.req:	bl	requete
	b	.veille
quit:	mov	x0,0
	sys	SYS_EXIT

; x0 = numero de la requete de msg : la sert
requete:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,msg
	ldr	x2,[x1]
	movl	x3,SVC_OP_FIN
	cmp	x2,x3
	b.ne	.op
	ldp	x0,x1,[x1,8]			; pid, code de sortie
	bl	fin_de
	b	.out
.op:	cmp	x2,INIT_OP_KIT_LOAD
	b.eq	.kit
	cmp	x2,INIT_OP_REBOOT
	b.eq	.reboot
	mov	x0,-22				; E_INVAL
	cmp	x2,INIT_OP_LAUNCH
	b.ne	.reply
	bl	launch_request
	b	.reply
.reboot:
	bl	sauver_heure
	mov	x0,x19
	bl	reboot_request			; (ne revient pas si tout va bien ;
	b	.out				;  la reponse est deja partie)
.kit:	bl	kit_request
.reply:	adr	x1,msg
	str	x0,[x1]
	str	wzr,[x1,40]
	mov	x0,x19
	sys	SYS_SVC_REPLY
.out:	ldr	x19,[sp],16
	epilogue

; Des paquets etages (docs/PAQUETS.md) : SD0:/bin/pkg commit, avant tout le
; reste ; init2 le sert pendant ce temps, COMMIT_MAX ms au plus. pkg absent ou
; qui echoue : le demarrage continue, les paquets restent etages.
commit_boot:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	adr	x0,p_stage
	mov	x1,sp
	sys	SYS_STAT
	tbnz	x0,63,.out
	adr	x0,l_pkg
	adr	x1,s_commit
	sys	SYS_SPAWN
	tbnz	x0,63,.out
	mov	x19,x0
	sys	SYS_TIME_MS
	mov	x20,x0
.l:	adr	x0,canal
	ldr	x0,[x0]
	tbnz	x0,63,.out
	adr	x1,msg
	mov	x2,1000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,.out
	cbz	x0,.t
	adr	x1,msg				; la fin de pkg ?
	ldr	x2,[x1]
	movl	x3,SVC_OP_FIN
	cmp	x2,x3
	b.ne	.serve
	ldr	x2,[x1,8]
	cmp	x2,x19
	b.eq	.out
.serve:	bl	requete
.t:	sys	SYS_TIME_MS
	sub	x0,x0,x20
	movl	x1,COMMIT_MAX
	cmp	x0,x1
	b.lo	.l
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; x0 = « chemin [arguments] » (modifiable), x1 = 1 : a surveiller
; -> x0 = pid ou erreur
; La carte passe en DMA : maintenant seulement, le noyau a lu les options
; (no_sd_dma) ; le demarrage lui-meme reste en PIO
sd_dma_request:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	adr	x0,s_blk_sd
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x0,s_sd_uhs			; (fichier d'essai : le 1,8 V, avant le DMA)
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.dma
	sys	SYS_CLOSE
	mov	x1,BLK_UHS
	str	x1,[sp]
	mov	x1,sp
	mov	x0,x19
	sys	SYS_SVC_CALL
.dma:	mov	x0,x19
	mov	x1,BLK_DMA
	str	x1,[sp]
	mov	x1,sp
	sys	SYS_SVC_CALL
.out:	add	sp,sp,MSG_TAILLE
	ldr	x19,[sp],16
	epilogue

s_blk_sd	db 'blk.sd0',0
s_sd_uhs	db 'SD0:/etc/sd_uhs',0
		align 4

lancer_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,ligne			; copie : la coupure ne touche pas
	mov	x1,x19				; l'original (relances)
	mov	x2,128
	bl	copie
	adr	x21,ligne
	mov	x22,0				; arguments
	mov	x0,x21
.c:	ldrb	w1,[x0]
	cbz	w1,.go
	cmp	w1,' '
	b.eq	.cut
	add	x0,x0,1
	b	.c
.cut:	strb	wzr,[x0],1
	mov	x22,x0
.go:	mov	x0,x21
	mov	x1,x22
	sys	SYS_SPAWN
	mov	x21,x0
	adr	x0,m_init2			; « init2 : SD0:/sys/elegant -> 7 »
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	tbnz	x21,63,.out
	cbz	x20,.out
	adr	x0,surv				; a surveiller : une place libre
	mov	x1,0
.s:	cmp	x1,SURV_MAX
	b.hs	.out
	ldr	x2,[x0,SV_PID]
	cbz	x2,.place
	add	x0,x0,SV_TAILLE
	add	x1,x1,1
	b	.s
.place:	str	x21,[x0,SV_PID]
	str	xzr,[x0,SV_RELANCES]
	add	x0,x0,SV_LIGNE
	mov	x1,x19
	mov	x2,128
	bl	copie
.out:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; SD0:/etc/lastclock : « secondes_UTC decalage » -> l'horloge du noyau. Sans
; reseau, l'heure repart de la derniere sauvegarde : jamais en arriere
; (docs/FICHIERS.md, dates). La carte n'a pas d'horloge qui survive a l'arret.
charger_heure:
	prologue
	str	x19,[sp,-16]!
	adr	x0,p_lastclock
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,heure_txt
	mov	x2,HEURE_TXT-1
	sys	SYS_READ
	cmp	x0,0
	csel	x2,x0,xzr,gt
	adr	x1,heure_txt
	strb	wzr,[x1,x2]
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,heure_txt
	bl	lire_dec
	mov	x19,x1
	bl	lire_dec
	mov	x0,x19
	sys	SYS_CLOCK_SET
.out:	ldr	x19,[sp],16
	epilogue

; l'horloge -> SD0:/etc/lastclock (rien tant que l'heure n'est pas connue)
sauver_heure:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0
	sys	SYS_CLOCK_GET
	mov	x19,x0
	movl	x1,1000000000
	cmp	x0,x1
	b.lo	.out
	mov	x0,1
	sys	SYS_CLOCK_GET
	mov	x20,x0
	mov	x0,x19
	adr	x1,heure_txt
	bl	ecrire_dec
	mov	w0,' '
	strb	w0,[x1],1
	mov	x0,x20
	bl	ecrire_dec
	mov	w0,10
	strb	w0,[x1],1
	adr	x0,heure_txt
	sub	x20,x1,x0
	adr	x0,p_lastclock
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,heure_txt
	mov	x2,x20
	sys	SYS_WRITE
	mov	x0,x19
	sys	SYS_CLOSE
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> x1 = nombre (signe), x0 apres lui
lire_dec:
	mov	x1,0
	mov	x2,0
.b:	ldrb	w3,[x0]
	cmp	w3,' '
	b.ne	.s
	add	x0,x0,1
	b	.b
.s:	cmp	w3,'-'
	b.ne	.c
	mov	x2,1
	add	x0,x0,1
.c:	ldrb	w3,[x0]
	sub	w3,w3,'0'
	cmp	w3,9
	b.hi	.f
	mov	x4,10
	mul	x1,x1,x4
	add	x1,x1,x3
	add	x0,x0,1
	b	.c
.f:	cbz	x2,.r
	neg	x1,x1
.r:	ret

; x0 = nombre (signe), x1 = sortie -> x1 apres le dernier chiffre
ecrire_dec:
	tbz	x0,63,.p
	mov	w2,'-'
	strb	w2,[x1],1
	neg	x0,x0
.p:	mov	x2,x1
	mov	x3,10
.d:	udiv	x4,x0,x3
	msub	x5,x4,x3,x0
	add	w5,w5,'0'
	strb	w5,[x1],1
	mov	x0,x4
	cbnz	x0,.d
	sub	x3,x1,1				; les chiffres sont a l'envers
.i:	cmp	x2,x3
	b.hs	.r
	ldrb	w4,[x2]
	ldrb	w5,[x3]
	strb	w5,[x2],1
	strb	w4,[x3]
	sub	x3,x3,1
	b	.i
.r:	ret

; SD0:/etc/paths.txt, une ligne par chemin virtuel, « # » : commentaire :
; « /gcc = SD0:/gcc » -> VFS_PATHMAP a vfs (docs/FICHIERS.md, chemins virtuels)
charger_chemins:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,s_vfs
	mov	x1,5000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x22,x0
	adr	x0,p_paths
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,liste
	mov	x2,LISTE_MAX-1
	sys	SYS_READ
	cmp	x0,0
	csel	x21,x0,xzr,gt
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x19,liste
	strb	wzr,[x19,x21]
.ligne:	bl	chemins_blancs
	ldrb	w0,[x19]
	cbz	w0,.out
	cmp	w0,'#'
	b.eq	.fin
	cmp	w0,13
	b.eq	.fin
	cmp	w0,10
	b.eq	.fin
	adr	x20,msg
	add	x20,x20,MSG_DATA
	bl	chemins_mot
	bl	chemins_blancs
	ldrb	w0,[x19]
	cmp	w0,'='
	b.ne	.fin
	add	x19,x19,1
	bl	chemins_blancs
	bl	chemins_mot
	adr	x1,msg
	mov	x0,VFS_PATHMAP
	str	x0,[x1]
	sub	x0,x20,x1
	sub	x0,x0,MSG_DATA
	str	w0,[x1,MSG_LEN]
	mov	x0,x22
	sys	SYS_SVC_CALL
	tbz	x0,63,.fin
	adr	x0,m_paths
	bl	puts
.fin:	ldrb	w0,[x19]			; ligne suivante
	cbz	w0,.out
	add	x19,x19,1
	cmp	w0,10
	b.ne	.fin
	b	.ligne
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x19 -> premier caractere ni espace ni tabulation
chemins_blancs:
	ldrb	w0,[x19]
	cmp	w0,' '
	b.eq	.s
	cmp	w0,9
	b.ne	.r
.s:	add	x19,x19,1
	b	chemins_blancs
.r:	ret

; copie un mot de x19 vers x20 (dans msg, termine par 0) ; x19 et x20 avances
chemins_mot:
	adr	x2,msg
	add	x2,x2,512-2
.c:	ldrb	w0,[x19]
	cbz	w0,.fin
	cmp	w0,' '
	b.eq	.fin
	cmp	w0,9
	b.eq	.fin
	cmp	w0,'='
	b.eq	.fin
	cmp	w0,13
	b.eq	.fin
	cmp	w0,10
	b.eq	.fin
	add	x19,x19,1
	cmp	x20,x2
	b.hs	.c
	strb	w0,[x20],1
	b	.c
.fin:	strb	wzr,[x20],1
	ret

; (init2 preload) SD0:/etc/preload.txt : chaque chemin est precharge
; (SYS_PRELOAD) ; au journal : un echec par ligne, puis le total et la duree
precharger:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x0,0				; le noyau precharge-t-il ?
	sys	SYS_PRELOAD
	tbnz	x0,63,.out			; (option no_preload)
	adr	x0,p_preload
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,liste
	mov	x2,LISTE_MAX-1
	sys	SYS_READ
	cmp	x0,0
	csel	x21,x0,xzr,gt
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,liste
	strb	wzr,[x0,x21]
	sys	SYS_TIME_MS
	mov	x23,x0
	mov	x24,0				; images prechargees
	adr	x19,liste			; ligne courante
.ligne:	ldrb	w0,[x19]
	cbz	w0,.fin
	mov	x21,x19				; fin de ligne -> 0
.f:	ldrb	w0,[x21]
	cbz	w0,.coupe
	cmp	w0,10
	b.eq	.nl
	cmp	w0,13
	b.eq	.nl
	add	x21,x21,1
	b	.f
.nl:	strb	wzr,[x21],1
.coupe:	sub	x0,x21,1			; espaces de fin
.sp:	cmp	x0,x19
	b.lo	.vu
	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.spz
	cmp	w1,9
	b.eq	.spz
	cbnz	w1,.vu
.spz:	strb	wzr,[x0]
	sub	x0,x0,1
	b	.sp
.vu:	ldrb	w0,[x19]			; espaces de debut
	cmp	w0,' '
	b.eq	.deb
	cmp	w0,9
	b.ne	.lu
.deb:	add	x19,x19,1
	b	.vu
.lu:	cbz	w0,.suiv			; vide, ou commentaire
	cmp	w0,'#'
	b.eq	.suiv
	sys	SYS_TIME_MS
	mov	x25,x0
	mov	x0,x19
	sys	SYS_PRELOAD
	tbz	x0,63,.ok
	mov	x22,x0				; « init2 : preload SD0:/x -> -2 »
	adr	x0,m_preload
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	b	.suiv
.ok:	add	x24,x24,1
	sys	SYS_TIME_MS			; un chargement qui se voit : notifie
	sub	x0,x0,x25
	cmp	x0,PRELOAD_NOTIFY_MS
	b.lo	.suiv
	mov	x0,x19
	bl	preload_notify
.suiv:	mov	x19,x21
	b	.ligne
.fin:	adr	x0,m_preload_n			; « init2 : preload : 2 en 85 ms »
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_preload_t
	bl	puts
	sys	SYS_TIME_MS
	sub	x0,x0,x23
	bl	print_dec
	adr	x0,m_preload_ms
	bl	puts
.out:	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

PRELOAD_NOTIFY_MS = 300			; (fasmarm, hello... : pas de notification)
include '../../inc/notify.inc'
include '../../inc/preload_notify.inc'

; x0, x1 = chaines -> x0 = 1 si egales
str_egal:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.non
	cbnz	w2,.c
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = pid termine, x1 = son code de sortie : un service surveille qui
; s'arrete sur une erreur est relance (RELANCES_MAX fois)
fin_de:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	adr	x19,surv
	mov	x20,0
.s:	cmp	x20,SURV_MAX
	b.hs	.out
	ldr	x1,[x19,SV_PID]
	cmp	x1,x0
	b.eq	.trouve
	add	x19,x19,SV_TAILLE
	add	x20,x20,1
	b	.s
.trouve:
	str	xzr,[x19,SV_PID]		; il ne tourne plus
	cbz	x21,.out			; arret normal (code 0) : rien a faire
	ldr	x0,[x19,SV_RELANCES]
	cmp	x0,RELANCES_MAX
	b.hs	.abandon
	add	x0,x0,1
	mov	x22,x0
	adr	x0,m_relance
	bl	puts
	add	x0,x19,SV_LIGNE
	mov	x1,0				; (relance : copiee, sans nouvelle place)
	bl	lancer_ligne
	tbnz	x0,63,.out
	str	x0,[x19,SV_PID]
	str	x22,[x19,SV_RELANCES]
	b	.out
.abandon:
	adr	x0,m_abandon
	bl	puts
	add	x0,x19,SV_LIGNE
	bl	puts
	adr	x0,m_nl
	bl	puts
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fichier (une ligne par programme), x1 = 1 : a surveiller
; -> x0 = 1 si le fichier existe
lancer_fichier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.absent
	mov	x19,x0
	adr	x1,liste
	mov	x2,LISTE_MAX-1
	sys	SYS_READ
	cmp	x0,0
	csel	x21,x0,xzr,gt
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,liste
	strb	wzr,[x0,x21]
	adr	x19,liste			; ligne courante
.ligne:	ldrb	w0,[x19]
	cbz	w0,.fin
	mov	x21,x19				; fin de ligne -> 0
.f:	ldrb	w0,[x21]
	cbz	w0,.coupe
	cmp	w0,10
	b.eq	.nl
	cmp	w0,13
	b.eq	.nl
	add	x21,x21,1
	b	.f
.nl:	strb	wzr,[x21],1
.coupe:	ldrb	w0,[x19]			; vide, ou commentaire
	cbz	w0,.suiv
	cmp	w0,'#'
	b.eq	.suiv
	mov	x0,x19
	mov	x1,x20
	bl	lancer_ligne
.suiv:	mov	x19,x21
	b	.ligne
.fin:	mov	x0,1
	b	.out
.absent:
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (msg) INIT_OP_LAUNCH, pour le process +44 -> x0 = pid ou erreur ; une
; ligne au journal : « init2 : launch par 12 terminal : SD0:/sys/svc/echo -> 14 »
launch_request:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,msg
	strb	wzr,[x19,511]			; (donnees terminees)
	add	x20,x19,48			; chemin
	mov	x0,x20				; arguments : apres son 0
.e:	ldrb	w1,[x0],1
	cbnz	w1,.e
	mov	x21,x0
	ldrb	w1,[x21]
	cmp	w1,0
	csel	x21,xzr,x21,eq
	adr	x0,launch_path			; (msg sert a la reponse)
	mov	x1,x20
	mov	x2,256
	bl	copie
	adr	x20,launch_path
	cbz	x21,.na
	adr	x0,launch_args
	mov	x1,x21
	mov	x2,256
	bl	copie
	adr	x21,launch_args
.na:	ldr	w0,[x19,44]			; le demandeur (nom : proclist)
	mov	x22,x0
	bl	proc_list
	mov	x0,x22
	bl	name_of_pid
	mov	x19,x0
	mov	x0,x20				; sous SD0:/sys/, sans « .. » ?
	bl	sys_path_ok
	mov	x1,-1				; E_PERM
	cbz	x0,.fait
	mov	x0,x20				; AppKit : droits (vfs : ATTR_SYS ; le
	mov	x1,x21				; noyau : init2 les a)
	sys	SYS_SPAWN
	mov	x1,x0
.fait:	mov	x21,x1
	adr	x0,m_launch			; au journal
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_esp
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_dp
	bl	puts
	mov	x0,x20
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (msg) INIT_OP_KIT_LOAD, pour le process +44 -> x0 = 0 ou erreur ; une
; ligne au journal : « init2 : kit_load par 12 monitor : netkit -> 0 »
; x0 = numero de la requete INIT_OP_REBOOT (a bit 0 : tryboot) : repond,
; laisse 500 ms aux dernieres ecritures (journal, fichiers fermes : fatfs
; ecrit tout de suite, sans cache differe), puis le bloc PM du Pi 4
; (0xFE100000) : chien de garde a 10 tics et reinitialisation complete
; (docs/RESEAU.md 5.3). tryboot : le firmware lira tryboot.txt une fois. Arret
; (bit 1, INIT_REBOOT_HALT) : la meme reinitialisation, la partition 63 dans
; PM_RSTS dit au firmware de s'arreter la au lieu de relancer le systeme.
reboot_request:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x9,msg
	ldr	x20,[x9,8]			; drapeaux
	adr	x0,m_reboot			; au journal
	bl	puts
	adr	x9,msg
	ldr	w0,[x9,44]
	bl	print_dec
	tbz	x20,0,.nl
	adr	x0,m_tryboot
	bl	puts
.nl:	adr	x0,m_nl
	bl	puts
	adr	x1,msg				; la reponse d'abord
	str	xzr,[x1]
	str	wzr,[x1,40]
	mov	x0,x19
	sys	SYS_SVC_REPLY
	mov	x0,500
	sys	SYS_SLEEP
	tbz	x20,0,.reset
	adr	x0,fw_tryboot			; SET_REBOOT_FLAGS(1) : tryboot
	sys	SYS_FW_CALL
.reset:	movl	x0,0xFE100000			; bloc PM
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	tbnz	x0,63,.ko
	tbz	x20,1,.wdog
	ldr	w2,[x0,0x20]			; PM_RSTS : partition 63 (bits pairs)
	movl	w3,0x5A000555
	orr	w2,w2,w3
	str	w2,[x0,0x20]
.wdog:	movl	w2,0x5A00000A			; PM_WDOG = mot de passe | 10
	str	w2,[x0,0x24]
	ldr	w2,[x0,0x1C]			; PM_RSTC : reinitialisation complete
	and	w2,w2,0xFFFFFFCF
	movl	w3,0x5A000020
	orr	w2,w2,w3
	str	w2,[x0,0x1C]
	mov	x0,3000
	sys	SYS_SLEEP
.ko:	adr	x0,m_reboot_ko
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

kit_request:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,msg
	strb	wzr,[x19,511]			; (donnees terminees)
	adr	x0,launch_path			; le nom (msg sert a la reponse)
	add	x1,x19,48
	mov	x2,32
	bl	copie
	adr	x20,launch_path
	ldr	w22,[x19,44]			; le demandeur
	bl	proc_list
	mov	x0,x22
	bl	name_of_pid
	mov	x19,x0
	mov	x0,x20				; lu par vfs, inscrit par le noyau
	bl	f_kit_load			; (le nom est verifie par AppKit)
	mov	x21,x0
	adr	x0,m_kit			; au journal
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_esp
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_dp
	bl	puts
	mov	x0,x20
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Remplit plist (proclist) -> x0 = nombre d'entrees (dans plist_n)
proc_list:
	adr	x0,plist
	mov	x1,PLIST_MAX
	sys	SYS_PROCLIST
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,plist_n
	str	x0,[x1]
	ret

; x0 = pid -> x0 = son nom (plist), « ? » s'il est absent
name_of_pid:
	adr	x1,plist
	adr	x2,plist_n
	ldr	x2,[x2]
.p:	cbz	x2,.non
	ldr	x3,[x1]
	cmp	x3,x0
	b.eq	.oui
	add	x1,x1,64
	sub	x2,x2,1
	b	.p
.oui:	add	x0,x1,24
	ret
.non:	adr	x0,m_inconnu
	ret

; x0 = chemin -> x0 = 1 s'il commence par « SD0:/sys/ » sans composant « .. »
sys_path_ok:
	adr	x1,p_sys
.c:	ldrb	w2,[x1],1
	cbz	w2,.dd
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	b	.non
.dd:	ldrb	w2,[x0]				; « .. » apres un « / » ?
	cbz	w2,.oui
	ldrb	w3,[x0,-1]
	cmp	w3,'/'
	b.ne	.s
	cmp	w2,'.'
	b.ne	.s
	ldrb	w3,[x0,1]
	cmp	w3,'.'
	b.eq	.non
.s:	add	x0,x0,1
	b	.dd
.oui:	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = destination, x1 = source, x2 = taille (fin comprise) : copie bornee
copie:
	sub	x2,x2,1
.c:	cbz	x2,.f
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	cbz	w3,.r
	sub	x2,x2,1
	b	.c
.f:	strb	wzr,[x0]
.r:	ret

s_init2		db 'init2',0
s_ws		db 'ws',0
s_vfs		db 'vfs',0
p_paths		db 'SD0:/etc/paths.txt',0
p_lastclock	db 'SD0:/etc/lastclock',0
m_paths		db 'init2 : ligne de paths.txt refusee',10,0
l_bus		db 'SD0:/sys/svc/busd',0
l_hw		db 'SD0:/sys/svc/hwmanager',0
l_hid		db 'SD0:/sys/svc/hidmanager',0
l_usbd		db 'SD0:/sys/svc/usbd',0
l_shell		db 'SD0:/sys/elegant',0
l_bienvenue	db 'SD0:/apps/welcome.app/main',0
p_services	db 'SD0:/etc/services.txt',0
p_startup	db 'SD0:/etc/startup.txt',0
p_preload	db 'SD0:/etc/preload.txt',0
p_stage		db 'SD0:/var/pkg/stage',0
l_pkg		db 'SD0:/bin/pkg',0
s_commit	db 'commit',0
l_preload	db 'SD0:/sys/svc/init2',0
s_preload	db 'preload',0
m_preload	db 'init2 : preload ',0
m_preload_n	db 'init2 : preload : ',0
m_preload_t	db ' image(s) en ',0
m_preload_ms	db ' ms',10,0
m_reboot	db 'init2 : red',0xC3,0xA9,'marrage demand',0xC3,0xA9,' par le pid ',0
m_tryboot	db ' (tryboot : tryboot.txt une fois)',0
m_reboot_ko	db 'init2 : le red',0xC3,0xA9,'marrage n',0x27,'a pas eu lieu',10,0
m_init2		db 'init2 : ',0
m_fleche	db ' -> ',0
m_nl		db 10,0
m_launch	db 'init2 : launch par ',0
m_kit		db 'init2 : kit_load par ',0
m_esp		db ' ',0
m_dp		db ' : ',0
m_inconnu	db '?',0
p_sys		db 'SD0:/sys/',0
m_sans_ws	db 'init2 : le shell ne repond pas, startup.txt quand meme',10,0
m_relance	db 'init2 : relance d''un service arrete sur une erreur',10,0
m_abandon	db 'init2 : abandon (trop de relances) : ',0
		align 8

segment readable writeable
canal		dd -1
kit_data
		align 16
fw_tryboot	dw 28, 0, 0x00038064, 4, 4, 1, 0	; SET_REBOOT_FLAGS(1)
		align 8
surv		rb SURV_MAX * SV_TAILLE
ligne		rb 128
msg		rb 512
heure_txt	rb HEURE_TXT
veilles		rb 8
liste		rb LISTE_MAX
plist_n		dd 0
launch_path	rb 256
launch_args	rb 256
plist		rb PLIST_MAX * 64
