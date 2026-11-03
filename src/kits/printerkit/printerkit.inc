; printerkit.inc - constantes publiques de PrinterKit (printerkit.abi, docs/IMPRESSION.md)

PRINT_DRAFT	= 3			; qualite
PRINT_NORMAL	= 4
PRINT_HIGH	= 5
PRINT_PORTRAIT	= 0
PRINT_LANDSCAPE	= 1
PRINT_BOLD	= 1			; style d'une police
PRINT_ITALIC	= 2
PRINT_IMG_ALPHA	= 1			; pixels 0xAARRGGBB (sinon 0x00RRGGBB : opaques)
PRINT_DLG_OWN_PAPER = 1			; le document a sa taille (diapositive, PDF) : pas de choix du papier

PRINT_JOB_QUEUED = 0
PRINT_JOB_PREPARING = 1
PRINT_JOB_SENDING = 2
PRINT_JOB_PRINTING = 3
PRINT_JOB_DONE	= 4
PRINT_JOB_FAILED = 5
PRINT_JOB_CANCELED = 6

; PrintSetup (printerkit.h) : ce que l'utilisateur a choisi
PS_SIZE		= 0			; (32 bits) sizeof
PS_PRINTER	= 4			; 64 : son nom (PDF : l'imprimante PDF)
PS_MEDIA	= 68			; 64 : le papier, nom IPP
PS_PAPER_W	= 132			; (32 bits) la page en milliemes de point, orientation appliquee
PS_PAPER_H	= 136
PS_MARGIN_L	= 140			; ce que l'imprimante ne peut imprimer, sur chaque bord
PS_MARGIN_T	= 144
PS_MARGIN_R	= 148
PS_MARGIN_B	= 152
PS_ORIENTATION	= 156
PS_COPIES	= 160
PS_MONO		= 164
PS_QUALITY	= 168
PS_FROM		= 172			; pages a imprimer, depuis 1 ; 0, 0 : toutes
PS_TO		= 176
PS_OUTPUT	= 180			; 256 : l'imprimante PDF : le fichier a ecrire
PS_BYTES	= 500

; PrintDialogInfo
PDI_SIZE	= 0
PDI_TITLE	= 8			; (64 bits) le nom du document
PDI_PAGES	= 16			; nombre de pages (0 : inconnu)
PDI_CURRENT	= 20			; la page montree, depuis 1 (0 : pas de choix de la page courante)
PDI_FLAGS	= 24
PDI_PAPER_W	= 28			; papier du document (0 : celui de l'imprimante)
PDI_PAPER_H	= 32
PDI_BYTES	= 40

; PrintPrinter
PR_NAME		= 0			; 64
PR_KIND		= 64			; 8 : pdf, ipp
PR_URI		= 72			; 160
PR_MODEL	= 232			; 64
PR_COLOR	= 296
PR_COPIES	= 300
PR_DPI		= 304
PR_DEFAULT	= 308
PR_QUALITY	= 312			; bits 3, 4, 5
PR_MEDIA_DEFAULT = 316			; 64
PR_BYTES	= 380

; PrintFound
PF_ADDRESS	= 0			; 32
PF_NAME		= 32			; 64
PF_MODEL	= 96			; 64
PF_USABLE	= 160
PF_BYTES	= 164

; PrintJobInfo
PJI_ID		= 0
PJI_STATE	= 4
PJI_PAGES	= 8
PJI_PAGE	= 12
PJI_PRINTER	= 16			; 64
PJI_TITLE	= 80			; 80
PJI_MESSAGE	= 160			; 96
PJI_BYTES	= 256
