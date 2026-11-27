; Entrées des 256 vecteurs d'interruption (FASM, objet ELF64).
; Chaque entrée empile un code d'erreur (0 si le processeur n'en donne pas)
; et son numéro, puis isr_common sauve les registres (struct regs de cpu.h)
; et appelle isr_dispatch(regs). Depuis le mode utilisateur, swapgs à
; l'entrée et à la sortie : dans le noyau, GS est toujours celui du cœur.

format ELF64

extrn isr_dispatch
extrn smap_on
extrn user_iretq
extrn user_iret

section '.text' executable

rept 256 n:0
{
isr_#n:
  if n = 8 | n = 10 | n = 11 | n = 12 | n = 13 | n = 14 | n = 17 | n = 21 | n = 29 | n = 30
        push    n
  else
        push    0
        push    n
  end if
  if n = 2 | n = 18
        jmp     isr_paranoid
  else
        jmp     isr_common
  end if
}

; NMI et #MC : elles arrivent n'importe où, aussi entre un swapgs et l'iretq ou le début d'une entrée
; d'appel système, où le CS ne dit pas quel GS est en place. Le MSR GS_BASE le dit : une adresse du
; noyau (bit 63) est celle du cœur. swapgs seulement s'il le faut, et le même GS rendu à la sortie
; (rbx, gardé par l'appel C) ; pas de user_iret : on revient exactement à l'état d'avant.
isr_paranoid:
        push    rax
        push    rcx
        push    rdx
        push    rbx
        push    rbp
        push    rsi
        push    rdi
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
        cld
        test    byte [smap_on], 1       ; SMAP : AC effacé (un programme peut le mettre ; l'entrée ne l'efface pas)
        jz      @f
        db      0x0F, 0x01, 0xCA        ; clac
@@:
        xor     ebx, ebx
        mov     ecx, 0xC0000101         ; GS_BASE
        rdmsr
        test    edx, edx
        js      @f                      ; GS du noyau déjà là
        swapgs
        mov     ebx, 1
@@:
        mov     rdi, rsp
        call    isr_dispatch
        test    ebx, ebx
        jz      @f
        swapgs
@@:
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdi
        pop     rsi
        pop     rbp
        pop     rbx
        pop     rdx
        pop     rcx
        pop     rax
        add     rsp, 16
        iretq

isr_common:
        test    qword [rsp + 24], 3     ; venu du mode utilisateur ? (cs)
        jnz     .swap
        ; faute sur l'iretq vers le mode utilisateur : swapgs déjà fait, GS est celui du programme
        push    rax
        lea     rax, [user_iretq]
        cmp     [rsp + 24], rax         ; rip (après rax, numéro et code)
        pop     rax
        jne     @f
.swap:
        swapgs
@@:
        push    rax
        push    rcx
        push    rdx
        push    rbx
        push    rbp
        push    rsi
        push    rdi
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
        cld
        test    byte [smap_on], 1       ; SMAP : AC effacé (un programme peut le mettre ; l'entrée ne l'efface pas)
        jz      @f
        db      0x0F, 0x01, 0xCA        ; clac
@@:
        mov     rdi, rsp
        call    isr_dispatch
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdi
        pop     rsi
        pop     rbp
        pop     rbx
        pop     rdx
        pop     rcx
        pop     rax
        add     rsp, 16                 ; numéro et code d'erreur
        test    qword [rsp + 8], 3      ; retour vers le mode utilisateur ? (cs)
        jnz     user_iret
        iretq

section '.data' writeable

public isr_table
align 8
isr_table:
rept 256 n:0
{
        dq      isr_#n
}
