; ============================================================================
;  ARMPI OS - noyau
;  Cible : Raspberry Pi 4 (BCM2711, 4x Cortex-A72), AArch64, FASMARM
;
;  Charge par le firmware a 0x80000 (kernel8.img, arm_64bit=1).
;  Jalon 3 : process EL0 (ELF64), espaces d'adressage + ASID, appels systeme.
; ============================================================================

	format	binary as 'img'
	processor cpu64_v8 +cpu64_fp +cpu64_simd
	code64
	org	0x80000

include 'inc/macros.inc'
; Carte cible, choisie a l'assemblage : fasmarm -d ARCH=pi4 (par defaut) ou pi5
match =ARCH, ARCH { ARCH equ pi4 }
match =pi4, ARCH { include 'arch/pi4/board.inc' }
match =pi5, ARCH { include 'arch/pi5/board.inc' }

; Resolution : 1024x768 par defaut, ou fixee a l'assemblage
; (fasmarm -d ECRAN_W=640 -d ECRAN_H=480 ; build.ps1 sd le fait)
match =ECRAN_W, ECRAN_W { ECRAN_W equ 1024 }
match =ECRAN_H, ECRAN_H { ECRAN_H equ 768 }
FB_W	= ECRAN_W
FB_H	= ECRAN_H
include 'inc/mmu.inc'

; Trace de diagnostic (fasmarm -d TRACE_FS=1) : "msg 0x<val>" sur la console,
; tous les registres preserves ; rien n'est assemble sans l'option.
match =TRACE_FS, TRACE_FS { TRACE_FS equ 0 }
macro ktrace msg*, val* {
  if TRACE_FS
	local	.s, .m
	str	x16,[sp,-16]!
	mov	x16,val
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x4,x5,[sp,-16]!
	stp	x6,x7,[sp,-16]!
	stp	x8,x9,[sp,-16]!
	stp	x10,x11,[sp,-16]!
	stp	x12,x13,[sp,-16]!
	stp	x14,x15,[sp,-16]!
	stp	x17,x30,[sp,-16]!
	mrs	x0,nzcv
	str	x0,[sp,-16]!
	mov	x1,x16
	adr	x0,.s
	bl	ktrace_print
	ldr	x0,[sp],16
	msr	nzcv,x0
	ldp	x17,x30,[sp],16
	ldp	x14,x15,[sp],16
	ldp	x12,x13,[sp],16
	ldp	x10,x11,[sp],16
	ldp	x8,x9,[sp],16
	ldp	x6,x7,[sp],16
	ldp	x4,x5,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	ldr	x16,[sp],16
	b	.m
.s	db	msg,' ',0
	align	4
.m:
  end if
}

; Appelle fn sauf si le mot opt figure dans /sd/etc/options.txt
macro init_unless opt*, fn* {
	local	.skip
	adr	x0,opt
	bl	opt_has
	cbnz	x0,.skip
	bl	fn
.skip:
}
macro init_if opt*, fn* {
	local	.skip
	adr	x0,opt
	bl	opt_has
	cbz	x0,.skip
	bl	fn
.skip:
}

; KERNEL_STACK_TOP : pile de boot du coeur 0 (board.inc : sous le noyau, ou
; boot_stack dans le BSS sur le Pi 5)

; ----------------------------------------------------------------------------
_start:
	mov	x28,x0				; adresse du DTB donnee par le firmware
	cpu_index x0
	cbz	x0,.primary
.park:	wfe					; secondaires : normalement retenus par
	b	.park				; le firmware dans la spin table
.primary:
	drop_to_el1 el1_entry

; ----------------------------------------------------------------------------
el1_entry:
	movl	x0,KERNEL_STACK_TOP
	mov	sp,x0
	adr	x0,vector_table
	msr	vbar_el1,x0
	isb

	adr	x0,bss_start			; mise a zero du BSS
	adr	x1,bss_end
.clr:	cmp	x0,x1
	b.hs	.clr_done
	str	xzr,[x0],8
	b	.clr
