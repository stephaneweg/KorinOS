; Trampoline de démarrage des autres cœurs (FASM, binaire plat).
;
; smp.c le copie dans une page libre sous 1 Mo (adresse T, alignée sur 4 Ko),
; remplit les paramètres à T + 8, puis envoie INIT-SIPI-SIPI avec le vecteur
; T >> 12. Le cœur démarre en mode réel à T:0, passe directement en mode
; long (PAE, LME, puis PG et PE ensemble), et saute à ap_entry du noyau.
;
; Tables de pages provisoires (préparées par smp.c) : T + 0x1000 = PML4,
; identité des 2 premiers Mo pour ce code, moitié haute du noyau recopiée.
; Le code est indépendant de T : les deux adresses linéaires dont le mode réel
; a besoin (base de la GDT, cible du saut lointain) sont calculées sur place.

format binary
org 0

use16
        jmp     short code16

        align   8
params:                                 ; T + 8 : struct tramp_params de smp.c
p_pml4          dd 0                    ; tables provisoires (physique, < 4 Go)
p_efer          dd 0                    ; bits d'EFER du cœur 0 à poser aussi (NXE : les tables
                                        ; du noyau ont des entrées NX, réservées sans lui)
p_stack         dq 0                    ; pile du thread inactif (virtuelle)
p_cr3           dq 0                    ; tables du noyau
p_entry         dq 0                    ; ap_entry
p_arg           dq 0                    ; struct cpu *

code16:
        cli
        cld
        mov     ax, cs
        mov     ds, ax
        xor     ebx, ebx
        mov     bx, ax
        shl     ebx, 4                  ; T
        lea     eax, [ebx + gdt]
        mov     [gdtr + 2], eax
        lea     eax, [ebx + long_entry]
        mov     [far_target], eax
        lgdt    [gdtr]

        mov     eax, cr4
        or      eax, 1 shl 5            ; PAE
        mov     cr4, eax
        mov     eax, [p_pml4]
        mov     cr3, eax
        mov     ecx, 0xC0000080         ; EFER
        rdmsr
        or      eax, 1 shl 8            ; LME
        or      eax, [p_efer]           ; NXE (avant toute table avec NX)
        wrmsr
        mov     eax, cr0
        or      eax, 0x80000001         ; PG et PE
        mov     cr0, eax
        db      0x66, 0xEA              ; jmp far dword 0x08:far_target
far_target      dd 0
                dw 0x08

use64
long_entry:
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        xor     eax, eax
        mov     fs, ax
        mov     gs, ax
        mov     rsp, [p_stack]
        mov     rsi, [p_cr3]
        mov     rdi, [p_arg]
        mov     rax, [p_entry]
        jmp     rax

        align   8
gdt:    dq      0
        dq      0x00AF9A000000FFFF      ; 0x08 : code 64 bits
        dq      0x00CF92000000FFFF      ; 0x10 : données
gdtr:   dw      3 * 8 - 1
        dd      0
