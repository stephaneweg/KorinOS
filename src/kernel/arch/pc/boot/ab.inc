; ab.inc : les deux emplacements des fichiers de démarrage, A et B (docs/etude/MISES-A-JOUR-AB.md, décision du
; 25/11/2026). \korin\ab.txt, écrit par pkg (commit d'une mise à jour du système) et réécrit ici sur place, à
; longueur égale (comme « firmware = install » de boot.cfg) :
;
;   confirmed=a trial=b tries=3 booted=a
;   0         10      18      26       35
;
; confirmed : la version de secours (a ou b) ; trial : la version en essai (a, b, ou « - » : aucune) ; tries : les
; démarrages d'essai qui restent (0 à 9) ; booted : la version démarrée (pour pkg). Une version en essai qui ne
; démarre pas use ses essais, puis le chargeur revient seul sur la version confirmée ; un démarrage réussi ne
; confirme jamais rien (pkg, à la mise à jour suivante, sur la réponse de root).
; Sans ab.txt (ou illisible) : \korin\kernel.elf et \korin\initrd.tar, comme avant.
; Les fichiers d'un emplacement : \korin\a\kernel.elf, \korin\a\initrd.tar (b de même).

AB_LEN          = 36
AB_CONF         = 10
AB_TRIAL        = 18
AB_TRIES        = 26
AB_BOOTED       = 35

; ab_load : lit ab.txt -> ab_ok (1 : lisible et bien formé), ab_buf
ab_load:
        penter
        mov     byte [ab_ok], 0
        mov     rcx, [root_dir]
        lea     rdx, [ab_file]
        lea     r8, [ab_path]
        mov     r9, 1                   ; EFI_FILE_MODE_READ
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        test    rax, rax
        jnz     .out
        mov     qword [ab_size], AB_LEN
        mov     rcx, [ab_file]
        lea     rdx, [ab_size]
        lea     r8, [ab_buf]
        call    qword [rcx + FP_READ]
        mov     rbx, rax
        mov     rcx, [ab_file]
        call    qword [rcx + FP_CLOSE]
        test    rbx, rbx
        jnz     .out
        cmp     qword [ab_size], AB_LEN
        jne     .out
        ; la forme : les clés à leur place, les valeurs dans leurs domaines
        lea     rsi, [ab_buf]
        lea     rdi, [ab_model]
        xor     ecx, ecx
.cmp:   cmp     ecx, AB_LEN
        jae     .vals
        mov     al, [rdi + rcx]
        cmp     al, '?'                 ; (une valeur)
        je      .skip
        cmp     al, [rsi + rcx]
        jne     .out
.skip:  inc     ecx
        jmp     .cmp
.vals:  mov     al, [ab_buf + AB_CONF]
        cmp     al, 'a'
        je      @f
        cmp     al, 'b'
        jne     .out
@@:     mov     al, [ab_buf + AB_TRIAL]
        cmp     al, '-'
        je      @f
        cmp     al, 'a'
        je      @f
        cmp     al, 'b'
        jne     .out
@@:     mov     al, [ab_buf + AB_TRIES]
        cmp     al, '0'
        jb      .out
        cmp     al, '9'
        ja      .out
        mov     byte [ab_ok], 1
        lea     rsi, [d_ab]
        call    dbg
.out:   pleave

; ab_has_safe -> al = 1 si une version en essai a une version de secours distincte (l'entrée du menu)
ab_has_safe:
        xor     eax, eax
        cmp     byte [ab_ok], 0
        je      .r
        mov     cl, [ab_buf + AB_TRIAL]
        cmp     cl, '-'
        je      .r
        cmp     cl, [ab_buf + AB_CONF]
        je      .r
        mov     al, 1
.r:     ret

; ab_choose : ecx = 1 la version de secours (l'entrée du menu), 0 le démarrage ordinaire. Pose kpath_ptr et
; ipath_ptr ; avec ab.txt : décompte l'essai, note la version démarrée, réécrit le fichier
ab_choose:
        penter
        mov     ebx, ecx
        lea     rax, [kernel_path]
        mov     [kpath_ptr], rax
        lea     rax, [initrd_path]
        mov     [ipath_ptr], rax
        cmp     byte [ab_ok], 0
        je      .out
        mov     al, [ab_buf + AB_CONF]
        test    ebx, ebx
        jnz     .slot
        mov     cl, [ab_buf + AB_TRIAL]
        cmp     cl, '-'
        je      .slot
        mov     dl, [ab_buf + AB_TRIES]
        cmp     dl, '0'
        jbe     .slot                   ; (plus d'essai : la version confirmée)
        dec     dl
        mov     [ab_buf + AB_TRIES], dl
        mov     al, cl
.slot:  mov     [ab_buf + AB_BOOTED], al
        mov     [ab_slot], al
        lea     rdx, [ka_path]
        lea     r8, [ia_path]
        cmp     al, 'a'
        je      @f
        lea     rdx, [kb_path]
        lea     r8, [ib_path]
@@:     mov     [kpath_ptr], rdx
        mov     [ipath_ptr], r8
        call    ab_write
.out:   pleave

; ab_write : ab_buf réécrit dans ab.txt (même longueur)
ab_write:
        penter
        mov     rcx, [root_dir]
        lea     rdx, [ab_file]
        lea     r8, [ab_path]
        mov     r9, 3                   ; EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE
        mov     qword [rsp + 32], 0
        call    qword [rcx + FP_OPEN]
        test    rax, rax
        jnz     .nowr
        mov     qword [ab_size], AB_LEN
        mov     rcx, [ab_file]
        lea     rdx, [ab_size]
        lea     r8, [ab_buf]
        call    qword [rcx + FP_WRITE]
        mov     rbx, rax
        mov     rcx, [ab_file]
        call    qword [rcx + FP_CLOSE]
        test    rbx, rbx
        jnz     .nowr
        lea     rsi, [d_ab_slot]
        call    dbg_str
        movzx   eax, byte [ab_slot]
        lea     rsi, [ab_slotstr]
        mov     [rsi], al
        call    dbg
        jmp     .out
.nowr:  lea     rsi, [d_ab_nowr]       ; (pas réécrit : le décompte n'avance pas, on démarre quand même)
        call    dbg
.out:   pleave

; ab_fallback : la lecture de kpath_ptr a échoué -> eax = 1 et d'autres chemins : la version confirmée (si on
; essayait l'autre), sinon les fichiers d'avant A/B ; 0 : plus rien à essayer
ab_fallback:
        penter
        lea     rsi, [d_ab_fall]
        call    dbg
        cmp     byte [ab_ok], 0
        je      .legacy
        mov     al, [ab_buf + AB_CONF]
        cmp     al, [ab_slot]
        je      .legacy                 ; (la confirmée a déjà échoué)
        mov     [ab_slot], al
        mov     [ab_buf + AB_BOOTED], al
        lea     rdx, [ka_path]
        lea     r8, [ia_path]
        cmp     al, 'a'
        je      @f
        lea     rdx, [kb_path]
        lea     r8, [ib_path]
@@:     mov     [kpath_ptr], rdx
        mov     [ipath_ptr], r8
        call    ab_write                ; (booted : la version qui démarre vraiment)
        mov     eax, 1
        jmp     .out
.legacy:
        lea     rax, [kernel_path]
        cmp     [kpath_ptr], rax
        je      .none
        mov     [kpath_ptr], rax
        lea     rax, [initrd_path]
        mov     [ipath_ptr], rax
        mov     byte [ab_slot], '-'
        mov     eax, 1
        jmp     .out
.none:  xor     eax, eax
.out:   pleave
