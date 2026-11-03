; ============================================================================
;  kits/audiokit/audiokit.asm - AudioKit : le son (docs/SON.md 3, docs/KITS.md)
;  -> SD0:/lib/audiokit.so
;
;  Le seul chemin des programmes vers le son :
;  * flux de lecture : ak_open(frequence, canaux, bits, sortie) ; ak_write
;    convertit dans le process de l'application (mono -> stereo, 8/24/32
;    bits -> 16, autre frequence -> 44,1 kHz par le reechantillonneur
;    lineaire) et ecrit dans l'anneau partage du flux, que le service
;    " sound " melange ;
;  * capture : ak_rec_open(source) (micro, boucle d'une sortie, entree n),
;    ak_rec_read : s16 stereo 44,1 kHz ;
;  * outils : reechantillonnage, melange, gain, courbe de volume, en-tete WAV.
;  (Le son du noyau, appels 28-35, est parti a l'etape 8 de docs/SON.md :
;  branche de retour son-noyau.)
;  * formats (docs/SON.md 9) : decodage WAV, FLAC, MP3, Vorbis, Opus
;    (ak_dec_* : fichier ou tampon ; ak_codec_* : paquets d'un
;    demultiplexeur, pour MediaKit), lecteur en arriere-plan (ak_play) ;
;    code commun avec le banc du PC dans ak_codecs.inc.
;  Variables par process (segment inscriptible du kit) : canal du service,
;  table des flux (AK_MAX).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include '../../inc/sound.inc'

AKE_BADF	= -9
AKE_PIPE	= -32

AK_MAX		= 8			; flux par process
AKS_USED	= 0			; 0 libre, 1 lecture, 2 capture, 3 reserve
AKS_ID		= 8			; numero chez sound (ou au noyau)
AKS_RING	= 16			; anneau mappe (service)
AKS_BUF		= 24			; son numero de tampon
AKS_RATE	= 32
AKS_CH		= 40
AKS_BITS	= 48
AKS_FBYTES	= 56			; octets par trame d'entree
AKS_RS		= 64			; reechantillonneur : precedente, position, pas
AKS_LAT		= 88			; avance maximale (octets)
AKS_WRITTEN	= 96			; trames ecrites (s16 stereo 44,1 kHz)
AKS_MODE	= 104			; 1 service
AKS_SIZE	= 128
AK_LAT_DEF	= 16384			; ~93 ms d'avance par defaut
AK_PIECE	= 512			; trames converties par morceau

MODE_SVC	= 1

segment readable executable
include 'audiokit.abi'
kit_use '../../kits/appkit/appkit.abi'		; fichiers (ak_dec_open, ak_play)

; AppKit ouvert au premier besoin (son client de fichiers) ; garde x0-x8
ak_kits:
	adr	x16,ak_kits_ok
	ldr	x17,[x16]
	cbnz	x17,.ok
	stp	x29,x30,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x4,x5,[sp,-16]!
	stp	x6,x7,[sp,-16]!
	str	x8,[sp,-16]!
	bl	kits_open
	adr	x16,ak_kits_ok
	mov	x17,1
	str	x17,[x16]
	ldr	x8,[sp],16
	ldp	x6,x7,[sp],16
	ldp	x4,x5,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	ldp	x29,x30,[sp],16
.ok:	ret

; ---------------------------------------------------------------------------
;  Liaison
; ---------------------------------------------------------------------------

; -> x0 = 1 si le service sound repond (canal ouvert), 0 : aucun son
ak_mode_get:
	prologue
	adr	x0,ak_mode
	ldr	x0,[x0]
	cbnz	x0,.out
	adr	x0,s_sound			; (sound peut demarrer apres nous)
	movl	x1,3000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.none
	add	x0,x0,1
	adr	x1,ak_chan
	str	x0,[x1]
	mov	x0,MODE_SVC
	adr	x1,ak_mode
	str	x0,[x1]
	b	.out
.none:	mov	x0,0
.out:	epilogue

; -> 1 service sound, 0 aucun (2 : l'ancien son du noyau, parti)
ak_backend:
	b	ak_mode_get

; x0 = message -> x0 = resultat (E_NODEV : pas de service)
ak_call:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,ak_chan
	ldr	x0,[x0]
	cbz	x0,.nodev
	sub	x0,x0,1
	mov	x1,x19
	sys	SYS_SVC_CALL
	cmn	x0,32				; E_PIPE : sound s'est arrete
	b.ne	.out
	adr	x1,ak_chan
	str	xzr,[x1]
	adr	x1,ak_mode
	str	xzr,[x1]
	b	.out
.nodev:	mov	x0,E_NODEV
.out:	ldr	x19,[sp],16
	epilogue

; x0 = op, x1 = a, x2 = b, x3 = c, x4 = message (512 o) -> x0 = resultat
ak_req:
	prologue
	str	x0,[x4,WM_OP]
	stp	x1,x2,[x4,WM_A]
	str	x3,[x4,WM_C]
	str	wzr,[x4,WM_LEN]
	mov	x0,x4
	bl	ak_call
	epilogue

; x0 = poignee, x1 = genre (1 lecture, 2 capture) -> x0 = entree, 0 si invalide
ak_entry:
	cmp	x0,AK_MAX
	b.hs	.bad
	adr	x2,ak_tab
	add	x2,x2,x0,lsl 7
	ldr	x3,[x2,AKS_USED]
	cmp	x3,x1
	b.ne	.bad
	mov	x0,x2
	ret
.bad:	mov	x0,0
	ret

; -> x0 = poignee libre, reservee (genre 3) ; -1 si plein
ak_slot:
	adr	x2,ak_tab
	mov	x0,0
.t:	add	x3,x2,x0,lsl 7
.l:	ldaxr	x4,[x3]
	cbnz	x4,.n
	mov	x4,3
	stxr	w5,x4,[x3]
	cbnz	w5,.l
	ret
.n:	clrex
	add	x0,x0,1
	cmp	x0,AK_MAX
	b.lo	.t
	mov	x0,-1
	ret

; ---------------------------------------------------------------------------
;  Flux de lecture
; ---------------------------------------------------------------------------

; x0 = frequence, x1 = canaux, x2 = bits, x3 = sortie -> poignee
ak_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	movl	x0,4000				; formats acceptes
	cmp	x19,x0
	b.lo	.inval
	movl	x0,192000
	cmp	x19,x0
	b.hi	.inval
	sub	x0,x20,1
	cmp	x0,7
	b.hi	.inval
	cmp	x21,8
	b.eq	.fmt
	cmp	x21,16
	b.eq	.fmt
	cmp	x21,24
	b.eq	.fmt
	cmp	x21,32
	b.ne	.inval
.fmt:	bl	ak_mode_get
	cbz	x0,.nodev
	mov	x24,x0
	bl	ak_slot
	tbnz	x0,63,.full
	mov	x23,x0
	adr	x25,ak_tab
	add	x25,x25,x23,lsl 7
	mov	x0,SND_OP_OPEN
	mov	x1,x22
	mov	x2,0
	mov	x3,0
	mov	x4,sp
	bl	ak_req
	tbnz	x0,63,.fail
	str	x0,[x25,AKS_ID]
	ldr	x0,[sp,WM_B]			; l'anneau du flux
	str	x0,[x25,AKS_BUF]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.unmap
	str	x0,[x25,AKS_RING]
	str	x24,[x25,AKS_MODE]
	str	x19,[x25,AKS_RATE]
	str	x20,[x25,AKS_CH]
	str	x21,[x25,AKS_BITS]
	lsr	x0,x21,3
	mul	x0,x0,x20
	str	x0,[x25,AKS_FBYTES]
	str	xzr,[x25,AKS_RS]		; reechantillonneur : 0, 0, pas
	str	xzr,[x25,AKS_RS+8]
	lsl	x0,x19,16
	movl	x1,SND_RATE_STD
	udiv	x0,x0,x1
	str	x0,[x25,AKS_RS+16]
	mov	x0,AK_LAT_DEF
	str	x0,[x25,AKS_LAT]
	str	xzr,[x25,AKS_WRITTEN]
	mov	x0,1
	add	x1,x25,AKS_USED
	stlr	x0,[x1]
	mov	x0,x23
	b	.out
.unmap:	str	x0,[sp,-16]!			; flux ouvert chez sound : le fermer
	mov	x0,SND_OP_CLOSE
	ldr	x1,[x25,AKS_ID]
	mov	x2,0
	mov	x3,0
	add	x4,sp,16
	bl	ak_req
	ldr	x0,[sp],16
.fail:	str	xzr,[x25,AKS_USED]
	b	.out
.full:	mov	x0,E_AGAIN
	b	.out
.nodev:	mov	x0,E_NODEV
	b	.out
.inval:	mov	x0,AKE_INVAL
.out:	add	sp,sp,512
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree -> x0 = octets en attente, x1 = place libre (s16 stereo)
ak_fill:
	ldr	x1,[x0,AKS_RING]
	ldr	x2,[x1,SR_HEAD]
	add	x3,x1,SR_TAIL
	ldar	x3,[x3]
	sub	x0,x2,x3
	movl	x1,SR_STREAM_BYTES
	sub	x1,x1,x0
	ret

; x0 = entree -> x0 = place utilisable (octets s16 stereo, avance comprise)
ak_room:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	ak_fill
	ldr	x2,[x19,AKS_LAT]
	subs	x2,x2,x0			; avance restante
	csel	x2,xzr,x2,lo
	cmp	x1,x2
	csel	x0,x1,x2,lo
	and	x0,x0,not 3
	ldr	x19,[sp],16
	epilogue

; x0 = entree, x1 = trames s16 stereo, x2 = octets (qui tiennent)
ak_put:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	ldr	x0,[x19,AKS_WRITTEN]
	add	x0,x0,x21,lsr 2
	str	x0,[x19,AKS_WRITTEN]
	ldr	x22,[x19,AKS_RING]
	ldr	x3,[x22,SR_HEAD]
	add	x4,x22,SR_DATA
	movl	x5,SR_STREAM_BYTES-1
	mov	x6,0
.c:	cmp	x6,x21
	b.hs	.done
	and	x7,x3,x5
	ldr	w8,[x20,x6]
	str	w8,[x4,x7]
	add	x3,x3,4
	add	x6,x6,4
	b	.c
.done:	add	x0,x22,SR_HEAD
	stlr	x3,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree, x1 = source, x2 = trames, x3 = destination s16 stereo
ak_convert:
	prologue
	ldr	x4,[x0,AKS_CH]
	ldr	x5,[x0,AKS_BITS]
	ldr	x6,[x0,AKS_FBYTES]
	lsr	x7,x5,3
.f:	cbz	x2,.done
	mov	x9,x1
	bl	ak_sample			; gauche
	mov	w11,w10
	cmp	x4,1
	b.eq	.mono				; (mono : droite = gauche)
	add	x9,x1,x7
	bl	ak_sample			; droite
.mono:	strh	w11,[x3],2
	strh	w10,[x3],2
	add	x1,x1,x6
	sub	x2,x2,1
	b	.f
.done:	epilogue

; x9 = echantillon, x5 = bits, x7 = octets -> w10 (s16) ; ne touche que w10, w12
ak_sample:
	cmp	x5,8
	b.ne	.w
	ldrb	w10,[x9]			; 8 bits : non signe
	sub	w10,w10,128
	lsl	w10,w10,8
	ret
.w:	sub	x12,x7,2			; les deux octets de poids fort
	add	x12,x9,x12			; (petit-boutiste, sans alignement)
	ldrb	w10,[x12]
	ldrsb	w12,[x12,1]
	orr	w10,w10,w12,lsl 8
	ret

; x0 = flux, x1 = tampon, x2 = octets -> octets pris (format du flux)
ak_write:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,AK_PIECE*8		; morceau converti, puis reechantillonne
	sub	sp,sp,64
	mov	x20,x1
	mov	x21,x2
	mov	x22,0				; octets pris
	mov	x1,1
	bl	ak_entry
	cbz	x0,.badf
	mov	x19,x0
	ldr	x23,[x19,AKS_FBYTES]
	ldr	x24,[x19,AKS_RATE]
	movl	x0,SND_RATE_STD			; format natif : copie directe
	cmp	x24,x0
	b.ne	.piece
	ldr	x0,[x19,AKS_CH]
	cmp	x0,2
	b.ne	.piece
	ldr	x0,[x19,AKS_BITS]
	cmp	x0,16
	b.ne	.piece
	mov	x0,x19
	bl	ak_room
	and	x2,x21,not 3
	cmp	x2,x0
	csel	x2,x2,x0,lo
	mov	x22,x2
	cbz	x2,.done
	mov	x0,x19
	mov	x1,x20
	bl	ak_put
	b	.done
.piece:	udiv	x25,x21,x23			; trames d'entree disponibles
	cbz	x25,.done
	mov	x0,x19
	bl	ak_room
	lsr	x26,x0,2			; trames de sortie possibles
	movl	x0,SND_RATE_STD
	cmp	x24,x0
	b.ne	.rs
	mov	x0,AK_PIECE			; meme frequence : trame pour trame
	cmp	x26,x0
	csel	x26,x26,x0,lo
	b	.min
.rs:	cmp	x26,2				; sortie <= m + 1 trames
	b.lo	.done
	sub	x26,x26,1
	mov	x0,AK_PIECE-1
	cmp	x26,x0
	csel	x26,x26,x0,lo
	ldr	x0,[x19,AKS_RS+16]
	mul	x26,x26,x0
	lsr	x26,x26,16
	mov	x0,AK_PIECE
	cmp	x26,x0
	csel	x26,x26,x0,lo
.min:	cmp	x25,x26
	csel	x25,x25,x26,lo
	cbz	x25,.done
	mov	x0,x19				; -> morceau converti (sp)
	mov	x1,x20
	mov	x2,x25
	mov	x3,sp
	bl	ak_convert
	mov	x1,x25
	mov	x0,sp
	movl	x2,SND_RATE_STD
	cmp	x24,x2
	b.eq	.put
	mov	x0,sp				; -> reechantillonne (sp + AK_PIECE*4)
	mov	x1,x25
	add	x2,sp,AK_PIECE*4
	add	x3,x19,AKS_RS
	bl	ak_resample
	mov	x1,x0
	add	x0,sp,AK_PIECE*4
.put:	lsl	x2,x1,2
	mov	x1,x0
	mov	x0,x19
	cbz	x2,.adv
	bl	ak_put
.adv:	mul	x0,x25,x23
	add	x20,x20,x0
	sub	x21,x21,x0
	add	x22,x22,x0
	b	.piece
.badf:	mov	x22,AKE_BADF
.done:	mov	x0,x22
	add	sp,sp,AK_PIECE*8
	add	sp,sp,64
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = flux, x1 = tampon, x2 = octets : tout ecrire (attend la place)
ak_write_all:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0
.w:	cbz	x21,.done
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	ak_write
	tbnz	x0,63,.err
	add	x20,x20,x0
	sub	x21,x21,x0
	add	x22,x22,x0
	cbnz	x0,.w
	mov	x0,x19				; plus rien ne passe : un reste plus
	mov	x1,1				; petit qu'une trame ?
	bl	ak_entry
	cbz	x0,.done
	ldr	x1,[x0,AKS_FBYTES]
	cmp	x21,x1
	b.lo	.done
	mov	x0,5
	sys	SYS_SLEEP
	b	.w
.err:	mov	x22,x0
.done:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = flux -> octets acceptes maintenant (format du flux)
ak_space:
	prologue
	str	x19,[sp,-16]!
	mov	x1,1
	bl	ak_entry
	cbz	x0,.badf
	mov	x19,x0
	bl	ak_room
	lsr	x0,x0,2				; trames 44,1 kHz
	ldr	x1,[x19,AKS_RATE]
	mul	x0,x0,x1
	movl	x1,SND_RATE_STD
	udiv	x0,x0,x1
	ldr	x1,[x19,AKS_FBYTES]
	mul	x0,x0,x1
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	ldr	x19,[sp],16
	epilogue

; x0 = flux -> octets en attente
ak_pending:
	prologue
	mov	x1,1
	bl	ak_entry
	cbz	x0,.badf
	bl	ak_fill
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	epilogue

; x0 = flux -> trames jouees
ak_position:
	prologue
	str	x19,[sp,-16]!
	mov	x1,1
	bl	ak_entry
	cbz	x0,.badf
	mov	x19,x0
	ldr	x1,[x19,AKS_RING]
	ldr	x0,[x1,SR_PLAYED]
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	ldr	x19,[sp],16
	epilogue

; x0 = flux, x1 = volume 0..256
ak_volume:
	prologue
	str	x1,[sp,-16]!
	mov	x1,1
	bl	ak_entry
	ldr	x1,[sp],16
	cbz	x0,.badf
	cmp	x1,256
	mov	x2,256
	csel	x1,x1,x2,ls
	ldr	x2,[x0,AKS_RING]
	cbz	x2,.ok
	str	x1,[x2,SR_VOLUME]
.ok:	mov	x0,0
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	epilogue

; x0 = flux, x1 = octets d'avance (4096..65536)
ak_latency:
	prologue
	str	x1,[sp,-16]!
	mov	x1,1
	bl	ak_entry
	ldr	x1,[sp],16
	cbz	x0,.badf
	mov	x2,4096
	cmp	x1,x2
	csel	x1,x1,x2,hs
	movl	x2,SR_STREAM_BYTES
	cmp	x1,x2
	csel	x1,x1,x2,ls
	and	x1,x1,not 3
	str	x1,[x0,AKS_LAT]
	mov	x0,0
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	epilogue

; x0 = flux, x1 = tampon {en attente, libre, materiel}
ak_status:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	mov	x1,1
	bl	ak_entry
	cbz	x0,.badf
	mov	x19,x0
	bl	ak_fill
	stp	x0,x1,[x20]
	ldr	x1,[x19,AKS_RING]
	ldr	x0,[x1,SR_OUT_TYPE]
	str	x0,[x20,16]
	mov	x0,0
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = flux : attend que tout soit joue (10 s au plus)
ak_drain:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,1000
.w:	mov	x0,x19
	bl	ak_pending
	cmp	x0,0
	b.le	.out
	mov	x0,10
	sys	SYS_SLEEP
	subs	x20,x20,1
	b.ne	.w
.out:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; x0 = flux (lecture ou capture) : fermeture
ak_close:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,512
	mov	x1,1
	mov	x19,x0
	bl	ak_entry
	cbnz	x0,.got
	mov	x0,x19
	mov	x1,2
	bl	ak_entry
	cbz	x0,.badf
.got:	mov	x20,x0
	mov	x0,SND_OP_CLOSE
	ldr	x1,[x20,AKS_ID]
	mov	x2,0
	mov	x3,0
	mov	x4,sp
	bl	ak_req
	ldr	x0,[x20,AKS_BUF]
	sys	SYS_BUF_FREE
	add	x0,x20,AKS_USED
	stlr	xzr,[x0]
	mov	x0,0
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	add	sp,sp,512
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Capture
; ---------------------------------------------------------------------------

; x0 = source -> poignee de capture
ak_rec_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	bl	ak_mode_get
	cbz	x0,.nodev
	mov	x21,x0
	bl	ak_slot
	tbnz	x0,63,.full
	mov	x22,x0
	adr	x20,ak_tab
	add	x20,x20,x22,lsl 7
	str	x21,[x20,AKS_MODE]
	mov	x0,SND_OP_REC_OPEN
	mov	x1,x19
	mov	x2,0
	mov	x3,0
	mov	x4,sp
	bl	ak_req
	tbnz	x0,63,.fail
	str	x0,[x20,AKS_ID]
	ldr	x0,[sp,WM_B]
	str	x0,[x20,AKS_BUF]
	sys	SYS_BUF_MAP
	tbnz	x0,63,.unmap
	str	x0,[x20,AKS_RING]
	mov	x0,2
	add	x1,x20,AKS_USED
	stlr	x0,[x1]
	mov	x0,x22
	b	.out
.unmap:	str	x0,[sp,-16]!
	mov	x0,SND_OP_CLOSE
	ldr	x1,[x20,AKS_ID]
	mov	x2,0
	mov	x3,0
	add	x4,sp,16
	bl	ak_req
	ldr	x0,[sp],16
.fail:	str	xzr,[x20,AKS_USED]
	b	.out
.full:	mov	x0,E_AGAIN
	b	.out
.nodev:	mov	x0,E_NODEV
.out:	add	sp,sp,512
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = capture, x1 = tampon, x2 = octets -> octets lus (non bloquant)
ak_rec_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	mov	x1,2
	bl	ak_entry
	cbz	x0,.badf
	mov	x19,x0
	ldr	x22,[x19,AKS_RING]
	add	x0,x22,SR_HEAD
	ldar	x3,[x0]
	ldr	x4,[x22,SR_TAIL]
	sub	x5,x3,x4			; disponibles
	and	x21,x21,not 3
	cmp	x21,x5
	csel	x21,x21,x5,lo
	add	x6,x22,SR_DATA
	movl	x7,SR_STREAM_BYTES-1
	mov	x8,0
.c:	cmp	x8,x21
	b.hs	.done
	and	x9,x4,x7
	ldr	w10,[x6,x9]
	str	w10,[x20,x8]
	add	x4,x4,4
	add	x8,x8,4
	b	.c
.done:	add	x0,x22,SR_TAIL
	stlr	x4,[x0]
	mov	x0,x21
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = capture, x1 = tampon {disponibles, source, pertes}
ak_rec_status:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x1
	mov	x1,2
	bl	ak_entry
	cbz	x0,.badf
	mov	x19,x0
	ldr	x1,[x19,AKS_RING]
	add	x0,x1,SR_HEAD
	ldar	x0,[x0]
	ldr	x2,[x1,SR_TAIL]
	sub	x0,x0,x2
	ldr	x2,[x1,SR_SOURCE]
	stp	x0,x2,[x20]
	ldr	x0,[x1,SR_LOST]
	str	x0,[x20,16]
	mov	x0,0
	b	.out
.badf:	mov	x0,AKE_BADF
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = capture
ak_rec_close:
	b	ak_close

; ---------------------------------------------------------------------------
;  Outils
; ---------------------------------------------------------------------------

; Reechantillonnage lineaire (s16 stereo), etat {precedente (L | R << 16),
; position 16.16 dans le bloc, pas 16.16} : la trame de sortie a la position
; p interpole entre in[i-1] et in[i] (i = p >> 16, in[-1] = precedente).
; x0 = entree, x1 = trames (> 0), x2 = sortie, x3 = etat -> x0 = trames produites
ak_resample:
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x12,0
	cbz	x1,.ret
	ldr	x9,[x3,8]
	ldr	x10,[x3,16]
	lsl	x11,x1,16			; fin du bloc (16.16)
.o:	cmp	x9,x11
	b.hs	.end
	lsr	x13,x9,16			; i
	and	x14,x9,0xFFFF			; fraction
	cbz	x13,.first
	sub	x15,x13,1
	ldr	w16,[x0,x15,lsl 2]		; in[i-1]
	b	.have
.first:	ldr	w16,[x3]
.have:	ldr	w17,[x0,x13,lsl 2]		; in[i]
	sxth	w19,w16				; gauche
	sxth	w20,w17
	sub	w21,w20,w19
	smull	x21,w21,w14
	asr	x21,x21,16
	add	w19,w19,w21
	asr	w20,w16,16			; droite
	asr	w21,w17,16
	sub	w22,w21,w20
	smull	x22,w22,w14
	asr	x22,x22,16
	add	w20,w20,w22
	and	w19,w19,0xFFFF
	orr	w19,w19,w20,lsl 16
	str	w19,[x2,x12,lsl 2]
	add	x12,x12,1
	add	x9,x9,x10
	b	.o
.end:	sub	x9,x9,x11
	str	x9,[x3,8]
	sub	x13,x1,1
	ldr	w16,[x0,x13,lsl 2]
	str	w16,[x3]
.ret:	mov	x0,x12
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	ret

; x0 = destination, x1 = source, x2 = trames, x3 = gain (0..65536) :
; destination += source * gain, avec saturation
ak_mix_s16:
	lsl	x2,x2,1
	mov	w9,0xFFFF8000
	mov	w10,32767
.s:	cbz	x2,.ret
	ldrsh	w4,[x1],2
	mul	w4,w4,w3
	asr	w4,w4,16
	ldrsh	w5,[x0]
	add	w4,w4,w5
	cmp	w4,w9
	csel	w4,w9,w4,lt
	cmp	w4,w10
	csel	w4,w10,w4,gt
	strh	w4,[x0],2
	sub	x2,x2,1
	b	.s
.ret:	ret

; x0 = tampon, x1 = trames, x2 = gain (0..65536)
ak_gain_s16:
	lsl	x1,x1,1
	mov	w9,0xFFFF8000
	mov	w10,32767
.s:	cbz	x1,.ret
	ldrsh	w4,[x0]
	mul	w4,w4,w2
	asr	w4,w4,16
	cmp	w4,w9
	csel	w4,w9,w4,lt
	cmp	w4,w10
	csel	w4,w10,w4,gt
	strh	w4,[x0],2
	sub	x1,x1,1
	b	.s
.ret:	ret

; x0 = volume 0..100 -> gain 0..65536 (carre : a l'oreille)
ak_volume_curve:
	cmp	x0,100
	mov	x1,100
	csel	x0,x0,x1,ls
	mul	x0,x0,x0
	lsl	x0,x0,16
	movl	x1,10000
	udiv	x0,x0,x1
	ret

; ---------------------------------------------------------------------------
;  Mixer (service sound seulement)
; ---------------------------------------------------------------------------

; -> x0 = tampon d'etat (SS_*), 0 sans service ; redemande a chaque appel
; (sound relance : nouveau tampon)
ak_state:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,512
	bl	ak_mode_get
	cmp	x0,MODE_SVC
	b.ne	.none
	mov	x0,SND_OP_STATE
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,sp
	bl	ak_req
	tbnz	x0,63,.none
	ldr	x19,[sp,WM_B]			; numero du tampon
	adr	x20,ak_stnum
	ldr	x0,[x20]
	cmp	x0,x19
	b.ne	.map
	ldr	x0,[x20,8]			; deja mappe
	cbnz	x0,.out
.map:	mov	x0,x19
	sys	SYS_BUF_MAP
	tbnz	x0,63,.none
	stp	x19,x0,[x20]
	b	.out
.none:	mov	x0,0
.out:	add	sp,sp,512
	ldp	x19,x20,[sp],16
	epilogue

; x0 = op, x1 = a, x2 = b, x3 = c -> resultat (E_NODEV sans service)
ak_mixer_op:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,512
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	ak_mode_get
	cmp	x0,MODE_SVC
	b.ne	.nodev
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,sp
	bl	ak_req
	b	.out
.nodev:	mov	x0,E_NODEV
.out:	add	sp,sp,512
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie, x1 = volume, x2 = muet
ak_out_volume:
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,SND_OP_OUT_VOLUME
	b	ak_mixer_op

; x0 = pid, x1 = volume, x2 = muet
ak_client_volume:
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,SND_OP_CLIENT_VOLUME
	b	ak_mixer_op

; x0 = pid, x1 = sortie
ak_route:
	mov	x3,0
	mov	x2,x1
	mov	x1,x0
	mov	x0,SND_OP_ROUTE
	b	ak_mixer_op

; x0 = sortie
ak_main:
	mov	x3,0
	mov	x2,0
	mov	x1,x0
	mov	x0,SND_OP_MAIN
	b	ak_mixer_op

; ---------------------------------------------------------------------------
;  Formats : memoire et fichiers pour ak_codecs.inc
; ---------------------------------------------------------------------------

; x0 = octets -> x0 = bloc a zero (pages de VM_MAP), 0 si echec
ak_alloc:
	prologue
	str	x19,[sp,-16]!
	add	x19,x0,16
	add	x19,x19,4095
	and	x19,x19,not 4095
	mov	x0,0
	mov	x1,x19
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.fail
	str	x19,[x0]			; taille, pour ak_free
	add	x0,x0,16
	b	.out
.fail:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = bloc (0 : rien)
ak_free:
	cbz	x0,.ret
	sub	x0,x0,16
	ldr	x1,[x0]
	sys	SYS_VM_UNMAP
.ret:	ret

; x0 = chemin -> decodeur (< 0 : erreur)
ak_dec_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	bl	ak_kits
	mov	x0,x19
	mov	x1,sp
	bl	f_stat
	tbnz	x0,63,.out
	ldr	x20,[sp]			; taille
	mov	x0,x19
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x21,x0
	adr	x0,akf_read
	adr	x1,akf_seek
	mov	x2,x21
	mov	x3,x20
	bl	ak_dec_open_io
	tbnz	x0,63,.close
	adr	x1,akf_close
	str	x1,[x0,AKD_IOCLOSE]
	b	.out
.close:	mov	x19,x0
	mov	x0,x21
	bl	f_fermer
	mov	x0,x19
.out:	add	sp,sp,32
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = format AKF_* (WAV, MP3, Opus), x2 = frequence, x3 = canaux,
; x4 = debit (bits/s, 0 : par defaut) -> encodeur (< 0 : erreur) ; le fichier
; est cree (ou vide)
ak_enc_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	bl	ak_kits
	mov	x0,x19
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x24,x0
	adr	x0,akf_write
	adr	x1,akf_seek
	mov	x2,x24
	mov	x3,x20
	mov	x4,x21
	mov	x5,x22
	mov	x6,x23
	bl	ak_enc_open_io
	tbnz	x0,63,.close
	adr	x1,akf_close
	str	x1,[x0,AKN_CLOSEFN]
	b	.out
.close:	mov	x20,x0				; echec : le fichier vide est retire
	mov	x0,x24
	bl	f_fermer
	mov	x0,x19
	bl	f_unlink
	mov	x0,x20
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; fonctions de la source (x0 = descripteur)
akf_write:
	b	f_ecrire
akf_read:
	b	f_lire
akf_seek:
	mov	x2,0
	b	f_position
akf_close:
	b	f_fermer

include 'ak_codecs.inc'
include 'ak_play.inc'

; ---------------------------------------------------------------------------
;  Jeu en direct de la synthese FM : un thread rend les voix dans un flux
; ---------------------------------------------------------------------------
FL_FM		= 0
FL_STREAM	= 8
FL_STOP		= 16
FL_TID		= 24
FL_STACK	= 32
FL_BUF		= 64
FL_FRAMES	= 256
FL_SIZE		= FL_BUF + FL_FRAMES * 4
FL_STACK_SIZE	= 32768
FL_LATENCY	= 8192			; ~46 ms d'avance : on joue au clavier

; x0 = synthe, x1 = sortie (0 : celle du process, -1 : arret) -> 0 / < 0
ak_fm_live:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x21,[x19,AKFM_LIVE]
	tbz	x20,63,.start
	cbz	x21,.ok				; arret
	mov	x0,1
	str	x0,[x21,FL_STOP]
	ldr	x0,[x21,FL_TID]
	sys	SYS_THREAD_JOIN
	ldr	x0,[x21,FL_STREAM]
	bl	ak_close
	ldr	x0,[x21,FL_STACK]
	bl	ak_free
	mov	x0,x21
	bl	ak_free
	str	xzr,[x19,AKFM_LIVE]
	b	.ok
.start:	cbnz	x21,.ok				; deja la
	movl	x0,SND_RATE_STD
	mov	x1,2
	mov	x2,16
	mov	x3,x20
	bl	ak_open
	tbnz	x0,63,.out
	mov	x22,x0
	mov	x1,FL_LATENCY
	bl	ak_latency
	movl	x0,FL_SIZE
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x21,x0
	str	x19,[x21,FL_FM]
	str	x22,[x21,FL_STREAM]
	movl	x0,FL_STACK_SIZE
	bl	ak_alloc
	cbz	x0,.nomem2
	str	x0,[x21,FL_STACK]
	movl	x1,FL_STACK_SIZE
	add	x1,x0,x1
	adr	x0,ak_fm_thread
	mov	x2,x21
	sys	SYS_THREAD_CREATE
	tbnz	x0,63,.nothr
	str	x0,[x21,FL_TID]
	str	x21,[x19,AKFM_LIVE]
.ok:	mov	x0,0
	b	.out
.nothr:	mov	x20,x0
	ldr	x0,[x21,FL_STACK]
	bl	ak_free
	b	.f2
.nomem2: mov	x20,AKE_NOMEM
.f2:	mov	x0,x21
	bl	ak_free
	b	.f3
.nomem:	mov	x20,AKE_NOMEM
.f3:	mov	x0,x22
	bl	ak_close
	mov	x0,x20
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = bloc : rend ce qui sonne, attend la place du flux
ak_fm_thread:
	mov	x19,x0
.loop:	ldr	x0,[x19,FL_STOP]
	cbnz	x0,.exit
	ldr	x0,[x19,FL_FM]
	bl	ak_fm_active
	cbnz	x0,.play
	mov	x0,2
	sys	SYS_SLEEP
	b	.loop
.play:	ldr	x0,[x19,FL_STREAM]
	bl	ak_space
	movl	x1,FL_FRAMES*4
	cmp	x0,x1
	b.ge	.go
	mov	x0,2
	sys	SYS_SLEEP
	b	.loop
.go:	ldr	x0,[x19,FL_FM]
	add	x1,x19,FL_BUF
	mov	x2,FL_FRAMES
	bl	ak_fm_render
	ldr	x0,[x19,FL_STREAM]
	add	x1,x19,FL_BUF
	movl	x2,FL_FRAMES*4
	bl	ak_write
	b	.loop
.exit:	mov	x0,0
	sys	SYS_THREAD_EXIT
	b	.exit

; ---------------------------------------------------------------------------
;  SoundFont sur la carte
; ---------------------------------------------------------------------------

; x0 = chemin -> SoundFont (le fichier est lu en memoire), < 0 : erreur
ak_sf_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	bl	ak_kits
	mov	x0,x19
	mov	x1,sp
	bl	f_stat
	tbnz	x0,63,.out
	ldr	x20,[sp]			; taille
	cmp	x20,12
	b.lo	.bad
	mov	x0,x19
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x0,x20
	bl	ak_alloc
	cbz	x0,.nomem
	mov	x22,x0
	mov	x19,x0				; ecriture
	mov	x23,x20
.rd:	cbz	x23,.eof
	mov	x0,x21
	mov	x1,x19
	mov	x2,x23				; d'un bloc (la boucle : lecture courte)
	bl	f_lire
	cmp	x0,0
	b.le	.rderr
	add	x19,x19,x0
	sub	x23,x23,x0
	b	.rd
.eof:	mov	x0,x21
	bl	f_fermer
	mov	x0,x22
	mov	x1,x20
	bl	sf_parse
	tbnz	x0,63,.fmem
	str	x22,[x0,SF_MEM]
	str	x20,[x0,SF_SIZE_]
	b	.out
.fmem:	mov	x19,x0
	mov	x0,x22
	bl	ak_free
	mov	x0,x19
	b	.out
.rderr:	mov	x0,x22
	bl	ak_free
	mov	x0,AKE_IO
	str	x0,[sp,-16]!
	b	.cl
.nomem:	mov	x0,AKE_NOMEM
	str	x0,[sp,-16]!
.cl:	mov	x0,x21
	bl	f_fermer
	ldr	x0,[sp],16
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = SoundFont -> 0
ak_sf_free:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	cbz	x19,.out
	ldr	x0,[x19,SF_MEM]
	bl	ak_free
	mov	x0,x19
	bl	ak_free
.out:	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; -> SoundFont du systeme, chargee au premier appel ; 0 : aucune
ak_sf_default:
	prologue
	str	x19,[sp,-16]!
	adr	x19,ak_sf_def
	ldr	x0,[x19]
	cmp	x0,1				; 1 : deja cherchee, aucune
	b.eq	.none
	cbnz	x0,.out
	adr	x0,s_sf1
	bl	ak_sf_load
	tbz	x0,63,.got
	adr	x0,s_sf2
	bl	ak_sf_load
	tbz	x0,63,.got
	mov	x0,1
	str	x0,[x19]
.none:	mov	x0,0
	b	.out
.got:	str	x0,[x19]
.out:	ldr	x19,[sp],16
	epilogue

s_sound		db 'sound',0
s_sf1		db 'SD0:/res/soundfonts/GeneralUser-GS.sf2',0
s_sf2		db 'SD0:/res/soundfonts/default.sf2',0
		align 8

segment readable writeable
		align 8
ak_chan		dd 0				; canal du service sound + 1
ak_mode		dd 0				; 0 a choisir, 1 service
ak_stnum	dd -1, 0			; tampon d'etat : numero, adresse
		align 16
ak_tab		rb AK_MAX * AKS_SIZE
kit_data					; import d'AppKit (kits_open)
ak_kits_ok	dd 0				; AppKit ouvert
ak_sf_def	dd 0				; SoundFont du systeme (1 : aucune)
