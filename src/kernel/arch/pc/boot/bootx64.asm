; BOOTX64.EFI : chargeur UEFI de Korin OS pour PC x86-64 (FASM).
;
; Sur le volume FAT d'où il a démarré (carte SD, clé USB) :
;   \EFI\BOOT\BOOTX64.EFI   ce programme
;   \korin\kernel.elf       le noyau (ELF64 x86-64, lié à 0xFFFFFFFF80000000)
;
; Étapes : framebuffer GOP, RSDP (ACPI), lecture et chargement du noyau ELF
; dans des pages neuves, tables de pages (celles de l'UEFI pour la moitié basse,
; plus le noyau en haut), carte mémoire, ExitBootServices, saut au noyau avec
; rdi = bloc de démarrage (bootinfo.inc).
;
; Convention d'appel de l'UEFI (Microsoft x64) : rcx, rdx, r8, r9, puis la
; pile après 32 octets réservés ; pile alignée sur 16 octets à l'appel.

format pe64 efi
entry start

include 'bootinfo.inc'

; EFI_SYSTEM_TABLE
ST_CONOUT       = 64
ST_BOOTSVC      = 96
ST_NTABLES      = 104
ST_TABLES       = 112
; EFI_BOOT_SERVICES
BS_ALLOC_PAGES  = 40
BS_GET_MMAP     = 56
BS_ALLOC_POOL   = 64
BS_HANDLE_PROTO = 152
BS_EXIT_BS      = 232
BS_STALL        = 248
BS_WATCHDOG     = 256
BS_LOCATE_PROTO = 320
; EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
CO_OUTPUT       = 8
; EFI_FILE_PROTOCOL
FP_OPEN         = 8
FP_CLOSE        = 16
FP_READ         = 32
FP_GET_INFO     = 64
; EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
SFS_OPEN_VOLUME = 8
; EFI_LOADED_IMAGE_PROTOCOL
LI_DEVICE       = 24

EFI_LOADER_DATA = 2
MMAP_BUF_SIZE   = 65536
ELF_PT_LOAD     = 1

section '.text' code executable readable

start:
        push    rbp
        mov     rbp, rsp
        and     rsp, -16
        sub     rsp, 64                 ; 32 réservés + arguments 5 et 6
        mov     [image_handle], rcx
        mov     [systab], rdx
        mov     rax, [rdx + ST_BOOTSVC]
        mov     [bootsvc], rax

        ; chien de garde de 5 minutes du micrologiciel : coupé
        xor     ecx, ecx
        xor     edx, edx
        xor     r8, r8
        xor     r9, r9
        call    qword [rax + BS_WATCHDOG]

        lea     rdx, [msg_hello]
        call    print

        ; ---- framebuffer GOP
        lea     rcx, [gop_guid]
        xor     edx, edx
        lea     r8, [gop]
        mov     rax, [bootsvc]
        call    qword [rax + BS_LOCATE_PROTO]
        test    rax, rax
        jnz     fail_gop
        mov     rbx, [gop]
        mov     rsi, [rbx + 24]         ; Mode
        mov     rax, [rsi + 24]
        mov     [bootinfo + BI_FB_BASE], rax
        mov     rax, [rsi + 32]
        mov     [bootinfo + BI_FB_SIZE], rax
        mov     rdi, [rsi + 8]          ; Info
        mov     eax, [rdi + 4]
        mov     [bootinfo + BI_FB_WIDTH], eax
        mov     eax, [rdi + 8]
        mov     [bootinfo + BI_FB_HEIGHT], eax
        mov     eax, [rdi + 12]
        cmp     eax, 1
        ja      fail_gop                ; masque de bits ou Blt seul : pas pris en charge
        mov     [bootinfo + BI_FB_FORMAT], eax
        mov     eax, [rdi + 32]
        mov     [bootinfo + BI_FB_PITCH], eax

        ; ---- RSDP : table ACPI 2.0, sinon 1.0
        lea     rsi, [acpi20_guid]
        call    find_config_table
        test    rax, rax
        jnz     .rsdp_ok
        lea     rsi, [acpi10_guid]
        call    find_config_table
