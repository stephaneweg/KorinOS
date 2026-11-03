; sound - le son en service (docs/SON.md, feuille de route section 6), lance
; par init2 (SD0:/etc/services.txt). Service " sound ", agnostique du
; materiel comme netd : il ne touche a aucun registre et ne charge aucun
; pilote. Il tient :
;  * les flux des programmes (AudioKit : ops 1-8), un anneau partage par flux
;    (s16 stereo 44,1 kHz) ;
;  * les sorties : la ou les sorties virtuelles (cadencees par l'horloge,
;    toujours la ; option sound_virtual=2 de SD0:/etc/options.txt : deux,
;    pour essayer le routage) et celles des pilotes (pwmaudio, hdmiaudio,
;    usbaudio : svc/inc/sndif.inc, op 20), un anneau partage par sortie, a la
;    frequence du pilote ; les canaux DMA « lite » (7-14) distribues aux
;    pilotes qui en demandent un ;
;  * les entrees des pilotes de micro (i2smic, usbaudio : op 21) ;
;  * le melange : un thread qui, toutes les 5 ms, remplit chaque anneau de
;    sortie par blocs de 1024 trames (modele « tire » : chaque sortie a son
;    horloge), gain du flux x client x sortie, saturation ; la capture
;    (boucle d'une sortie, micro) ;
;  * la sortie principale (priorite : preferee, USB, HDMI, jack, virtuelle),
;    les volumes et le routage (docs/SON.md 4), le tampon d'etat partage.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/sound.inc'

E_PERM		= -1
E_NOMEM		= -12
E_INVAL		= -22
E_NOSYS		= -38

MSTACK		= 32768			; pile du thread de melange

STR_MAX		= 32			; flux (lecture et capture)
ST_KIND		= 0			; 0 libre, 1 lecture, 2 capture
ST_PID		= 8
ST_BUF		= 16
ST_RING		= 24
ST_OUT		= 32			; lecture : sortie demandee (0 : suit le client) ;
					; capture : source (REC_SRC_*)
ST_CUR		= 40			; lecture : sortie ou il est melange
ST_CLI		= 48			; lecture : son client
STR_SIZE	= 64

CLI_MAX		= SS_CLI_MAX		; process qui jouent
CL_PID		= 0
CL_VOL		= 8			; 0..100
CL_MUTE		= 16
CL_ROUTE	= 24			; sortie choisie (0 : la principale)
CL_PEAK		= 32
CL_NSTR		= 40			; flux de lecture ouverts
CL_CUR		= 48			; sortie ou il joue
CL_NAME		= 64			; 32 o
CL_RNAME	= 96			; sortie choisie, par son nom (32 o ; vide :
CL_SIZE		= 128			;   la principale)

OUT_MAX		= SS_OUT_MAX
OU_ID		= 0			; 0 : place libre
OU_TYPE		= 8
OU_RATE		= 16
OU_PID		= 24			; pilote (0 : sortie virtuelle)
OU_BUF		= 32
OU_RING		= 40
OU_VOL		= 48			; 0..100
OU_MUTE		= 56
OU_DMA		= 64			; canal DMA donne (-1 : aucun)
OU_LEAD		= 72			; avance maximale (octets)
OU_BLK		= 80			; octets d'un bloc a la frequence de la sortie (max.)
OU_RS		= 88			; reechantillonneur (24 o)
OU_VT0		= 112			; virtuelle : instant de depart (ms)
OU_VCONS	= 120			; virtuelle : trames consommees
OU_PEAK		= 128
OU_RANK		= 136			; priorite de la sortie principale
OU_BASE		= 144			; priorite de son type (sans la preference)
OU_NAME		= 160			; 32 o
OUT_SIZE	= 192

IN_MAX		= SS_IN_MAX
IN_ID		= 0
IN_TYPE		= 8
IN_RATE		= 16
IN_PID		= 24
IN_BUF		= 32
IN_RING		= 40
IN_DMA		= 48
IN_RS		= 56			; 24 o
IN_PEAK		= 80
IN_NAME		= 96
IN_SIZE		= 128

LEAD_BLOCKS	= 3			; avance d'une sortie : 3 blocs (~70 ms)
RATE_MAX	= 96000

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so
kit_use '../../kits/audiokit/audiokit.abi'	; outils : ak_resample, ak_volume_curve

start:
	bl	kits_open
	bl	options_read
	bl	dma_mask_read
	bl	settings_load			; SD0:/etc/sound.txt
	sys	SYS_TIME_MS
	adr	x1,t_start
	str	x0,[x1]
	movl	x0,SS_BYTES			; tampon d'etat (lecture seule ailleurs)
	sys	SYS_BUF_CREATE
	tbnz	x0,63,quit
	adr	x1,state_buf
	str	x0,[x1]
	sys	SYS_BUF_MAP
	tbnz	x0,63,quit
	adr	x1,state
	str	x0,[x1]
	movl	x1,SS_BYTES
	bl	zero
	adr	x0,s_sound
	mov	x1,SVF_FINS			; (clients et pilotes qui s'arretent)
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	adr	x19,virt_n			; les sorties virtuelles
	ldr	x19,[x19]
	mov	x20,0
.v:	cmp	x20,x19
	b.hs	.vdone
	mov	x0,SND_OUT_VIRTUAL
	movl	x1,SND_RATE_STD
	mov	x2,0				; (pas de pilote)
	adr	x3,n_virtual
	cbz	x20,.vn
	adr	x3,n_virtual2
.vn:	bl	out_add
	add	x20,x20,1
	b	.v
.vdone:	bl	lock_take
	bl	main_choose
	bl	lock_drop
	bl	state_update
	adr	x0,m_ready
	bl	puts
	adr	x0,virt_n
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_ready2
	bl	puts
	adr	x0,dma_mask
	ldr	x0,[x0]
	bl	print_hex
	adr	x0,m_ready3
	bl	puts
	adr	x0,mix_thread
	bl	spawn_thread
	tbnz	x0,63,quit
.loop:	adr	x0,canal			; requetes
	ldr	x0,[x0]
	adr	x1,msg
	mov	x2,1000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,.err
	cbz	x0,.idle
	mov	x19,x0
	adr	x0,msg
	bl	dispatch
	cbz	x0,.loop			; (avis : pas de reponse)
	mov	x0,x19
	adr	x1,msg
	sys	SYS_SVC_REPLY
	bl	idle_work
	b	.loop
.idle:	bl	idle_work
	b	.loop
.err:	mov	x0,100
	sys	SYS_SLEEP
	b	.loop
quit:	adr	x0,m_fail
	bl	puts
	mov	x0,1				; (init2 relance)
	sys	SYS_EXIT

; Reglages a ecrire (une seconde apres le dernier changement)
idle_work:
	prologue
	adr	x0,dirty
	ldr	x0,[x0]
	cbz	x0,.out
	sys	SYS_TIME_MS
	adr	x1,dirty
	ldr	x1,[x1]
	sub	x0,x0,x1
	cmp	x0,1000
	b.lo	.out
	adr	x1,dirty
	str	xzr,[x1]
	bl	settings_save
.out:	epilogue

; Reglages changes : a ecrire bientot
mark_dirty:
	prologue
	sys	SYS_TIME_MS
	cmp	x0,0
	csinc	x0,x0,xzr,ne
	adr	x1,dirty
	str	x0,[x1]
	epilogue

; x0 = entree : un thread avec sa pile (MSTACK), en priorite haute (le
; melange passe devant les threads normaux) -> x0 = tid, < 0 si echec
spawn_thread:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,MSTACK + 16
	sys	SYS_SBRK
	tbnz	x0,63,.out
	add	x1,x0,MSTACK
	and	x1,x1,0xFFFFFFFFFFFFFFF0
	mov	x0,x19
	mov	x2,0
	mov	x3,0				; TPIDR_EL0
	mov	x4,THREAD_PRIO_HIGH
	adr	x5,s_mix_name
	sys	SYS_THREAD_CREATE_EX
.out:	ldr	x19,[sp],16
	epilogue
s_mix_name	db 'sound-mix',0
		align 4

; ---------------------------------------------------------------------------
;  Verrou des tables (requetes / melange)
; ---------------------------------------------------------------------------
; (les deux gardent tous les registres)
lock_take:
	stp	x0,x1,[sp,-32]!
	stp	x2,x8,[sp,16]
	adr	x0,tab_lock
	mov	w2,1
.t:	ldaxr	w1,[x0]
	cbnz	w1,.busy
	stxr	w1,w2,[x0]
	cbnz	w1,.t
	ldp	x2,x8,[sp,16]
	ldp	x0,x1,[sp],32
	ret
.busy:	clrex
	mov	x8,SYS_YIELD
	svc	0
	adr	x0,tab_lock
	mov	w2,1
	b	.t

lock_drop:
	str	x0,[sp,-16]!
	adr	x0,tab_lock
	stlr	wzr,[x0]
	ldr	x0,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Requetes
; ---------------------------------------------------------------------------

; x0 = message -> x0 = 1 : repondre (resultat en +0)
dispatch:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	str	wzr,[x19,WM_LEN]
	ldr	x0,[x19]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_NAISSANCE
	cmp	x0,x1
	b.eq	.avis
	cmp	x0,SND_OP_DRV_ALIVE
	b.hi	.inval
	adr	x1,op_table
	ldrsw	x1,[x1,x0,lsl 2]
	cbz	x1,.inval
	adr	x2,op_table
	add	x1,x2,x1
	mov	x0,x19
	blr	x1
	b	.rep
.inval:	mov	x0,E_INVAL
.rep:	str	x0,[x19]
	mov	x0,1
	b	.out
.fin:	ldr	x0,[x19,WM_A]			; un process s'arrete
	bl	proc_gone
.avis:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

macro op_entry f {
  if f eq 0
	dw	0
  else
	dw	f - op_table
  end if
}
	align 4
op_table:
	op_entry 0				; 0
	op_entry op_open			; 1
	op_entry op_close
	op_entry op_rec_open
	op_entry op_state
	op_entry op_out_volume			; 5
	op_entry op_client_volume
	op_entry op_route
	op_entry op_main
	op_entry 0
	op_entry 0				; 10
	op_entry 0
	op_entry 0
	op_entry 0
	op_entry 0
	op_entry 0				; 15
	op_entry 0
	op_entry 0
	op_entry 0
	op_entry 0
	op_entry op_drv_output			; 20
	op_entry op_drv_input
	op_entry op_drv_alive

; x0 = taille des donnees -> x0 = tampon partage cree, x1 = son adresse
; (en-tete a zero) ; x0 < 0 si echec
ring_new:
	prologue
	stp	x19,x20,[sp,-16]!
	add	x0,x0,SR_DATA
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x19,x0
	sys	SYS_BUF_MAP
	tbnz	x0,63,.free
	mov	x20,x0
	mov	x1,SR_DATA
	bl	zero
	mov	x0,x19
	mov	x1,x20
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x19
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 1 OPEN : a sortie (0 : celle du client) -> flux ; b tampon ---------------
op_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,WM_PID]
	ldr	x0,[x19,WM_A]			; sortie demandee : presente ?
	cbz	x0,.ok
	bl	out_find
	cbz	x0,.inval
.ok:	movl	x0,SR_STREAM_BYTES
	bl	ring_new
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x22,x1
	mov	x0,256
	str	x0,[x22,SR_VOLUME]
	mov	x0,x21
	mov	x1,x20
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.free
	mov	x0,x20				; son client (cree au besoin)
	bl	client_get
	tbnz	x0,63,.free
	mov	x23,x0
	bl	lock_take
	adr	x2,streams
	mov	x3,0
.t:	ldr	x4,[x2,ST_KIND]
	cbz	x4,.place
	add	x2,x2,STR_SIZE
	add	x3,x3,1
	cmp	x3,STR_MAX
	b.lo	.t
	bl	lock_drop
	mov	x0,E_NOMEM
	b	.free
.place:	mov	x24,x3
	mov	x4,1
	str	x4,[x2,ST_KIND]
	str	x20,[x2,ST_PID]
	str	x21,[x2,ST_BUF]
	str	x22,[x2,ST_RING]
	ldr	x4,[x19,WM_A]
	str	x4,[x2,ST_OUT]
	str	xzr,[x2,ST_CUR]
	str	x23,[x2,ST_CLI]
	adr	x4,clients
	mov	x5,CL_SIZE
	madd	x4,x23,x5,x4
	ldr	x5,[x4,CL_NSTR]
	add	x5,x5,1
	str	x5,[x4,CL_NSTR]
	bl	reroute
	bl	lock_drop
	bl	state_update
	str	x21,[x19,WM_B]
	mov	x0,x24
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x21
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 2 CLOSE : a flux ------------------------------------------------------
op_close:
	prologue
	ldr	x1,[x0,WM_A]
	cmp	x1,STR_MAX
	b.hs	.inval
	ldr	w2,[x0,WM_PID]
	adr	x0,streams
	mov	x3,STR_SIZE
	madd	x0,x1,x3,x0
	ldr	x3,[x0,ST_KIND]
	cbz	x3,.inval
	ldr	x3,[x0,ST_PID]
	cmp	x3,x2
	b.ne	.inval
	bl	stream_close
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	epilogue

; x0 = flux : fermeture (retire sous le verrou, tampon rendu, client oublie
; s'il n'a plus de flux)
stream_close:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	lock_take
	ldr	x0,[x19,ST_KIND]
	str	xzr,[x19,ST_KIND]
	ldr	x20,[x19,ST_BUF]
	cmp	x0,1
	b.ne	.unl
	ldr	x1,[x19,ST_CLI]			; lecture : un flux de moins
	adr	x2,clients
	mov	x3,CL_SIZE
	madd	x2,x1,x3,x2
	ldr	x3,[x2,CL_NSTR]
	subs	x3,x3,1
	csel	x3,xzr,x3,lo
	str	x3,[x2,CL_NSTR]
	cbnz	x3,.unl
	str	xzr,[x2,CL_PID]			; (plus de flux : client oublie)
.unl:	bl	lock_drop
	mov	x0,x20
	sys	SYS_BUF_FREE
	bl	state_update
	ldp	x19,x20,[sp],16
	epilogue

; --- 3 REC_OPEN : a source -> flux de capture ; b tampon ---------------------
op_rec_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	ldr	w20,[x19,WM_PID]
	ldr	x0,[x19,WM_A]
	bl	source_check			; -> source effective (REC_FROM_*)
	tbnz	x0,63,.out
	mov	x23,x0
	movl	x0,SR_STREAM_BYTES
	bl	ring_new
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x22,x1
	str	x23,[x22,SR_SOURCE]
	mov	x0,x21
	mov	x1,x20
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.free
	bl	lock_take
	adr	x2,streams
	mov	x3,0
.t:	ldr	x4,[x2,ST_KIND]
	cbz	x4,.place
	add	x2,x2,STR_SIZE
	add	x3,x3,1
	cmp	x3,STR_MAX
	b.lo	.t
	bl	lock_drop
	mov	x0,E_NOMEM
	b	.free
.place:	mov	x4,2
	str	x4,[x2,ST_KIND]
	str	x20,[x2,ST_PID]
	str	x21,[x2,ST_BUF]
	str	x22,[x2,ST_RING]
	ldr	x4,[x19,WM_A]
	str	x4,[x2,ST_OUT]
	str	xzr,[x2,ST_CUR]
	mov	x23,x3
	bl	lock_drop
	str	x21,[x19,WM_B]
	mov	x0,x23
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x21
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = source -> x0 = source effective (REC_FROM_*), E_NOSYS sans micro,
; E_INVAL si inconnue
source_check:
	prologue
	cmp	x0,REC_SRC_LOOP
	b.eq	.loop
	cbz	x0,.mic
	and	x1,x0,0xFF00
	and	x2,x0,0xFF
	cmp	x1,REC_SRC_LOOP_OUT
	b.eq	.lout
	cmp	x1,REC_SRC_INPUT
	b.ne	.inval
	mov	x0,x2
	bl	in_find
	cbz	x0,.nosys
	ldr	x0,[x0,IN_TYPE]
	b	.out
.lout:	mov	x0,x2
	bl	out_find
	cbz	x0,.inval
.loop:	mov	x0,REC_FROM_LOOP
	b	.out
.mic:	bl	in_first
	cbz	x0,.nosys
	ldr	x0,[x0,IN_TYPE]
	b	.out
.nosys:	mov	x0,E_NOSYS
	b	.out
.inval:	mov	x0,E_INVAL
.out:	epilogue

; --- 4 STATE -> b tampon d'etat ----------------------------------------------
op_state:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,state_buf
	ldr	x0,[x0]
	ldr	w1,[x19,WM_PID]
	sys	SYS_BUF_GRANT			; (deja accorde : sans effet)
	adr	x0,state_buf
	ldr	x0,[x0]
	str	x0,[x19,WM_B]
	mov	x0,0
	ldr	x19,[sp],16
	epilogue

; --- 5 OUT_VOLUME : a sortie, b volume, c muet -------------------------------
op_out_volume:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,WM_A]
	bl	out_find
	cbz	x0,.inval
	mov	x20,x0
	ldr	x1,[x19,WM_B]
	cmp	x1,100
	mov	x2,100
	csel	x1,x1,x2,ls
	ldr	x2,[x19,WM_C]
	cmp	x2,0
	cset	x2,ne
	stp	x1,x2,[x20,OU_VOL]		; (OU_VOL, OU_MUTE contigus)
	add	x0,x20,OU_NAME			; retenu par son nom
	mov	x1,1
	bl	outmem_find
	cbz	x0,.st
	ldp	x1,x2,[x20,OU_VOL]
	stp	x1,x2,[x0,OM_VOL]
	bl	mark_dirty
