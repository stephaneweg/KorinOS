; kittest - essai du chargement des kits hors du noyau (docs/FICHIERS.md,
; etape 5) : un programme sans droits fait inscrire NetKit par init2
; (f_kit_load -> INIT_OP_KIT_LOAD), puis l'ouvre (lib_open) ; 150
; chargements de suite ne doivent rien laisser (tampons partages rendus :
; memoire libre et tas inchanges, pas de table de tampons pleine) ; refus :
; nom invalide, kit absent, kit_register et boot_file sans droits ; anciens
; appels de fichiers du noyau : E_NOSYS.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

N_CHARGES = 150

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,p_netkit			; avant : en memoire ou non
	mov	x1,1
	mov	x8,SYS_LIB_OPEN
	svc	0
	mov	x19,x0
	adr	x0,m_avant
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,p_netkit			; par init2 (sans droits)
	bl	f_kit_load
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_charge
	bl	verdict
	adr	x0,p_netkit
	mov	x1,1
	mov	x8,SYS_LIB_OPEN
	svc	0
	mov	x2,x0
	cmp	x0,0
	cset	x0,gt
	adr	x1,l_ouvre
	bl	verdict
	bl	libre				; memoire avant les chargements
	mov	x20,x0
	bl	tas_init2
	mov	x22,x0
	mov	x21,0
.l:	cmp	x21,N_CHARGES
	b.hs	.fin_l
	adr	x0,p_netkit
	bl	f_kit_load
	cbnz	x0,.fin_l
	add	x21,x21,1
	b	.l
.fin_l:	mov	x2,x21
	cmp	x21,N_CHARGES
	cset	x0,eq
	adr	x1,l_boucle
	bl	verdict
	bl	libre				; difference (Ko) : < 64
	sub	x2,x20,x0
	cmp	x2,64
	cset	x0,lt
	adr	x1,l_memoire
	bl	verdict
	bl	tas_init2
	sub	x2,x0,x22
	cmp	x2,0
	cset	x0,eq
	adr	x1,l_tas
	bl	verdict
	adr	x0,p_absent			; refus
	bl	f_kit_load
	mov	x2,x0
	cmn	x0,2
	cset	x0,eq
	adr	x1,l_absent
	bl	verdict
	adr	x0,p_mauvais
	bl	f_kit_load
	mov	x2,x0
	cmn	x0,22
	cset	x0,eq
	adr	x1,l_mauvais
	bl	verdict
	adr	x0,p_uikit			; kit_register : droits de SD0:/sys
	adr	x1,tampon
	mov	x2,4096
	mov	x8,SYS_KIT_REGISTER
	svc	0
	mov	x2,x0
	cmn	x0,1
	cset	x0,eq
	adr	x1,l_register
	bl	verdict
	adr	x0,p_options			; boot_file : init seulement
	adr	x1,tampon
	mov	x2,16
	mov	x8,SYS_BOOT_FILE
	svc	0
	mov	x2,x0
	cmn	x0,1
	cset	x0,eq
	adr	x1,l_boot
	bl	verdict
	adr	x0,p_options			; ancien open du noyau
	mov	x1,O_READ
	mov	x8,SYS_OPEN
	svc	0
	mov	x2,x0
	cmn	x0,38
	cset	x0,eq
	adr	x1,l_nosys
	bl	verdict
	mov	x0,5				; descripteur 5 (ni AppKit ni stdio)
	adr	x1,tampon
	mov	x2,16
	sys	SYS_READ
	mov	x2,x0
	cmn	x0,9
	cset	x0,eq
	adr	x1,l_badf
	bl	verdict
	adr	x0,m_bilan
	bl	puts
	adr	x0,n_pass
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_bilan2
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_bilan3
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	sys	SYS_EXIT

; -> x0 = memoire libre (Ko)
libre:
	prologue
	adr	x0,infos
	sys	SYS_SYSINFO
	adr	x0,infos
	ldr	x0,[x0,8]
	epilogue

; -> x0 = tas d'init2 (Ko, proclist), -1 s'il est absent
tas_init2:
	prologue
	adr	x0,plist
	mov	x1,64
	sys	SYS_PROCLIST
	adr	x1,plist
.p:	cmp	x0,0
	b.le	.non
	ldr	w2,[x1,24]			; « init », « 2 »
	movl	w3,0x74696E69
	cmp	w2,w3
	b.ne	.s
	ldrh	w2,[x1,28]
	cmp	w2,0x32
	b.ne	.s
	ldr	x0,[x1,16]
	b	.out
.s:	add	x1,x1,64
	sub	x0,x0,1
	b	.p
.non:	mov	x0,-1
.out:	epilogue

; x0 = 1 si reussi, x1 = libelle, x2 = valeur : « kittest : PASS libelle (valeur) »
verdict:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	adr	x1,n_pass
	adr	x2,m_pass
	cbnz	x0,.c
	adr	x1,n_fail
	adr	x2,m_fail
.c:	ldr	x3,[x1]
	add	x3,x3,1
	str	x3,[x1]
	mov	x21,x2
	adr	x0,m_kt
	bl	puts
	mov	x0,x21
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_par
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_par2
	bl	puts
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

p_netkit	db 'netkit',0
p_absent	db 'absent',0
p_mauvais	db '../uikit',0
p_uikit		db 'uikit',0
p_options	db 'SD0:/etc/options.txt',0
m_avant		db 'kittest : lib_open(netkit) avant : ',0
m_kt		db 'kittest : ',0
m_pass		db 'PASS ',0
m_fail		db 'FAIL ',0
m_par		db ' (',0
m_par2		db ')',10,0
m_nl		db 10,0
m_bilan		db 'kittest : ',0
m_bilan2	db ' PASS, ',0
m_bilan3	db ' FAIL',10,0
l_charge	db 'f_kit_load(netkit) par init2',0
l_ouvre		db 'lib_open(netkit) ensuite',0
l_boucle	db 'chargements de suite',0
l_memoire	db 'memoire libre perdue (Ko)',0
l_tas		db 'tas d''init2 (Ko de plus)',0
l_absent	db 'kit absent : E_NOENT',0
l_mauvais	db 'nom invalide : E_INVAL',0
l_register	db 'kit_register sans droits : E_PERM',0
l_boot		db 'boot_file sans droits : E_PERM',0
l_nosys		db 'ancien open du noyau : E_NOSYS',0
l_badf		db 'read(5) : E_BADF',0
		align 8

segment readable writeable
kit_data
		align 8
n_pass		dd 0
n_fail		dd 0
infos		rb 80
plist		rb 64 * 64
tampon		rb 4096
