; ============================================================================
;  fmt_song.inc - FM Tracker : le morceau et ses fichiers
;
;  .FMS (" fm-song-project ", FM Song pour QBasic / AdLib, 2001) : binaire, petit
;  boutiste :
;	"fm-song-project" (15) | titre (20) | auteur (20) | commentaire (50) |
;	motifs (s16)
;	8 x instrument : nom (8) + 26 octets de parametres
;	par motif : lignes (s16) | vitesse (s16, une ligne dure vitesse / 20 s) |
;	8 drapeaux muet | 8 canaux x lignes octets de notes
;  Octet de note : 0 rien de neuf (la note continue, " --- "), 128 silence (rien
;  d'affiche), sinon diese * 64 + octave * 8 + note (1..7 = do re mi fa sol la si).
;
;  Les 26 octets sont des paires (operateur 1 = modulateur, 2 = porteuse) :
;  P_AR, P_DR, P_SL, P_RR, P_TL, P_KSL, P_VIB, P_AM, P_MULT, P_KSR, P_WAVE ;
;  puis P_FB, P_CON, puis la paire P_EGT.
;  .FMI (un instrument, texte) : " fm-song instrument ", le nom, puis les memes
;  paires en lignes " a,b ", P_EGT avant " feedback,connexion ".
;
;  En memoire : les notes d'un motif sont dans une case de CELLS octets (case
;  n[canal * 256 + ligne]) ; les 64 cases sont fixes (pas de tas), les motifs
;  echangent leurs pointeurs.
; ============================================================================

; x0 = motif -> x0 = adresse de sa fiche (x1 change)
pat_ptr:
	lsl	x0,x0,5
	adr	x1,g_pats
	add	x0,x0,x1
	ret

; -> x0 = fiche du motif courant
cur_pat:
	ldv	x0,g_pat
	b	pat_ptr

; x0 = canal -> x0 = instrument
ins_ptr:
	mov	x1,INS_SIZE
	mul	x0,x0,x1
	adr	x1,g_ins
	add	x0,x0,x1
	ret

; chaque motif recoit sa case de notes
slots_init:
	adr	x0,g_pats
	adr	x1,g_notes
	mov	x2,FMS_MAXPAT
.l:	str	x1,[x0,PT_N]
	add	x0,x0,PT_SIZE
	add	x1,x1,CELLS
	subs	x2,x2,1
	b.ne	.l
	ret

; x0 = fiche, x1 = lignes, x2 = vitesse : motif vide
pattern_clear:
	cmp	x1,1
	mov	x3,1
	csel	x1,x3,x1,lt
	mov	x3,FMS_MAXROWS
	cmp	x1,x3
	csel	x1,x3,x1,gt
	cmp	x2,1
	mov	x3,1
	csel	x2,x3,x2,lt
	str	x1,[x0,PT_ROWS]
	str	x2,[x0,PT_SPEED]
	str	xzr,[x0,PT_MUTE]
	ldr	x1,[x0,PT_N]
	mov	x2,CELLS/8
.z:	str	xzr,[x1],8
	subs	x2,x2,1
	b.ne	.z
	ret

; x0 = fiche, x1 = lignes : change la longueur, garde les notes
pattern_resize:
	cmp	x1,1
	mov	x3,1
	csel	x1,x3,x1,lt
	mov	x3,FMS_MAXROWS
	cmp	x1,x3
	csel	x1,x3,x1,gt
	ldr	x2,[x0,PT_N]
	str	x1,[x0,PT_ROWS]
	mov	x3,0				; ce qui depasse : efface
.c:	mov	x4,x1
.r:	cmp	x4,FMS_MAXROWS
	b.hs	.n
	strb	wzr,[x2,x4]
	add	x4,x4,1
	b	.r
.n:	add	x2,x2,FMS_MAXROWS
	add	x3,x3,1
	cmp	x3,FMS_CH
	b.lo	.c
	ret

; x0 = instrument : le piano electrique par defaut
ins_default:
	adr	x1,s_epiano
	mov	x2,0
.n:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	cmp	x2,9
	b.lo	.n
	adr	x1,ins_epiano
	mov	x2,0
.p:	ldrb	w3,[x1,x2]
	add	x4,x0,INS_P
	strb	w3,[x4,x2]
	add	x2,x2,1
	cmp	x2,26
	b.lo	.p
	ret

