; ============================================================================
;  tools/audiotest/akhost.asm - banc d'essai des decodeurs d'AudioKit sur le
;  PC : image nue pour QEMU virt (semihosting), sans ARMPI.
;
;  Le meme code que le kit (src/kits/audiokit/ak_*.inc : decodeurs, sans
;  appel systeme) ; ici, la memoire vient d'un simple compteur et les
;  fichiers de l'hote par le semihosting d'ARM (SYS_OPEN, SYS_READ...).
;
;   qemu-system-aarch64 -M virt -cpu cortex-a72 -m 1024 -nographic
;     -semihosting-config enable=on,target=native,arg=akhost,arg=<mode>,
;       arg=<entree>,arg=<sortie> -kernel akhost.elf
;   mode : dec  (ak_dec_open_mem puis ak_dec_read : s16 entrelace)
;          enc  (encodeurs : etape 6)
;  (tools/audiotest/run.py fait tout cela et compare a la reference)
; ============================================================================
format	ELF64 executable 3 at 0x40080000
processor cpu64_v8 +cpu64_fp +cpu64_simd
code64
entry	start

include '../../src/inc/armpi.inc'

AKHOST		= 1			; (les .inc savent qu'ils sont sur le banc)
HOST_IN		= 0x50000000		; fichier d'entree
HOST_OUT	= 0x58000000		; tampon de sortie
HOST_HEAP	= 0x60000000		; tas (ak_alloc)
HOST_STACK	= 0x4FF00000

SH_OPEN		= 0x01
SH_CLOSE	= 0x02
SH_WRITE0	= 0x04
SH_WRITE	= 0x05
SH_READ		= 0x06
SH_FLEN		= 0x0C
SH_CMDLINE	= 0x15
SH_EXIT		= 0x18

segment readable writeable executable

start:
	movl	x0,HOST_STACK
	mov	sp,x0
	mov	x0,3 shl 20			; FP/NEON en EL1
	msr	cpacr_el1,x0
	isb
	; MMU : 1 Go en blocs ; 0-1 Go peripheriques, 1-2 Go memoire normale
	adr	x0,pt_l1
	movl	x1,0x0000000000000401		; bloc, AttrIndx 0 (peripherique), AF
	str	x1,[x0]
	movl	x1,0x0000000040000705		; bloc, AttrIndx 1 (normale), SH interne, AF
	str	x1,[x0,8]
	movl	x1,0x0000000080000401
	str	x1,[x0,16]
	movl	x1,0x00000000C0000401
	str	x1,[x0,24]
	msr	ttbr0_el1,x0
	movl	x0,0xFF00			; attr0 = peripherique nGnRnE, attr1 = WB
	msr	mair_el1,x0
	movl	x0,(32) or (1 shl 8) or (1 shl 10) or (3 shl 12) or (1 shl 23)
	msr	tcr_el1,x0
	isb
	tlbi	vmalle1
	dsb	sy
	isb
	mrs	x0,sctlr_el1
	orr	x0,x0,1				; M
	orr	x0,x0,1 shl 2			; C
	orr	x0,x0,1 shl 12			; I
	bic	x0,x0,2				; A : pas de controle d'alignement
	msr	sctlr_el1,x0
	isb
	mrs	x0,cntvct_el0
	adr	x1,t_start
	str	x0,[x1]

	; ligne de commande : akhost <mode> <entree> <sortie>
	adr	x1,sh_blk
	adr	x0,cmdline
	str	x0,[x1]
	mov	x0,1024
	str	x0,[x1,8]
	mov	w0,SH_CMDLINE
	hlt	0xF000
	adr	x0,cmdline			; decoupe aux espaces
	adr	x9,argv
	mov	x10,0
.sp:	ldrb	w1,[x0]
	cbz	w1,.eol
	cmp	w1,' '
	b.ne	.word
	strb	wzr,[x0],1
	b	.sp
.word:	str	x0,[x9,x10,lsl 3]
	add	x10,x10,1
.wl:	ldrb	w1,[x0]
	cbz	w1,.eol
	cmp	w1,' '
	b.eq	.sp
	add	x0,x0,1
	b	.wl
.eol:	adr	x0,argc
	str	x10,[x0]
	cmp	x10,4
	b.lo	usage
	; entree -> HOST_IN
	adr	x0,argv
	ldr	x0,[x0,16]
	mov	x1,1				; "rb"
	bl	h_open
	tbnz	x0,63,fail_in
	mov	x19,x0
	adr	x1,sh_blk
	str	x19,[x1]
	mov	w0,SH_FLEN
	hlt	0xF000
	mov	x20,x0				; taille
	adr	x1,sh_blk
	stp	x19,xzr,[x1]
	movl	x2,HOST_IN
	str	x2,[x1,8]
	str	x20,[x1,16]
	mov	w0,SH_READ
	hlt	0xF000
	adr	x1,sh_blk
	str	x19,[x1]
	mov	w0,SH_CLOSE
	hlt	0xF000
	adr	x0,in_size
	str	x20,[x0]
	; sortie
	adr	x0,argv
	ldr	x0,[x0,24]
	mov	x1,5				; "wb"
	bl	h_open
	tbnz	x0,63,fail_out
	adr	x1,out_fd
	str	x0,[x1]
	adr	x0,argv
	ldr	x0,[x0,8]
	ldrb	w1,[x0]
	cmp	w1,'d'
	b.eq	mode_dec
	cmp	w1,'s'
	b.eq	mode_dec
	cmp	w1,'m'
	b.eq	mode_mdct
	cmp	w1,'e'
	b.eq	mode_enc
	cmp	w1,'p'
	b.eq	mode_pkt
	b	usage

; --- pkt : fichier AKPK (pktmux.py) -> ak_codec_* -> s16 -------------------------
mode_pkt:
	movl	x19,HOST_IN
	ldr	w0,[x19]
	movl	w1,0x4B504B41			; 'AKPK'
	cmp	w0,w1
	b.ne	usage
	ldr	w0,[x19,4]			; format
	ldr	w1,[x19,8]			; frequence
	ldr	w2,[x19,12]			; canaux
	ldr	w4,[x19,16]			; extradata
	add	x3,x19,20
	add	x20,x3,x4			; premier paquet
	cmp	x4,0
	csel	x3,xzr,x3,eq
	bl	ak_codec_open
	tbnz	x0,63,fail_dec
	mov	x21,x0
	mov	x22,0				; trames
	mov	x23,0				; erreurs
.pk:	ldr	w2,[x20],4
	cmn	w2,1
	b.eq	.end
	mov	x24,x2
	mov	x0,x21
	mov	x1,x20
	movl	x3,HOST_OUT
	mov	x4,8192
	mov	x5,AKC_S16
	bl	ak_codec_decode
	add	x20,x20,x24
	cmp	x0,0
	b.lt	.err
	b.eq	.pk
	add	x22,x22,x0
	mov	x24,x0
	mov	x0,x21
	adr	x1,info
	bl	ak_codec_info
	adr	x1,info
	ldr	x1,[x1,AKI_CHANNELS]
	lsl	x1,x1,1
	mul	x2,x24,x1
	movl	x1,HOST_OUT
	bl	h_write
	b	.pk
.err:	add	x23,x23,1
	b	.pk
.end:	adr	x0,s_frames
	bl	h_puts
	mov	x0,x22
	bl	h_dec
	adr	x0,s_ret
	bl	h_puts
	mov	x0,x23
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
	adr	x0,info
	ldr	x0,[x0,AKI_RATE]
	bl	h_dec
	adr	x0,info
	ldr	x0,[x0,AKI_CHANNELS]
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
	b	finish

; --- enc : e<w|m|o>[kbit/s] : entree WAV (ou autre) -> fichier encode ----------------
mode_enc:
	adr	x0,argv
	ldr	x0,[x0,8]
	ldrb	w1,[x0,1]
	mov	x23,AKF_WAV
	cmp	w1,'m'
	b.ne	.no_m
	mov	x23,AKF_MP3
.no_m:	cmp	w1,'o'
	b.ne	.no_o
	mov	x23,AKF_OPUS
.no_o:	mov	x24,0				; debit
	add	x0,x0,2
.dg:	ldrb	w1,[x0],1
	sub	w1,w1,'0'
	cmp	w1,9
	b.hi	.dgd
	mov	x2,10
	madd	x24,x24,x2,x1
	b	.dg
.dgd:	mov	x2,1000
	mul	x24,x24,x2
	movl	x0,HOST_IN
	adr	x1,in_size
	ldr	x1,[x1]
	bl	ak_dec_open_mem
	tbnz	x0,63,fail_dec
	mov	x19,x0
	mov	x0,x19
	adr	x1,info
	bl	ak_dec_info
	adr	x0,h_enc_write
	adr	x1,h_enc_seek
	adr	x2,out_fd
	ldr	x2,[x2]
	mov	x3,x23
	adr	x4,info
	ldr	x5,[x4,AKI_CHANNELS]
	ldr	x4,[x4,AKI_RATE]
	mov	x6,x24
	bl	ak_enc_open_io
	tbnz	x0,63,fail_dec
	mov	x20,x0
	adr	x0,info
	ldr	x21,[x0,AKI_CHANNELS]
	mov	x22,0
.rd:	mov	x0,x19
	movl	x1,HOST_OUT
	mov	x2,4096
	bl	ak_dec_read
	cmp	x0,0
	b.le	.end
	add	x22,x22,x0
	mov	x2,x0
	movl	x1,HOST_OUT
	mov	x0,x20
	bl	ak_enc_write
	tbnz	x0,63,.end
	b	.rd
.end:	mov	x0,x20
	bl	ak_enc_close
	mov	x23,x0
	adr	x0,s_frames
	bl	h_puts
	mov	x0,x22
	bl	h_dec
	adr	x0,s_ret
	bl	h_puts
	mov	x0,x23
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
	b	finish_noclose

; (contexte = poignee) : ecriture, positionnement (semihosting)
h_enc_write:
	adr	x3,sh_blk
	stp	x0,x1,[x3]
	str	x2,[x3,16]
	mov	x4,x2
	mov	x1,x3
	mov	w0,SH_WRITE
	hlt	0xF000
	sub	x0,x4,x0			; (SYS_WRITE rend ce qui reste)
	ret
h_enc_seek:
	adr	x3,sh_blk
	stp	x0,x1,[x3]
	mov	x1,x3
	mov	w0,0x0A				; SYS_SEEK
	hlt	0xF000
	ret

; --- mdct : entree = N (32 bits) puis N/2 flottants -> N flottants ----------------
mode_mdct:
	mov	x0,HOST_HEAP-0x100000		; (decodeur factice pour akd_alloc)
	movl	x0,0x5F000000
	mov	x1,AKD_BYTES
	bl	ak_memzero
	movl	x0,0x5F000000
	movl	x1,0x5F010000			; plan
	movl	x2,HOST_IN
	ldr	w2,[x2]
	bl	ak_imdct_init
	tbnz	x0,63,fail_dec
	movl	x0,0x5F010000
	movl	x1,HOST_IN+4
	movl	x2,HOST_OUT
	bl	ak_imdct
	movl	x1,HOST_OUT
	movl	x2,HOST_IN
	ldr	w2,[x2]
	lsl	x2,x2,2
	bl	h_write
	b	finish

; --- dec : ak_dec_open_mem + ak_dec_read --------------------------------------
mode_dec:
	movl	x0,HOST_IN
	adr	x1,in_size
	ldr	x1,[x1]
	bl	ak_dec_open_mem
	tbnz	x0,63,fail_dec
	mov	x19,x0
	adr	x0,s_info
	bl	h_puts
	mov	x0,x19
	adr	x1,info
	bl	ak_dec_info
	adr	x1,info
	ldr	x0,[x1,AKI_FORMAT]
	bl	h_dec
	adr	x1,info
	ldr	x0,[x1,AKI_RATE]
	bl	h_dec
	adr	x1,info
	ldr	x0,[x1,AKI_CHANNELS]
	bl	h_dec
	adr	x1,info
	ldr	x0,[x1,AKI_FRAMES]
	bl	h_dec
	adr	x1,info
	ldr	x0,[x1,AKI_BITRATE]
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
	mov	x0,x19				; etiquettes
	mov	x1,AKT_TITLE
	bl	put_tag
	mov	x0,x19
	mov	x1,AKT_ARTIST
	bl	put_tag
	mov	x0,x19
	mov	x1,AKT_ALBUM
	bl	put_tag
	adr	x0,argv				; seek : depuis 1 s (puis 0,5 s plus loin)
	ldr	x0,[x0,8]
	ldrb	w0,[x0]
	cmp	w0,'s'
	b.ne	.noseek
	mov	x0,x19
	mov	x1,1000
	bl	ak_dec_seek
	mov	x20,x0
	adr	x0,s_seek
	bl	h_puts
	mov	x0,x20
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
.noseek:
	adr	x0,info
	ldr	x21,[x0,AKI_CHANNELS]
	lsl	x21,x21,1			; octets par trame
	mov	x22,0				; trames
.rd:	mov	x0,x19
	movl	x1,HOST_OUT
	mov	x2,4096
	bl	ak_dec_read
	cmp	x0,0
	b.le	.end
	add	x22,x22,x0
	mul	x2,x0,x21
	movl	x1,HOST_OUT
	bl	h_write
	b	.rd
.end:	mov	x23,x0
	adr	x0,s_frames
	bl	h_puts
	mov	x0,x22
	bl	h_dec
	adr	x0,s_ret
	bl	h_puts
	mov	x0,x23
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
	mov	x0,x19
	bl	ak_dec_close
	b	finish

; x0 = decodeur, x1 = AKT_*
put_tag:
	prologue
	adr	x2,tagbuf
	mov	x3,256
	bl	ak_dec_tag
	cmp	x0,0
	b.le	.out
	adr	x0,s_tag
	bl	h_puts
	adr	x0,tagbuf
	bl	h_puts
	adr	x0,s_nl
	bl	h_puts
.out:	epilogue

finish:
	adr	x1,out_fd
	ldr	x0,[x1]
	adr	x1,sh_blk
	str	x0,[x1]
	mov	w0,SH_CLOSE
	hlt	0xF000
finish_noclose:
	mrs	x0,cntvct_el0
	adr	x1,t_start
	ldr	x1,[x1]
	sub	x0,x0,x1
	mov	x1,1000
	mul	x0,x0,x1
	mrs	x1,cntfrq_el0
	udiv	x0,x0,x1
	mov	x19,x0
	adr	x0,s_ms
	bl	h_puts
	mov	x0,x19
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
	mov	x0,0
	b	h_exit

usage:	adr	x0,s_usage
	b	die
fail_in:
	adr	x0,s_in
	b	die
fail_out:
	adr	x0,s_out
	b	die
fail_dec:
	mov	x19,x0
	adr	x0,s_fdec
	bl	h_puts
	mov	x0,x19
	bl	h_dec
	adr	x0,s_nl
die:	bl	h_puts
	mov	x0,1
	b	h_exit

; --- semihosting ----------------------------------------------------------------
; x0 = chemin, x1 = mode -> poignee
h_open:
	adr	x2,sh_blk
	str	x0,[x2]
	str	x1,[x2,8]
	mov	x3,0
.l:	ldrb	w4,[x0,x3]
	cbz	w4,.e
	add	x3,x3,1
	b	.l
.e:	str	x3,[x2,16]
	mov	x1,x2
	mov	w0,SH_OPEN
	hlt	0xF000
	ret

; x1 = tampon, x2 = octets -> fichier de sortie
h_write:
	adr	x3,out_fd
	ldr	x3,[x3]
	adr	x4,sh_blk
	stp	x3,x1,[x4]
	str	x2,[x4,16]
	mov	x1,x4
	mov	w0,SH_WRITE
	hlt	0xF000
	ret

; x0 = chaine
h_puts:
	mov	x1,x0
	mov	w0,SH_WRITE0
	hlt	0xF000
	ret

; x0 = nombre signe : ecrit "nombre "
h_dec:
	adr	x1,numbuf+30
	mov	w2,' '
	strb	w2,[x1]
	strb	wzr,[x1,1]
	mov	x3,x0
	cmp	x0,0
	cneg	x0,x0,lt
	mov	x4,10
.d:	udiv	x5,x0,x4
	msub	x6,x5,x4,x0
	add	w6,w6,'0'
	strb	w6,[x1,-1]!
	mov	x0,x5
	cbnz	x0,.d
	tbz	x3,63,.p
	mov	w2,'-'
	strb	w2,[x1,-1]!
.p:	mov	x0,x1
	b	h_puts

; x0 = message, x1 = valeur (akdbg)
h_dbg:
	stp	x29,x30,[sp,-16]!
	str	x1,[sp,-16]!
	bl	h_puts
	ldr	x0,[sp],16
	bl	h_dec
	adr	x0,s_nl
	bl	h_puts
	ldp	x29,x30,[sp],16
	ret

; x0 = code
h_exit:
	adr	x1,sh_blk
	movl	x2,0x20026			; ADP_Stopped_ApplicationExit
	stp	x2,x0,[x1]
	mov	w0,SH_EXIT
	hlt	0xF000
.l:	b	.l

; --- environnement des .inc : memoire -------------------------------------------
; x0 = octets -> x0 = bloc a zero (aligne sur 64), 0 si echec
ak_alloc:
	adr	x1,heap_ptr
	ldr	x2,[x1]
	cbnz	x2,.g
	movl	x2,HOST_HEAP
.g:	add	x0,x0,63
	and	x0,x0,not 63
	add	x3,x2,x0
	str	x3,[x1]
	mov	x0,x2
	ret

; x0 = bloc (rien : le banc ne rend pas la memoire)
ak_free:
	ret

include '../../src/kits/audiokit/ak_codecs.inc'

s_usage	db 'akhost <dec|enc> <entree> <sortie>',10,0
s_in	db 'entree illisible',10,0
s_out	db 'sortie impossible',10,0
s_fdec	db 'ak_dec_open : ',0
s_info	db 'format/frequence/canaux/trames/debit : ',0
s_frames db 'trames decodees : ',0
s_ret	db ' (fin : ',0
s_tag	db 'etiquette : ',0
s_ms	db 'ms : ',0
s_seek	db 'ak_dec_seek(1000) : ',0
s_nl	db 10,0
	align	8
heap_ptr dd 0
t_start	dd 0
in_size	dd 0
out_fd	dd 0
argc	dd 0
argv	rd 8
sh_blk	rd 4
info	rb 128
numbuf	rb 32
tagbuf	rb 256
cmdline	rb 1024
	align	4096
pt_l1	rd 512
