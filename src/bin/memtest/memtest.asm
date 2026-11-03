; memtest - essai de la memoire des process (docs/ABI.md, appels 0x06-0x0C,
; 0x1A, 0x65 ; docs/ARCHITECTURE.md, section Memoire) :
;  * reservation et peuplement a la demande : 256 Mo reserves, une page sur
;    64 touchee, pages reellement prises (vm_query, vm_stats, pages libres),
;    page jamais ecrite lue a zero ; munmap (moitie, trou au milieu, tout :
;    pages rendues) ;
;  * reservation de 8 Go PROT_NONE (plus que la RAM), mprotect d'un morceau
;    (lecture / ecriture, puis lecture seule), tampon PROT_NONE refuse par le
;    noyau (E_FAULT), munmap hors de l'arene refuse ;
;  * le noyau ecrit dans une page pas encore peuplee (getrandom) ;
;  * madvise DONTNEED (pages rendues, relues a zero), WILLNEED (peuplees) ;
;  * VM_FIXED_NOREPLACE sur une place prise (E_EXIST), VM_FIXED (remplacee) ;
;  * mprotect : un fils qui ecrit apres PROT_READ, un fils qui lit une page
;    PROT_NONE : tues proprement (code -11), le systeme continue ;
;  * image prechargee (SD0:/etc/preload.txt doit lister SD0:/bin/memtest) :
;    pages de code partagees, page de donnees en copie sur ecriture ; deux
;    fils ecrivent chacun dans la meme variable : chacun ne voit que sa
;    valeur, le pere garde l'originale ; references de l'image (+2 pendant,
;    rendues apres) ;
;  * charge sur plusieurs coeurs : 4 threads qui font fauter les memes pages
;    en meme temps (contenu juste, pas de page en double), et 50 cycles
;    map / touche / verifie / unmap chacun ; rien ne fuit ;
;  * fichier change : SD0:/bin/hello (precharge) ouvert en ecriture -> son
;    image est oubliee (vfs, IMAGE_FORGET), il se lance quand meme.
; Sortie : « memtest : N PASS, M FAIL », code de sortie = M.
;   memtest bench [n]  : temps de lancement de SD0:/bin/fasmarm precharge et
;                        d'une copie non prechargee (SD0:/tmp), n fois (10)
;   memtest child ...  : (fils) cow, protect, none, stats
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

MO		= 0x100000
GO		= 0x40000000
LAZY_TAILLE	= 256*MO
PAS		= 64*4096		; une page sur 64
N_TOUCHES	= LAZY_TAILLE / PAS
COW_INIT	= 0x777
N_TH		= 4
TH_PAGES	= 4096			; region commune : 16 Mo
TH_CYCLES	= 50
STK		= 16384
E_FAULT		= -14
E_EXIST		= -17
E_NOENT		= -2
COPIE_TAMPON	= 65536

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,essais
	mov	x0,x19
	adr	x1,s_child
	bl	prefixe
	cbnz	x0,fils
	mov	x0,x19
	adr	x1,s_bench
	bl	prefixe
	cbnz	x0,bench
essais:
	adr	x0,m_debut
	bl	puts
	bl	t_paresseux
	bl	t_reserve
	bl	t_noyau
	bl	t_conseil
	bl	t_fixe
	bl	t_protect
	bl	t_cow
	bl	t_coeurs
	bl	t_oubli
	adr	x0,m_bilan
	bl	puts
	adr	x0,n_pass
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_bilan2
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_bilan3
	bl	puts
	adr	x0,n_fail
	ldr	x0,[x0]
	sys	SYS_EXIT

; --- reservation et peuplement a la demande, munmap ---------------------------
t_paresseux:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	libres
	mov	x20,x0				; pages libres avant
	bl	stats
	ldr	x21,[x0,VS_FAULTS]
	mov	x0,0
	movl	x1,LAZY_TAILLE
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x19,x0
	mov	x2,x0
	movl	x1,VM_ARENA_BASE
	cmp	x0,x1
	cset	x0,hs
	adr	x1,l_map
	bl	verdict
	mov	x0,x19
	bl	query
	mov	x22,x0
	ldp	x1,x2,[x0]
	movl	x3,LAZY_TAILLE
	add	x3,x19,x3
	cmp	x1,x19
	ccmp	x2,x3,0,eq
	ldr	w2,[x22,24]
	ccmp	w2,0,0,eq
	cset	x0,eq
	adr	x1,l_query0
	bl	verdict
	mov	x0,0				; une page sur 64