.st:	bl	state_update
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = message -> x0 = pid vise (a, 0 : l'appelant)
msg_pid:
	ldr	x1,[x0,WM_A]
	ldr	w0,[x0,WM_PID]
	cmp	x1,0
	csel	x0,x0,x1,eq
	ret

; --- 6 CLIENT_VOLUME : a pid (0 : soi), b volume, c muet ---------------------
; (sans flux ouvert : retenu pour son nom, pris a sa prochaine ouverture)
op_client_volume:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	msg_pid
	mov	x21,x0
	ldr	x1,[x19,WM_B]
	cmp	x1,100
	mov	x2,100
	csel	x20,x1,x2,ls
	ldr	x2,[x19,WM_C]
	cmp	x2,0
	cset	x22,ne
	mov	x0,x21
	bl	client_by_pid
	cbz	x0,.mem
	stp	x20,x22,[x0,CL_VOL]		; (CL_VOL, CL_MUTE contigus)
	add	x0,x0,CL_NAME
	b	.save
.mem:	mov	x0,x21				; le nom du process
	adr	x1,namebuf
	bl	proc_name
	adr	x0,namebuf
.save:	ldrb	w1,[x0]
	cbz	w1,.st
	mov	x1,1
	bl	appmem_find
	cbz	x0,.st
	stp	x20,x22,[x0,AM_VOL]
	bl	mark_dirty
.st:	bl	state_update
	mov	x0,0
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 7 ROUTE : a pid (0 : soi), b sortie (0 : la principale) -----------------
op_route:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	msg_pid
	mov	x21,x0
	adr	x20,s_empty			; nom de la sortie (vide : principale)
	ldr	x0,[x19,WM_B]
	cbz	x0,.name
	bl	out_find
	cbz	x0,.inval
	add	x20,x0,OU_NAME
.name:	mov	x0,x21
	bl	client_by_pid
	cbz	x0,.mem
	mov	x22,x0
	bl	lock_take
	add	x0,x22,CL_RNAME
	mov	x1,x20
	mov	x2,32
	bl	str_copy_n
	bl	reroute
	bl	lock_drop
	add	x0,x22,CL_NAME
	b	.save
.mem:	mov	x0,x21
	adr	x1,namebuf
	bl	proc_name
	adr	x0,namebuf
.save:	ldrb	w1,[x0]
	cbz	w1,.st
	mov	x1,1
	bl	appmem_find
	cbz	x0,.st
	add	x0,x0,AM_ROUTE
	mov	x1,x20
	mov	x2,32
	bl	str_copy_n
	bl	mark_dirty
.st:	bl	state_update
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 8 MAIN : a sortie (0 : plus de preferee, priorite des types) -------------
op_main:
	prologue
	str	x19,[sp,-16]!
	ldr	x0,[x0,WM_A]
	adr	x19,s_empty
	cbz	x0,.set
	bl	out_find
	cbz	x0,.inval
	add	x19,x0,OU_NAME
.set:	bl	lock_take
	adr	x0,pref_name
	mov	x1,x19
	mov	x2,32
	bl	str_copy_n
	bl	ranks_update
	bl	main_choose
	bl	reroute
	bl	lock_drop
	bl	mark_dirty
	bl	state_update
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = pid -> x0 = son client, 0 si aucun
client_by_pid:
	adr	x1,clients
	mov	x2,CLI_MAX
.t:	ldr	x3,[x1,CL_PID]
	cmp	x3,x0
	b.eq	.oui
	add	x1,x1,CL_SIZE
	subs	x2,x2,1
	b.ne	.t
.non:	mov	x0,0
	ret
.oui:	cbz	x0,.non
	mov	x0,x1
	ret

; (sous le verrou) OU_RANK = OU_BASE, + 10 pour la preferee (sound.txt) ;
; pref_out : son numero si elle est la
ranks_update:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,pref_out
	str	xzr,[x0]
	adr	x19,outs
	mov	x20,OUT_MAX
.t:	ldr	x0,[x19,OU_ID]
	cbz	x0,.n
	ldr	x0,[x19,OU_BASE]
	str	x0,[x19,OU_RANK]
	adr	x0,pref_name
	ldrb	w1,[x0]
	cbz	w1,.n
	add	x1,x19,OU_NAME
	bl	str_eq
	cbz	x0,.n
	ldr	x0,[x19,OU_RANK]
	add	x0,x0,10
	str	x0,[x19,OU_RANK]
	ldr	x0,[x19,OU_ID]
	adr	x1,pref_out
	str	x0,[x1]
.n:	add	x19,x19,OUT_SIZE
	subs	x20,x20,1
	b.ne	.t
	ldp	x19,x20,[sp],16
	epilogue

; --- 20 DRV_OUTPUT : a type, b frequence, c DMA ; donnees : nom --------------
op_drv_output:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,WM_A]
	cmp	x0,SND_OUT_USB
	b.hi	.inval
	ldr	x1,[x19,WM_B]
	movl	x2,8000
	cmp	x1,x2
	b.lo	.inval
	movl	x2,RATE_MAX
	cmp	x1,x2
	b.hi	.inval
	ldr	w2,[x19,WM_PID]
	add	x3,x19,WM_DATA
	strb	wzr,[x3,31]
	bl	out_add
	tbnz	x0,63,.out
	mov	x20,x0				; l'entree
	mov	x0,-1
	ldr	x1,[x19,WM_C]
	cbz	x1,.nodma
	mov	x0,x1
	bl	dma_take
