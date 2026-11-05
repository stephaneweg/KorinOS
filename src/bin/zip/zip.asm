; zip - fait ou change des archives ZIP (sur ArchiveKit), comme zip d'Info-ZIP et celui d'Onyx
; (docs/OUTILS.md) :
;
;   zip [-r] [-0 .. -9] [-j] [-k] [-q] [-p DOSSIER] ARCHIVE CHEMIN ...    ajoute (ou remplace) des fichiers
;   zip -d ARCHIVE NOM ...                                                 retire des entrees
;
;   -r  les dossiers avec ce qu'ils contiennent (sans : l'entree du dossier seule)
;   -0  stocke, -1 le plus rapide .. -9 le meilleur (6 par defaut)
;   -j  sans dossiers : les fichiers seuls, en haut de l'archive (ou dans DOSSIER)
;   -k  garde un nom que l'archive a deja (par defaut : il est remplace)
;   -p  les met dans ce dossier de l'archive (« -p docs/images »)
;   -q  silencieux     NOM (avec -d) : un nom, un motif (* ?) ou un dossier (tout ce qu'il contient)
;
; Un chemin relatif est range tel qu'il est donne (« zip -r paquet.zip app/res » : app/res/...) ; un
; chemin avec son volume, d'apres sa derniere partie (« zip -r p.zip SD0:/apps/foo.app » : foo.app/...).
; Une archive qui existe est recopiee dans ARCHIVE.tmp avec les changements, puis prend sa place.
; Code de sortie : 0 bien, 1 rien de fait, 2 une erreur, 3 une mauvaise ligne de commande.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'
include '../../kits/archivekit/archivekit.inc'

macro getv reg*, var* {
	adr	reg,var
	ldr	reg,[reg]
}
macro setv var*, reg*, tmp* {
	adr	tmp,var
	str	reg,[tmp]
}

PLAN_E		= 24			; une entree du plan : nom, chemin, 1 si dossier

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/archivekit/archivekit.abi'		; SD0:/lib/archivekit.so

start:
	bl	t_debut
	lsl	x0,x19,3
	add	x0,x0,8
	bl	malloc
	setv	g_items,x0,x1
	mov	x0,6
	setv	g_level,x0,x1
	adr	x0,s_empty
	setv	g_prefix,x0,x1
	mov	x21,1
.a:	cmp	x21,x19
	b.hs	.fa
	ldr	x22,[x20,x21,lsl 3]
	add	x21,x21,1
	ldrb	w0,[x22]
	cmp	w0,'-'
	b.ne	.mot
	ldrb	w0,[x22,1]
	cbz	w0,.mot
	getv	x0,g_path
	cbnz	x0,.mot
	mov	x23,x22				; un groupe d'options
.l:	ldrb	w0,[x23,1]!
	cbz	w0,.a
	adr	x1,g_rec
	cmp	w0,'r'
	b.eq	.un
	adr	x1,g_junk
	cmp	w0,'j'
	b.eq	.un
	adr	x1,g_keep
	cmp	w0,'k'
	b.eq	.un
	adr	x1,g_quiet
	cmp	w0,'q'
	b.eq	.un
	adr	x1,g_del
	cmp	w0,'d'
	b.eq	.un
	cmp	w0,'p'
	b.ne	.h
	cmp	x21,x19
	b.hs	.a
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	setv	g_prefix,x0,x1
	b	.a
.h:	cmp	w0,'h'
	b.ne	.chif
	bl	usage
	mov	x0,0
	bl	t_fin
.chif:	sub	w1,w0,'0'
	cmp	w1,9
	b.hi	.inc
	setv	g_level,x1,x2
	b	.l
.un:	mov	x2,1
	str	x2,[x1]
	b	.l
.inc:	adr	x1,optbuf			; « -c : option inconnue »
	mov	w2,'-'
	strb	w2,[x1]
	strb	w0,[x1,1]
	strb	wzr,[x1,2]
	mov	x0,x1
	adr	x1,m_opt
	bl	zc_err
	bl	usage
	mov	x0,3
	bl	t_fin
.mot:	getv	x0,g_path
	cbnz	x0,.it
	setv	g_path,x22,x1
	b	.a
.it:	getv	x1,g_items
	getv	x2,g_ni
	str	x22,[x1,x2,lsl 3]
	add	x2,x2,1
	setv	g_ni,x2,x3
	b	.a

.fa:	getv	x0,g_path
	cbz	x0,.use
	getv	x0,g_ni
	cbnz	x0,.arch
.use:	bl	usage
	mov	x0,3
	bl	t_fin
.arch:	adr	x0,pbuf				; l'archive : ARCHIVE.zip si elle n'a pas d'extension
	getv	x1,g_path			;   et n'existe pas
	bl	zc_copy
	adr	x0,pbuf
	bl	t_base
	bl	zc_has_dot
	cbnz	x0,.px
	adr	x0,pbuf
	bl	t_type
	tbz	x0,63,.px
	adr	x0,pbuf
	adr	x1,s_zip
	bl	zc_append
.px:	adr	x0,pbuf
	bl	zc_norm
	setv	g_normp,x0,x1
	adr	x0,pbuf
	bl	t_type
	tbnz	x0,63,.neuf
	adr	x0,pbuf
	bl	ar_open
	tbz	x0,63,.o1
	adr	x1,pbuf
	b	fail
.o1:	setv	g_old,x0,x1
	bl	ar_format
	cmp	x0,AR_FMT_ZIP
	b.eq	.mode
	adr	x0,pbuf
	adr	x1,m_notzip
	bl	zc_err
	mov	x0,2
	bl	t_fin
.neuf:	getv	x0,g_del
	cbz	x0,.mode
	adr	x0,pbuf
	adr	x1,m_noarch
	bl	zc_err
	mov	x0,2
	bl	t_fin
.mode:	getv	x0,g_del
	cbnz	x0,do_delete
	b	do_add

; x0 = erreur, x1 = chemin : « zip : chemin : erreur », code 2
fail:
	mov	x21,x1
	bl	ar_strerror
	mov	x1,x0
	mov	x0,x21
	bl	zc_err
	mov	x0,2
	bl	t_fin

usage:
	prologue
	adr	x0,m_usage
	bl	t_dire
	epilogue

; --- -d : retirer des entrees ----------------------------------------------------------------
do_delete:
	getv	x21,g_old
	mov	x0,x21
	bl	ar_count
	mov	x23,x0				; entrees
	add	x0,x23,1
	bl	malloc
	mov	x24,x0
	setv	g_sel,x0,x1
	mov	x22,0
	mov	x25,0				; retirees
.e:	cmp	x22,x23
	b.hs	.f
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x0,info
	ldr	x0,[x0,ARE_NAME]
	getv	x1,g_items
	getv	x2,g_ni
	bl	zc_chosen
	strb	w0,[x24,x22]
	cbz	x0,.n
	add	x25,x25,1
	getv	x0,g_quiet
	cbnz	x0,.n
	adr	x0,m_deleting
	bl	t_dire
	adr	x0,info
	ldr	x0,[x0,ARE_NAME]
	bl	t_puts
	adr	x0,info
	ldr	x0,[x0,ARE_DIR]
	cbz	x0,.nl
	mov	w0,'/'
	bl	t_putc
.nl:	bl	t_nl
.n:	add	x22,x22,1
	b	.e
.f:	cbnz	x25,.go
	adr	x0,pbuf
	adr	x1,m_nomatch
	bl	zc_err
	mov	x0,1
	bl	t_fin
.go:	bl	rewrite
	getv	x0,g_quiet
	cbnz	x0,.fin
	mov	x0,x25
	bl	t_putn
	cmp	x25,1
	adr	x0,m_del1
	adr	x1,m_deln
	csel	x0,x0,x1,eq
	bl	t_dire
	sub	x26,x23,x25
	mov	x0,x26
	bl	t_putn
	cmp	x26,1
	adr	x0,m_left1
	adr	x1,m_leftn
	csel	x0,x0,x1,ls
	bl	t_dire
.fin:	mov	x0,0
	bl	t_fin

; --- ajouter ---------------------------------------------------------------------------------
do_add:
	mov	x0,64				; le plan
	setv	g_pcap,x0,x1
	mov	x0,64*PLAN_E
	bl	malloc
	setv	g_plan,x0,x1
	getv	x0,g_prefix
	bl	zc_norm
	setv	g_pre,x0,x1
	mov	x26,0				; chemin donne
.c:	getv	x0,g_ni
	cmp	x26,x0
	b.hs	.plan
	getv	x0,g_items
	ldr	x22,[x0,x26,lsl 3]
	add	x26,x26,1
	mov	x0,x22
	bl	t_type
	tbz	x0,63,.ex
	mov	x0,x22
	adr	x1,m_notfound
	bl	zc_err
	b	.c
.ex:	mov	x23,x0				; 1 : un dossier
	mov	x24,0				; 1 : avec son volume (« SD0:/... »)
	mov	x1,0
.vo:	ldrb	w2,[x22,x1]
	cbz	w2,.nv
	cmp	w2,':'
	b.eq	.avol
	add	x1,x1,1
	cmp	x1,6
	b.lo	.vo
	b	.nv
.avol:	mov	x24,1
.nv:	mov	x0,x22
	bl	zc_norm
	mov	x25,x0
	ldrb	w0,[x25]
	cbnz	w0,.nomme
	cbz	x23,.c
	mov	x0,x22				; « . », « SD0:/ » : ce qu'il contient
	getv	x1,g_pre
	bl	walk_children
	b	.c
.nomme:	getv	x0,g_junk
	orr	x0,x0,x24
	cbz	x0,.rel
	mov	x0,x25
	bl	t_base
	mov	x1,x0
	b	.j
.rel:	mov	x1,x25
.j:	getv	x0,g_pre
	bl	zc_join
	mov	x1,x0
	mov	x0,x22
	bl	walk
	b	.c
.plan:	getv	x0,g_pn
	cbnz	x0,.dire
	adr	x0,pbuf
	adr	x1,m_nothing
	getv	x2,g_skipped
	cbz	x2,.r1
	adr	x1,m_nothingk
.r1:	bl	zc_err
	mov	x0,1
	bl	t_fin
.dire:	getv	x0,g_quiet			; ce qui sera fait
	cbnz	x0,.ecr
	mov	x22,0
.d:	getv	x0,g_pn
	cmp	x22,x0
	b.hs	.ecr
	getv	x0,g_plan
	mov	x1,PLAN_E
	madd	x23,x22,x1,x0
	adr	x24,m_adding
	getv	x0,g_old
	cbz	x0,.d1
	ldr	x1,[x23]
	bl	ar_find
	tbnz	x0,63,.d1
	adr	x24,m_updating
.d1:	mov	x0,2
	bl	spaces
	mov	x0,x24
	bl	t_dire
	ldr	x0,[x23]
	bl	t_puts
	ldr	x0,[x23,16]
	cbz	x0,.d2
	mov	w0,'/'
	bl	t_putc
.d2:	bl	t_nl
	add	x22,x22,1
	b	.d
.ecr:	bl	rewrite
	getv	x0,g_quiet			; le bilan, relu dans l'archive
	cbnz	x0,.fin
	adr	x0,pbuf
	bl	ar_open
	tbnz	x0,63,.fin
	mov	x21,x0
	bl	ar_count
	mov	x23,x0
	mov	x22,0
	mov	x25,0
	mov	x26,0
.s:	cmp	x22,x23
	b.hs	.sf
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x0,info
	ldr	x1,[x0,ARE_SIZE]
	add	x25,x25,x1
	ldr	x1,[x0,ARE_PACKED]
	add	x26,x26,x1
	add	x22,x22,1
	b	.s
.sf:	mov	x0,x21
	bl	ar_close
	adr	x0,pbuf
	bl	t_puts
	adr	x0,zc_s_sep
	bl	t_dire
	mov	x0,x23
	bl	t_putn
	adr	x0,m_entries
	bl	t_dire
	mov	x0,x25
	bl	zc_put_size
	adr	x0,s_arrow
	bl	t_puts
	mov	x0,x26
	bl	zc_put_size
	adr	x0,s_par
	bl	t_puts
	mov	x0,x25
	mov	x1,x26
	bl	zc_pct
	bl	t_putn
	adr	x0,m_saved
	bl	t_dire
.fin:	mov	x0,0
	bl	t_fin

; x0 = nombre de blancs
spaces:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.b:	cbz	x19,.out
	mov	w0,' '
	bl	t_putc
	sub	x19,x19,1
	b	.b
.out:	ldr	x19,[sp],16
	epilogue

; x0 = chemin, x1 = nom dans l'archive : au plan (un dossier : avec -r, ce qu'il contient)
walk:
	prologue
	stp	x19,x21,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	bl	t_type
	tbnz	x0,63,.out
	cbz	x0,.fic
	getv	x0,g_junk
	cbnz	x0,.nd
	mov	x0,x21
	mov	x1,x19
	mov	x2,1
	bl	plan_add