.t:	movl	x1,PAS
	madd	x1,x0,x1,x19
	add	x2,x0,1
	movl	x3,0x5A5A0000
	orr	x2,x2,x3
	str	x2,[x1]
	add	x0,x0,1
	cmp	x0,N_TOUCHES
	b.lo	.t
	mov	x0,x19
	bl	query
	ldr	w2,[x0,24]
	cmp	w2,N_TOUCHES
	cset	x0,eq
	adr	x1,l_touch
	bl	verdict
	bl	stats
	ldr	x2,[x0,VS_FAULTS]
	sub	x2,x2,x21
	cmp	x2,N_TOUCHES
	cset	x0,eq
	adr	x1,l_fautes
	bl	verdict
	bl	libres
	sub	x2,x20,x0			; pages prises (+ tables : une L3 par
	cmp	x2,N_TOUCHES + (LAZY_TAILLE shr 21) + 16	; 2 Mo touches)
	cset	x0,ls
	adr	x1,l_prises
	bl	verdict
	mov	x0,0				; contenu relu
	mov	x23,0
.v:	movl	x1,PAS
	madd	x1,x0,x1,x19
	ldr	x2,[x1]
	add	x3,x0,1
	movl	x4,0x5A5A0000
	orr	x3,x3,x4
	cmp	x2,x3
	cinc	x23,x23,ne
	add	x0,x0,1
	cmp	x0,N_TOUCHES
	b.lo	.v
	mov	x2,x23
	cmp	x23,0
	cset	x0,eq
	adr	x1,l_relu
	bl	verdict
	ldr	x2,[x19,4096]			; jamais ecrite
	cmp	x2,0
	cset	x0,eq
	adr	x1,l_zero
	bl	verdict
	movl	x0,LAZY_TAILLE/2		; munmap de la seconde moitie
	add	x0,x19,x0
	movl	x1,LAZY_TAILLE/2
	sys	SYS_VM_UNMAP
	mov	x0,x19
	bl	query
	ldr	x1,[x0,8]
	ldr	w2,[x0,24]
	movl	x3,LAZY_TAILLE/2
	add	x3,x19,x3
	mov	w4,N_TOUCHES/2 + 1		; (+ la page lue a zero)
	cmp	x1,x3
	ccmp	w2,w4,0,eq
	cset	x0,eq
	adr	x1,l_unmap_moitie
	bl	verdict
	movl	x0,32*MO			; un trou au milieu
	add	x0,x19,x0
	movl	x1,32*MO
	sys	SYS_VM_UNMAP
	movl	x0,32*MO
	add	x0,x19,x0
	bl	query_r
	mov	x22,x0				; 1 : la suivante, au-dessus
	ldr	x1,[x1]
	movl	x3,64*MO
	add	x3,x19,x3
	cmp	x22,1
	ccmp	x1,x3,0,eq
	cset	x0,eq
	mov	x2,x22
	adr	x1,l_unmap_trou
	bl	verdict
	mov	x0,x19				; tout
	movl	x1,LAZY_TAILLE/2
	sys	SYS_VM_UNMAP
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_unmap_tout
	bl	verdict
	bl	libres
	sub	x2,x20,x0			; (les tables L3 restent : une par
	cmp	x2,(LAZY_TAILLE shr 21) + 16	; 2 Mo touches)
	cset	x0,le
	adr	x1,l_rendues
	bl	verdict
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- grande reservation PROT_NONE, mprotect ---------------------------------
t_reserve:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x0,0
	movl	x1,8*GO
	mov	x2,PROT_NONE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x19,x0
	mov	x2,x0
	cmp	x0,0
	cset	x0,gt
	adr	x1,l_8go
	bl	verdict
	movl	x20,4*GO			; 1 Mo au milieu : lecture / ecriture
	add	x20,x19,x20
	mov	x0,x20
	mov	x1,MO
	mov	x2,PROT_READ or PROT_WRITE
	sys	SYS_VM_PROTECT
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_prot_rw
	bl	verdict
	mov	x0,0				; ecriture, relecture
.w:	add	x1,x20,x0,lsl 12
	str	x0,[x1,8]
	add	x0,x0,1
	cmp	x0,256
	b.lo	.w
	mov	x0,0
	mov	x21,0
