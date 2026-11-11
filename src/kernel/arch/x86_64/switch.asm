; Bascule de threads, passage en mode utilisateur et entrée des appels
; système (FASM, objet ELF64).
;
; GS : dans le noyau, GS pointe sur la structure du cœur (struct cpu de
; cpu.c : +0 sommet de la pile du noyau du thread courant, +8 rsp de
; l'utilisateur pendant l'entrée) ; en mode utilisateur, swapgs l'échange.

format ELF64

extrn syscall_dispatch
extrn thread_exit

section '.text' executable

; context_switch(uint64_t *save_sp, uint64_t new_sp)
public context_switch
context_switch:
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        mov     [rdi], rsp
        mov     rsp, rsi
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret

; premier passage d'un thread neuf : r12 = fonction, r13 = argument
public thread_trampoline
thread_trampoline:
        sti
        mov     rdi, r13
        call    r12
        xor     edi, edi
        call    thread_exit             ; ne revient pas

; arch_enter_user(entry, stack, arg)
public arch_enter_user
arch_enter_user:
        cli
        push    0x1B                    ; SS utilisateur (0x18 | 3)
        push    rsi                     ; RSP
        push    0x202                   ; RFLAGS : IF
        push    0x23                    ; CS utilisateur (0x20 | 3)
        push    rdi                     ; RIP
        mov     rdi, rdx
        xor     eax, eax
        xor     ebx, ebx
        xor     ecx, ecx
        xor     edx, edx
        xor     esi, esi
        xor     ebp, ebp
        xor     r8, r8
        xor     r9, r9
        xor     r10, r10
        xor     r11, r11
        xor     r12, r12
        xor     r13, r13
        xor     r14, r14
        xor     r15, r15
        swapgs
        iretq

; syscall : rax = numéro, rdi rsi rdx r10 r8 r9 = arguments ;
; rcx = rip et r11 = rflags de l'utilisateur. Retour dans rax.
public syscall_entry
syscall_entry:
        swapgs
        mov     [gs:8], rsp
        mov     rsp, [gs:0]
        push    qword [gs:8]            ; rsp de l'utilisateur
        push    r11
        push    rcx
        push    r9                      ; arguments 5 .. 0 : tableau a[6]
        push    r8
        push    r10
        push    rdx
        push    rsi
        push    rdi
        sti
        mov     rdi, rax                ; numéro
        mov     rsi, rsp                ; a[6]
        sub     rsp, 8                  ; 9 qwords empilés : réaligner sur 16
        call    syscall_dispatch
        add     rsp, 8
        cli
        pop     rdi
        pop     rsi
        pop     rdx
        pop     r10
        pop     r8
        pop     r9
        pop     rcx
        pop     r11
        pop     rsp
        swapgs
        sysretq
