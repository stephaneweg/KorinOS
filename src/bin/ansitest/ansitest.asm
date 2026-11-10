; ansitest - essai du Terminal VT : ecrit chaque famille de sequences
; comprise (docs/TERMINAL-ET-FORK.md 1.4), avec son libelle. A comparer a
; xterm ; aucun ESC ne doit s'afficher.
;	ansitest		tout
;	ansitest -n		sans la page de l'ecran alternatif (ni attente)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

ESC_	equ 27

; texte avec ESC : e 'a', '[1m' ... (chaque morceau apres le premier : ESC devant)
macro e [t] {
	db t
}

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	mov	x21,0				; 1 : -n
	cmp	x19,2
	b.lo	.go
	ldr	x0,[x20,8]
	adr	x1,s_n
	bl	str_eq
	mov	x21,x0
.go:	adr	x0,s_part1			; 16 couleurs, attributs
	bl	t_puts
	adr	x0,s_256			; 256 couleurs
	bl	t_puts
	mov	x19,16
.c256:	adr	x0,s_bg5
	bl	t_puts
	mov	x0,x19
	bl	t_putn
	adr	x0,s_blk
	bl	t_puts
	sub	x0,x19,15			; 36 par ligne (cube), puis les gris
	mov	x1,36
	udiv	x2,x0,x1
	msub	x2,x2,x1,x0
	cmp	x19,231
	b.hi	.gris
	cbnz	x2,.c256n
	b	.nl
.gris:	cmp	x19,255
	b.ne	.c256n
.nl:	adr	x0,s_rnl
	bl	t_puts
.c256n:	add	x19,x19,1
	cmp	x19,256
	b.lo	.c256
	adr	x0,s_tc				; 24 bits : trois degrades
	bl	t_puts
	mov	x22,0
.tcl:	mov	x19,0
.tc:	adr	x0,s_bg2
	bl	t_puts
	lsl	x23,x19,2			; 0..252
	mov	x0,0
	cmp	x22,0
	csel	x0,x23,x0,eq
	bl	t_putn
	mov	w0,';'
	bl	t_putc
	mov	x0,0
	cmp	x22,1
	csel	x0,x23,x0,eq
	bl	t_putn
	mov	w0,';'
	bl	t_putc
	mov	x0,0
	cmp	x22,2
	csel	x0,x23,x0,eq
	bl	t_putn
	adr	x0,s_blk1
	bl	t_puts
	add	x19,x19,1
	cmp	x19,64
	b.lo	.tc
	adr	x0,s_rnl
	bl	t_puts
	add	x22,x22,1
	cmp	x22,3
	b.lo	.tcl
	adr	x0,s_part2			; curseur, effacement, inconnues, UTF-8
	bl	t_puts
	cbnz	x21,.fin
	adr	x0,s_alt			; ecran alternatif, zone de defilement
	bl	t_puts
	bl	t_flush
	mov	x0,1500
	sys	SYS_SLEEP
	mov	x22,1				; la zone (lignes 6 a 10) defile
.sc:	adr	x0,s_scl
	bl	t_puts
	mov	x0,x22
	bl	t_putn
	bl	t_flush
	mov	x0,250
	sys	SYS_SLEEP
	add	x22,x22,1
	cmp	x22,12
	b.ls	.sc
	adr	x0,s_alt2
	bl	t_puts
	bl	t_flush
	mov	x0,4000
	sys	SYS_SLEEP
	adr	x0,s_altq
	bl	t_puts
.fin:	adr	x0,s_end
	bl	t_puts
	mov	x0,0
	bl	t_fin

include '../../inc/tool.inc'
s_n	db '-n',0