.clr_done:
	adr	x0,boot_dtb			; (x28 garde depuis _start)
	str	x28,[x0]

	bl	uart_init
	bl	mbox_get_arm_memory
	adr	x1,ram_end
	str	x0,[x1]

	bl	fb_init				; avant la MMU : framebuffer place par le VC
	cbz	x0,.fb_fail
	adr	x0,ram_end
	ldr	x0,[x0]
	bl	mmu_init
	bl	memmap_detect			; RAM au-dela du 1er Go (DTB), mappee
	mov	x0,0
	bl	sched_init_cpu			; per-CPU du coeur 0 (TPIDR_EL1)
	bl	con_init

	adr	x0,msg_banner
	bl	kputs
	adr	x0,msg_ram
	bl	kputs
	adr	x0,ram_end
	ldr	x0,[x0]
	lsr	x0,x0,20
	bl	kput_dec
	adr	x0,msg_mb
	bl	kputs

	adr	x0,ram_end
	ldr	x0,[x0]
	bl	pmm_init
	bl	memmap_log
	adr	x0,msg_pmm
	bl	kputs
	adr	x0,pmm_free_pages
	ldr	x0,[x0]
	lsl	x0,x0,2
	bl	kput_dec
	adr	x0,msg_kb
	bl	kputs
	bl	modules_init			; initramfs (avant toute allocation)
	bl	board_is_emulated		; (revision du firmware lue une fois,
					;  avant les threads des pilotes)
	bl	rng_init			; alea : gigue du compteur (random.asm)

	; --- interruptions + coeurs secondaires -----------------------------
	adr	x0,cpus_online
	mov	x1,1
	str	x1,[x0]
	bl	gic_init_dist
	bl	gic_init_cpu
	bl	timer_init_global
	bl	timer_init_cpu
	bl	smp_start
	adr	x0,msg_cpus
	bl	kputs
	adr	x0,cpus_online
	ldr	x0,[x0]
	bl	kput_dec
	adr	x0,msg_cpus2
	bl	kputs
	adr	x0,msg_arm			; le processeur : trouve -> regle
	bl	kputs
	bl	mbox_arm_rate
	movl	x1,1000000
	udiv	x0,x0,x1
	bl	kput_dec
	adr	x0,msg_arm2
	bl	kputs
if ~ defined NO_ARM_MAX
	bl	mbox_arm_max
else
	bl	mbox_arm_rate
end if
	movl	x1,1000000
	udiv	x0,x0,x1
	bl	kput_dec
	adr	x0,msg_mhz
	bl	kputs

	; --- la carte et ses volumes : en services (docs/MICRO-NOYAU.md,
	; section 4). Le module « init » de initramfs les lance ; la suite du
	; demarrage attend qu'il leve le verrou.
	adr	x0,mod_nom_init
	bl	module_find
	cbnz	x0,.services
	adr	x0,msg_sans_init		; sans init : pas de volume, rien a faire
	bl	kputs
	b	.suite_faite
.services:
	bl	lancer_init
	adr	x0,demarrage_thread
	mov	x1,0
	adr	x2,nom_demarrage
	mov	x3,-1
	bl	thread_create
.suite_faite:

	; --- services noyau + premiers process -------------------------------
	adr	x0,reaper_thread
	mov	x1,0
	adr	x2,name_reaper
	mov	x3,0
	bl	thread_create
	adr	x0,alarm_thread			; SIGALRM (alarm.asm)
	mov	x1,0
	adr	x2,name_alarm
	mov	x3,-1
	bl	thread_create
	adr	x19,initrd_table		; lance chaque ELF embarque
.spawn:	ldp	x0,x1,[x19,8]			; image, taille
	cbz	x0,.spawned
	ldr	x2,[x19]			; nom
	mov	x3,0
	mov	x4,0
	mov	x5,0
	mov	x6,0
	mov	x7,0
	bl	proc_spawn_elf
	mov	x20,x0
	adr	x0,msg_spawn
	bl	kputs
	ldr	x0,[x19]
	bl	kputs
	adr	x0,msg_spawn2
	bl	kputs
	mov	x0,x20
	bl	kput_dec
	mov	w0,10
	bl	kputc
	add	x19,x19,24
	b	.spawn
.spawned:
	b	cpu_idle			; le contexte de boot devient l'idle du coeur 0

