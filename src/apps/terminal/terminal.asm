; terminal - console graphique et interpreteur de commandes d'ARMPI
;
; Tampon de sortie circulaire (TERM_MAXL lignes), ligne de saisie avec
; historique, commandes integrees, et lancement des programmes de SD0:/apps
; avec leur sortie redirigee dans un tube (SYS_SPAWN_PIPE) lue en continu.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W		= 760
WIN_H		= 480
PAD		= 6
LH		= 18				; hauteur de ligne
FONT_PX		= 15
ROWS		= (WIN_H - 2*PAD) / LH
TERM_MAXL	= 400				; lignes conservees
LINE_SZ		= 256
IN_MAX		= 240
HIST_N		= 16

COL_BG		= 0x001C2026
COL_TXT		= 0x00D8DEE6
COL_PROMPT	= 0x007FC1FF
COL_CURSOR	= 0x00F0C674

macro tputs lbl* {
	adr	x0,lbl
	bl	lang_tr
	bl	term_puts
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	adr	x0,ui_win				; variables de l'interface
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,s_title
	mov	x5,WF_GLYPH_LIGHT or WF_BODY_DARK
	bl	ui_window_ex
	tbnz	x0,63,quit
	adr	x0,s_m100			; avance de la police mono (x100)
	mov	x1,FONT_PX
	adr	x2,ui_font_m
	bl	ui_text_width
	cmp	x0,0
	mov	x1,900
	csel	x0,x1,x0,le
	adr	x1,adv100
	str	x0,[x1]
	movl	x1,(WIN_W-2*PAD)*100		; colonnes
	udiv	x1,x1,x0
	adr	x2,cols
	str	x1,[x2]
	adr	x0,nlines
	mov	x1,1
	str	x1,[x0]
	bl	update_cwd
	tputs	s_banner
	bl	term_draw

main_loop:
	adr	x0,run_pid
	ldr	x0,[x0]
	mov	x1,1000
	cbz	x0,.wait
	bl	poll_child
	mov	x1,15
.wait:	mov	x0,x1
	bl	ui_event
	cbz	x0,main_loop
	cmp	x0,WEV_CLOSE
	b.eq	quit
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_WHEEL
	b.ne	main_loop
	adr	x0,ui_ev			; molette : 3 lignes par cran
	ldrsw	x1,[x0,EV_Y]
	mov	x2,3
	mul	x1,x1,x2
	bl	scroll_by
	bl	term_draw
	b	main_loop
.key:	bl	on_key
	b	main_loop

quit:	mov	x0,0
	sys	SYS_EXIT

; x1 = deplacement (+ = vers le haut)
scroll_by:
	adr	x2,scroll
	ldr	x3,[x2]
	add	x3,x3,x1
	cmp	x3,0
	csel	x3,xzr,x3,lt
	adr	x4,nlines			; au plus ce qui est conserve
	ldr	x4,[x4]
	mov	x5,TERM_MAXL
	cmp	x4,x5
	csel	x4,x5,x4,hi
	sub	x4,x4,ROWS-1
	cmp	x4,0
	csel	x4,xzr,x4,lt
	cmp	x3,x4
	csel	x3,x4,x3,gt
	str	x3,[x2]
	ret

; ---------------------------------------------------------------------------
;  Clavier
; ---------------------------------------------------------------------------
on_key:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.out				; relache
	ldr	w19,[x0,EV_CODE]
	ldr	w20,[x0,EV_CHAR]
	ldr	w2,[x0,EV_MODS]
	mov	w3,0x11
	tst	w2,w3
	b.eq	.noctrl
	cmp	w19,0x06			; Ctrl+C : tuer le programme
	b.ne	.out
	adr	x0,run_pid
	ldr	x0,[x0]
	cbz	x0,.out
	sys	SYS_KILL
	tputs	s_ctrlc
	bl	term_draw
	b	.out
.noctrl:
	cmp	w19,KEY_PGUP
	b.ne	.pgdn
	mov	x1,ROWS-2
	bl	scroll_by
	b	.redraw
.pgdn:	cmp	w19,KEY_PGDN
	b.ne	.run
	mov	x1,-(ROWS-2)
	bl	scroll_by
	b	.redraw
.run:	adr	x0,run_pid			; programme en cours : pas de saisie
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,scroll			; toute frappe ramene en bas
	str	xzr,[x0]
	cmp	w19,KEY_ENTER
	b.eq	.enter
	cmp	w19,KEY_BACKSPACE
	b.eq	.bs
	cmp	w19,KEY_DELETE
	b.eq	.del
	cmp	w19,KEY_LEFT
	b.eq	.left
	cmp	w19,KEY_RIGHT
	b.eq	.right
	cmp	w19,KEY_HOME
	b.eq	.home
	cmp	w19,KEY_END
	b.eq	.end
	cmp	w19,KEY_UP
	b.eq	.up
	cmp	w19,KEY_DOWN
	b.eq	.down
	cmp	w20,0x20
	b.lo	.out
	; insertion du caractere (UTF-8) a la position du curseur
	sub	sp,sp,16
	mov	w0,w20
	mov	x1,sp
	bl	utf8_put
	mov	x19,x0				; octets
	adr	x1,inlen
	ldr	x2,[x1]
	add	x3,x2,x19
	cmp	x3,IN_MAX
	b.hi	.ins_done
	str	x3,[x1]
	adr	x4,incur
	ldr	x5,[x4]
	adr	x6,inbuf
	add	x1,x6,x5			; decaler la fin
	add	x0,x1,x19
	sub	x2,x2,x5
	add	x2,x2,1				; avec le 0 final
	stp	x5,x6,[sp,-16]!
	bl	mem_move
	ldp	x5,x6,[sp],16
	add	x0,x6,x5
	mov	x1,sp
	mov	x2,x19
	bl	mem_move
	adr	x4,incur
	ldr	x5,[x4]
	add	x5,x5,x19
	str	x5,[x4]
.ins_done:
	add	sp,sp,16
	b	.redraw
.bs:	adr	x4,incur
	ldr	x5,[x4]
	cbz	x5,.out
	adr	x6,inbuf
	mov	x7,x5
.bsb:	sub	x7,x7,1				; debut du caractere precedent
	cbz	x7,.bsd
	ldrb	w8,[x6,x7]
	and	w8,w8,0xC0
	cmp	w8,0x80
	b.eq	.bsb
.bsd:	str	x7,[x4]
	b	.cut				; supprimer [x7, x5[
.del:	adr	x4,incur
	ldr	x7,[x4]
	adr	x6,inbuf
	ldrb	w8,[x6,x7]
	cbz	w8,.out
	add	x5,x7,1
.delf:	ldrb	w8,[x6,x5]
	and	w8,w8,0xC0
	cmp	w8,0x80
	b.ne	.cut
	add	x5,x5,1
	b	.delf
.cut:	; supprime inbuf[x7, x5[
	adr	x1,inlen
	ldr	x2,[x1]
	sub	x3,x5,x7
	sub	x9,x2,x3
	str	x9,[x1]
	add	x0,x6,x7
	add	x1,x6,x5
	sub	x2,x2,x5
	add	x2,x2,1
	bl	mem_move
	b	.redraw
.left:	adr	x4,incur
	ldr	x5,[x4]
	cbz	x5,.out
	adr	x6,inbuf
.lb:	sub	x5,x5,1
	cbz	x5,.ls
	ldrb	w8,[x6,x5]
	and	w8,w8,0xC0
	cmp	w8,0x80
	b.eq	.lb
.ls:	str	x5,[x4]
	b	.redraw
.right:	adr	x4,incur
	ldr	x5,[x4]
	adr	x6,inbuf
	ldrb	w8,[x6,x5]
	cbz	w8,.out
.rb:	add	x5,x5,1
	ldrb	w8,[x6,x5]
	and	w8,w8,0xC0
	cmp	w8,0x80
	b.eq	.rb
	str	x5,[x4]
	b	.redraw
.home:	adr	x4,incur
	str	xzr,[x4]
	b	.redraw
.end:	adr	x1,inlen
	ldr	x2,[x1]
	adr	x4,incur
	str	x2,[x4]
	b	.redraw
.up:	adr	x0,hist_pos
	ldr	x1,[x0]
	adr	x2,hist_n
	ldr	x2,[x2]
	cmp	x1,x2
	b.hs	.out
	add	x1,x1,1
	str	x1,[x0]
	bl	hist_load
	b	.redraw
.down:	adr	x0,hist_pos
	ldr	x1,[x0]
	cbz	x1,.out
	sub	x1,x1,1
	str	x1,[x0]
	bl	hist_load
	b	.redraw
.enter:	bl	execute
.redraw:
	bl	term_draw
.out:	ldp	x19,x20,[sp],16
	epilogue

; charge l'entree d'historique hist_pos (1 = la plus recente, 0 = ligne vide)
hist_load:
	prologue
	adr	x0,hist_pos
	ldr	x1,[x0]
	adr	x0,inbuf
	strb	wzr,[x0]
	cbz	x1,.len
	adr	x2,hist_n
	ldr	x2,[x2]
	sub	x1,x2,x1			; index absolu
	and	x1,x1,HIST_N-1
	adr	x2,hist
	add	x1,x2,x1,lsl 8
	bl	str_cat
.len:	adr	x0,inbuf
	bl	str_len
	adr	x1,inlen
	str	x0,[x1]
	adr	x1,incur
	str	x0,[x1]
	epilogue

; ---------------------------------------------------------------------------
;  Tampon de sortie
; ---------------------------------------------------------------------------

; x0 = numero de ligne -> x0 = adresse de la ligne dans l'anneau
line_ptr:
	mov	x1,TERM_MAXL
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	adr	x1,lines
	mov	x2,LINE_SZ
	madd	x0,x0,x2,x1
	ret

term_newline:
	prologue
	adr	x1,nlines
	ldr	x0,[x1]
	add	x0,x0,1
	str	x0,[x1]
	sub	x0,x0,1
	bl	line_ptr
	strb	wzr,[x0]
	adr	x1,curlen
	stp	xzr,xzr,[x1]			; curlen, curcol
	epilogue

; w0 = octet
term_putb:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,w0
	cmp	w19,10
	b.ne	.notnl
	bl	term_newline
	b	.out
.notnl:	cmp	w19,13
	b.eq	.out
	cmp	w19,9
	b.ne	.byte
.tab:	mov	w0,' '				; tabulation : espaces jusqu'a 8
	bl	term_putb
	adr	x1,curcol
	ldr	x1,[x1]
	tst	x1,7
	b.ne	.tab
	b	.out
.byte:	and	w0,w19,0xC0
	cmp	w0,0x80
	b.eq	.store				; octet de continuation : meme colonne
	adr	x1,curcol
	ldr	x2,[x1]
	adr	x3,cols
	ldr	x3,[x3]
	cmp	x2,x3
	b.lo	.col
	bl	term_newline			; retour a la ligne automatique
	adr	x1,curcol
	ldr	x2,[x1]
.col:	add	x2,x2,1
	str	x2,[x1]
.store:	adr	x1,curlen
	ldr	x20,[x1]
	cmp	x20,LINE_SZ-4
	b.hs	.out
	adr	x1,nlines
	ldr	x0,[x1]
	sub	x0,x0,1
	bl	line_ptr
	strb	w19,[x0,x20]
	add	x20,x20,1
	strb	wzr,[x0,x20]
	adr	x1,curlen
	str	x20,[x1]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = donnees, x1 = octets
term_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
.c:	cbz	x20,.out
	ldrb	w0,[x19],1
	bl	term_putb
	sub	x20,x20,1
	b	.c
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine
term_puts:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
.c:	ldrb	w0,[x19],1
	cbz	w0,.out
	bl	term_putb
	b	.c
.out:	ldr	x19,[sp],16
	epilogue

; x0 = valeur signee
term_dec:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	tbz	x0,63,.pos
	mov	w2,'-'
	strb	w2,[x1],1
	neg	x0,x0
.pos:	bl	fmt_dec
	mov	x0,sp
	bl	term_puts
	add	sp,sp,32
	epilogue

; passe a la ligne si la ligne courante n'est pas vide
term_ensure_nl:
	adr	x0,curlen
	ldr	x0,[x0]
	cbz	x0,.ret
	b	term_newline
.ret:	ret

; x0 = code d'erreur : "erreur N"
term_err:
	prologue
	str	x0,[sp,-16]!
	tputs	s_err
	ldr	x0,[sp],16
	bl	term_dec
	mov	w0,10
	bl	term_putb
	epilogue

; ---------------------------------------------------------------------------
;  Affichage
; ---------------------------------------------------------------------------
term_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x0,0
	mov	x1,0
	mov	x2,WIN_W
	mov	x3,WIN_H
	movl	w4,COL_BG
	bl	ui_fill
	; lignes de sortie affichables + ligne d'invite si inactif
	adr	x0,nlines
	ldr	x19,[x0]			; lignes de sortie
	adr	x0,curlen
	ldr	x0,[x0]
	adr	x1,run_pid
	ldr	x1,[x1]
	cbnz	x1,.busy
	cbnz	x0,.idle_partial
	sub	x19,x19,1			; derniere ligne vide : l'invite la remplace
.idle_partial:
	add	x20,x19,1			; lignes virtuelles (sortie + invite)
	b	.first
.busy:	mov	x20,x19
.first:	adr	x0,scroll
	ldr	x0,[x0]
	sub	x21,x20,ROWS
	sub	x21,x21,x0			; premiere ligne visible
	adr	x0,nlines
	ldr	x0,[x0]
	sub	x0,x0,TERM_MAXL
	sub	x0,x0,1
	cmp	x21,x0
	csel	x21,x0,x21,lt
	cmp	x21,0
	csel	x21,xzr,x21,lt
	mov	x22,0				; rang d'ecran
.row:	cmp	x22,ROWS
	b.hs	.done
	add	x23,x21,x22			; ligne virtuelle
	mov	x24,LH				; ligne de base
	madd	x24,x22,x24,x24
	add	x24,x24,PAD-4
	cmp	x23,x19
	b.hs	.prompt
	mov	x0,x23
	bl	line_ptr
	mov	x1,PAD
	mov	x2,x24
	movl	w3,COL_TXT
	mov	x4,FONT_PX
	adr	x5,ui_font_m
	mov	x6,0
	bl	ui_text
	b	.next
.prompt:
	adr	x0,run_pid
	ldr	x0,[x0]
	cbnz	x0,.done
	cmp	x23,x19
	b.ne	.done
	bl	draw_prompt_line		; x24 = ligne de base
	b	.done
.next:	add	x22,x22,1
	b	.row
.done:	bl	ui_update_all
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x24 = ligne de base (appele depuis term_draw) : invite, saisie, curseur
draw_prompt_line:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,272
	mov	x0,sp				; invite = "chemin> "
	adr	x1,cwd
	bl	str_cat
	adr	x1,s_gt
	bl	str_cat
	mov	x0,sp
	mov	x1,PAD
	mov	x2,x24
	movl	w3,COL_PROMPT
	mov	x4,FONT_PX
	adr	x5,ui_font_m
	mov	x6,0
	bl	ui_text
	mov	x0,sp				; colonnes de l'invite
	bl	count_cols
	mov	x19,x0
	adr	x0,inbuf
	mov	x1,PAD
	adr	x2,adv100
	ldr	x2,[x2]
	mul	x3,x19,x2
	mov	x4,100
	udiv	x3,x3,x4
	add	x1,x1,x3
	mov	x20,x1
	mov	x2,x24
	movl	w3,COL_TXT
	mov	x4,FONT_PX
	adr	x5,ui_font_m
	mov	x6,0
	bl	ui_text
	; curseur : colonne = invite + caracteres avant incur
	adr	x0,inbuf
	adr	x1,incur
	ldr	x1,[x1]
	mov	x2,0
	mov	x3,0
.cc:	cmp	x2,x1
	b.hs	.ccd
	ldrb	w4,[x0,x2]
	and	w4,w4,0xC0
	cmp	w4,0x80
	cinc	x3,x3,ne
	add	x2,x2,1
	b	.cc
.ccd:	add	x3,x3,x19
	adr	x2,adv100
	ldr	x2,[x2]
	mul	x3,x3,x2
	mov	x4,100
	udiv	x3,x3,x4
	add	x0,x3,PAD
	sub	x1,x24,13
	mov	x2,2
	mov	x3,LH-2
	movl	w4,COL_CURSOR
	bl	ui_fill
	add	sp,sp,272
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine UTF-8 -> x0 = nombre de caracteres
count_cols:
	mov	x1,0
.c:	ldrb	w2,[x0],1
	cbz	w2,.e
	and	w2,w2,0xC0
	cmp	w2,0x80
	cinc	x1,x1,ne
	b	.c
.e:	mov	x0,x1
	ret

update_cwd:
	adr	x0,cwd
	mov	x1,256
	sys	SYS_GETCWD
	ret

; ---------------------------------------------------------------------------
;  Programme en cours : lecture du tube
; ---------------------------------------------------------------------------
poll_child:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,0				; quelque chose a afficher ?
	mov	x19,16				; lectures max par passage
.rd:	cbz	x19,.out
	sub	x19,x19,1
	adr	x0,run_fd
	ldr	x0,[x0]
	adr	x1,iobuf
	mov	x2,512
	sys	SYS_READ
	cmn	x0,11				; E_AGAIN : rien pour l'instant
	b.eq	.out
	cmp	x0,0
	b.le	.eof
	mov	x1,x0
	adr	x0,iobuf
	bl	term_write
	mov	x20,1
	b	.rd
.eof:	adr	x0,run_fd			; fin : fermer, attendre, code
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,run_pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	mov	x19,x0
	adr	x0,run_pid
	str	xzr,[x0]
	bl	term_ensure_nl
	cbz	x19,.done
	tputs	s_code
	mov	x0,x19
	bl	term_dec
	tputs	s_close
.done:	bl	update_cwd
	mov	x20,1
.out:	cbz	x20,.ret
	bl	term_draw
.ret:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Execution d'une ligne de commande
; ---------------------------------------------------------------------------
execute:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	; echo de la ligne
	adr	x0,cwd
	bl	term_puts
	tputs	s_gt
	tputs	inbuf
	mov	w0,10
	bl	term_putb
	; historique
	adr	x0,inlen
	ldr	x0,[x0]
	cbz	x0,.nohist
	adr	x0,hist_n
	ldr	x1,[x0]
	add	x2,x1,1
	str	x2,[x0]
	and	x1,x1,HIST_N-1
	adr	x0,hist
	add	x0,x0,x1,lsl 8
	adr	x1,inbuf
	bl	str_cat
.nohist:
	adr	x0,hist_pos
	str	xzr,[x0]
	; decoupe : commande (cmdbuf) + arguments (args, sans espaces en bordure)
	adr	x19,inbuf
.sk:	ldrb	w0,[x19]
	cmp	w0,' '
	b.ne	.cmd
	add	x19,x19,1
	b	.sk
.cmd:	adr	x20,cmdbuf
	mov	x2,0
.cw:	ldrb	w0,[x19]
	cbz	w0,.cwe
	cmp	w0,' '
	b.eq	.cwe
	cmp	x2,62
	b.hs	.cwn
	strb	w0,[x20,x2]
	add	x2,x2,1
.cwn:	add	x19,x19,1
	b	.cw
.cwe:	strb	wzr,[x20,x2]
.sa:	ldrb	w0,[x19]
	cmp	w0,' '
	b.ne	.argc
	add	x19,x19,1
	b	.sa
.argc:	adr	x0,args
	mov	x1,x19
	bl	str_cat
	adr	x0,args				; retirer espaces et caracteres
	bl	str_len				; invisibles finaux (le clavier du
	adr	x1,args				; PocketTerm en laisse un)
.tr:	cbz	x0,.trd
	sub	x2,x0,1
	ldrb	w3,[x1,x2]
	cmp	w3,' '
	b.hi	.trd
	strb	wzr,[x1,x2]
	mov	x0,x2
	b	.tr
.trd:	adr	x0,inbuf			; vider la saisie
	strb	wzr,[x0]
	adr	x0,inlen
	str	xzr,[x0]
	adr	x0,incur
	str	xzr,[x0]
	adr	x0,cmdbuf
	ldrb	w0,[x0]
	cbz	w0,.out
	; commandes integrees
	adr	x21,builtins
.bi:	ldr	x0,[x21]
	cbz	x0,.program
	adr	x1,cmdbuf
	bl	str_eq
	cbnz	x0,.call
	add	x21,x21,16
	b	.bi
.call:	ldr	x0,[x21,8]
	blr	x0
	bl	term_ensure_nl
	b	.out
.program:
	bl	run_program
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; lance cmdbuf (programme) avec args, sortie dans un tube
run_program:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,272
	mov	x19,0				; 1 : nom simple, essayer SD0:/bin puis SD0:/apps
	adr	x0,cmdbuf			; chemin explicite, SD0:/bin/<nom>, SD0:/apps/<nom>.app/main
	mov	w1,'/'
	mov	x2,0
.sl:	ldrb	w3,[x0,x2]
	cbz	w3,.noslash
	cmp	w3,w1
	b.eq	.explicit
	add	x2,x2,1
	b	.sl
.explicit:
	mov	x0,sp
	adr	x1,cmdbuf
	bl	str_cat
	b	.ext
.noslash:
	mov	x19,1
	mov	x0,sp
	adr	x1,s_bin
	bl	str_cat
	adr	x1,cmdbuf
	bl	str_cat
	b	.ext
.try_apps:
	mov	x19,0
	strb	wzr,[sp]
	mov	x0,sp
	adr	x1,s_apps
	bl	str_cat
	adr	x1,cmdbuf
	bl	str_cat
	adr	x1,s_appmain
	bl	str_cat
.ext:
.go:	adr	x1,args
	ldrb	w2,[x1]
	cmp	w2,0
	csel	x1,xzr,x1,eq
	mov	x0,sp
	adr	x2,run_fd
	sys	SYS_SPAWN_PIPE
	tbz	x0,63,.started
	cbz	x19,.fail			; introuvable dans SD0:/bin : SD0:/apps
	cmn	x0,2
	b.eq	.try_apps
	b	.fail
.started:
	adr	x1,run_fd			; -1 : detache (programme de SD0:/sys
	ldr	x1,[x1]				; lance par init2, sortie au journal) :
	tbnz	x1,63,.detache			; pas d'attente
	adr	x1,run_pid
	str	x0,[x1]
	b	.out
.detache:
	mov	x19,x0
	tputs	s_detache
	mov	x0,x19
	bl	term_dec
	tputs	s_detache2
	b	.out
.fail:	mov	x19,x0
	cmn	x19,2
	b.ne	.other
	tputs	s_notfound
	tputs	cmdbuf
	mov	w0,10
	bl	term_putb
	b	.out
.other:	mov	x0,x19
	bl	term_err
.out:	add	sp,sp,272
	ldr	x19,[sp],16
	epilogue

; --- commandes integrees (args = arguments) ---------------------------------

cmd_help:
	prologue
	tputs	s_help
	epilogue

cmd_clear:
	prologue
	adr	x0,nlines
	mov	x1,1
	str	x1,[x0]
	mov	x0,0
	bl	line_ptr
	strb	wzr,[x0]
	adr	x0,curlen
	stp	xzr,xzr,[x0]
	adr	x0,scroll
	str	xzr,[x0]
	epilogue

cmd_exit:
	mov	x0,0
	sys	SYS_EXIT

cmd_pwd:
	prologue
	bl	update_cwd
	tputs	cwd
	mov	w0,10
	bl	term_putb
	epilogue

cmd_cd:
	prologue
	adr	x0,args
	ldrb	w1,[x0]
	cbnz	w1,.go
	adr	x0,s_sd
.go:	sys	SYS_CHDIR
	cbz	x0,.ok
	bl	term_err
.ok:	bl	update_cwd
	epilogue

cmd_mkdir:
	prologue
	adr	x0,args
	sys	SYS_MKDIR
	cbz	x0,.ok
	bl	term_err
.ok:	epilogue

cmd_rm:
	prologue
	adr	x0,args
	sys	SYS_UNLINK
	cbz	x0,.ok
	bl	term_err
.ok:	epilogue

cmd_kill:
	prologue
	adr	x0,args
	bl	parse_dec
	sys	SYS_KILL
	cbz	x0,.ok
	bl	term_err
.ok:	epilogue

cmd_ls:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,args
	ldrb	w1,[x0]
	cbnz	w1,.op
	adr	x0,s_dot
.op:	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
.next:	mov	x0,x19
	adr	x1,dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.end
	adr	x0,dirent
	bl	term_puts
	adr	x1,dirent
	ldr	x0,[x1,DIRENT_ATTR]
	tst	x0,ATTR_DIR
	b.eq	.file
	mov	w0,'/'
	bl	term_putb
	b	.nl
.file:	adr	x0,dirent			; aligner la taille en colonne 40
	bl	count_cols
	mov	x20,x0
.pad:	cmp	x20,38
	b.hs	.size
	mov	w0,' '
	bl	term_putb
	add	x20,x20,1
	b	.pad
.size:	mov	w0,' '
	bl	term_putb
	adr	x1,dirent
	ldr	x0,[x1,DIRENT_SIZE]
	bl	term_dec
	tputs	s_octets
.nl:	mov	w0,10
	bl	term_putb
	b	.next
.end:	mov	x0,x19
	sys	SYS_CLOSE
	b	.out
.err:	bl	term_err
.out:	ldp	x19,x20,[sp],16
	epilogue

cmd_cat:
	prologue
	str	x19,[sp,-16]!
	adr	x0,args
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
.rd:	mov	x0,x19
	adr	x1,iobuf
	mov	x2,512
	sys	SYS_READ
	cmp	x0,0
	b.le	.end
	mov	x1,x0
	adr	x0,iobuf
	bl	term_write
	b	.rd
.end:	mov	x0,x19
	sys	SYS_CLOSE
	b	.out
.err:	bl	term_err
.out:	ldr	x19,[sp],16
	epilogue

; cp source destination
cmd_cp:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,args			; separer les deux chemins
	mov	x20,x19
.f:	ldrb	w0,[x20]
	cbz	w0,.usage
	cmp	w0,' '
	b.eq	.split
	add	x20,x20,1
	b	.f
.split:	strb	wzr,[x20],1
.s2:	ldrb	w0,[x20]
	cmp	w0,' '
	b.ne	.open
	add	x20,x20,1
	b	.s2
.open:	ldrb	w0,[x20]
	cbz	w0,.usage
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x21,x0
	mov	x0,x20
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.err_close
	mov	x22,x0
.cp:	mov	x0,x21
	adr	x1,iobuf
	mov	x2,512
	sys	SYS_READ
	cmp	x0,0
	b.le	.done
	mov	x2,x0
	mov	x0,x22
	adr	x1,iobuf
	sys	SYS_WRITE
	b	.cp
.done:	mov	x0,x22
	sys	SYS_CLOSE
	mov	x0,x21
	sys	SYS_CLOSE
	b	.out
.err_close:
	mov	x19,x0
	mov	x0,x21
	sys	SYS_CLOSE
	mov	x0,x19
.err:	bl	term_err
	b	.out
.usage:	tputs	s_cp_usage
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; echo texte [> fichier]
cmd_echo:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,args
	mov	x20,x19
.f:	ldrb	w0,[x20]
	cbz	w0,.print
	cmp	w0,'>'
	b.eq	.redir
	add	x20,x20,1
	b	.f
.print:	mov	x0,x19
	bl	term_puts
	mov	w0,10
	bl	term_putb
	b	.out
.redir:	strb	wzr,[x20],1			; texte | fichier
.sp:	ldrb	w0,[x20]
	cmp	w0,' '
	b.ne	.tx
	add	x20,x20,1
	b	.sp
.tx:	mov	x0,x19				; espaces finaux du texte
	bl	str_len
.tt:	cbz	x0,.wr
	sub	x1,x0,1
	ldrb	w2,[x19,x1]
	cmp	w2,' '
	b.ne	.wr
	strb	wzr,[x19,x1]
	mov	x0,x1
	b	.tt
.wr:	mov	x0,x19				; ajoute un saut de ligne
	bl	str_len
	mov	w1,10
	strb	w1,[x19,x0]
	add	x0,x0,1
	strb	wzr,[x19,x0]
	mov	x0,x20
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.err
	mov	x20,x0
	mov	x0,x19
	bl	str_len
	mov	x2,x0
	mov	x0,x20
	mov	x1,x19
	sys	SYS_WRITE
	mov	x0,x20
	sys	SYS_CLOSE
	b	.out
.err:	bl	term_err
.out:	ldp	x19,x20,[sp],16
	epilogue

cmd_ps:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	tputs	s_ps_head
	mov	x21,0				; emplacement de depart (SYS_PROCLIST_FROM)
.paq:	adr	x0,iobuf2
	mov	x1,64
	mov	x2,x21
	sys	SYS_PROCLIST_FROM
	tbnz	x0,63,.err
	mov	x19,x0
	mov	x22,x0
	adr	x20,iobuf2
.p:	cbz	x19,.suite
	ldr	x0,[x20]
	bl	term_dec_pad6
	ldr	x0,[x20,8]
	bl	term_dec_pad6
	ldr	x0,[x20,16]
	bl	term_dec_pad6
	mov	w0,' '
	bl	term_putb
	add	x0,x20,24
	bl	term_puts
	mov	w0,10
	bl	term_putb
	ldr	x21,[x20]			; (la suite : apres ce process)
	and	x21,x21,0xFFFF
	add	x20,x20,64
	sub	x19,x19,1
	b	.p
.suite:	cmp	x22,64				; morceau plein : la suite
	b.eq	.paq
	b	.out
.err:	bl	term_err
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur : ecrite sur 6 colonnes alignees a droite, puis 2 espaces
term_dec_pad6:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x0,sp
	bl	str_len
	mov	x19,x0
.p:	cmp	x19,6
	b.hs	.v
	mov	w0,' '
	bl	term_putb
	add	x19,x19,1
	b	.p
.v:	mov	x0,sp
	bl	term_puts
	mov	w0,' '
	bl	term_putb
	mov	w0,' '
	bl	term_putb
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

cmd_mem:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,iobuf2
	sys	SYS_SYSINFO
	tbnz	x0,63,.err
	adr	x19,iobuf2
	tputs	s_mem_cpu
	ldr	x0,[x19]
	bl	term_dec
	tputs	s_mem_free
	ldr	x0,[x19,8]
	bl	term_dec
	tputs	s_mem_tot
	ldr	x0,[x19,16]
	bl	term_dec
	tputs	s_mem_up
	ldr	x0,[x19,24]
	mov	x1,1000
	udiv	x0,x0,x1
	bl	term_dec
	tputs	s_mem_proc
	ldr	x0,[x19,32]
	bl	term_dec
	tputs	s_mem_load
	mov	x20,0
.l:	ldr	x0,[x19,40]
	add	x19,x19,8
	bl	term_dec
	tputs	s_pct
	add	x20,x20,1
	cmp	x20,4
	b.lo	.l
	mov	w0,10
	bl	term_putb
	b	.out
.err:	bl	term_err
.out:	ldp	x19,x20,[sp],16
	epilogue


segment readable writeable
kit_data

ui_data
		align 8
builtins:
	dd n_help, cmd_help, n_clear, cmd_clear, n_exit, cmd_exit, n_pwd, cmd_pwd
	dd n_cd, cmd_cd, n_mkdir, cmd_mkdir, n_rm, cmd_rm, n_kill, cmd_kill
	dd n_ls, cmd_ls, n_cat, cmd_cat, n_cp, cmd_cp, n_echo, cmd_echo
	dd n_ps, cmd_ps, n_mem, cmd_mem, n_free, cmd_mem, n_dir, cmd_ls
	dd 0, 0

n_help	db 'help',0
n_clear	db 'clear',0
n_exit	db 'exit',0
n_pwd	db 'pwd',0
n_cd	db 'cd',0
n_mkdir	db 'mkdir',0
n_rm	db 'rm',0
n_kill	db 'kill',0
n_ls	db 'ls',0
n_dir	db 'dir',0
n_cat	db 'cat',0
n_cp	db 'cp',0
n_echo	db 'echo',0
n_ps	db 'ps',0
n_mem	db 'mem',0
n_free	db 'free',0

s_title		db 'Terminal',0
s_m100		db 'MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM',0
s_banner	db 'Korin OS - terminal. Tapez help pour la liste des commandes.',10,0
s_gt		db '> ',0
s_err		db 'erreur ',0
s_code		db '[code de sortie ',0
s_close		db ']',10,0
s_detache	db '[lanc',0xC3,0xA9,' par init2 : pid ',0
s_detache2	db ', sortie dans le journal]',10,0
s_ctrlc		db '^C',10,0
s_notfound	db 'commande introuvable : ',0
s_apps		db 'SD0:/apps/',0
s_bin		db 'SD0:/bin/',0
s_appmain	db '.app/main',0
s_sd		db 'SD0:/',0
s_dot		db '.',0
s_octets	db ' o',0
s_cp_usage	db 'usage : cp source destination',10,0
s_ps_head	db '   PID  THREADS  TAS Ko  NOM',10,0
s_mem_cpu	db 'Coeurs       : ',0
s_mem_free	db 10,'M',0xC3,0xA9,'moire libre : ',0
s_mem_tot	db ' Ko sur ',0
s_mem_up	db ' Ko',10,'En marche    : ',0
s_mem_proc	db ' s',10,'Process      : ',0
s_mem_load	db 10,'Charge       : ',0
s_pct		db '% ',0
s_help	db 'Commandes int',0xC3,0xA9,'gr',0xC3,0xA9,'es :',10
	db '  ls [chemin]      liste un dossier        cd [chemin]   change de dossier',10
	db '  pwd              dossier courant         cat fichier   affiche un fichier',10
	db '  mkdir dossier    cr',0xC3,0xA9,'e un dossier          rm chemin     supprime',10
	db '  cp src dest      copie un fichier        echo texte [> fichier]',10
	db '  ps               liste des process       kill pid      termine un process',10
	db '  mem              m',0xC3,0xA9,'moire et processeurs   clear         efface',10
	db '  exit             ferme le terminal',10
	db 'Tout autre nom lance SD0:/bin/<nom> ou SD0:/apps/<nom>.app (Ctrl+C pour l',39,'interrompre).',10,0
	align 8
adv100	dd 0
cols	dd 0
nlines	dd 0
scroll	dd 0
curlen	dd 0				; curlen, curcol contigus
curcol	dd 0
inlen	dd 0
incur	dd 0
hist_n	dd 0
hist_pos dd 0
run_pid	dd 0
run_fd	dd 0
cwd	rb 256
inbuf	rb 256
cmdbuf	rb 64
args	rb 256
dirent	rb DIRENT_BYTES
iobuf	rb 520
	align 8
iobuf2	rb 64*64
hist	rb HIST_N*256
lines	rb TERM_MAXL*LINE_SZ
