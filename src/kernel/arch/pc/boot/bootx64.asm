; BOOTX64.EFI : chargeur UEFI de Korin OS pour PC x86-64 (FASM).
;
; Sur le volume FAT d'où il a démarré (carte SD, clé USB, partition KORIN OS du PC) :
;   \EFI\BOOT\BOOTX64.EFI   ce programme (\EFI\Korin\korin.efi sur la partition KORIN OS)
;   \korin\kernel.elf       le noyau (ELF64 x86-64, lié à 0xFFFFFFFF80000000)
;   \korin\initrd.tar       facultatif : archive ustar des premiers programmes
;
; Étapes : menu de démarrage (menu.inc : Korin, Windows, réglages du firmware,
; redémarrer, éteindre ; réglages dans \korin\boot.cfg), lecture et chargement
; du noyau ELF dans des pages neuves, framebuffer GOP, RSDP (ACPI), tables de pages (celles de l'UEFI pour la moitié basse,
; plus le noyau en haut), carte mémoire, ExitBootServices, saut au noyau avec
; rdi = bloc de démarrage (bootinfo.inc).
;
; Convention d'appel de l'UEFI (Microsoft x64) : rcx, rdx, r8, r9, puis la
; pile après 32 octets réservés ; pile alignée sur 16 octets à l'appel.

format pe64 efi
entry start

include 'bootinfo.inc'

; EFI_SYSTEM_TABLE
ST_CONIN        = 48
ST_CONOUT       = 64
ST_RUNTIME      = 88
ST_BOOTSVC      = 96
ST_NTABLES      = 104
ST_TABLES       = 112
; EFI_BOOT_SERVICES
BS_ALLOC_PAGES  = 40
BS_GET_MMAP     = 56
BS_ALLOC_POOL   = 64
BS_FREE_POOL    = 72
BS_HANDLE_PROTO = 152
BS_LOAD_IMAGE   = 200
BS_START_IMAGE  = 208
BS_UNLOAD_IMAGE = 224
BS_EXIT_BS      = 232
BS_STALL        = 248
BS_WATCHDOG     = 256
BS_LOCATE_HBUF  = 312
BS_LOCATE_PROTO = 320
; EFI_RUNTIME_SERVICES
RT_GET_VAR      = 72
RT_SET_VAR      = 88
RT_RESET        = 104
; EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
CO_OUTPUT       = 8
CO_TEST         = 16
CO_QUERY        = 24
CO_ATTR         = 40
CO_CLEAR        = 48
CO_CURSOR       = 56
CO_ENABLE_CUR   = 64
CO_MODE         = 72
; EFI_SIMPLE_TEXT_INPUT_PROTOCOL
CI_RESET        = 0
CI_READKEY      = 8
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
        mov     rax, [rdx + ST_RUNTIME]
        mov     [runtime], rax
        mov     rax, [rdx + ST_CONOUT]
        mov     [conout], rax
        mov     rax, [rdx + ST_CONIN]
        mov     [conin], rax

        ; chien de garde de 5 minutes du micrologiciel : coupé (le menu peut attendre)
        call    watchdog_off

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

        ; ---- menu : ne revient que lorsque le noyau et l'initrd sont chargés
        call    boot_menu
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
        mov     [bootinfo + BB_FB_BASE], rax
        mov     rax, [rsi + 32]
        mov     [bootinfo + BB_FB_SIZE], rax
        mov     rdi, [rsi + 8]          ; Info
        mov     eax, [rdi + 4]
        mov     [bootinfo + BB_FB_WIDTH], eax
        mov     eax, [rdi + 8]
        mov     [bootinfo + BB_FB_HEIGHT], eax
        mov     eax, [rdi + 12]
        cmp     eax, 1
        ja      fail_gop                ; masque de bits ou Blt seul : pas pris en charge
        mov     [bootinfo + BB_FB_FORMAT], eax
        mov     eax, [rdi + 32]
        mov     [bootinfo + BB_FB_PITCH], eax

        ; ---- RSDP : table ACPI 2.0, sinon 1.0
        lea     rsi, [acpi20_guid]
        call    find_config_table
        test    rax, rax
        jnz     .rsdp_ok
        lea     rsi, [acpi10_guid]
        call    find_config_table
