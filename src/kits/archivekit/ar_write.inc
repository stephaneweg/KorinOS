; ============================================================================
;  ar_write.inc - creer une archive : ZIP, tar, tar.gz, gz, dans un fichier ou
;  en memoire. Tout est ecrit a la suite. ZIP : en-tete local avec des champs
;  provisoires, donnees (deflate ou stockees), puis l'en-tete est corrige ;
;  aucun extra, aucun descripteur de donnees : un mimetype d'ODF ajoute en
;  premier au niveau 0 est bien a l'octet 30, sans rien autour. Une entree de
;  memoire que deflate ne reduit pas est stockee.
; ============================================================================

W_FMT		= 24
W_DF		= 32			; etat de deflate
W_SINK		= 40			; le puits de l'archive
W_CD		= W_SINK + SK_SIZE	; le repertoire central, en memoire
W_N		= W_CD + SK_SIZE	; entrees
W_TIME		= W_N + 8		; date imposee (0 : l'heure courante)
W_DONE		= W_N + 16
W_TARPOS	= W_N + 24		; octets du tar deja ecrits
W_EOFF		= W_N + 32		; ZIP : en-tete local de l'entree en cours
W_EDATA		= W_N + 40		; ZIP : debut de ses donnees
W_ESIZE		= W_N + 48		; octets recus pour l'entree
W_ECRC		= W_N + 56
W_ELEVEL	= W_N + 64
W_ETIME		= W_N + 72		; date DOS (ZIP), secondes (tar)
W_ENLEN		= W_N + 80		; longueur du nom
W_EUNIX		= W_N + 88		; date, secondes
W_EDIR		= W_N + 96
W_NAME		= 512			; le nom propre de l'entree en cours (4096)
WR_NAMEMAX	= 3900
WR_SIZE		= 8192 - 16

; x0 = poignee, bad -> x19 = ecrivain non termine
macro wr_handle bad* {
	tbnz	x0,63,bad
	cbz	x0,bad
	ldr	x4,[x0,H_MAGIC]
	movl	x5,AR_MAGIC_WR
	cmp	x4,x5
	b.ne	bad
	mov	x19,x0
	ldr	x4,[x19,W_DONE]
	cbnz	x4,bad
}

; x0 = format, x1 = genre de puits, x2 = descripteur -> x0 = ecrivain, erreur
wr_new:
	ar_enter
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	sub	x0,x20,1
	cmp	x0,AR_FMT_GZIP-1
	b.hi	.bad
	mov	x0,WR_SIZE
	bl	ar_alloc
	cbz	x0,.nomem
	mov	x19,x0
	movl	x1,AR_MAGIC_WR
	str	x1,[x19,H_MAGIC]
	str	x20,[x19,W_FMT]
	str	x21,[x19,W_SINK+SK_KIND]
	cmp	x21,SKK_FD
	b.ne	.a
	str	x22,[x19,W_SINK+SK_PTR]
.a:	mov	x0,SKK_GROW
	str	x0,[x19,W_CD+SK_KIND]
	cmp	x20,AR_FMT_TGZ
	b.ne	.ok
	; un seul flux deflate pour tout le tar
	bl	wr_df
	cbz	x0,.nomem
	mov	x1,AR_DEFAULT
	mov	x2,AR_GZIP
	bl	df_begin
	add	x0,x19,W_SINK
	mov	x1,AR_GZIP
	mov	x2,AR_DEFAULT
	bl	wrap_head
	cbnz	x0,.fail
.ok:	mov	x0,x19
	b	.out
.fail:	mov	x20,x0
	mov	x0,x19
	bl	wr_free
	mov	x0,x20
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x19 = ecrivain -> x0 = etat de deflate, 0 si plus de memoire
wr_df:
	prologue
	ldr	x0,[x19,W_DF]
	cbnz	x0,.out
	add	x0,x19,W_SINK
	bl	df_new
	str	x0,[x19,W_DF]
.out:	epilogue

; x0 = chemin, x1 = format -> ecrivain
ar_create:
	bl_kits
	ar_enter
	mov	x19,x0
	mov	x20,x1
	sub	x0,x20,1
	cmp	x0,AR_FMT_GZIP-1
	b.hi	.bad
	mov	x0,x19
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x0,x20
	mov	x1,SKK_FD
	mov	x2,x21
	bl	wr_new
	tbz	x0,63,.out
	mov	x20,x0
	mov	x0,x21
	bl	f_fermer
	mov	x0,x20
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x0 = format -> ecrivain
ar_create_mem:
	bl_kits
	mov	x1,SKK_GROW
	mov	x2,0
	b	wr_new

; x0 = ecrivain -> 0
wr_free:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	str	xzr,[x19,H_MAGIC]
	ldr	x0,[x19,W_SINK+SK_KIND]
	cmp	x0,SKK_FD
	b.ne	.m
	ldr	x0,[x19,W_DONE]
	cbnz	x0,.d
	ldr	x0,[x19,W_SINK+SK_PTR]
	bl	f_fermer
	b	.d
.m:	ldr	x0,[x19,W_SINK+SK_PTR]
	bl	ar_mfree
.d:	ldr	x0,[x19,W_CD+SK_PTR]
	bl	ar_mfree
	ldr	x0,[x19,W_DF]
	bl	ar_mfree
	mov	x0,x19
	bl	ar_mfree
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; x0 = ecrivain, x1 = secondes UTC -> 0
ar_set_time:
	prologue
	str	x19,[sp,-16]!
	wr_handle .bad
	str	x1,[x19,W_TIME]
	mov	x0,0
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; secondes UTC -> x0 = date DOS (date << 16 | heure) ; avant 1980 : 1980-01-01
unix_dos:
	movl	x1,315532800
	cmp	x0,x1
	b.hs	.a
	mov	x0,0x21
	lsl	x0,x0,16
	ret
.a:	movl	x1,86400
	udiv	x2,x0,x1			; jours
	msub	x3,x2,x1,x0			; secondes du jour
	; jours -> annee, mois, jour
	movl	x1,719468
	add	x2,x2,x1
	movl	x1,146097
	udiv	x4,x2,x1			; ere
	msub	x5,x4,x1,x2			; jour de l'ere
	mov	x1,1460
	udiv	x6,x5,x1
	sub	x7,x5,x6
	movl	x1,36524
	udiv	x6,x5,x1
	add	x7,x7,x6
	movl	x1,146096
	udiv	x6,x5,x1
	sub	x7,x7,x6
	mov	x1,365
	udiv	x7,x7,x1			; annee de l'ere
	mov	x1,400
	madd	x8,x4,x1,x7			; annee
	mov	x1,365
	mul	x9,x7,x1
	add	x9,x9,x7,lsr 2
	mov	x1,100
	udiv	x6,x7,x1
	sub	x9,x9,x6
	sub	x5,x5,x9			; jour de l'annee (depuis mars)
	add	x6,x5,x5,lsl 2
	add	x6,x6,2
	mov	x1,153
	udiv	x6,x6,x1			; mois depuis mars
	mul	x9,x6,x1
	add	x9,x9,2
	mov	x1,5
	udiv	x9,x9,x1
	sub	x5,x5,x9
	add	x5,x5,1				; jour
	add	x6,x6,3
	cmp	x6,12
	b.ls	.b
	sub	x6,x6,12
	add	x8,x8,1
.b:	sub	x8,x8,1980
	mov	x1,127
	cmp	x8,x1
	csel	x8,x1,x8,hi
	lsl	x0,x8,25
	orr	x0,x0,x6,lsl 21
	orr	x0,x0,x5,lsl 16
	mov	x1,3600
	udiv	x4,x3,x1
	msub	x3,x4,x1,x3
	orr	x0,x0,x4,lsl 11
	mov	x1,60
	udiv	x4,x3,x1
	msub	x3,x4,x1,x3
	orr	x0,x0,x4,lsl 5
	orr	x0,x0,x3,lsr 1
	ret

; x19 = ecrivain, x0 = nom : W_NAME, W_ENLEN -> x0 = 0, erreur. Sans / au debut, \ -> /,
; sans / a la fin
wr_name:
	cbz	x0,.bad
	mov	x1,W_NAME
	add	x1,x19,x1
	mov	x2,x1
.lead:	ldrb	w3,[x0]
	cmp	w3,'/'
	b.eq	.l1
	cmp	w3,'\'
	b.ne	.l2
.l1:	add	x0,x0,1
	b	.lead
.l2:	cmp	w3,'.'
	b.ne	.c
	ldrb	w3,[x0,1]
	cmp	w3,'/'
	b.ne	.c
	add	x0,x0,2
	b	.lead
.c:	ldrb	w3,[x0],1
	cbz	w3,.e
	cmp	w3,'\'
	b.ne	.p
	mov	w3,'/'
.p:	strb	w3,[x2],1
	sub	x4,x2,x1
	cmp	x4,WR_NAMEMAX
	b.lo	.c
.bad:	mov	x0,AR_E_INVAL
	ret
.e:	cmp	x2,x1
	b.eq	.bad
	sub	x4,x2,1
	ldrb	w3,[x4]
	cmp	w3,'/'
	b.ne	.f
	mov	x2,x4
	b	.e
.f:	strb	wzr,[x2]
	sub	x2,x2,x1
	str	x2,[x19,W_ENLEN]
	mov	x0,0
	ret

; x19 : W_EUNIX et W_ETIME -> touche x0-x9 (x0 = secondes, 0 : maintenant)
wr_stamp:
	prologue
	cbnz	x0,.t
	ldr	x0,[x19,W_TIME]
	cbnz	x0,.t
	mov	x0,0
	sys	SYS_CLOCK_GET
	cmp	x0,0
	b.gt	.t
	mov	x0,0
.t:	str	x0,[x19,W_EUNIX]
	bl	unix_dos
	str	x0,[x19,W_ETIME]
	epilogue

; x19, x1 = donnees, x2 = octets : a l'archive (a travers le gzip pour un tar.gz) -> x0
wr_out:
	ldr	x0,[x19,W_FMT]
	cmp	x0,AR_FMT_TGZ
	b.eq	.gz
	add	x0,x19,W_SINK
	b	sink_write
.gz:	ldr	x0,[x19,W_DF]
	b	df_feed

; --- tar --------------------------------------------------------------------------

; x0 = valeur, x1 = champ, x2 = chiffres : octal, complete par des 0, puis un 0 final
tar_oct:
	strb	wzr,[x1,x2]
.a:	cbz	x2,.e
	sub	x2,x2,1
	and	w3,w0,7
	add	w3,w3,'0'
	strb	w3,[x1,x2]
	lsr	x0,x0,3
	b	.a
.e:	ret

; x19, x0 = en-tete (512 o, nom deja en place), x1 = taille, x2 = date, x3 = type (lettre)
; -> en-tete complet, a ecrire
tar_fill:
	mov	x8,x0
	mov	x9,x1
	mov	x10,x2
	mov	x11,x3
	mov	x12,x30
	mov	x0,420				; 0644
	cmp	x11,'5'
	b.ne	.m
	mov	x0,493				; 0755
.m:	add	x1,x8,100
	mov	x2,7
	bl	tar_oct
	mov	x0,0
	add	x1,x8,108
	mov	x2,7
	bl	tar_oct
	mov	x0,0
	add	x1,x8,116
	mov	x2,7
	bl	tar_oct
	mov	x0,x9
	add	x1,x8,124
	mov	x2,11
	bl	tar_oct
	mov	x0,x10
	add	x1,x8,136
	mov	x2,11
	bl	tar_oct
	strb	w11,[x8,156]
	movl	x0,0x3030007261747375		; "ustar\0" "00"
	add	x1,x8,257
	str	x0,[x1]
	; somme, le champ compte pour des blancs
	movl	x0,0x2020202020202020
	str	x0,[x8,148]
	mov	x0,0
	mov	x1,0
.s:	ldrb	w2,[x8,x1]
	add	x0,x0,x2
	add	x1,x1,1
	cmp	x1,512
	b.lo	.s
	add	x1,x8,148
	mov	x2,6
	bl	tar_oct
	mov	w0,' '
	strb	w0,[x8,155]
	mov	x30,x12
	ret

; x19, x0 = taille, x1 = type : l'en-tete de l'entree W_NAME -> x0 = 0, erreur
tar_head:
	ar_enter
	sub	sp,sp,512
	mov	x20,x0
	mov	x21,x1
	mov	x0,sp
	mov	w1,0
	mov	x2,512
	bl	ar_memset
	mov	x22,W_NAME
	add	x22,x19,x22
	ldr	x23,[x19,W_ENLEN]
	cmp	x21,'5'				; (les dossiers finissent par /)
	b.ne	.a
	mov	w0,'/'
	strb	w0,[x22,x23]
	add	x23,x23,1
	strb	wzr,[x22,x23]
.a:	cmp	x23,100
	b.ls	.plain
	; prefixe : couper a un / qui laisse au plus 155 + 100
	mov	x0,x23
	mov	x1,155
	cmp	x0,x1
	csel	x0,x1,x0,hi			; le dernier / possible
.cut:	cbz	x0,.long
	sub	x0,x0,1
	ldrb	w1,[x22,x0]
	cmp	w1,'/'
	b.ne	.cut
	sub	x1,x23,x0
	sub	x1,x1,1
	cmp	x1,100
	b.hi	.long
	cbz	x1,.long
	mov	x24,x0
	add	x0,sp,345
	mov	x1,x22
	mov	x2,x24
	bl	ar_memcpy
	mov	x0,sp
	add	x1,x22,x24
	add	x1,x1,1
	sub	x2,x23,x24
	sub	x2,x2,1
	bl	ar_memcpy
	b	.fill
.long:	; nom long de GNU : un en-tete L, le nom, puis l'en-tete tronque
	adr	x1,s_longlink
	mov	x0,sp
	mov	x2,14
	bl	ar_memcpy
	mov	x0,sp
	add	x1,x23,1
	mov	x2,0
	mov	x3,'L'
	bl	tar_fill
	mov	x1,sp
	mov	x2,512
	bl	wr_out
	cbnz	x0,.out
	mov	x1,x22
	add	x2,x23,1
	bl	wr_out
	cbnz	x0,.out
	add	x0,x23,1
	bl	tar_pad_of
	mov	x2,x0
	mov	x24,x0
	bl	wr_zeros
	cbnz	x0,.out
	add	x0,x23,1
	add	x0,x0,512
	add	x0,x0,x24
	ldr	x1,[x19,W_TARPOS]
	add	x0,x0,x1
	str	x0,[x19,W_TARPOS]
	mov	x0,sp
	mov	w1,0
	mov	x2,512
	bl	ar_memset
	mov	x23,100
.plain:	mov	x0,sp
	mov	x1,x22
	mov	x2,x23
	bl	ar_memcpy
.fill:	mov	x0,sp
	mov	x1,x20
	ldr	x2,[x19,W_EUNIX]
	mov	x3,x21
	bl	tar_fill
	mov	x1,sp
	mov	x2,512
	bl	wr_out
	cbnz	x0,.out
	ldr	x0,[x19,W_TARPOS]
	add	x0,x0,512
	str	x0,[x19,W_TARPOS]
	mov	x0,0
.out:	add	sp,sp,512
	ar_leave

; x0 = octets -> x0 = bourrage jusqu'a 512
tar_pad_of:
	neg	x0,x0
	and	x0,x0,511
	ret

; x19, x2 = octets (<= 512 fois 20) : des zeros a l'archive -> x0
wr_zeros:
	prologue
	stp	x20,x21,[sp,-16]!
	sub	sp,sp,512
	mov	x20,x2
	mov	x0,sp
	mov	w1,0
	mov	x2,512
	bl	ar_memset
.a:	cbz	x20,.ok
	mov	x2,512
	cmp	x20,x2
	csel	x2,x20,x2,lo
	sub	x20,x20,x2
	mov	x1,sp
	bl	wr_out
	cbz	x0,.a
	b	.out
.ok:	mov	x0,0
.out:	add	sp,sp,512
	ldp	x20,x21,[sp],16
	epilogue

; --- une entree, en trois temps ------------------------------------------------------

; x19, W_NAME pret, W_EUNIX, x0 = taille annoncee, x1 = niveau -> x0 = 0, erreur
wr_begin:
	ar_enter
	mov	x20,x0
	mov	x21,x1
	str	x1,[x19,W_ELEVEL]
	str	xzr,[x19,W_ESIZE]
	str	xzr,[x19,W_ECRC]
	str	xzr,[x19,W_EDIR]
	ldr	x0,[x19,W_FMT]
	cmp	x0,AR_FMT_ZIP
	b.eq	.zip
	cmp	x0,AR_FMT_GZIP
	b.eq	.gz
	movl	x1,1 shl 33
	cmp	x20,x1
	b.hs	.big
	mov	x0,x20
	mov	x1,'0'
	bl	tar_head
	b	.out
.gz:	ldr	x0,[x19,W_N]
	cbnz	x0,.bad
	mov	x0,1
	str	x0,[x19,W_N]
	bl	gz_head
	cbnz	x0,.out
	bl	wr_df
	cbz	x0,.nomem
	mov	x1,x21
	mov	x2,AR_GZIP
	bl	df_begin
	mov	x0,0
	b	.out
.zip:	movl	x1,0xFFFFFFFF
	cmp	x20,x1
	b.hs	.big
	ldr	x0,[x19,W_SINK+SK_POS]
	cmp	x0,x1
	b.hs	.big
	str	x0,[x19,W_EOFF]
	mov	x0,x21
	bl	zip_local
	cbnz	x0,.out
	ldr	x0,[x19,W_SINK+SK_POS]
	str	x0,[x19,W_EDATA]
	cbz	x21,.ok
	bl	wr_df
	cbz	x0,.nomem
	mov	x1,x21
	mov	x2,AR_RAW
	bl	df_begin
.ok:	mov	x0,0
	b	.out
.big:	mov	x0,AR_E_FBIG
	b	.out
.bad:	mov	x0,AR_E_INVAL
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
.out:	ar_leave

; x19, x1 = donnees, x2 = octets -> x0 = 0, erreur
wr_write:
	ar_enter
	mov	x20,x1
	mov	x21,x2
	ldr	x0,[x19,W_ESIZE]
	add	x0,x0,x21
	str	x0,[x19,W_ESIZE]
	ldr	x0,[x19,W_FMT]
	cmp	x0,AR_FMT_ZIP
	b.ne	.ot
	ldr	x0,[x19,W_ECRC]
	mov	x1,x20
	mov	x2,x21
	bl	ar_crc32
	str	x0,[x19,W_ECRC]
	ldr	x0,[x19,W_ELEVEL]
	cbz	x0,.st
.df:	ldr	x0,[x19,W_DF]
	mov	x1,x20
	mov	x2,x21
	bl	df_feed
	b	.out
.st:	add	x0,x19,W_SINK
	mov	x1,x20
	mov	x2,x21
	bl	sink_write
	b	.out
.ot:	cmp	x0,AR_FMT_GZIP
	b.eq	.df
	mov	x1,x20
	mov	x2,x21
	bl	wr_out
	cbnz	x0,.out
	ldr	x0,[x19,W_TARPOS]
	add	x0,x0,x21
	str	x0,[x19,W_TARPOS]
	mov	x0,0
.out:	ar_leave

; x19, x0 = taille annoncee (tar), x1 = les donnees si elles sont en memoire (0 : non),
; x2 = leur taille -> x0 = 0, erreur
wr_end:
	ar_enter
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	ldr	x0,[x19,W_FMT]
	cmp	x0,AR_FMT_ZIP
	b.eq	.zip
	cmp	x0,AR_FMT_GZIP
	b.eq	.ok
	ldr	x0,[x19,W_ESIZE]
	cmp	x0,x20
	b.ne	.io				; le fichier a change sous nos yeux
	mov	x0,x20
	bl	tar_pad_of
	mov	x2,x0
	mov	x20,x0
	bl	wr_zeros
	cbnz	x0,.out
	ldr	x0,[x19,W_TARPOS]
	add	x0,x0,x20
	str	x0,[x19,W_TARPOS]
.ok:	mov	x0,0
	b	.out
.io:	mov	x0,AR_E_IO
	b	.out
.zip:	mov	x23,0				; methode
	ldr	x0,[x19,W_ELEVEL]
	cbz	x0,.close
	mov	x23,8
	ldr	x0,[x19,W_DF]
	bl	df_finish
	cbnz	x0,.out
	; pas plus petit : stocker, si les donnees sont la et qu'on peut revenir en arriere
	cbz	x21,.close
	ldr	x0,[x19,W_SINK+SK_POS]
	ldr	x1,[x19,W_EDATA]
	sub	x0,x0,x1			; compresse
	cmp	x0,x22
	b.lo	.close
	ldr	x2,[x19,W_SINK+SK_KIND]
	cmp	x2,SKK_FD
	b.ne	.back
	sub	x2,x0,x22
	cmp	x2,64				; (le repertoire central recouvrira l'exces)
	b.hi	.close
	ldr	x0,[x19,W_SINK+SK_PTR]
	mov	x2,0
	bl	f_position
	tbnz	x0,63,.out
.back:	ldr	x1,[x19,W_EDATA]
	str	x1,[x19,W_SINK+SK_POS]
	add	x0,x19,W_SINK
	mov	x1,x21
	mov	x2,x22
	bl	sink_write
	cbnz	x0,.out
	mov	x23,0
.close:	; l'en-tete local corrige
	ldr	x24,[x19,W_SINK+SK_POS]
	ldr	x1,[x19,W_EDATA]
	sub	x25,x24,x1			; compresse
	movl	x0,0xFFFFFFFF
	cmp	x24,x0
	b.hs	.big
	ldr	x0,[x19,W_EOFF]
	add	x0,x0,8
	mov	x1,x23
	mov	x2,2
	bl	zip_patch
	cbnz	x0,.out
	ldr	x0,[x19,W_EOFF]
	add	x0,x0,14
	ldr	x1,[x19,W_ECRC]
	mov	x2,4
	bl	zip_patch
	cbnz	x0,.out
	ldr	x0,[x19,W_EOFF]
	add	x0,x0,18
	mov	x1,x25
	mov	x2,4
	bl	zip_patch
	cbnz	x0,.out
	ldr	x0,[x19,W_EOFF]
	add	x0,x0,22
	ldr	x1,[x19,W_ESIZE]
	mov	x2,4
	bl	zip_patch
	cbnz	x0,.out
	ldr	x0,[x19,W_SINK+SK_KIND]
	cmp	x0,SKK_FD
	b.ne	.cd
	ldr	x0,[x19,W_SINK+SK_PTR]
	mov	x1,x24
	mov	x2,0
	bl	f_position
	tbnz	x0,63,.out
.cd:	mov	x0,x23
	mov	x1,x25
	bl	zip_central
	b	.out
.big:	mov	x0,AR_E_FBIG
.out:	ar_leave

; x19, x0 = position, x1 = valeur, x2 = octets : corrige ce qui est deja ecrit -> x0
zip_patch:
	sub	sp,sp,32
	stp	x29,x30,[sp,16]
	str	x1,[sp]
	ldr	x3,[x19,W_SINK+SK_KIND]
	cmp	x3,SKK_FD
	b.eq	.fd
	ldr	x3,[x19,W_SINK+SK_PTR]
	add	x0,x3,x0
	mov	x1,sp
	bl	ar_memcpy
	mov	x0,0
	b	.out
.fd:	str	x2,[sp,8]
	mov	x1,x0
	ldr	x0,[x19,W_SINK+SK_PTR]
	mov	x2,0
	bl	f_position
	tbnz	x0,63,.out
	ldr	x0,[x19,W_SINK+SK_PTR]
	mov	x1,sp
	ldr	x2,[sp,8]
	bl	f_ecrire
	tbnz	x0,63,.out
	mov	x0,0
.out:	ldp	x29,x30,[sp,16]
	add	sp,sp,32
	ret

; x19 -> drapeaux de l'entree W_NAME : x0 = bit 11 si le nom n'est pas ASCII
zip_flags:
	mov	x1,W_NAME
	add	x1,x19,x1
	mov	x0,0
.a:	ldrb	w2,[x1],1
	cbz	w2,.e
	cmp	w2,0x80
	b.lo	.a
	mov	x0,1 shl 11
.e:	ret

; x19, x0 = niveau : l'en-tete local provisoire et le nom -> x0 = 0, erreur
zip_local:
	prologue
	sub	sp,sp,32
	mov	x3,x0
	movl	x0,ZIP_LOCAL
	str	w0,[sp]
	mov	w0,20
	strh	w0,[sp,4]
	bl	zip_flags
	strh	w0,[sp,6]
	mov	w0,0
	cbz	x3,.m
	mov	w0,8
.m:	strh	w0,[sp,8]
	ldr	x0,[x19,W_ETIME]
	str	w0,[sp,10]
	str	wzr,[sp,14]
	str	wzr,[sp,18]
	str	wzr,[sp,22]
	ldr	x0,[x19,W_ENLEN]
	strh	w0,[sp,26]
	strh	wzr,[sp,28]
	add	x0,x19,W_SINK
	mov	x1,sp
	mov	x2,30
	bl	sink_write
	cbnz	x0,.out
	add	x0,x19,W_SINK
	mov	x1,W_NAME
	add	x1,x19,x1
	ldr	x2,[x19,W_ENLEN]
	bl	sink_write
.out:	add	sp,sp,32
	epilogue

; x19, x0 = methode, x1 = octets compresses : la fiche du repertoire central -> x0 = 0, erreur
zip_central:
	prologue
	sub	sp,sp,64
	mov	x3,x0
	mov	x4,x1
	movl	x0,ZIP_CENTRAL
	str	w0,[sp]
	movz	w0,0x031E			; Unix, version 3.0
	strh	w0,[sp,4]
	mov	w0,20
	strh	w0,[sp,6]
	bl	zip_flags
	strh	w0,[sp,8]
	strh	w3,[sp,10]
	ldr	x0,[x19,W_ETIME]
	str	w0,[sp,12]
	ldr	x0,[x19,W_ECRC]
	str	w0,[sp,16]
	str	w4,[sp,20]
	ldr	x0,[x19,W_ESIZE]
	str	w0,[sp,24]
	ldr	x0,[x19,W_ENLEN]
	strh	w0,[sp,28]
	strh	wzr,[sp,30]
	strh	wzr,[sp,32]
	strh	wzr,[sp,34]
	strh	wzr,[sp,36]
	movl	x0,0x81A40000			; -rw-r--r--
	ldr	x1,[x19,W_EDIR]
	cbz	x1,.a
	movl	x0,0x41ED0010			; drwxr-xr-x, dossier
.a:	str	w0,[sp,38]
	ldr	x0,[x19,W_EOFF]
	str	w0,[sp,42]
	add	x0,x19,W_CD
	mov	x1,sp
	mov	x2,46
	bl	sink_write
	cbnz	x0,.out
	add	x0,x19,W_CD
	mov	x1,W_NAME
	add	x1,x19,x1
	ldr	x2,[x19,W_ENLEN]
	bl	sink_write
	cbnz	x0,.out
	ldr	x0,[x19,W_N]
	add	x0,x0,1
	str	x0,[x19,W_N]
	mov	x0,0
.out:	add	sp,sp,64
	epilogue

; x19 : l'en-tete gzip, avec le nom W_NAME et la date -> x0 = 0, erreur
gz_head:
	prologue
	sub	sp,sp,16
	movz	w0,0x8B1F
	strh	w0,[sp]
	mov	w0,8
	strb	w0,[sp,2]
	mov	w0,8				; FNAME
	strb	w0,[sp,3]
	ldr	x0,[x19,W_EUNIX]
	str	w0,[sp,4]
	strb	wzr,[sp,8]
	mov	w0,3
	strb	w0,[sp,9]
	add	x0,x19,W_SINK
	mov	x1,sp
	mov	x2,10
	bl	sink_write
	cbnz	x0,.out
	add	x0,x19,W_SINK
	mov	x1,W_NAME
	add	x1,x19,x1
	ldr	x2,[x19,W_ENLEN]
	add	x2,x2,1
	bl	sink_write
.out:	add	sp,sp,16
	epilogue

; --- l'interface ---------------------------------------------------------------------

; x0 = ecrivain, x1 = nom, x2 = donnees, x3 = octets, x4 = niveau -> 0
ar_add_mem:
	mov	x6,x4
	ar_enter
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x6
	wr_handle .bad
	mov	x0,x23
	bl	ar_level
	tbnz	x0,63,.out
	mov	x23,x0
	mov	x0,x20
	bl	wr_name
	cbnz	x0,.out
	mov	x0,0
	bl	wr_stamp
	mov	x0,x22
	mov	x1,x23
	bl	wr_begin
	cbnz	x0,.out
	mov	x1,x21
	mov	x2,x22
	bl	wr_write
	cbnz	x0,.out
	mov	x0,x22
	mov	x1,x21
	mov	x2,x22
	bl	wr_end
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x0 = ecrivain, x1 = nom -> 0 : une entree dossier
ar_add_dir:
	ar_enter
	mov	x20,x1
	wr_handle .bad
	mov	x0,x20
	bl	wr_name
	cbnz	x0,.out
	mov	x0,0
	bl	wr_stamp
	bl	wr_dir
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x19, W_NAME, W_EUNIX : une entree dossier -> x0
wr_dir:
	prologue
	ldr	x0,[x19,W_FMT]
	cmp	x0,AR_FMT_GZIP
	b.eq	.bad
	cmp	x0,AR_FMT_ZIP
	b.eq	.zip
	mov	x0,0
	mov	x1,'5'
	bl	tar_head
	epilogue
.zip:	; ZIP : le nom finit par /
	mov	x0,W_NAME
	add	x0,x19,x0
	ldr	x1,[x19,W_ENLEN]
	mov	w2,'/'
	strb	w2,[x0,x1]
	add	x1,x1,1
	strb	wzr,[x0,x1]
	str	x1,[x19,W_ENLEN]
	mov	x0,0
	mov	x1,0
	bl	wr_begin
	cbnz	x0,.out
	mov	x0,1
	str	x0,[x19,W_EDIR]
	mov	x0,0
	mov	x1,0
	mov	x2,0
	bl	wr_end
.out:	epilogue
.bad:	mov	x0,AR_E_INVAL
	epilogue

; x0 = chemin, x1 = tampon {taille, attributs, date} -> x0 = 0, erreur
ar_stat:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,MSG_TAILLE
	mov	x19,x1
	mov	x2,x0
	mov	x1,V_STAT
	stp	x1,xzr,[sp]
	stp	xzr,xzr,[sp,16]
	str	xzr,[sp,32]
	add	x4,sp,MSG_DATA
	mov	x5,0
.c:	cmp	x5,MSG_TAILLE-MSG_DATA-1
	b.hs	.long
	ldrb	w6,[x2,x5]
	strb	w6,[x4,x5]
	add	x5,x5,1
	cbnz	w6,.c
	str	w5,[sp,MSG_LEN]
	mov	x0,sp
	bl	ar_vfs
	tbnz	x0,63,.out
	ldr	x1,[sp,MSG_B]
	str	x1,[x19]
	ldr	x1,[sp,MSG_C]
	str	x1,[x19,8]
	ldr	x1,[sp,MSG_D]
	str	x1,[x19,16]
	mov	x0,0
	b	.out
.long:	mov	x0,AR_E_INVAL
.out:	add	sp,sp,MSG_TAILLE
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ecrivain, x1 = nom (0 : celui du fichier), x2 = chemin, x3 = niveau -> 0
ar_add_file:
	bl_kits
	ar_enter
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	wr_handle .bad
	cbz	x21,.bad
	mov	x0,x22
	bl	ar_level
	tbnz	x0,63,.out
	mov	x22,x0
	cbnz	x20,.go
	; le nom de base
	mov	x20,x21
	mov	x0,x21
.b:	ldrb	w1,[x0],1
	cbz	w1,.go
	cmp	w1,'/'
	b.eq	.b1
	cmp	w1,':'
	b.ne	.b
.b1:	ldrb	w1,[x0]
	cbz	w1,.b
	mov	x20,x0
	b	.b
.go:	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	mov	x3,0
	bl	wr_path
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x19 = ecrivain, x0 = nom, x1 = chemin, x2 = niveau, x3 = profondeur -> x0 = 0, erreur
wr_path:
	ar_enter
	sub	sp,sp,32
	mov	x20,x0
	mov	x21,x1
	mov	x22,x2
	mov	x23,x3
	cmp	x23,32
	b.hs	.bad
	mov	x0,x21
	mov	x1,sp
	bl	ar_stat
	cbnz	x0,.out
	mov	x0,x20
	bl	wr_name
	cbnz	x0,.out
	ldr	x0,[sp,16]
	bl	wr_stamp
	ldr	x0,[sp,8]
	tst	x0,ATTR_DIR
	b.ne	.dir
	; un fichier
	mov	x0,x21
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x24,x0
	ldr	x25,[sp]			; taille
	mov	x0,x25
	mov	x1,x22
	bl	wr_begin
	cbnz	x0,.fclose
	mov	x0,65536
	bl	ar_alloc
	cbz	x0,.nomemf
	mov	x26,x0
.rd:	mov	x0,x24
	mov	x1,x26
	mov	x2,65536
	bl	f_lire
	cmp	x0,0
	b.le	.rde
	mov	x1,x26
	mov	x2,x0
	bl	wr_write
	cbz	x0,.rd
	b	.rdf
.rde:	b.lt	.rdf
	mov	x0,x25
	mov	x1,0
	mov	x2,0
	bl	wr_end
.rdf:	mov	x25,x0
	mov	x0,x26
	bl	ar_mfree
	mov	x0,x25
.fclose:
	mov	x25,x0
	mov	x0,x24
	bl	f_fermer
	mov	x0,x25
	b	.out
.nomemf:
	mov	x0,AR_E_NOMEM
	b	.fclose
.dir:	; un dossier : son entree, puis ce qu'il contient
	bl	wr_dir
	cbnz	x0,.out
	mov	x0,12288
	bl	ar_alloc			; DIRENT, nom suivant, chemin suivant
	cbz	x0,.nomem
	mov	x24,x0
	mov	x0,x21
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.dfree
	mov	x25,x0
.de:	mov	x0,x25
	mov	x1,x24
	bl	f_lire_rep
	cmp	x0,0
	b.lt	.dclose
	b.eq	.ddone
	ldrb	w0,[x24]			; . et ..
	cmp	w0,'.'
	b.ne	.dg
	ldrb	w0,[x24,1]
	cbz	w0,.de
	cmp	w0,'.'
	b.ne	.dg
	ldrb	w0,[x24,2]
	cbz	w0,.de
.dg:	add	x0,x24,1024
	mov	x1,x20
	mov	x2,x24
	bl	.join
	cbnz	x0,.dclose
	add	x0,x24,2048
	add	x0,x0,2048
	mov	x1,x21
	mov	x2,x24
	bl	.join
	cbnz	x0,.dclose
	add	x0,x24,1024
	add	x1,x24,2048
	add	x1,x1,2048
	mov	x2,x22
	add	x3,x23,1
	bl	wr_path
	cbz	x0,.de
	b	.dclose
.ddone:	mov	x0,0
.dclose:
	mov	x26,x0
	mov	x0,x25
	bl	f_fermer
	mov	x0,x26
.dfree:	mov	x26,x0
	mov	x0,x24
	bl	ar_mfree
	mov	x0,x26
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	add	sp,sp,32
	ar_leave
; x0 = destination (2048 o au plus), x1 = dossier, x2 = nom -> x0 = 0, erreur
.join:	mov	x3,x0
.j1:	ldrb	w4,[x1],1
	cbz	w4,.j2
	strb	w4,[x0],1
	sub	x5,x0,x3
	cmp	x5,1500
	b.lo	.j1
	b	.jb
.j2:	sub	x4,x0,1
	cmp	x4,x3
	b.lo	.j3
	ldrb	w4,[x4]
	cmp	w4,'/'
	b.eq	.j3
	cmp	w4,':'
	b.eq	.j3
	mov	w4,'/'
	strb	w4,[x0],1
.j3:	ldrb	w4,[x2],1
	strb	w4,[x0],1
	cbnz	w4,.j3
	mov	x0,0
	ret
.jb:	mov	x0,AR_E_INVAL
	ret

; x0 = ecrivain -> octets de l'archive
ar_finish:
	ar_enter
	wr_handle .bad
	ldr	x0,[x19,W_FMT]
	cmp	x0,AR_FMT_ZIP
	b.eq	.zip
	cmp	x0,AR_FMT_GZIP
	b.eq	.gz
	; tar : deux blocs de zeros, puis jusqu'a un enregistrement de 10240 octets
	ldr	x0,[x19,W_TARPOS]
	add	x0,x0,1024
	mov	x1,10240
	udiv	x2,x0,x1
	msub	x2,x2,x1,x0
	sub	x2,x1,x2
	udiv	x3,x2,x1
	msub	x2,x3,x1,x2
	add	x2,x2,1024
	bl	wr_zeros
	cbnz	x0,.out
	ldr	x0,[x19,W_FMT]
	cmp	x0,AR_FMT_TGZ
	b.ne	.done
	b	.gzend
.gz:	ldr	x0,[x19,W_N]
	cbnz	x0,.gzend
	; aucune entree : un gz vide, sans nom
	mov	x0,x19
	add	x0,x19,W_SINK
	mov	x1,AR_GZIP
	mov	x2,AR_DEFAULT
	bl	wrap_head
	cbnz	x0,.out
	bl	wr_df
	cbz	x0,.nomem
	mov	x1,AR_DEFAULT
	mov	x2,AR_GZIP
	bl	df_begin
.gzend:	ldr	x0,[x19,W_DF]
	bl	df_finish
	cbnz	x0,.out
	add	x0,x19,W_SINK
	ldr	x1,[x19,W_DF]
	bl	wrap_tail
	cbnz	x0,.out
	b	.done
.zip:	ldr	x20,[x19,W_SINK+SK_POS]		; position du repertoire
	ldr	x21,[x19,W_CD+SK_POS]		; sa taille
	ldr	x22,[x19,W_N]
	movz	x0,0xFFFF
	cmp	x22,x0
	b.hs	.big
	movl	x0,0xFFFFFFFF
	add	x1,x20,x21
	cmp	x1,x0
	b.hs	.big
	add	x0,x19,W_SINK
	ldr	x1,[x19,W_CD+SK_PTR]
	mov	x2,x21
	bl	sink_write
	cbnz	x0,.out
	sub	sp,sp,32
	movl	x0,ZIP_END
	str	w0,[sp]
	str	wzr,[sp,4]
	strh	w22,[sp,8]
	strh	w22,[sp,10]
	str	w21,[sp,12]
	str	w20,[sp,16]
	strh	wzr,[sp,20]
	add	x0,x19,W_SINK
	mov	x1,sp
	mov	x2,22
	bl	sink_write
	add	sp,sp,32
	cbnz	x0,.out
.done:	mov	x0,1
	str	x0,[x19,W_DONE]
	ldr	x0,[x19,W_SINK+SK_PTR]
	str	x0,[x19,H_DATA]
	ldr	x0,[x19,W_SINK+SK_POS]
	str	x0,[x19,H_SIZE]
	ldr	x0,[x19,W_SINK+SK_KIND]
	cmp	x0,SKK_FD
	b.ne	.mem
	ldr	x0,[x19,W_SINK+SK_PTR]
	bl	f_fermer
	str	xzr,[x19,H_DATA]
	ldr	x0,[x19,H_SIZE]
	b	.out
.mem:	add	x0,x19,W_SINK			; un 0 suit les octets
	bl	sink_zero
	tbnz	x0,63,.out
	ldr	x0,[x19,W_SINK+SK_PTR]
	str	x0,[x19,H_DATA]
	ldr	x0,[x19,H_SIZE]
	b	.out
.big:	mov	x0,AR_E_FBIG
	b	.out
.nomem:	mov	x0,AR_E_NOMEM
	b	.out
.bad:	mov	x0,AR_E_INVAL
.out:	ar_leave

; x0 = tableau, x1 = places -> nombre de formats
ar_formats:
	ar_enter
	mov	x19,x0
	mov	x20,x1
	mov	x21,0
	cbz	x19,.done
.f:	cmp	x21,AR_NFORMATS
	b.hs	.done
	cmp	x21,x20
	b.hs	.done
	add	x22,x21,1
	mov	x23,ARFI_SIZE
	mul	x0,x21,x23
	add	x24,x19,x0
	str	x22,[x24,ARFI_ID]
	mov	x0,1
	str	x0,[x24,ARFI_READ]
	str	x0,[x24,ARFI_WRITE]
	mov	x0,x22
	bl	ar_format_name
	add	x1,x24,ARFI_NAME
.n:	ldrb	w2,[x0],1
	strb	w2,[x1],1
	cbnz	w2,.n
	adr	x0,ar_extensions
	mov	x1,x21
.x:	cbz	x1,.x2
.x1:	ldrb	w2,[x0],1
	cbnz	w2,.x1
	sub	x1,x1,1
	b	.x
.x2:	add	x1,x24,ARFI_EXT
.x3:	ldrb	w2,[x0],1
	strb	w2,[x1],1
	cbnz	w2,.x3
	add	x21,x21,1
	b	.f
.done:	mov	x0,AR_NFORMATS
	ar_leave

; x0 = AR_FMT_* -> chaine, 0
ar_format_name:
	sub	x1,x0,1
	cmp	x1,AR_NFORMATS
	b.hs	.no
	adr	x0,ar_fmtnames
.a:	cbz	x1,.e
.b:	ldrb	w2,[x0],1
	cbnz	w2,.b
	sub	x1,x1,1
	b	.a
.e:	ret
.no:	mov	x0,0
	ret

; x0 = erreur -> chaine
ar_strerror:
	adr	x1,ar_errors
.a:	ldrb	w2,[x1],1
	cbz	w2,.unk
	mvn	w3,w0
	add	w3,w3,1				; -erreur
	cmp	w2,w3
	b.eq	.hit
.s:	ldrb	w2,[x1],1
	cbnz	w2,.s
	b	.a
.hit:	mov	x0,x1
	ret
.unk:	adr	x0,s_err_unk
	ret

AR_NFORMATS	= 4
s_longlink	db '././@LongLink',0
ar_fmtnames	db 'ZIP',0,'TAR',0,'TAR.GZ',0,'GZIP',0
ar_extensions	db 'zip jar docx xlsx pptx odt ods odp epub apk',0
		db 'tar',0
		db 'tar.gz tgz',0
		db 'gz',0
ar_errors	db 2,'fichier ou dossier introuvable',0
		db 5,'lecture ou ecriture impossible',0
		db 12,'plus de memoire',0
		db 13,'entree chiffree : pas prise en charge',0
		db 22,'argument invalide',0
		db 27,'trop grand pour ce format',0
		db 28,'plus de place',0
		db 38,'methode de compression inconnue',0
		db 74,'donnees abimees ou tronquees',0
		db 84,'somme de controle fausse',0
		db 95,'format inconnu',0
		db 0
s_err_unk	db 'erreur inconnue',0
		align	4