.nd:	getv	x0,g_rec
	cbz	x0,.out
	getv	x1,g_junk
	cbz	x1,.bn
	getv	x1,g_pre
	b	.wc
.bn:	mov	x1,x21
.wc:	mov	x0,x19
	bl	walk_children
	b	.out
.fic:	mov	x0,x21
	mov	x1,x19
	mov	x2,0
	bl	plan_add
.out:	ldp	x19,x21,[sp],16
	epilogue

; x0 = dossier, x1 = nom de base dans l'archive : chacun de ses enfants, par walk
walk_children:
	prologue
	stp	x19,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	sub	sp,sp,DIRENT_BYTES
	mov	x19,x0
	mov	x21,x1
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x22,x0
.l:	mov	x0,x22
	mov	x1,sp
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.f
	ldrb	w0,[sp]
	cmp	w0,'.'
	b.ne	.p
	ldrb	w0,[sp,1]
	cbz	w0,.l
	cmp	w0,'.'
	b.ne	.p
	ldrb	w0,[sp,2]
	cbz	w0,.l
.p:	mov	x0,x19
	mov	x1,sp
	bl	zc_join
	mov	x23,x0
	mov	x0,x21
	mov	x1,sp
	bl	zc_join
	mov	x1,x0
	mov	x0,x23
	bl	walk
	b	.l
