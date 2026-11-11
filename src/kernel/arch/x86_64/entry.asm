; Point d'entrée du noyau x86-64 (FASM, objet ELF64).
; BOOTX64.EFI saute ici en mode long, interruptions coupées, rdi = adresse
; physique du bloc de démarrage (encore accessible par l'identité de l'UEFI).

format ELF64

extrn kmain
extrn boot_stack

BOOT_STACK_SIZE = 65536                 ; boot_stack dans pc_main.c

section '.text.entry' executable

public _start
_start:
        cli
        cld
        lea     rsp, [boot_stack + BOOT_STACK_SIZE]
        xor     ebp, ebp
        call    kmain
.hang:
        cli
        hlt
        jmp     .hang

section '.text' executable

; gdt_load(gdtr, code, data) : charge la GDT et recharge les segments
public gdt_load
gdt_load:
        lgdt    [rdi]
        mov     ax, dx
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        xor     eax, eax
        mov     fs, ax
        mov     gs, ax
        pop     rax                     ; adresse de retour
        push    rsi                     ; sélecteur de code
        push    rax
        retfq