; morceau neuf
song_new:
	prologue
	str	x19,[sp,-16]!
	adr	x0,g_title
	adr	x1,s_untitled_raw
	mov	x2,21
	bl	str_copy
	adr	x0,g_author
	strb	wzr,[x0]
	adr	x0,g_comment
	strb	wzr,[x0]
	mov	x0,1
	stv	x0,g_npat
	mov	x0,0
	bl	pat_ptr
	mov	x1,64
	mov	x2,3
	bl	pattern_clear
	mov	x19,0
.i:	mov	x0,x19
	bl	ins_ptr
	bl	ins_default
	add	x19,x19,1
	cmp	x19,FMS_CH
	b.lo	.i
	ldr	x19,[sp],16
	epilogue

; x0 = dest, x1 = source, x2 = capacite : copie bornee, terminee par NUL
str_copy:
	mov	x3,0
	cbz	x1,.e
	sub	x2,x2,1
.l:	cmp	x3,x2
	b.hs	.e
	ldrb	w4,[x1,x3]
	cbz	w4,.e
	strb	w4,[x0,x3]
	add	x3,x3,1
	b	.l
.e:	strb	wzr,[x0,x3]
	ret

; x0 = dest, x1 = capacite, x2 = source, x3 = octets : champ complete par des
; espaces -> texte sans les espaces de fin
fms_field:
	mov	x4,0
	sub	x7,x1,1
.l:	cmp	x4,x3
	b.hs	.t
	cmp	x4,x7
	b.hs	.t
	ldrb	w5,[x2,x4]
	cmp	w5,32
	b.hs	.ok
	mov	w5,32
.ok:	strb	w5,[x0,x4]
	add	x4,x4,1
	b	.l
.t:	cbz	x4,.z
	sub	x5,x4,1
	ldrb	w6,[x0,x5]
	cmp	w6,' '
	b.ne	.z
	mov	x4,x5
	b	.t
.z:	strb	wzr,[x0,x4]
	ret

; x0 = d, x1 = position -> x0 = s16
fms_s16:
	ldrsh	x0,[x0,x1]
	ret

; x0 = fichier, x1 = octets -> x0 = 1 si c'est un .FMS (le morceau est remplace)
fms_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0				; d
	mov	x20,x1				; n
	mov	x0,107+FMS_CH*34
	cmp	x20,x0
	b.lt	.no
	adr	x1,s_magic
	mov	x2,0
.m:	ldrb	w3,[x1,x2]
	ldrb	w4,[x19,x2]
	cmp	w3,w4
	b.ne	.no
	add	x2,x2,1
	cmp	x2,15
	b.lo	.m
	mov	x1,105
	ldrsh	x21,[x19,x1]			; motifs
	cmp	x21,1
	b.lt	.no
	cmp	x21,FMS_MAXPAT
	b.gt	.no
	mov	x22,107+FMS_CH*34		; le premier motif : valide ?
	add	x0,x22,12
	cmp	x0,x20
	b.gt	.no
	ldrsh	x0,[x19,x22]
	cmp	x0,1
	b.lt	.no
	mov	x1,FMS_MAXROWS
	cmp	x0,x1
	b.gt	.no
	adr	x0,g_title
	mov	x1,21
	add	x2,x19,15
	mov	x3,20
	bl	fms_field
	adr	x0,g_author
	mov	x1,21
	add	x2,x19,35
	mov	x3,20
	bl	fms_field
	adr	x0,g_comment
	mov	x1,51
	add	x2,x19,55
	mov	x3,50
	bl	fms_field
	mov	x22,107
	mov	x23,0				; instruments
.ins:	mov	x0,x23
	bl	ins_ptr
	mov	x24,x0
	mov	x1,9
	add	x2,x19,x22
	mov	x3,8
	bl	fms_field
	add	x22,x22,8
	mov	x0,0
.ip:	add	x1,x19,x22
	ldrb	w2,[x1,x0]
	add	x1,x24,INS_P
	strb	w2,[x1,x0]
	add	x0,x0,1
	cmp	x0,26
	b.lo	.ip
	add	x22,x22,26
	add	x23,x23,1
	cmp	x23,FMS_CH
	b.lo	.ins
	mov	x23,0				; motifs lus
.pat:	cmp	x23,x21
	b.hs	.done
	add	x0,x22,12
	cmp	x0,x20
	b.gt	.done
	ldrsh	x25,[x19,x22]			; lignes
	cmp	x25,1
	b.lt	.done
	mov	x0,FMS_MAXROWS
	cmp	x25,x0
	b.gt	.done
	add	x0,x22,2
	ldrsh	x26,[x19,x0]			; vitesse
	add	x22,x22,4
	mov	x0,x23
	bl	pat_ptr
	mov	x24,x0
	mov	x1,x25
	mov	x2,x26
	bl	pattern_clear
	mov	x0,0				; muet
