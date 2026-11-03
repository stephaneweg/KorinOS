; ============================================================================
;  kits/uikit_lex.inc - UIKit : les couleurs de C# et de XML du texte
;  multiligne (UTA_CS, UTA_XML), un morceau a la fois.
;
;  Un commentaire, une chaine ou une balise peut tenir sur plusieurs lignes :
;  l'etat au debut de la premiere ligne visible se calcule depuis le debut
;  du texte (uk_lex_start), puis suit les lignes dessinees (UW_TA_LEX).
;
;  C#  : 0, 1 dans un commentaire, 2 dans une chaine @, 3 dans trois guillemets
;  XML : 0 texte, 1 dans une balise, 2 dans un commentaire, 3 dans " ", 4 dans ' '
;
;  Registres de l'analyse : x9 texte, x10 position, x11 fin, x12 etat,
;  x14 genre du morceau.
; ============================================================================

; x0 = widget : UW_TA_LEX au debut de la premiere ligne visible
uk_lex_start:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,UW_STYLE]
	tst	x0,UTA_CS or UTA_XML
	b.eq	.out
	mov	x0,x19
	ldr	x1,[x19,UW_POS]
	bl	uk_ta_line
	mov	x20,x0				; debut de la ligne
	mov	x21,0				; position
	mov	x22,0				; etat
.scan:	cmp	x21,x20
	b.hs	.done
	ldr	x0,[x19,UW_TEXT]
	mov	x1,x21
	ldr	x2,[x19,UW_TLEN]
	mov	x3,x22
	ldr	x4,[x19,UW_STYLE]
	bl	uk_lex
	mov	x21,x0
	mov	x22,x2
	b	.scan
.done:	str	x22,[x19,UW_TA_LEX]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = position (< x2), x2 = fin, x3 = etat, x4 = style -> x0 =
; fin du morceau, x1 = UKC_* (-1 : blanc), x2 = etat. Un morceau s'arrete
; toujours a la fin de la ligne. Ne touche que x0..x7, x9..x14.
uk_lex:
	prologue
	mov	x9,x0
	mov	x10,x1
	mov	x11,x2
	mov	x12,x3
	tbnz	x4,13,.xml
	bl	uk_lex_cs
	b	.out
.xml:	bl	uk_lex_xml
.out:	mov	x0,x10
	mov	x1,x14
	mov	x2,x12
	epilogue

; x0 = decalage depuis la position -> w0 = l'octet, 0 apres la fin (feuille)
uk_lex_peek:
	add	x0,x10,x0
	cmp	x0,x11
	b.hs	.no
	ldrb	w0,[x9,x0]
	ret
.no:	mov	w0,0
	ret

; w0 = octet -> x0 = 1 s'il fait partie d'un nom (feuille)
uk_lex_name:
	cmp	w0,0x80
	b.hs	.y
	sub	w1,w0,'0'
	cmp	w1,10
	b.lo	.y
	orr	w1,w0,0x20
	sub	w1,w1,'a'
	cmp	w1,26
	b.lo	.y
	cmp	w0,'_'
	b.eq	.y
	mov	x0,0
	ret
.y:	mov	x0,1
	ret

; position : un de plus, puis jusqu'a la fin d'un nom
uk_lex_rest:
	prologue
.l:	add	x10,x10,1
	cmp	x10,x11
	b.hs	.out
	ldrb	w0,[x9,x10]
	bl	uk_lex_name
	cbnz	x0,.l
.out:	epilogue

; position : jusqu'a la fin de la ligne (feuille)
uk_lex_eol:
.l:	cmp	x10,x11
	b.hs	.out
	ldrb	w0,[x9,x10]
	cmp	w0,10
	b.eq	.out
	add	x10,x10,1
	b	.l
.out:	ret

; -- C# ----------------------------------------------------------------------

uk_lex_cs:
	prologue
	ldrb	w5,[x9,x10]
	cmp	w5,10
	b.eq	.blank
	cbnz	x12,.long
	cmp	w5,' '
	b.eq	.blank
	cmp	w5,9
	b.eq	.blank
	cmp	w5,13
	b.eq	.blank
	mov	x0,1
	bl	uk_lex_peek
	mov	w6,w0				; le suivant
	cmp	w5,'/'
	b.ne	.pre
	cmp	w6,'/'
	b.eq	.line
	cmp	w6,'*'
	b.ne	.punct
	add	x10,x10,2
	mov	x12,1
	b	.long