.f:	mov	x0,x22
	sys	SYS_CLOSE
.out:	add	sp,sp,DIRENT_BYTES
	ldp	x22,x23,[sp],16
	ldp	x19,x21,[sp],16
	epilogue

; x0 = nom, x1 = chemin, x2 = 1 si dossier : une entree du plan (sauf l'archive elle-meme, et avec
; -k un nom que l'archive a deja)
plan_add:
	prologue
	stp	x19,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x19,x0
	mov	x21,x1
	mov	x22,x2
	cbnz	x22,.k
	mov	x0,x21
	bl	zc_norm
	getv	x1,g_normp
	bl	zc_streqi
	cbnz	x0,.out
.k:	getv	x0,g_keep
	cbz	x0,.place
	getv	x0,g_old
	cbz	x0,.place
	mov	x1,x19
	bl	ar_find
	tbnz	x0,63,.place
	adr	x1,g_skipped
	ldr	x0,[x1]
	add	x0,x0,1
	str	x0,[x1]
	b	.out
.place:	getv	x0,g_pn
	getv	x1,g_pcap
	cmp	x0,x1
	b.lo	.put
	lsl	x1,x1,1				; plus de place : le double
	setv	g_pcap,x1,x2
	mov	x0,PLAN_E
	mul	x0,x0,x1
	bl	malloc
	mov	x23,x0
	getv	x1,g_plan
	getv	x2,g_pn
	mov	x3,PLAN_E
	mul	x2,x2,x3
	bl	mem_move
	setv	g_plan,x23,x0
.put:	getv	x0,g_plan
	getv	x1,g_pn
	mov	x2,PLAN_E
	madd	x0,x1,x2,x0
	stp	x19,x21,[x0]
	str	x22,[x0,16]
	add	x1,x1,1
	setv	g_pn,x1,x2
.out:	ldp	x22,x23,[sp],16
	ldp	x19,x21,[sp],16
	epilogue

; x0 = nom -> x0 = 1 si le plan a ce nom (sans la casse)
in_plan:
	prologue
	stp	x19,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	mov	x19,x0
	mov	x21,0
	getv	x22,g_plan
	getv	x23,g_pn
.c:	cmp	x21,x23
	b.hs	.non
	ldr	x0,[x22]
	mov	x1,x19
	bl	zc_streqi
	cbnz	x0,.out
	add	x22,x22,PLAN_E
	add	x21,x21,1
	b	.c
.non:	mov	x0,0
.out:	ldp	x22,x23,[sp],16
	ldp	x19,x21,[sp],16
	epilogue

; L'archive refaite : les anciennes entrees gardees (sans celles que -d retire ou que le plan
; remplace), puis le plan. Dans ARCHIVE.tmp si elle existait, qui prend ensuite sa place.
rewrite:
	prologue
	stp	x19,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	stp	x26,x27,[sp,-16]!
	getv	x19,g_old
	adr	x0,tbuf
	adr	x1,pbuf
	bl	zc_copy
	cbz	x19,.t1
	adr	x0,tbuf
	adr	x1,s_tmp
	bl	zc_append
