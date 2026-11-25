; Bascule de threads, passage en mode utilisateur et entrée des appels
; système (FASM, objet ELF64).
;
; GS : dans le noyau, GS pointe sur la structure du cœur (struct cpu de
; cpu.c : +0 sommet de la pile du noyau du thread courant, +8 rsp de
; l'utilisateur pendant l'entrée) ; en mode utilisateur, swapgs l'échange.

format ELF64

extrn syscall_dispatch
extrn sched_thread_start

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

; premier passage d'un thread neuf : r12 = fonction, r13 = argument.
; sched_thread_start rend le verrou de l'ordonnanceur, permet les
; interruptions, appelle la fonction puis thread_exit.
public thread_trampoline
thread_trampoline:
        mov     rdi, r12
        mov     rsi, r13
        call    sched_thread_start      ; ne revient pas

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
        jmp     user_iret

; syscall : rax = numéro, rdi rsi rdx r10 r8 r9 = arguments ;
; rcx = rip et r11 = rflags de l'utilisateur. Retour dans rax.
;
; L'entrée construit la même trame qu'une interruption (struct regs de
; cpu.h : registres, numéro, code, puis rip cs rflags rsp ss) : les signaux,
; fork et exec lisent et changent tous les registres du programme. Le retour
; passe par iretq (ret_to_user), qui recharge toute la trame.
extrn syscall_entry_c

public syscall_entry
syscall_entry:
        swapgs
        mov     [gs:8], rsp
        mov     rsp, [gs:0]
        push    0x1B                    ; ss
        push    qword [gs:8]            ; rsp
        push    r11                     ; rflags
        push    0x23                    ; cs
        push    rcx                     ; rip
        push    0                       ; code d'erreur
        push    -1                      ; « vecteur » : appel système
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
        sti
        mov     rdi, rsp                ; struct regs * (22 qwords : aligné sur 16)
        call    syscall_entry_c
        jmp     ret_to_user

; ret_to_user : rsp = struct regs d'un retour vers le mode utilisateur
public ret_to_user
ret_to_user:
        cli
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
; le seul retour vers le mode utilisateur (aussi celui d'isr.asm et d'arch_enter_user). Une faute
; sur cet iretq (trame refusée : rip non canonique…) arrive en anneau 0 avec le GS du programme :
; isr_common la reconnaît à son rip, refait swapgs, et exception() arrête le process.
public user_iret
public user_iretq
user_iret:
        swapgs
user_iretq:
        iretq

; arch_copy_user(dst, src, len) -> 0, ou -14 (E_FAULT) : copie entre le noyau et la mémoire d'un
; programme. Une faute de page sur user_copy_insn qu'idt.c ne peut pas amener (page rendue par un
; autre thread après user_check, munmap) reprend à user_copy_fault : l'appel rend E_FAULT au lieu
; d'arrêter le process en gardant un verrou (futex_lock, sig_lock : la machine gelait).
public arch_copy_user
public user_copy_insn
public user_copy_fault
arch_copy_user:
        mov     rcx, rdx
user_copy_insn:
        rep movsb
        xor     eax, eax
        ret
user_copy_fault:
        mov     rax, -14
        ret

; premier passage d'un fils de fork : r12 = struct regs (copiée sur sa pile)
public fork_trampoline
extrn sched_fork_start
fork_trampoline:
        mov     rdi, r12
        call    sched_fork_start        ; rend le verrou de l'ordonnanceur
        mov     rsp, r12
        jmp     ret_to_user