.rsdp_ok:
        mov     [bootinfo + BB_RSDP], rax

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
        mov     [bootinfo + BB_MMAP], rax
        mov     rax, [mmap_size]
        mov     [bootinfo + BB_MMAP_SIZE], rax
        mov     rax, [desc_size]
        mov     [bootinfo + BB_MMAP_DESC], rax
        mov     rax, BB_MAGIC_VALUE
        mov     [bootinfo + BB_MAGIC], rax
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
        mov     [bootinfo + BB_KERNEL_PHYS], rax
        mov     rax, [total_pages]
        shl     rax, 12
        mov     [bootinfo + BB_KERNEL_SIZE], rax
        mov     rax, [kernel_virt]
        mov     [bootinfo + BB_KERNEL_VIRT], rax
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
; read_file : rdx = chemin UTF-16 sur le volume de démarrage. Lit tout le
; fichier dans des pages neuves (EfiLoaderData, alignées sur 4 Ko).
; rax = 0 si tout va bien ; rf_buf, rf_size.
read_file:
        push    rbx
        sub     rsp, 48                 ; 32 réservés + 5e argument (aligné)
        mov     rcx, [root_dir]
        mov     r8, rdx
        lea     rdx, [rf_file]
        mov     r9, 1                   ; EFI_FILE_MODE_READ
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        test    rax, rax
        jnz     .out
        mov     qword [info_size], 512
        mov     rcx, [rf_file]
        lea     rdx, [file_info_guid]
        lea     r8, [info_size]
        lea     r9, [file_info]
        call    qword [rcx + FP_GET_INFO]
        test    rax, rax
        jnz     .close
        mov     rax, [file_info + 8]    ; FileSize
        mov     [rf_size], rax
        xor     ecx, ecx                ; AllocateAnyPages
        mov     edx, EFI_LOADER_DATA
        lea     r8, [rax + 4095]
        shr     r8, 12
        inc     r8                      ; au moins une page
        lea     r9, [rf_buf]
        mov     rax, [bootsvc]
        call    qword [rax + BS_ALLOC_PAGES]
        test    rax, rax
        jnz     .close
        mov     rax, [rf_size]
        mov     [rf_read], rax
        mov     rcx, [rf_file]
        lea     rdx, [rf_read]
        mov     r8, [rf_buf]
        call    qword [rcx + FP_READ]
.close:
        mov     rbx, rax
        mov     rcx, [rf_file]
        call    qword [rcx + FP_CLOSE]
        mov     rax, rbx
.out:
        add     rsp, 48
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

include 'menu.inc'