.rsdp_ok:
        mov     [bootinfo + BI_RSDP], rax

        ; ---- volume de démarrage
        mov     rcx, [image_handle]
        lea     rdx, [loaded_image_guid]
        lea     r8, [loaded_image]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     fail_file
        mov     rax, [loaded_image]
        mov     rcx, [rax + LI_DEVICE]
        lea     rdx, [sfs_guid]
        lea     r8, [sfs]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     fail_file
        mov     rcx, [sfs]
        lea     rdx, [root_dir]
        call    qword [rcx + SFS_OPEN_VOLUME]
        test    rax, rax
        jnz     fail_file

        ; ---- \korin\kernel.elf
        mov     rcx, [root_dir]
        lea     rdx, [kfile]
        lea     r8, [kernel_path]
        mov     r9, 1                   ; EFI_FILE_MODE_READ
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        test    rax, rax
        jnz     fail_file
        mov     qword [info_size], 512
        mov     rcx, [kfile]
        lea     rdx, [file_info_guid]
        lea     r8, [info_size]
        lea     r9, [file_info]
        call    qword [rcx + FP_GET_INFO]
        test    rax, rax
        jnz     fail_file
        mov     rax, [file_info + 8]    ; FileSize
        mov     [kfile_size], rax
        mov     ecx, EFI_LOADER_DATA
        mov     rdx, rax
        lea     r8, [kfile_buf]
        mov     rax, [bootsvc]
        call    qword [rax + BS_ALLOC_POOL]
        test    rax, rax
        jnz     fail_mem
        mov     rcx, [kfile]
        lea     rdx, [kfile_size]
        mov     r8, [kfile_buf]
        call    qword [rcx + FP_READ]
        test    rax, rax
        jnz     fail_file
        mov     rcx, [kfile]
        call    qword [rcx + FP_CLOSE]

        call    load_elf
        test    rax, rax
        jnz     fail_elf

        ; ---- tampon de la carte mémoire (alloué avant la carte elle-même)
        mov     ecx, EFI_LOADER_DATA
        mov     edx, MMAP_BUF_SIZE
        lea     r8, [mmap_buf]
        mov     rax, [bootsvc]
        call    qword [rax + BS_ALLOC_POOL]
        test    rax, rax
        jnz     fail_mem

        lea     rdx, [msg_exit]
        call    print

        ; ---- carte mémoire puis ExitBootServices (deuxième essai si la carte a changé)
        mov     qword [exit_tries], 3
.get_map:
        mov     qword [mmap_size], MMAP_BUF_SIZE
        lea     rcx, [mmap_size]
        mov     rdx, [mmap_buf]
        lea     r8, [map_key]
        lea     r9, [desc_size]
        lea     rax, [desc_version]
        mov     [rsp + 32], rax
        mov     rax, [bootsvc]
        call    qword [rax + BS_GET_MMAP]
        test    rax, rax
        jnz     fail_mem
        mov     rcx, [image_handle]
        mov     rdx, [map_key]
        mov     rax, [bootsvc]
        call    qword [rax + BS_EXIT_BS]
        test    rax, rax
        jz      .exited
        dec     qword [exit_tries]
        jnz     .get_map
        jmp     fail_mem

.exited:
        ; plus aucun service de démarrage à partir d'ici
        cli
        mov     rax, [mmap_buf]
        mov     [bootinfo + BI_MMAP], rax
        mov     rax, [mmap_size]
        mov     [bootinfo + BI_MMAP_SIZE], rax
        mov     rax, [desc_size]
        mov     [bootinfo + BI_MMAP_DESC], rax
        mov     rax, BI_MAGIC_VALUE
        mov     [bootinfo + BI_MAGIC], rax
        mov     rax, [new_pml4]
        mov     cr3, rax
        lea     rdi, [bootinfo]
        mov     rax, [kernel_entry]
        jmp     rax

; ---------------------------------------------------------------------------
; load_elf : vérifie l'ELF lu dans kfile_buf, réserve le bloc du noyau, copie
; les segments, construit les tables de pages. rax = 0 si tout va bien.
load_elf:
        push    rbx
        push    rsi
        push    rdi
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 48                 ; 32 réservés + alignement (7 push + retour = 64)
        mov     rbx, [kfile_buf]
        cmp     dword [rbx], 0x464C457F ; 7F 'E' 'L' 'F'
        jne     .bad
        cmp     byte [rbx + 4], 2       ; 64 bits
        jne     .bad
        cmp     word [rbx + 18], 0x3E   ; x86-64
        jne     .bad
        mov     rax, [rbx + 24]
        mov     [kernel_entry], rax

        ; étendue virtuelle des segments PT_LOAD
        mov     r12, -1                 ; plus petite adresse
        xor     r13, r13                ; plus grande fin
        mov     rsi, [rbx + 32]         ; e_phoff
        add     rsi, rbx
        movzx   ecx, word [rbx + 56]    ; e_phnum
        movzx   r14d, word [rbx + 54]   ; e_phentsize