.r:	add	x1,x20,x0,lsl 12
	ldr	x2,[x1,8]
	cmp	x2,x0
	cinc	x21,x21,ne
	add	x0,x0,1
	cmp	x0,256
	b.lo	.r
	mov	x2,x21
	cmp	x21,0
	cset	x0,eq
	adr	x1,l_prot_ecrit
	bl	verdict
	mov	x0,x20				; coupee en trois : la sienne
	bl	query
	mov	x22,x0
	ldp	x1,x2,[x0]
	ldr	w3,[x0,16]
	add	x4,x20,MO
	cmp	x1,x20
	ccmp	x2,x4,0,eq
	ccmp	w3,PROT_READ or PROT_WRITE,0,eq
	cset	x0,eq
	ldr	w2,[x22,24]
	adr	x1,l_prot_region
	bl	verdict
	mov	x0,x20				; lecture seule : la lecture marche
	mov	x1,MO
	mov	x2,PROT_READ
	sys	SYS_VM_PROTECT
	ldr	x2,[x20,8+5*4096]
	cmp	x2,5
	cset	x0,eq
	adr	x1,l_prot_ro
	bl	verdict
	mov	x0,x19				; tampon PROT_NONE : refuse
	mov	x1,32
	sys	SYS_GETRANDOM
	mov	x2,x0
	cmp	x0,E_FAULT
	cset	x0,eq
	adr	x1,l_none_noyau
	bl	verdict
	adr	x0,start			; munmap hors de l'arene : E_INVAL
	mov	x1,4096
	sys	SYS_VM_UNMAP
	mov	x2,x0
	cmn	x0,22
	cset	x0,eq
	adr	x1,l_unmap_hors
	bl	verdict
	mov	x0,x19
	movl	x1,8*GO
	sys	SYS_VM_UNMAP
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_8go_rendu
	bl	verdict
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- le noyau dans une page pas encore peuplee ------------------------------
t_noyau:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0
	mov	x1,65536
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x19,x0
	add	x0,x19,4*4096			; getrandom a cheval sur deux pages
	sub	x0,x0,128
	mov	x1,256
	sys	SYS_GETRANDOM
	mov	x20,x0
	add	x0,x19,4*4096
	sys	SYS_VM_PAGEINFO
	and	x1,x0,PI_PRESENT
	cmp	x20,256
	ccmp	x1,PI_PRESENT,0,eq
	cset	x0,eq
	mov	x2,x20
	adr	x1,l_noyau
	bl	verdict
	mov	x0,x19
	mov	x1,65536
	sys	SYS_VM_UNMAP
	ldp	x19,x20,[sp],16
	epilogue

; --- madvise ------------------------------------------------------------------
t_conseil:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0
	mov	x1,MO
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x19,x0
	mov	x0,0
.w:	add	x1,x19,x0,lsl 12
	mov	x2,0x55
	str	x2,[x1]
	add	x0,x0,1
	cmp	x0,256
	b.lo	.w
	mov	x0,x19
	mov	x1,MO
	mov	x2,MADV_DONTNEED
	sys	SYS_VM_ADVISE
	mov	x0,x19
	bl	query
	ldr	w20,[x0,24]
	ldr	x1,[x19,4096]
	cmp	x20,0
	ccmp	x1,0,0,eq
	cset	x0,eq
	mov	x2,x20
	adr	x1,l_dontneed
	bl	verdict
	mov	x0,x19
	mov	x1,MO
	mov	x2,MADV_WILLNEED
	sys	SYS_VM_ADVISE
	mov	x0,x19
	bl	query
	ldr	w2,[x0,24]
	cmp	w2,256
	cset	x0,eq
	adr	x1,l_willneed
	bl	verdict
	mov	x0,x19
	mov	x1,MO
	sys	SYS_VM_UNMAP
	ldp	x19,x20,[sp],16
	epilogue

; --- VM_FIXED, VM_FIXED_NOREPLACE ----------------------------------------------
t_fixe:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0
	mov	x1,65536
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x19,x0
	mov	x1,7
	str	x1,[x19]
	mov	x0,x19
	mov	x1,65536
	mov	x2,PROT_READ or PROT_WRITE
	movl	x3,VM_FIXED_NOREPLACE
	sys	SYS_VM_MAP
	mov	x2,x0
	cmp	x0,E_EXIST
	cset	x0,eq
	adr	x1,l_noreplace
	bl	verdict
	mov	x0,x19
	mov	x1,65536
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,VM_FIXED
	sys	SYS_VM_MAP
	ldr	x2,[x19]
	cmp	x0,x19
	ccmp	x2,0,0,eq
	cset	x0,eq
	adr	x1,l_fixed
	bl	verdict
	mov	x0,x19
	mov	x1,65536
	sys	SYS_VM_UNMAP
	ldp	x19,x20,[sp],16
	epilogue

; --- mprotect : fils tues proprement -----------------------------------------
t_protect:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,a_protect
	bl	fils_code
	mov	x2,x0
	cmn	x0,11
	cset	x0,eq
	adr	x1,l_tue_ro
	bl	verdict
	adr	x0,a_none
	bl	fils_code
	mov	x2,x0
	cmn	x0,11
	cset	x0,eq
	adr	x1,l_tue_none
	bl	verdict
	ldp	x19,x20,[sp],16
	epilogue

; x0 = arguments : lance « SD0:/bin/memtest <arguments> » -> x0 = son code
fils_code:
	prologue
	mov	x1,x0
	adr	x0,p_moi
	sys	SYS_SPAWN
	tbnz	x0,63,.out
	sys	SYS_WAIT
.out:	epilogue

; --- image prechargee : pages partagees, copie sur ecriture --------------------
t_cow:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,start			; lance depuis une image prechargee ?
	sys	SYS_VM_PAGEINFO
	mov	x2,x0
	and	x0,x0,PI_SHARED
	adr	x1,l_precharge
	bl	verdict
	adr	x0,start
	sys	SYS_VM_PAGEINFO
	tbnz	x0,1,.oui			; (PI_SHARED)
	adr	x0,m_pas_precharge
	bl	puts
	b	.out
