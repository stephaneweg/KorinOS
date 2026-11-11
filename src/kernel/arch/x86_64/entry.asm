; Point d'entrée du noyau x86-64 (FASM, objet ELF64).
; BOOTX64.EFI saute ici en mode long, interruptions coupées, rdi = adresse
; physique du bloc de démarrage (encore accessible par l'identité de l'UEFI).

format ELF64

extrn kmain
extrn ap_main
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

; ap_entry : les autres cœurs arrivent ici depuis le trampoline
; (smp_trampoline.asm), sur les tables de pages provisoires ; rdi = struct cpu,
; rsi = CR3 du noyau, rsp = pile du thread inactif du cœur.
public ap_entry
ap_entry:
        mov     cr3, rsi
        xor     ebp, ebp
        call    ap_main
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
        mov     ss, ax                  ; FS et GS gardés : charger GS effacerait la base du cœur
        pop     rax                     ; adresse de retour
        push    rsi                     ; sélecteur de code
        push    rax
        retfq