.scan:
        test    ecx, ecx
        jz      .scanned
        cmp     dword [rsi], ELF_PT_LOAD
        jne     .scan_next
        mov     rax, [rsi + 16]         ; p_vaddr
        cmp     rax, r12
        jae     @f
        mov     r12, rax
@@:     add     rax, [rsi + 40]         ; + p_memsz
        cmp     rax, r13
        jbe     .scan_next
        mov     r13, rax
.scan_next:
        add     rsi, r14
        dec     ecx
        jmp     .scan
.scanned:
        cmp     r12, -1
        je      .bad
        test    r12, 0x1FFFFF           ; début aligné sur 2 Mo (kernel.ld)
        jnz     .bad
        mov     [kernel_virt], r12
        add     r13, 4095
        and     r13, -4096
        sub     r13, r12                ; taille de l'image
        shr     r13, 12                 ; pages de l'image
        mov     [image_pages], r13
        ; + PML4, PDPT, PD et les PT (une par 512 pages, avec marge)
        lea     rax, [r13 + 3 + 511]
        shr     rax, 9
        inc     rax
        mov     [pt_count], rax
        lea     rax, [r13 + rax + 3]
        mov     [total_pages], rax
        cmp     rax, 512 * 16           ; au plus 32 Mo : un seul PD
        ja      .bad

        xor     ecx, ecx                ; AllocateAnyPages
        mov     edx, EFI_LOADER_DATA
        mov     r8, [total_pages]
        lea     r9, [kernel_phys]
        mov     rax, [bootsvc]
        call    qword [rax + BS_ALLOC_PAGES]
        test    rax, rax
        jnz     .bad

        ; tout à zéro (BSS compris)
        mov     rdi, [kernel_phys]
        mov     rcx, [total_pages]
        shl     rcx, 9
        xor     eax, eax
        rep     stosq

        ; copie des segments
        mov     rsi, [rbx + 32]
        add     rsi, rbx
        movzx   r15d, word [rbx + 56]
.copy:
        test    r15d, r15d
        jz      .copied
        cmp     dword [rsi], ELF_PT_LOAD
        jne     .copy_next
        push    rsi
        mov     rdi, [rsi + 16]
        sub     rdi, r12
        add     rdi, [kernel_phys]
        mov     rcx, [rsi + 32]         ; p_filesz
        mov     rax, [rsi + 8]          ; p_offset
        lea     rsi, [rbx + rax]
        rep     movsb
        pop     rsi
.copy_next:
        add     rsi, r14
        dec     r15d
        jmp     .copy
.copied:

        ; tables : PML4, PDPT, PD, PT juste après l'image
        mov     rax, cr4
        bt      rax, 12                 ; LA57 : pagination à 5 niveaux non prise en charge
        jc      .bad
        mov     rax, [image_pages]
        shl     rax, 12
        add     rax, [kernel_phys]
        mov     [new_pml4], rax
        lea     rdx, [rax + 4096]       ; PDPT
        lea     r8, [rax + 8192]        ; PD
        lea     r9, [rax + 12288]       ; première PT

        ; moitié basse : les entrées de l'UEFI (identité)
        mov     rdi, rax
        mov     rsi, cr3
        and     rsi, -4096
        mov     ecx, 512
        rep     movsq
        ; 0xFFFFFFFF80000000 : PML4[511] -> PDPT[510] -> PD
        mov     rax, [new_pml4]
        lea     rcx, [rdx + 3]
        mov     [rax + 511 * 8], rcx
        lea     rcx, [r8 + 3]
        mov     [rdx + 510 * 8], rcx
        ; PD[k] -> PT k
        mov     rcx, [pt_count]
        xor     r10, r10
.pd_fill:
        mov     rax, r10
        shl     rax, 12
        add     rax, r9
        or      rax, 3
        mov     [r8 + r10 * 8], rax
        inc     r10
        cmp     r10, rcx
        jb      .pd_fill
        ; PT : chaque page du bloc, dans l'ordre
        mov     rcx, [total_pages]
        xor     r10, r10
        mov     r11, [kernel_phys]
