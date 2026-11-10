; ramtest - essai des volumes en espace utilisateur (docs/MICRO-NOYAU.md,
; section 4) : « ramtest [VOLUME] » (RAM0 par defaut ; IMG0 : fat32 sur une
; image). Repertoire, ecriture et relecture par morceaux, seek, readdir,
; stat, chdir / getcwd, unlink ; puis la vitesse d'ecriture et de lecture de
; 256 Ko, sur ce volume et sur SD0:.
; « ramtest SD9 » (fat32 sur la carte, en lecture seule) : compare un fichier
; lu par les services et par le noyau (SD0:), et verifie le refus d'ecrire.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

N	= 3000				; octets du fichier d'essai
CMP_MAX	= 65536				; comparaison (SD9)
GROS	= 262144			; fichier de vitesse

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	adr	x20,s_ram0			; volume : l'argument, sinon RAM0
	cbz	x19,.vol
	mov	x20,x19
.vol:	adr	x0,p_dir			; chemins de ce volume
	mov	x1,x20
	adr	x2,s_docs
	bl	chemin
	adr	x0,p_fic
	mov	x1,x20
	adr	x2,s_fic
	bl	chemin
	adr	x0,p_gros_ram
	mov	x1,x20
	adr	x2,s_gros
	bl	chemin
	adr	x0,m_vol			; le dossier d'essai (volume compris)
	bl	puts
	adr	x0,p_dir
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x0,p_dir			; « SD9 » : comparaison
	ldr	w1,[x0]
	movl	w2,0x3A394453			; « SD9: »
	cmp	w1,w2
	b.eq	compare
	mov	x0,2000				; (lance au demarrage : le temps que
	sys	SYS_SLEEP			;  vfs monte RAM0:)
	adr	x0,p_dir			; 1. mkdir
	sys	SYS_MKDIR
	adr	x1,m_mkdir
	bl	res
	adr	x0,p_dir			; (et le repertoire est la ?)
	adr	x1,infos
	sys	SYS_STAT
	adr	x1,m_stat_dir
	bl	res
	mov	x0,0				; 2. ecrire N octets (motif)
.mo:	and	w1,w0,0xFF			; motif : i x 7 + 3
	mov	w3,7
	mul	w1,w1,w3
	add	w1,w1,3
	adr	x2,tampon
	strb	w1,[x2,x0]
	add	x0,x0,1
	cmp	x0,N
	b.lo	.mo
	adr	x0,p_fic
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	mov	x19,x0
	tbnz	x0,63,.e_open
	mov	x0,x19
	adr	x1,tampon
	mov	x2,N
	sys	SYS_WRITE
	adr	x1,m_ecrit
	bl	res
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_fic			; 3. relire par morceaux de 700
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,x0
	tbnz	x0,63,.e_open
	mov	x20,0				; lus
.r:	mov	x0,x19
	adr	x1,relu
	add	x1,x1,x20
	mov	x2,700
	sys	SYS_READ
	cmp	x0,0
	b.le	.rf
	add	x20,x20,x0
	b	.r
.rf:	mov	x0,x20
	adr	x1,m_lu
	bl	res
	mov	x0,0				; comparaison
	mov	x1,0
.cmp:	cmp	x0,N
	b.hs	.cf
	adr	x2,tampon
	ldrb	w3,[x2,x0]
	adr	x2,relu
	ldrb	w4,[x2,x0]
	cmp	w3,w4
	cinc	x1,x1,ne
	add	x0,x0,1
	b	.cmp
.cf:	mov	x0,x1
	adr	x1,m_diff
	bl	res
	mov	x0,x19				; 4. seek a 2990 : 10 octets restent
	mov	x1,2990
	mov	x2,0
	sys	SYS_SEEK
	mov	x0,x19
	adr	x1,relu
	mov	x2,100
	sys	SYS_READ
	adr	x1,m_seek
	bl	res
	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_fic			; 5. stat
	adr	x1,infos
	sys	SYS_STAT
	adr	x0,infos
	ldr	x0,[x0]
	adr	x1,m_stat
	bl	res
	adr	x0,p_dir			; 6. readdir
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,x0
	tbnz	x0,63,.e_open
.d:	mov	x0,x19
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.df
	adr	x0,m_ent
	bl	puts
	adr	x0,dirent
	bl	puts
	adr	x0,m_nl
	bl	puts
	b	.d
.df:	mov	x0,x19
	sys	SYS_CLOSE
	adr	x0,p_dir			; 7. chdir, getcwd
	sys	SYS_CHDIR
	adr	x1,m_chdir
	bl	res
	adr	x0,dirent
	mov	x1,256
	sys	SYS_GETCWD
	adr	x0,m_cwd
	bl	puts
	adr	x0,dirent
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x0,p_fic			; 8. unlink (fichier, puis repertoire)
	sys	SYS_UNLINK
	adr	x1,m_unlink
	bl	res
	adr	x0,p_dir
	sys	SYS_UNLINK
	adr	x1,m_rmdir
	bl	res
	adr	x0,p_fic			; (plus la)
	adr	x1,infos
	sys	SYS_STAT
	adr	x1,m_parti
	bl	res
	adr	x0,p_gros_ram			; 9. vitesse
	bl	vitesse
	adr	x0,p_gros_sd
	bl	vitesse
	mov	x0,0
	sys	SYS_EXIT
.e_open:
	adr	x1,m_open
	bl	res
	mov	x0,1
	sys	SYS_EXIT

; x0 = destination, x1 = volume, x2 = suite (« :/docs ») -> chemin complet
chemin:
.v:	ldrb	w3,[x1],1			; (« IMG0 », « IMG0: », sans
	cmp	w3,' '				;  espace ni caractere invisible)
	b.ls	.s
	cmp	w3,':'
	b.eq	.s
	strb	w3,[x0],1
	b	.v
