; ============================================================================
;  v3d.inc - le service " v3d " (SD0:/sys/svc/v3d) et GpuKit : protocole
;  (docs/V3D.md)
;
;  Le service possede le GPU V3D 4.2 : il l'allume, lance les jobs l'un
;  apres l'autre et reprend la main si l'un d'eux ne finit pas. GpuKit, dans
;  le process de l'application, fait tout le reste : listes de commandes,
;  sommets, shaders, textures, cache du processeur. Le GPU n'a pas de MMU
;  ici : il lit et ecrit des adresses physiques, donc dans des tampons
;  partages (BUF_CREATE avec BUF_LOW : un seul bloc sous 1 Go ; BUF_INFO
;  donne leur adresse physique).
;
;  Message (512 o, SVC_CALL) : +0 op, puis les arguments ; resultat en x0.
; ============================================================================

V3D_SVC_NAME	equ 'v3d'

V3D_OP_INFO	= 1		; -> 1 pret / 0 absent ; +8 : V3DI_* (reponse)
V3D_OP_RUN	= 2		; un job (V3DR_*) -> 0 fait, V3D_E_*
V3D_OP_SUBMIT	= 3		; un job de Mesa : memes champs, en adresses physiques (plus bas)
V3D_OP_HEALTH	= 4		; -> 1 ; +8 : V3DH_* (reponse) : le GPU et la carte (bin/gpuinfo)

; --- reponse de V3D_OP_INFO --------------------------------------------------
V3DI_STATE	= 8		; 1 pret, 0 absent (QEMU, no_gpu), -1 eteint apres un blocage
V3DI_VERSION	= 16		; 42 : V3D 4.2
V3DI_REV	= 24		; revision (HUB_IDENT1 bits 7:4)
V3DI_JOBS	= 48		; jobs faits depuis le demarrage
V3DI_BUSY_US	= 56		; temps GPU cumule (us)
V3DI_UIFCFG	= 64		; registres pour Mesa, un par mot de 8 o : UIFCFG, HUB_IDENT1 a 3,
				;   CORE0_IDENT0 a 2 (+64 a +112 ; 0 si le GPU n'est pas pret)

; --- reponse de V3D_OP_HEALTH ------------------------------------------------
V3DH_STATE	= 8		; comme V3DI_STATE
V3DH_TEMP	= 16		; temperature du SoC, en millidegres
V3DH_THROTTLED	= 24		; GET_THROTTLED du firmware : bits 0-3 maintenant, 16-19 depuis le demarrage
V3DH_V3D_HZ	= 32		; frequence du GPU
V3DH_ARM_HZ	= 40		; frequence des coeurs
V3DH_JOBS	= 48		; jobs faits depuis le demarrage
V3DH_RESETS	= 56		; remises a zero depuis le demarrage

; --- V3D_OP_RUN ------------------------------------------------------------------
; Tout est dans UN tampon partage de l'appelant (son « tampon de travail »),
; en decalages depuis son debut. Le service verifie que le tampon est a
; l'appelant et que chaque zone y tient ; les adresses ecrites DANS les
; listes (cible, source, textures, sommets, shaders) sont l'affaire de GpuKit.
V3DR_BUF	= 8		; numero du tampon de travail
V3DR_BCL	= 16		; liste de binning : decalage
V3DR_BCL_LEN	= 24		;   octets (0 : pas de binning)
V3DR_RCL	= 48		; liste de rendu : decalage (+40 / +44 sont au noyau :
V3DR_RCL_LEN	= 56		;   octets              longueur, pid de l'appelant)
V3DR_TALLOC	= 64		; allocation des tuiles : decalage (aligne sur 4 Ko)
V3DR_TALLOC_LEN	= 72		;   octets
V3DR_TSTATE	= 80		; etat des tuiles : decalage (aligne sur 256)
V3DR_TSTATE_LEN	= 88		;   octets (256 par tuile de 64 x 64)
V3DR_US		= 96		; (reponse) duree du job sur le GPU (us)

; V3D_OP_SUBMIT : Mesa chaine ses listes sur plusieurs tampons : V3DR_BCL et V3DR_RCL sont des
; adresses physiques, V3DR_BCL_LEN = fin - debut (modulo 2^32, la fin peut etre ailleurs),
; V3DR_TALLOC et V3DR_TSTATE aussi. V3DR_BUF ne sert pas. Le service ne verifie que le premier Go :
; le contenu des listes n'etait deja pas verifiable (pas de MMU GPU).

V3D_E_ABSENT	= -19		; pas de GPU (E_NODEV) : faire le travail sur le CPU
V3D_E_INVAL	= -22		; tampon pas a l'appelant, zone hors du tampon
V3D_E_LOST	= -5		; le job n'a pas fini : GPU remis a zero, ou eteint apres HANG_MAX blocages de suite