.t1:	adr	x0,tbuf
	mov	x1,AR_FMT_ZIP
	bl	ar_create
	tbz	x0,63,.cree
	adr	x1,tbuf
	b	fail
.cree:	mov	x21,x0				; x21 : l'ecrivain
	cbz	x19,.new
	mov	x0,x19
	bl	ar_count
	mov	x27,x0
	mov	x22,0
.c:	cmp	x22,x27
	b.hs	.new
	getv	x0,g_del
	cbz	x0,.rep
	getv	x0,g_sel
	ldrb	w0,[x0,x22]
	cbnz	w0,.next
	b	.copie
.rep:	getv	x0,g_keep
	cbnz	x0,.copie
	mov	x0,x19
	mov	x1,x22
	bl	ar_name
	bl	in_plan
	cbnz	x0,.next
.copie:	mov	x0,x19
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x23,info
	mov	x0,x21
	ldr	x1,[x23,ARE_TIME]
	bl	ar_set_time
	ldr	x0,[x23,ARE_DIR]
	cbz	x0,.cf
	mov	x0,x21
	ldr	x1,[x23,ARE_NAME]
	bl	ar_add_dir
	tbnz	x0,63,.werr
	b	.next
.cf:	mov	x0,x19
	mov	x1,x22
	bl	ar_load
	tbnz	x0,63,.werr
	mov	x24,x0
	bl	ar_data
	mov	x25,x0
	mov	x0,x24
	bl	ar_bytes
	mov	x3,x0
	ldr	x4,[x23,ARE_METHOD]
	cbz	x4,.lv
	getv	x4,g_level