.nodma:	str	x0,[x20,OU_DMA]
	str	x0,[x19,WM_C]
	ldr	x0,[x20,OU_BUF]
	str	x0,[x19,WM_B]
	bl	lock_take
	bl	main_choose
	bl	reroute
	bl	lock_drop
	bl	state_update
	mov	x0,x20
	bl	out_log
	mov	x0,x20
	mov	x1,1
	bl	out_notify
	ldr	x0,[x20,OU_ID]
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie, x1 = 1 branchee / 0 retiree : avis a l'utilisateur (notifyd),
; pour le HDMI et l'USB, apres les 15 premieres secondes (pas au demarrage)
out_notify:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,OU_TYPE]
	cmp	x0,SND_OUT_HDMI
	b.lo	.out
	sys	SYS_TIME_MS
	adr	x1,t_start
	ldr	x1,[x1]
	sub	x0,x0,x1
	movl	x1,15000
	cmp	x0,x1
	b.lo	.out
	adr	x0,notebuf
	add	x1,x19,OU_NAME
	bl	str_cat				; (-> x0 : la fin)
	adr	x1,t_plugged
	cbnz	x20,.cat
	adr	x1,t_unplugged
.cat:	bl	str_cat
	mov	x20,x0
	adr	x0,main_out			; et ou va le son
	ldr	x0,[x0]
	bl	out_find
	cbz	x0,.send
	add	x19,x0,OU_NAME
	mov	x0,x20
	adr	x1,t_on
	bl	str_cat
	mov	x1,x19
	bl	str_cat
