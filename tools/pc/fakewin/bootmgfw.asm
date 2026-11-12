; Faux \EFI\Microsoft\Boot\bootmgfw.efi, pour essayer le menu de démarrage de Korin
; sous QEMU seulement (tools/pc/menu_esp.sh le met dans build/pc/esp-menu ; jamais
; dans build/pc/esp, ni sur une carte, ni sur la partition EFI d'un vrai PC).
;
; Il vérifie ce dont le vrai gestionnaire de Windows a besoin pour trouver son BCD :
;   LoadedImage->DeviceHandle : un volume (SimpleFileSystem) où \EFI\Microsoft\Boot\BCD s'ouvre ;
;   LoadedImage->FilePath : un nœud de fichier « \EFI\Microsoft\Boot\bootmgfw.efi », puis la fin.
; Il montre les deux chemins (DevicePathToText), écrit le verdict sur l'écran et sur le
; port 0xE9 (« fakewin: … »), attend une touche puis rend la main avec EFI_ABORTED :
; le chargeur de Korin doit alors revenir à son menu.

format pe64 efi
entry start

ST_CONIN        = 48
ST_CONOUT       = 64
ST_BOOTSVC      = 96
BS_WAIT_EVENT   = 96
BS_HANDLE_PROTO = 152
BS_LOCATE_PROTO = 320
CO_OUTPUT       = 8
CI_READKEY      = 8
CI_WAITKEY      = 16
FP_OPEN         = 8
FP_CLOSE        = 16
SFS_OPEN_VOLUME = 8
LI_DEVICE       = 24
LI_FILEPATH     = 32
DPT_TO_TEXT     = 8             ; EFI_DEVICE_PATH_TO_TEXT_PROTOCOL.ConvertDevicePathToText

section '.text' code executable readable

start:
        push    rbp
        mov     rbp, rsp
        and     rsp, -16
        sub     rsp, 48
        mov     [image_handle], rcx
        mov     [systab], rdx
        mov     rax, [rdx + ST_BOOTSVC]
        mov     [bootsvc], rax

        lea     rdx, [m_title]
        call    print
        lea     rsi, [d_start]
        call    dbg

        mov     rcx, [image_handle]
        lea     rdx, [loaded_image_guid]
        lea     r8, [li]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     .no_li

        ; chemin texte (DevicePathToText), s'il y en a un
        lea     rcx, [dp2text_guid]
        xor     edx, edx
        lea     r8, [dp2text]
        mov     rax, [bootsvc]
        call    qword [rax + BS_LOCATE_PROTO]

        ; ---- DeviceHandle
        lea     rdx, [m_dev]
        call    print
        mov     rax, [li]
        mov     rcx, [rax + LI_DEVICE]
        test    rcx, rcx
        jz      .dev_bad
        lea     rdx, [devpath_guid]
        lea     r8, [dev_dp]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     .dev_nodp
        mov     rcx, [dev_dp]
        call    print_dp
.dev_nodp:
        lea     rdx, [m_crlf]
        call    print
        ; un volume, et le BCD dessus
        mov     rax, [li]
        mov     rcx, [rax + LI_DEVICE]
        lea     rdx, [sfs_guid]
        lea     r8, [sfs]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     .dev_bad
        mov     rcx, [sfs]
        lea     rdx, [root]
        call    qword [rcx + SFS_OPEN_VOLUME]
        test    rax, rax
        jnz     .dev_bad
        mov     rcx, [root]
        lea     rdx, [bcd_file]
        lea     r8, [bcd_path]
        mov     r9, 1
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        test    rax, rax
        jnz     .bcd_bad
        mov     rcx, [bcd_file]
        call    qword [rcx + FP_CLOSE]
        mov     byte [ok_dev], 1
        lea     rdx, [m_bcd_ok]
        call    print
        lea     rsi, [d_dev_ok]
        call    dbg
        jmp     .filepath
.bcd_bad:
        lea     rdx, [m_bcd_bad]
        call    print
        lea     rsi, [d_bcd_bad]
        call    dbg
        jmp     .filepath
.dev_bad:
        lea     rdx, [m_dev_bad]
        call    print
        lea     rsi, [d_dev_bad]
        call    dbg

        ; ---- FilePath : un nœud de fichier avec le bon chemin, puis la fin
.filepath:
        lea     rdx, [m_fp]
        call    print
        mov     rax, [li]
        mov     rbx, [rax + LI_FILEPATH]
        test    rbx, rbx
        jz      .fp_bad
        mov     rcx, rbx
        call    print_dp
        lea     rdx, [m_crlf]
        call    print
        cmp     byte [rbx], 4           ; MEDIA_DEVICE_PATH
        jne     .fp_bad
        cmp     byte [rbx + 1], 4       ; MEDIA_FILEPATH_DP
        jne     .fp_bad
        movzx   ecx, word [rbx + 2]
        cmp     ecx, 4 + WIN_PATH_BYTES
        jne     .fp_bad
        lea     rsi, [rbx + 4]
        lea     rdi, [win_path]
        mov     ecx, WIN_PATH_BYTES
        repe    cmpsb
        jne     .fp_bad
        movzx   ecx, word [rbx + 2]
        cmp     byte [rbx + rcx], 0x7F  ; nœud de fin
        jne     .fp_bad
        cmp     byte [rbx + rcx + 1], 0xFF
        jne     .fp_bad
        mov     byte [ok_fp], 1
        lea     rdx, [m_fp_ok]
        call    print
        lea     rsi, [d_fp_ok]
        call    dbg
        jmp     .verdict
.fp_bad:
        lea     rdx, [m_fp_bad]
        call    print
        lea     rsi, [d_fp_bad]
        call    dbg

.verdict:
        lea     rdx, [m_verdict_bad]
        lea     rsi, [d_verdict_bad]
        cmp     byte [ok_dev], 0
        je      @f
        cmp     byte [ok_fp], 0
        je      @f
        lea     rdx, [m_verdict_ok]
        lea     rsi, [d_verdict_ok]
@@:     push    rsi
        sub     rsp, 8
        call    print
        add     rsp, 8
        pop     rsi
        call    dbg
        jmp     .wait
.no_li:
        lea     rdx, [m_no_li]
        call    print

.wait:  ; une touche, puis retour au chargeur
        lea     rdx, [m_key]
        call    print
        mov     rax, [systab]
        mov     rax, [rax + ST_CONIN]
        add     rax, CI_WAITKEY
        mov     ecx, 1
        mov     rdx, rax
        lea     r8, [ev_index]
        mov     rax, [bootsvc]
        call    qword [rax + BS_WAIT_EVENT]
        mov     rcx, [systab]
        mov     rcx, [rcx + ST_CONIN]
        lea     rdx, [key]
        call    qword [rcx + CI_READKEY]
        lea     rsi, [d_back]
        call    dbg
        mov     rax, 0x8000000000000015 ; EFI_ABORTED
        mov     rsp, rbp
        pop     rbp
        ret

; print_dp : rcx = chemin de périphérique, affiché en texte (ou « ? »)
print_dp:
        sub     rsp, 40
        mov     rax, [dp2text]
        test    rax, rax
        jz      .none
        xor     edx, edx                ; DisplayOnly = FALSE
        xor     r8d, r8d                ; AllowShortcuts = FALSE
        call    qword [rax + DPT_TO_TEXT]
        test    rax, rax
        jz      .none
        mov     rdx, rax
        call    print
        jmp     .out
.none:  lea     rdx, [m_q]
        call    print
.out:   add     rsp, 40
        ret

; print : rdx = chaîne UTF-16
print:
        sub     rsp, 40
        mov     rcx, [systab]
        mov     rcx, [rcx + ST_CONOUT]
        call    qword [rcx + CO_OUTPUT]
        add     rsp, 40
        ret

; dbg : rsi = chaîne ASCII, sur le port 0xE9 puis une fin de ligne
dbg:
        push    rsi
@@:     lodsb
        test    al, al
        jz      @f
        out     0xE9, al
        jmp     @b
@@:     mov     al, 10
        out     0xE9, al
        pop     rsi
        ret

section '.data' data readable writeable

loaded_image_guid dd 0x5B1B31A1
                  dw 0x9562, 0x11D2
                  db 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
sfs_guid          dd 0x964E5B22
                  dw 0x6459, 0x11D2
                  db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
devpath_guid      dd 0x09576E91
                  dw 0x6D3F, 0x11D2
                  db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
dp2text_guid      dd 0x8B843E20
                  dw 0x8132, 0x4852
                  db 0x90, 0xCC, 0x55, 0x1A, 0x4E, 0x4A, 0x7F, 0x1C

win_path      du '\EFI\Microsoft\Boot\bootmgfw.efi', 0
WIN_PATH_BYTES = $ - win_path
bcd_path      du '\EFI\Microsoft\Boot\BCD', 0
m_title       du 13, 10, '*** FAUX WINDOWS (essai du menu de Korin, QEMU) ***', 13, 10, 13, 10, 0
m_dev         du 'DeviceHandle : ', 0
m_fp          du 'FilePath     : ', 0
m_crlf        du 13, 10, 0
m_q           du '?', 0
m_bcd_ok      du '  volume OK, \EFI\Microsoft\Boot\BCD trouve', 13, 10, 0
m_bcd_bad     du '  volume OK, mais pas de \EFI\Microsoft\Boot\BCD', 13, 10, 0
m_dev_bad     du '  FAUX : pas de DeviceHandle ou pas de volume', 13, 10, 0
m_fp_ok       du '  OK : noeud de fichier \EFI\Microsoft\Boot\bootmgfw.efi, puis la fin', 13, 10, 0
m_fp_bad      du '  FAUX : FilePath n''est pas le noeud de fichier attendu', 13, 10, 0
m_no_li       du 'FAUX : pas de LoadedImage', 13, 10, 0
m_verdict_ok  du 13, 10, 'Verdict : OK (le vrai bootmgfw trouverait son BCD)', 13, 10, 0
m_verdict_bad du 13, 10, 'Verdict : ECHEC', 13, 10, 0
m_key         du 13, 10, 'Une touche : retour au menu de Korin (EFI_ABORTED)', 13, 10, 0

d_start       db 'fakewin: lance', 0
d_dev_ok      db 'fakewin: DeviceHandle ok, BCD ok', 0
d_bcd_bad     db 'fakewin: DeviceHandle ok, BCD absent', 0
d_dev_bad     db 'fakewin: DeviceHandle FAUX', 0
d_fp_ok       db 'fakewin: FilePath ok', 0
d_fp_bad      db 'fakewin: FilePath FAUX', 0
d_verdict_ok  db 'fakewin: verdict OK', 0
d_verdict_bad db 'fakewin: verdict ECHEC', 0
d_back        db 'fakewin: retour au chargeur', 0

align 8
image_handle  dq 0
systab        dq 0
bootsvc       dq 0
li            dq 0
dp2text       dq 0
dev_dp        dq 0
sfs           dq 0
root          dq 0
bcd_file      dq 0
ev_index      dq 0
key           dd 0
ok_dev        db 0
ok_fp         db 0
align 8
reloc_anchor  dq start

section '.reloc' fixups data readable discardable
