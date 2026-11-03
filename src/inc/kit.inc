; ============================================================================
;  kit.inc - kits (bibliotheques partagees) : construction et utilisation
;  (docs/KITS.md)
;
;  La liste des fonctions d'un kit est ecrite une fois, dans kits/<kit>/<kit>.abi :
;	kit_begin uikit, 'uikit'
;	kit_fn ui_window
;	...
;	kit_end
;
;  Dans un kit (kit_header) : la .abi produit la table d'exportation.
;  Dans un programme :
;	include '../../inc/kit.inc'			; apres app_header
;	segment readable executable
;	kit_use '../../kits/uikit/uikit.abi'		; un relais par fonction
;	start:	bl	kits_open		; ouvre les kits declares
;	...
;	segment readable writeable
;	kit_data				; tables d'import des kits
; ============================================================================

KIT_MAGIC	= 0x54494B41		; « AKIT »
KIT_LOAD_RANK	= 31			; f_kit_load : rang dans la table d'AppKit
KIT_LOAD_VERSION = 32			; (appkit.abi le verifie)

KIT_BUILD	= 0			; 1 dans un kit (kit_header)

; En-tete d'un kit : ELF64 lie a 0, entree = table d'exportation
macro kit_header {
	format	ELF64 executable 3 at 0
	processor cpu64_v8 +cpu64_fp +cpu64_simd
	code64
	entry	kit_table
	KIT_BUILD = 1
}

; --- liste des fonctions (.abi) ------------------------------------------------
macro kit_begin kit*, kname* {
	KIT_CUR equ kit
	kit#_n = 0
  if KIT_BUILD
	align	4
kit_table:
	dw	KIT_MAGIC, kit#_count
  else
	define	KITS_USED kit
	kit#_name equ kname
  end if
}

macro kit_fn fname* {
	match k, KIT_CUR \{
  if KIT_BUILD
	dw	fname - kit_table
  else
fname:	adr	x16,k\#_imports			; relais : x16/x17 seulement (AAPCS64)
	ldr	x16,[x16,k\#_n * 8]
	br	x16
  end if
	k\#_n = k\#_n + 1
	\}
}

macro kit_end {
	match k, KIT_CUR \{
	k\#_count = k\#_n
	\}
	restore	KIT_CUR
}

; Relais des fonctions d'un kit (dans le segment de code du programme) ; le
; premier kit_use apporte aussi kits_open
; Dans un kit (KIT_BUILD = 1), kit_use importe un autre kit comme un
; programme (relais, kit_data, kits_open) : UIKit importe AppKit.
macro kit_use abi* {
	KIT_BUILD_AVANT = KIT_BUILD
	KIT_BUILD = 0
	include	abi
	KIT_BUILD = KIT_BUILD_AVANT
	match =KITS_RT, KITS_RT \{
	KITS_RT equ 1
	kits_runtime
	\}
}

; --- donnees d'import (dans le segment inscriptible du programme) -------------
; pour chaque kit : nom, nombre de fonctions attendues, table des adresses
macro kit_data {
	align	8
kits_list:					; (decalages depuis kits_list : sans
	irpv k, KITS_USED \{			;  adresse absolue, valable dans un kit)
	dd	k\#_rec - kits_list
	\}
	dd	0
	irpv k, KITS_USED \{
	align	8
k\#_rec:
	dd	k\#_str - kits_list, k\#_count, k\#_imports - kits_list
k\#_imports:
	rd	k\#_count
k\#_str:
	db	k\#_name, 0
	\}
	align	8
}

macro kits_runtime {
; Ouvre les kits declares et remplit leurs tables d'import ; un kit absent
; ou trop ancien : message sur la console et fin du programme (code 127).
; Un kit pas encore en memoire (lib_open : E_NOENT) est lu par AppKit
; (f_kit_load : SD0:/lib/<nom>.so par vfs, inscrit par le noyau ou par
; init2), puis rouvert (docs/FICHIERS.md, etape 5).
; Ne modifie que x0-x8, x16, x17.
kits_open:
	adr	x6,kits_list
	mov	x5,x6				; base des decalages
.kit:	ldr	x7,[x6],8			; enregistrement du kit
	cbz	x7,.done
	add	x7,x7,x5
	ldp	x0,x1,[x7]			; nom, version minimale
	add	x0,x0,x5
	mov	x8,SYS_LIB_OPEN
	svc	0
	tbz	x0,63,.ouvert
	cmn	x0,2				; E_NOENT : a lire par AppKit
	b.ne	.fail
	sub	sp,sp,112			; (f_kit_load suit l'AAPCS)
	stp	x5,x6,[sp]
	stp	x7,x9,[sp,16]
	stp	x10,x11,[sp,32]
	stp	x12,x13,[sp,48]
	stp	x14,x15,[sp,64]
	stp	x18,x30,[sp,80]
	adr	x0,kits_appkit
	mov	x1,KIT_LOAD_VERSION
	mov	x8,SYS_LIB_OPEN
	svc	0
	tbnz	x0,63,.sans
	ldr	w1,[x0,8+KIT_LOAD_RANK*4]	; f_kit_load(nom)
	add	x16,x0,x1
	ldr	x0,[x7]
	add	x0,x0,x5
	blr	x16
.sans:	ldp	x18,x30,[sp,80]
	ldp	x14,x15,[sp,64]
	ldp	x12,x13,[sp,48]
	ldp	x10,x11,[sp,32]
	ldp	x7,x9,[sp,16]
	ldp	x5,x6,[sp]
	add	sp,sp,112
	ldp	x0,x1,[x7]			; une seconde fois
	add	x0,x0,x5
	mov	x8,SYS_LIB_OPEN
	svc	0
	tbnz	x0,63,.fail
.ouvert:
	ldr	x1,[x7,8]			; nombre de fonctions
	ldr	x2,[x7,16]			; table d'import
	add	x2,x2,x5
	add	x3,x0,8				; decalages des fonctions
.fn:	cbz	x1,.kit
	ldr	w4,[x3],4
	add	x4,x0,x4			; adresse = table + decalage
	str	x4,[x2],8
	sub	x1,x1,1
	b	.fn
.done:	ret
.fail:	adr	x0,kits_msg			; « kit absent : <nom> »
	mov	x1,kits_msg_len
	mov	x8,SYS_PRINT
	svc	0
	ldr	x0,[x7]
	add	x0,x0,x5
	mov	x1,0
.len:	ldrb	w2,[x0,x1]
	cbz	w2,.name
	add	x1,x1,1
	b	.len
.name:	mov	x8,SYS_PRINT
	svc	0
	adr	x0,kits_nl
	mov	x1,1
	mov	x8,SYS_PRINT
	svc	0
	mov	x0,127
	mov	x8,SYS_EXIT
	svc	0
kits_msg	db 'kit absent ou trop ancien : SD0:/lib/'
kits_msg_len	= $ - kits_msg
kits_nl		db 10
kits_appkit	db 'appkit',0
		align	4
}
