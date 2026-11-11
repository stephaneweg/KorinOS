; spin : boucle de calcul sans jamais céder la main, pour vérifier que
; l'ordonnanceur reprend le processeur (préemption). Écrit un point tous les
; 200 millions de tours, cinq fois, puis s'arrête avec le code 7.

format ELF64 executable 3 at 0x400000
entry start

include '../../../../inc/syscalls.inc'
include '../../../../inc/x86/sys.inc'

segment readable executable

start:
        mov     ebx, 5
.outer:
        mov     ecx, 200000000
.inner:
        dec     ecx
        jnz     .inner
        lea     rdi, [dot]
        mov     esi, dot_len
        sys     SYS_PRINT
        dec     ebx
        jnz     .outer
        mov     edi, 7
        sys     SYS_EXIT

segment readable

dot     db 'spin : encore 200 millions de tours', 10
dot_len = $ - dot