.oui:	adr	x0,cow_var			; page de donnees jamais ecrite
	sys	SYS_VM_PAGEINFO
	mov	x2,x0
	and	x0,x0,PI_COW or PI_SHARED
	cmp	x0,PI_COW or PI_SHARED
	cset	x0,eq
	adr	x1,l_cow_pere
	bl	verdict
	mov	x0,100				; (les fils de t_protect : le faucheur rend
	sys	SYS_SLEEP			; leur reference apres SYS_WAIT)
	bl	refs_moi
	mov	x21,x0				; references avant
	adr	x0,p_moi			; deux fils en meme temps
	adr	x1,a_cow
	sys	SYS_SPAWN
	mov	x19,x0
	adr	x0,p_moi
	adr	x1,a_cow
	sys	SYS_SPAWN
	mov	x20,x0
	mov	x0,150
	sys	SYS_SLEEP
	bl	refs_moi
	sub	x2,x0,x21
	cmp	x2,2
	cset	x0,eq
	adr	x1,l_refs_plus
	bl	verdict
	mov	x0,x19
	sys	SYS_WAIT
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_cow_a
	bl	verdict
	mov	x0,x20
	sys	SYS_WAIT
	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_cow_b
	bl	verdict
	bl	refs_moi
	mov	x2,x0
	cmp	x0,x21
	cset	x0,eq
	adr	x1,l_refs_rendues
	bl	verdict
	adr	x0,cow_var			; le pere : la valeur d'origine
	ldr	x2,[x0]
	cmp	x2,COW_INIT
	cset	x0,eq
	adr	x1,l_cow_orig
	bl	verdict
	adr	x0,a_stats			; un fils : pages partagees
	bl	fils_code
	lsr	x2,x0,20			; (pages partagees << 20 | a lui)
	adr	x3,start			; pages de code de memtest
	adr	x4,fin_code
	sub	x3,x4,x3
	lsr	x3,x3,12
	cmp	x2,x3
	cset	x0,hs
	adr	x1,l_partagees
	bl	verdict
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = references de l'image de SD0:/bin/memtest (0 : absente)
refs_moi:
	prologue
	adr	x0,p_moi
	bl	image_de
	cbz	x0,.out
	ldr	x0,[x0,IML_REFS]
.out:	epilogue

; x0 = chemin -> x0 = son entree dans image_list (liste_img), 0 : absente
image_de:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	adr	x0,liste_img
	mov	x1,16
	sys	SYS_IMAGE_LIST
	mov	x20,x0
	adr	x21,liste_img
.s:	cmp	x20,0
	b.le	.none
	mov	x0,x19
	add	x1,x21,IML_PATH
	bl	str_ieq
	cbnz	x0,.got
	add	x21,x21,IML_SIZE
	sub	x20,x20,1
	b	.s
.got:	mov	x0,x21
	b	.out
.none:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- plusieurs coeurs : fautes concurrentes, map / unmap ------------------------
t_coeurs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	libres
	mov	x21,x0
	mov	x0,0				; region commune
	movl	x1,TH_PAGES*4096
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	adr	x1,commune
	str	x0,[x1]
	mov	x19,0
.c:	adr	x0,w_coeur			; N_TH threads
	adr	x1,piles
	add	x2,x19,1
	mov	x3,STK
	madd	x1,x2,x3,x1			; sommet de sa pile
	mov	x2,x19				; argument : son rang
	mov	x3,0
	mov	x4,THREAD_PRIO_NORMAL
	adr	x5,n_coeur
	sys	SYS_THREAD_CREATE_EX
	adr	x1,tids
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,N_TH
	b.lo	.c
	mov	x19,0
	mov	x20,0				; erreurs des cycles prives
.j:	adr	x0,tids
	ldr	x0,[x0,x19,lsl 3]
	sys	SYS_THREAD_JOIN
	add	x20,x20,x0
	add	x19,x19,1
	cmp	x19,N_TH
	b.lo	.j
	adr	x0,commune			; chaque mot de chaque page
	ldr	x22,[x0]
	mov	x19,0
	mov	x3,0				; faux
.vp:	cmp	x19,TH_PAGES
	b.hs	.vd
	mov	x4,0
.vt:	add	x5,x22,x19,lsl 12
	ldr	x6,[x5,x4,lsl 3]
	add	x7,x4,1
	lsl	x7,x7,32
	orr	x7,x7,x19
	cmp	x6,x7
	cinc	x3,x3,ne
	add	x4,x4,1
	cmp	x4,N_TH
	b.lo	.vt
	add	x19,x19,1
	b	.vp