.send:	adr	x0,t_sound
	adr	x1,notebuf
	bl	notifier
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- 21 DRV_INPUT : a type, b frequence, c DMA ; donnees : nom ---------------
op_drv_input:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,WM_B]
	movl	x2,8000
	cmp	x1,x2
	b.lo	.inval
	movl	x2,RATE_MAX
	cmp	x1,x2
	b.hi	.inval
	movl	x0,SR_IN_BYTES
	bl	ring_new
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x22,x1
	ldr	x0,[x19,WM_B]
	str	x0,[x22,SR_RATE]
	mov	x0,x21
	ldr	w1,[x19,WM_PID]
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.free
	bl	lock_take
	adr	x20,inputs
	mov	x3,0
.t:	ldr	x4,[x20,IN_ID]
	cbz	x4,.place
	add	x20,x20,IN_SIZE
	add	x3,x3,1
	cmp	x3,IN_MAX
	b.lo	.t
	bl	lock_drop
	mov	x0,E_NOMEM
	b	.free
.place:	adr	x0,next_in			; numero d'entree : jamais repris
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	str	x1,[x20,IN_ID]
	ldr	x0,[x19,WM_A]
	str	x0,[x20,IN_TYPE]
	ldr	x0,[x19,WM_B]
	str	x0,[x20,IN_RATE]
	ldr	w0,[x19,WM_PID]
	str	x0,[x20,IN_PID]
	str	x21,[x20,IN_BUF]
	str	x22,[x20,IN_RING]
	str	xzr,[x20,IN_RS]
	str	xzr,[x20,IN_RS+8]
	ldr	x0,[x19,WM_B]			; pas : frequence / 44 100 (16.16)
	lsl	x0,x0,16
	movl	x1,SND_RATE_STD
	udiv	x0,x0,x1
	str	x0,[x20,IN_RS+16]
	add	x0,x20,IN_NAME
	add	x1,x19,WM_DATA
	mov	x2,32
	bl	str_copy_n
	bl	lock_drop
	mov	x0,-1
	ldr	x1,[x19,WM_C]
	cbz	x1,.nodma
	mov	x0,x1
	bl	dma_take