.mu:	add	x1,x19,x22
	ldrb	w2,[x1,x0]
	cmp	w2,0
	cset	w2,ne
	add	x1,x24,PT_MUTE
	strb	w2,[x1,x0]
	add	x0,x0,1
	cmp	x0,FMS_CH
	b.lo	.mu
	add	x22,x22,8
	ldr	x4,[x24,PT_N]
	mov	x0,0				; canal
.ch:	mov	x1,0				; ligne
.rw:	cmp	x22,x20
	b.hs	.nr
	ldrb	w2,[x19,x22]
	add	x22,x22,1
	strb	w2,[x4,x1]
.nr:	add	x1,x1,1
	cmp	x1,x25
	b.lo	.rw
	add	x4,x4,FMS_MAXROWS
	add	x0,x0,1
	cmp	x0,FMS_CH
	b.lo	.ch
	add	x23,x23,1
	b	.pat
.done:	stv	x23,g_npat
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dest, x1 = texte, x2 = largeur -> x0 = fin (complete par des espaces)
fms_pad:
	mov	x3,0
	mov	x5,0				; 1 : fin du texte
.l:	cmp	x3,x2
	b.hs	.e
	mov	w4,' '
	cbnz	x5,.p
	ldrb	w4,[x1,x3]
	cbnz	w4,.p
	mov	x5,1
	mov	w4,' '
.p:	strb	w4,[x0,x3]
	add	x3,x3,1
	b	.l
.e:	add	x0,x0,x2
	ret

; x0 = sortie -> x0 = octets du fichier .FMS
fms_write:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0				; sortie
	mov	x20,x0				; debut
	adr	x1,s_magic
	mov	x2,0
.m:	ldrb	w3,[x1,x2]
	strb	w3,[x19],1
	add	x2,x2,1
	cmp	x2,15
	b.lo	.m
	mov	x0,x19
	adr	x1,g_title
	mov	x2,20
	bl	fms_pad
	adr	x1,g_author
	mov	x2,20
	bl	fms_pad
	adr	x1,g_comment
	mov	x2,50
	bl	fms_pad
	mov	x19,x0
	ldv	x21,g_npat
	strh	w21,[x19],2
	mov	x21,0				; instruments
.i:	mov	x0,x21
	bl	ins_ptr
	mov	x22,x0
	mov	x1,x0
	mov	x0,x19
	mov	x2,8
	bl	fms_pad
	mov	x19,x0
	mov	x0,0
.ip:	add	x1,x22,INS_P
	ldrb	w2,[x1,x0]
	strb	w2,[x19],1
	add	x0,x0,1
	cmp	x0,26
	b.lo	.ip
	add	x21,x21,1
	cmp	x21,FMS_CH
	b.lo	.i
	mov	x21,0				; motifs
.pt:	ldv	x0,g_npat
	cmp	x21,x0
	b.hs	.end
	mov	x0,x21
	bl	pat_ptr
	ldr	x1,[x0,PT_ROWS]
	strh	w1,[x19],2
	ldr	x2,[x0,PT_SPEED]
	strh	w2,[x19],2
	mov	x2,0
.mu:	add	x3,x0,PT_MUTE
	ldrb	w3,[x3,x2]
	strb	w3,[x19],1
	add	x2,x2,1
	cmp	x2,FMS_CH
	b.lo	.mu
	ldr	x4,[x0,PT_N]
	mov	x2,0
.ch:	mov	x3,0
.rw:	ldrb	w5,[x4,x3]
	strb	w5,[x19],1
	add	x3,x3,1
	cmp	x3,x1
	b.lo	.rw
	add	x4,x4,FMS_MAXROWS
	add	x2,x2,1
	cmp	x2,FMS_CH
	b.lo	.ch
	add	x21,x21,1
	b	.pt
.end:	sub	x0,x19,x20
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = tampon, x2 = taille max -> x0 = octets lus (< 0 : erreur)
read_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,0
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x22,x0
.r:	sub	x2,x20,x21
	cbz	x2,.e
	mov	x0,x22
	add	x1,x19,x21
	bl	f_lire
	cmp	x0,0
	b.le	.e
	add	x21,x21,x0
	b	.r