.s:	ldrb	w3,[x2],1
	strb	w3,[x0],1
	cbnz	w3,.s
	ret

; SD9 : fat32 (services) contre le noyau, sur le meme fichier ; ecriture refusee
compare:
	adr	x0,p_cmp_svc
	adr	x1,tampon
	bl	lire_tout
	mov	x19,x0
	adr	x0,p_cmp_noyau
	adr	x1,relu
	bl	lire_tout
	mov	x20,x0
	mov	x0,x19
	adr	x1,m_lu_svc
	bl	res
	mov	x0,x20
	adr	x1,m_lu_noyau
	bl	res
	mov	x0,0				; octets differents
	mov	x1,0
.c:	cmp	x0,x19
	b.hs	.cf
	adr	x2,tampon
	ldrb	w3,[x2,x0]
	adr	x2,relu
	ldrb	w4,[x2,x0]
	cmp	w3,w4
	cinc	x1,x1,ne
	add	x0,x0,1
	b	.c
.cf:	mov	x0,x1
	adr	x1,m_diff
	bl	res
	adr	x0,p_dir			; ecrire : refuse
	sys	SYS_MKDIR
	adr	x1,m_ro
	bl	res
	adr	x0,p_dir			; lister la racine de SD9:
	mov	w1,0
	strb	w1,[x0,4]
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,x0
	tbnz	x0,63,.fin
.d:	mov	x0,x19
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.df
	adr	x0,m_ent
	bl	puts
	adr	x0,dirent
	bl	puts
	adr	x0,m_nl
	bl	puts
	b	.d
.df:	mov	x0,x19
	sys	SYS_CLOSE
.fin:	mov	x0,0
	sys	SYS_EXIT

; x0 = chemin, x1 = destination (CMP_MAX au plus) -> x0 = octets lus
lire_tout:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x20,x1
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,x0
	mov	x21,0
	tbnz	x0,63,.out
.l:	mov	x0,x19
	add	x1,x20,x21
	mov	x2,4096
	sys	SYS_READ
	cmp	x0,0
	b.le	.f
	add	x21,x21,x0
	mov	x0,CMP_MAX-4096
	cmp	x21,x0
	b.lo	.l
.f:	mov	x0,x19
	sys	SYS_CLOSE
.out:	mov	x0,x21
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = libelle : « ramtest : libelle valeur »
res:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,m_pre
	bl	puts
	mov	x0,x20
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin : ecrit puis relit GROS octets (morceaux de 4 Ko), affiche les
; durees, efface le fichier
vitesse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	mov	x19,x0
	tbnz	x0,63,.out
	sys	SYS_TIME_MS
	mov	x22,x0
	mov	x20,0
.w:	mov	x0,x19
	adr	x1,gros
	mov	x2,4096
	sys	SYS_WRITE
	add	x20,x20,4096
	mov	x0,GROS
	cmp	x20,x0
	b.lo	.w
	mov	x0,x19
	sys	SYS_CLOSE
	sys	SYS_TIME_MS
	sub	x22,x0,x22
	adr	x0,m_pre
	bl	puts
	mov	x0,x21
	bl	puts
	adr	x0,m_ecr
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,x21
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,x0
	tbnz	x0,63,.out
	sys	SYS_TIME_MS
	mov	x22,x0
.l:	mov	x0,x19
	adr	x1,gros
	mov	x2,4096
	sys	SYS_READ
	cmp	x0,0
	b.gt	.l
	mov	x0,x19
	sys	SYS_CLOSE
	sys	SYS_TIME_MS
	sub	x22,x0,x22
	adr	x0,m_lec
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_ms2
	bl	puts
	mov	x0,x21
	sys	SYS_UNLINK
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_ram0	db 'RAM0',0
s_docs	db ':/docs',0
s_fic	db ':/docs/essai.txt',0
s_gros	db ':/gros.bin',0
p_gros_sd db 'SD0:/gros.bin',0
p_cmp_svc db 'SD9:/lib/uikit.so',0
p_cmp_noyau db 'SD0:/lib/uikit.so',0
m_lu_svc db 'SD9:/lib/uikit.so lu par fat32 -> ',0
m_lu_noyau db 'SD0:/lib/uikit.so lu par le noyau -> ',0
m_ro	db 'mkdir sur SD9: (lecture seule) -> ',0
m_pre	db 'ramtest : ',0
m_vol	db 'ramtest : dossier d''essai ',0
m_mkdir	db 'mkdir /docs -> ',0
m_stat_dir db 'stat /docs -> ',0
m_ecrit	db 'ecrit -> ',0
m_lu	db 'relu (morceaux de 700) -> ',0
m_diff	db 'octets differents -> ',0
m_seek	db 'apres seek 2990, lu -> ',0
m_stat	db 'stat : taille -> ',0
m_ent	db 'ramtest : entree ',0
m_chdir	db 'chdir /docs -> ',0
m_cwd	db 'ramtest : getcwd -> ',0
m_unlink db 'unlink du fichier -> ',0
m_rmdir	db 'unlink du repertoire -> ',0
m_parti	db 'stat apres unlink -> ',0
m_open	db 'open -> ',0
m_ecr	db ' : 256 Ko ecrits en ',0
m_ms	db ' ms',0
m_lec	db ', relus en ',0
m_ms2	db ' ms',10,0
m_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 16
infos	rb 16
dirent	rb 272
p_dir	rb 64
p_fic	rb 64
p_gros_ram rb 64
	align 16
tampon	rb CMP_MAX
relu	rb CMP_MAX
	align 16
gros	rb 4096
