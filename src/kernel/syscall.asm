; ============================================================================
;  syscall.asm - appels systeme
;
;  ABI : svc 0, x8 = numero, x0..x5 = arguments, retour dans x0
;  (negatif = erreur). Tout pointeur EL0 est verifie avant usage.
;  Le noyau ne touche jamais les registres FP/SIMD du process.
; ============================================================================

E_INVAL		= -22
E_FAULT		= -14
E_NOMEM		= -12
E_AGAIN		= -11
E_NOSYS		= -38
E_NOENT		= -2
E_BADF		= -9
E_EXIST		= -17
E_NOTDIR	= -20
E_ISDIR		= -21
E_MFILE		= -24
E_NOSPC		= -28
E_NOTEMPTY	= -39
E_NODEV		= -19
E_TIMEDOUT	= -110

include '../inc/syscalls.inc'		; numeros des appels (SYS_*), table en fin de fichier

; x0 = trame d'exception
syscall_dispatch:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x8,[x19,FR_X8]
	cmp	x8,SYSCALL_COUNT
	b.hs	.nosys
	adr	x9,syscall_table
	ldr	x9,[x9,x8,lsl 3]
	ldp	x0,x1,[x19,FR_X0]
	ldp	x2,x3,[x19,FR_X0+16]
	ldp	x4,x5,[x19,FR_X0+32]
	blr	x9
	b	.ret
.nosys:	mov	x0,E_NOSYS
.ret:	str	x0,[x19,FR_X0]
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = TCB courant, x1 = PCB courant
macro current_proc {
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_PROC]
}

; x0 = adresse EL0, x1 = taille -> saute a .fault de l'appelant si invalide
macro check_user addr*, size*, fail* {
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	mov	x2,size
	mov	x1,addr
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	ldr	x0,[x0,TH_PROC]
	bl	vm_check_user
	mov	x9,x0
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	cbz	x9,fail
}

; x0 = chaine EL0, x1 = tampon noyau, x2 = taille du tampon -> x0 = longueur,
; E_INVAL si trop longue, E_FAULT si illisible (chaque page verifiee)
copy_str_from_user:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	sub	x23,x2,1
	mov	x21,0
	mov	x22,0				; 0 : page a verifier
.c:	cmp	x21,x23
	b.hs	.long
	add	x0,x19,x21
	tst	x0,0xFFF
	b.ne	.chk_done
	mov	x22,0				; debut de page : re-verifier
.chk_done:
	cbnz	x22,.read
	this_cpu x2				; nouvelle page : verification
	ldr	x2,[x2,PC_CURRENT]
	ldr	x2,[x2,TH_PROC]
	mov	x1,x0
	mov	x0,x2
	mov	x2,1
	bl	vm_check_user
	cbz	x0,.fault
	mov	x22,1
.read:	ldrb	w0,[x19,x21]
	strb	w0,[x20,x21]
	cbz	w0,.done
	add	x21,x21,1
	b	.c
.done:	mov	x0,x21
	b	.out
.long:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_EXIT exit(code) -----------------------------------------------------------
sys_exit:
	mov	x19,x0
	current_proc
	mov	x0,x1
	mov	x1,x19
	bl	proc_kill
	b	thread_exit

; --- SYS_PRINT print(chaine, longueur) : console texte du noyau ----------------------
sys_print:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x20,4096
	b.hi	.inval
	check_user x19,x20,.fault
	adr	x0,con_lock			; (sortie standard dans un tube : AppKit)
	bl	spin_lock_irqsave
	mov	x21,x0
	adr	x0,klog_user			; journal : la ligne du process, pas
	mov	x1,2				; celle du noyau pour lui (console.asm)
	str	x1,[x0]
.c:	cbz	x20,.done
	ldrb	w0,[x19],1
	bl	kputc_raw
	sub	x20,x20,1
	b	.c
.done:	adr	x0,klog_user
	str	xzr,[x0]
	adr	x0,con_lock
	mov	x1,x21
	bl	spin_unlock_irqrestore
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_SLEEP sleep(ms) ------------------------------------------------------------
sys_sleep:
	prologue
	bl	thread_sleep
	this_cpu x0
	ldr	x0,[x0,PC_CURRENT]
	bl	sig_ready
	cbz	x0,.out
	mov	x0,E_INTR			; un signal l'a reveille
	epilogue
.out:	mov	x0,0
	epilogue

; --- SYS_YIELD yield() --------------------------------------------------------------
sys_yield:
	prologue
	bl	thread_yield
	mov	x0,0
	epilogue

