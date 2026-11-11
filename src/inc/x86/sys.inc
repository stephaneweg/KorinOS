; Appels système de Korin depuis un programme x86-64 (FASM).
; Les numéros viennent de src/inc/syscalls.inc, à inclure avant.
; Arguments : rdi, rsi, rdx, r10, r8, r9 ; résultat dans rax ; rcx et r11
; sont détruits par l'instruction syscall.

macro sys n*
{
        mov     eax, n
        syscall
}