s_part1:
	e 'ansitest : chaque ligne dit ce qu',39,'elle doit montrer ; aucun ESC ne doit s',39,'afficher.',10
	e 10,ESC_,'[1m== 16 couleurs (30-37, 90-97 ; fond 40-47, 100-107)',ESC_,'[0m',10
	e ESC_,'[30m noir',ESC_,'[31m rouge',ESC_,'[32m vert',ESC_,'[33m jaune',ESC_,'[34m bleu'
	e ESC_,'[35m magenta',ESC_,'[36m cyan',ESC_,'[37m blanc',ESC_,'[39m (defaut)',10
	e ESC_,'[90m noir',ESC_,'[91m rouge',ESC_,'[92m vert',ESC_,'[93m jaune',ESC_,'[94m bleu'
	e ESC_,'[95m magenta',ESC_,'[96m cyan',ESC_,'[97m blanc',ESC_,'[39m (vifs)',10
	e ESC_,'[40m 0 ',ESC_,'[41m 1 ',ESC_,'[42m 2 ',ESC_,'[43m 3 ',ESC_,'[44m 4 ',ESC_,'[45m 5 '
	e ESC_,'[46m 6 ',ESC_,'[47;30m 7 ',ESC_,'[49;39m fonds 40-47',10
	e ESC_,'[100m 8 ',ESC_,'[101m 9 ',ESC_,'[102m10 ',ESC_,'[103m11 ',ESC_,'[104m12 ',ESC_,'[105m13 '
	e ESC_,'[106m14 ',ESC_,'[107;30m15 ',ESC_,'[m fonds 100-107',10
	e 10,ESC_,'[1m== Attributs',ESC_,'[0m',10
	e ESC_,'[1mgras',ESC_,'[22m ',ESC_,'[2matt',0xC3,0xA9,'nu',ESC_,'[22m ',ESC_,'[3mitalique (droit)',ESC_,'[23m '
	e ESC_,'[4msoulign',0xC3,0xA9,ESC_,'[24m ',ESC_,'[7minverse',ESC_,'[27m ',ESC_,'[9mbarr',0xC3,0xA9,ESC_,'[29m '
	e '[',ESC_,'[8mcach',0xC3,0xA9,ESC_,'[28m] (vide entre crochets)',10
	e ESC_,'[1;31mgras rouge = rouge vif',ESC_,'[0m ',ESC_,'[1;4;7;32m gras souligne inverse vert ',ESC_,'[0m',10,0

s_256:	e 10,ESC_,'[1m== 256 couleurs (48;5;n : cube 16-231, gris 232-255)',ESC_,'[0m',10,0
s_bg5:	e ESC_,'[48;5;',0
s_bg2:	e ESC_,'[48;2;',0
s_blk:	e 'm  ',0
s_blk1:	e 'm ',0
s_rnl:	e ESC_,'[0m',10,0
s_tc:	e 10,ESC_,'[1m== 24 bits (48;2;r;g;b : rouge, vert, bleu)',ESC_,'[0m',10,0

s_part2:
	e 10,ESC_,'[1m== Couleur du texte 38;5 et 38;2',ESC_,'[0m',10
	e ESC_,'[38;5;208morange 208',ESC_,'[39m ',ESC_,'[38;5;33mbleu 33',ESC_,'[39m '
	e ESC_,'[38;2;255;105;180mrose 24 bits',ESC_,'[39m ',ESC_,'[38:2:0:200:200mcyan (deux-points)',ESC_,'[0m',10
	e 10,ESC_,'[1m== Curseur',ESC_,'[0m',10
	e 'ABCDEFGH',ESC_,'[5Dx',ESC_,'[2Cy',13,ESC_,'[30G<- CUB, CUF, CR, CHA : ABCxEFyH',10
	e '..........',ESC_,'[A',ESC_,'[10D',ESC_,'[B',13,'1',ESC_,'[3C2',ESC_,'[20G<- 1...2..... (CUU, CUB, CUD)',10
	e ESC_,'7','sauve',ESC_,'[3G!!',ESC_,'8',ESC_,'[C',ESC_,'[C',ESC_,'[C',ESC_,'[C',ESC_,'[C|  <- sa!!e| (ESC 7 / 8)',10
	e ESC_,'[s','12345',ESC_,'[u','ab',ESC_,'[30G<- ab345 (CSI s / u)',10
	e 10,ESC_,'[1m== Effacement',ESC_,'[0m',10
	e 'XXXXXXXXXXXXXXX',ESC_,'[8D',ESC_,'[K',ESC_,'[30G<- 7 X (EL 0)',10
	e 'XXXXXXXXXXXXXXX',ESC_,'[8D',ESC_,'[1K',ESC_,'[30G<- 8 blancs puis 7 X (EL 1)',10
	e 'XXXXXXXXXXXXXXX',ESC_,'[2K',ESC_,'[30G<- rien (EL 2)',10
	e 'abcdefgh',ESC_,'[6D',ESC_,'[2P',ESC_,'[30G<- abefgh (DCH)',10
	e 'abcdefgh',ESC_,'[6D',ESC_,'[2@',ESC_,'[30G<- ab  cdefgh (ICH)',10
	e 'abcdefgh',ESC_,'[6D',ESC_,'[3X',ESC_,'[30G<- ab   fgh (ECH)',10
	e '=',ESC_,'[9b',ESC_,'[30G<- 10 = (REP)',10
	e ESC_,'[41m',ESC_,'[K',ESC_,'[0m','fond rouge jusqu',39,'au bord (EL avec le fond courant)',10
	e 10,ESC_,'[1m== S',0xC3,0xA9,'quences inconnues (rien ne doit appara',0xC3,0xAE,'tre entre les crochets)',ESC_,'[0m',10
	e '[',ESC_,'[99;99z',ESC_,']0;titre',7,ESC_,']8;;http://x',ESC_,92,ESC_,'P1$r',ESC_,92
	e ESC_,'[?9999h',ESC_,'(B',ESC_,'#8',ESC_,'[>4;1m',ESC_,'[ q',ESC_,'[6n',ESC_,'[c',ESC_,'=',ESC_,'>',']',10
	e '[',ESC_,'[?2004h',ESC_,'[?2004l',ESC_,'[?1h',ESC_,'[?1l',']  <- collage encadr',0xC3,0xA9,', mode des fl',0xC3,0xA8,'ches : not',0xC3,0xA9,'s, rien d',39,'affich',0xC3,0xA9,10
	e 10,ESC_,'[1m== UTF-8 (une cellule par caract',0xC3,0xA8,'re)',ESC_,'[0m',10
	e 0xC3,0xA9,0xC3,0xA0,0xC3,0xBC,0xC3,0xA7,' ',0xE2,0x86,0x92,' ',0xE2,0x9C,0x93,' ',0xE2,0x94,0x80,0xE2,0x94,0x80,0xE2,0x94,0x90
	e ' ',0xCE,0xBB,0xCF,0x80,' <- ',0xC3,0xA9,0xC3,0xA0,0xC3,0xBC,0xC3,0xA7,', fl',0xC3,0xA8,'che, coche, bord de bo',0xC3,0xAE,'te, lambda pi',10
	e 0,0

