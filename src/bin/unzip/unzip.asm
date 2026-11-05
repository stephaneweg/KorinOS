; unzip - extrait, liste, essaie des archives ZIP (et ce qu'ArchiveKit lit : tar, tar.gz, gz), comme
; unzip d'Info-ZIP et celui d'Onyx (docs/OUTILS.md) :
;
;   unzip [-l | -v | -t | -p] [-o | -n] [-j] [-q] [-d DOSSIER] ARCHIVE [NOM ...] [-x NOM ...]
;
;   -l  liste   -v  liste avec la methode, la taille compressee, le CRC   -t  essai (tout est lu, verifie)
;   -p  les fichiers sur la sortie standard (un tube)   -o  remplace les fichiers   -n  ne remplace jamais
;   -j  sans dossiers (tout dans DOSSIER)   -d  le dossier ou extraire (cree ; par defaut : ici)
;   -q  silencieux   NOM : un nom, un motif (* ?) ou un dossier (tout ce qu'il contient) ; -x : laisses
;
; Sans -o ni -n, un fichier qui existe fait poser la question : [o]ui, [n]on, [t]ous, [r]ien.
; Pas de -P : ArchiveKit ne lit pas les entrees chiffrees. Code de sortie : 0 bien, 1 un avertissement
; (des fichiers sautes, rien ne correspond), 2 une erreur, 3 une mauvaise ligne de commande.
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

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/archivekit/archivekit.abi'		; SD0:/lib/archivekit.so

start:
	bl	t_debut
	lsl	x0,x19,3
	add	x0,x0,8
	bl	malloc
	setv	g_pat,x0,x1
	lsl	x0,x19,3
	add	x0,x0,8
	bl	malloc
	setv	g_excl,x0,x1
	mov	x0,'x'
	setv	g_mode,x0,x1
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
	cbnz	x0,.apres
	mov	x23,x22				; un groupe d'options
.l:	ldrb	w0,[x23,1]!
	cbz	w0,.a
	cmp	w0,'l'
	b.eq	.md
	cmp	w0,'v'
	b.eq	.md
	cmp	w0,'t'
	b.eq	.md
	cmp	w0,'p'
	b.eq	.md
	cmp	w0,'o'
	b.ne	.l1
	mov	x0,1
	setv	g_ow,x0,x1
	b	.l
.l1:	cmp	w0,'n'
	b.ne	.l2
	mov	x0,2
	setv	g_ow,x0,x1
	b	.l
.l2:	cmp	w0,'j'
	b.ne	.l3
	mov	x0,1
	setv	g_junk,x0,x1
	b	.l
.l3:	cmp	w0,'q'
	b.ne	.l4
	mov	x0,1
	setv	g_quiet,x0,x1
	b	.l
.l4:	cmp	w0,'d'
	b.ne	.l5
	cmp	x21,x19
	b.hs	.a
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	setv	g_dest,x0,x1
	b	.a
.l5:	cmp	w0,'P'
	b.ne	.l6
	mov	x0,0
	adr	x1,m_crypt
	bl	zc_err
	mov	x0,3
	bl	t_fin
.l6:	cmp	w0,'h'
	b.ne	.l7
	bl	usage
	mov	x0,0
	bl	t_fin
.l7:	adr	x1,optbuf			; « -c : option inconnue »
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
.md:	setv	g_mode,x0,x1
	b	.l
.apres:	mov	x0,x22				; apres l'archive : -x, -d
	adr	x1,s_x
	bl	str_eq
	cbz	x0,.ad
	mov	x0,1
	setv	g_inexcl,x0,x1
	b	.a
.ad:	mov	x0,x22
	adr	x1,s_d
	bl	str_eq
	cbz	x0,.mot
	cmp	x21,x19
	b.hs	.mot
	ldr	x0,[x20,x21,lsl 3]
	add	x21,x21,1
	setv	g_dest,x0,x1
	b	.a
.mot:	getv	x0,g_path
	cbnz	x0,.pm
	setv	g_path,x22,x1
	b	.a
.pm:	getv	x0,g_inexcl
	cbnz	x0,.px
	getv	x1,g_pat
	getv	x2,g_np
	str	x22,[x1,x2,lsl 3]
	add	x2,x2,1
	setv	g_np,x2,x3
	b	.a
.px:	getv	x1,g_excl
	getv	x2,g_nx
	str	x22,[x1,x2,lsl 3]
	add	x2,x2,1
	setv	g_nx,x2,x3
	b	.a

.fa:	getv	x22,g_path			; l'archive (ARCHIVE.zip si ARCHIVE n'a pas de point)
	cbnz	x22,.ok
	bl	usage
	mov	x0,3
	bl	t_fin
.ok:	adr	x0,pbuf
	mov	x1,x22
	bl	zc_copy
	adr	x0,pbuf
	bl	ar_open
	tbz	x0,63,.ouvert
	mov	x23,x0
	mov	x0,x22
	bl	zc_has_dot
	cbnz	x0,.echec
	adr	x0,pbuf
	adr	x1,s_zip
	bl	zc_append
	adr	x0,pbuf
	bl	ar_open
	tbz	x0,63,.ouvert
.echec:	mov	x0,x23
	bl	ar_strerror
	mov	x1,x0
	mov	x0,x22
	bl	zc_err
	mov	x0,2
	bl	t_fin
.ouvert:
	mov	x21,x0				; x21 : l'archive
	bl	ar_count
	mov	x23,x0				; x23 : entrees
	add	x0,x23,1
	bl	malloc
	mov	x24,x0				; x24 : choisies (un octet chacune)
	mov	x22,0
	mov	x25,0
.sc:	cmp	x22,x23
	b.hs	.sf
	mov	x0,x21
	mov	x1,x22
	bl	ar_name
	mov	x26,x0
	getv	x1,g_pat
	getv	x2,g_np
	bl	zc_chosen
	cbz	x0,.sn
	getv	x2,g_nx
	cbz	x2,.sy
	mov	x0,x26
	getv	x1,g_excl
	bl	zc_chosen
	cbnz	x0,.sn
.sy:	mov	w0,1
	strb	w0,[x24,x22]
	add	x25,x25,1
	b	.sx
.sn:	strb	wzr,[x24,x22]
.sx:	add	x22,x22,1
	b	.sc
.sf:	getv	x0,g_np
	cbz	x0,.mode
	cbnz	x25,.mode
	adr	x0,pbuf
	adr	x1,m_nomatch
	bl	zc_err
	mov	x0,1
	bl	t_fin
.mode:	getv	x0,g_mode
	cmp	x0,'l'
	b.eq	do_list
	cmp	x0,'v'
	b.eq	do_list
	cmp	x0,'t'
	b.eq	do_test
	cmp	x0,'p'
	b.eq	do_pipe
	b	do_extract

; l'aide, sur la sortie standard
usage:
	prologue
	adr	x0,m_usage
	bl	t_dire
	epilogue

; x21 = archive, x23 = entrees, x24 = choisies : x0 = code, fin
finir:
	mov	x22,x0
	mov	x0,x21
	bl	ar_close
	mov	x0,x22
	bl	t_fin

; --- -l, -v : la liste -----------------------------------------------------------------------
do_list:
	adr	x0,m_archive
	bl	t_dire
	adr	x0,pbuf
	bl	t_puts
	bl	t_nl
	getv	x0,g_mode
	cmp	x0,'v'
	adr	x0,m_hl
	adr	x1,m_hv
	csel	x0,x1,x0,eq
	bl	t_dire
	mov	x22,0
	mov	x25,0				; taille
	mov	x26,0				; compresse
	mov	x27,0				; fichiers
.e:	cmp	x22,x23
	b.hs	.tot
	ldrb	w0,[x24,x22]
	cbz	w0,.n
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x28,info
	ldr	x0,[x28,ARE_PACKED]
	add	x26,x26,x0
	ldr	x0,[x28,ARE_DIR]
	cbnz	x0,.dr
	add	x27,x27,1
.dr:	ldr	x0,[x28,ARE_SIZE]
	add	x25,x25,x0
	getv	x1,g_mode
	cmp	x1,'v'
	b.eq	.lv
	mov	x1,9				; -l : taille, date, nom
	bl	zc_put_w
	adr	x0,s_2sp
	bl	t_puts
	ldr	x0,[x28,ARE_TIME]
	bl	zc_put_date
	adr	x0,s_3sp
	bl	t_puts
	b	.nom
.lv:	mov	x1,8				; -v : taille, methode, compresse, taux, date, CRC, nom
	bl	zc_put_w
	adr	x0,s_2sp
	bl	t_puts
	ldr	x0,[x28,ARE_DIR]
	cbnz	x0,.stocke
	ldr	x0,[x28,ARE_METHOD]
	cbz	x0,.stocke
	cmp	x0,8
	b.ne	.mq
	adr	x0,s_defl
	b	.mp
.mq:	adr	x0,s_qm
	b	.mp
.stocke:
	adr	x0,s_stored
.mp:	mov	x1,6
	bl	zc_put_pad
	mov	w0,' '
	bl	t_putc
	ldr	x0,[x28,ARE_PACKED]
	mov	x1,8
	bl	zc_put_w
	mov	w0,' '
	bl	t_putc
	ldr	x0,[x28,ARE_SIZE]
	ldr	x1,[x28,ARE_PACKED]
	bl	zc_pct
	mov	x1,3
	bl	zc_put_w
	mov	w0,'%'
	bl	t_putc
	mov	w0,' '
	bl	t_putc
	ldr	x0,[x28,ARE_TIME]
	bl	zc_put_date
	adr	x0,s_2sp
	bl	t_puts
	ldr	x0,[x28,ARE_CRC]
	bl	zc_hex8
	adr	x0,s_2sp
	bl	t_puts
.nom:	ldr	x0,[x28,ARE_NAME]
	bl	t_puts
	ldr	x0,[x28,ARE_DIR]
	cbz	x0,.nl
	mov	w0,'/'
	bl	t_putc
.nl:	bl	t_nl
.n:	add	x22,x22,1
	b	.e
.tot:	getv	x0,g_mode
	cmp	x0,'v'
	b.eq	.tv
	adr	x0,s_tl
	bl	t_puts
	mov	x0,x25
	mov	x1,9
	bl	zc_put_w
	mov	x0,21
	bl	spaces
	b	.nf
.tv:	adr	x0,s_tv
	bl	t_puts
	mov	x0,x25
	mov	x1,8
	bl	zc_put_w
	mov	x0,9
	bl	spaces
	mov	x0,x26
	mov	x1,8
	bl	zc_put_w
	mov	w0,' '
	bl	t_putc
	mov	x0,x25
	mov	x1,x26
	bl	zc_pct
	mov	x1,3
	bl	zc_put_w
	mov	w0,'%'
	bl	t_putc
	mov	x0,28
	bl	spaces
.nf:	mov	x0,x27
	bl	t_putn
	cmp	x27,1
	adr	x0,m_file1
	adr	x1,m_files
	csel	x0,x0,x1,eq
	bl	t_dire
	bl	t_nl
	mov	x0,0
	b	finir

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

; --- -t : l'essai ----------------------------------------------------------------------------
do_test:
	mov	x22,0
	mov	x25,0				; mauvaises
	mov	x26,0				; essayees
.e:	cmp	x22,x23
	b.hs	.fin
	ldrb	w0,[x24,x22]
	cbz	w0,.n
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x28,info
	ldr	x0,[x28,ARE_DIR]
	cbnz	x0,.n
	add	x26,x26,1
	mov	x0,x21
	mov	x1,x22
	bl	ar_test
	mov	x27,x0
	cbnz	x27,.pr
	getv	x0,g_quiet
	cbnz	x0,.n
.pr:	mov	x0,4
	bl	spaces
	adr	x0,m_testing
	bl	t_dire
	ldr	x0,[x28,ARE_NAME]
	mov	x1,40
	bl	zc_put_pad
	mov	w0,' '
	bl	t_putc
	cbnz	x27,.mauv
	adr	x0,s_ok
	bl	t_puts
	b	.pn
.mauv:	add	x25,x25,1
	mov	x0,x27
	bl	ar_strerror
	bl	t_puts
.pn:	bl	t_nl
.n:	add	x22,x22,1
	b	.e
.fin:	cbz	x25,.bon
	adr	x0,m_tbad1
	bl	t_dire
	adr	x0,pbuf
	bl	t_puts
	adr	x0,m_tbad2
	bl	t_dire
	mov	x0,x25
	bl	t_putn
	adr	x0,m_tbad3
	bl	t_dire
	mov	x0,x26
	bl	t_putn
	adr	x0,m_tbad4
	bl	t_dire
	mov	x0,2
	b	finir
.bon:	adr	x0,m_tgood
	bl	t_dire
	adr	x0,pbuf
	bl	t_puts
	adr	x0,s_dotnl
	bl	t_puts
	mov	x0,0
	b	finir

; --- -p : vers la sortie standard ------------------------------------------------------------
do_pipe:
	mov	x22,0
	bl	t_flush
.e:	cmp	x22,x23
	b.hs	.fin
	ldrb	w0,[x24,x22]
	cbz	w0,.n
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x0,info
	ldr	x0,[x0,ARE_DIR]
	cbnz	x0,.n
	mov	x0,x21
	mov	x1,x22
	bl	ar_load
	tbnz	x0,63,.erreur
	mov	x27,x0
	bl	ar_data
	mov	x25,x0
	mov	x0,x27
	bl	ar_bytes
	mov	x26,x0
.w:	cbz	x26,.wf
	mov	x0,1
	mov	x1,x25
	mov	x2,x26
	sys	SYS_WRITE
	cmp	x0,0
	b.le	.wf
	add	x25,x25,x0
	sub	x26,x26,x0
	b	.w
.wf:	mov	x0,x27
	bl	ar_free
.n:	add	x22,x22,1
	b	.e
.fin:	mov	x0,0
	b	finir
.erreur:
	bl	ar_strerror
	mov	x1,x0
	adr	x0,info
	ldr	x0,[x0,ARE_NAME]
	bl	zc_err
	mov	x0,2
	b	finir

; --- l'extraction ----------------------------------------------------------------------------
do_extract:
	getv	x0,g_quiet
	cbnz	x0,.q
	adr	x0,m_archive
	bl	t_dire
	adr	x0,pbuf
	bl	t_puts
	bl	t_nl
.q:	mov	x0,0				; les reponses : sur l'entree standard
	bl	t_lire_debut
	mov	x22,0
.e:	cmp	x22,x23
	b.hs	.fin
	ldrb	w0,[x24,x22]
	cbz	w0,.n
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	bl	ar_entry
	adr	x28,info
	ldr	x26,[x28,ARE_NAME]		; x26 : le nom
	ldr	x27,[x28,ARE_DIR]		; x27 : 1 un dossier
	mov	x0,x26
	bl	zc_dotdot
	cbz	x0,.sur
	mov	x0,x26				; « .. » : jamais hors du dossier
	adr	x1,m_unsafe
	bl	zc_err
	adr	x1,g_skipped
	ldr	x0,[x1]
	add	x0,x0,1
	str	x0,[x1]
	b	.n
.sur:	getv	x0,g_junk
	cbz	x0,.lead
	cbnz	x27,.n				; -j : pas de dossiers
	mov	x0,x26
	bl	t_base
	mov	x26,x0
.lead:	ldrb	w0,[x26]			; (sans / en tete)
	cmp	w0,'/'
	b.ne	.chem
	add	x26,x26,1
	b	.lead
.chem:	ldrb	w0,[x26]
	cbz	w0,.n
	mov	x25,x26				; x25 : le chemin ou ecrire
	getv	x0,g_dest
	cbz	x0,.p
	mov	x1,x26
	bl	zc_join
	mov	x25,x0
.p:	cbnz	x27,.dossier
	mov	x0,x25
	bl	t_type
	tbnz	x0,63,.go
	getv	x0,g_ow
	cmp	x0,1
	b.eq	.go
	cmp	x0,2
	b.eq	.saute
	mov	x0,x25
	bl	ask
	cbz	x0,.saute
.go:	mov	x0,x21
	mov	x1,x22
	mov	x2,x25
	bl	ar_extract
	tbnz	x0,63,.erreur
	adr	x1,g_files
	ldr	x0,[x1]
	add	x0,x0,1
	str	x0,[x1]
	getv	x0,g_quiet
	cbnz	x0,.n
	mov	x0,2
	bl	spaces
	adr	x0,m_inflating
	bl	t_dire
	mov	x0,x25
	bl	t_puts
	bl	t_nl
	b	.n
.dossier:
	mov	x0,x21
	mov	x1,x22
	mov	x2,x25
	bl	ar_extract
	tbz	x0,63,.n
.erreur:
	bl	ar_strerror
	mov	x1,x0
	mov	x0,x25
	bl	zc_err
	mov	x0,2
	setv	g_rc,x0,x1
	b	.n
.saute:	adr	x1,g_skipped
	ldr	x0,[x1]
	add	x0,x0,1
	str	x0,[x1]
.n:	add	x22,x22,1
	b	.e
.fin:	getv	x0,g_rc
	cmp	x0,2
	b.eq	.code
	getv	x0,g_quiet
	cbnz	x0,.rc
	getv	x0,g_files
	bl	t_putn
	getv	x0,g_files
	cmp	x0,1
	adr	x0,m_extr1
	adr	x1,m_extrn
	csel	x0,x0,x1,eq
	bl	t_dire
	getv	x0,g_skipped
	cbz	x0,.pt
	adr	x0,m_some
	bl	t_dire
.pt:	adr	x0,s_dotnl
	bl	t_puts
.rc:	getv	x0,g_skipped
	cmp	x0,0
	cset	x0,ne
	b	finir
.code:	mov	x0,2
	b	finir

; x0 = chemin d'un fichier qui existe -> x0 = 1 : le remplacer
ask:
	prologue
	stp	x19,x21,[sp,-16]!
	mov	x19,x0
.q:	adr	x0,m_repl
	bl	t_dire
	mov	x0,x19
	bl	t_puts
	adr	x0,m_reply
	bl	t_dire
	bl	t_flush
	bl	t_lire_ligne
	cbz	x0,.non
	ldrb	w0,[x0]
	cmp	w0,'o'
	b.eq	.oui
	cmp	w0,'y'
	b.eq	.oui
	cmp	w0,'n'
	b.eq	.non
	cmp	w0,'t'
	b.eq	.tous
	cmp	w0,'A'
	b.eq	.tous
	cmp	w0,'r'
	b.eq	.rien
	cmp	w0,'N'
	b.eq	.rien
	b	.q
.tous:	mov	x0,1
	setv	g_ow,x0,x1
	b	.oui
.rien:	mov	x0,2
	setv	g_ow,x0,x1
.non:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x19,x21,[sp],16
	epilogue

include '../zip/zipcli.inc'

m_usage		db 'unzip -- extrait, liste, essaie des archives ZIP',10
		db 'usage : unzip [-l|-v|-t|-p] [-o|-n] [-j] [-q] [-d DOSSIER] ARCHIVE [NOM...] [-x NOM...]',10
		db '  -l liste  -v liste d',0xC3,0xA9,'taill',0xC3,0xA9,'e  -t essai  -p vers la sortie',10
		db '  -o remplace les fichiers  -n ne remplace jamais  -j sans dossiers  -d DOSSIER o',0xC3,0xB9,'  -q silencieux',10,0
m_crypt		db '-P : les archives chiffr',0xC3,0xA9,'es ne sont pas prises en charge',0
m_opt		db 'option inconnue',0
m_nomatch	db 'rien ne correspond dans l',0x27,'archive',0
m_unsafe	db 'nom refus',0xC3,0xA9,' (il sort du dossier)',0
m_archive	db 'Archive : ',0
m_hl		db '   Taille     Date      Heure   Nom',10
		db '---------  ---------- -----   ---',10,0
m_hv		db '  Taille  M',0xC3,0xA9,'thode  Compr.  Taux    Date      Heure  CRC-32    Nom',10
		db '--------  ------  -------- ---- ---------- -----  --------  ---',10,0
m_file1		db ' fichier',0
m_files		db ' fichiers',0
m_testing	db 'essai : ',0
m_tbad1		db 'Au moins une erreur dans ',0
m_tbad2		db ' (',0
m_tbad3		db ' sur ',0
m_tbad4		db ' fichiers).',10,0
m_tgood		db 'Aucune erreur dans les donn',0xC3,0xA9,'es compress',0xC3,0xA9,'es de ',0
m_inflating	db 'extrait : ',0
m_extr1		db ' fichier extrait',0
m_extrn		db ' fichiers extraits',0
m_some		db ', certains saut',0xC3,0xA9,'s',0
m_repl		db 'remplacer ',0
m_reply		db ' ? [o]ui, [n]on, [t]ous, [r]ien : ',0
s_x		db '-x',0
s_d		db '-d',0
s_zip		db '.zip',0
s_2sp		db '  ',0
s_3sp		db '   ',0
s_ok		db 'OK',0
s_dotnl		db '.',10,0
s_defl		db 'Defl:N',0
s_stored	db 'Stored',0
s_qm		db '?',0
s_tl		db '---------                     -------',10,0
s_tv		db '--------          -------- ----                            -------',10,0
		align 4

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
		align 8
g_mode		dd 0
g_ow		dd 0			; 0 demander, 1 remplacer, 2 jamais
g_junk		dd 0
g_quiet		dd 0
g_dest		dd 0
g_path		dd 0
g_inexcl	dd 0
g_pat		dd 0
g_np		dd 0
g_excl		dd 0
g_nx		dd 0
g_rc		dd 0
g_files		dd 0
g_skipped	dd 0
info		rb AR_ENTRY_SIZE
optbuf		rb 8
pbuf		rb 1040
