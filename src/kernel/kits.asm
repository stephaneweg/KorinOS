; ============================================================================
;  kits.asm - bibliotheques partagees (« kits », comme dans Onyx)
;
;  Un kit est un ELF64 lie a 0 (SD0:/lib/<nom>.so) : segment lecture +
;  execution (code, table d'exportation), segment lecture + ecriture
;  (variables) sur des pages distinctes ; code sans adresse absolue (adr).
;  Le point d'entree est la table d'exportation : 'AKIT', version, puis les
;  decalages des fonctions depuis la table (docs/KITS.md).
;
;  * Charge une fois pour tout le systeme, a une place fixe : KIT_VA_BASE +
;    n x KIT_SLOT, la meme dans tous les process.
;  * Dans chaque process : code mappe partage (PTE_SHARED : jamais libere par
;    vm_destroy), variables recopiees dans des pages privees.
;  * Appel systeme 54 lib_open(nom, version_min) -> adresse de la table
;    (kit_find : deja en memoire, ou module de demarrage : appkit.so) ; 99
;    kit_register(nom, image, taille) : un kit lu par AppKit (droits de
;    SD0:/sys ; sinon par init2). Le noyau ne lit aucun fichier
;    (docs/FICHIERS.md, etape 5) ; un kit charge reste en memoire.
; ============================================================================

KIT_MAX		= 16
KIT_VA_BASE	= 0x5000000000		; L1[320] : moitie utilisateur
KIT_SLOT	= 0x4000000		; 64 Mo par kit (GlKit : Mesa, 16 Mo et plus) ; 16 x 64 Mo = L1[320]
KIT_MAGIC	= 0x54494B41		; « AKIT »
KIT_NAME_MAX	= 24

; --- kit charge (KT_SIZE octets) ----------------------------------------------
KT_NAME		= 0			; nom (KIT_NAME_MAX octets, 0 final)
KT_USED		= 32
KT_RX		= 40			; pages physiques du code (contigues)
KT_RX_SIZE	= 48			; octets, multiple de PAGE_SIZE
KT_RW_VA	= 56			; debut des variables (decalage, page)
KT_RW		= 64			; copie initiale des variables (noyau)
KT_RW_SIZE	= 72
KT_ENTRY	= 80			; decalage de la table d'exportation
KT_VERSION	= 88
KT_SIZE		= 96

; --- lib_open(nom, version_min) -> adresse de la table d'exportation -------
; (= kit_find, docs/FICHIERS.md etape 5) : un kit deja en memoire, ou un kit
; des modules de demarrage (appkit.so : initramfs) ; sinon E_NOENT : le
; programme le fait lire par AppKit (kits_open, f_kit_load) puis l'inscrit
; par kit_register (99). Le kit est mappe dans le process appelant.
sys_lib_open:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,32
	mov	x20,x1				; version minimale
	mov	x1,sp				; nom (court, minuscules, chiffres, _)
	mov	x2,KIT_NAME_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.out
	mov	x0,sp
	bl	kit_name_ok
	cbz	x0,.inval
	adr	x0,kits_mutex
	bl	mutex_lock
	mov	x0,sp
	mov	x1,0				; (pas d'image : modules seulement)
	bl	kit_find_or_load		; -> x0 = indice, < 0 si erreur
	tbnz	x0,63,.unlock
	mov	x1,x20
	bl	kit_map_current
.unlock:
	mov	x19,x0
	adr	x0,kits_mutex
	bl	mutex_unlock
	mov	x0,x19
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; --- kit_register(nom, image, taille) -> adresse de la table --------------
; Inscrit un kit lu par AppKit (SD0:/lib/<nom>.so, ATTR_LIB de vfs) : image
; recopiee dans des pages du noyau, chargee, puis mappee dans l'appelant.
; Droits de SD0:/sys seulement (un kit est partage par tous les process) :
; sans eux E_PERM, AppKit passe par init2 (INIT_OP_KIT_LOAD). Un nom deja
; inscrit : rien n'est recharge, l'existant est mappe (deux chargements en
; meme temps donnent le meme kit).
sys_kit_register:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	this_cpu x0				; (current_proc : defini plus loin)
	ldr	x0,[x0,PC_CURRENT]
	ldr	x1,[x0,TH_PROC]
	ldr	x0,[x1,P_DROITS]
	cbz	x0,.perm
	mov	x0,x19
	mov	x1,sp
	mov	x2,KIT_NAME_MAX
	bl	copy_str_from_user
	tbnz	x0,63,.out
	mov	x0,sp
	bl	kit_name_ok
	cbz	x0,.inval
	cmp	x21,64
	b.lo	.inval
	mov	x0,KIT_SLOT
	cmp	x21,x0
	b.hi	.inval
	check_user x20,x21,.fault
	add	x0,x21,0xFFF			; l'image, dans des pages du noyau
	lsr	x23,x0,12			; (un autre thread de l'appelant
	mov	x0,x23				; pourrait la changer)
	bl	pmm_alloc_n
	cbz	x0,.nomem
	mov	x22,x0
	mov	x1,x20
	mov	x2,x21
	bl	mem_copy
	adr	x0,kits_mutex
	bl	mutex_lock
	mov	x0,sp
	mov	x1,x22
	mov	x2,x21
	bl	kit_find_or_load
	tbnz	x0,63,.unlock
	mov	x1,0
	bl	kit_map_current
.unlock:
	mov	x24,x0
	adr	x0,kits_mutex
	bl	mutex_unlock
	mov	x0,x22				; la copie n'est plus utile
	mov	x1,x23
	bl	pmm_free_n
	mov	x0,x24
	b	.out
.perm:	mov	x0,E_PERM
	b	.out
.nomem:	mov	x0,E_NOMEM
	b	.out
.fault:	mov	x0,E_FAULT
	b	.out
.inval:	mov	x0,E_INVAL
.out:	add	sp,sp,32
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom (noyau) -> x0 = 1 s'il est valable : 1 a 23 caracteres a-z 0-9 _
kit_name_ok:
	mov	x1,0
.c:	ldrb	w2,[x0,x1]
	cbz	w2,.fin
	cmp	w2,'_'
	b.eq	.ok
	sub	w3,w2,'a'
	cmp	w3,25
	b.ls	.ok
	sub	w3,w2,'0'
	cmp	w3,9
	b.hi	.non
.ok:	add	x1,x1,1
	b	.c
.fin:	cbz	x1,.non
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = indice d'un kit charge, x1 = version minimale (0 : toutes) -> x0 =
; adresse de sa table dans le process courant (mappe si besoin), < 0 si
; erreur (sous kits_mutex)
kit_map_current:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x2,kit_table
	mov	x3,KT_SIZE
	madd	x21,x19,x3,x2
	ldr	x0,[x21,KT_VERSION]
	cmp	x0,x1
	b.lo	.old
	this_cpu x0				; deja mappe dans ce process ?
	ldr	x0,[x0,PC_CURRENT]
	ldr	x22,[x0,TH_PROC]
	ldr	x0,[x22,P_KITS]
	lsr	x0,x0,x19
	tbnz	x0,0,.addr
	mov	x0,x22
	mov	x1,x19
	bl	kit_map
	cbz	x0,.nomem
	ldr	x0,[x22,P_KITS]
	mov	x1,1
	lsl	x1,x1,x19
	orr	x0,x0,x1
	str	x0,[x22,P_KITS]
.addr:	movl	x0,KIT_VA_BASE			; adresse de la table dans le process
	mov	x1,KIT_SLOT
	madd	x0,x19,x1,x0
	ldr	x1,[x21,KT_ENTRY]
	add	x0,x0,x1
	b	.out
.old:	mov	x0,E_INVAL			; version trop ancienne
	b	.out
.nomem:	mov	x0,E_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom (noyau), x1 = image ELF a inscrire (0 : aucune), x2 = sa taille
; -> x0 = indice du kit, < 0 si erreur (sous kits_mutex). Un kit deja charge
; est rendu tel quel ; sinon l'image donnee, ou le module de demarrage
; <nom>.so (initramfs : appkit.so), est chargee ; sans l'une ni l'autre :
; E_NOENT. L'image reste a l'appelant (un module reste en memoire).
kit_find_or_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	mov	x23,x1
	mov	x24,x2
	mov	x20,0				; recherche
	mov	x21,-1				; premiere place libre
.find:	cmp	x20,KIT_MAX
	b.hs	.notfound
	adr	x0,kit_table
	mov	x1,KT_SIZE
	madd	x22,x20,x1,x0
	ldr	x0,[x22,KT_USED]
	cbnz	x0,.cmp
	cmn	x21,1
	csel	x21,x20,x21,eq
	b	.next
.cmp:	mov	x0,x19
	add	x1,x22,KT_NAME
	bl	str_equal
	cbnz	x0,.found
.next:	add	x20,x20,1
	b	.find
.found:	mov	x0,x20
	b	.out
.notfound:
	tbnz	x21,63,.full
	cbnz	x23,.image			; image donnee (kit_register)
	mov	x0,sp				; module de demarrage <nom>.so
	mov	x1,x19
	bl	str_copy_n
	adr	x1,kit_ext
	bl	str_copy_n
	mov	x0,sp
	bl	module_find
	cbz	x0,.noent
	ldp	x23,x24,[x0,ME_ADR]		; image, taille (gardee)
.image:
	adr	x0,kit_table
	mov	x1,KT_SIZE
	madd	x22,x21,x1,x0
	mov	x0,x22
	mov	x1,x23
	mov	x2,x24
	bl	kit_load_image
	cbz	x0,.bad
	add	x0,x22,KT_NAME
	mov	x1,x19
	bl	str_copy_n
	mov	x0,1
	str	x0,[x22,KT_USED]
	adr	x0,kit_msg
	bl	kputs
	add	x0,x22,KT_NAME
	bl	kputs
	adr	x0,kit_msg2
	bl	kputs
	ldr	x0,[x22,KT_VERSION]
	bl	kput_dec
	adr	x0,kit_msg3
	bl	kputs
	ldr	x0,[x22,KT_RX_SIZE]
	lsr	x0,x0,10
	bl	kput_dec
	adr	x0,kit_msg4
	bl	kputs
	mov	x0,x21
	b	.out
.bad:	adr	x0,kit_msg_bad
	bl	kputs
	mov	x0,x19
	bl	kputs
	mov	w0,10
	bl	kputc
	mov	x0,E_INVAL
	b	.out
.noent:	mov	x0,E_NOENT
	b	.out
.full:	mov	x0,E_NOMEM
.out:	add	sp,sp,64
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree KT_*, x1 = image ELF, x2 = taille -> x0 = 1 si le kit est pret
; Recopie le code dans des pages physiques (rendues executables) et garde
; une copie des variables initiales.
kit_load_image:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cmp	x21,64				; --- en-tete ELF64 AArch64 executable
	b.lo	.bad
	ldr	w0,[x20]
	movl	w1,0x464C457F
	cmp	w0,w1
	b.ne	.bad
	ldrb	w0,[x20,4]
	cmp	w0,2
	b.ne	.bad
	ldrh	w0,[x20,16]
	cmp	w0,ET_EXEC
	b.ne	.bad
	ldrh	w0,[x20,18]
	cmp	w0,EM_AARCH64
	b.ne	.bad
	str	xzr,[x19,KT_RX_SIZE]
	str	xzr,[x19,KT_RW_SIZE]
	ldr	x22,[x20,32]			; --- segments
	add	x22,x20,x22
	ldrh	w23,[x20,56]
.ph:	cbz	w23,.ph_done
	ldr	w0,[x22]
	cmp	w0,PT_LOAD
	b.ne	.ph_next
	ldr	x24,[x22,16]			; p_vaddr
	ldr	x25,[x22,40]			; p_memsz
	ldr	x0,[x22,8]			; p_offset + p_filesz dans le fichier
	ldr	x1,[x22,32]
	add	x0,x0,x1
	cmp	x0,x21
	b.hi	.bad
	add	x0,x24,x25			; dans la place du kit
	mov	x1,KIT_SLOT
	cmp	x0,x1
	b.hi	.bad
	ldr	w0,[x22,4]			; p_flags
	tbnz	w0,1,.rw			; PF_W : variables
	cbnz	x24,.bad			; code : lie a 0
	ldr	x0,[x19,KT_RX_SIZE]
	cbnz	x0,.bad				; un seul segment de code
	add	x26,x25,0xFFF
	and	x26,x26,not 0xFFF
	str	x26,[x19,KT_RX_SIZE]
	lsr	x0,x26,12
	bl	pmm_alloc_n
	cbz	x0,.bad
	str	x0,[x19,KT_RX]
	mov	x1,x26
	bl	mem_zero
	ldr	x0,[x19,KT_RX]			; contenu du fichier
	ldr	x1,[x22,8]
	add	x1,x20,x1
	ldr	x2,[x22,32]
	bl	mem_copy
	ldr	x0,[x19,KT_RX]			; visible par le cache d'instructions
	mov	x1,x26
	bl	icache_sync_range
	b	.ph_next
.rw:	ldr	x0,[x19,KT_RW_SIZE]
	cbnz	x0,.bad				; un seul segment de variables
	and	x0,x24,not 0xFFF		; premiere page
	str	x0,[x19,KT_RW_VA]
	add	x26,x24,x25			; fin, a la page
	add	x26,x26,0xFFF
	and	x26,x26,not 0xFFF
	sub	x26,x26,x0
	str	x26,[x19,KT_RW_SIZE]
	lsr	x0,x26,12
	bl	pmm_alloc_n
	cbz	x0,.bad
	str	x0,[x19,KT_RW]
	mov	x1,x26
	bl	mem_zero
	ldr	x0,[x19,KT_RW]
	and	x1,x24,0xFFF			; meme decalage dans la page
	add	x0,x0,x1
	ldr	x1,[x22,8]
	add	x1,x20,x1
	ldr	x2,[x22,32]
	bl	mem_copy
.ph_next:
	add	x22,x22,56
	sub	w23,w23,1
	b	.ph
.ph_done:
	ldr	x0,[x19,KT_RX_SIZE]		; du code, et les variables apres lui
	cbz	x0,.bad
	ldr	x1,[x19,KT_RW_SIZE]
	cbz	x1,.entry
	ldr	x1,[x19,KT_RW_VA]
	cmp	x1,x0
	b.lo	.bad
.entry:	ldr	x1,[x20,24]			; e_entry : la table, dans le code
	add	x2,x1,8
	cmp	x2,x0
	b.hi	.bad
	str	x1,[x19,KT_ENTRY]
	ldr	x2,[x19,KT_RX]
	ldr	w3,[x2,x1]
	movl	w4,KIT_MAGIC
	cmp	w3,w4
	b.ne	.bad
	add	x1,x1,4
	ldr	w3,[x2,x1]
	str	x3,[x19,KT_VERSION]
	mov	x0,1
	b	.out
.bad:	mov	x0,0				; (pages deja prises : perdues, cas rare)
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = PCB, x1 = indice du kit : code partage + variables privees
; -> x0 = 1 si ok
kit_map:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,kit_table
	mov	x2,KT_SIZE
	madd	x20,x1,x2,x0
	movl	x21,KIT_VA_BASE			; place du kit
	mov	x2,KIT_SLOT
	madd	x21,x1,x2,x21
	mov	x0,x19				; code : pages communes, jamais liberees ici
	mov	x1,x21
	ldr	x2,[x20,KT_RX]
	ldr	x3,[x20,KT_RX_SIZE]
	movl	x4,VM_RX or PTE_SHARED
	bl	vm_map_phys
	cbz	x0,.out
	ldr	x22,[x20,KT_RW_SIZE]		; variables : copie privee
	cbz	x22,.ok
	mov	x0,x19
	ldr	x1,[x20,KT_RW_VA]
	add	x1,x21,x1
	mov	x2,x22
	movl	x3,VM_RW
	bl	vm_alloc_range
	cbz	x0,.out
	mov	x0,x19
	ldr	x1,[x20,KT_RW]
	mov	x2,x22
	ldr	x3,[x20,KT_RW_VA]
	add	x3,x21,x3
	bl	vm_copy_to_user
.ok:	dsb	ish
	isb
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = chaines -> x0 = 1 si egales
str_equal:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.no
	cbnz	w2,.c
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

kit_ext		db '.so',0
kit_msg		db 'Kit              : ',0
kit_msg2	db ' version ',0
kit_msg3	db ', ',0
kit_msg4	db ' Ko de code partag',0xC3,0xA9,'s',10,0
kit_msg_bad	db 'Kit              : format refus',0xC3,0xA9,' : ',0
		align 8
kits_mutex	dw 0
		align 8
kit_table	rb KIT_MAX * KT_SIZE
		align 8