.line:	bl	uk_lex_eol
	mov	x14,UKC_COMMENT
	b	.out
.pre:	cmp	w5,'#'				; #region... : seul en tete de ligne
	b.ne	.at
	mov	x6,x10
.back:	cbz	x6,.prep
	sub	x6,x6,1
	ldrb	w0,[x9,x6]
	cmp	w0,10
	b.eq	.prep
	cmp	w0,' '
	b.eq	.back
	cmp	w0,9
	b.eq	.back
	b	.punct
.prep:	bl	uk_lex_eol
	mov	x14,UKC_LABEL
	b	.out
.at:	cmp	w5,'@'
	b.eq	.prefix
	cmp	w5,'$'
	b.ne	.quote
.prefix:mov	x7,1				; @" $" $@" @$"
	mov	w4,w5
	cmp	w6,'@'
	b.eq	.pref2
	cmp	w6,'$'
	b.ne	.pref3
.pref2:	orr	w4,w4,w6			; (@ | $ = 0x64, @ seul 0x40, $ seul 0x24)
	mov	x7,2
.pref3:	mov	x0,x7
	bl	uk_lex_peek
	cmp	w0,'"'
	b.ne	.noq
	tbnz	x4,6,.verb
	add	x10,x10,x7
	mov	w5,'"'
	b	.str
.verb:	add	x7,x7,1
	add	x10,x10,x7
	mov	x12,2
	b	.long
.noq:	cmp	w5,'@'				; @mot
	b.ne	.punct
	bl	uk_lex_rest
	mov	x14,UKC_WORD
	b	.out
.quote:	cmp	w5,0x27
	b.eq	.str
	cmp	w5,'"'
	b.ne	.num
	cmp	w6,'"'
	b.ne	.str
	mov	x0,2
	bl	uk_lex_peek
	cmp	w0,'"'
	b.ne	.str
	add	x10,x10,3
	mov	x12,3
	b	.long
.str:	add	x10,x10,1			; chaine : \ passe le suivant
.sc:	cmp	x10,x11
	b.hs	.sd
	ldrb	w0,[x9,x10]
	cmp	w0,10
	b.eq	.sd
	add	x10,x10,1
	cmp	w0,0x5C
	b.eq	.esc
	cmp	w0,w5
	b.ne	.sc
	b	.sd
.esc:	cmp	x10,x11
	b.hs	.sd
	ldrb	w0,[x9,x10]
	cmp	w0,10
	b.eq	.sd
	add	x10,x10,1
	b	.sc
.sd:	mov	x14,UKC_STRING
	b	.out
.num:	sub	w0,w5,'0'
	cmp	w0,10
	b.hs	.word
.nc:	add	x10,x10,1
	cmp	x10,x11
	b.hs	.nd
	ldrb	w0,[x9,x10]
	cmp	w0,'.'
	b.eq	.nc
	bl	uk_lex_name
	cbnz	x0,.nc
.nd:	mov	x14,UKC_NUMBER
	b	.out
.word:	mov	w0,w5
	bl	uk_lex_name
	cbz	x0,.punct
	bl	uk_lex_rest
	mov	x14,UKC_WORD
	b	.out
.punct:	add	x10,x10,1
	mov	x14,UKC_PUNCT
	b	.out
.blank:	add	x10,x10,1
	mov	x14,-1
	b	.out
.long:	mov	x14,UKC_STRING			; un morceau long, jusqu'a sa fin ou la
	cmp	x12,1				; fin de la ligne
	b.ne	.lc
	mov	x14,UKC_COMMENT
.lc:	cmp	x10,x11
	b.hs	.out
	ldrb	w5,[x9,x10]
	cmp	w5,10
	b.eq	.out
	mov	x0,1
	bl	uk_lex_peek
	mov	w6,w0
	cmp	x12,1
	b.ne	.lq
	cmp	w5,'*'
	b.ne	.ln
	cmp	w6,'/'
	b.ne	.ln
	add	x10,x10,2
	b	.closed