.fb_fail:
	adr	x0,msg_fb_ko
	bl	kputs
.halt:	wfe
	b	.halt

; La suite du demarrage, une fois SD0: disponible : options, clavier,
; pilotes encore dans le noyau. Elegant, les services et les programmes de
; startup.txt : init2 (SD0:/sys/svc/init2, lance par init)
demarrage_suite:
	prologue
	bl	opts_load			; SD0:/etc/options.txt : pilotes a desactiver
	init_unless opt_no_highmem, highmem_on	; RAM au-dela de 1 Go
	init_if opt_no_preload, preload_disable	; SD0:/etc/preload.txt ignore
	init_if opt_no_irq, irq_disable		; pilotes : sondage seulement
	init_if opt_no_sd_dma, irq_sd_disable	; carte SD : PIO
	bl	rng200_probe			; alea : RNG200 (option rng200), au journal
	bl	screen_option			; « ecran=LxH »
	adr	x0,demarrage_ecran		; ecran pret : init peut lancer
	mov	w1,1				; init2 (et Elegant)
	str	w1,[x0]
	adr	x0,msg_boot_done
	bl	kputs
	epilogue

; Zone haute de l'allocateur (pmm_enable_high), sauf option Â« no_highmem Â»
highmem_on:
	prologue
	bl	pmm_enable_high
	str	x0,[sp,-16]!
	adr	x0,msg_highmem
	bl	kputs
	ldr	x0,[sp],16
	lsl	x0,x0,2
	bl	kput_dec
	adr	x0,msg_highmem2
	bl	kputs
	adr	x0,pmm_free_pages
	ldr	x0,[x0]
	lsl	x0,x0,2
	bl	kput_dec
	adr	x0,msg_kb
	bl	kputs
	epilogue

; (mode services) lance le module « init » de initramfs (droits de SD0:/sys)
lancer_init:
	prologue
	str	x19,[sp,-16]!
	adr	x0,mod_nom_init
	bl	module_find
	cbz	x0,.out
	mov	x19,x0
	ldp	x0,x1,[x19,ME_ADR]		; image, taille
	adr	x2,mod_nom_init
	mov	x3,0
	mov	x4,0
	mov	x5,0
	mov	x6,1
	mov	x7,0
	bl	proc_spawn_elf
	mov	x19,x0
	adr	x0,msg_init
	bl	kputs
	mov	x0,x19
	bl	kput_dec
	mov	w0,10
	bl	kputc
.out:	ldr	x19,[sp],16
	epilogue

; (mode services, thread) attend le verrou : init (etape 1) leve
; demarrage_pret quand vfs, sd et fat32 servent SD0: ; 30 s au plus. Puis la
; suite du demarrage (etape 2 : services.txt, startup.txt, depuis SD0:)
demarrage_thread:
	mov	x19,300
.w:	adr	x0,demarrage_ok
	ldr	w0,[x0]
	cbnz	w0,.pret
	mov	x0,100
	bl	thread_sleep
	subs	x19,x19,1
	b.ne	.w
	adr	x0,msg_sd0_absent
	bl	kputs
	b	thread_exit
.pret:	adr	x0,msg_sd0_servi
	bl	kputs
	bl	demarrage_suite
	b	thread_exit

; --- demarrage_pret() : init leve le verrou du demarrage -------------------
; (SD0: est servi ; SD0:/sys seulement). Rend la main une fois les options
; et l'ecran appliques (10 s au plus) : Elegant, lance ensuite, prend
; l'ecran a sa taille definitive.
sys_demarrage_pret:
	prologue
	bl	mat_droits
	cbz	x0,.perm
	adr	x0,demarrage_ok
	mov	w1,1
	str	w1,[x0]
	str	x19,[sp,-16]!
	mov	x19,1000
.ecran:	adr	x0,demarrage_ecran
	ldr	w0,[x0]
	cbnz	w0,.pret
	mov	x0,10
	bl	thread_sleep
	subs	x19,x19,1
	b.ne	.ecran
.pret:	ldr	x19,[sp],16
	mov	x0,0
	epilogue
.perm:	mov	x0,E_PERM
	epilogue