.pt_fill:
        mov     rax, r10
        shl     rax, 12
        add     rax, r11
        or      rax, 3                  ; présent, écriture
        mov     [r9 + r10 * 8], rax
        inc     r10
        cmp     r10, rcx
        jb      .pt_fill

        mov     rax, [kernel_phys]
        mov     [bootinfo + BI_KERNEL_PHYS], rax
        mov     rax, [total_pages]
        shl     rax, 12
        mov     [bootinfo + BI_KERNEL_SIZE], rax
        mov     rax, [kernel_virt]
        mov     [bootinfo + BI_KERNEL_VIRT], rax
        xor     eax, eax
        jmp     .out
.bad:
        mov     eax, 1
.out:
        add     rsp, 48
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rdi
        pop     rsi
        pop     rbx
        ret

; ---------------------------------------------------------------------------
; find_config_table : rsi = GUID cherché ; rax = VendorTable ou 0
find_config_table:
        push    rbx
        push    rdi
        mov     rbx, [systab]
        mov     rcx, [rbx + ST_NTABLES]
        mov     rdi, [rbx + ST_TABLES]
.next:
        test    rcx, rcx
        jz      .none
        mov     rax, [rdi]
        cmp     rax, [rsi]
        jne     .skip
        mov     rax, [rdi + 8]
        cmp     rax, [rsi + 8]
        jne     .skip
        mov     rax, [rdi + 16]
        jmp     .done
.skip:
        add     rdi, 24
        dec     rcx
        jmp     .next
.none:
        xor     eax, eax
.done:
        pop     rdi
        pop     rbx
        ret

; ---------------------------------------------------------------------------
; print : rdx = chaîne UTF-16 terminée par 0 (pile alignée à l'appel de print)
print:
        sub     rsp, 40
        mov     rcx, [systab]
        mov     rcx, [rcx + ST_CONOUT]
        call    qword [rcx + CO_OUTPUT]
        add     rsp, 40
        ret

fail_gop:
        lea     rdx, [msg_gop]
        jmp     fail
fail_file:
        lea     rdx, [msg_file]
        jmp     fail
fail_elf:
        lea     rdx, [msg_elf]
        jmp     fail
fail_mem:
        lea     rdx, [msg_mem]
fail:
        call    print
        mov     ecx, 10000000           ; 10 s pour lire le message
        mov     rax, [bootsvc]
        call    qword [rax + BS_STALL]
        mov     rax, 0x8000000000000001 ; EFI_LOAD_ERROR
        mov     rsp, rbp
        pop     rbp
        ret

section '.data' data readable writeable

gop_guid          dd 0x9042A9DE
                  dw 0x23DC, 0x4A38
                  db 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A
loaded_image_guid dd 0x5B1B31A1
                  dw 0x9562, 0x11D2
                  db 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
sfs_guid          dd 0x964E5B22
                  dw 0x6459, 0x11D2
                  db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
file_info_guid    dd 0x09576E92
                  dw 0x6D3F, 0x11D2
                  db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
acpi20_guid       dd 0x8868E871
                  dw 0xE4F1, 0x11D3
                  db 0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81
acpi10_guid       dd 0xEB9D2D30
                  dw 0x2D88, 0x11D3
                  db 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D

kernel_path du '\korin\kernel.elf', 0
msg_hello   du 'Korin OS : chargeur UEFI', 13, 10, 0
msg_exit    du 'Korin OS : noyau charge, sortie de l''UEFI', 13, 10, 0
msg_gop     du 'Korin OS : pas de framebuffer GOP utilisable', 13, 10, 0
msg_file    du 'Korin OS : \korin\kernel.elf introuvable sur le volume', 13, 10, 0
msg_elf     du 'Korin OS : kernel.elf invalide ou trop gros', 13, 10, 0
msg_mem     du 'Korin OS : erreur de memoire de l''UEFI', 13, 10, 0

align 8
image_handle    dq 0
systab          dq 0
bootsvc         dq 0
gop             dq 0
loaded_image    dq 0
sfs             dq 0
root_dir        dq 0
kfile           dq 0
kfile_size      dq 0
kfile_buf       dq 0
info_size       dq 0
kernel_entry    dq 0
kernel_virt     dq 0
kernel_phys     dq 0
image_pages     dq 0
pt_count        dq 0
total_pages     dq 0
new_pml4        dq 0
mmap_buf        dq 0
mmap_size       dq 0
map_key         dq 0
desc_size       dq 0
desc_version    dq 0
exit_tries      dq 0
reloc_anchor    dq start                ; garde une table de relocation non vide

align 16
bootinfo:       rb BI_SIZE
align 8
file_info:      rb 512

section '.reloc' fixups data readable discardable
