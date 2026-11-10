; chmod - le bit d'execution : chmod mode fichiers (docs/OUTILS.md, docs/FICHIERS.md)
; mode : octal (755 : executable si un bit x, 0111) ou symbolique, des clauses
; separees par « , » : [ugoa]* puis + - ou = puis [rwxXst]* (+x, a-x, u=rwx,go=r).
; Seul x compte (FAT32 et exFAT n'ont pas d'autres droits) : vfs tient l'index
; des executables (V_STATX pour l'etat present, V_CHMOD pour le nouveau). Un
; repertoire, un fichier des regles de chemin (SD0:/bin, SD0:/sys...) : rien.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	cmp	x19,3
	b.hs	.ok
	adr	x0,s_usage
	bl	t_err_msg
	mov	x0,2
	bl	t_fin
.ok:	mov	x23,0				; code
	mov	x21,2
.a:	cmp	x21,x19
	b.hs	.fin
	ldr	x24,[x20,x21,lsl 3]
	add	x21,x21,1
	mov	x0,x24
	bl	etat_x				; -> 0/1, ou erreur
	tbnz	x0,63,.err
	mov	x1,x0
	ldr	x0,[x20,8]
	bl	appliquer			; -> 0/1, ou erreur (mode illisible)
	tbnz	x0,63,.mode
	mov	x2,0x1ED			; 0755
	mov	x3,0x1A4			; 0644
	cmp	x0,0
	csel	x2,x3,x2,eq
	sub	sp,sp,MSG_TAILLE
	mov	x0,sp
	mov	x1,V_CHMOD
	mov	x3,0
	mov	x4,x24
	mov	x5,0
	bl	t_msg
	mov	x0,sp
	bl	t_vfs
	add	sp,sp,MSG_TAILLE
	tbz	x0,63,.a
.err:	mov	x0,x24
	bl	t_err_fic
	mov	x23,1
	b	.a
.mode:	adr	x0,s_mode
	bl	t_err_msg
	mov	x23,2
.fin:	mov	x0,x23
	bl	t_fin

; x0 = chemin -> x0 = 1 s'il est executable, 0 sinon, ou erreur (V_STATX)
etat_x:
	prologue
	sub	sp,sp,MSG_TAILLE
	mov	x4,x0
	mov	x0,sp
	mov	x1,V_STATX
	mov	x2,0
	mov	x3,0
	mov	x5,0
	bl	t_msg
	mov	x0,sp
	bl	t_vfs
	tbnz	x0,63,.out
	ldr	x0,[sp,MSG_C]
	tst	x0,ATTR_EXEC
	cset	x0,ne
.out:	add	sp,sp,MSG_TAILLE
	epilogue

; x0 = mode (texte), x1 = executable maintenant (0/1) -> x0 = executable
; ensuite (0/1), E_INVAL si le mode est illisible
appliquer:
	ldrb	w2,[x0]
	sub	w3,w2,'0'
	cmp	w3,7
	b.hi	.symb
	mov	x4,0				; octal
.o:	ldrb	w2,[x0],1
	cbz	w2,.of
	sub	w3,w2,'0'
	cmp	w3,7
	b.hi	.inval
	add	x4,x3,x4,lsl 3
	b	.o
.of:	mov	x5,0x49				; 0111
	tst	x4,x5
	cset	x0,ne
	ret
.symb:	mov	x6,x1				; etat courant
.clause:
	ldrb	w2,[x0]				; [ugoa]*
	cmp	w2,'u'
	b.eq	.qui
	cmp	w2,'g'
	b.eq	.qui
	cmp	w2,'o'
	b.eq	.qui
	cmp	w2,'a'
	b.ne	.op
.qui:	add	x0,x0,1
	b	.clause
.op:	ldrb	w7,[x0],1			; + - =
	cmp	w7,'+'
	b.eq	.droits
	cmp	w7,'-'
	b.eq	.droits
	cmp	w7,'='
	b.ne	.inval
.droits:
	mov	x8,0				; 1 : x dans les droits
.d:	ldrb	w2,[x0]
	cbz	w2,.applique
	cmp	w2,','
	b.eq	.applique
	cmp	w2,'+'				; (une autre operation : « u+x-w »)
	b.eq	.applique
	cmp	w2,'-'
	b.eq	.applique
	cmp	w2,'='
	b.eq	.applique
	cmp	w2,'x'
	b.eq	.x
	cmp	w2,'X'
	b.eq	.x
	cmp	w2,'r'
	b.eq	.dn
	cmp	w2,'w'
	b.eq	.dn
	cmp	w2,'s'
	b.eq	.dn
	cmp	w2,'t'
	b.ne	.inval
	b	.dn
.x:	mov	x8,1
.dn:	add	x0,x0,1
	b	.d
.applique:
	cmp	w7,'='
	b.ne	.pm
	mov	x6,x8
	b	.suite
.pm:	cbz	x8,.suite
	cmp	w7,'+'
	cset	x6,eq
.suite:	ldrb	w2,[x0]
	cbz	w2,.fin
	cmp	w2,','
	b.ne	.op				; (« +x-w » : l'operation suivante)
	add	x0,x0,1
	b	.clause
.fin:	mov	x0,x6
	ret
.inval:	mov	x0,E_INVAL
	ret

include '../../inc/tool.inc'

s_usage	db 'usage : chmod mode fichier...  (mode : 755, +x, a-x, u=rwx,go=r)',0
s_mode	db 'mode illisible',0

segment readable writeable
kit_data
tool_data
