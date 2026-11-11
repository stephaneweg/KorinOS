; testkit.so : petit kit d'essai pour LIB_OPEN (ELF64 lié à 0, code sans
; adresse absolue). Table d'exportation : « AKIT », nombre de fonctions (la
; version), puis les décalages (32 bits) des fonctions depuis la table.

format ELF64 executable 3 at 0
entry table

segment readable executable

table:  dd      0x54494B41, 2
        dd      add2 - table
        dd      bump - table

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
