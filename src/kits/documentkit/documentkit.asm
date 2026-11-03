; ============================================================================
;  kits/documentkit/documentkit.asm - DocumentKit : la plomberie des formats
;  bureautiques -> SD0:/lib/documentkit.so (docs/KITS.md)
;
;  Ce que Letter, Sheet, Present et Cardfile ont en commun, en entiers et sur
;  des tampons : lecteur XML par evenements et echappement, UTF-8, CP1252,
;  longueurs Office et ODF, couleurs, dates civiles et series Excel, CSV,
;  reconnaissance d'un paquet, relations OOXML, enveloppes OOXML et ODF.
;  Le ZIP est a ArchiveKit ; les moteurs de documents restent dans les apps.
;
;  Sans kit importe ni tas : les lecteurs sont des cases d'une table du
;  process, reservees par echange atomique (une poignee = rang + 1).
;  Fonctions AAPCS ; seuls x0-x17 sont touches. Sortie texte comme snprintf
;  (documentkit.abi) ; registres de la sortie : x10 ecriture (0 : rien),
;  x11 place restante, x12 longueur complete.
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include 'documentkit.inc'

; --- une case de lecteur (XML ou CSV) -------------------------------------------
DK_SLOTS	= 64
SLOT_SHIFT	= 7
SLOT_SIZE	= 128
XR_USED		= 0			; 0 libre, 1 XML, 2 CSV, 3 reservee
XR_P		= 8			; position
XR_E		= 16			; fin du tampon
XR_BASE		= 24			; debut du tampon
XR_EV		= 32			; evenement courant
XR_FL		= 40			; FL_*
XR_NM		= 48			; nom de l'element (avec son prefixe)
XR_NML		= 56
XR_AT		= 64			; ses attributs (texte brut)
XR_ATE		= 72
XR_TS		= 80			; texte courant (non decode)
XR_TE		= 88
XR_SEP		= XR_NM			; CSV : separateur
XR_FS		= XR_TS			; CSV : debut du champ courant

FL_EMPTY	= 1			; ce START vient de <a/>
FL_PEND		= 2			; un END suit tout seul
FL_RAW		= 4			; texte d'un CDATA : rien a decoder
FL_SEP		= 8			; CSV : le dernier champ finissait par le separateur
FLB_PEND	= 1
FLB_RAW		= 2
FLB_EMPTY	= 0
FLB_SEP		= 3

KIND_XML	= 1
KIND_CSV	= 2

; --- macros ---------------------------------------------------------------------

; x0 = poignee -> x9 = case ; autre genre ou poignee inconnue : va a bad. Touche x8.
macro dk_slot kind*, bad* {
	sub	x8,x0,1
	cmp	x8,DK_SLOTS
	b.hs	bad
	adr	x9,dk_slots
	add	x9,x9,x8,lsl SLOT_SHIFT
	ldr	x8,[x9,XR_USED]
	cmp	x8,kind
	b.ne	bad
}

; x10 = out, x11 = cap -> sortie prete
macro sink_init {
	mov	x12,0
	cmp	x11,0
	b.gt	@f
	mov	x10,0
@@:	cbnz	x10,@f
	mov	x11,1
@@:	sub	x11,x11,1
}

macro sink_end {
	cbz	x10,@f
	strb	wzr,[x10]
@@:
}

macro emit rg* {
	add	x12,x12,1
	cbz	x11,@f
	strb	rg,[x10],1
	sub	x11,x11,1
@@:
}

segment readable executable
include 'documentkit.abi'

; ---------------------------------------------------------------------------
;  Cases
; ---------------------------------------------------------------------------

; x1 = KIND_* -> x0 = poignee, x9 = case ; DKE_NOMEM si plein. Touche x2-x4.
slot_new:
	adr	x9,dk_slots
	mov	x0,1
.t:	ldaxr	x2,[x9]
	cbnz	x2,.n
	mov	x2,3
	stxr	w3,x2,[x9]
	cbnz	w3,.t
	dmb	ish
	add	x2,x9,8				; (tout le reste a zero)
	mov	x3,(SLOT_SIZE-8)/8
.z:	str	xzr,[x2],8
	subs	x3,x3,1
	b.ne	.z
	str	x1,[x9,XR_USED]
	ret
.n:	clrex
	add	x9,x9,SLOT_SIZE
	add	x0,x0,1
	cmp	x0,DK_SLOTS
	b.ls	.t
	mov	x0,DKE_NOMEM
	ret

dk_badf:
	mov	x0,DKE_BADF
	ret

; ---------------------------------------------------------------------------
;  Sortie
; ---------------------------------------------------------------------------

; w7 = point de code -> UTF-8 (invalide : U+FFFD). Touche w6, w7.
sk_cp:
	cmp	w7,0x80
	b.hs	.m
	emit	w7
	ret
.m:	movl	w6,0x10FFFF
	cmp	w7,w6
	b.hi	.bad
	movl	w6,0xD800
	sub	w6,w7,w6
	cmp	w6,0x800
	b.hs	.ok
.bad:	movl	w7,0xFFFD
.ok:	cmp	w7,0x800
	b.hs	.m3
	lsr	w6,w7,6
	orr	w6,w6,0xC0
	emit	w6
	b	.last
.m3:	movl	w6,0x10000
	cmp	w7,w6
	b.hs	.m4
	lsr	w6,w7,12
	orr	w6,w6,0xE0
	emit	w6
	b	.mid
.m4:	lsr	w6,w7,18
	orr	w6,w6,0xF0
	emit	w6
	lsr	w6,w7,12
	and	w6,w6,0x3F
	orr	w6,w6,0x80
	emit	w6
.mid:	lsr	w6,w7,6
	and	w6,w6,0x3F
	orr	w6,w6,0x80
	emit	w6
.last:	and	w6,w7,0x3F
	orr	w6,w6,0x80
	emit	w6
	ret

; x0 = texte fini par 0 -> copie. Touche x0, w6.
sk_str:
.l:	ldrb	w6,[x0],1
	cbz	w6,.e
	emit	w6
	b	.l
.e:	ret

; x0 = texte, x1 = octets -> copie. Touche x0, x1, w6.
sk_mem:
	cmp	x1,0
	b.le	.e
	ldrb	w6,[x0],1
	emit	w6
	sub	x1,x1,1
	b	sk_mem
.e:	ret

; x0 = nombre sans signe -> en decimal. Touche x0-x4, w6.
sk_num:
	mov	x1,1
; x1 = chiffres au moins
sk_num_pad:
	sub	sp,sp,32
	mov	x2,sp
	mov	x4,10
.d:	udiv	x3,x0,x4
	msub	x6,x3,x4,x0
	add	x6,x6,'0'
	strb	w6,[x2],1
	mov	x0,x3
	sub	x1,x1,1
	cbnz	x0,.d
	cmp	x1,0
	b.gt	.d
.o:	ldrb	w6,[x2,-1]!
	emit	w6
	mov	x3,sp
	cmp	x2,x3
	b.hi	.o
	add	sp,sp,32
	ret