.nodma:	str	x0,[x20,IN_DMA]
	str	x0,[x19,WM_C]
	str	x21,[x19,WM_B]
	bl	state_update
	adr	x0,m_in
	bl	puts
	ldr	x0,[x20,IN_ID]
	bl	print_dec
	adr	x0,m_guil
	bl	puts
	add	x0,x20,IN_NAME
	bl	puts
	adr	x0,m_guil2
	bl	puts
	ldr	x0,[x20,IN_PID]
	bl	print_dec
	adr	x0,m_hz_sep
	bl	puts
	ldr	x0,[x20,IN_RATE]
	bl	print_dec
	adr	x0,m_hz
	bl	puts
	ldr	x0,[x20,IN_DMA]
	bl	dma_log
	ldr	x0,[x20,IN_ID]
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x21
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- 22 DRV_ALIVE : a sortie (ou entree | SND_ID_INPUT) -> 0, E_INVAL ---------
op_drv_alive:
	prologue
	str	x19,[sp,-16]!
	ldr	w19,[x0,WM_PID]
	ldr	x0,[x0,WM_A]
	tst	x0,SND_ID_INPUT
	b.ne	.in
	bl	out_find
	cbz	x0,.inval
	ldr	x1,[x0,OU_PID]
	b	.chk
.in:	and	x0,x0,SND_ID_INPUT-1
	bl	in_find
	cbz	x0,.inval
	ldr	x1,[x0,IN_PID]
.chk:	cmp	x1,x19
	b.ne	.inval
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Sorties, entrees, clients
; ---------------------------------------------------------------------------

; x0 = type, x1 = frequence, x2 = pid du pilote (0 : virtuelle), x3 = nom
; -> x0 = entree de la sortie (anneau cree et accorde), < 0 si echec
out_add:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	movl	x0,SR_OUT_BYTES
	bl	ring_new
	tbnz	x0,63,.out
	mov	x23,x0
	mov	x24,x1
	str	x20,[x24,SR_RATE]
	cbz	x21,.mine
	mov	x0,x23
	mov	x1,x21
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.free
.mine:	bl	lock_take
	adr	x9,outs
	mov	x3,0
.t:	ldr	x4,[x9,OU_ID]
	cbz	x4,.place
	add	x9,x9,OUT_SIZE
	add	x3,x3,1
	cmp	x3,OUT_MAX
	b.lo	.t
	bl	lock_drop
	mov	x0,E_NOMEM
	b	.free
.place:	mov	x0,x9
	mov	x1,OUT_SIZE
	bl	zero
	adr	x0,next_out			; numero de sortie : jamais repris
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	str	x19,[x9,OU_TYPE]
	str	x20,[x9,OU_RATE]
	str	x21,[x9,OU_PID]
	str	x23,[x9,OU_BUF]
	str	x24,[x9,OU_RING]
	mov	x0,100
	str	x0,[x9,OU_VOL]
	mov	x0,-1
	str	x0,[x9,OU_DMA]
	movl	x2,SND_RATE_STD			; pas : 44 100 / frequence (16.16)
	lsl	x0,x2,16
	udiv	x0,x0,x20
	str	x0,[x9,OU_RS+16]
	mov	x0,SND_BLOCK			; un bloc a la frequence de la sortie
	mul	x0,x0,x20
	udiv	x0,x0,x2
	add	x0,x0,2
	lsl	x0,x0,2
	str	x0,[x9,OU_BLK]
	mov	x2,LEAD_BLOCKS
	mul	x0,x0,x2
	str	x0,[x9,OU_LEAD]
	mov	x0,1				; priorite (sortie principale)
	cmp	x19,SND_OUT_PWM
	b.ne	.r1
	mov	x0,2
.r1:	cmp	x19,SND_OUT_HDMI
	b.ne	.r2
	mov	x0,3
.r2:	cmp	x19,SND_OUT_USB
	b.ne	.r3
	mov	x0,4
.r3:	cmp	x19,SND_OUT_VIRTUAL		; seconde virtuelle : apres la premiere
	b.ne	.r4
	cmp	x1,1
	b.eq	.r4
	mov	x0,0
.r4:	str	x0,[x9,OU_RANK]
	str	x0,[x9,OU_BASE]
	mov	x19,x9
	str	x1,[sp,-16]!
	sys	SYS_TIME_MS
	str	x0,[x19,OU_VT0]
	add	x0,x19,OU_NAME
	mov	x1,x22
	mov	x2,32
	bl	str_copy_n
	add	x0,x19,OU_NAME			; volume retenu (sound.txt)
	mov	x1,0
	bl	outmem_find
	cbz	x0,.nomem
	ldp	x1,x2,[x0,OM_VOL]
	stp	x1,x2,[x19,OU_VOL]
.nomem:	ldr	x1,[sp],16
	str	x1,[x19,OU_ID]			; (en dernier : visible du melange)
	bl	ranks_update
	bl	lock_drop
	mov	x0,x19
	b	.out
.free:	str	x0,[sp,-16]!
	mov	x0,x23
	sys	SYS_BUF_FREE
	ldr	x0,[sp],16
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sortie : au journal
out_log:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,m_out
	bl	puts
	ldr	x0,[x19,OU_ID]
	bl	print_dec
	adr	x0,m_guil
	bl	puts
	add	x0,x19,OU_NAME
	bl	puts
	adr	x0,m_guil2
	bl	puts
	ldr	x0,[x19,OU_PID]
	bl	print_dec
	adr	x0,m_hz_sep
	bl	puts
	ldr	x0,[x19,OU_RATE]
	bl	print_dec
	adr	x0,m_hz
	bl	puts
	ldr	x0,[x19,OU_DMA]
	bl	dma_log
	ldr	x19,[sp],16
	epilogue

; x0 = canal DMA (-1 : aucun) : suite de la ligne du journal
dma_log:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	tbnz	x19,63,.nl
	adr	x0,m_dma
	bl	puts
	mov	x0,x19
	bl	print_dec
.nl:	adr	x0,m_nl
	bl	puts
	ldr	x19,[sp],16
	epilogue

; x0 = numero de sortie -> x0 = son entree, 0 si absente
out_find:
	cbz	x0,.non
	adr	x1,outs
	mov	x2,OUT_MAX
.t:	ldr	x3,[x1,OU_ID]
	cmp	x3,x0
	b.eq	.oui
	add	x1,x1,OUT_SIZE
	subs	x2,x2,1
	b.ne	.t
.non:	mov	x0,0
	ret
.oui:	mov	x0,x1
	ret

; x0 = nom -> x0 = la sortie de ce nom, 0 si absente (ou nom vide)
out_by_name:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	ldrb	w0,[x21]
	cbz	w0,.non
	adr	x19,outs
	mov	x20,OUT_MAX
.t:	ldr	x0,[x19,OU_ID]
	cbz	x0,.n
	add	x0,x19,OU_NAME
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.oui
.n:	add	x19,x19,OUT_SIZE
	subs	x20,x20,1
	b.ne	.t
.non:	mov	x0,0
	b	.out
