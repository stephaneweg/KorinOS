; L'entrée « Korin OS » du firmware, créée par le chargeur lui-même (inclus par bootx64.asm ; docs/etude/INSTALLATEUR.md
; §6.2) : le noyau n'appelle pas les services d'exécution de l'UEFI, tout se fait ici, avant ExitBootServices.
;
;  * Sur un Korin installé, l'installateur écrit « firmware = install » dans \korin\boot.cfg. Au démarrage,
;    nvram_register crée Boot4B4F (« Korin OS », le chemin de CE chargeur : son volume, puis son fichier), le met en
;    tête de BootOrder (l'entrée Windows reste, derrière), puis récrit la ligne en « firmware = done   ».
;  * La clé d'installation a « target = auto » : avant son menu, target_find cherche un autre volume dont
;    \korin\boot.cfg contient « firmware = install » (un Korin qui vient d'être installé) ; s'il y en a un, la clé
;    lance son \EFI\Korin\korin.efi (comme Windows), qui s'inscrit puis démarre Korin installé. Après, la ligne dit
;    « done » : la clé démarre de nouveau sur elle-même.

FP_WRITE        = 40
FP_SET_POS      = 56
LI_FILEPATH     = 32
TF_BUF_SIZE     = 2048
NV_OPT_SIZE     = 1024
NV_ORDER_MAX    = 256           ; entrées de BootOrder gardées

; ---------------------------------------------------------------------------
; nvram_register : Boot4B4F et BootOrder, puis boot.cfg (« done »). En cas d'échec : une trace, Korin démarre quand
; même (la clé, ou F9, le relancera).
nvram_register:
        penter
        lea     rsi, [d_nv_begin]
        call    dbg
        mov     rsi, [bp_dp]            ; le chemin du volume (find_bootpart)
        test    rsi, rsi
        jz      .fail
        lea     rdi, [nv_opt]
        mov     dword [rdi], 1          ; LOAD_OPTION_ACTIVE
        add     rdi, 6                  ; (FilePathListLength : plus bas)
        push    rsi
        lea     rsi, [nv_desc]
        mov     ecx, NV_DESC_BYTES
        rep     movsb
        pop     rsi
        mov     r12, rdi                ; début de FilePathList
        xor     ebx, ebx                ; le volume, sans son nœud de fin
.vol:   cmp     ebx, NV_OPT_SIZE - 300
        ja      .fail
        cmp     byte [rsi + rbx], 0x7F
        jne     @f
        cmp     byte [rsi + rbx + 1], 0xFF
        je      .vend
@@:     movzx   eax, word [rsi + rbx + 2]
        cmp     eax, 4
        jb      .fail
        add     ebx, eax
        jmp     .vol
.vend:  mov     ecx, ebx
        rep     movsb
        mov     rax, [loaded_image]     ; puis le fichier de ce chargeur, nœud de fin compris
        mov     rsi, [rax + LI_FILEPATH]
        test    rsi, rsi
        jz      .fail
        xor     ebx, ebx
.file:  cmp     ebx, 280
        ja      .fail
        movzx   eax, word [rsi + rbx + 2]
        cmp     eax, 4
        jb      .fail
        cmp     byte [rsi + rbx], 0x7F
        jne     @f
        cmp     byte [rsi + rbx + 1], 0xFF
        jne     @f
        add     ebx, 4
        jmp     .fend
@@:     add     ebx, eax
        jmp     .file
.fend:  mov     ecx, ebx
        rep     movsb
        mov     rax, rdi
        sub     rax, r12                ; FilePathListLength
        mov     word [nv_opt + 4], ax
        lea     r9, [rdi]
        lea     rax, [nv_opt]
        sub     r9, rax                 ; taille de l'option
        lea     rcx, [nv_boot]
        lea     rdx, [global_guid]
        mov     r8d, EFI_VAR_NV_BS_RT
        lea     rax, [nv_opt]
        mov     [rsp + 32], rax
        mov     rax, [runtime]
        call    qword [rax + RT_SET_VAR]
        test    rax, rax
        jnz     .fail
        lea     rsi, [d_nv_boot]
        call    dbg

        ; BootOrder : 4B4F en tête, puis les autres (sans un ancien 4B4F)
        mov     qword [var_size], NV_ORDER_MAX * 2
        lea     rcx, [nv_order_name]
        lea     rdx, [global_guid]
        lea     r8, [var_attr]
        lea     r9, [var_size]
        lea     rax, [nv_order_old]
        mov     [rsp + 32], rax
        mov     rax, [runtime]
        call    qword [rax + RT_GET_VAR]
        test    rax, rax
        jz      @f
        mov     qword [var_size], 0     ; pas encore d'ordre (ou trop long : remplacé)
@@:     lea     rdi, [nv_order_new]
        mov     word [rdi], 0x4B4F
        mov     r9d, 2
        xor     ecx, ecx
.ord:   mov     rax, rcx
        add     rax, rax
        cmp     rax, [var_size]
        jae     .oend
        movzx   eax, word [nv_order_old + rcx * 2]
        inc     ecx
        cmp     eax, 0x4B4F
        je      .ord
        cmp     r9d, NV_ORDER_MAX * 2
        jae     .oend
        mov     [rdi + r9], ax
        add     r9d, 2
        jmp     .ord
.oend:  lea     rcx, [nv_order_name]
        lea     rdx, [global_guid]
        mov     r8d, EFI_VAR_NV_BS_RT
        lea     rax, [nv_order_new]
        mov     [rsp + 32], rax
        mov     rax, [runtime]
        call    qword [rax + RT_SET_VAR]
        test    rax, rax
        jnz     .fail
        lea     rsi, [d_nv_order]
        call    dbg

        ; boot.cfg : « install » devient « done   » (même longueur : rien d'autre ne bouge)
        mov     rcx, [root_dir]
        lea     rdx, [fe_file]
        lea     r8, [cfg_path]
        mov     r9, 3                   ; EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        test    rax, rax
        jnz     .cfgfail
        mov     rcx, [fe_file]
        mov     rdx, [cfg_fw_off]
        call    qword [rcx + FP_SET_POS]
        mov     qword [var_size], 7
        mov     rcx, [fe_file]
        lea     rdx, [var_size]
        lea     r8, [nv_done]
        call    qword [rcx + FP_WRITE]
        mov     rbx, rax
        mov     rcx, [fe_file]
        call    qword [rcx + FP_CLOSE]
        test    rbx, rbx
        jnz     .cfgfail
        lea     rsi, [d_nv_done]
        call    dbg
        jmp     .out
.cfgfail:
        lea     rsi, [d_nv_cfg]
        call    dbg
        jmp     .out
.fail:  lea     rsi, [d_nv_fail]
        call    dbg
.out:   pleave

; ---------------------------------------------------------------------------
; target_find : un autre volume dont \korin\boot.cfg a une ligne « firmware = install » -> tgt_device (0 : aucun)
; (en début de ligne : osinstall écrivait aussi ces mots dans le commentaire de tête, que l'ancien test trouvait :
; la clé relançait alors toujours le Korin installé, même après son premier démarrage)
target_find:
        penter
        mov     qword [tgt_device], 0
        mov     qword [fw_handles], 0
        mov     qword [fw_count], 0
        mov     ecx, 2                  ; ByProtocol
        lea     rdx, [sfs_guid]
        xor     r8, r8
        lea     r9, [fw_count]
        lea     rax, [fw_handles]
        mov     [rsp + 32], rax
        mov     rax, [bootsvc]
        call    qword [rax + BS_LOCATE_HBUF]
        test    rax, rax
        jnz     .none
        xor     r12d, r12d
.next:  cmp     r12, [fw_count]
        jae     .free
        mov     rax, [fw_handles]
        mov     rbx, [rax + r12 * 8]
        inc     r12
        mov     rax, [loaded_image]     ; (pas notre propre volume)
        cmp     rbx, [rax + LI_DEVICE]
        je      .next
        mov     rcx, rbx
        lea     rdx, [sfs_guid]
        lea     r8, [fw_sfs]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     .next
        mov     rcx, [fw_sfs]
        lea     rdx, [fw_root]
        call    qword [rcx + SFS_OPEN_VOLUME]
        test    rax, rax
        jnz     .next
        mov     rcx, [fw_root]
        lea     rdx, [fe_file]
        lea     r8, [cfg_path]
        mov     r9, 1                   ; EFI_FILE_MODE_READ
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        mov     r14, rax
        test    rax, rax
        jnz     .close
        mov     qword [var_size], TF_BUF_SIZE - 1
        mov     rcx, [fe_file]
        lea     rdx, [var_size]
        lea     r8, [tf_buf]
        call    qword [rcx + FP_READ]
        mov     r14, rax
        mov     rcx, [fe_file]
        call    qword [rcx + FP_CLOSE]
.close: mov     rcx, [fw_root]
        call    qword [rcx + FP_CLOSE]
        test    r14, r14
        jnz     .next
        ; « firmware = install » dans les octets lus ?
        mov     r9, [var_size]
        cmp     r9, NV_KEY_BYTES
        jb      .next
        sub     r9, NV_KEY_BYTES
        xor     ecx, ecx
.scan:  cmp     rcx, r9
        ja      .next
        test    rcx, rcx                ; (en début de ligne seulement : un commentaire qui cite la clé ne compte pas)
        jz      .beg
        cmp     byte [tf_buf + rcx - 1], 10
        jne     .adv
.beg:   xor     edx, edx
.cmp:   cmp     edx, NV_KEY_BYTES
        jae     .found
        movzx   eax, byte [tf_buf + rcx + rdx]
        cmp     al, [nv_key + rdx]
        jne     .adv
        inc     edx
        jmp     .cmp
.adv:   inc     rcx
        jmp     .scan
.found: mov     [tgt_device], rbx
        lea     rsi, [d_tgt_found]
        call    dbg
.free:  mov     rcx, [fw_handles]
        mov     rax, [bootsvc]
        call    qword [rax + BS_FREE_POOL]
.none:  pleave