.e:	mov	x0,x22
	bl	f_fermer
	mov	x0,x21
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = donnees, x2 = octets -> x0 = 0 / < 0
write_file:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x22,0
.w:	cmp	x22,x20
	b.hs	.ok
	mov	x0,x21
	add	x1,x19,x22
	sub	x2,x20,x22
	bl	f_ecrire
	cmp	x0,0
	b.le	.bad
	add	x22,x22,x0
	b	.w
.ok:	mov	x0,x21
	bl	f_fermer
	mov	x0,0
	b	.out
.bad:	mov	x0,x21
	bl	f_fermer
	mov	x0,-5
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- .FMI ------------------------------------------------------------------

; x0 = texte -> x0 = suite ; x1 = longueur : la ligne, sans " ni retour chariot,
; dans g_line
fmi_line:
	adr	x1,g_line
	mov	x2,0
.l:	ldrb	w3,[x0]
	cbz	w3,.e
	add	x0,x0,1
	cmp	w3,10
	b.eq	.e
	cmp	w3,13
	b.eq	.l
	cmp	w3,'"'
	b.eq	.l
	cmp	x2,95
	b.hs	.l
	strb	w3,[x1,x2]
	add	x2,x2,1
	b	.l
.e:	strb	wzr,[x1,x2]
	mov	x1,x2
	ret

; x0 = pointeur -> x0 = apres les espaces et virgules
fmi_skip:
	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.n
	cmp	w1,','
	b.ne	.r
.n:	add	x0,x0,1
	b	fmi_skip
.r:	ret

; x0 = pointeur -> x0 = apres le nombre, x1 = sa valeur
fmi_num:
	mov	x1,0
.l:	ldrb	w2,[x0]
	sub	w2,w2,'0'
	cmp	w2,10
	b.hs	.r
	mov	x3,10
	mul	x1,x1,x3
	add	x1,x1,x2
	add	x0,x0,1
	b	.l
.r:	ret

; x0 = texte NUL, x1 = instrument -> x0 = 1 si c'est un .FMI
fmi_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; ligne
	mov	x22,0				; paires
.next:	cmp	x22,13
	b.hs	.end
	ldrb	w0,[x19]
	cbz	w0,.end
	mov	x0,x19
	bl	fmi_line
	mov	x19,x0
	mov	x2,x1
	cbnz	x21,.l1
	adr	x0,s_fmi_head			; premiere ligne : la signature
	adr	x1,g_line
	mov	x3,0
.h:	ldrb	w4,[x0,x3]
	cbz	w4,.adv
	ldrb	w5,[x1,x3]
	cmp	w4,w5
	b.ne	.no
	add	x3,x3,1
	b	.h
.l1:	cmp	x21,1
	b.ne	.pair
	adr	x0,g_line			; le nom (9 octets, espaces de fin retires)
	bl	fmi_skip
	mov	x1,x0
	mov	x0,x20
	mov	x2,9
	bl	str_copy
	mov	x0,x20
	bl	str_len
.tr:	cbz	x0,.adv
	sub	x1,x0,1
	ldrb	w2,[x20,x1]
	cmp	w2,' '
	b.ne	.adv
	strb	wzr,[x20,x1]
	mov	x0,x1
	b	.tr
.pair:	cbz	x2,.adv
	adr	x0,g_line
	bl	fmi_skip
	bl	fmi_num
	mov	x4,x1				; a
	bl	fmi_skip
	bl	fmi_num
	mov	x5,x1				; b
	mov	x6,P_FB
	cmp	x22,12
	b.hs	.st
	adr	x0,fmi_order
	ldrb	w6,[x0,x22]
.st:	add	x0,x20,INS_P
	strb	w4,[x0,x6]
	add	x6,x6,1
	strb	w5,[x0,x6]
	add	x22,x22,1
.adv:	add	x21,x21,1
	b	.next
.end:	mov	x0,0
	cmp	x22,13
	b.lo	.out
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie, x1 = valeur -> x0 = fin (decimal)
fmi_put:
	prologue
	stp	x0,xzr,[sp,-16]!
	mov	x0,x1
	ldr	x1,[sp]
	bl	fmt_dec
	add	sp,sp,16
	epilogue

; x0 = sortie, x1 = texte -> x0 = fin
fmi_str:
	ldrb	w2,[x1],1
	cbz	w2,.r
	strb	w2,[x0],1
	b	fmi_str
.r:	ret

; x0 = instrument, x1 = sortie -> x0 = octets du texte
fmi_write:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x1
	mov	x0,x20
	adr	x1,s_fmi_start
	bl	fmi_str
	mov	x1,x19
	bl	fmi_str
	adr	x1,s_fmi_q
	bl	fmi_str
	mov	x20,x0
	mov	x22,0
