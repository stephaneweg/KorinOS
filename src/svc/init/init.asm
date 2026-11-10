; init - etape 1 du demarrage en mode services (docs/MICRO-NOYAU.md,
; section 4) : seul module que le noyau lance lui-meme (initramfs). Il lit
; la liste init.txt (un module texte de initramfs : « nom arguments » par
; ligne), lance ces services de demarrage (sd, vfs qui monte SD0: avec
; fatfs), attend que SD0: reponde, donne au noyau les fichiers qu'il attend
; (options.txt, WiFi : appel boot_file, docs/FICHIERS.md etape 5 - le noyau
; ne lit aucun fichier), puis leve le verrou du demarrage (le noyau
; continue : options, pilotes encore internes) et lance l'etape 2 depuis le
; disque : init2 (SD0:/sys/svc/init2).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

LISTE_MAX = 2048
NOMS_MAX = 512				; boot_file(0) : noms attendus
ATTENTE	= 300				; x 100 ms : SD0: doit repondre

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; AppKit (initramfs en mode services)

start:
	bl	kits_open
	adr	x0,s_liste			; la liste des services de demarrage
	adr	x1,liste
	mov	x2,LISTE_MAX-1
	sys	SYS_MOD_LIRE
	tbnz	x0,63,.sans
	adr	x1,liste
	strb	wzr,[x1,x0]
	mov	x19,x1				; ligne courante
.ligne:	ldrb	w0,[x19]
	cbz	w0,.attente
	mov	x20,x19				; fin de ligne -> 0
.f:	ldrb	w0,[x20]
	cbz	w0,.coupe
	cmp	w0,10
	b.eq	.nl
	cmp	w0,13
	b.eq	.nl
	add	x20,x20,1
	b	.f
.nl:	strb	wzr,[x20],1
.coupe:	ldrb	w0,[x19]			; vide, ou commentaire
	cbz	w0,.suiv
	cmp	w0,'#'
	b.eq	.suiv
	mov	x21,x19				; nom, puis arguments
	mov	x22,0
.n:	ldrb	w0,[x21]
	cbz	w0,.lance
	cmp	w0,' '
	b.eq	.args
	add	x21,x21,1
	b	.n
.args:	strb	wzr,[x21],1
	mov	x22,x21
.lance:	mov	x0,x19
	mov	x1,x22
	sys	SYS_MOD_LANCER
	mov	x21,x0
	adr	x0,m_init			; « init : sd -> 5 »
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.suiv:	mov	x19,x20
	b	.ligne
.sans:	adr	x0,m_sans
	bl	puts
.attente:					; SD0: repond ?
	mov	x19,ATTENTE
.w:	adr	x0,s_sd0
	adr	x1,infos
	sys	SYS_STAT
	cbz	x0,.pret
	mov	x0,100
	sys	SYS_SLEEP
	subs	x19,x19,1
	b.ne	.w
	adr	x0,m_echec
	bl	puts
	b	quit
.pret:	bl	boot_files			; options.txt, WiFi -> noyau
	sys	SYS_DEMARRAGE_PRET		; le verrou est leve
	adr	x0,m_pret
	bl	puts
	adr	x0,p_init2			; etape 2, depuis le disque
	mov	x1,0
	sys	SYS_SPAWN
quit:	mov	x0,0
	sys	SYS_EXIT

; Les fichiers que le noyau attend (boot_file(0) : la liste) : chacun est lu
; entier dans un tampon partage temporaire et donne au noyau ; absent : passe.
boot_files:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x0,0
	adr	x1,noms
	mov	x2,NOMS_MAX
	sys	SYS_BOOT_FILE
	cmp	x0,1
	b.le	.out
	adr	x19,noms
.f:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.suiv
	mov	x20,x0
	mov	x1,0				; taille : position de la fin
	mov	x2,2
	sys	SYS_SEEK
	mov	x21,x0
	mov	x0,x20
	mov	x1,0
	mov	x2,0
	sys	SYS_SEEK
	cmp	x21,0
	b.le	.ferme
	mov	x0,x21
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.ferme
	mov	x22,x0
	movl	x23,BUF_VA_BASE
	add	x23,x23,x22,lsl 26		; (BUF_SLOT = 64 Mo)
	mov	x0,x20
	mov	x1,x23
	mov	x2,x21
	sys	SYS_READ
	cmp	x0,x21
	b.ne	.rend
	mov	x0,x19
	mov	x1,x23
	mov	x2,x21
	sys	SYS_BOOT_FILE
	mov	x24,x0
	adr	x0,m_fichier			; « init : SD0:/etc/options.txt -> 0 »
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.rend:	mov	x0,x22
	sys	SYS_BUF_FREE
.ferme:	mov	x0,x20
	sys	SYS_CLOSE
.suiv:	ldrb	w0,[x19],1			; nom suivant
	cbnz	w0,.suiv
	b	.f
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_liste	db 'init.txt',0
s_sd0	db 'SD0:/',0
p_init2	db 'SD0:/sys/svc/init2',0
m_init	db 'init : ',0
m_fichier db 'init : pour le noyau ',0
m_fleche db ' -> ',0
m_nl	db 10,0
m_sans	db 'init : pas de liste init.txt',10,0
m_pret	db 'init : SD0: pret, verrou du demarrage leve',10,0
m_echec	db 'init : SD0: ne repond pas',10,0
	align 8

segment readable writeable
kit_data
	align 8
infos	rb 16
liste	rb LISTE_MAX
noms	rb NOMS_MAX