; barre d'etat : uptime, charge par coeur, changements de contexte, memoire
status_thread:
.loop:	mov	x0,500
	bl	thread_sleep
	adr	x0,status_buf
	adr	x1,st_uptime
	bl	str_copy
	mov	x19,x0
	adr	x0,ticks
	ldr	x0,[x0]
	mov	x1,HZ
	udiv	x0,x0,x1
	mov	x1,x19
	bl	fmt_dec
	adr	x1,st_s
	bl	str_copy
	mov	x20,0				; charge de chaque coeur
.cpu:	mov	x19,x0
	mov	w1,'C'
	strb	w1,[x19],1
	add	w1,w20,'0'
	strb	w1,[x19],1
	mov	w1,' '
	strb	w1,[x19],1
	mov	x0,x20
	bl	percpu_of
	ldr	x0,[x0,PC_LOAD]
	mov	x1,x19
	bl	fmt_dec
	adr	x1,st_pct
	bl	str_copy
	add	x20,x20,1
	cmp	x20,MAX_CPUS
	b.lo	.cpu
	adr	x1,st_switch
	bl	str_copy
	mov	x19,x0
	mov	x20,0				; total des changements de contexte
	mov	x21,0
.sw:	mov	x0,x20
	bl	percpu_of
	ldr	x0,[x0,PC_SWITCHES]
	add	x21,x21,x0
	add	x20,x20,1
	cmp	x20,MAX_CPUS
	b.lo	.sw
	mov	x0,x21
	mov	x1,x19
	bl	fmt_dec
	adr	x1,st_free
	bl	str_copy
	mov	x1,x0
	adr	x0,pmm_free_pages
	ldr	x0,[x0]
	lsl	x0,x0,2
	bl	fmt_dec
	adr	x1,st_kb
	bl	str_copy
	adr	x0,status_buf
	bl	con_status
	b	.loop

; x0 = message, x1 = valeur : "message 0x...." + fin de ligne (ktrace)
ktrace_print:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	bl	kputs
	mov	x0,x19
	bl	kput_hex
	mov	w0,10
	bl	kputc
	ldr	x19,[sp],16
	epilogue

; Prend SD0:/etc/options.txt (mots « pas_... », pour isoler un pilote sur
; materiel reel ; donne par init : bootfiles.asm) dans opts_buf ; fichier
; absent : tout est actif
opts_load:
	prologue
	str	x19,[sp,-16]!
	adr	x0,path_options
	bl	boot_file_take
	cbz	x0,.out
	mov	x19,x0
	add	x2,x1,0xFFF			; pages a liberer
	lsr	x2,x2,12
	str	x2,[sp,-16]!
	mov	x2,OPTS_MAX-1
	cmp	x1,x2
	csel	x2,x1,x2,lo
	adr	x0,opts_buf
	mov	x1,x19
	strb	wzr,[x0,x2]
	bl	mem_copy
	ldr	x1,[sp],16
	mov	x0,x19
	bl	pmm_free_n
.out:	ldr	x19,[sp],16
	epilogue

; x0 = mot -> x0 = 1 s'il figure dans /sd/etc/options.txt (et l'annonce)
opt_has:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,opts_buf
.start:	ldrb	w2,[x1]
	cbz	w2,.no
	mov	x3,0
.cmp:	ldrb	w4,[x19,x3]
	cbz	w4,.yes
	ldrb	w5,[x1,x3]
	cmp	w4,w5
	b.ne	.next
	add	x3,x3,1
	b	.cmp
.next:	add	x1,x1,1
	b	.start