.p:	cmp	x22,13
	b.hs	.e
	mov	x6,P_FB
	cmp	x22,12
	b.hs	.v
	adr	x0,fmi_order
	ldrb	w6,[x0,x22]
.v:	add	x7,x19,INS_P
	add	x7,x7,x6
	ldrb	w1,[x7]
	mov	x0,x20
	bl	fmi_put
	mov	w1,','
	strb	w1,[x0],1
	mov	x20,x0
	add	x7,x19,INS_P
	mov	x6,P_FB
	cmp	x22,12
	b.hs	.v2
	adr	x0,fmi_order
	ldrb	w6,[x0,x22]
.v2:	add	x7,x7,x6
	ldrb	w1,[x7,1]
	mov	x0,x20
	bl	fmi_put
	adr	x1,s_crlf
	bl	fmi_str
	mov	x20,x0
	add	x22,x22,1
	b	.p
.e:	strb	wzr,[x20]
	sub	x0,x20,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = instrument -> x0 = 1 si lu
read_fmi:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	adr	x1,g_fmibuf
	mov	x2,1023
	bl	read_file
	cmp	x0,0
	b.le	.no
	adr	x1,g_fmibuf
	strb	wzr,[x1,x0]
	mov	x0,x1
	mov	x1,x19
	bl	fmi_parse
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---- notes -----------------------------------------------------------------

; x0 = octet de note -> x0 = milli-Hz (0 : pas une note)
note_mhz:
	tbnz	x0,7,.z
	ands	x1,x0,7
	b.eq	.z
	ubfx	x2,x0,3,3
	ubfx	x3,x0,6,1
	adr	x4,semi_tab
	ldrb	w0,[x4,x1]
	add	x0,x0,x3
	mov	x1,x2
	b	ak_note_octave_mhz
.z:	mov	x0,0
	ret

; x0 = note (1..7), x1 = diese, x2 = octave -> x0 = octet de note
make_note:
	cbz	x1,.n
	cmp	x0,3				; mi diese = fa
	b.ne	.b
	mov	x0,4
	mov	x1,0
	b	.n
.b:	cmp	x0,7				; si diese = do de l'octave suivante
	b.ne	.n
	mov	x0,1
	mov	x1,0
	cmp	x2,7
	b.hs	.n
	add	x2,x2,1
.n:	and	x0,x0,7
	and	x2,x2,7
	lsl	x2,x2,3
	orr	x0,x0,x2
	cbz	x1,.r
	orr	x0,x0,64
.r:	ret

; x0 = octet, x1 = demi-tons -> x0 = note transposee (bornee ; le reste inchange)
note_transpose:
	tbnz	x0,7,.r
	tst	x0,7
	b.eq	.r
	and	x2,x0,7
	adr	x3,semi_tab
	ldrb	w2,[x3,x2]
	ubfx	x3,x0,3,3
	mov	x4,12
	mul	x3,x3,x4
	add	x2,x2,x3
	ubfx	x3,x0,6,1
	add	x2,x2,x3
	add	x2,x2,x1
	cmp	x2,0
	csel	x2,xzr,x2,lt
	mov	x3,95
	cmp	x2,x3
	csel	x2,x3,x2,gt
	mov	x4,12
	udiv	x3,x2,x4			; octave
	msub	x5,x3,x4,x2			; demi-ton
	adr	x6,tr_note
	ldrb	w0,[x6,x5]
	adr	x6,tr_sharp
	ldrb	w1,[x6,x5]
	lsl	x1,x1,6
	lsl	x3,x3,3
	orr	x0,x0,x1
	orr	x0,x0,x3
.r:	ret

; x0 = octet, x1 = sortie (4 octets) : " C#4 ", " D-3 ", " --- ", " " (silence)
note_text:
	tbnz	x0,7,.off
	ands	x2,x0,7
	b.ne	.note
	mov	w2,'-'
	strb	w2,[x1]
	strb	w2,[x1,1]
	strb	w2,[x1,2]
	strb	wzr,[x1,3]
	ret
.off:	strb	wzr,[x1]
	ret
.note:	adr	x3,s_note_letters
	ldrb	w3,[x3,x2]
	strb	w3,[x1]
	mov	w3,'-'
	tst	x0,64
	b.eq	.s
	mov	w3,'#'
.s:	strb	w3,[x1,1]
	ubfx	x3,x0,3,3
	add	w3,w3,'0'
	strb	w3,[x1,2]
	strb	wzr,[x1,3]
	ret
