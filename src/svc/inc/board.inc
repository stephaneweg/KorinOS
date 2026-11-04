; ============================================================================
;  inc/board.inc - quelle carte ? pour les services qui pilotent un materiel
;  du Pi 4 seulement (docs/PI5.md) : sur un Pi 5, ils sortent (code 0 : init2
;  ne les relance pas) au lieu de toucher des adresses du BCM2711.
;
;	include '../inc/board.inc'	(apres kit.inc)
;	segment executable ... board_code
;	segment writeable ...  board_data
;	bl board_pi5			; -> x0 = 1 sur un Pi 5
;  Revision de la carte (firmware, GET_BOARD_REVISION) au nouveau format
;  (bit 23), processeur (bits 12-15) : 3 BCM2711, 4 BCM2712. QEMU raspi4b
;  annonce un Pi 4.
; ============================================================================

BOARD_CPU_BCM2712	= 4

macro board_code {

; -> x0 = 1 sur un Pi 5 (BCM2712), 0 sinon ou inconnu
board_pi5:
	prologue
	adr	x0,board_msg
	mov	w1,28
	str	w1,[x0]
	str	wzr,[x0,4]
	movl	w1,0x00010002			; GET_BOARD_REVISION
	str	w1,[x0,8]
	mov	w1,4
	str	w1,[x0,12]
	str	wzr,[x0,16]
	str	wzr,[x0,20]
	str	wzr,[x0,24]
	sys	SYS_FW_CALL
	tbnz	x0,63,.non
	adr	x0,board_msg
	ldr	w0,[x0,20]
	tbz	w0,23,.non			; ancien format : avant le Pi 4
	ubfx	w0,w0,12,4
	cmp	w0,BOARD_CPU_BCM2712
	cset	x0,eq
	b	.out
.non:	mov	x0,0
.out:	epilogue
}

macro board_data {
		align 16
board_msg	rb 32				; message au firmware (reponse ecrite)
}