; --- SYS_TIME_MS time_ms() ------------------------------------------------------------
sys_time_ms:
	b	time_ms

; --- SYS_CLOCK_GET clock_get(quoi) ----------------------------------------------------
; L'horloge du systeme, en secondes (docs/FICHIERS.md, dates) : clock_base + temps depuis le
; demarrage. 0 : UTC, 1 : decalage de l'heure locale, 2 : heure locale.
sys_clock_get:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	time_ms
	mov	x1,1000
	udiv	x0,x0,x1
	adr	x1,clock_base
	ldp	x1,x2,[x1]
	add	x0,x0,x1
	cmp	x19,1
	b.ne	.pas1
	mov	x0,x2
	b	.out
.pas1:	cmp	x19,2
	b.ne	.out
	add	x0,x0,x2
.out:	ldr	x19,[sp],16
	epilogue

; --- SYS_CLOCK_SET clock_set(secondes UTC, decalage) -> 0 ------------------------------
; Par netd (NTP) et init2 (SD0:/etc/lastclock) : droits de SD0:/sys. L'heure ne recule jamais :
; une date de fichier doit rester dans l'ordre des ecritures (make).
sys_clock_set:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	current_proc
	ldr	x0,[x1,P_DROITS]
	cbz	x0,.perm
	bl	time_ms
	mov	x1,1000
	udiv	x2,x0,x1
	adr	x1,clock_base
	ldr	x3,[x1]
	add	x4,x3,x2
	cmp	x19,x4
	b.ls	.dec
	sub	x3,x19,x2
	str	x3,[x1]
.dec:	str	x20,[x1,8]
	mov	x0,0
	b	.out
.perm:	mov	x0,E_PERM
.out:	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_SBRK sbrk(increment) -> ancienne limite du tas (ou erreur) ------------------
SBRK_WAITS	= 32			; mem_wait au plus (16 Mo chacun)
sys_sbrk:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x23,SBRK_WAITS
	current_proc
	mov	x20,x1
.retry:	add	x0,x20,P_LOCK
	bl	spin_lock_irqsave
	mov	x22,x0
	ldr	x21,[x20,P_BRK]
	cbz	x19,.ok				; sbrk(0) : simple lecture
	add	x2,x21,x19
	movl	x3,KIT_VA_BASE			; (au-dessus : kits, regions de VM_MAP)
	cmp	x2,x3
	b.hi	.nomem
	mov	x0,x20
	mov	x1,x21
	mov	x2,x19
	movl	x3,VM_RW
	bl	vm_alloc_range
	cbz	x0,.short
	add	x0,x21,x19
	str	x0,[x20,P_BRK]
.ok:	mov	x19,x21
	b	.unlock
.short:	add	x0,x20,P_LOCK			; plus de page : les services en rendent ?
	mov	x1,x22				; (les pages deja prises restent)
	bl	spin_unlock_irqrestore
	subs	x23,x23,1
	b.lo	.nowait
	bl	mem_wait
	cbnz	x0,.retry
.nowait:
	mov	x0,E_NOMEM
	b	.out
.nomem:	mov	x19,E_NOMEM
.unlock:
	add	x0,x20,P_LOCK
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x19
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_GETPID getpid() -------------------------------------------------------------
sys_getpid:
	current_proc
	ldr	x0,[x1,P_PID]
	ret

; --- SYS_SYSINFO sysinfo(tampon) : etat du systeme ------------------------------------
;  +0 coeurs, +8 Ko libres, +16 Ko total, +24 ms depuis demarrage,
;  +32 nombre de process, +40..+71 charge des 4 coeurs (%), +72 commutations
SYSINFO_SIZE	= 80
sys_sysinfo:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	check_user x19,SYSINFO_SIZE,.fault
	adr	x0,cpus_online
	ldr	x0,[x0]
	str	x0,[x19]
	adr	x0,pmm_free_pages
	ldr	x0,[x0]
	lsl	x0,x0,2
	str	x0,[x19,8]
	adr	x0,pmm_total_pages		; RAM geree (zones basse et haute)
	ldr	x0,[x0]
	lsl	x0,x0,2
	str	x0,[x19,16]
	bl	time_ms
	str	x0,[x19,24]
	adr	x1,proc_cap			; process vivants
	ldar	x5,[x1]
	adr	x1,proc_tab
	ldr	x1,[x1]
	mov	x2,0
	mov	x3,0
