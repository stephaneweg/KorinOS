; crash : écrit à l'adresse 0 pour vérifier qu'une faute n'arrête que son
; process, pas le noyau.

format ELF64 executable 3 at 0x400000
entry start

include '../../../../inc/syscalls.inc'
include '../../../../inc/x86/sys.inc'

segment readable executable

start:
        lea     rdi, [msg]
        mov     esi, msg_len
        sys     SYS_PRINT
        mov     qword [0], 1
        xor     edi, edi
        sys     SYS_EXIT

segment readable

msg     db 'crash : écriture à l''adresse 0', 10
msg_len = $ - msg