.lq:	cmp	w5,'"'
	b.ne	.ln
	cmp	x12,2
	b.ne	.l3
	cmp	w6,'"'				; @"" : un guillemet
	b.ne	.l1
	add	x10,x10,2
	b	.lc
.l1:	add	x10,x10,1
	b	.closed
.l3:	cmp	w6,'"'
	b.ne	.ln
	mov	x0,2
	bl	uk_lex_peek
	cmp	w0,'"'
	b.ne	.ln
	add	x10,x10,3
	b	.closed
.ln:	add	x10,x10,1
	b	.lc
.closed:mov	x12,0
.out:	epilogue

; x0 = mot, x1 = longueur -> x0 = 1 : mot-cle de C#, avec la casse (feuille)
uk_cs_word:
	adr	x2,uk_cs_words
.w:	ldrb	w3,[x2]
	cbz	w3,.no
	mov	x4,0
.c:	ldrb	w3,[x2,x4]
	cbz	w3,.end
	cmp	x4,x1
	b.hs	.skip
	ldrb	w5,[x0,x4]
	cmp	w5,w3
	b.ne	.skip
	add	x4,x4,1
	b	.c
.end:	cmp	x4,x1
	b.eq	.yes
.skip:	ldrb	w3,[x2],1
	cbnz	w3,.skip
	b	.w
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

uk_cs_words	db 'abstract',0,'as',0,'async',0,'await',0,'base',0,'bool',0,'break',0
		db 'byte',0,'case',0,'catch',0,'char',0,'checked',0,'class',0,'const',0
		db 'continue',0,'decimal',0,'default',0,'delegate',0,'do',0,'double',0
		db 'dynamic',0,'else',0,'enum',0,'event',0,'explicit',0,'extern',0
		db 'false',0,'finally',0,'fixed',0,'float',0,'for',0,'foreach',0,'get',0
		db 'global',0,'goto',0,'if',0,'implicit',0,'in',0,'init',0,'int',0
		db 'interface',0,'internal',0,'is',0,'lock',0,'long',0,'namespace',0
		db 'new',0,'nint',0,'not',0,'nuint',0,'null',0,'object',0,'operator',0
		db 'or',0,'out',0,'override',0,'params',0,'partial',0,'private',0
		db 'protected',0,'public',0,'readonly',0,'record',0,'ref',0,'required',0
		db 'return',0,'sbyte',0,'sealed',0,'set',0,'short',0,'sizeof',0
		db 'stackalloc',0,'static',0,'string',0,'struct',0,'switch',0,'this',0
		db 'throw',0,'true',0,'try',0,'typeof',0,'uint',0,'ulong',0,'unchecked',0
		db 'unsafe',0,'ushort',0,'using',0,'value',0,'var',0,'virtual',0,'void',0
		db 'volatile',0,'when',0,'where',0,'while',0,'with',0,'yield',0,0
		align 4

; -- XML ---------------------------------------------------------------------

uk_lex_xml:
	prologue
	ldrb	w5,[x9,x10]
	cmp	w5,10
	b.eq	.blank
	cmp	x12,2
	b.eq	.cmnt
	b.hi	.value
	cbnz	x12,.tag
	cmp	w5,' '				; texte
	b.eq	.blank
	cmp	w5,9
	b.eq	.blank
	cmp	w5,13
	b.eq	.blank
	cmp	w5,'<'
	b.ne	.amp
	mov	x0,1
	bl	uk_lex_peek
	mov	w6,w0
	cmp	w6,'!'
	b.ne	.open
	mov	x0,2
	bl	uk_lex_peek
	cmp	w0,'-'
	b.ne	.open
	mov	x0,3
	bl	uk_lex_peek
	cmp	w0,'-'
	b.ne	.open
	add	x10,x10,4
	mov	x12,2
	b	.cmnt
.open:	cmp	w6,'/'				; <nom </nom <?nom <!nom
	b.eq	.op1
	cmp	w6,'?'
	b.eq	.op1
	cmp	w6,'!'
	b.ne	.op2