.vd:	mov	x2,x3
	cmp	x3,0
	cset	x0,eq
	adr	x1,l_coeurs
	bl	verdict
	mov	x0,x22
	bl	query
	ldr	w2,[x0,24]
	cmp	w2,TH_PAGES
	cset	x0,eq
	adr	x1,l_double
	bl	verdict
	mov	x2,x20
	cmp	x20,0
	cset	x0,eq
	adr	x1,l_cycles
	bl	verdict
	mov	x0,x22
	movl	x1,TH_PAGES*4096
	sys	SYS_VM_UNMAP
	bl	libres
	sub	x2,x21,x0
	cmp	x2,64
	cset	x0,le
	adr	x1,l_fuite
	bl	verdict
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (thread) x0 = rang : ecrit son mot dans chaque page de la region commune
; (tous en meme temps : fautes sur les memes pages), puis TH_CYCLES cycles
; map 256 Ko / touche / verifie / unmap -> code = erreurs
w_coeur:
	mov	x19,x0
	adr	x0,commune
	ldr	x20,[x0]
	mov	x21,0
.p:	add	x0,x20,x21,lsl 12
	add	x1,x19,1
	lsl	x1,x1,32
	orr	x1,x1,x21
	str	x1,[x0,x19,lsl 3]
	add	x21,x21,1
	cmp	x21,TH_PAGES
	b.lo	.p
	mov	x22,0				; erreurs
	mov	x23,0
.cy:	mov	x0,0
	mov	x1,256*1024
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.mal
	mov	x24,x0
	mov	x1,0
.tw:	add	x2,x24,x1,lsl 12
	add	x3,x1,x19
	str	x3,[x2,16]
	add	x1,x1,1
	cmp	x1,64
	b.lo	.tw
	mov	x1,0
.tr:	add	x2,x24,x1,lsl 12
	ldr	x3,[x2,16]
	add	x4,x1,x19
	cmp	x3,x4
	cinc	x22,x22,ne
	add	x1,x1,1
	cmp	x1,64
	b.lo	.tr
	mov	x0,x24
	mov	x1,256*1024
	sys	SYS_VM_UNMAP
	cbz	x0,.nx
.mal:	add	x22,x22,1
.nx:	add	x23,x23,1
	cmp	x23,TH_CYCLES
	b.lo	.cy
	mov	x0,x22
	sys	SYS_THREAD_EXIT

; --- fichier change : image oubliee -------------------------------------------
t_oubli:
	prologue
	str	x19,[sp,-16]!
	adr	x0,p_hello
	bl	image_de
	cbnz	x0,.oui
	adr	x0,m_hello_absent
	bl	puts
	b	.out
.oui:	adr	x0,p_hello			; ouvert en ecriture, rien d'ecrit
	mov	x1,O_WRITE
	sys	SYS_OPEN
	mov	x19,x0
	tbnz	x0,63,.ferme
	sys	SYS_CLOSE
.ferme:	adr	x0,p_hello
	bl	image_de
	cmp	x0,0
	cset	x0,eq
	mov	x2,x19
	adr	x1,l_oubli
	bl	verdict
	adr	x0,p_hello			; lance quand meme (lu sur la carte)
	mov	x1,0
	sys	SYS_SPAWN
	tbnz	x0,63,.code
	sys	SYS_WAIT
.code:	mov	x2,x0
	cmp	x0,0
	cset	x0,eq
	adr	x1,l_hello
	bl	verdict
.out:	ldr	x19,[sp],16
	epilogue

; --- fils ---------------------------------------------------------------------
fils:
	add	x19,x19,6			; apres « child »
	mov	x0,x19
	adr	x1,s_cow
	bl	prefixe
	cbnz	x0,fils_cow
	mov	x0,x19
	adr	x1,s_protect
	bl	prefixe
	cbnz	x0,fils_protect
	mov	x0,x19
	adr	x1,s_none
	bl	prefixe
	cbnz	x0,fils_none
	mov	x0,x19
	adr	x1,s_stats
	bl	prefixe
	cbnz	x0,fils_stats
	mov	x0,99
	sys	SYS_EXIT

; ecrit dans une page lecture / ecriture devenue lecture seule : doit etre tue
fils_protect:
	mov	x0,0
	mov	x1,4096
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x19,x0
	mov	x1,1
	str	x1,[x19]
	mov	x0,x19
	mov	x1,4096
	mov	x2,PROT_READ
	sys	SYS_VM_PROTECT
	ldr	x1,[x19]
	add	x1,x1,1
	str	x1,[x19]			; faute ici
	mov	x0,0				; (pas tue : 0, compte comme un echec)
	sys	SYS_EXIT

; lit une page PROT_NONE : doit etre tue
fils_none:
	mov	x0,0
	mov	x1,4096
	mov	x2,PROT_NONE
	mov	x3,0
	sys	SYS_VM_MAP
	ldr	x1,[x0]				; faute ici
	mov	x0,0
	sys	SYS_EXIT

; copie sur ecriture : code = bits des echecs (0 : tout va bien)
fils_cow:
	mov	x20,0
	adr	x0,cow_var			; partagee, en copie sur ecriture
	sys	SYS_VM_PAGEINFO
	and	x0,x0,PI_COW or PI_SHARED
	cmp	x0,PI_COW or PI_SHARED
	b.eq	.a
	orr	x20,x20,1
