; hello.asm : un programme x86-64 pour Korin sur PC, assemblé dans Korin par fasm :
;     fasm hello.asm hello && ./hello
; ELF64 à adresse fixe (0x400000), comme les commandes du PC. Le noyau entre à start avec la ligne
; d'arguments dans rdi ; AppKit (SYS_LIB_OPEN) donne puts, qui écrit sur la sortie standard.

	format	ELF64 executable 3 at 0x400000
	entry	start

SYS_EXIT	= 0x10			; (src/inc/syscalls.inc)
SYS_LIB_OPEN	= 0x64

segment readable executable

start:
	and	rsp,-16
	lea	rdi,[appkit]
	mov	esi,1			; version minimale de la table
	mov	eax,SYS_LIB_OPEN
	syscall
	test	rax,rax
	js	.fin
	mov	edx,[rax+8]		; puts : première entrée de la table d'AppKit
	add	rax,rdx
	lea	rdi,[message]
	call	rax
.fin:	xor	edi,edi
	mov	eax,SYS_EXIT
	syscall

segment readable

appkit	db 'appkit',0
message	db 'Bonjour depuis fasm, sur Korin PC !',10,0