.op1:	add	x10,x10,1
.op2:	bl	uk_lex_tagname
	mov	x12,1
	mov	x14,UKC_KEYWORD
	b	.out
.amp:	cmp	w5,'&'
	b.ne	.text
.ac:	add	x10,x10,1			; &entite;
	cmp	x10,x11
	b.hs	.ad
	ldrb	w0,[x9,x10]
	cmp	w0,';'
	b.eq	.ae
	cmp	w0,'<'
	b.eq	.ad
	cmp	w0,' '
	b.ls	.ad
	b	.ac
.ae:	add	x10,x10,1
.ad:	mov	x14,UKC_NUMBER
	b	.out
.text:	add	x10,x10,1
	cmp	x10,x11
	b.hs	.td
	ldrb	w0,[x9,x10]
	cmp	w0,10
	b.eq	.td
	cmp	w0,'<'
	b.eq	.td
	cmp	w0,'&'
	b.ne	.text
.td:	mov	x14,UKC_TEXT
	b	.out
.tag:	cmp	w5,' '				; dans une balise
	b.eq	.blank
	cmp	w5,9
	b.eq	.blank
	cmp	w5,13
	b.eq	.blank
	cmp	w5,'>'
	b.eq	.close1
	cmp	w5,'/'
	b.eq	.close2
	cmp	w5,'?'
	b.eq	.close2
	cmp	w5,'"'
	b.eq	.dq
	cmp	w5,0x27
	b.eq	.sq
	mov	w0,w5
	bl	uk_lex_tagch
	cbz	x0,.punct
	bl	uk_lex_tagname0
	mov	x14,UKC_REGISTER
	b	.out
.close2:mov	x0,1
	bl	uk_lex_peek
	cmp	w0,'>'
	b.ne	.punct
	add	x10,x10,1
.close1:add	x10,x10,1
	mov	x12,0
	mov	x14,UKC_PUNCT
	b	.out
.dq:	mov	x12,3
	add	x10,x10,1
	b	.val
.sq:	mov	x12,4
	add	x10,x10,1
	b	.val
.value:
.val:	mov	w5,'"'				; valeur d'un attribut
	cmp	x12,3
	b.eq	.vc
	mov	w5,0x27
.vc:	cmp	x10,x11
	b.hs	.vd
	ldrb	w0,[x9,x10]
	cmp	w0,10
	b.eq	.vd
	add	x10,x10,1
	cmp	w0,w5
	b.ne	.vc
	mov	x12,1
.vd:	mov	x14,UKC_STRING
	b	.out
.cmnt:	cmp	x10,x11				; jusqu'a -->
	b.hs	.cd
	ldrb	w5,[x9,x10]
	cmp	w5,10
	b.eq	.cd
	cmp	w5,'-'
	b.ne	.cn
	mov	x0,1
	bl	uk_lex_peek
	cmp	w0,'-'
	b.ne	.cn
	mov	x0,2
	bl	uk_lex_peek
	cmp	w0,'>'
	b.ne	.cn
	add	x10,x10,3
	mov	x12,0
	b	.cd
.cn:	add	x10,x10,1
	b	.cmnt
.cd:	mov	x14,UKC_COMMENT
	b	.out
.punct:	add	x10,x10,1
	mov	x14,UKC_PUNCT
	b	.out
.blank:	add	x10,x10,1
	mov	x14,-1
.out:	epilogue

; w0 = octet -> x0 = 1 s'il fait partie d'un nom XML (: - . en plus)
uk_lex_tagch:
	cmp	w0,':'
	b.eq	.y
	cmp	w0,'-'
	b.eq	.y
	cmp	w0,'.'
	b.eq	.y
	b	uk_lex_name
.y:	mov	x0,1
	ret

; position sur le < (ou le / qui le suit) : jusqu'a la fin du nom
uk_lex_tagname:
	add	x10,x10,1
uk_lex_tagname0:
	prologue
.l:	cmp	x10,x11
	b.hs	.out
	ldrb	w0,[x9,x10]
	bl	uk_lex_tagch
	cbz	x0,.out
	add	x10,x10,1
	b	.l
.out:	epilogue