.a:	adr	x0,cow_var			; valeur d'origine
	ldr	x0,[x0]
	cmp	x0,COW_INIT
	b.eq	.b
	orr	x20,x20,2
.b:	sys	SYS_GETPID			; son pid dans la variable
	mov	x21,x0
	adr	x1,cow_var
	str	x21,[x1]
	adr	x0,cow_var			; a lui, inscriptible
	sys	SYS_VM_PAGEINFO
	and	x1,x0,PI_COW or PI_SHARED
	cbz	x1,.c
	orr	x20,x20,4
.c:	tbnz	x0,3,.d				; PI_WRITE
	orr	x20,x20,4
.d:	mov	x0,300				; l'autre fils ecrit pendant ce temps
	sys	SYS_SLEEP
	adr	x0,cow_var
	ldr	x0,[x0]
	cmp	x0,x21
	b.eq	.e
	orr	x20,x20,8
.e:	adr	x0,start			; code partage
	sys	SYS_VM_PAGEINFO
	tbnz	x0,1,.f
	orr	x20,x20,16
.f:	bl	stats				; au moins une copie
	ldr	x0,[x0,VS_COWS]
	cbnz	x0,.g
	orr	x20,x20,32
.g:	mov	x0,x20
	sys	SYS_EXIT

; code = pages partagees << 20 | pages a lui
fils_stats:
	bl	stats
	ldr	x1,[x0,VS_SHARED]
	ldr	x0,[x0,VS_OWN]
	orr	x0,x0,x1,lsl 20
	sys	SYS_EXIT

; --- bench : temps de lancement ------------------------------------------------
bench:
	add	x0,x19,5			; nombre de lancements (10)
	bl	nombre
	cmp	x0,0
	mov	x1,10
	csel	x24,x1,x0,eq
	adr	x0,p_fasm			; precharge ?
	bl	image_de
	mov	x19,x0
	adr	x0,m_b_fasm
	bl	puts
	cbz	x19,.np
	ldr	x0,[x19,IML_FSIZE]
	lsr	x0,x0,10
	bl	print_dec
	adr	x0,m_b_ko
	bl	puts
	b	.cp
.np:	adr	x0,m_b_np
	bl	puts
.cp:	adr	x0,p_tmp			; la copie, non prechargee
	sys	SYS_MKDIR
	bl	copier
	mov	x21,x0
	adr	x0,m_b_copie
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,p_fasm			; une fois chacun (caches chauds)
	bl	lancer_lire
	adr	x0,p_copie
	bl	lancer_lire
	sys	SYS_TIME_MS
	mov	x20,x0
	mov	x21,0
.l1:	adr	x0,p_fasm
	bl	lancer_lire
	add	x21,x21,1
	cmp	x21,x24
	b.lo	.l1
	sys	SYS_TIME_MS
	sub	x20,x0,x20
	sys	SYS_TIME_MS
	mov	x22,x0
	mov	x21,0
.l2:	adr	x0,p_copie
	bl	lancer_lire
	add	x21,x21,1
	cmp	x21,x24
	b.lo	.l2
	sys	SYS_TIME_MS
	sub	x22,x0,x22
	adr	x0,m_b_res			; « precharge X ms, copie Y ms, n lancements »
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_b_res2
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,m_b_res3
	bl	puts
	mov	x0,x24
	bl	print_dec
	adr	x0,m_b_res4
	bl	puts
	mov	x0,10				; par lancement, au dixieme de ms
	mul	x0,x20,x0
	udiv	x0,x0,x24
	bl	dixiemes
	adr	x0,m_b_res5
	bl	puts
	mov	x0,10
	mul	x0,x22,x0
	udiv	x0,x0,x24
	bl	dixiemes
	adr	x0,m_b_res6
	bl	puts
	adr	x0,p_copie
	sys	SYS_UNLINK
	cmp	x20,x22				; code 0 : le precharge va plus vite
	cset	x0,hs
	sys	SYS_EXIT

; x0 = chemin : lance sans argument, lit sa sortie jusqu'a la fin, l'attend
lancer_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,0
	adr	x2,fd
	sys	SYS_SPAWN_PIPE
	mov	x19,x0
	tbnz	x0,63,.out
.rd:	adr	x0,fd
	ldr	x0,[x0]
	adr	x1,tampon_pipe
	mov	x2,256
	sys	SYS_READ
	cmn	x0,11
	b.eq	.att
	cmp	x0,0
	b.gt	.rd
	adr	x0,fd
	ldr	x0,[x0]
	sys	SYS_CLOSE
	mov	x0,x19
	sys	SYS_WAIT
	b	.out
.att:	mov	x0,1
	sys	SYS_SLEEP
	b	.rd
.out:	ldp	x19,x20,[sp],16
	epilogue

