; hello : premier process de Korin sur PC. Écrit trois lignes, une par
; seconde, avec son pid, puis s'arrête (code 0).

format ELF64 executable 3 at 0x400000
entry start

include '../../../../inc/syscalls.inc'
include '../../../../inc/x86/sys.inc'

segment readable executable

start:
        sys     SYS_GETPID
        mov     [pid], rax
        mov     qword [round], 1
.loop:
        lea     rdi, [line]
        lea     rsi, [msg]
        call    copy
        mov     rax, [pid]
        call    decimal
        lea     rsi, [msg2]
        call    copy
        mov     rax, [round]
        call    decimal
        mov     byte [rdi], 10
        inc     rdi
        lea     rsi, [line]
        sub     rdi, rsi
        mov     rsi, rdi                ; longueur
        lea     rdi, [line]
        sys     SYS_PRINT
        mov     edi, 1000
        sys     SYS_SLEEP
        inc     qword [round]
        cmp     qword [round], 3
        jbe     .loop
        xor     edi, edi
        sys     SYS_EXIT

; copy : chaîne terminée par 0 de rsi vers rdi (rdi avance)
copy:
        lodsb
        test    al, al
        jz      .done
        stosb
        jmp     copy
.done:
        ret

; decimal : rax en décimal à rdi (rdi avance)
decimal:
        lea     rsi, [digits + 20]
        mov     ecx, 10
.div:
        xor     edx, edx
        div     rcx
        add     dl, '0'
        dec     rsi
        mov     [rsi], dl
        test    rax, rax
        jnz     .div
        jmp     copy                    ; digits + 20 = 0

segment readable writeable

msg     db 'Bonjour depuis le process ', 0
msg2    db ' (mode utilisateur), tour ', 0
pid     dq 0
round   dq 0
digits  rb 20
        db 0
line    rb 128