; ---------------------------------------------------------------------------
; print : rdx = chaîne UTF-16 terminée par 0 (pile alignée à l'appel de print).
; Si la console n'a pas les lettres accentuées (TestString, menu.inc), elles
; deviennent leur lettre de base.
print:
        cmp     byte [accents_ok], 0
        jne     .direct
        push    rsi
        push    rdi
        mov     rsi, rdx
        lea     rdi, [ascii_buf]
        mov     ecx, ASCII_MAX
.t:     movzx   eax, word [rsi]
        test    eax, eax
        jz      .td
        cmp     eax, 0x7F
        jbe     .st
        lea     r8, [xlat_tab]
.x:     movzx   r9d, word [r8]
        test    r9d, r9d
        jz      .unk
        cmp     r9d, eax
        je      .hit
        add     r8, 4
        jmp     .x
.hit:   movzx   eax, word [r8 + 2]
        jmp     .st
.unk:   mov     eax, '?'
.st:    mov     [rdi], ax
        add     rsi, 2
        add     rdi, 2
        dec     ecx
        jnz     .t
.td:    mov     word [rdi], 0
        pop     rdi
        pop     rsi
        lea     rdx, [ascii_buf]
.direct:
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

devpath_guid      dd 0x09576E91
                  dw 0x6D3F, 0x11D2
                  db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
global_guid       dd 0x8BE4DF61             ; EFI_GLOBAL_VARIABLE
                  dw 0x93CA, 0x11D2
                  db 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C
partinfo_guid     dd 0x8CF2F62C             ; EFI_PARTITION_INFO_PROTOCOL
                  dw 0xBC9B, 0x4821
                  db 0x80, 0x8D, 0xEC, 0x9E, 0xC4, 0x21, 0xA1, 0xA0

kernel_path du '\korin\kernel.elf', 0
initrd_path du '\korin\initrd.tar', 0
cfg_path    du '\korin\boot.cfg', 0
win_path    du '\EFI\Microsoft\Boot\bootmgfw.efi', 0
WIN_PATH_BYTES = $ - win_path
v_os_ind     du 'OsIndications', 0
v_os_ind_sup du 'OsIndicationsSupported', 0
msg_hello   du 'Korin OS : chargeur UEFI', 13, 10, 0
msg_exit    du 'Korin OS : noyau charge, sortie de l''UEFI', 13, 10, 0
msg_gop     du 'Korin OS : pas de framebuffer GOP utilisable', 13, 10, 0
msg_file    du 'Korin OS : volume de demarrage illisible', 13, 10, 0
msg_mem     du 'Korin OS : erreur de memoire de l''UEFI', 13, 10, 0

; menu (menu.inc) : les lettres accentuées en codes UTF-16 (du ne convertit pas l'UTF-8)
s_title     du 'Korin OS : menu de d', 0xE9, 'marrage', 0
lbl_korin   du 'Korin OS', 0
lbl_windows du 'Windows', 0
lbl_fw      du 'R', 0xE9, 'glages du firmware (BIOS)', 0
lbl_reboot  du 'Red', 0xE9, 'marrer', 0
lbl_off     du 0xC9, 'teindre', 0
s_2sp       du '  ', 0
s_count1    du 'D', 0xE9, 'marrage de ', 0
s_count2    du ' dans ', 0
s_count3    du ' s (une touche arr', 0xEA, 'te le compte ', 0xE0, ' rebours)', 0
s_hint      du 'Fl', 0xE8, 'ches haut et bas puis Entr', 0xE9, 'e, ou le chiffre d', 0x27, 'une ligne', 0
s_code      du ' (code ', 0
s_efi       du ' EFI_', 0
n_load      du 'LOAD_ERROR', 0
n_inval     du 'INVALID_PARAMETER', 0
n_unsup     du 'UNSUPPORTED', 0
n_res       du 'OUT_OF_RESOURCES', 0
n_notfound  du 'NOT_FOUND', 0
n_denied    du 'ACCESS_DENIED', 0
n_aborted   du 'ABORTED', 0
n_security  du 'SECURITY_VIOLATION', 0
msg_loading du 'Korin OS : chargement de \korin\kernel.elf', 13, 10, 0
msg_win     du 'Lancement de \EFI\Microsoft\Boot\bootmgfw.efi', 13, 10, 0
msg_w_load  du 'Windows : LoadImage a ', 0xE9, 'chou', 0xE9, 0
msg_w_back  du 'Windows a rendu la main', 0
msg_w_dp    du 'Windows : chemin du volume introuvable (DevicePath)', 0
msg_fw      du 'R', 0xE9, 'glages du firmware : SetVariable a ', 0xE9, 'chou', 0xE9, 0
msg_reset   du 'ResetSystem a ', 0xE9, 'chou', 0xE9, 0
m_nokernel  du '\korin\kernel.elf introuvable sur ce volume', 0
m_badelf    du '\korin\kernel.elf invalide ou trop gros', 0
accent_test du 0xE9, 0xE8, 0xEA, 0xE0, 0xC9, 0
azerty_row  du '&', 0xE9, '"', 0x27, '(', '-', 0xE8, '_', 0xE7, 0
xlat_tab    dw 0xE9, 'e', 0xE8, 'e', 0xEA, 'e', 0xE0, 'a', 0xC9, 'E', 0xE7, 'c', 0, 0

k_default   db 'default', 0
k_timeout   db 'timeout', 0
k_cmdline   db 'cmdline', 0
k_korin     db 'korin', 0
k_windows   db 'windows', 0
d_menu      db 'boot: menu', 0
d_def_korin db 'boot: defaut korin', 0
d_def_windows db 'boot: defaut windows', 0
d_timeout   db 'boot: delai ', 0
d_fwui      db 'boot: reglages du firmware possibles', 0
d_cfg       db 'boot: \korin\boot.cfg lu', 0
d_expire    db 'boot: fin du compte a rebours', 0
d_stop      db 'boot: compte a rebours arrete', 0
d_korin     db 'boot: lance korin', 0
d_windows   db 'boot: lance windows', 0
d_fw        db 'boot: lance reglages du firmware', 0
d_reboot    db 'boot: lance redemarrer', 0
d_off       db 'boot: lance eteindre', 0
d_win_start db 'boot: windows charge, StartImage', 0
d_win_back  db 'boot: windows a rendu la main', 0
d_win_fail  db 'boot: windows, LoadImage refuse', 0
d_fw_reset  db 'boot: OsIndications pose, ResetSystem', 0
d_win_vol   db 'boot: bootmgfw.efi sur le volume ', 0
d_win_esp   db 'boot: ce volume est une partition systeme EFI : choisi', 0
d_win_none  db 'boot: pas de bootmgfw.efi, pas d''entree windows', 0

align 8
image_handle    dq 0
systab          dq 0
bootsvc         dq 0
runtime         dq 0
conout          dq 0
conin           dq 0
gop             dq 0
loaded_image    dq 0
sfs             dq 0
root_dir        dq 0
rf_file         dq 0
rf_buf          dq 0
rf_size         dq 0
rf_read         dq 0
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
labels          dq lbl_korin, lbl_windows, lbl_fw, lbl_reboot, lbl_off ; par ID_*
dbg_launch      dq d_korin, d_windows, d_fw, d_reboot, d_off
efi_names       dq 1, n_load, 2, n_inval, 3, n_unsup, 9, n_res, 14, n_notfound, 15, n_denied
                dq 21, n_aborted, 26, n_security, 0, 0 ; codes d'erreur EFI (sans le bit 63)
cfg_timeout     dq -1                   ; -1 : pas dans boot.cfg
timeout         dq 0
menu_count      dq 0
sel             dq 0
secs            dq 0
cols            dq 0
rows            dq 0
left            dq 0
top             dq 0
bpos            dq 0
msg_ptr         dq 0
msg_code        dq 0
vol_dp          dq 0
win_handle      dq 0
win_device      dq 0                    ; volume où est bootmgfw.efi (find_windows)
fw_handles      dq 0
fw_count        dq 0
fw_sfs          dq 0
fw_root         dq 0
fw_pinfo        dq 0
fe_file         dq 0
var_size        dq 0
var_val         dq 0
var_attr        dd 0
key             dd 0                    ; EFI_INPUT_KEY : ScanCode, UnicodeChar
cfg_default     db 0                    ; 0 : absent, 1 : korin, 2 : windows
has_windows     db 0
has_fwui        db 0
accents_ok      db 0
def_id          db 0
counting        db 0
msg_has_code    db 0
menu_ids        db 8 dup 0
dec_tmp         db 24 dup 0

align 16
bootinfo:       rb BB_SIZE
align 8
file_info:      rb 512
line_buf:       rw LINE_MAX + 1
ascii_buf:      rw ASCII_MAX + 1
align 8
chain_dp:       rb CHAIN_DP_SIZE

section '.reloc' fixups data readable discardable
