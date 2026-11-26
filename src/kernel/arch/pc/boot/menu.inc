; Menu de démarrage du chargeur UEFI de Korin OS (inclus par bootx64.asm).
;
; Entrées : « Korin OS », « Windows » (si \EFI\Microsoft\Boot\bootmgfw.efi est sur l'un des volumes),
; « Réglages du firmware (BIOS) » (si OsIndicationsSupported a EFI_OS_INDICATIONS_BOOT_TO_FW_UI),
; « Redémarrer », « Éteindre ». Flèches haut et bas, Début, Fin, Entrée ; un chiffre lance sa ligne
; (aussi la rangée du haut d'un clavier AZERTY sans Maj : & é " ' ( - è _ ç). Compte à rebours vers
; le choix par défaut ; une touche l'arrête.
;
; Réglages : \korin\boot.cfg sur le même volume (texte, une clé par ligne, « # » pour un commentaire) :
;   default = korin | windows      (défaut : windows s'il est là, sinon korin)
;   timeout = secondes             (défaut 5 ; 0 = sans menu, sauf une touche tenue au lancement)
;   cmdline = texte                (options du noyau, 127 octets au plus : bootinfo.cmdline)
;   firmware = install             (l'installateur : ce chargeur crée son entrée du firmware, firmware.inc)
;   target = auto                  (la clé d'installation : lancer d'abord un Korin installé qui attend son entrée)
;
; Le noyau, l'initrd et boot.cfg sont lus sur le volume d'où ce chargeur a démarré (sur le PC : la
; partition KORIN OS) ; bootmgfw.efi est cherché sur TOUS les volumes (find_windows, en lecture
; seule), la partition système EFI d'abord.
;
; « Windows » : LoadImage + StartImage de bootmgfw.efi par un vrai chemin de périphérique (celui du
; volume où il a été trouvé, puis un nœud de fichier, puis le nœud de fin) : le gestionnaire de
; Windows trouve son BCD comme s'il avait été lancé par le firmware. S'il échoue ou rend la main :
; message et retour au menu.
;
; Traces sur le port 0xE9 (« boot: … ») : debugcon de QEMU, comme la console du noyau.

ID_KORIN        = 0
ID_WINDOWS      = 1
ID_FW           = 2
ID_REBOOT       = 3
ID_OFF          = 4
ID_SAFE         = 5                     ; Korin OS, la version de secours (ab.inc)

MENU_W          = 46            ; largeur de la barre de sélection
STATUS_W        = 72            ; largeur des lignes d'état et de message
TICK_US         = 50000         ; 50 ms entre deux lectures du clavier
TICKS_S         = 20
LINE_MAX        = 200           ; caractères du tampon de ligne
ASCII_MAX       = 255
CHAIN_DP_SIZE   = 1024

A_NORMAL        = 0x07          ; gris clair sur noir
A_TITLE         = 0x0F          ; blanc
A_SEL           = 0x1F          ; blanc sur bleu
A_WARN          = 0x0E          ; jaune

EFI_RESET_COLD     = 0
EFI_RESET_SHUTDOWN = 2
EFI_VAR_NV_BS_RT   = 7          ; NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS

macro penter {
        push    rbp
        mov     rbp, rsp
        push    rbx
        push    rsi
        push    rdi
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 56                 ; 32 réservés + arguments 5 à 7 ; pile alignée
}
macro pleave {
        add     rsp, 56
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rdi
        pop     rsi
        pop     rbx
        pop     rbp
        ret
}

; ---------------------------------------------------------------------------
; boot_menu : réglages, menu, lancement. Ne revient que lorsque Korin est chargé
; (noyau et initrd en mémoire) ; Windows, le firmware, Redémarrer et Éteindre ne
; reviennent que sur un échec, et le menu reprend.
boot_menu:
        penter
        call    cfg_load
        call    ab_load
        cmp     byte [cfg_fw], 0
        je      @f
        call    nvram_register          ; (firmware.inc)
@@:     call    connect_blocks
        cmp     byte [cfg_target], 0
        je      .notgt
        call    target_find             ; la clé : un Korin installé à inscrire ?
        mov     rax, [tgt_device]
        test    rax, rax
        jz      .notgt
        mov     [chain_dev], rax
        lea     rax, [tgt_path]
        mov     [chain_path], rax
        mov     qword [chain_plen], TGT_PATH_BYTES
        lea     rax, [msg_tgt]
        mov     [chain_msg], rax
        lea     rax, [msg_t_load]
        mov     [chain_mload], rax
        lea     rax, [msg_t_back]
        mov     [chain_mback], rax
        call    chain_windows           ; (ne revient que sur un échec : le menu de la clé suit)
.notgt: call    find_windows
        call    fwui_check
        mov     [has_fwui], al
        mov     rcx, [conout]
        lea     rdx, [accent_test]
        call    qword [rcx + CO_TEST]
        test    rax, rax
        setz    byte [accents_ok]

        ; les entrées, dans l'ordre
        lea     rdi, [menu_ids]
        xor     ebx, ebx
        mov     byte [rdi + rbx], ID_KORIN
        inc     ebx
        push    rdi
        call    ab_has_safe             ; (une version en essai : la version de secours aussi, au clavier)
        pop     rdi
        test    al, al
        jz      @f
        mov     byte [rdi + rbx], ID_SAFE
        inc     ebx
@@:     cmp     byte [has_windows], 0
        je      @f
        mov     byte [rdi + rbx], ID_WINDOWS
        inc     ebx
@@:     cmp     byte [has_fwui], 0
        je      @f
        mov     byte [rdi + rbx], ID_FW
        inc     ebx
@@:     mov     byte [rdi + rbx], ID_REBOOT
        inc     ebx
        mov     byte [rdi + rbx], ID_OFF
        inc     ebx
        mov     [menu_count], rbx

        ; le choix par défaut : boot.cfg, sinon Windows s'il est là, sinon Korin
        mov     ecx, ID_KORIN
        cmp     byte [cfg_default], 1
        je      .def
        cmp     byte [has_windows], 0
        je      .def
        mov     ecx, ID_WINDOWS
.def:   mov     [def_id], cl
        xor     eax, eax
.find:  cmp     [rdi + rax], cl
        je      .found
        inc     eax
        jmp     .find
.found: mov     [sel], rax
        mov     rax, [cfg_timeout]
        test    rax, rax
        jns     @f
        mov     eax, 5
@@:     mov     [timeout], rax

        lea     rsi, [d_menu]
        call    dbg
        lea     rsi, [d_def_korin]
        cmp     byte [def_id], ID_KORIN
        je      @f
        lea     rsi, [d_def_windows]
@@:     call    dbg
        lea     rsi, [d_timeout]
        call    dbg_str
        mov     rax, [timeout]
        call    dbg_num
        cmp     byte [has_fwui], 0
        je      @f
        lea     rsi, [d_fwui]
        call    dbg
@@:
        ; délai nul : pas de menu, sauf si une touche est déjà là
        cmp     qword [timeout], 0
        jne     .count
        call    read_key
        test    rax, rax
        jz      .nocount
        movzx   ecx, byte [def_id]
        call    launch
        test    rax, rax
        jz      .done
        jmp     .nocount
.count:
        mov     r12, [timeout]
        imul    r12, r12, TICKS_S
        mov     byte [counting], 1
        mov     rax, [timeout]
        mov     [secs], rax
        jmp     .draw
.nocount:
        mov     byte [counting], 0
.draw:
        call    menu_draw

.loop:
        cmp     byte [counting], 0
        je      .key
        test    r12, r12
        jz      .expire
        mov     rax, r12
        xor     edx, edx
        mov     ecx, TICKS_S
        div     rcx
        test    rdx, rdx
        jnz     .tick
        mov     [secs], rax
        call    menu_status
.tick:  dec     r12
.key:   call    read_key
        test    rax, rax
        jz      .got
        mov     ecx, TICK_US
        mov     rax, [bootsvc]
        call    qword [rax + BS_STALL]
        jmp     .loop

.expire:
        lea     rsi, [d_expire]
        call    dbg
        movzx   ecx, byte [def_id]
        jmp     .go

.got:   cmp     byte [counting], 0      ; une touche : fin du compte à rebours
        je      .handle
        mov     byte [counting], 0
        lea     rsi, [d_stop]
        call    dbg
        call    menu_status
.handle:
        movzx   eax, word [key]         ; ScanCode
        movzx   edx, word [key + 2]     ; UnicodeChar
        cmp     eax, 1                  ; haut
        je      .up
        cmp     eax, 2                  ; bas
        je      .down
        cmp     eax, 5                  ; Début
        je      .home
        cmp     eax, 6                  ; Fin
        je      .end
        cmp     edx, 13                 ; Entrée
        je      .enter
        ; un chiffre : '1' à '9', ou la rangée du haut d'un AZERTY sans Maj
        lea     eax, [edx - '1']
        cmp     eax, 9
        jb      .digit
        lea     rsi, [azerty_row]
        xor     eax, eax
.az:    movzx   ecx, word [rsi + rax * 2]
        test    ecx, ecx
        jz      .loop
        cmp     ecx, edx
        je      .digit
        inc     eax
        jmp     .az
.digit: cmp     rax, [menu_count]
        jae     .loop
        mov     [sel], rax
        call    menu_items
        jmp     .enter

.up:    mov     rax, [sel]
        test    rax, rax
        jnz     @f
        mov     rax, [menu_count]
@@:     dec     rax
        mov     [sel], rax
        call    menu_items
        jmp     .loop
.down:  mov     rax, [sel]
        inc     rax
        cmp     rax, [menu_count]
        jb      @f
        xor     eax, eax
@@:     mov     [sel], rax
        call    menu_items
        jmp     .loop
.home:  mov     qword [sel], 0
        call    menu_items
        jmp     .loop
.end:   mov     rax, [menu_count]
        dec     rax
        mov     [sel], rax
        call    menu_items
        jmp     .loop

.enter: lea     rdi, [menu_ids]
        mov     rax, [sel]
        movzx   ecx, byte [rdi + rax]
.go:    call    launch
        test    rax, rax
        jz      .done
        mov     byte [counting], 0      ; échec : le menu, sans compte à rebours
        call    menu_draw
        jmp     .loop
.done:
        pleave

; ---------------------------------------------------------------------------
; launch : ecx = entrée. rax = 0 : Korin est chargé ; 1 : échec (msg_ptr posé).
launch:
        penter
        mov     ebx, ecx
        mov     qword [msg_ptr], 0
        mov     byte [msg_has_code], 0
        lea     rdi, [dbg_launch]
        mov     rsi, [rdi + rbx * 8]
        call    dbg
        cmp     ebx, ID_SAFE
        je      .korin
        cmp     ebx, ID_KORIN
        jne     .win
.korin: xor     ecx, ecx
        cmp     ebx, ID_SAFE
        sete    cl
        call    ab_choose
        call    cls
        lea     rdx, [msg_loading]
        call    print
        call    korin_load
        test    rax, rax
        jz      .out
        mov     [msg_ptr], rax
        jmp     .fail
.win:   cmp     ebx, ID_WINDOWS
        jne     .fw
        mov     rax, [win_device]
        mov     [chain_dev], rax
        lea     rax, [win_path]
        mov     [chain_path], rax
        mov     qword [chain_plen], WIN_PATH_BYTES
        lea     rax, [msg_win]
        mov     [chain_msg], rax
        lea     rax, [msg_w_load]
        mov     [chain_mload], rax
        lea     rax, [msg_w_back]
        mov     [chain_mback], rax
        call    chain_windows
        jmp     .fail
.fw:    cmp     ebx, ID_FW
        jne     .reset
        call    fw_setup
        jmp     .fail
.reset: mov     ecx, EFI_RESET_COLD
        cmp     ebx, ID_REBOOT
        je      @f
        mov     ecx, EFI_RESET_SHUTDOWN
@@:     xor     edx, edx
        xor     r8, r8
        xor     r9, r9
        mov     rax, [runtime]
        call    qword [rax + RT_RESET]
        lea     rax, [msg_reset]
        mov     [msg_ptr], rax
.fail:  mov     eax, 1
.out:   pleave

; ---------------------------------------------------------------------------
; korin_load : \korin\kernel.elf (chargé et placé), \korin\initrd.tar et \korin\local.tar
; (facultatifs).
; rax = 0, ou le message d'erreur (pour la ligne de message du menu).
korin_load:
        sub     rsp, 40
.again: mov     rdx, [kpath_ptr]
        call    read_file
        test    rax, rax
        jz      .kok
        call    ab_fallback             ; (illisible : la version confirmée, puis les fichiers d'avant A/B)
        test    eax, eax
        jnz     .again
        jmp     .nofile
.kok:
        mov     rax, [rf_buf]
        mov     [kfile_buf], rax
        call    load_elf
        test    rax, rax
        jz      .elfok
        call    ab_fallback             ; (invalide : de même)
        test    eax, eax
        jnz     .again
        jmp     .badelf
.elfok:
        mov     rdx, [ipath_ptr]
        call    read_file
        test    rax, rax
        jnz     .ok
        mov     rax, [rf_buf]
        mov     [bootinfo + BB_INITRD], rax
        mov     rax, [rf_size]
        mov     [bootinfo + BB_INITRD_SIZE], rax
        ; \korin\local.tar (facultatif) : les fichiers propres à cette machine (nvme.txt…), que le
        ; noyau fait passer avant ceux de l'initrd ; l'initrd vient des paquets, local.tar jamais
        lea     rdx, [local_path]
        call    read_file
        test    rax, rax
        jnz     .ok
        mov     rax, [rf_buf]
        mov     [bootinfo + BB_LOCAL], rax
        mov     rax, [rf_size]
        mov     [bootinfo + BB_LOCAL_SIZE], rax
        lea     rsi, [d_local]
        call    dbg
.ok:    xor     eax, eax
        jmp     .out
.nofile:
        lea     rax, [m_nokernel]
        jmp     .out
.badelf:
        lea     rax, [m_badelf]
.out:   add     rsp, 40
        ret

; ---------------------------------------------------------------------------
; chain_windows : lance \EFI\Microsoft\Boot\bootmgfw.efi. Ne revient que sur un
; échec, ou si le programme rend la main : msg_ptr (et msg_code) posés.
chain_windows:
        penter
        ; chemin de périphérique du volume (chain_dev : celui de bootmgfw.efi, ou d'un Korin installé)
        mov     rcx, [chain_dev]
        test    rcx, rcx
        jz      .nodp
        lea     rdx, [devpath_guid]
        lea     r8, [vol_dp]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     .nodp
        mov     rsi, [vol_dp]
        xor     ebx, ebx                ; longueur sans le nœud de fin
.walk:  cmp     rbx, CHAIN_DP_SIZE - 512
        ja      .nodp
        cmp     byte [rsi + rbx], 0x7F
        jne     @f
        cmp     byte [rsi + rbx + 1], 0xFF
        je      .end
@@:     movzx   eax, word [rsi + rbx + 2]
        cmp     eax, 4
        jb      .nodp
        add     rbx, rax
        jmp     .walk
.end:   lea     rdi, [chain_dp]
        mov     rcx, rbx
        rep     movsb
        ; nœud de fichier : MEDIA_DEVICE_PATH (4), MEDIA_FILEPATH_DP (4), longueur, chemin UTF-16
        mov     rax, [chain_plen]
        add     eax, 4
        shl     eax, 16
        or      eax, 4 + (4 shl 8)
        mov     dword [rdi], eax
        add     rdi, 4
        mov     rsi, [chain_path]
        mov     rcx, [chain_plen]
        rep     movsb
        mov     dword [rdi], 0x7F + (0xFF shl 8) + (4 shl 16) ; fin du chemin entier

        call    cls
        mov     rdx, [chain_msg]
        call    print
        ; chien de garde de 5 minutes, comme le firmware avant une option de démarrage
        mov     ecx, 300
        xor     edx, edx
        xor     r8, r8
        xor     r9, r9
        mov     rax, [bootsvc]
        call    qword [rax + BS_WATCHDOG]

        mov     qword [win_handle], 0
        xor     ecx, ecx                ; BootPolicy = FALSE
        mov     rdx, [image_handle]
        lea     r8, [chain_dp]
        xor     r9, r9                  ; pas de tampon : le firmware lit le fichier
        mov     qword [rsp + 32], 0
        lea     rax, [win_handle]
        mov     [rsp + 40], rax
        mov     rax, [bootsvc]
        call    qword [rax + BS_LOAD_IMAGE]
        test    rax, rax
        jnz     .loadfail
        lea     rsi, [d_win_start]
        call    dbg
        mov     rcx, [win_handle]
        xor     edx, edx
        xor     r8, r8
        mov     rax, [bootsvc]
        call    qword [rax + BS_START_IMAGE]
        mov     rbx, rax
        lea     rsi, [d_win_back]
        call    dbg
        mov     rax, [chain_mback]
        jmp     .code
.loadfail:
        mov     rbx, rax
        lea     rsi, [d_win_fail]
        call    dbg
        mov     rcx, [win_handle]       ; chargé mais refusé (sécurité) : à décharger
        test    rcx, rcx
        jz      @f
        mov     rax, [bootsvc]
        call    qword [rax + BS_UNLOAD_IMAGE]
@@:     mov     rax, [chain_mload]
.code:  mov     [msg_ptr], rax
        mov     [msg_code], rbx
        mov     byte [msg_has_code], 1
        call    watchdog_off
        mov     rcx, [conin]            ; touches laissées par l'autre programme
        xor     edx, edx
        call    qword [rcx + CI_RESET]
        jmp     .out
.nodp:  lea     rax, [msg_w_dp]
        mov     [msg_ptr], rax
.out:   pleave

; ---------------------------------------------------------------------------
; fwui_check : al = 1 si le firmware sait démarrer sur ses réglages (OsIndicationsSupported)
fwui_check:
        sub     rsp, 56
        mov     qword [var_size], 8
        mov     qword [var_val], 0
        lea     rcx, [v_os_ind_sup]
        lea     rdx, [global_guid]
        xor     r8, r8
        lea     r9, [var_size]
        lea     rax, [var_val]
        mov     [rsp + 32], rax
        mov     rax, [runtime]
        call    qword [rax + RT_GET_VAR]
        test    rax, rax
        jnz     .no
        mov     eax, dword [var_val]
        and     eax, 1                  ; EFI_OS_INDICATIONS_BOOT_TO_FW_UI
        jmp     .out
.no:    xor     eax, eax
.out:   add     rsp, 56
        ret

; fw_setup : OsIndications |= BOOT_TO_FW_UI, puis redémarrage. Ne revient que sur un échec.
fw_setup:
        penter
        mov     qword [var_size], 8
        mov     qword [var_val], 0
        lea     rcx, [v_os_ind]
        lea     rdx, [global_guid]
        lea     r8, [var_attr]
        lea     r9, [var_size]
        lea     rax, [var_val]
        mov     [rsp + 32], rax
        mov     rax, [runtime]
        call    qword [rax + RT_GET_VAR]
        test    rax, rax
        jz      @f
        mov     qword [var_val], 0      ; pas encore de variable
@@:     or      qword [var_val], 1
        lea     rcx, [v_os_ind]
        lea     rdx, [global_guid]
        mov     r8d, EFI_VAR_NV_BS_RT
        mov     r9d, 8
        lea     rax, [var_val]
        mov     [rsp + 32], rax
        mov     rax, [runtime]
        call    qword [rax + RT_SET_VAR]
        test    rax, rax
        jnz     .err
        lea     rsi, [d_fw_reset]
        call    dbg
        mov     ecx, EFI_RESET_COLD
        xor     edx, edx
        xor     r8, r8
        xor     r9, r9
        mov     rax, [runtime]
        call    qword [rax + RT_RESET]
        lea     rax, [msg_reset]
        mov     [msg_ptr], rax
        jmp     .out
.err:   mov     [msg_code], rax
        mov     byte [msg_has_code], 1
        lea     rax, [msg_fw]
        mov     [msg_ptr], rax
.out:   pleave

; ---------------------------------------------------------------------------
; cfg_load : lit \korin\boot.cfg s'il est là (cfg_default, cfg_timeout, bootinfo.cmdline)
cfg_load:
        penter
        lea     rdx, [cfg_path]
        call    read_file
        test    rax, rax
        jnz     .out
        lea     rsi, [d_cfg]
        call    dbg
        mov     rsi, [rf_buf]
        mov     r12, rsi
        add     r12, [rf_size]          ; fin
        cmp     qword [rf_size], 3      ; BOM UTF-8
        jb      .line
        cmp     word [rsi], 0xBBEF
        jne     .line
        cmp     byte [rsi + 2], 0xBF
        jne     .line
        add     rsi, 3
.line:  cmp     rsi, r12                ; blancs et fins de ligne
        jae     .out
        movzx   eax, byte [rsi]
        cmp     al, ' '
        ja      @f
        inc     rsi
        jmp     .line
@@:     cmp     al, '#'
        je      .skip
        mov     r13, rsi                ; la clé : jusqu'à un blanc ou « = »
.k:     cmp     rsi, r12
        jae     .skip
        movzx   eax, byte [rsi]
        cmp     al, '='
        je      .keyend
        cmp     al, ' '
        jbe     .keyend
        inc     rsi
        jmp     .k
.keyend:
        mov     r14, rsi
        sub     r14, r13                ; longueur de la clé
.b1:    cmp     rsi, r12
        jae     .skip
        movzx   eax, byte [rsi]
        cmp     al, ' '
        je      .b1n
        cmp     al, 9
        jne     .b1d
.b1n:   inc     rsi
        jmp     .b1
.b1d:   cmp     al, '='
        jne     .skip
        inc     rsi
.b2:    cmp     rsi, r12
        jae     .val
        movzx   eax, byte [rsi]
        cmp     al, ' '
        je      .b2n
        cmp     al, 9
        jne     .val
.b2n:   inc     rsi
        jmp     .b2
.val:   mov     r15, rsi                ; la valeur : jusqu'à la fin de la ligne
.v:     cmp     rsi, r12
        jae     .vend
        movzx   eax, byte [rsi]
        cmp     al, 10
        je      .vend
        cmp     al, 13
        je      .vend
        inc     rsi
        jmp     .v
.vend:  mov     rbx, rsi
.trim:  cmp     rbx, r15
        jbe     .gotval
        movzx   eax, byte [rbx - 1]
        cmp     al, ' '
        je      .tr
        cmp     al, 9
        jne     .gotval
.tr:    dec     rbx
        jmp     .trim
.gotval:
        sub     rbx, r15                ; longueur de la valeur

        mov     rcx, r13
        mov     rdx, r14
        lea     r8, [k_default]
        call    kw_eq
        test    eax, eax
        jz      .t1
        mov     rcx, r15
        mov     rdx, rbx
        lea     r8, [k_korin]
        call    kw_eq
        test    eax, eax
        jz      @f
        mov     byte [cfg_default], 1
        jmp     .skip
@@:     mov     rcx, r15
        mov     rdx, rbx
        lea     r8, [k_windows]
        call    kw_eq
        test    eax, eax
        jz      .skip
        mov     byte [cfg_default], 2
        jmp     .skip

.t1:    mov     rcx, r13
        mov     rdx, r14
        lea     r8, [k_timeout]
        call    kw_eq
        test    eax, eax
        jz      .t2
        xor     eax, eax
        xor     ecx, ecx
        mov     rdx, r15
        lea     r9, [r15 + rbx]
.dg:    cmp     rdx, r9
        jae     .dgd
        movzx   r8d, byte [rdx]
        sub     r8d, '0'
        cmp     r8d, 9
        ja      .skip                   ; pas un nombre : ignoré
        imul    eax, eax, 10
        add     eax, r8d
        cmp     eax, 3600
        jbe     @f
        mov     eax, 3600
@@:     inc     rdx
        inc     ecx
        jmp     .dg
.dgd:   test    ecx, ecx
        jz      .skip
        mov     [cfg_timeout], rax
        jmp     .skip

.t2:    mov     rcx, r13
        mov     rdx, r14
        lea     r8, [k_cmdline]
        call    kw_eq
        test    eax, eax
        jz      .t3
        lea     rdi, [bootinfo + BB_CMDLINE]
        mov     rcx, rbx
        cmp     rcx, BB_CMDLINE_MAX - 1
        jbe     @f
        mov     ecx, BB_CMDLINE_MAX - 1
@@:     push    rsi
        mov     rsi, r15
        rep     movsb
        pop     rsi
        mov     byte [rdi], 0
        jmp     .skip

.t3:    mov     rcx, r13                ; firmware = install (sa position, pour la récrire)
        mov     rdx, r14
        lea     r8, [k_firmware]
        call    kw_eq
        test    eax, eax
        jz      .t4
        cmp     rbx, 7
        jne     .skip
        mov     rcx, r15
        mov     rdx, rbx
        lea     r8, [k_install]
        call    kw_eq
        test    eax, eax
        jz      .skip
        mov     byte [cfg_fw], 1
        mov     rax, r15
        sub     rax, [rf_buf]
        mov     [cfg_fw_off], rax
        jmp     .skip

.t4:    mov     rcx, r13                ; target = auto
        mov     rdx, r14
        lea     r8, [k_target]
        call    kw_eq
        test    eax, eax
        jz      .skip
        mov     rcx, r15
        mov     rdx, rbx
        lea     r8, [k_auto]
        call    kw_eq
        test    eax, eax
        jz      .skip
        mov     byte [cfg_target], 1

.skip:  cmp     rsi, r12                ; jusqu'à la fin de la ligne
        jae     .out
        cmp     byte [rsi], 10
        je      .line
        inc     rsi
        jmp     .skip
.out:   pleave

; kw_eq : rcx = texte, rdx = longueur, r8 = mot ASCII en minuscules (fini par 0).
; eax = 1 s'ils sont égaux (sans tenir compte de la casse du texte).
kw_eq:
        xor     r9d, r9d
.c:     movzx   eax, byte [r8 + r9]
        cmp     r9, rdx
        jae     .end
        test    eax, eax
        jz      .no
        movzx   r10d, byte [rcx + r9]
        cmp     r10d, 'A'
        jb      @f
        cmp     r10d, 'Z'
        ja      @f
        add     r10d, 32
@@:     cmp     eax, r10d
        jne     .no
        inc     r9
        jmp     .c
.end:   test    eax, eax
        jnz     .no
        mov     eax, 1
        ret
.no:    xor     eax, eax
        ret

; ---------------------------------------------------------------------------
; connect_blocks : demande au firmware de brancher ses pilotes sur tous les périphériques
; blocs (disques et partitions : ConnectController, récursif). Lancé par une entrée de
; l'ordre de démarrage, le firmware HP n'ouvre le système de fichiers que de la partition
; de cette entrée (KORIN OS) : sans cela, find_windows ne voit pas la partition système EFI
; de Windows (par F9 > Boot From EFI File, tout était déjà branché).
connect_blocks:
        penter
        mov     qword [fw_handles], 0
        mov     qword [fw_count], 0
        xor     ecx, ecx                ; AllHandles : aussi les contrôleurs sans pilote encore (le NVMe quand on
        xor     edx, edx                ; démarre d'une clé USB : sinon ni Windows ni un Korin installé ne se voient)
        xor     r8, r8
        lea     r9, [fw_count]
        lea     rax, [fw_handles]
        mov     [rsp + 32], rax
        mov     rax, [bootsvc]
        call    qword [rax + BS_LOCATE_HBUF]
        test    rax, rax
        jnz     .done
        xor     r12d, r12d
.next:  cmp     r12, [fw_count]
        jae     .free
        mov     rax, [fw_handles]
        mov     rcx, [rax + r12 * 8]
        inc     r12
        xor     edx, edx                ; tous les pilotes
        xor     r8, r8                  ; tous les enfants
        mov     r9, 1                   ; récursif
        mov     rax, [bootsvc]
        call    qword [rax + BS_CONNECT]  ; (un refus : rien à brancher)
        jmp     .next
.free:  mov     rcx, [fw_handles]
        mov     rax, [bootsvc]
        call    qword [rax + BS_FREE_POOL]
.done:  pleave

; ---------------------------------------------------------------------------
; find_windows : cherche \EFI\Microsoft\Boot\bootmgfw.efi sur tous les volumes que le
; firmware connaît (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL), en lecture seule. Le premier volume
; qui l'a et qui est une partition système EFI (EFI_PARTITION_INFO_PROTOCOL.System = 1)
; gagne ; sinon le premier volume qui l'a (firmware sans PartitionInfo). win_device = son
; handle (0 : rien), has_windows = 1 s'il y en a un.
find_windows:
        penter
        mov     qword [win_device], 0
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
        xor     r12d, r12d              ; rang du volume
        xor     r13d, r13d              ; premier volume trouvé hors partition système EFI
.next:  cmp     r12, [fw_count]
        jae     .end
        mov     rax, [fw_handles]
        mov     rbx, [rax + r12 * 8]
        inc     r12
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
        jnz     .next                   ; pas de support, système de fichiers illisible
        mov     rcx, [fw_root]
        lea     rdx, [fe_file]
        lea     r8, [win_path]
        mov     r9, 1                   ; EFI_FILE_MODE_READ
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        mov     r14, rax
        test    rax, rax
        jnz     @f
        mov     rcx, [fe_file]
        call    qword [rcx + FP_CLOSE]
@@:     mov     rcx, [fw_root]
        call    qword [rcx + FP_CLOSE]
        test    r14, r14
        jnz     .next
        lea     rsi, [d_win_vol]
        call    dbg_str
        mov     rax, r12
        call    dbg_num
        mov     rcx, rbx                ; partition système EFI ?
        lea     rdx, [partinfo_guid]
        lea     r8, [fw_pinfo]
        mov     rax, [bootsvc]
        call    qword [rax + BS_HANDLE_PROTO]
        test    rax, rax
        jnz     .other
        mov     rax, [fw_pinfo]
        cmp     byte [rax + 8], 1       ; EFI_PARTITION_INFO_PROTOCOL.System
        jne     .other
        mov     [win_device], rbx
        lea     rsi, [d_win_esp]
        call    dbg
        jmp     .end
.other: test    r13, r13
        jnz     .next
        mov     r13, rbx
        jmp     .next
.end:   cmp     qword [win_device], 0
        jne     .free
        mov     [win_device], r13
.free:  mov     rcx, [fw_handles]
        mov     rax, [bootsvc]
        call    qword [rax + BS_FREE_POOL]
.none:  cmp     qword [win_device], 0
        setne   byte [has_windows]
        jne     @f
        lea     rsi, [d_win_none]
        call    dbg
@@:     pleave

; ---------------------------------------------------------------------------
; Affichage

; menu_draw : tout l'écran (titre, entrées, état, message, aide)
menu_draw:
        penter
        call    cls
        mov     rcx, [conout]
        xor     edx, edx
        call    qword [rcx + CO_ENABLE_CUR]
        mov     rcx, [conout]
        mov     rax, [rcx + CO_MODE]
        mov     edx, dword [rax + 4]    ; mode courant
        lea     r8, [cols]
        lea     r9, [rows]
        call    qword [rcx + CO_QUERY]
        test    rax, rax
        jz      @f
        mov     qword [cols], 80
        mov     qword [rows], 25
@@:     xor     eax, eax                ; colonne : centrée sur STATUS_W
        mov     rcx, [cols]
        sub     rcx, STATUS_W
        jbe     @f
        shr     rcx, 1
        mov     rax, rcx
@@:     mov     [left], rax
        xor     eax, eax                ; ligne : centrée sur le bloc entier
        mov     rcx, [rows]
        sub     rcx, [menu_count]
        sub     rcx, 7
        jbe     @f
        shr     rcx, 1
        mov     rax, rcx
@@:     mov     [top], rax

        mov     ecx, dword [left]
        mov     edx, dword [top]
        call    cur_at
        mov     ecx, A_TITLE
        call    attr
        lea     rdx, [s_title]
        call    print
        call    menu_items
        call    menu_status
        mov     ecx, dword [left]
        mov     edx, dword [top]
        add     edx, dword [menu_count]
        add     edx, 6
        call    cur_at
        mov     ecx, A_NORMAL
        call    attr
        lea     rdx, [s_hint]
        call    print
        pleave

; menu_items : les entrées, la sélection en barre
menu_items:
        penter
        xor     ebx, ebx
.next:  cmp     rbx, [menu_count]
        jae     .done
        mov     ecx, dword [left]
        mov     edx, dword [top]
        lea     edx, [edx + ebx + 2]
        call    cur_at
        mov     ecx, A_NORMAL
        cmp     rbx, [sel]
        jne     @f
        mov     ecx, A_SEL
@@:     call    attr
        call    bclear
        lea     rsi, [s_2sp]
        call    bstr
        lea     eax, [ebx + '1']
        call    bchar
        lea     rsi, [s_2sp]
        call    bstr
        lea     rdi, [menu_ids]
        movzx   eax, byte [rdi + rbx]
        lea     rdi, [labels]
        mov     rsi, [rdi + rax * 8]
        call    bstr
        mov     ecx, MENU_W
        call    bpad
        call    bout
        inc     ebx
        jmp     .next
.done:  mov     ecx, A_NORMAL
        call    attr
        pleave

; menu_status : ligne du compte à rebours et ligne de message
menu_status:
        penter
        mov     ecx, dword [left]
        mov     edx, dword [top]
        add     edx, dword [menu_count]
        add     edx, 3
        call    cur_at
        mov     ecx, A_NORMAL
        call    attr
        call    bclear
        cmp     byte [counting], 0
        je      .pad
        lea     rsi, [s_count1]
        call    bstr
        movzx   eax, byte [def_id]
        lea     rdi, [labels]
        mov     rsi, [rdi + rax * 8]
        call    bstr
        lea     rsi, [s_count2]
        call    bstr
        mov     rax, [secs]
        call    bdec
        lea     rsi, [s_count3]
        call    bstr
.pad:   mov     ecx, STATUS_W
        call    bpad
        call    bout

        mov     ecx, dword [left]
        mov     edx, dword [top]
        add     edx, dword [menu_count]
        add     edx, 4
        call    cur_at
        mov     ecx, A_WARN
        call    attr
        call    bclear
        mov     rsi, [msg_ptr]
        test    rsi, rsi
        jz      .pad2
        call    bstr
        cmp     byte [msg_has_code], 0
        je      .pad2
        lea     rsi, [s_code]
        call    bstr
        mov     rax, [msg_code]
        call    bhex
        mov     rax, [msg_code]         ; son nom, pour les codes courants
        btr     rax, 63
        lea     rdi, [efi_names]
.nm:    mov     rcx, [rdi]
        test    rcx, rcx
        jz      .nmend
        cmp     rcx, rax
        je      .nmhit
        add     rdi, 16
        jmp     .nm
.nmhit: lea     rsi, [s_efi]
        call    bstr
        mov     rsi, [rdi + 8]
        call    bstr
.nmend: mov     eax, ')'
        call    bchar
.pad2:  mov     ecx, STATUS_W
        call    bpad
        call    bout
        mov     ecx, A_NORMAL
        call    attr
        pleave

; cur_at : ecx = colonne, edx = ligne
cur_at:
        sub     rsp, 40
        mov     r8d, edx
        mov     edx, ecx
        mov     rcx, [conout]
        call    qword [rcx + CO_CURSOR]
        add     rsp, 40
        ret

; attr : ecx = attribut (couleurs)
attr:
        sub     rsp, 40
        mov     edx, ecx
        mov     rcx, [conout]
        call    qword [rcx + CO_ATTR]
        add     rsp, 40
        ret

; cls : couleurs normales, écran effacé
cls:
        sub     rsp, 40
        mov     rcx, [conout]
        mov     edx, A_NORMAL
        call    qword [rcx + CO_ATTR]
        mov     rcx, [conout]
        call    qword [rcx + CO_CLEAR]
        add     rsp, 40
        ret

; read_key : rax = 0 si une touche a été lue (dans key)
read_key:
        sub     rsp, 40
        mov     rcx, [conin]
        lea     rdx, [key]
        call    qword [rcx + CI_READKEY]
        add     rsp, 40
        ret

; watchdog_off : coupe le chien de garde du firmware
watchdog_off:
        sub     rsp, 40
        xor     ecx, ecx
        xor     edx, edx
        xor     r8, r8
        xor     r9, r9
        mov     rax, [bootsvc]
        call    qword [rax + BS_WATCHDOG]
        add     rsp, 40
        ret

; Tampon de ligne (UTF-16) : bclear, bchar (ax), bstr (rsi), bdec et bhex (rax),
; bpad (ecx = largeur), bout (affiche). Seuls rax, rcx, rdx, r8 à r11 changent.
bclear:
        mov     qword [bpos], 0
        ret
bchar:
        mov     r10, [bpos]
        cmp     r10, LINE_MAX
        jae     @f
        lea     r11, [line_buf]
        mov     [r11 + r10 * 2], ax
        inc     qword [bpos]
@@:     ret
bstr:
        push    rsi
@@:     movzx   eax, word [rsi]
        test    eax, eax
        jz      @f
        call    bchar
        add     rsi, 2
        jmp     @b
@@:     pop     rsi
        ret
bdec:
        mov     ecx, 10
        xor     r8d, r8d
        lea     r9, [dec_tmp]
.d:     xor     edx, edx
        div     rcx
        add     edx, '0'
        mov     [r9 + r8], dl
        inc     r8
        test    rax, rax
        jnz     .d
.o:     dec     r8
        movzx   eax, byte [r9 + r8]
        call    bchar
        test    r8, r8
        jnz     .o
        ret
bhex:
        mov     r8, rax
        mov     eax, '0'
        call    bchar
        mov     eax, 'x'
        call    bchar
        mov     r9d, 16
.h:     rol     r8, 4
        mov     eax, r8d
        and     eax, 15
        cmp     eax, 10
        jb      @f
        add     eax, 'A' - '0' - 10
@@:     add     eax, '0'
        call    bchar
        dec     r9d
        jnz     .h
        ret
bpad:
        mov     edx, ecx
@@:     cmp     [bpos], rdx
        jae     @f
        mov     eax, ' '
        call    bchar
        jmp     @b
@@:     ret
bout:
        mov     r10, [bpos]
        lea     r11, [line_buf]
        mov     word [r11 + r10 * 2], 0
        mov     qword [bpos], 0
        lea     rdx, [line_buf]
        jmp     print

; dbg : rsi = chaîne ASCII, sur le port 0xE9 puis une fin de ligne ; dbg_str sans
; la fin de ligne ; dbg_num : rax en décimal, puis une fin de ligne
dbg:
        call    dbg_str
        mov     al, 10
        out     0xE9, al
        ret
dbg_str:
        push    rsi
@@:     lodsb
        test    al, al
        jz      @f
        out     0xE9, al
        jmp     @b
@@:     pop     rsi
        ret
dbg_num:
        mov     ecx, 10
        xor     r8d, r8d
        lea     r9, [dec_tmp]
.d:     xor     edx, edx
        div     rcx
        add     edx, '0'
        mov     [r9 + r8], dl
        inc     r8
        test    rax, rax
        jnz     .d
.o:     dec     r8
        mov     al, [r9 + r8]
        out     0xE9, al
        test    r8, r8
        jnz     .o
        mov     al, 10
        out     0xE9, al
        ret
