; testkit.so : petit kit d'essai pour LIB_OPEN (ELF64 lié à 0, code sans
; adresse absolue). Table d'exportation : « AKIT », version 3, puis les
; décalages des fonctions depuis la table.

format ELF64 executable 3 at 0
entry table

segment readable executable

table:  dd      0x54494B41, 3
        dq      add2 - table
        dq      bump - table

; add2(a, b) -> a + b
add2:
        lea     rax, [rdi + rsi]
        ret

; bump() -> compteur + 1 (variable du kit : une copie par process)
bump:
        inc     qword [counter]
        mov     rax, [counter]
        ret

segment readable writeable

counter dq      100