.oui:	mov	x0,x19
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero d'entree -> x0 = son entree, 0 si absente
in_find:
	cbz	x0,.non
	adr	x1,inputs
	mov	x2,IN_MAX
.t:	ldr	x3,[x1,IN_ID]
	cmp	x3,x0
	b.eq	.oui
	add	x1,x1,IN_SIZE
	subs	x2,x2,1
	b.ne	.t
.non:	mov	x0,0
	ret
.oui:	mov	x0,x1
	ret

; -> x0 = le micro par defaut : l'entree USB s'il y en a une, sinon la
; premiere ; 0 si aucune
in_first:
	adr	x1,inputs
	mov	x2,IN_MAX
	mov	x0,0
.t:	ldr	x3,[x1,IN_ID]
	cbz	x3,.n
	cbnz	x0,.usb
	mov	x0,x1
.usb:	ldr	x3,[x1,IN_TYPE]
	cmp	x3,SND_IN_USB
	b.ne	.n
	mov	x0,x1
	ret
.n:	add	x1,x1,IN_SIZE
	subs	x2,x2,1
	b.ne	.t
	ret

; (sous le verrou) la sortie principale : la plus haute priorite presente
; (OU_RANK ; la preferee de sound.txt passe devant) ; au journal si elle change
main_choose:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,0				; meilleure
	mov	x20,-1				; sa priorite
	adr	x1,outs
	mov	x2,OUT_MAX
.t:	ldr	x3,[x1,OU_ID]
	cbz	x3,.n
	ldr	x4,[x1,OU_RANK]
	cmp	x4,x20
	b.le	.n
	mov	x20,x4
	mov	x19,x3
.n:	add	x1,x1,OUT_SIZE
	subs	x2,x2,1
	b.ne	.t
	adr	x0,main_out
	ldr	x1,[x0]
	cmp	x1,x19
	b.eq	.out
	str	x19,[x0]
	adr	x0,m_main
	bl	puts
	mov	x0,x19
	bl	print_dec
	mov	x0,x19
	bl	out_find
	cbz	x0,.nl
	add	x19,x0,OU_NAME
	adr	x0,m_guil
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_guilf
	bl	puts
.nl:	adr	x0,m_nl
	bl	puts
.out:	ldp	x19,x20,[sp],16
	epilogue

; (sous le verrou) chaque flux de lecture va sur sa sortie : celle demandee a
; l'ouverture si elle est la, sinon celle du client (routage), sinon la
; principale. Une sortie demandee qui a disparu : etat « sortie perdue ».
reroute:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,streams
	mov	x20,STR_MAX
.s:	ldr	x0,[x19,ST_KIND]
	cmp	x0,1
	b.ne	.n
	ldr	x21,[x19,ST_RING]
	mov	x22,0				; (etat du flux)
	ldr	x0,[x19,ST_OUT]
	cbz	x0,.cli
	bl	out_find
	cbnz	x0,.got
	mov	x22,1				; sortie perdue
.cli:	ldr	x1,[x19,ST_CLI]
	adr	x2,clients
	mov	x3,CL_SIZE
	madd	x2,x1,x3,x2
	str	xzr,[x2,CL_ROUTE]
	add	x0,x2,CL_RNAME			; sortie choisie (par son nom) : la ?
	str	x2,[sp,-16]!
	bl	out_by_name
	ldr	x2,[sp],16
	cbz	x0,.main
	ldr	x1,[x0,OU_ID]
	str	x1,[x2,CL_ROUTE]
	b	.got
.main:
	adr	x0,main_out
	ldr	x0,[x0]
	bl	out_find
	cbz	x0,.none
.got:	ldr	x1,[x0,OU_ID]
	ldr	x2,[x0,OU_TYPE]
	str	x1,[x19,ST_CUR]
	str	x1,[x21,SR_OUTPUT]
	str	x2,[x21,SR_OUT_TYPE]
	ldr	x1,[x19,ST_CLI]			; le client joue la
	adr	x2,clients
	mov	x3,CL_SIZE
	madd	x2,x1,x3,x2
	ldr	x1,[x0,OU_ID]
	str	x1,[x2,CL_CUR]
	b	.st
.none:	str	xzr,[x19,ST_CUR]
	str	xzr,[x21,SR_OUTPUT]
.st:	str	x22,[x21,SR_STATE]
.n:	add	x19,x19,STR_SIZE
	subs	x20,x20,1
	b.ne	.s
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid -> x0 = numero de son client (cree au besoin, nom du process),
; < 0 si la table est pleine
client_get:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	lock_take
	adr	x1,clients
	mov	x2,0
	mov	x3,-1				; premiere place libre
.t:	ldr	x4,[x1,CL_PID]
	cmp	x4,x19
	b.eq	.found
	cbnz	x4,.n
	tbz	x3,63,.n
	mov	x3,x2
.n:	add	x1,x1,CL_SIZE
	add	x2,x2,1
	cmp	x2,CLI_MAX
	b.lo	.t
	tbnz	x3,63,.full
	adr	x20,clients
	mov	x4,CL_SIZE
	madd	x20,x3,x4,x20
	mov	x21,x3
	mov	x0,x20
	mov	x1,CL_SIZE
	bl	zero
	str	x19,[x20,CL_PID]
	mov	x0,100
	str	x0,[x20,CL_VOL]
	bl	lock_drop
	mov	x0,x19				; son nom (liste des process)
	add	x1,x20,CL_NAME
	bl	proc_name
	add	x0,x20,CL_NAME			; volume et sortie retenus (sound.txt)
	mov	x1,0
	bl	appmem_find
	cbz	x0,.new
	mov	x19,x0
	bl	lock_take
	ldp	x0,x1,[x19,AM_VOL]
	stp	x0,x1,[x20,CL_VOL]
	add	x0,x20,CL_RNAME
	add	x1,x19,AM_ROUTE
	mov	x2,32
	bl	str_copy_n
	bl	lock_drop
.new:	mov	x0,x21
	b	.out
.found:	mov	x0,x2
	bl	lock_drop
	b	.out
.full:	bl	lock_drop
	mov	x0,E_NOMEM
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid, x1 = destination (32 o) : nom du process (liste des process)
proc_name:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	strb	wzr,[x20]
	adr	x0,plist
	mov	x1,64
	sys	SYS_PROCLIST
	cmp	x0,0
	b.le	.out
	adr	x1,plist
.t:	ldr	x2,[x1]
	cmp	x2,x19
	b.eq	.got
	add	x1,x1,64
	subs	x0,x0,1
	b.ne	.t
	b	.out
.got:	mov	x0,x20
	add	x1,x1,24
	mov	x2,32
	bl	str_copy_n
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid termine : ses flux, sa capture, ses sorties et entrees (pilote)
proc_gone:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	adr	x19,streams			; flux
	mov	x20,STR_MAX