.p:	cmp	x2,x5
	b.hs	.pf
	lsl	x4,x2,5
	ldr	x4,[x1,x4]			; (PT_PCB)
	cbz	x4,.pn
	add	x3,x3,1
.pn:	add	x2,x2,1
	b	.p
.pf:	str	x3,[x19,32]
	mov	x20,0
	mov	x3,0
.cpu:	mov	x0,x20
	bl	percpu_of
	ldr	x1,[x0,PC_LOAD]
	add	x2,x19,40
	str	x1,[x2,x20,lsl 3]
	ldr	x1,[x0,PC_SWITCHES]
	add	x3,x3,x1
	add	x20,x20,1
	cmp	x20,MAX_CPUS
	b.lo	.cpu
	str	x3,[x19,72]
	mov	x0,0
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x19,x20,[sp],16
	epilogue

; Appele par le faucheur avant la liberation d'un process (x0 = PCB)
proc_on_exit:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	ws_proc_exit
	mov	x0,0				; (s'il etait un service)
	mov	x1,x19
	bl	mc_release
	mov	x0,x19
	bl	svc_proc_exit
	mov	x0,x19
	bl	sig_proc_exit
	mov	x0,x19
	bl	irq_proc_exit
	adr	x0,msg_pexit
	bl	kputs
	ldr	x0,[x19,P_PID]
	bl	kput_dec
	mov	w0,' '
	bl	kputc
	add	x0,x19,P_NAME
	bl	kputs
	adr	x0,msg_pexit2
	bl	kputs
	ldr	x0,[x19,P_EXIT_CODE]
	bl	kput_sdec
	mov	w0,')'
	bl	kputc
	mov	w0,10
	bl	kputc
	ldr	x19,[sp],16
	epilogue

; x0 = valeur signee, en decimal
kput_sdec:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	tbz	x19,63,.pos
	mov	w0,'-'
	bl	kputc
	neg	x19,x19
.pos:	mov	x0,x19
	bl	kput_dec
	ldr	x19,[sp],16
	epilogue

; --- SYS_PROCLIST proclist(tampon, max) -> nombre ; entrees de 64 o :
;     +0 pid, +8 threads, +16 Ko de tas, +24 nom (32 o), +56 etat
; Les premiers process seulement si max est trop petit : SYS_PROCLIST_FROM
; donne la suite.
sys_proclist:
	mov	x2,0

; --- SYS_PROCLIST_FROM proclist_from(tampon, max, depuis) -> nombre ; les
; process a partir de l'emplacement depuis (0 : le premier) ; la suite :
; depuis = (pid de la derniere entree) & 0xFFFF
sys_proclist_from:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x23,x2
	mov	x0,PROC_SLOTS_MAX
	cmp	x20,x0
	csel	x20,x0,x20,hi
	lsl	x1,x20,6
	check_user x19,x1,.fault
	adr	x0,proc_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	adr	x1,proc_tab
	ldr	x1,[x1]
	adr	x24,proc_cap
	ldr	x24,[x24]
	mov	x2,x23				; emplacement
	mov	x21,0				; entrees ecrites
.p:	cmp	x2,x24
	b.hs	.done
	cmp	x21,x20
	b.hs	.done
	lsl	x3,x2,5
	ldr	x3,[x1,x3]			; (PT_PCB)
	add	x2,x2,1
	cbz	x3,.p
	add	x4,x19,x21,lsl 6
	bl	proc_entry
	add	x21,x21,1
	b	.p
.done:	adr	x0,proc_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x3 = PCB, x4 = entree de 64 o (SYS_PROCLIST) : remplie ; ne touche que
; x5-x7
proc_entry:
	ldr	x5,[x3,P_PID]
	str	x5,[x4]
	ldr	x5,[x3,P_NTHREADS]
	str	x5,[x4,8]
	ldr	x5,[x3,P_BRK]
	ldr	x6,[x3,P_BRK_START]
	sub	x5,x5,x6
	lsr	x5,x5,10
	str	x5,[x4,16]
	mov	x5,0
.n:	add	x6,x3,P_NAME
	ldrb	w6,[x6,x5]
	add	x7,x4,24
	strb	w6,[x7,x5]
	add	x5,x5,1
	cmp	x5,32
	b.lo	.n
	ldr	x5,[x3,P_STATE]
	str	x5,[x4,56]
	ret

; --- SYS_PROC_INFO proc_info(pid, sortie 64 o) -> 0 / E_SRCH ; l'entree de
; SYS_PROCLIST de ce process
sys_proc_info:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	check_user x20,64,.fault
	adr	x0,proc_lock
	bl	spin_lock_irqsave
	mov	x22,x0
	mov	x0,x19
	bl	proc_lookup
	mov	x21,-3				; (E_SRCH)
	cbz	x0,.rendre
	mov	x3,x0
	mov	x4,x20
	bl	proc_entry
	mov	x21,0
.rendre:
	adr	x0,proc_lock
	mov	x1,x22
	bl	spin_unlock_irqrestore
	mov	x0,x21
	b	.out
.fault:	mov	x0,E_FAULT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- SYS_KILL kill(pid) ----------------------------------------------------------------
sys_kill:
	prologue
	bl	proc_lookup
	cbz	x0,.inval
	mov	x1,-9
	bl	proc_kill
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	epilogue

sys_nosys:
	mov	x0,E_NOSYS
	ret

msg_pexit	db '[pid ',0
msg_pexit2	db '] termin',0xC3,0xA9,' (code ',0
		align 8

; Table : numeros de src/inc/syscalls.inc (SYS_*), groupes par domaine sur le
; chiffre hexadecimal de tete ; sysent remplit la place libre en fin de plage
; par sys_nosys (E_NOSYS) et refuse un numero deja passe.
macro sysent n*, f* {
	repeat (n) - ($ - syscall_table) / 8
		dd sys_nosys
	end repeat
	assert ($ - syscall_table) / 8 = (n)
		dd f
}

syscall_table:
	sysent SYS_SBRK, sys_sbrk				; 0x00 memoire
	sysent SYS_BUF_CREATE, sys_buf_create			;  (ws.asm)
	sysent SYS_BUF_GRANT, sys_buf_grant
	sysent SYS_BUF_MAP, sys_buf_map
	sysent SYS_BUF_FREE, sys_buf_free
	sysent SYS_DMA_ALLOC, sys_dma_alloc			;  (hardware.asm)
	sysent SYS_VM_MAP, sys_vm_map				;  (vmspace.asm)
	sysent SYS_VM_UNMAP, sys_vm_unmap
	sysent SYS_VM_PROTECT, sys_vm_protect
	sysent SYS_VM_ADVISE, sys_vm_advise
	sysent SYS_VM_QUERY, sys_vm_query
	sysent SYS_VM_STATS, sys_vm_stats
	sysent SYS_VM_PAGEINFO, sys_vm_pageinfo
	sysent SYS_BUF_INFO, sys_buf_info			;  (ws.asm)
	sysent SYS_EXIT, sys_exit				; 0x10 process et temps
	sysent SYS_GETPID, sys_getpid
	sysent SYS_SPAWN_IMAGE, sys_spawn_image			;  (spawn.asm)
	sysent SYS_WAIT, sys_wait
	sysent SYS_KILL, sys_kill
	sysent SYS_PROCLIST, sys_proclist
	sysent SYS_PROC_FLAGS, sys_proc_flags
	sysent SYS_SLEEP, sys_sleep
	sysent SYS_YIELD, sys_yield
	sysent SYS_TIME_MS, sys_time_ms
	sysent SYS_SPAWN_PRELOADED, sys_spawn_preloaded		;  (preload.asm)
	sysent SYS_CLOCK_GET, sys_clock_get
	sysent SYS_CLOCK_SET, sys_clock_set
	sysent SYS_PROC_RIGHTS, sys_proc_rights
	sysent SYS_PROC_INFO, sys_proc_info
	sysent SYS_PROCLIST_FROM, sys_proclist_from
	sysent SYS_THREAD_CREATE, sys_thread_create		; 0x20 threads et futex
	sysent SYS_THREAD_EXIT, sys_thread_exit			;  (threads.asm)
	sysent SYS_FUTEX_WAIT, sys_futex_wait
	sysent SYS_FUTEX_WAKE, sys_futex_wake
	sysent SYS_THREAD_CREATE_EX, sys_thread_create_ex
	sysent SYS_THREAD_JOIN, sys_thread_join
	sysent SYS_THREAD_SELF, sys_thread_self
	sysent SYS_THREAD_SET_PRIORITY, sys_thread_set_priority
	sysent SYS_THREAD_SET_NAME, sys_thread_set_name
	sysent SYS_THREAD_KILL, sys_thread_kill
	sysent SYS_THREAD_LIST, sys_thread_list
	sysent SYS_SIG_ACTION, sys_sig_action			;  (signal.asm)
	sysent SYS_SIG_SEND, sys_sig_send
	sysent SYS_SIG_RETURN, sys_sig_return
	sysent SYS_SIG_MASK, sys_sig_mask
	sysent SYS_SIG_ALTSTACK, sys_sig_altstack
	sysent SYS_SVC_REGISTER, sys_svc_register		; 0x30 IPC (svc.asm)
	sysent SYS_SVC_OPEN, sys_svc_open
	sysent SYS_SVC_CALL, sys_svc_call
	sysent SYS_SVC_NEXT, sys_svc_next
	sysent SYS_SVC_REPLY, sys_svc_reply
	sysent SYS_SVC_SIGNAL, sys_svc_signal
	sysent SYS_SVC_MAP_CALLER, sys_svc_map_caller		; (mapcaller.asm)
	sysent SYS_SVC_UNMAP_CALLER, sys_svc_unmap_caller
	sysent SYS_SVC_CALLER_PHYS, sys_svc_caller_phys
	sysent SYS_SVC_FORWARD, sys_svc_forward			;  (svc.asm)
	sysent SYS_DEV_MAP, sys_dev_map				; 0x40 materiel, demarrage
	sysent SYS_FW_CALL, sys_fw_call
	sysent SYS_MOD_LANCER, sys_mod_lancer			;  (modules_sys.asm)
	sysent SYS_MOD_LIRE, sys_mod_lire
	sysent SYS_DEMARRAGE_PRET, sys_demarrage_pret
	sysent SYS_BOOT_FILE, sys_boot_file			;  (bootfiles.asm)
	sysent SYS_KIT_REGISTER, sys_kit_register		;  (kits.asm)
	sysent SYS_IMAGE_PRELOAD, sys_image_preload		;  (preload.asm)
	sysent SYS_IMAGE_FORGET, sys_image_forget
	sysent SYS_IRQ_BIND, sys_irq_bind			;  (irq.asm)
	sysent SYS_IRQ_ACK, sys_irq_ack
	sysent SYS_IRQ_WAIT, sys_irq_wait
	sysent SYS_IRQ_UNBIND, sys_irq_unbind
	sysent SYS_WS_REGISTER, sys_ws_register			; 0x50 graphique (ws.asm)
	sysent SYS_WS_CALL, sys_ws_call
	sysent SYS_WS_NEXT, sys_ws_next
	sysent SYS_WS_REPLY, sys_ws_reply
	sysent SYS_WS_POST, sys_ws_post
	sysent SYS_WS_DISPLAY, sys_ws_display
	sysent SYS_EV_WAIT, sys_ev_wait
	sysent SYS_SCREEN_INFO, sys_screen_info
	sysent SYS_CLIP_SET, sys_clip_set
	sysent SYS_CLIP_GET, sys_clip_get
	sysent SYS_WS_SIGNAL, sys_ws_signal
	sysent SYS_WS_MODE, sys_ws_mode
	sysent SYS_PRINT, sys_print				; 0x60 systeme
	sysent SYS_KLOG, sys_klog				;  (console.asm)
	sysent SYS_SYSINFO, sys_sysinfo
	sysent SYS_GETRANDOM, sys_getrandom			;  (random.asm)
	sysent SYS_LIB_OPEN, sys_lib_open			;  (kits.asm)
	sysent SYS_IMAGE_LIST, sys_image_list			;  (preload.asm)
	sysent SYS_IMAGE_CREATE, sys_image_create
	sysent SYS_IMAGE_SEAL, sys_image_seal
	sysent SYS_MEMBARRIER, sys_membarrier
	sysent SYS_HANDLE_CLOSE, sys_handle_close		; 0x70 IPC entre process (ipc.asm)
	sysent SYS_HANDLE_INFO, sys_handle_info
	sysent SYS_HANDLE_WAIT, sys_handle_wait
	sysent SYS_HANDLE_LIST, sys_handle_list
	sysent SYS_HANDLE_INHERIT, sys_handle_inherit
	sysent SYS_CHAN_CREATE, sys_chan_create
	sysent SYS_CHAN_SEND, sys_chan_send
	sysent SYS_CHAN_RECV, sys_chan_recv
	sysent SYS_CHAN_SHUTDOWN, sys_chan_shutdown
	sysent SYS_SHM_CREATE, sys_shm_create
	sysent SYS_SHM_OPEN, sys_shm_open
	sysent SYS_SHM_UNLINK, sys_shm_unlink
	sysent SYS_SHM_CTL, sys_shm_ctl
	sysent SYS_SHM_MAP, sys_shm_map
SYSCALL_COUNT	= ($ - syscall_table) / 8			; 0x80- : AppKit (E_NOSYS)