; x0 = texte, x1 = octets (< 0 : jusqu'au 0), x2 = 1 : valeur d'attribut.
; Touche x0-x5, w6, w7.
sk_esc:
	tbz	x1,63,.b
	mov	x1,x0
.s:	ldrb	w3,[x1],1
	cbnz	w3,.s
	sub	x1,x1,1
	sub	x1,x1,x0
.b:	add	x3,x0,x1
.l:	cmp	x0,x3
	b.hs	.e
	ldrb	w7,[x0],1
	cmp	w7,'&'
	b.eq	.amp
	cmp	w7,'<'
	b.eq	.lt
	cmp	w7,'>'
	b.eq	.gt
	cmp	w7,'"'
	b.eq	.qu
	cmp	w7,32
	b.hs	.p
	cmp	w7,9
	b.eq	.c
	cmp	w7,10
	b.eq	.c
	cmp	w7,13
	b.ne	.l
.c:	cbz	x2,.p
	mov	w1,'&'
	emit	w1
	mov	w1,'#'
	emit	w1
	mov	w1,w7
	cmp	w1,10
	b.lo	.c1
	mov	w6,'1'
	emit	w6
	sub	w1,w1,10
.c1:	add	w1,w1,'0'
	emit	w1
	mov	w1,';'
	emit	w1
	b	.l
.p:	emit	w7
	b	.l
.amp:	adr	x5,s_amp
	b	.ent
.lt:	adr	x5,s_lt
	b	.ent
.gt:	adr	x5,s_gt
	b	.ent
.qu:	adr	x5,s_quot
.ent:	ldrb	w6,[x5],1
	cbz	w6,.l
	emit	w6
	b	.ent
.e:	ret

; x0 = valeur d'attribut finie par 0 (0 : rien)
sk_attr:
	cbz	x0,.e
	mov	x1,-1
	mov	x2,1
	b	sk_esc
.e:	ret

; ---------------------------------------------------------------------------
;  XML : decodage
; ---------------------------------------------------------------------------

; x0 = texte, x1 = fin -> sortie (entites, references numeriques). Une entite
; inconnue ou sans ';' reste telle quelle. Touche x0-x8.
xml_decode:
	mov	x8,x30
.l:	cmp	x0,x1
	b.hs	.e
	ldrb	w2,[x0]
	cmp	w2,'&'
	b.eq	.amp
.lit:	emit	w2
	add	x0,x0,1
	b	.l
.amp:	add	x3,x0,1
.q:	cmp	x3,x1
	b.hs	.lit
	ldrb	w4,[x3]
	cmp	w4,';'
	b.eq	.have
	sub	x4,x3,x0
	cmp	x4,12
	b.hs	.lit
	add	x3,x3,1
	b	.q
.have:	sub	x4,x3,x0
	sub	x4,x4,1				; longueur du nom
	cbz	x4,.other
	ldrb	w5,[x0,1]
	ldrb	w6,[x0,2]			; (si x4 = 1 : le ';')
	cmp	x4,2
	b.ne	.n3
	cmp	w6,'t'
	b.ne	.num
	mov	w7,'<'
	cmp	w5,'l'
	b.eq	.one
	mov	w7,'>'
	cmp	w5,'g'
	b.eq	.one
	b	.num
.n3:	cmp	x4,3
	b.ne	.n4
	cmp	w5,'a'
	b.ne	.num
	cmp	w6,'m'
	b.ne	.num
	ldrb	w7,[x0,3]
	cmp	w7,'p'
	b.ne	.num
	mov	w7,'&'
	b	.one
.n4:	cmp	x4,4
	b.ne	.num
	ldrb	w7,[x0,3]
	ldrb	w2,[x0,4]
	cmp	w5,'q'
	b.ne	.ap
	cmp	w6,'u'
	b.ne	.num
	cmp	w7,'o'
	b.ne	.num
	cmp	w2,'t'
	b.ne	.num
	mov	w7,'"'
	b	.one
.ap:	cmp	w5,'a'
	b.ne	.num
	cmp	w6,'p'
	b.ne	.num
	cmp	w7,'o'
	b.ne	.num
	cmp	w2,'s'
	b.ne	.num
	mov	w7,39
.one:	emit	w7
.past:	add	x0,x3,1
	b	.l
.num:	cmp	x4,1
	b.ls	.other
	cmp	w5,'#'
	b.ne	.other
	add	x4,x0,2				; apres &#
	mov	w7,0
	cmp	w6,'x'
	b.eq	.hx
	cmp	w6,'X'
	b.eq	.hx
.dd:	ldrb	w5,[x4],1
	sub	w5,w5,'0'
	cmp	w5,9
	b.hi	.nd
	mov	w6,10
	mul	w7,w7,w6
	add	w7,w7,w5
	b	.sat
.hx:	add	x4,x4,1
.hd:	ldrb	w5,[x4],1
	sub	w6,w5,'0'
	cmp	w6,9
	b.ls	.hv
	orr	w5,w5,32
	sub	w6,w5,'a'
	cmp	w6,5
	b.hi	.nd
	add	w6,w6,10
.hv:	add	w7,w6,w7,lsl 4
	movl	w6,0x110000
	cmp	w7,w6
	b.ls	.hd
	mov	w7,w6
	b	.hd
.sat:	movl	w6,0x110000
	cmp	w7,w6
	b.ls	.dd
	mov	w7,w6
	b	.dd
.nd:	cbz	w7,.past			; (&#0; : rien)
	bl	sk_cp
	b	.past
.other:	add	x4,x3,1				; & ... ; tels quels
.oc:	ldrb	w2,[x0],1
	emit	w2
	cmp	x0,x4
	b.lo	.oc
	b	.l
.e:	mov	x30,x8
	ret

; ---------------------------------------------------------------------------
;  XML : lecteur
; ---------------------------------------------------------------------------

dk_xml_open:
	cmp	x1,0
	b.lt	.inval
	mov	x5,x0
	mov	x6,x1
	mov	x1,KIND_XML
	mov	x7,x30
	bl	slot_new
	mov	x30,x7
	tbnz	x0,63,.e
	str	x5,[x9,XR_P]
	str	x5,[x9,XR_BASE]
	add	x5,x5,x6
	str	x5,[x9,XR_E]
.e:	ret
.inval:	mov	x0,DKE_INVAL
	ret

dk_xml_close:
	dk_slot	KIND_XML,dk_badf
	stlr	xzr,[x9]
	mov	x0,0
	ret

dk_xml_next:
	dk_slot	KIND_XML,dk_badf
; x9 = case -> x0 = evenement. Touche x1-x8.
xml_step:
	ldr	x1,[x9,XR_P]
	ldr	x2,[x9,XR_E]
	ldr	x3,[x9,XR_FL]
	tbz	x3,FLB_PEND,.loop
	eor	x3,x3,FL_PEND
	str	x3,[x9,XR_FL]
	stp	xzr,xzr,[x9,XR_AT]
	mov	x0,DKX_END
	str	x0,[x9,XR_EV]
	ret
.loop:	cmp	x1,x2
	b.hs	.eof
	ldrb	w4,[x1]
	cmp	w4,'<'
	b.eq	.tag
	mov	x5,x1
.txt:	add	x1,x1,1
	cmp	x1,x2
	b.hs	.te
	ldrb	w4,[x1]
	cmp	w4,'<'
	b.ne	.txt
.te:	stp	x5,x1,[x9,XR_TS]
	and	x3,x3,FL_EMPTY
.text:	str	x1,[x9,XR_P]
	str	x3,[x9,XR_FL]
	mov	x0,DKX_TEXT
	str	x0,[x9,XR_EV]
	ret
.eof:	str	x2,[x9,XR_P]
	mov	x0,DKX_EOF
	str	x0,[x9,XR_EV]
	ret
.tag:	sub	x6,x2,x1
	cmp	x6,2
	b.lo	.open
	ldrb	w4,[x1,1]
	cmp	w4,'/'
	b.eq	.close
	cmp	w4,'?'
	b.eq	.decl
	cmp	w4,'!'
	b.ne	.open
	cmp	x6,4
	b.lo	.decl
	ldrb	w4,[x1,2]
	cmp	w4,'-'
	b.ne	.cd
	ldrb	w4,[x1,3]
	cmp	w4,'-'
	b.ne	.decl
	add	x1,x1,4				; <!-- ... -->
.cm:	sub	x6,x2,x1
	cmp	x6,3
	b.lo	.cmeof
	ldrb	w4,[x1]
	cmp	w4,'-'
	b.ne	.cmn
	ldrb	w4,[x1,1]
	cmp	w4,'-'
	b.ne	.cmn
	ldrb	w4,[x1,2]
	cmp	w4,'>'
	b.eq	.cme
.cmn:	add	x1,x1,1
	b	.cm
.cme:	add	x1,x1,3
	b	.loop
.cmeof:	mov	x1,x2
	b	.loop
.cd:	cmp	x6,9				; <![CDATA[ ... ]]>
	b.lo	.decl
	add	x5,x1,1
	adr	x7,s_cdata
	mov	x8,8
.cc:	ldrb	w4,[x5],1
	ldrb	w0,[x7],1
	cmp	w4,w0
	b.ne	.decl
	subs	x8,x8,1
	b.ne	.cc
	mov	x7,x5
.cdl:	sub	x6,x2,x7
	cmp	x6,3
	b.lo	.cdeof
	ldrb	w4,[x7]
	cmp	w4,']'
	b.ne	.cdn
	ldrb	w4,[x7,1]
	cmp	w4,']'
	b.ne	.cdn
	ldrb	w4,[x7,2]
	cmp	w4,'>'
	b.eq	.cdf
.cdn:	add	x7,x7,1
	b	.cdl
.cdf:	add	x1,x7,3
	b	.cdt
.cdeof:	mov	x1,x2
.cdt:	stp	x5,x7,[x9,XR_TS]
	and	x3,x3,FL_EMPTY
	orr	x3,x3,FL_RAW
	b	.text
.decl:	cmp	x1,x2				; <? ... > et <! ... >
	b.hs	.loop
	ldrb	w4,[x1],1
	cmp	w4,'>'
	b.ne	.decl
	b	.loop
.close:	add	x5,x1,2				; </nom ... >
	mov	x7,x5
.cl:	cmp	x7,x2
	b.hs	.cle
	ldrb	w4,[x7]
	cmp	w4,'>'
	b.eq	.cle
	cmp	w4,' '
	b.eq	.cle
	cmp	w4,9
	b.eq	.cle
	cmp	w4,10
	b.eq	.cle
	cmp	w4,13
	b.eq	.cle
	add	x7,x7,1
	b	.cl
.cle:	sub	x4,x7,x5
	stp	x5,x4,[x9,XR_NM]
.clg:	cmp	x7,x2
	b.hs	.clh
	ldrb	w4,[x7],1
	cmp	w4,'>'
	b.ne	.clg
.clh:	str	x7,[x9,XR_P]
	stp	xzr,xzr,[x9,XR_AT]
	mov	x0,DKX_END
	str	x0,[x9,XR_EV]
	ret
.open:	add	x5,x1,1				; <nom attributs>
	mov	x7,x5
.on:	cmp	x7,x2
	b.hs	.one
	ldrb	w4,[x7]
	cmp	w4,'>'
	b.eq	.one
	cmp	w4,'/'
	b.eq	.one
	cmp	w4,' '
	b.eq	.one
	cmp	w4,9
	b.eq	.one
	cmp	w4,10
	b.eq	.one
	cmp	w4,13
	b.eq	.one
	add	x7,x7,1
	b	.on
.one:	sub	x4,x7,x5
	stp	x5,x4,[x9,XR_NM]
	mov	x5,x7				; debut des attributs
	mov	w8,0				; guillemet ouvert
.ql:	cmp	x7,x2
	b.hs	.qe
	ldrb	w4,[x7]
	cbnz	w8,.inq
	cmp	w4,'>'
	b.eq	.qe
	cmp	w4,'"'
	b.eq	.setq
	cmp	w4,39
	b.ne	.qn
.setq:	mov	w8,w4
	b	.qn
.inq:	cmp	w4,w8
	b.ne	.qn
	mov	w8,0
.qn:	add	x7,x7,1
	b	.ql
.qe:	mov	x3,0
	mov	x6,x7				; fin des attributs
	cmp	x7,x5
	b.ls	.past
	ldrb	w4,[x7,-1]
	cmp	w4,'/'
	b.ne	.past
	sub	x6,x7,1
	mov	x3,FL_EMPTY or FL_PEND
.past:	stp	x5,x6,[x9,XR_AT]
	str	x3,[x9,XR_FL]
	cmp	x7,x2
	b.hs	.pe
	add	x7,x7,1
.pe:	str	x7,[x9,XR_P]
	mov	x0,DKX_START
	str	x0,[x9,XR_EV]
	ret

dk_xml_event:
	dk_slot	KIND_XML,dk_badf
	ldr	x0,[x9,XR_EV]
	ret

dk_xml_empty:
	dk_slot	KIND_XML,dk_badf
	ldr	x0,[x9,XR_FL]
	and	x0,x0,FL_EMPTY
	ret

dk_xml_is:
	dk_slot	KIND_XML,dk_badf
; x9 = case, x1 = nom -> x0 = 1 si le nom sans prefixe est celui-la. Touche x0-x6.
xml_is:
	ldp	x2,x3,[x9,XR_NM]
	mov	x4,0
.c:	cmp	x4,x3
	b.hs	.n
	ldrb	w5,[x2,x4]
	add	x4,x4,1
	cmp	w5,':'
	b.ne	.c
	add	x2,x2,x4			; apres le premier ':'
	sub	x3,x3,x4
.n:	b	nm_eq

dk_xml_isq:
	dk_slot	KIND_XML,dk_badf
	ldp	x2,x3,[x9,XR_NM]
; x1 = nom fini par 0, x2 = texte, x3 = octets -> x0 = 1 si egaux. Touche x0, x2-x6.
nm_eq:
	mov	x4,x1
.c:	cbz	x3,.fin
	ldrb	w5,[x4],1
	cbz	w5,.non
	ldrb	w6,[x2],1
	cmp	w5,w6
	b.ne	.non
	sub	x3,x3,1
	b	.c
.fin:	ldrb	w5,[x4]
	cbnz	w5,.non
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

dk_xml_name:
	dk_slot	KIND_XML,dk_badf
	mov	x10,x1
	mov	x11,x2
	sink_init
	ldp	x0,x1,[x9,XR_NM]
	mov	x13,x30
	bl	sk_mem
	mov	x30,x13
	sink_end
	mov	x0,x12
	ret

; x9 = case, x1 = nom -> x2 = valeur, x3 = sa fin ; x2 = 0 : absent. Le nom exact d'abord,
; ou qu'il soit ; sinon le premier a prefixe (id : pas r:id si id existe aussi). Touche x1-x8,
; x10, x11, x16, x17 ; garde x12-x15.
attr_find:
	mov	x2,0
	ldr	x4,[x9,XR_EV]
	cmp	x4,DKX_START
	b.ne	.no
	ldp	x2,x3,[x9,XR_AT]
	cbz	x2,.no
	mov	x4,0				; longueur du nom cherche
	mov	x5,1				; sans prefixe ?
.k:	ldrb	w6,[x1,x4]
	cbz	w6,.go
	cmp	w6,':'
	b.ne	.kn
	mov	x5,0
.kn:	add	x4,x4,1
	b	.k
.go:	stp	xzr,xzr,[sp,-16]!		; (le premier a prefixe trouve)
.t:	cmp	x2,x3
	b.hs	.no0
	ldrb	w6,[x2]
	cmp	w6,' '
	b.eq	.w
	cmp	w6,9
	b.eq	.w
	cmp	w6,10
	b.eq	.w
	cmp	w6,13
	b.ne	.an
.w:	add	x2,x2,1
	b	.t
.an:	mov	x6,x2				; nom de l'attribut
.a:	cmp	x2,x3
	b.hs	.ae
	ldrb	w7,[x2]
	cmp	w7,'='
	b.eq	.ae
	cmp	w7,' '
	b.eq	.ae
	cmp	w7,9
	b.eq	.ae
	cmp	w7,10
	b.eq	.ae
	cmp	w7,13
	b.eq	.ae
	add	x2,x2,1
	b	.a
.ae:	sub	x7,x2,x6			; sa longueur
.eqs:	cmp	x2,x3
	b.hs	.no0
	ldrb	w8,[x2],1
	cmp	w8,'='
	b.ne	.eqs
.qt:	cmp	x2,x3
	b.hs	.no0
	ldrb	w8,[x2],1
	cmp	w8,'"'
	b.eq	.vs
	cmp	w8,39
	b.ne	.qt
.vs:	mov	x10,x2				; valeur
.v:	cmp	x2,x3
	b.hs	.ve
	ldrb	w11,[x2]
	cmp	w11,w8
	b.eq	.ve
	add	x2,x2,1
	b	.v
.ve:	mov	x11,x2
	cmp	x2,x3
	b.hs	.cmn
	add	x2,x2,1
.cmn:	cmp	x7,x4				; nom exact ?
	b.ne	.pref
	mov	x16,0
.c1:	cmp	x16,x4
	b.hs	.yes
	ldrb	w8,[x6,x16]
	ldrb	w17,[x1,x16]
	cmp	w8,w17
	b.ne	.pref
	add	x16,x16,1
	b	.c1
.pref:	cbz	x5,.t				; apres le premier ':' de l'attribut
	mov	x16,0
.c2:	cmp	x16,x7
	b.hs	.t
	ldrb	w8,[x6,x16]
	add	x16,x16,1
	cmp	w8,':'
	b.ne	.c2
	sub	x8,x7,x16
	cmp	x8,x4
	b.ne	.t
	cmp	x16,6				; xmlns:... : jamais
	b.ne	.c3
	ldrb	w8,[x6]
	cmp	w8,'x'
	b.ne	.c3
	ldrb	w8,[x6,1]
	cmp	w8,'m'
	b.ne	.c3
	ldrb	w8,[x6,2]
	cmp	w8,'l'
	b.ne	.c3
	ldrb	w8,[x6,3]
	cmp	w8,'n'
	b.ne	.c3
	ldrb	w8,[x6,4]
	cmp	w8,'s'
	b.eq	.t
.c3:	add	x6,x6,x16
	mov	x16,0
.c4:	cmp	x16,x4
	b.hs	.cand
	ldrb	w8,[x6,x16]
	ldrb	w17,[x1,x16]
	cmp	w8,w17
	b.ne	.t
	add	x16,x16,1
	b	.c4
.cand:	ldr	x8,[sp]
	cbnz	x8,.t
	stp	x10,x11,[sp]
	b	.t
.yes:	add	sp,sp,16
	mov	x2,x10
	mov	x3,x11
	ret
.no0:	ldp	x2,x3,[sp],16
.no:	ret

; x9 = case, x1 = nom, x2 = out, x3 = cap -> x0 = longueur, DKE_NOTFOUND
xml_attr_s:
	mov	x13,x30
	mov	x14,x2
	mov	x15,x3
	bl	attr_find
	cbz	x2,.abs
	mov	x0,x2
	mov	x1,x3
	mov	x10,x14
	mov	x11,x15
	sink_init
	bl	xml_decode
	sink_end
	mov	x0,x12
	mov	x30,x13
	ret
.abs:	mov	x0,DKE_NOTFOUND
	mov	x30,x13
	ret

dk_xml_attr:
	dk_slot	KIND_XML,dk_badf
	b	xml_attr_s

dk_xml_attr_int:
	dk_slot	KIND_XML,dk_badf
	stp	x29,x30,[sp,-64]!
	str	x2,[sp,16]
	add	x2,sp,24
	mov	x3,40
	bl	xml_attr_s
	ldr	x1,[sp,16]
	tbnz	x0,63,.def
	add	x2,sp,24
.sp:	ldrb	w3,[x2]
	cmp	w3,' '
	b.eq	.sk
	cmp	w3,9
	b.eq	.sk
	cmp	w3,10
	b.eq	.sk
	cmp	w3,13
	b.ne	.sg
.sk:	add	x2,x2,1
	b	.sp
.sg:	mov	x4,0
	cmp	w3,'-'
	b.ne	.pl
	mov	x4,1
	add	x2,x2,1
	b	.dg
.pl:	cmp	w3,'+'
	b.ne	.dg
	add	x2,x2,1
.dg:	mov	x1,0
	mov	x5,10
.d:	ldrb	w3,[x2],1
	sub	w3,w3,'0'
	cmp	w3,9
	b.hi	.fin
	madd	x1,x1,x5,x3
	b	.d
.fin:	cbz	x4,.def
	neg	x1,x1
.def:	mov	x0,x1
	ldp	x29,x30,[sp],64
	ret

dk_xml_attr_bool:
	dk_slot	KIND_XML,dk_badf
	stp	x29,x30,[sp,-32]!
	str	x2,[sp,16]
	add	x2,sp,24
	mov	x3,8
	bl	xml_attr_s
	ldr	x1,[sp,16]
	tbnz	x0,63,.def
	mov	x1,0
	cbz	x0,.def
	ldrb	w2,[sp,24]
	cmp	w2,'1'
	b.eq	.yes
	cmp	w2,'t'
	b.eq	.yes
	cmp	w2,'T'
	b.ne	.def
.yes:	mov	x1,1
.def:	mov	x0,x1
	ldp	x29,x30,[sp],32
	ret

dk_xml_attr_on:
	dk_slot	KIND_XML,dk_badf
	stp	x29,x30,[sp,-32]!
	cbnz	x1,.nm
	adr	x1,s_val
.nm:	add	x2,sp,16
	mov	x3,16
	bl	xml_attr_s
	tbnz	x0,63,.on
	ldrb	w2,[sp,16]
	ldrb	w3,[sp,17]
	cmp	w2,'0'
	b.eq	.off
	cmp	w2,'f'
	b.eq	.off
	cmp	w2,'F'
	b.eq	.off
	cmp	w2,'o'
	b.ne	.no
	cmp	w3,'f'
	b.eq	.off
.no:	cmp	w2,'n'
	b.ne	.on
	cmp	w3,'o'
	b.ne	.on
.off:	mov	x0,0
	b	.out
.on:	mov	x0,1
.out:	ldp	x29,x30,[sp],32
	ret

dk_xml_text:
	dk_slot	KIND_XML,dk_badf
	mov	x13,x30
	mov	x10,x1
	mov	x11,x2
	sink_init
	ldr	x2,[x9,XR_EV]
	cmp	x2,DKX_TEXT
	b.ne	.out
	ldp	x0,x1,[x9,XR_TS]
	ldr	x2,[x9,XR_FL]
	tbnz	x2,FLB_RAW,.raw
	bl	xml_decode
	b	.out
.raw:	sub	x1,x1,x0
	bl	sk_mem
.out:	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

dk_xml_skip:
	dk_slot	KIND_XML,dk_badf
	ldr	x1,[x9,XR_EV]
	cmp	x1,DKX_START
	b.ne	.done
	mov	x13,x30
	mov	x14,1
.l:	bl	xml_step
	cbz	x0,.out
	cmp	x0,DKX_START
	b.ne	.s
	add	x14,x14,1
	b	.l
.s:	cmp	x0,DKX_END
	b.ne	.l
	subs	x14,x14,1
	b.ne	.l
.out:	mov	x30,x13
.done:	mov	x0,0
	ret

dk_xml_pos:
	dk_slot	KIND_XML,dk_badf
	ldr	x0,[x9,XR_P]
	ldr	x1,[x9,XR_BASE]
	sub	x0,x0,x1
	ret

dk_xml_esc:
	mov	x13,x30
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x2,0
	bl	sk_esc
	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

dk_xml_esc_attr:
	mov	x13,x30
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x2,1
	bl	sk_esc
	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

; ---------------------------------------------------------------------------
;  UTF-8, CP1252, Latin-1
; ---------------------------------------------------------------------------

; x0 = texte, x1 = fin (x0 < x1) -> w2 = point de code, x3 = octets, 0 : invalide.
; Touche x2-x5, garde x0, x1, x6-x15.
u8_next:
	ldrb	w2,[x0]
	cmp	w2,0x80
	b.hs	.m
	mov	x3,1
	ret
.m:	cmp	w2,0xC2
	b.lo	.bad
	mov	x3,1
	and	w2,w2,0x1F
	ldrb	w4,[x0]
	cmp	w4,0xE0
	b.lo	.go
	mov	x3,2
	and	w2,w2,0x0F
	cmp	w4,0xF0
	b.lo	.go
	mov	x3,3
	and	w2,w2,0x07
	cmp	w4,0xF5
	b.hs	.bad
.go:	sub	x4,x1,x0
	cmp	x4,x3
	b.ls	.bad
	mov	x4,1
.c:	ldrb	w5,[x0,x4]
	and	w5,w5,0xC0
	cmp	w5,0x80
	b.ne	.bad
	ldrb	w5,[x0,x4]
	and	w5,w5,0x3F
	orr	w2,w5,w2,lsl 6
	add	x4,x4,1
	cmp	x4,x3
	b.ls	.c
	mov	x3,x4				; octets
	cmp	x3,2
	b.eq	.ok
	cmp	x3,3
	b.ne	.f4
	cmp	w2,0x800
	b.lo	.bad
	movl	w5,0xD800
	sub	w5,w2,w5
	cmp	w5,0x800
	b.lo	.bad
	b	.ok
.f4:	movl	w5,0x10000
	cmp	w2,w5
	b.lo	.bad
	movl	w5,0x10FFFF
	cmp	w2,w5
	b.hi	.bad
.ok:	ret
.bad:	mov	x3,0
	ret

dk_utf8_encode:
	mov	x13,x30
	mov	w7,w0
	mov	x10,x1
	mov	x11,5
	mov	x12,0
	bl	sk_cp
	mov	x0,x12
	mov	x30,x13
	ret

dk_utf8_decode:
	cmp	x1,0
	b.le	.fin
	mov	x6,x2
	add	x1,x0,x1
	mov	x13,x30
	bl	u8_next
	mov	x30,x13
	cbz	x3,.bad
	cbz	x6,.nop
	str	w2,[x6]
.nop:	mov	x0,x3
	ret
.bad:	mov	x0,DKE_CORRUPT
	ret
.fin:	mov	x0,0
	ret

dk_utf8_valid:
	mov	x13,x30
	cmp	x1,0
	b.le	.yes
	add	x1,x0,x1
.l:	cmp	x0,x1
	b.hs	.yes
	bl	u8_next
	cbz	x3,.no
	add	x0,x0,x3
	b	.l
.yes:	mov	x0,1
	mov	x30,x13
	ret
.no:	mov	x0,0
	mov	x30,x13
	ret

dk_utf8_count:
	mov	x13,x30
	mov	x14,0
	cmp	x1,0
	b.le	.e
	add	x1,x0,x1
.l:	cmp	x0,x1
	b.hs	.e
	bl	u8_next
	cbnz	x3,.n
	mov	x3,1
.n:	add	x0,x0,x3
	add	x14,x14,1
	b	.l
.e:	mov	x0,x14
	mov	x30,x13
	ret

dk_utf8_to_cp:
	mov	x13,x30
	mov	x14,0
	cmp	x1,0
	b.le	.e
	add	x1,x0,x1
	mov	x6,x2
	mov	x7,x3
.l:	cmp	x0,x1
	b.hs	.e
	cmp	x14,x7
	b.ge	.e
	bl	u8_next
	cbnz	x3,.n
	ldrb	w2,[x0]
	mov	x3,1
.n:	add	x0,x0,x3
	str	w2,[x6,x14,lsl 2]
	add	x14,x14,1
	b	.l
.e:	mov	x0,x14
	mov	x30,x13
	ret

dk_cp_to_utf8:
	mov	x13,x30
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x2,x0
.l:	cmp	x1,0
	b.le	.e
	ldr	w7,[x2],4
	bl	sk_cp
	sub	x1,x1,1
	b	.l
.e:	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

dk_latin1_to_utf8:
	mov	x14,0
	b	to_utf8
dk_cp1252_to_utf8:
	mov	x14,1
; x0 = texte, x1 = octets, x2 = out, x3 = cap, x14 = 1 : CP1252
to_utf8:
	mov	x13,x30
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x2,x0
	adr	x3,cp1252_tab
.l:	cmp	x1,0
	b.le	.e
	ldrb	w7,[x2],1
	cbz	x14,.p
	sub	w4,w7,0x80
	cmp	w4,0x20
	b.hs	.p
	ldrh	w7,[x3,x4,lsl 1]
.p:	bl	sk_cp
	sub	x1,x1,1
	b	.l
.e:	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

dk_cp1252_cp:
	and	x0,x0,0xFF
	sub	w1,w0,0x80
	cmp	w1,0x20
	b.hs	.e
	adr	x2,cp1252_tab
	ldrh	w0,[x2,x1,lsl 1]
.e:	ret

dk_cp_cp1252:
	mov	x13,x30
	bl	cp_to_1252
	mov	x30,x13
	ret

; x0 = point de code -> x0 = octet, DKE_NOTFOUND. Touche x0, x4-x6.
cp_to_1252:
	cmp	x0,0x80
	b.lo	.e
	sub	x4,x0,0xA0
	cmp	x4,0x60
	b.lo	.e
	adr	x4,cp1252_tab
	mov	x5,0
.l:	ldrh	w6,[x4,x5,lsl 1]
	cmp	x6,x0
	b.eq	.f
	add	x5,x5,1
	cmp	x5,0x20
	b.lo	.l
	mov	x0,DKE_NOTFOUND
.e:	ret
.f:	add	x0,x5,0x80
	ret

dk_utf8_to_latin1:
	mov	x14,0
	b	from_utf8
dk_utf8_to_cp1252:
	mov	x14,1
; x0 = texte, x1 = octets, x2 = out, x3 = cap, x4 = remplacant, x14 = 1 : CP1252
from_utf8:
	mov	x13,x30
	mov	x15,x4
	mov	x10,x2
	mov	x11,x3
	sink_init
	cmp	x1,0
	b.le	.e
	add	x1,x0,x1
.l:	cmp	x0,x1
	b.hs	.e
	bl	u8_next
	cbnz	x3,.n
	ldrb	w2,[x0]				; (octet invalide : tel quel)
	mov	x3,1
.n:	add	x0,x0,x3
	mov	x7,x0
	mov	w0,w2
	cbz	x14,.l1
	bl	cp_to_1252
	b	.t
.l1:	cmp	x0,0x100
	b.lo	.t
	mov	x0,DKE_NOTFOUND
.t:	tbz	x0,63,.p
	mov	x0,x15
	cbz	x0,.s
.p:	emit	w0
.s:	mov	x0,x7
	b	.l
.e:	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

; ---------------------------------------------------------------------------
;  Longueurs, couleurs
; ---------------------------------------------------------------------------

; x1 = DKU_* -> x1 = EMU de l'unite (inconnue : 1). Touche x4.
unit_emu:
	cmp	x1,DKU_COUNT
	b.hs	.u
	adr	x4,unit_tab
	ldr	w1,[x4,x1,lsl 2]
	ret
.u:	mov	x1,1
	ret

; x0 = EMU (positif), x2 = DKU_* -> x0 = valeur arrondie. Touche x1-x4.
emu_to:
	mov	x1,x2
	mov	x3,x30
	bl	unit_emu
	mov	x30,x3
	add	x0,x0,x1,lsr 1
	udiv	x0,x0,x1
	ret

dk_len_conv:
	mov	x13,x30
	mov	x5,0
	tbz	x0,63,.p
	mov	x5,1
	neg	x0,x0
.p:	mov	x6,x0
	bl	unit_emu
	mul	x0,x6,x1
	bl	emu_to
	cbz	x5,.r
	neg	x0,x0
.r:	mov	x30,x13
	ret

dk_len_parse:
	mov	x13,x30
	mov	x14,x1				; unite voulue
	mov	x15,x2				; unite d'un nombre nu
	mov	x9,x3				; defaut
	cbz	x0,.def
.sp:	ldrb	w2,[x0]
	cmp	w2,' '
	b.ne	.sg
	add	x0,x0,1
	b	.sp
.sg:	mov	x5,0				; negatif
	cmp	w2,'-'
	b.ne	.pl
	mov	x5,1
	add	x0,x0,1
	b	.ip
.pl:	cmp	w2,'+'
	b.ne	.ip
	add	x0,x0,1
.ip:	mov	x6,0				; partie entiere
	mov	x7,0				; 1e-6
	mov	x8,0				; chiffres vus
	mov	x3,10
.i:	ldrb	w2,[x0]
	sub	w4,w2,'0'
	cmp	w4,9
	b.hi	.dot
	add	x0,x0,1
	add	x8,x8,1
	lsr	x1,x6,31
	cbnz	x1,.i				; (au-dela : sature)
	madd	x6,x6,x3,x4
	b	.i
.dot:	cmp	w2,'.'
	b.eq	.fr
	cmp	w2,','
	b.ne	.unit
.fr:	add	x0,x0,1
	mov	x1,0				; decimales gardees
.f:	ldrb	w2,[x0]
	sub	w4,w2,'0'
	cmp	w4,9
	b.hi	.pad
	add	x0,x0,1
	add	x8,x8,1
	cmp	x1,6
	b.hs	.f
	madd	x7,x7,x3,x4
	add	x1,x1,1
	b	.f
.pad:	cmp	x1,6
	b.hs	.unit
	mul	x7,x7,x3
	add	x1,x1,1
	b	.pad
.unit:	cbz	x8,.def
	ldrb	w2,[x0]
	cmp	w2,'%'
	b.eq	.def
	mov	w3,0
	cbz	w2,.suf
	ldrb	w3,[x0,1]
.suf:	orr	w2,w2,w3,lsl 8
	mov	x1,DKU_CM
	movl	w4,'c' + ('m' shl 8)
	cmp	w2,w4
	b.eq	.got
	mov	x1,DKU_MM
	movl	w4,'m' + ('m' shl 8)
	cmp	w2,w4
	b.eq	.got
	mov	x1,DKU_IN
	movl	w4,'i' + ('n' shl 8)
	cmp	w2,w4
	b.eq	.got
	mov	x1,DKU_PT
	movl	w4,'p' + ('t' shl 8)
	cmp	w2,w4
	b.eq	.got
	mov	x1,DKU_PC
	movl	w4,'p' + ('c' shl 8)
	cmp	w2,w4
	b.eq	.got
	mov	x1,DKU_PX
	movl	w4,'p' + ('x' shl 8)
	cmp	w2,w4
	b.eq	.got
	mov	x1,x15
.got:	bl	unit_emu
	mul	x0,x6,x1			; EMU
	mul	x7,x7,x1
	movl	x3,500000
	add	x7,x7,x3
	movl	x3,1000000
	udiv	x7,x7,x3
	add	x0,x0,x7
	mov	x2,x14
	bl	emu_to
	cbz	x5,.r
	neg	x0,x0
.r:	mov	x30,x13
	ret
.def:	mov	x0,x9
	mov	x30,x13
	ret

dk_len_write:
	stp	x29,x30,[sp,-16]!
	mov	x10,x4
	mov	x11,x5
	sink_init
	mov	x13,x2				; unite ecrite
	mov	x14,x3				; decimales
	cmp	x14,6
	b.ls	.dok
	mov	x14,6
.dok:	tbz	x0,63,.p
	neg	x0,x0
	mov	w6,'-'
	emit	w6
.p:	mov	x15,x0
	bl	unit_emu
	mul	x15,x15,x1			; EMU
	mov	x1,x13
	bl	unit_emu
	mov	x8,x1				; EMU par unite ecrite
	udiv	x0,x15,x8
	msub	x7,x0,x8,x15			; reste
	mov	x3,1				; 10^decimales
	mov	x2,x14
	mov	x4,10
.pw:	cbz	x2,.pwe
	mul	x3,x3,x4
	sub	x2,x2,1
	b	.pw
.pwe:	mul	x7,x7,x3
	add	x7,x7,x8,lsr 1
	udiv	x7,x7,x8			; decimales arrondies
	cmp	x7,x3
	b.lo	.nc
	add	x0,x0,1
	mov	x7,0
.nc:	bl	sk_num
	cbz	x14,.su
	mov	w6,'.'
	emit	w6
	mov	x0,x7
	mov	x1,x14
	bl	sk_num_pad
.su:	cmp	x13,DKU_COUNT
	b.hs	.fin
	adr	x0,unit_suffix
	add	x0,x0,x13,lsl 2
	bl	sk_str
.fin:	sink_end
	mov	x0,x12
	ldp	x29,x30,[sp],16
	ret

dk_color_parse:
	cbz	x0,.def
	ldrb	w2,[x0]
	cmp	w2,'#'
	b.ne	.n
	add	x0,x0,1
.n:	mov	x3,0				; chiffres
	mov	x4,0				; les 6 premiers
	mov	x5,0				; 6 apres deux premiers (8 chiffres)
.l:	cmp	x3,8
	b.hs	.fin
	ldrb	w2,[x0,x3]
	sub	w6,w2,'0'
	cmp	w6,9
	b.ls	.d
	orr	w2,w2,32
	sub	w6,w2,'a'
	cmp	w6,5
	b.hi	.fin
	add	w6,w6,10
.d:	cmp	x3,6
	b.hs	.h2
	add	x4,x6,x4,lsl 4
.h2:	add	x5,x6,x5,lsl 4
	add	x3,x3,1
	b	.l
.fin:	cmp	x3,6
	b.lo	.def
	mov	x0,x4
	cmp	x3,8
	b.ne	.r
	and	x0,x5,0xFFFFFF
.r:	ret
.def:	mov	x0,x1
	ret

dk_color_write:
	adr	x3,s_hex_up
	tbz	x1,1,.c
	adr	x3,s_hex_low
.c:	mov	x4,x2
	tbz	x1,0,.h
	mov	w5,'#'
	strb	w5,[x2],1
.h:	tbz	x1,2,.b
	mov	w5,'F'
	tbz	x1,1,.a
	mov	w5,'f'
.a:	strb	w5,[x2],1
	strb	w5,[x2],1
.b:	mov	x6,20
.l:	lsr	x5,x0,x6
	and	x5,x5,15
	ldrb	w5,[x3,x5]
	strb	w5,[x2],1
	subs	x6,x6,4
	b.pl	.l
	strb	wzr,[x2]
	sub	x0,x2,x4
	ret

; ---------------------------------------------------------------------------
;  Dates
; ---------------------------------------------------------------------------

; x0 = annee, x1 = mois (hors bornes : reporte), x2 = jour -> x0 = jours, x1 = mois
; ramene, x7 = annee ramenee. Touche x3-x8.
civil2days:
	sub	x3,x1,1
	mov	x4,12
	sdiv	x5,x3,x4
	msub	x6,x5,x4,x3
	tbz	x6,63,.m
	sub	x5,x5,1
	add	x6,x6,12
.m:	add	x0,x0,x5
	add	x1,x6,1
	mov	x7,x0				; annee ramenee
	cmp	x1,2
	b.hi	.y
	sub	x0,x0,1
.y:	mov	x4,400
	sdiv	x5,x0,x4
	msub	x6,x5,x4,x0
	tbz	x6,63,.e
	sub	x5,x5,1
	add	x6,x6,400
.e:	cmp	x1,2
	b.hi	.a
	add	x3,x1,9
	b	.b
.a:	sub	x3,x1,3
.b:	mov	x4,153
	mul	x3,x3,x4
	add	x3,x3,2
	mov	x4,5
	udiv	x3,x3,x4
	add	x3,x3,x2
	sub	x3,x3,1				; jour de l'annee
	mov	x4,365
	mul	x0,x6,x4
	add	x0,x0,x6,lsr 2
	mov	x4,100
	udiv	x4,x6,x4
	sub	x0,x0,x4
	add	x0,x0,x3			; jour de l'ere
	movl	x4,146097
	mul	x5,x5,x4
	add	x0,x0,x5
	movl	x4,719468
	sub	x0,x0,x4
	ret

; x0 = jours -> x1 = annee, x2 = mois, x3 = jour. Touche x0, x4-x8.
days2civil:
	movl	x4,719468
	add	x0,x0,x4
	movl	x4,146097
	sdiv	x5,x0,x4
	msub	x6,x5,x4,x0
	tbz	x6,63,.e
	sub	x5,x5,1
	add	x6,x6,x4
.e:	mov	x4,1460				; annee de l'ere
	udiv	x7,x6,x4
	sub	x1,x6,x7
	movl	x4,36524
	udiv	x7,x6,x4
	add	x1,x1,x7
	movl	x4,146096
	udiv	x7,x6,x4
	sub	x1,x1,x7
	mov	x4,365
	udiv	x7,x1,x4			; yoe
	mov	x4,400
	mul	x1,x5,x4
	add	x1,x1,x7			; annee
	mov	x4,365				; jour de l'annee
	mul	x3,x7,x4
	add	x3,x3,x7,lsr 2
	mov	x4,100
	udiv	x4,x7,x4
	sub	x3,x3,x4
	sub	x3,x6,x3
	mov	x4,5				; mois depuis mars
	mul	x5,x3,x4
	add	x5,x5,2
	mov	x4,153
	udiv	x5,x5,x4
	mul	x6,x5,x4
	add	x6,x6,2
	mov	x4,5
	udiv	x6,x6,x4
	sub	x3,x3,x6
	add	x3,x3,1				; jour
	cmp	x5,10
	b.hs	.j
	add	x2,x5,3
	ret
.j:	sub	x2,x5,9
	add	x1,x1,1
	ret

dk_days_from_civil:
	mov	x13,x30
	bl	civil2days
	mov	x30,x13
	ret

dk_civil_from_days:
	mov	x13,x30
	mov	x9,x1
	mov	x10,x2
	mov	x11,x3
	bl	days2civil
	mov	x30,x13
; x1, x2, x3 vers les int x9, x10, x11 (0 : ignore) -> x0 = 0
put_ymd:
	cbz	x9,.m
	str	w1,[x9]
.m:	cbz	x10,.d
	str	w2,[x10]
.d:	cbz	x11,.e
	str	w3,[x11]
.e:	mov	x0,0
	ret

dk_weekday:
	add	x0,x0,4				; 1970-01-01 : un jeudi
	mov	x1,7
	sdiv	x2,x0,x1
	msub	x0,x2,x1,x0
	tbz	x0,63,.e
	add	x0,x0,7
.e:	ret

dk_excel_from_civil:
	mov	x13,x30
	mov	x14,x3				; systeme 1904
	mov	x15,x2				; jour
	cmp	x0,1900				; (annees 0..1899, comme DATE)
	b.hs	.g
	add	x0,x0,1900
.g:	mov	x2,1
	bl	civil2days
	add	x0,x0,x15
	sub	x0,x0,1
	movl	x2,25569
	add	x0,x0,x2
	cmp	x0,61
	b.ge	.f
	sub	x0,x0,1
.f:	cmp	x7,1900				; 29 fevrier 1900 : 60
	b.ne	.s
	cmp	x1,2
	b.ne	.s
	cmp	x15,29
	b.ne	.s
	mov	x0,60
.s:	cbz	x14,.r
	movl	x2,1462
	sub	x0,x0,x2
.r:	mov	x30,x13
	ret

dk_civil_from_excel:
	mov	x13,x30
	mov	x9,x2
	mov	x10,x3
	mov	x11,x4
	cbz	x1,.b
	movl	x2,1462
	add	x0,x0,x2
.b:	cmp	x0,60
	b.ne	.nf
	movl	x1,1900
	mov	x2,2
	mov	x3,29
	b	put_ymd2
.nf:	cmp	x0,0
	b.gt	.p
	movl	x1,1900
	mov	x2,1
	mov	x3,0
	b	put_ymd2
.p:	cmp	x0,60
	b.ge	.c
	add	x0,x0,1
.c:	movl	x2,25569
	sub	x0,x0,x2
	bl	days2civil
put_ymd2:
	mov	x30,x13
	b	put_ymd

; ---------------------------------------------------------------------------
;  CSV
; ---------------------------------------------------------------------------

; x1 = position, x2 = fin, w3 = separateur, sortie prete -> x1 = apres le champ,
; x4 = DKV_EOL + DKV_QUOTED, x0 : 1 si fini par le separateur. Touche x5, w6.
csv_scan:
	mov	x4,0
	mov	x5,0				; entre guillemets
	mov	x0,0
.l:	cmp	x1,x2
	b.hs	.fin
	ldrb	w6,[x1]
	cbz	x5,.out
	cmp	w6,'"'
	b.ne	.pc
	add	x6,x1,1
	cmp	x6,x2
	b.hs	.cq
	ldrb	w6,[x6]
	cmp	w6,'"'
	b.ne	.cq
	add	x1,x1,2
	mov	w6,'"'
	emit	w6
	b	.l
.cq:	mov	x5,0
	add	x1,x1,1
	b	.l
.pc:	add	x1,x1,1
	emit	w6
	b	.l
.out:	cmp	w6,'"'
	b.ne	.sep
	cbnz	x12,.sep
	mov	x5,1
	orr	x4,x4,DKV_QUOTED
	add	x1,x1,1
	b	.l
.sep:	cmp	w6,w3
	b.ne	.lf
	add	x1,x1,1
	mov	x0,1
	ret
.lf:	cmp	w6,10
	b.ne	.cr
	add	x1,x1,1
.fin:	orr	x4,x4,DKV_EOL
	ret
.cr:	cmp	w6,13
	b.ne	.pc
	add	x0,x1,1
	cmp	x0,x2
	mov	x0,0
	b.hs	.pc
	ldrb	w0,[x1,1]
	cmp	w0,10
	mov	x0,0
	b.ne	.pc
	add	x1,x1,2
	b	.fin

; x0 = texte, x1 = fin -> w0 = le separateur le plus regulier des 20 premieres lignes
csv_guess:
	stp	x29,x30,[sp,-16]!
	mov	x13,x0
	mov	x14,x1
	mov	x15,0				; meilleur
	mov	x16,-1				; son score
	mov	x17,0				; rang du candidat
.k:	adr	x2,csv_seps
	ldrb	w2,[x2,x17]
	mov	x3,x13				; i
	mov	x4,0				; lignes
	mov	x5,-1				; premiere ligne
	mov	x6,0				; lignes pareilles
	mov	x7,0				; dans la ligne
	mov	x8,0				; guillemets
.l:	cmp	x3,x14
	b.hs	.sc
	cmp	x4,20
	b.hs	.sc
	ldrb	w9,[x3],1
	cmp	w9,'"'
	b.ne	.nq
	eor	x8,x8,1
	b	.l
.nq:	cbnz	x8,.l
	cmp	w9,w2
	b.ne	.nl
	add	x7,x7,1
	b	.l
.nl:	cmp	w9,10
	b.ne	.l
	tbz	x5,63,.sec
	mov	x5,x7
	b	.nx
.sec:	cmp	x7,x5
	b.ne	.nx
	cbz	x7,.nx
	add	x6,x6,1
.nx:	add	x4,x4,1
	mov	x7,0
	b	.l
.sc:	mov	x0,0
	cmp	x5,0
	b.le	.cmp
	mov	x1,100
	madd	x0,x6,x1,x5
.cmp:	cmp	x0,x16
	b.le	.nk
	mov	x16,x0
	mov	x15,x17
.nk:	add	x17,x17,1
	cmp	x17,3
	b.lo	.k
	adr	x2,csv_seps
	ldrb	w0,[x2,x15]
	ldp	x29,x30,[sp],16
	ret

dk_csv_open:
	cmp	x1,0
	b.lt	.inval
	stp	x29,x30,[sp,-16]!
	mov	x10,x0
	mov	x11,x1
	mov	x12,x2
	cmp	x11,3				; BOM
	b.lo	.nb
	ldrb	w2,[x10]
	cmp	w2,0xEF
	b.ne	.nb
	ldrb	w2,[x10,1]
	cmp	w2,0xBB
	b.ne	.nb
	ldrb	w2,[x10,2]
	cmp	w2,0xBF
	b.ne	.nb
	add	x10,x10,3
	sub	x11,x11,3
.nb:	add	x11,x10,x11			; fin
	cbnz	x12,.ns
	mov	x0,x10
	mov	x1,x11
	bl	csv_guess
	mov	x12,x0
.ns:	mov	x1,KIND_CSV
	bl	slot_new
	tbnz	x0,63,.e
	str	x10,[x9,XR_P]
	str	x10,[x9,XR_BASE]
	str	x11,[x9,XR_E]
	str	x12,[x9,XR_SEP]
.e:	ldp	x29,x30,[sp],16
	ret
.inval:	mov	x0,DKE_INVAL
	ret

dk_csv_close:
	dk_slot	KIND_CSV,dk_badf
	stlr	xzr,[x9]
	mov	x0,0
	ret

dk_csv_next:
	dk_slot	KIND_CSV,dk_badf
	ldr	x1,[x9,XR_P]
	ldr	x2,[x9,XR_E]
	ldr	x3,[x9,XR_SEP]
	ldr	x7,[x9,XR_FL]
	cmp	x1,x2
	b.lo	.go
	tbnz	x7,FLB_SEP,.go			; (a,b, : un dernier champ vide)
	mov	x0,0
	ret
.go:	str	x1,[x9,XR_FS]
	mov	x10,0
	mov	x11,0
	mov	x12,0
	mov	x13,x30
	bl	csv_scan
	mov	x30,x13
	str	x1,[x9,XR_P]
	lsl	x0,x0,FLB_SEP
	str	x0,[x9,XR_FL]
	orr	x0,x4,DKV_FIELD
	ret

dk_csv_text:
	dk_slot	KIND_CSV,dk_badf
	mov	x10,x1
	mov	x11,x2
	sink_init
	ldr	x1,[x9,XR_FS]
	ldr	x2,[x9,XR_E]
	ldr	x3,[x9,XR_SEP]
	cbz	x1,.out
	mov	x13,x30
	bl	csv_scan
	mov	x30,x13
.out:	sink_end
	mov	x0,x12
	ret

dk_csv_sep:
	dk_slot	KIND_CSV,dk_badf
	ldr	x0,[x9,XR_SEP]
	ret

dk_csv_esc:
	mov	x13,x30
	mov	x10,x3
	mov	x11,x4
	sink_init
	mov	x3,x2				; separateur
	tbz	x1,63,.len
	mov	x1,x0
.s:	ldrb	w2,[x1],1
	cbnz	w2,.s
	sub	x1,x1,1
	sub	x1,x1,x0
.len:	add	x2,x0,x1			; fin
	mov	x4,x0
.q:	cmp	x4,x2
	b.hs	.plain
	ldrb	w5,[x4],1
	cmp	w5,w3
	b.eq	.need
	cmp	w5,'"'
	b.eq	.need
	cmp	w5,10
	b.eq	.need
	cmp	w5,13
	b.ne	.q
.need:	mov	w5,'"'
	emit	w5
.c:	cmp	x0,x2
	b.hs	.ce
	ldrb	w5,[x0],1
	cmp	w5,'"'
	b.ne	.cp
	emit	w5
.cp:	emit	w5
	b	.c
.ce:	mov	w5,'"'
	emit	w5
	b	.e
.plain:	bl	sk_mem
.e:	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

; ---------------------------------------------------------------------------
;  Paquets
; ---------------------------------------------------------------------------

; x3 = texte, x4 = lit, x5 = octets -> Z : egaux. Touche x3-x7.
mem_eq:
.l:	cbz	x5,.y
	ldrb	w6,[x3],1
	ldrb	w7,[x4],1
	cmp	w6,w7
	b.ne	.e
	sub	x5,x5,1
	b	.l
.y:	cmp	x5,x5
.e:	ret

dk_doc_kind:
	cmp	x1,4
	b.lt	.unk
	ldr	w2,[x0]
	movl	w3,0xE011CFD0
	cmp	w2,w3
	b.ne	.nole
	mov	x0,DKK_OLE
	ret
.nole:	movl	w3,0x74725C7B			; {\rt
	cmp	w2,w3
	b.ne	.nrtf
	cmp	x1,5
	b.lt	.unk
	ldrb	w3,[x0,4]
	cmp	w3,'f'
	b.ne	.unk
	mov	x0,DKK_RTF
	ret
.nrtf:	movl	w3,0x06054B50			; ZIP vide
	cmp	w2,w3
	b.eq	.zip
	movl	w3,0x04034B50
	cmp	w2,w3
	b.ne	.unk
	cmp	x1,38				; mimetype d'abord, non compresse : ODF
	b.lt	.cd
	ldrh	w2,[x0,26]
	cmp	w2,8
	b.ne	.cd
	ldrh	w2,[x0,8]
	cbnz	w2,.cd
	ldur	x2,[x0,30]
	adr	x4,s_mimetype
	ldr	x3,[x4]
	cmp	x2,x3
	b.ne	.cd
	ldrh	w2,[x0,28]
	add	x2,x2,38			; donnees
	ldur	w3,[x0,18]
	add	x4,x2,x3
	cmp	x4,x1
	b.hi	.cd
	mov	x10,x0
	mov	x11,x1
	mov	x13,x30
	add	x0,x0,x2
	mov	x1,x3
	bl	dk_doc_kind_mime
	mov	x30,x13
	cbnz	x0,.ret
	mov	x0,x10
	mov	x1,x11
.cd:	sub	x2,x1,22			; EOCD, depuis la fin
	movl	x3,65535
	sub	x3,x2,x3
	cmp	x3,0
	b.ge	.cl
	mov	x3,0
.cl:	movl	w4,0x06054B50
.ce:	cmp	x2,x3
	b.lt	.zip
	ldr	w5,[x0,x2]
	cmp	w5,w4
	b.eq	.eocd
	sub	x2,x2,1
	b	.ce
.eocd:	add	x2,x0,x2
	ldr	w2,[x2,16]			; debut du repertoire central
	mov	x8,0				; [Content_Types].xml vu
	mov	x9,0				; famille d'apres un nom
	movl	w10,0x02014B50
.ent:	add	x3,x2,46
	cmp	x3,x1
	b.hi	.fin
	add	x11,x0,x2
	ldr	w3,[x11]
	cmp	w3,w10
	b.ne	.fin
	ldrh	w12,[x11,28]			; nom
	ldrh	w13,[x11,30]
	ldrh	w14,[x11,32]
	add	x2,x2,46
	add	x3,x2,x12
	cmp	x3,x1
	b.hi	.fin
	add	x15,x0,x2			; le nom
	cmp	x12,19
	b.ne	.pre
	mov	x3,x15
	adr	x4,s_ct_name
	mov	x5,19
	str	x30,[sp,-16]!
	bl	mem_eq
	ldr	x30,[sp],16
	b.ne	.next
	mov	x8,1
	b	.next
.pre:	cbnz	x9,.next
	cmp	x12,5
	b.lo	.next
	ldr	w3,[x15]
	movl	w4,0x64726F77			; word
	cmp	w3,w4
	b.ne	.xl
	ldrb	w3,[x15,4]
	cmp	w3,'/'
	b.ne	.next
	mov	x9,DKK_DOCX
	b	.next
.xl:	movl	w4,0x2F6C78			; xl/
	and	w3,w3,0xFFFFFF
	cmp	w3,w4
	b.ne	.pp
	mov	x9,DKK_XLSX
	b	.next
.pp:	ldr	w3,[x15]
	movl	w4,0x2F747070			; ppt/
	cmp	w3,w4
	b.ne	.next
	mov	x9,DKK_PPTX
.next:	add	x2,x2,x12
	add	x2,x2,x13
	add	x2,x2,x14
	b	.ent
.fin:	cbz	x8,.zip
	cbz	x9,.zip
	mov	x0,x9
.ret:	ret
.zip:	mov	x0,DKK_ZIP
	ret
.unk:	mov	x0,DKK_UNKNOWN
	ret

dk_doc_kind_mime:
	tbz	x1,63,.len
	mov	x1,x0
.s:	ldrb	w2,[x1],1
	cbnz	w2,.s
	sub	x1,x1,1
	sub	x1,x1,x0
.len:	cmp	x1,0				; blancs de fin
	b.le	.none
	sub	x2,x1,1
	ldrb	w2,[x0,x2]
	cmp	w2,32
	b.hi	.t
	sub	x1,x1,1
	b	.len
.t:	mov	x2,DKK_DOCX			; rang
	adr	x8,mime_tab
.n:	ldr	w4,[x8,x2,lsl 2]
	add	x4,x4,x8			; le type
	mov	x5,0
.c:	cmp	x5,x1
	b.hs	.end
	ldrb	w6,[x4,x5]
	cbz	w6,.tail			; reste du texte : -template ?
	ldrb	w7,[x0,x5]
	cmp	w6,w7
	b.ne	.nx
	add	x5,x5,1
	b	.c
.end:	ldrb	w6,[x4,x5]
	cbz	w6,.ok
	b	.nx
.tail:	sub	x6,x1,x5
	cmp	x6,9
	b.ne	.nx
	add	x3,x0,x5
	adr	x4,s_template
	mov	x5,9
	mov	x9,x30
	bl	mem_eq
	mov	x30,x9
	b.eq	.ok
.nx:	add	x2,x2,1
	cmp	x2,DKK_ODP
	b.ls	.n
.none:	mov	x0,DKK_UNKNOWN
	ret
.ok:	mov	x0,x2
	ret

dk_doc_mime:
	sub	x1,x0,1
	cmp	x1,DKK_ODP
	b.hs	.no
	adr	x1,mime_tab
	ldr	w2,[x1,x0,lsl 2]
	add	x0,x1,x2
	ret
.no:	mov	x0,0
	ret

dk_part_path:
	mov	x13,x30
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x2,x0				; base
	mov	x3,x1				; cible
	ldrb	w4,[x3]
	cmp	w4,'/'
	b.ne	.rel
	add	x0,x3,1
	bl	sk_str
	b	.out
.rel:	mov	x4,0				; apres le dernier '/'
	mov	x5,0
.c:	ldrb	w6,[x2,x5]
	add	x5,x5,1
	cbz	w6,.up
	cmp	w6,'/'
	b.ne	.c
	mov	x4,x5
	b	.c
.up:	ldrb	w6,[x3]				; ../ et ./
	cmp	w6,'.'
	b.ne	.cp
	ldrb	w6,[x3,1]
	cmp	w6,'/'
	b.ne	.dd
	add	x3,x3,2
	b	.up
.dd:	cmp	w6,'.'
	b.ne	.cp
	ldrb	w6,[x3,2]
	cmp	w6,'/'
	b.ne	.cp
	add	x3,x3,3
	cbz	x4,.up
	sub	x4,x4,1
.bk:	cbz	x4,.up
	sub	x5,x4,1
	ldrb	w6,[x2,x5]
	cmp	w6,'/'
	b.eq	.up
	mov	x4,x5
	b	.bk
.cp:	mov	x0,x2
	mov	x1,x4
	bl	sk_mem
	mov	x0,x3
	bl	sk_str
.out:	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

dk_rels_path:
	mov	x13,x30
	mov	x10,x1
	mov	x11,x2
	sink_init
	mov	x2,x0
	mov	x4,0
	mov	x5,0
.c:	ldrb	w6,[x2,x5]
	add	x5,x5,1
	cbz	w6,.go
	cmp	w6,'/'
	b.ne	.c
	mov	x4,x5
	b	.c
.go:	mov	x0,x2
	mov	x1,x4
	bl	sk_mem
	adr	x0,s_rels_dir
	bl	sk_str
	add	x0,x2,x4
	bl	sk_str
	adr	x0,s_rels_ext
	bl	sk_str
	sink_end
	mov	x0,x12
	mov	x30,x13
	ret

; Un lecteur XML sur la pile pour parcourir un .rels ; x19 = rang restant ou Id,
; x20, x21, x22 : ce que la fonction garde.
macro rel_enter {
	stp	x29,x30,[sp,-48]!
	stp	x19,x20,[sp,16]
	stp	x21,x22,[sp,32]
	sub	sp,sp,SLOT_SIZE
	mov	x9,sp
	str	x0,[x9,XR_P]
	str	x0,[x9,XR_BASE]
	cmp	x1,0
	csel	x1,x1,xzr,gt
	add	x0,x0,x1
	str	x0,[x9,XR_E]
	str	xzr,[x9,XR_FL]
	str	xzr,[x9,XR_EV]
}
macro rel_leave {
	add	sp,sp,SLOT_SIZE
	ldp	x21,x22,[sp,32]
	ldp	x19,x20,[sp,16]
	ldp	x29,x30,[sp],48
	ret
}

; x9 = case -> x0 = 1 : sur un <Relationship>, 0 : fin. Garde x19-x22.
rel_next:
	mov	x16,x30
.l:	bl	xml_step
	cbz	x0,.e
	cmp	x0,DKX_START
	b.ne	.l
	adr	x1,s_Relationship
	bl	xml_is
	cbz	x0,.l
.e:	mov	x30,x16
	ret

dk_rel_count:
	rel_enter
	mov	x19,0
.l:	bl	rel_next
	cbz	x0,.e
	add	x19,x19,1
	b	.l
.e:	mov	x0,x19
	rel_leave

dk_rel_get:
	rel_enter
	mov	x19,x2
	mov	x20,x3
	mov	x21,x4
	mov	x22,x5
	cmp	x19,0
	b.lt	.nf
.l:	bl	rel_next
	cbz	x0,.nf
	subs	x19,x19,1
	b.pl	.l
	mov	x0,x20
	cmp	x0,DKR_MODE
	b.hi	.nf
	adr	x1,rel_attrs
	ldr	w2,[x1,x0,lsl 2]
	add	x1,x1,x2
	bl	attr_find
	mov	x10,x21
	mov	x11,x22
	sink_init
	cbz	x2,.fin
	cmp	x20,DKR_TYPE
	b.ne	.dec
	mov	x4,x3				; dernier composant
.b:	cmp	x4,x2
	b.ls	.dec
	ldrb	w5,[x4,-1]
	cmp	w5,'/'
	b.eq	.cut
	sub	x4,x4,1
	b	.b
.cut:	mov	x2,x4
.dec:	mov	x0,x2
	mov	x1,x3
	bl	xml_decode
.fin:	sink_end
	mov	x0,x12
	b	.out
.nf:	mov	x0,DKE_NOTFOUND
.out:	rel_leave

; x9 = case, sur un Relationship, x21 = out, x22 = cap -> sa cible dans x0
rel_put_target:
	mov	x20,x30
	adr	x1,s_Target
	bl	attr_find
	mov	x10,x21
	mov	x11,x22
	sink_init
	cbz	x2,.e
	mov	x0,x2
	mov	x1,x3
	bl	xml_decode
.e:	sink_end
	mov	x0,x12
	mov	x30,x20
	ret

dk_rel_target:
	rel_enter
	mov	x19,x2
	mov	x21,x3
	mov	x22,x4
.l:	bl	rel_next
	cbz	x0,.nf
	adr	x1,s_Id
	bl	attr_find
	cbz	x2,.l
	mov	x4,x19				; compare la valeur brute a l'Id
.c:	cmp	x2,x3
	b.hs	.f
	ldrb	w5,[x2],1
	ldrb	w6,[x4],1
	cmp	w5,w6
	b.eq	.c
	b	.l
.f:	ldrb	w6,[x4]
	cbnz	w6,.l
	bl	rel_put_target
	b	.out
.nf:	mov	x0,DKE_NOTFOUND
.out:	rel_leave

; w5 -> w5 en minuscule. Touche w7.
macro lower r* {
	sub	w7,r,'A'
	cmp	w7,25
	b.hi	@f
	add	r,r,32
@@:
}

dk_rel_by_type:
	rel_enter
	mov	x19,x2
	mov	x21,x3
	mov	x22,x4
.l:	bl	rel_next
	cbz	x0,.nf
	adr	x1,s_Type
	bl	attr_find
	cbz	x2,.l
	mov	x4,x3				; dernier composant
.b:	cmp	x4,x2
	b.ls	.cmp
	ldrb	w5,[x4,-1]
	cmp	w5,'/'
	b.eq	.cmp
	sub	x4,x4,1
	b	.b
.cmp:	mov	x6,x19
.c:	cmp	x4,x3
	b.hs	.f
	ldrb	w5,[x4],1
	ldrb	w8,[x6],1
	lower	w5
	lower	w8
	cmp	w5,w8
	b.eq	.c
	b	.l
.f:	ldrb	w8,[x6]
	cbnz	w8,.l
	adr	x1,s_TargetMode			; External : non
	bl	attr_find
	cbz	x2,.ok
	sub	x4,x3,x2
	cmp	x4,8
	b.ne	.ok
	ldrb	w5,[x2]
	lower	w5
	cmp	w5,'e'
	b.eq	.l
.ok:	bl	rel_put_target
	b	.out
.nf:	mov	x0,DKE_NOTFOUND
.out:	rel_leave

dk_env_text:
	cmp	x0,DKS_COUNT
	b.hs	.no
	adr	x1,env_tab
	ldr	w2,[x1,x0,lsl 2]
	add	x0,x1,x2
	ret
.no:	mov	x0,0
	ret

dk_rel_write:
	stp	x29,x30,[sp,-16]!
	mov	x10,x4
	mov	x11,x5
	sink_init
	mov	x13,x1				; type
	mov	x14,x2				; cible
	mov	x15,x3				; externe
	mov	x4,x0
	adr	x0,s_rw1
	bl	sk_str
	mov	x0,x4
	bl	sk_num
	adr	x0,s_rw2
	bl	sk_str
	mov	x0,x13				; un ':' dans le type : URI complete
.c:	ldrb	w6,[x0],1
	cbz	w6,.pre
	cmp	w6,':'
	b.ne	.c
	b	.full
.pre:	adr	x0,s_rel_ns
	bl	sk_str
.full:	mov	x0,x13
	bl	sk_attr
	adr	x0,s_rw3
	bl	sk_str
	mov	x0,x14
	bl	sk_attr
	cbz	x15,.ne
	adr	x0,s_rw4
	bl	sk_str
.ne:	adr	x0,s_rw5
	bl	sk_str
	sink_end
	mov	x0,x12
	ldp	x29,x30,[sp],16
	ret

dk_opc_default:
	stp	x29,x30,[sp,-16]!
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x13,x0
	mov	x14,x1
	adr	x0,s_od1
	bl	sk_str
	mov	x0,x13
	bl	sk_attr
	b	opc_tail

dk_opc_override:
	stp	x29,x30,[sp,-16]!
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x13,x0
	mov	x14,x1
	adr	x0,s_ov1
	bl	sk_str
	ldrb	w6,[x13]
	cmp	w6,'/'
	b.eq	.s
	mov	w6,'/'
	emit	w6
.s:	mov	x0,x13
	bl	sk_attr
opc_tail:
	adr	x0,s_od2
	bl	sk_str
	mov	x0,x14
	bl	sk_attr
	adr	x0,s_rw5
	bl	sk_str
	sink_end
	mov	x0,x12
	ldp	x29,x30,[sp],16
	ret

dk_odf_entry:
	stp	x29,x30,[sp,-16]!
	mov	x10,x2
	mov	x11,x3
	sink_init
	mov	x13,x0
	mov	x14,x1
	adr	x0,s_me1
	bl	sk_str
	mov	x0,x13
	bl	sk_attr
	ldrb	w6,[x13]
	cmp	w6,'/'
	b.ne	.nv
	ldrb	w6,[x13,1]
	cbnz	w6,.nv
	adr	x0,s_me2
	bl	sk_str
.nv:	adr	x0,s_me3
	bl	sk_str
	mov	x0,x14
	bl	sk_attr
	adr	x0,s_rw5
	bl	sk_str
	sink_end
	mov	x0,x12
	ldp	x29,x30,[sp],16
	ret

dk_hash:
	movl	w2,2166136261
	movl	w3,16777619
	mov	x4,x1
	cmp	x4,0
	b.le	.e
.l:	ldrb	w5,[x0],1
	eor	w2,w2,w5
	mul	w2,w2,w3
	subs	x4,x4,1
	b.ne	.l
.e:	eor	w0,w2,w1
	ret

; ---------------------------------------------------------------------------
;  Tables et textes (decalages : pas d'adresse absolue dans un kit)
; ---------------------------------------------------------------------------
	align	4
unit_tab:	dw 1, 635, 360, 12700, 914400, 360000, 36000, 152400, 9525
rel_attrs:	dw s_Id - rel_attrs, s_Type - rel_attrs, s_Target - rel_attrs, s_TargetMode - rel_attrs
unit_suffix:	db 0,0,0,0, 0,0,0,0, 0,0,0,0, 'pt',0,0, 'in',0,0, 'cm',0,0, 'mm',0,0, 'pc',0,0, 'px',0,0
csv_seps:	db ',', ';', 9, 0
cp1252_tab:	dh 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021
		dh 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F
		dh 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014
		dh 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
s_hex_up:	db '0123456789ABCDEF'
s_hex_low:	db '0123456789abcdef'
s_mimetype:	db 'mimetype'
s_cdata:	db '![CDATA[',0
s_template:	db '-template'
s_ct_name:	db '[Content_Types].xml'
s_amp:		db '&amp;',0
s_lt:		db '&lt;',0
s_gt:		db '&gt;',0
s_quot:		db '&quot;',0
s_val:		db 'val',0
s_Relationship:	db 'Relationship',0
s_Id:		db 'Id',0
s_Type:		db 'Type',0
s_Target:	db 'Target',0
s_TargetMode:	db 'TargetMode',0
s_rels_dir:	db '_rels/',0
s_rels_ext:	db '.rels',0
s_rel_ns:	db 'http://schemas.openxmlformats.org/officeDocument/2006/relationships/',0
s_rw1:		db '<Relationship Id="rId',0
s_rw2:		db '" Type="',0
s_rw3:		db '" Target="',0
s_rw4:		db '" TargetMode="External',0
s_rw5:		db '"/>',0
s_od1:		db '<Default Extension="',0
s_od2:		db '" ContentType="',0
s_ov1:		db '<Override PartName="',0
s_me1:		db '<manifest:file-entry manifest:full-path="',0
s_me2:		db '" manifest:version="1.3',0
s_me3:		db '" manifest:media-type="',0

	align	4
mime_tab:	dw 0
		dw m_docx - mime_tab, m_xlsx - mime_tab, m_pptx - mime_tab
		dw m_odt - mime_tab, m_ods - mime_tab, m_odp - mime_tab
env_tab:	dw e_decl - env_tab, e_rels - env_tab, e_relsc - env_tab, e_types - env_tab
		dw e_typesc - env_tab, e_man - env_tab, e_manc - env_tab
m_docx:		db 'application/vnd.openxmlformats-officedocument.wordprocessingml.document',0
m_xlsx:		db 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',0
m_pptx:		db 'application/vnd.openxmlformats-officedocument.presentationml.presentation',0
m_odt:		db 'application/vnd.oasis.opendocument.text',0
m_ods:		db 'application/vnd.oasis.opendocument.spreadsheet',0
m_odp:		db 'application/vnd.oasis.opendocument.presentation',0
e_decl:		db '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',10,0
e_rels:		db '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">',0
e_relsc:	db '</Relationships>',0
e_types:	db '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
		db '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
		db '<Default Extension="xml" ContentType="application/xml"/>',0
e_typesc:	db '</Types>',0
e_man:		db '<?xml version="1.0" encoding="UTF-8"?>',10
		db '<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.3">',0
e_manc:		db '</manifest:manifest>',0

segment readable writeable
		align 8
dk_slots	rb DK_SLOTS*SLOT_SIZE