.lv:	mov	x0,x21
	ldr	x1,[x23,ARE_NAME]
	mov	x2,x25
	bl	ar_add_mem
	mov	x26,x0
	mov	x0,x24
	bl	ar_free
	mov	x0,x26
	tbnz	x0,63,.werr
.next:	add	x22,x22,1
	b	.c
.new:	mov	x0,x21				; (les nouveaux dossiers : l'heure courante)
	mov	x1,0
	bl	ar_set_time
	mov	x22,0
.p:	getv	x0,g_pn
	cmp	x22,x0
	b.hs	.fin
	getv	x0,g_plan
	mov	x1,PLAN_E
	madd	x23,x22,x1,x0
	ldr	x0,[x23,16]
	cbz	x0,.pf
	mov	x0,x21
	ldr	x1,[x23]
	bl	ar_add_dir
	tbnz	x0,63,.perr
	b	.pn
.pf:	mov	x0,x21
	ldr	x1,[x23]
	ldr	x2,[x23,8]
	getv	x3,g_level
	bl	ar_add_file
	tbnz	x0,63,.perr
.pn:	add	x22,x22,1
	b	.p
.fin:	mov	x0,x21
	bl	ar_finish
	tbnz	x0,63,.werr
	mov	x0,x21
	bl	ar_free
	cbz	x19,.out
	mov	x0,x19				; la copie prend la place de l'archive
	bl	ar_close
	setv	g_old,xzr,x0
	adr	x0,pbuf
	sys	SYS_UNLINK
	adr	x0,tbuf
	adr	x1,pbuf
	bl	t_rename
	tbz	x0,63,.out
	adr	x0,tbuf
	adr	x1,m_rename
	bl	zc_err
	mov	x0,2
	bl	t_fin
.out:	ldp	x26,x27,[sp],16
	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x19,x21,[sp],16
	epilogue
.perr:	ldr	x24,[x23,8]			; (un fichier du plan)
	b	.abandon
.werr:	adr	x24,pbuf			; (l'ancienne archive, ou l'ecriture)
.abandon:
	mov	x25,x0
	mov	x0,x21
	bl	ar_free
	adr	x0,tbuf
	sys	SYS_UNLINK
	mov	x0,x25
	mov	x1,x24
	b	fail

include 'zipcli.inc'

m_usage		db 'zip -- fait ou change des archives ZIP',10
		db 'usage : zip [-r] [-0..-9] [-j] [-k] [-q] [-p DOSSIER] ARCHIVE CHEMIN...',10
		db '        zip -d ARCHIVE NOM...',10
		db '  -r avec ce que les dossiers contiennent  -0 stocke .. -9 le mieux  -j sans dossiers',10
		db '  -k garde les noms d',0xC3,0xA9,'j',0xC3,0xA0,' pr',0xC3,0xA9,'sents  -p DOSSIER dans ce dossier de l',0x27,'archive  -d retire  -q silencieux',10,0
m_opt		db 'option inconnue',0
m_notzip	db 'seul le format ZIP s',0x27,0xC3,0xA9,'crit ici',0
m_noarch	db 'pas d',0x27,'archive de ce nom',0
m_nomatch	db 'rien ne correspond dans l',0x27,'archive',0
m_notfound	db 'introuvable',0
m_nothing	db 'rien ',0xC3,0xA0,' faire',0
m_nothingk	db 'rien ',0xC3,0xA0,' faire (les noms existent : -k)',0
m_rename	db 'impossible de remplacer l',0x27,'archive : la nouvelle est ici',0
m_deleting	db 'suppression : ',0
m_adding	db 'ajout : ',0
m_updating	db 'mise ',0xC3,0xA0,' jour : ',0
m_del1		db ' entr',0xC3,0xA9,'e supprim',0xC3,0xA9,'e, ',0
m_deln		db ' entr',0xC3,0xA9,'es supprim',0xC3,0xA9,'es, ',0
m_left1		db ' restante.',10,0
m_leftn		db ' restantes.',10,0
m_entries	db ' entr',0xC3,0xA9,'es, ',0
m_saved		db ' % gagn',0xC3,0xA9,'s).',10,0
s_arrow		db ' -> ',0
s_par		db ' (',0
s_zip		db '.zip',0
s_tmp		db '.tmp',0
s_empty		db 0
		align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
		align 8
g_rec		dd 0
g_junk		dd 0
g_keep		dd 0
g_quiet		dd 0
g_del		dd 0
g_level		dd 0
g_prefix	dd 0
g_path		dd 0
g_items		dd 0
g_ni		dd 0
g_old		dd 0
g_sel		dd 0
g_normp		dd 0
g_pre		dd 0
g_plan		dd 0
g_pn		dd 0
g_pcap		dd 0
g_skipped	dd 0
info		rb AR_ENTRY_SIZE
optbuf		rb 8
pbuf		rb 1040
tbuf		rb 1048
