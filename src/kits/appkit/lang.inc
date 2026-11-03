; lang.inc - la langue et les traductions de tout programme : commandes de /bin, apps, kits
; (docs/KITS.md ; ui_tr de UIKit passe par ici, comme lang.cpp d'Onyx).
;
; Langue : « lang = xx » de SD0:/etc/system.ini, choisie dans Reglages (sans cle : fr).
; Dictionnaires : SD0:/res/lang/<xx>.txt, puis ceux du programme (lang_load), le dernier lu
; gagne. Une ligne : « source = traduction », « # » : commentaire ; \n, \= et \\ dans le texte.

LANG_SEAUX	= 1024			; (puissance de 2)

; x0 = texte (dans la langue de son source) -> x0 = sa traduction (ou le texte)
lang_tr:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	bl	lang_init
	adr	x20,lang_seaux
	ldr	x20,[x20]
	cbz	x20,.out
	mov	x0,x19
	bl	lang_hash
	ldr	x20,[x20,x0,lsl 3]
.l:	cbz	x20,.out
	ldr	x0,[x20]			; { source, traduction, suivante }
	mov	x2,x19
.c:	ldrb	w3,[x0],1
	ldrb	w4,[x2],1
	cmp	w3,w4
	b.ne	.n
	cbnz	w3,.c
	ldr	x19,[x20,8]
	b	.out
.n:	ldr	x20,[x20,16]
	b	.l
.out:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte -> sortie standard, traduit
lang_puts:
	prologue
	bl	lang_tr
	bl	puts
	epilogue

; -> x0 = la langue (« fr », « en »...)
lang_get:
	prologue
	bl	lang_init
	adr	x0,lang_cur
	epilogue

; x0 = chemin d'un dictionnaire du programme, deja dans la langue choisie
; (ex. SD0:/apps/x.app/lang/<lang_get>.txt) -> x0 = 1 si lu
lang_load:
	prologue
	str	x0,[sp,-16]!
	bl	lang_init
	ldr	x0,[sp],16
	bl	lang_ajouter
	epilogue

; x0 = texte -> x0 = son seau (FNV-1a)
lang_hash:
	mov	w1,0x9DC5
	movk	w1,0x811C,lsl 16
	mov	w2,0x0193
	movk	w2,0x0100,lsl 16
.l:	ldrb	w3,[x0],1
	cbz	w3,.f
	eor	w1,w1,w3
	mul	w1,w1,w2
	b	.l
.f:	eor	w1,w1,w1,lsr 15
	and	x0,x1,LANG_SEAUX-1
	ret

; la langue et le dictionnaire du systeme, au premier besoin
lang_init:
	prologue
	adr	x0,lang_ok
	ldr	x1,[x0]
	cbnz	x1,.out
	mov	x1,1
	str	x1,[x0]
	adr	x0,lang_s_ini			; « lang = xx »
	bl	lang_lire
	cbz	x0,.def
	mov	x1,x0
.f:	ldrb	w2,[x1]
	cbz	w2,.def
	ldrb	w3,[x1,1]
	cmp	w2,'l'
	b.ne	.nl
	cmp	w3,'a'
	b.ne	.nl
	ldrb	w3,[x1,2]
	cmp	w3,'n'
	b.ne	.nl
	ldrb	w3,[x1,3]
	cmp	w3,'g'
	b.ne	.nl
	add	x1,x1,4
.sp:	ldrb	w2,[x1]				; espaces et « = »
	cmp	w2,' '
	b.eq	.s1
	cmp	w2,'='
	b.ne	.v
.s1:	add	x1,x1,1
	b	.sp
.v:	adr	x2,lang_cur			; la langue (2 a 7 lettres)
	mov	x3,0
.vl:	ldrb	w4,[x1,x3]
	sub	w5,w4,'a'
	cmp	w5,26
	b.hs	.vd
	strb	w4,[x2,x3]
	add	x3,x3,1
	cmp	x3,7
	b.lo	.vl
.vd:	strb	wzr,[x2,x3]
	b	.def
.nl:	ldrb	w2,[x1],1			; ligne suivante
	cbz	w2,.def
	cmp	w2,10
	b.ne	.nl
	b	.f
.def:	adr	x0,lang_cur			; sans cle : fr (fr.txt : les apps portees,
	ldrb	w1,[x0]				; ecrites en anglais)
	cbnz	w1,.load
	mov	w1,'f'
	strb	w1,[x0]
	mov	w1,'r'
	strb	w1,[x0,1]
	strb	wzr,[x0,2]
.load:	adr	x0,lang_chemin			; SD0:/res/lang/<xx>.txt
	adr	x1,lang_s_dir
	bl	str_cat
	adr	x1,lang_cur
	bl	str_cat
	adr	x1,lang_s_txt
	bl	str_cat
	adr	x0,lang_chemin
	bl	lang_ajouter
.out:	epilogue

; x0 = chemin : lit le dictionnaire et retient ses lignes -> x0 = 1 si lu
; (le texte reste en memoire : les entrees y pointent)
lang_ajouter:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	lang_lire
	cbz	x0,.fail
	mov	x19,x0
	adr	x0,lang_seaux
	ldr	x0,[x0]
	cbnz	x0,.line
	mov	x0,LANG_SEAUX*8
	bl	malloc
	cbz	x0,.fail
	adr	x1,lang_seaux
	str	x0,[x1]
	mov	x1,LANG_SEAUX
.z:	sub	x1,x1,1
	str	xzr,[x0,x1,lsl 3]
	cbnz	x1,.z
.line:	ldrb	w0,[x19]			; une ligne
	cbz	w0,.ok
	mov	x20,x19				; debut
.e:	ldrb	w0,[x19]			; fin de ligne -> NUL
	cbz	w0,.got
	add	x19,x19,1
	cmp	w0,10
	b.ne	.e
	sub	x1,x19,1
	strb	wzr,[x1]
	sub	x1,x1,1				; (retour chariot)
	cmp	x1,x20
	b.lo	.got
	ldrb	w2,[x1]
	cmp	w2,13
	b.ne	.got
	strb	wzr,[x1]
.got:	ldrb	w0,[x20]
	cmp	w0,'#'
	b.eq	.line
	mov	x21,x20				; le premier « = » : source avant, traduction apres
.q:	ldrb	w0,[x21]
	cbz	w0,.line
	cmp	w0,'='
	b.eq	.split
	cmp	w0,'\'
	b.ne	.q1
	ldrb	w0,[x21,1]!
	cbz	w0,.line
.q1:	add	x21,x21,1
	b	.q
.split:	strb	wzr,[x21]
	add	x22,x21,1
.t1:	ldrb	w0,[x22]			; espaces apres « = »
	cmp	w0,' '
	b.ne	.t2
	add	x22,x22,1
	b	.t1
.t2:	sub	x21,x21,1			; espaces avant « = »
	cmp	x21,x20
	b.lo	.line
	ldrb	w0,[x21]
	cmp	w0,' '
	b.ne	.st
	strb	wzr,[x21]
	b	.t2
.st:	mov	x0,x20
	bl	lang_decoder
	mov	x0,x22
	bl	lang_decoder
	mov	x0,24
	bl	malloc
	cbz	x0,.ok
	mov	x21,x0
	stp	x20,x22,[x21]
	mov	x0,x20
	bl	lang_hash
	adr	x1,lang_seaux
	ldr	x1,[x1]
	ldr	x2,[x1,x0,lsl 3]
	str	x2,[x21,16]
	str	x21,[x1,x0,lsl 3]
	b	.line
.ok:	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte : \n, \=, \\ decodes sur place
lang_decoder:
	mov	x1,x0
.l:	ldrb	w2,[x1],1
	cmp	w2,'\'
	b.ne	.p
	ldrb	w2,[x1],1
	cmp	w2,'n'
	b.ne	.p0
	mov	w2,10
	b	.p
.p0:	cbnz	w2,.p
	strb	wzr,[x0]
	ret
.p:	strb	w2,[x0],1
	cbnz	w2,.l
	ret

; x0 = chemin -> x0 = son contenu termine par NUL (0 : absent)
lang_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x1,sp
	bl	f_stat
	cbnz	x0,.fail
	ldr	x20,[sp]
	add	x0,x20,1
	bl	malloc
	cbz	x0,.fail
	mov	x21,x0
	mov	x0,x19
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,x21
	mov	x2,x20
	bl	f_lire
	mov	x22,x0
	mov	x0,x19
	bl	f_fermer
	tbnz	x22,63,.fail
	strb	wzr,[x21,x22]
	mov	x0,x21
	b	.out
.fail:	mov	x0,0
.out:	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

lang_s_ini	db 'SD0:/etc/system.ini',0
lang_s_dir	db 'SD0:/res/lang/',0
lang_s_txt	db '.txt',0
align 4

macro lang_data {
align 8
lang_ok		dd 0
lang_seaux	dd 0
lang_cur	rb 8
lang_chemin	rb 64
}