; p_fasm -> p_copie (tampon de VM_MAP) -> x0 = octets copies
copier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x0,0
	mov	x1,COPIE_TAMPON
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	mov	x21,x0
	adr	x0,p_fasm
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,x0
	mov	x22,x0
	tbnz	x0,63,.out
	adr	x0,p_copie
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	mov	x20,x0
	mov	x22,x0
	tbnz	x0,63,.f1
	mov	x22,0
.c:	mov	x0,x19
	mov	x1,x21
	mov	x2,COPIE_TAMPON
	sys	SYS_READ
	cmp	x0,0
	b.lt	.f2w
	b.eq	.f2
	mov	x2,x0
	mov	x0,x20
	mov	x1,x21
	sys	SYS_WRITE
	cmp	x0,0
	b.le	.f2w
	add	x22,x22,x0
	b	.c
.f2w:	mov	x22,x0				; (erreur d'ecriture : montree)
.f2:	mov	x0,x20
	sys	SYS_CLOSE
.f1:	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x21
	mov	x1,COPIE_TAMPON
	sys	SYS_VM_UNMAP
	mov	x0,x22
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = dixiemes : « 12,3 »
dixiemes:
	prologue
	str	x19,[sp,-16]!
	mov	x1,10
	udiv	x19,x0,x1
	msub	x2,x19,x1,x0
	str	x2,[sp,-16]!
	mov	x0,x19
	bl	print_dec
	adr	x0,m_virgule
	bl	puts
	ldr	x0,[sp],16
	bl	print_dec
	ldr	x19,[sp],16
	epilogue

; --- utilitaires ---------------------------------------------------------------
; -> x0 = sortie de vm_stats (stats_buf)
stats:
	prologue
	mov	x0,0
	adr	x1,stats_buf
	sys	SYS_VM_STATS
	adr	x0,stats_buf
	epilogue

; -> x0 = pages libres (zones basse + haute)
libres:
	prologue
	bl	stats
	ldr	x1,[x0,VS_FREE_LOW]
	ldr	x2,[x0,VS_FREE_HIGH]
	add	x0,x1,x2
	epilogue

; x0 = adresse -> x0 = sortie de vm_query (query_buf)
query:
	prologue
	adr	x1,query_buf
	sys	SYS_VM_QUERY
	adr	x0,query_buf
	epilogue

; x0 = adresse -> x0 = resultat de vm_query, x1 = query_buf
query_r:
	prologue
	adr	x1,query_buf
	sys	SYS_VM_QUERY
	adr	x1,query_buf
	epilogue

; x0 = chaine, x1 = debut attendu -> x0 = 1 si elle commence ainsi
prefixe:
.c:	ldrb	w3,[x1],1
	cbz	w3,.oui
	ldrb	w2,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.oui:	mov	x0,1
	ret

; x0, x1 = chaines -> x0 = 1 si egales aux majuscules pres
str_ieq:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	sub	w4,w2,'A'
	cmp	w4,25
	b.hi	.a
	add	w2,w2,32
.a:	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.b
	add	w3,w3,32
.b:	cmp	w2,w3
	b.ne	.non
	cbnz	w2,.c
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = texte -> x0 = nombre decimal en tete (espaces sautes ; 0 sinon)
nombre:
	mov	x1,0
.e:	ldrb	w2,[x0]
	cmp	w2,' '
	b.ne	.d
	add	x0,x0,1
	b	.e
.d:	ldrb	w2,[x0],1
	sub	w2,w2,'0'
	cmp	w2,9
	b.hi	.f
	mov	x3,10
	madd	x1,x1,x3,x2
	b	.d
.f:	mov	x0,x1
	ret

; x0 = 1 si reussi, x1 = libelle, x2 = valeur montree
verdict:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	adr	x1,n_pass
	adr	x2,m_pass
	cbnz	x0,.c
	adr	x1,n_fail
	adr	x2,m_fail
.c:	ldr	x3,[x1]
	add	x3,x3,1
	str	x3,[x1]
	mov	x21,x2
	adr	x0,m_mt
	bl	puts
	mov	x0,x21
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_par
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_par2
	bl	puts
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

m_debut		db 'memtest : reservations, pagination a la demande, munmap, mprotect, copie sur ecriture, 4 coeurs',10,0
m_mt		db 'memtest : ',0
m_pass		db 'PASS ',0
m_fail		db 'FAIL ',0
m_par		db ' (',0
m_par2		db ')',10,0
m_nl		db 10,0
m_virgule	db ',',0
m_bilan		db 'memtest : ',0
m_bilan2	db ' PASS, ',0
m_bilan3	db ' FAIL',10,0
m_pas_precharge	db 'memtest : pas lance depuis une image prechargee (SD0:/etc/preload.txt) : copie sur ecriture non essayee',10,0
m_hello_absent	db 'memtest : SD0:/bin/hello pas precharge (deja oublie ?) : essai du fichier change saute',10,0
m_b_fasm	db 'bench : SD0:/bin/fasmarm ',0
m_b_ko		db ' Ko, precharge',10,0
m_b_np		db 'PAS precharge (preload.txt)',10,0
m_b_copie	db 'bench : copie non prechargee SD0:/tmp/fasmarm.copy, octets ',0
m_b_res		db 'bench : precharge ',0
m_b_res2	db ' ms, copie lue sur la carte ',0
m_b_res3	db ' ms, pour ',0
m_b_res4	db ' lancements (spawn + sortie + wait) ; par lancement : ',0
m_b_res5	db ' ms contre ',0
m_b_res6	db ' ms',10,0
s_child		db 'child ',0
s_bench		db 'bench',0
s_cow		db 'cow',0
s_protect	db 'protect',0
s_none		db 'none',0
s_stats		db 'stats',0
a_cow		db 'child cow',0
a_protect	db 'child protect',0
a_none		db 'child none',0
a_stats		db 'child stats',0
p_moi		db 'SD0:/bin/memtest',0
p_hello		db 'SD0:/bin/hello',0
p_fasm		db 'SD0:/bin/fasmarm',0
p_tmp		db 'SD0:/tmp',0
p_copie		db 'SD0:/tmp/fasmarm.copy',0
n_coeur		db 'memtest-coeur',0
l_map		db 'vm_map 256 Mo : adresse dans l''arene',0
l_query0	db 'vm_query : la region entiere, aucune page presente',0
l_touch		db 'une page sur 64 touchee : pages presentes',0
l_fautes	db 'fautes a la demande (vm_stats)',0
l_prises	db 'pages reellement prises (pages libres)',0
l_relu		db 'contenu relu (erreurs)',0
l_zero		db 'page jamais ecrite : lue a zero',0
l_unmap_moitie	db 'munmap de la moitie : region raccourcie, pages restantes',0
l_unmap_trou	db 'munmap au milieu : region coupee en deux (vm_query : la suivante)',0
l_unmap_tout	db 'munmap du reste',0
l_rendues	db 'pages rendues (ecart des pages libres)',0
l_8go		db 'reservation de 8 Go PROT_NONE (plus que la RAM)',0
l_prot_rw	db 'mprotect lecture / ecriture d''1 Mo au milieu',0
l_prot_ecrit	db 'ecriture et relecture dans la partie ouverte (erreurs)',0
l_prot_region	db 'region coupee en trois, droits de la partie ouverte',0
l_prot_ro	db 'mprotect lecture seule : la lecture marche',0
l_none_noyau	db 'tampon PROT_NONE donne au noyau : E_FAULT',0
l_unmap_hors	db 'munmap hors de l''arene : E_INVAL',0
l_8go_rendu	db 'munmap des 8 Go',0
l_noyau		db 'le noyau ecrit dans des pages pas encore peuplees (getrandom)',0
l_dontneed	db 'MADV_DONTNEED : pages rendues, relues a zero',0
l_willneed	db 'MADV_WILLNEED : pages peuplees',0
l_noreplace	db 'VM_FIXED_NOREPLACE sur une place prise : E_EXIST',0
l_fixed		db 'VM_FIXED : remplacee, a zero',0
l_tue_ro	db 'fils qui ecrit apres PROT_READ : tue (code)',0
l_tue_none	db 'fils qui lit une page PROT_NONE : tue (code)',0
l_precharge	db 'lance depuis une image prechargee (code partage)',0
l_cow_pere	db 'page de donnees jamais ecrite : partagee, en copie sur ecriture',0
l_refs_plus	db 'references de l''image : +2 avec deux fils',0
l_cow_a		db 'fils 1 : sa valeur a lui seul (code : bits des echecs)',0
l_cow_b		db 'fils 2 : sa valeur a lui seul (code : bits des echecs)',0
l_refs_rendues	db 'references rendues a la fin des fils',0
l_cow_orig	db 'le pere voit toujours la valeur d''origine',0
l_partagees	db 'fils : pages partagees, pas copiees (au moins le code)',0
l_coeurs	db '4 threads : fautes en meme temps sur les memes pages, contenu juste',0
l_double	db 'pas de page en double (pages presentes de la region commune)',0
l_cycles	db '4 x 50 cycles map / touche / verifie / unmap (erreurs)',0
l_fuite		db 'rien ne fuit (ecart des pages libres)',0
l_oubli		db 'SD0:/bin/hello ouvert en ecriture : image oubliee (vfs)',0
l_hello		db 'hello se lance quand meme (lu sur la carte)',0
		align 4096
fin_code:

segment readable writeable
kit_data
		align 8
n_pass		dd 0
n_fail		dd 0
commune		dd 0
fd		dd 0
tids		rb 8*N_TH
stats_buf	rb 64
query_buf	rb 32
liste_img	rb 16*IML_SIZE
tampon_pipe	rb 256
		align 4096			; une page a elle seule : jamais ecrite
cow_var		dd COW_INIT			; par le pere (copie sur ecriture)
		align 4096
		align 16
piles		rb STK*(N_TH+1)
