; Entrées des 256 vecteurs d'interruption (FASM, objet ELF64).
; Chaque entrée empile un code d'erreur (0 si le processeur n'en donne pas)
; et son numéro, puis isr_common sauve les registres (struct regs de cpu.h)
; et appelle isr_dispatch(regs).

format ELF64

extrn isr_dispatch

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
        jmp     isr_common
}

isr_common:
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
        iretq

section '.data' writeable

public isr_table
align 8
isr_table:
rept 256 n:0
{
        dq      isr_#n
}