.s:	ldr	x0,[x19,ST_KIND]
	cbz	x0,.sn
	ldr	x0,[x19,ST_PID]
	cmp	x0,x21
	b.ne	.sn
	mov	x0,x19
	bl	stream_close
.sn:	add	x19,x19,STR_SIZE
	subs	x20,x20,1
	b.ne	.s
	adr	x19,outs			; sorties de ce pilote
	mov	x20,OUT_MAX
.o:	ldr	x0,[x19,OU_ID]
	cbz	x0,.on
	ldr	x0,[x19,OU_PID]
	cmp	x0,x21
	b.ne	.on
	bl	lock_take
	ldr	x22,[x19,OU_ID]
	str	xzr,[x19,OU_ID]
	bl	main_choose
	bl	reroute
	bl	lock_drop
	ldr	x0,[x19,OU_BUF]
	sys	SYS_BUF_FREE
	ldr	x0,[x19,OU_DMA]
	bl	dma_give
	adr	x0,m_out
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_gone
	bl	puts
	bl	state_update
	mov	x0,x19				; (le nom reste dans l'entree)
	mov	x1,0
	bl	out_notify
.on:	add	x19,x19,OUT_SIZE
	subs	x20,x20,1
	b.ne	.o
	adr	x19,inputs			; entrees de ce pilote
	mov	x20,IN_MAX
.i:	ldr	x0,[x19,IN_ID]
	cbz	x0,.in
	ldr	x0,[x19,IN_PID]
	cmp	x0,x21
	b.ne	.in
	bl	lock_take
	ldr	x22,[x19,IN_ID]
	str	xzr,[x19,IN_ID]
	bl	lock_drop
	ldr	x0,[x19,IN_BUF]
	sys	SYS_BUF_FREE
	ldr	x0,[x19,IN_DMA]
	bl	dma_give
	adr	x0,m_in
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_gone
	bl	puts
	bl	state_update
.in:	add	x19,x19,IN_SIZE
	subs	x20,x20,1
	b.ne	.i
	adr	x19,clients			; son client (sans flux : reste)
	mov	x20,CLI_MAX
	bl	lock_take
.c:	ldr	x0,[x19,CL_PID]
	cmp	x0,x21
	b.ne	.cn
	str	xzr,[x19,CL_PID]
.cn:	add	x19,x19,CL_SIZE
	subs	x20,x20,1
	b.ne	.c
	bl	lock_drop
	bl	state_update
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Canaux DMA « lite » (7-14) : distribues aux pilotes (docs/SON.md 2.1)
; ---------------------------------------------------------------------------

; Masque du firmware (GET_DMA_CHANNELS), 0x7F35 a defaut
dma_mask_read:
	prologue
	adr	x0,fw_dma
	sys	SYS_FW_CALL
	adr	x1,fw_dma
	ldr	w1,[x1,20]
	tbnz	x0,63,.def
	cbnz	w1,.set
.def:	movl	w1,0x7F35
.set:	adr	x0,dma_mask
	str	x1,[x0]
	epilogue

; x0 = demande (1 : n'importe lequel ; 0x100 + n : le canal n s'il est
; libre, celui d'un pilote qui se reinscrit) -> x0 = canal libre (7-14 dans
; le masque du firmware), -1 si aucun ; d'abord un canal seul sur sa ligne
; d'interruption (7-8 et 9-10 la partagent, sndif.inc)
dma_take:
	adr	x1,dma_mask
	ldr	x1,[x1]
	adr	x2,dma_used
	ldr	x3,[x2]
	cmp	x0,0x100
	b.lo	.any
	sub	x0,x0,0x100
	cmp	x0,7
	b.lo	.any
	cmp	x0,14
	b.hi	.any
	lsrv	x4,x1,x0
	tbz	x4,0,.any
	lsrv	x4,x3,x0
	tbz	x4,0,.take
.any:	mov	x5,1
.pass:	mov	x0,7
.t:	cmp	x0,14
	b.hi	.next
	lsrv	x4,x1,x0
	tbz	x4,0,.n
	lsrv	x4,x3,x0
	tbnz	x4,0,.n
	cbz	x5,.take
	cmp	x0,10
	b.hi	.take
	sub	x4,x0,7				; l'autre canal de la ligne
	eor	x4,x4,1
	add	x4,x4,7
	lsrv	x4,x3,x4
	tbnz	x4,0,.n
.take:	mov	x4,1
	lslv	x4,x4,x0
	orr	x3,x3,x4
	str	x3,[x2]
	ret
.n:	add	x0,x0,1
	b	.t
.next:	subs	x5,x5,1
	b.eq	.pass
	mov	x0,-1
	ret

; x0 = canal rendu (-1 : rien)
dma_give:
	tbnz	x0,63,.ret
	adr	x2,dma_used
	ldr	x3,[x2]
	mov	x4,1
	lslv	x4,x4,x0
	bic	x3,x3,x4
	str	x3,[x2]
.ret:	ret

; ---------------------------------------------------------------------------
;  Tampon d'etat (SS_*) : recopie des tables, generation + 1
; ---------------------------------------------------------------------------
state_update:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	lock_take
	adr	x0,state
	ldr	x19,[x0]
	adr	x0,main_out
	ldr	x0,[x0]
	str	x0,[x19,SS_MAIN]
	adr	x20,outs			; sorties
	add	x21,x19,SS_OUTS
	mov	x22,0				; nombre
	mov	x9,OUT_MAX
.o:	ldr	x0,[x20,OU_ID]
	str	w0,[x21,SO_ID]
	cbz	x0,.on
	add	x22,x22,1
	ldr	x1,[x20,OU_TYPE]
	str	w1,[x21,SO_TYPE]
	ldr	x1,[x20,OU_RATE]
	str	w1,[x21,SO_RATE]
	ldr	x1,[x20,OU_VOL]
	str	w1,[x21,SO_VOLUME]
	ldr	x1,[x20,OU_MUTE]
	str	w1,[x21,SO_MUTE]
	adr	x1,main_out
	ldr	x1,[x1]
	cmp	x1,x0
	cset	x1,eq
	adr	x2,pref_out
	ldr	x2,[x2]
	cmp	x2,x0
	cset	x2,eq
	orr	x1,x1,x2,lsl 1
	str	w1,[x21,SO_FLAGS]
	add	x1,x20,OU_NAME
	add	x2,x21,SO_NAME
	mov	x3,32
.cn:	ldrb	w4,[x1],1
	strb	w4,[x2],1
	subs	x3,x3,1
	b.ne	.cn
.on:	add	x20,x20,OUT_SIZE
	add	x21,x21,64
	subs	x9,x9,1
	b.ne	.o
	str	x22,[x19,SS_NOUT]
	adr	x20,clients			; clients
	add	x21,x19,SS_CLIS
	mov	x22,0
	mov	x9,CLI_MAX
.c:	ldr	x0,[x20,CL_PID]
	str	w0,[x21,SC_PID]
	cbz	x0,.cn2
	add	x22,x22,1
	ldr	x1,[x20,CL_VOL]
	str	w1,[x21,SC_VOLUME]
	ldr	x1,[x20,CL_MUTE]
	str	w1,[x21,SC_MUTE]
	ldr	x1,[x20,CL_ROUTE]
	str	w1,[x21,SC_ROUTE]
	ldr	x1,[x20,CL_CUR]
	str	w1,[x21,SC_OUTPUT]
	ldr	x1,[x20,CL_NSTR]
	str	w1,[x21,SC_STREAMS]
	add	x1,x20,CL_NAME
	add	x2,x21,SC_NAME
	mov	x3,32
.cc:	ldrb	w4,[x1],1
	strb	w4,[x2],1
	subs	x3,x3,1
	b.ne	.cc
.cn2:	add	x20,x20,CL_SIZE
	add	x21,x21,64
	subs	x9,x9,1
	b.ne	.c
	str	x22,[x19,SS_NCLI]
	adr	x20,inputs			; entrees
	movl	x21,SS_INS
	add	x21,x19,x21
	mov	x22,0
	mov	x9,IN_MAX
.i:	ldr	x0,[x20,IN_ID]
	str	w0,[x21,SO_ID]
	cbz	x0,.inn
	add	x22,x22,1
	ldr	x1,[x20,IN_TYPE]
	str	w1,[x21,SO_TYPE]
	ldr	x1,[x20,IN_RATE]
	str	w1,[x21,SO_RATE]
	add	x1,x20,IN_NAME
	add	x2,x21,SO_NAME
	mov	x3,32
.ci:	ldrb	w4,[x1],1
	strb	w4,[x2],1
	subs	x3,x3,1
	b.ne	.ci
.inn:	add	x20,x20,IN_SIZE
	add	x21,x21,64
	subs	x9,x9,1
	b.ne	.i
	str	x22,[x19,SS_NIN]
	ldr	x0,[x19,SS_GEN]			; (en dernier : les lecteurs comparent)
	add	x0,x0,1
	add	x1,x19,SS_GEN
	stlr	x0,[x1]
	bl	lock_drop
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Options (SD0:/etc/options.txt)
; ---------------------------------------------------------------------------

; sound_virtual=N (1 ou 2 sorties virtuelles)
options_read:
	prologue
	adr	x0,s_opt_virtual
	bl	option_value
	tbnz	x0,63,.out
	cmp	x0,2
	b.ne	.out
	adr	x1,virt_n
	str	x0,[x1]
.out:	epilogue

; x0 = cle (« sound_virtual= ») -> x0 = valeur decimale de la ligne qui
; commence par la cle, -1 si absente
option_value:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,x0
	adr	x0,p_options
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.non
	mov	x19,x0
	adr	x1,optbuf
	mov	x2,1023
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.non
	adr	x0,optbuf
	strb	wzr,[x0,x20]
.ligne:	mov	x2,0
.c:	ldrb	w3,[x21,x2]
	cbz	w3,.oui
	ldrb	w4,[x0,x2]
	cmp	w3,w4
	b.ne	.suiv
	add	x2,x2,1
	b	.c
.suiv:	ldrb	w3,[x0],1
	cbz	w3,.non
	cmp	w3,10
	b.ne	.suiv
	b	.ligne
.oui:	add	x0,x0,x2
	bl	parse_dec
	b	.out
.non:	mov	x0,-1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Outils
; ---------------------------------------------------------------------------

; x0 = valeur : en hexadecimal sur la console
print_hex:
	prologue
	sub	sp,sp,32
	add	x1,sp,17
	strb	wzr,[x1]
	mov	x2,x0
.c:	and	x3,x2,15
	cmp	x3,10
	add	x4,x3,'0'
	add	x5,x3,'A'-10
	csel	x3,x4,x5,lo
	sub	x1,x1,1
	strb	w3,[x1]
	lsr	x2,x2,4
	cbnz	x2,.c
	mov	x0,x1
	bl	puts
	add	sp,sp,32
	epilogue

; x0 = adresse, x1 = octets (multiple de 8)
zero:
	cbz	x1,.ret
	str	xzr,[x0],8
	subs	x1,x1,8
	b.hi	zero
.ret:	ret

include 'mix.inc'
include 'settings.inc'
include '../../inc/notify.inc'
include '../../inc/applist.inc'			; ini_read, ini_field

s_sound		db 'sound',0
s_opt_virtual	db 'sound_virtual=',0
p_options	db 'SD0:/etc/options.txt',0
n_virtual	db 'Sortie virtuelle',0
n_virtual2	db 'Sortie virtuelle 2',0
m_ready		db 'sound : en service (sorties virtuelles : ',0
m_ready2	db ', blocs de 1024 trames, canaux DMA du firmware 0x',0
m_ready3	db ' : lite 7-14 aux pilotes)',10,0
m_fail		db 'sound : d',0xC3,0xA9,'marrage impossible',10,0
m_out		db 'sound : sortie ',0
m_in		db 'sound : entr',0xC3,0xA9,'e ',0
m_guil		db ' ',0xC2,0xAB,' ',0
m_guilf		db ' ',0xC2,0xBB,0
m_guil2		db ' ',0xC2,0xBB,' (pilote ',0
m_hz_sep	db '), ',0
m_hz		db ' Hz',0
m_dma		db ', canal DMA ',0
m_gone		db ' partie',10,0
m_main		db 'sound : sortie principale : ',0
m_nl		db 10,0
s_empty		db 0
t_sound		db 'Son',0
t_plugged	db ' : branch',0xC3,0xA9,0
t_unplugged	db ' : retir',0xC3,0xA9,0
t_on		db ', son sur ',0
		align 8

segment readable writeable
kit_data
applis_data
		align 16
fw_dma		dw 28, 0, 0x00060001, 4, 0, 0, 0	; GET_DMA_CHANNELS
		align 8
canal		dd 0
tab_lock	dd 0
state_buf	dd 0				; tampon d'etat : numero, adresse
state		dd 0
virt_n		dd 1				; sorties virtuelles (sound_virtual=2)
main_out	dd 0				; sortie principale
pref_out	dd 0				; preferee (sound.txt), si presente
next_out	dd 0
next_in		dd 0
dma_mask	dd 0
dma_used	dd 0
t_start		dd 0				; demarrage de sound (ms)
dirty		dd 0				; reglages changes (ms), 0 : a jour
pref_name	rb 32				; sortie principale preferee (nom)
namebuf		rb 32
notebuf		rb 128
settings_data
		align 16
streams		rb STR_MAX * STR_SIZE
clients		rb CLI_MAX * CL_SIZE
outs		rb OUT_MAX * OUT_SIZE
inputs		rb IN_MAX * IN_SIZE
mix_data
msg		rb 512
plist		rb 64 * 64
optbuf		rb 1024