s_alt:	e 10,ESC_,'[1m== ',0xC3,0x89,'cran alternatif (quelques secondes), zone de d',0xC3,0xA9,'filement',ESC_,'[0m',10
	e ESC_,'[?1049h',ESC_,'[?25l',ESC_,'[H',ESC_,'[2J'
	e ESC_,'[1;1H',ESC_,'[7m ',0xC3,0x89,'cran alternatif (?1049h) : curseur cach',0xC3,0xA9,' (?25l) ',ESC_,'[0m'
	e ESC_,'[3;5HCUP 3;5',ESC_,'[4;20HCUP 4;20'
	e ESC_,'[5;1H',ESC_,'[33m---- zone de d',0xC3,0xA9,'filement : lignes 6 ',0xC3,0xA0,' 10 (CSI 6;10 r) ----',ESC_,'[0m'
	e ESC_,'[11;1H',ESC_,'[33m---- fin de la zone ----',ESC_,'[0m'
	e ESC_,'[12;1Hcette ligne ne doit pas bouger'
	e ESC_,'[6;10r',ESC_,'[10;1H',0
s_scl:	e 10,'ligne ',0
s_alt2:	e ESC_,'[r',ESC_,'[13;1H',ESC_,'[32mIL/DL : ',ESC_,'[0m'
	e ESC_,'[14;1Ha',ESC_,'[15;1Hb',ESC_,'[16;1Hc',ESC_,'[15;1H',ESC_,'[L'
	e ESC_,'[15;5H<- ligne ins',0xC3,0xA9,'r',0xC3,0xA9,'e (IL) entre a et b',ESC_,'[17;1H',ESC_,'[M',ESC_,'[17;1H(c supprim',0xC3,0xA9,'e par DL)'
	e ESC_,'[18;1Hretour ',0xC3,0xA0,' l',39,0xC3,0xA9,'cran principal dans 4 s : le texte d',39,'avant doit revenir intact',0
s_altq:	e ESC_,'[?25h',ESC_,'[?1049l',ESC_,'[1m== Retour de l',39,0xC3,0xA9,'cran alternatif (?1049l) : tout ce qui pr',0xC3,0xA9,'c',0xC3,0xA8,'de est intact',ESC_,'[0m',10,0
s_end:	e 'fin de ansitest',10,0
	align 4

segment readable writeable
kit_data
tool_data