.yes:	adr	x0,msg_opt
	bl	kputs
	mov	x0,x19
	bl	kputs
	adr	x0,msg_opt2
	bl	kputs
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; « ecran=LxH » de SD0:/etc/options.txt : nouvelle taille d'ecran (de
; 640 x 480 a 2560 x 1600 : fb_size_ok) ; la console se refait a cette taille
screen_option:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	adr	x0,opt_ecran
	mov	x1,sp
	mov	x2,24
	bl	opt_value
	cbz	x0,.out
	mov	x0,sp				; L x H
	bl	.num
	mov	x19,x0
	ldrb	w2,[x1]
	orr	w2,w2,0x20
	cmp	w2,'x'
	b.ne	.out
	add	x0,x1,1
	bl	.num
	mov	x20,x0
	mov	x0,x19
	mov	x1,x20
	bl	fb_size_ok
	cbz	x0,.out
	adr	x0,fb_w				; deja a cette taille ?
	ldp	x0,x1,[x0]
	cmp	x0,x19
	ccmp	x1,x20,0,eq
	b.eq	.out
	mov	x0,x19
	mov	x1,x20
	bl	fb_set
	cbz	x0,.out
	bl	con_init
	adr	x0,msg_ecran
	bl	kputs
	adr	x0,fb_w
	ldr	x0,[x0]
	bl	kput_dec
	mov	w0,'x'
	bl	kputc
	adr	x0,fb_h
	ldr	x0,[x0]
	bl	kput_dec
	adr	x0,msg_ecran_pas		; pas et profondeur obtenus
	bl	kputs
	adr	x0,fb_pitch
	ldr	x0,[x0]
	bl	kput_dec
	adr	x0,msg_ecran_bits
	bl	kputs
	adr	x0,fb_depth
	ldr	w0,[x0]
	bl	kput_dec
	mov	w0,10
	bl	kputc
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue
; (x0 = chiffres -> x0 = valeur, x1 = premier caractere apres)
.num:	mov	x2,0
	mov	x1,x0
.d:	ldrb	w3,[x1]
	sub	w3,w3,'0'
	cmp	w3,9
	b.hi	.e
	mov	x4,10
	madd	x2,x2,x4,x3
	add	x1,x1,1
	b	.d
.e:	mov	x0,x2
	ret

