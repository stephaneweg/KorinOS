; ============================================================================
;  inc/apprun.inc - les applications qui tournent, pour la coquille (le
;  panel, l'accueil du mode poche) : un pid par application du catalogue
;  (le noyau nomme le process d'un paquet d'apres lui), et l'ouverture d'une
;  application (sa fenetre ramenee, sinon lancee). Une application qui
;  demarre passe devant les recentes (inc/recent.inc), sauf au premier tour
;  (ce qui tournait deja). A inclure apres applist.inc et recent.inc.
;
;	bl apps_running		pids : x0 = 1 si quelque chose a change
;	mov x0,app ; bl app_open
; ============================================================================

MAXPROCS	= 64			; process lus par morceau (SYS_PROCLIST_FROM)
PE_SIZE		= 64			; entree de proclist : nom en +24

; pids des applications qui tournent (tous les process, par morceaux) ->
; x0 = 1 si quelque chose a change
apps_running:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,pids_new			; rien de trouve encore
	mov	x1,0
.z:	str	xzr,[x0,x1,lsl 3]
	add	x1,x1,1
	cmp	x1,AP_MAX
	b.lo	.z
	mov	x21,0				; tous les process, par morceaux
.paq:	adr	x0,procs			; (SYS_PROCLIST_FROM : pas de plafond)
	mov	x1,MAXPROCS
	mov	x2,x21
	sys	SYS_PROCLIST_FROM
	cmp	x0,0
	b.le	.cmp
	mov	x22,x0
	mov	x24,x0
	adr	x20,procs
.e:	cbz	x22,.suite
	ldr	x21,[x20]			; (la suite : apres ce process)
	and	x21,x21,0xFFFF
	mov	x19,0				; une application de ce nom ?
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.en
	add	x0,x20,24
	adr	x1,ap_nom
	add	x1,x1,x19,lsl 5
	bl	str_eq
	cbz	x0,.an
	adr	x0,pids_new			; (le premier process de ce nom)
	ldr	x1,[x0,x19,lsl 3]
	cbnz	x1,.en
	ldr	w1,[x20]
	str	x1,[x0,x19,lsl 3]
	b	.en
.an:	add	x19,x19,1
	b	.a
.en:	add	x20,x20,PE_SIZE
	sub	x22,x22,1
	b	.e
.suite:	cmp	x24,MAXPROCS			; morceau plein : la suite
	b.eq	.paq
.cmp:	mov	x23,0				; change ?
	mov	x19,0
.c:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,pids_new
	ldr	x2,[x0,x19,lsl 3]
	adr	x0,pids
	ldr	x1,[x0,x19,lsl 3]
	cmp	x1,x2
	cinc	x23,x23,ne
	str	x2,[x0,x19,lsl 3]
	cbnz	x1,.nc				; elle demarre : une recente
	cbz	x2,.nc
	adr	x0,run_seen
	ldr	x0,[x0]
	cbz	x0,.nc
	mov	x0,x19
	bl	rc_touch
.nc:	add	x19,x19,1
	b	.c
.out:	adr	x0,run_seen
	mov	x1,1
	str	x1,[x0]
	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = application : ramene sa fenetre (reduite aussi), ou la lance
app_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,pids
	ldr	x20,[x0,x19,lsl 3]
	cbz	x20,.launch
	mov	x21,0				; ses fenetres : la premiere
.page:	mov	x0,x21
	adr	x1,wlist
	bl	ui_winlist
	cmp	x0,x21
	b.le	.launch				; pas de fenetre : relancer
	sub	x22,x0,x21
	cmp	x22,WL_PER_MSG
	mov	x1,WL_PER_MSG
	csel	x22,x1,x22,gt
	adr	x2,wlist
.e:	cbz	x22,.np
	ldr	w3,[x2,WL_PID]
	cmp	x3,x20
	b.eq	.raise
	add	x2,x2,WL_SIZE
	sub	x22,x22,1
	b	.e
.np:	add	x21,x21,WL_PER_MSG
	b	.page
.raise:	ldr	x0,[x2,WL_ID]
	bl	ui_raise
	b	.out
.launch:
	mov	x0,x19
	bl	app_launch
	mov	x0,x19				; (sans attendre le prochain tour)
	bl	rc_touch
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

macro apprun_data {
		align 8
run_seen	dd 0				; un premier tour fait
pids		rd AP_MAX
pids_new	rd AP_MAX			; (apps_running)
wlist		rb WL_PER_MSG * WL_SIZE
procs		rb MAXPROCS * PE_SIZE
		align 8
}