; x0 = cle (« keyb= »), x1 = destination, x2 = taille -> x0 = 1 si la cle
; figure dans /sd/etc/options.txt (valeur copiee jusqu'a la fin de ligne)
opt_value:
	adr	x3,opts_buf
.start:	ldrb	w4,[x3]
	cbz	w4,.no
	mov	x5,0
.cmp:	ldrb	w6,[x0,x5]
	cbz	w6,.yes
	ldrb	w7,[x3,x5]
	cmp	w6,w7
	b.ne	.next
	add	x5,x5,1
	b	.cmp
.next:	add	x3,x3,1
	b	.start
.yes:	add	x3,x3,x5
	sub	x2,x2,1
	mov	x5,0
.v:	cmp	x5,x2
	b.hs	.end
	ldrb	w6,[x3,x5]
	cmp	w6,' '				; fin de la valeur
	b.ls	.end
	strb	w6,[x1,x5]
	add	x5,x5,1
	b	.v
.end:	strb	wzr,[x1,x5]
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; ----------------------------------------------------------------------------
include 'cache.asm'
include 'spinlock.asm'
include 'memmap.asm'
include 'pmm.asm'
include 'modules.asm'
include 'sched.asm'
include 'vm.asm'
include 'proc.asm'
include 'syscall.asm'
include 'threads.asm'			; (apres syscall.asm : check_user)
include 'vmspace.asm'			; (apres syscall.asm : check_user)
include 'preload.asm'			; (apres syscall.asm : check_user)
include 'kits.asm'			; (apres syscall.asm : check_user)
include 'spawn.asm'
include 'ws.asm'
include 'svc.asm'
include 'mapcaller.asm'
include 'hardware.asm'
include 'modules_sys.asm'
include 'gic.asm'
include 'timer.asm'
include 'console.asm'
include 'diag.asm'
include 'bootfiles.asm'
include 'random.asm'			; (apres syscall.asm : check_user)
include 'ipc.asm'			; (apres vmspace.asm : regions)
include 'signal.asm'			; (apres syscall.asm : check_user)
include 'fork.asm'			; (apres syscall.asm : current_proc)
include 'exec.asm'			; (apres syscall.asm : current_proc)
include 'alarm.asm'			; (apres syscall.asm : check_user)
include 'irq.asm'
include 'vectors.asm'
; --- pilotes propres a la carte (arch/<carte>/arch.inc) ---
match =pi4, ARCH { include 'arch/pi4/arch.inc' }
match =pi5, ARCH { include 'arch/pi5/arch.inc' }

; --- donnees ----------------------------------------------------------------
msg_banner	db 'Korin OS - jalon 6 : interface graphique',10
		db '-----------------------------------------------------',10,0
msg_ram		db 'RAM ARM          : ',0
msg_mb		db ' Mo, MMU 4 Ko + caches actifs',10,0
msg_pmm		db 'M',0xC3,0xA9,'moire libre    : ',0
msg_kb		db ' Ko',10,0
msg_cpus	db 'Coeurs en ligne  : ',0
msg_cpus2	db ' (timer 1 kHz par coeur, quantum 10 ms)',10,0
msg_arm		db 'Processeur       : ',0
msg_arm2	db ' MHz -> ',0
msg_mhz		db ' MHz',10,0
msg_sd_ko	db 'Carte SD         : absente ou erreur',10,0
msg_fb_ko	db 'Framebuffer : echec mailbox',10,0
name_status	db 'etat',0
name_reaper	db 'faucheur',0
name_alarm	db 'alarmes',0
mod_nom_init	db 'init',0
nom_demarrage	db 'demarrage',0
		align 4
demarrage_ok	dw 0				; verrou leve par init
demarrage_ecran	dw 0				; options et ecran appliques
msg_init	db 'Init             : services de demarrage (initramfs), pid ',0
msg_sd0_servi	db 'Init             : SD0: servi par les services, suite du demarrage',10,0
msg_sd0_absent	db 'Init             : SD0: non servi apres 30 s, demarrage arrete',10,0
msg_sans_init	db 'Init             : pas de module init (initramfs) : aucun volume, demarrage arrete',10,0
path_options	db 'SD0:/etc/options.txt',0
opt_no_highmem	db 'no_highmem',0
opt_no_preload	db 'no_preload',0
opt_no_irq	db 'no_irq',0
opt_no_sd_dma	db 'no_sd_dma',0
msg_highmem	db 'M',0xC3,0xA9,'moire haute    : +',0
msg_highmem2	db ' Ko (zone haute active), libre ',0
opt_ecran	db 'ecran=',0
msg_ecran	db 'Ecran            : ',0
msg_ecran_pas	db ', pas ',0
msg_ecran_bits	db ', bits ',0
msg_opt		db 'Option           : ',0
msg_opt2	db ' (pilote d',0xC3,0xA9,'sactiv',0xC3,0xA9,')',10,0
msg_boot_done	db 'Noyau            : d',0xC3,0xA9,'marrage termin',0xC3,0xA9,10,0
msg_spawn	db 'Process lanc',0xC3,0xA9,' : ',0
msg_spawn2	db '  pid ',0
st_uptime	db ' uptime ',0
st_s		db ' s | ',0
st_pct		db '% ',0
st_switch	db '| commut. ',0
st_free		db ' | libre ',0
st_kb		db ' Ko',0
		align 8

; --- initrd : applications embarquees (nom, image, taille) -------------------
initrd_table:					; (applications : sur la carte, /sd/apps)
		dd 0, 0, 0
		align 8

; --- BSS (non stocke dans l'image : doit rester en fin de fichier) ----------
		align 4096
bss_start:
pt_l1		rb 4096				; tables de pages (alignees 4 Ko)
pt_l2_low	rb 4096
pt_l2_high	rb 4096
pt_l2_spare	rb 4096 * 2			; RAM au-dela de 1 Go (memmap.asm)
		align 64
percpu_area	rb PERCPU_SIZE * MAX_CPUS
mb_ack		rb 8 * MAX_CPUS			; (signal.asm) reponses a membarrier
irq_table2	rb IE_SIZE * IRQ_MAX		; (irq.asm) second pilote d'une ligne partagee
irq_table	rb IE_SIZE * IRQ_MAX		; (irq.asm) lignes tenues par les pilotes
		align 16
idle_tcbs	rb TCB_SIZE * MAX_CPUS
ram_end		rd 1
fb_base		rd 1
fb_pitch	rd 1
fb_w		rd 1				; taille obtenue (fb_w, fb_h contigus)
fb_h		rd 1
con_cols	rd 1				; console (con_cols, con_rows contigus)
con_rows	rd 1
ticks		rd 1
timer_interval	rd 1
pmm_free_pages	rd 1
pmm_total_pages	rd 1
pmm_bitmap	rd 1				; adresse de la bitmap (apres le noyau)
pmm_npages	rd 1				; pages couvertes par la bitmap
pmm_high_on	rd 1
pmm_zone_low	rb Z_SIZE
pmm_zone_high	rb Z_SIZE
boot_dtb	rd 1
mem_source	rd 1
mem_dtb_n	rd 1
mem_dtb_banks	rd 2 * MEM_BANKS_MAX		; plages du DTB (debut, fin)
mem_nbanks	rd 1
mem_banks	rd 2 * MEM_BANKS_MAX		; RAM au-dela de 1 Go
mem_high_start	rd 1				; (mem_high_start, mem_high_end contigus)
mem_high_end	rd 1
mem_high_bytes	rd 1
mem_nrsv	rd 1
mem_rev_n	rd 1
mem_rev_banks	rd 2 * 2			; d'apres la revision (BCM2711)
board_rev	rd 1				; revision de la carte (mailbox)
mem_rsv		rd 2 * MEM_RSV_MAX
pt_l2_spare_n	rd 1
preload_off	rd 1				; option no_preload (preload.asm)
img_lock	rd 1
img_table	rb IMG_MAX * IM_SIZE		; images prechargees
mod_n		rd 1
mod_table	rb MOD_MAX * ME_SIZE
svc_table	rb SVC_MAX * SV_SIZE		; services nommes (svc.asm)
svc_requetes	rb SR_MAX * SR_SIZE
mc_table	rb MC_MAX * MC_SIZE		; (mapcaller.asm)
		align 16
cpus_online	rd 1
next_tid	rd 1
next_cpu	rd 1
con_x		rd 1				; con_x et con_y contigus (ldp/stp)
con_y		rd 1
con_ready	rd 1
con_utf		rd 1				; UTF-8 en cours : code (bits 0-31), octets restants (32-)
gic_nirq	rw 1
pmm_lock	rw 1
con_lock	rw 1
proc_lock	rw 1
zombie_lock	rw 1
futex_lock	rw 1
		align 8
zombie_list	rd 1
clipboard	rd 1
clipboard_len	rd 1
		align 8
		align 16
asid_hint	rd 1				; ASID (vm.asm) : mot de depart de la recherche
asid_lock	rw 1
		align 8
asid_map	rb ASID_N / 8			; bit n : ASID n pris
proc_tab	rd 1				; table des process (proc.asm : PT_*)
proc_cap	rd 1				; ses emplacements (0 : pas encore de table)
proc_next	rd 1				; (proc_register) prochain emplacement essaye
clock_base	rd 2			; secondes au demarrage, decalage local (SYS_CLOCK_*)
		align 8
status_buf	rb 160
		align 16
sector_buf	rb 512
OPTS_MAX	= 512
opts_buf	rb OPTS_MAX			; /sd/etc/options.txt
		align 8
klog_len	rd 1
klog_user	rd 1				; journal : 2 pendant un print (console.asm)
klog_victim	rd 1				; journal : case suivante a vider (console.asm)
klog_slots	rb 256 * 8			; journal : une ligne en cours par ecrivain
		align 8
buf_table	rb BUF_MAX * BF_SIZE		; tampons partages (ws.asm)
klog_buf	rb KLOG_SIZE			; journal du noyau
		align 8
ipc_seq		rd 1				; IPC (ipc.asm) : avance a chaque reveil
ipc_dead	rd 1				; bouts de canaux a fermer
ipc_rings	rd 1				; anneaux de reception alloues
ipc_shm_ids	rd 1				; numeros des objets partages
ipcw_lock	rw 1
ipcd_lock	rw 1
isn_lock	rw 1
		align 8
ipcw_table	rb IW_MAX * IW_SIZE		; threads qui attendent un canal
isn_table	rb ISN_MAX * ISN_SIZE		; noms des objets partages (shm_open)
if BOOT_STACK_BSS
		align 16
boot_stack	rb 128 * 1024			; pile de boot du coeur 0 (Pi 5)
boot_stack_top:
end if
		align 4096
bss_end:
kernel_end:
