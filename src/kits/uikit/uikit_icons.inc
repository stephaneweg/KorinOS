; ============================================================================
;  kits/uikit_icons.inc - UIKit : icones des applications (docs/DESIGN-ELEGANT.md 9)
;
;  D'abord une image pre-rendue (tools/icons, format plus bas) :
;  SD0:/apps/<nom>.app/icon.rgba (elle part avec le paquet), sinon
;  SD0:/res/icons/<nom>.rgba (applets sans paquet propre). Chargee a la
;  premiere demande, gardee en memoire pour ce programme, a chaque taille
;  demandee ; une application sans image n'est recherchee qu'une fois toutes
;  les IC_RETRY ms (un paquet peut l'installer pendant la session).
;
;  Repli, pour les applications tierces : le dessin par le code, une tuile
;  arrondie de la couleur de l'application, puis un symbole, sur une grille de
;  40 mise a l'echelle. Une application inconnue : son initiale, couleur tiree
;  de son nom.
;
;  Description d'un symbole : operations de 16 octets
;	+0 op (IC_*), +1 x, +2 y, +3 largeur (texte : taille), +4 hauteur
;	(texte : police 0 Sans, 1 Bold, 2 Mono), +5 rayon, +8 couleur,
;	+12 texte : decalage de la chaine depuis l'operation
;  couleurs speciales : IC_C (la tuile), IC_CL (la tuile eclaircie)
; ============================================================================

IC_END		= 0
IC_BOX		= 1			; boite arrondie unie
IC_FILL		= 2			; rectangle
IC_TEXT		= 3			; texte centre dans la tuile
IC_TEXTAT	= 4			; texte : x, ligne de base y
IC_C		= 0x01000000
IC_CL		= 0x02000000

macro ic op*, x*, y*, w*, h*, r*, c*, s {
	db op, x, y, w, h, r, 0, 0
	dw c
  if s eq
	dw 0
  else
	dw s - $ + 12
  end if
}

; entree : nom (16 o), couleur, decalage du symbole depuis l'entree
macro icone nom*, c*, sym* {
local .e
.e:	db nom
	times 16 - ($ - .e) db 0
	dw c, sym - .e
}

		align 4
uk_icones:
	icone 'welcome',	0x004F5BD5, ics_bienvenue
	icone 'calculator',	0x005B6472, ics_calc
	icone 'targets',		0x00E5484D, ics_cibles
	icone 'paint',		0x00F59E0B, ics_dessin
	icone 'editor',	0x006E56CF, ics_editeur
	icone 'files',	0x001BA1B5, ics_fichiers
	icone 'clock',	0x00E8833A, ics_horloge
	icone 'recorder',	0x00D6336C, ics_magneto
	icone 'taskman',	0x0030A46C, ics_moniteur
	icone 'jet',		0x001E88E5, ics_navigateur
	icone 'piano',		0x002B2F36, ics_piano
	icone 'fmtracker',	0x002F7D8C, ics_fmtracker
	icone 'terminal',	0x001B1F24, ics_terminal
	icone 'corbeille',	0x005B6472, ics_corbeille
	icone 'control',	0x005B6472, ics_control
	icone 'theme',		0x008E6BD8, ics_theme
	icone 'clavier',	0x003A4458, ics_clavier
	icone 'ecran',		0x001E88E5, ics_ecran
	icone 'dockconf',	0x0030A46C, ics_dockconf
	icone 'panelconf',	0x0030A46C, ics_dockconf
	icone 'mixer',		0x00495766, ics_mixer
	icone 'sound',		0x00E5484D, ics_son
	icone 'letters',	0x002F74E0, ics_letters
	icone 'sheet',		0x002E9E6B, ics_sheet
	icone 'slides',		0x00E8590C, ics_slides
	icone 'cardfile',	0x00B7791F, ics_cardfile
	icone 'circuits',	0x0030A46C, ics_circuits
	icone 'calendar',	0x00D23A30, ics_calendar
	icone 'notes',		0x00E2B02C, ics_notes
	icone 'archiver',	0x00B7791F, ics_archiver
	icone 'iconedit',	0x00D6336C, ics_iconedit
	icone 'mandelbrot',	0x001E2A5A, ics_fractale
	icone 'disks',		0x005B6472, ics_disques
	icone 'padconf',	0x004F5BD5, ics_manette
	dw 0, 0, 0, 0, 0, 0
UK_ICONE_SIZE	= 24
uk_ic_couleurs:					; couleurs d'une application inconnue
	dw 0x004F5BD5, 0x00E5484D, 0x00F59E0B, 0x006E56CF
	dw 0x001BA1B5, 0x00E8833A, 0x0030A46C, 0x001E88E5

ics_bienvenue:
	ic IC_TEXT, 0, 0, 26, 0, 0, 0x00FFFFFF, uk_s_losange
	ic IC_BOX, 17, 17, 6, 6, 3, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_calc:
	ic IC_BOX, 9, 6, 22, 8, 2, 0x00DCEFE2
	ic IC_BOX, 9, 17, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 17, 17, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 25, 17, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 9, 23, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 17, 23, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 25, 23, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 9, 29, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 17, 29, 6, 5, 1, 0x00FFFFFF
	ic IC_BOX, 25, 29, 6, 5, 1, 0x00F59E0B
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_cibles:
	ic IC_BOX, 6, 6, 28, 28, 14, 0x00FFFFFF
	ic IC_BOX, 10, 10, 20, 20, 10, IC_C
	ic IC_BOX, 14, 14, 12, 12, 6, 0x00FFFFFF
	ic IC_BOX, 17, 17, 6, 6, 3, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_letters:
	ic IC_BOX, 10, 7, 20, 26, 3, 0x00FFFFFF
	ic IC_FILL, 14, 12, 12, 2, 0, IC_C
	ic IC_FILL, 14, 17, 12, 2, 0, IC_C
	ic IC_FILL, 14, 22, 12, 2, 0, IC_C
	ic IC_FILL, 14, 27, 7, 2, 0, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_sheet:
	ic IC_BOX, 8, 9, 24, 22, 3, 0x00FFFFFF
	ic IC_FILL, 8, 15, 24, 1, 0, IC_C
	ic IC_FILL, 8, 22, 24, 1, 0, IC_C
	ic IC_FILL, 16, 9, 1, 22, 0, IC_C
	ic IC_FILL, 24, 9, 1, 22, 0, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_slides:
	ic IC_BOX, 7, 9, 26, 18, 3, 0x00FFFFFF
	ic IC_FILL, 11, 13, 12, 3, 0, IC_C
	ic IC_FILL, 11, 19, 18, 2, 0, IC_CL
	ic IC_FILL, 19, 27, 2, 5, 0, 0x00FFFFFF
	ic IC_FILL, 13, 32, 14, 2, 0, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_cardfile:
	ic IC_BOX, 11, 8, 18, 8, 2, IC_CL
	ic IC_BOX, 7, 13, 26, 19, 3, 0x00FFFFFF
	ic IC_FILL, 11, 18, 11, 2, 0, IC_C
	ic IC_FILL, 11, 23, 18, 2, 0, IC_CL
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_calendar:
	ic IC_BOX, 8, 9, 24, 23, 3, 0x00FFFFFF
	ic IC_FILL, 8, 15, 24, 2, 0, IC_C
	ic IC_FILL, 13, 6, 2, 6, 0, 0x00FFFFFF
	ic IC_FILL, 25, 6, 2, 6, 0, 0x00FFFFFF
	ic IC_BOX, 13, 20, 4, 4, 1, IC_C
	ic IC_BOX, 19, 20, 4, 4, 1, IC_C
	ic IC_BOX, 25, 20, 4, 4, 1, IC_CL
	ic IC_BOX, 13, 26, 4, 3, 1, IC_CL
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_notes:
	ic IC_BOX, 9, 8, 22, 25, 3, 0x00FFFFFF
	ic IC_FILL, 13, 14, 14, 2, 0, IC_C
	ic IC_FILL, 13, 19, 14, 2, 0, IC_C
	ic IC_FILL, 13, 24, 9, 2, 0, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_circuits:
	ic IC_FILL, 6, 14, 10, 2, 0, 0x00FFFFFF
	ic IC_FILL, 6, 24, 10, 2, 0, 0x00FFFFFF
	ic IC_BOX, 13, 10, 16, 20, 8, 0x00FFFFFF
	ic IC_FILL, 13, 10, 8, 20, 0, 0x00FFFFFF
	ic IC_FILL, 29, 19, 6, 2, 0, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_dessin:
	ic IC_BOX, 6, 10, 28, 20, 10, 0x00FFFFFF
	ic IC_BOX, 11, 14, 5, 5, 2, 0x00E5484D
	ic IC_BOX, 18, 12, 5, 5, 2, 0x002F74E0
	ic IC_BOX, 25, 14, 5, 5, 2, 0x0030A46C
	ic IC_BOX, 11, 21, 5, 5, 2, 0x006E56CF
	ic IC_BOX, 22, 21, 6, 6, 3, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_editeur:
	ic IC_BOX, 11, 7, 18, 26, 3, 0x00FFFFFF
	ic IC_FILL, 14, 12, 12, 2, 0, IC_CL
	ic IC_FILL, 14, 16, 12, 2, 0, IC_CL
	ic IC_FILL, 14, 20, 12, 2, 0, IC_CL
	ic IC_FILL, 14, 24, 8, 2, 0, IC_CL
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_fichiers:
	ic IC_BOX, 8, 11, 11, 6, 2, 0x00D7F0F4
	ic IC_BOX, 8, 14, 24, 17, 3, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_horloge:
	ic IC_BOX, 8, 8, 24, 24, 12, 0x00FFFFFF
	ic IC_FILL, 19, 12, 2, 9, 0, 0x001B1F24
	ic IC_FILL, 19, 19, 7, 2, 0, 0x001B1F24
	ic IC_BOX, 18, 18, 4, 4, 2, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_magneto:
	ic IC_BOX, 7, 17, 2, 6, 1, 0x00FFFFFF
	ic IC_BOX, 11, 14, 2, 12, 1, 0x00FFFFFF
	ic IC_BOX, 15, 10, 2, 20, 1, 0x00FFFFFF
	ic IC_BOX, 19, 13, 2, 14, 1, 0x00FFFFFF
	ic IC_BOX, 23, 11, 2, 18, 1, 0x00FFFFFF
	ic IC_BOX, 27, 16, 2, 8, 1, 0x00FFFFFF
	ic IC_BOX, 31, 18, 2, 4, 1, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_archiver:
	ic IC_BOX, 8, 12, 24, 20, 3, 0x00FFFFFF
	ic IC_BOX, 6, 9, 28, 7, 2, 0x00F1E2BE
	ic IC_FILL, 16, 19, 8, 3, 0, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_iconedit:
	ic IC_FILL, 8, 8, 7, 7, 0, 0x00FFFFFF
	ic IC_FILL, 16, 8, 7, 7, 0, 0x00FFD43B
	ic IC_FILL, 24, 8, 7, 7, 0, 0x00FFFFFF
	ic IC_FILL, 8, 16, 7, 7, 0, 0x0074C0FC
	ic IC_FILL, 16, 16, 7, 7, 0, 0x00FFFFFF
	ic IC_FILL, 24, 16, 7, 7, 0, 0x0074C0FC
	ic IC_FILL, 8, 24, 7, 7, 0, 0x00FFFFFF
	ic IC_FILL, 16, 24, 7, 7, 0, 0x00FFD43B
	ic IC_FILL, 24, 24, 7, 7, 0, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_fractale:
	ic IC_BOX, 15, 9, 20, 22, 10, 0x0000000F
	ic IC_BOX, 7, 15, 10, 10, 5, 0x0000000F
	ic IC_BOX, 4, 19, 4, 2, 1, 0x00FFB000
	ic IC_BOX, 13, 8, 5, 5, 2, 0x00FFB000
	ic IC_BOX, 13, 27, 5, 5, 2, 0x0074C0FC
	ic IC_BOX, 28, 19, 5, 3, 1, 0x001E2A5A
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_disques:
	ic IC_BOX, 7, 10, 26, 20, 3, 0x00FFFFFF
	ic IC_FILL, 10, 22, 20, 1, 0, IC_C
	ic IC_BOX, 25, 25, 4, 3, 1, 0x0040C057
	ic IC_BOX, 12, 13, 16, 6, 2, 0x00C8D0DA
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_manette:
	ic IC_BOX, 5, 13, 30, 15, 7, 0x00FFFFFF
	ic IC_FILL, 12, 16, 3, 9, 0, IC_C
	ic IC_FILL, 9, 19, 9, 3, 0, IC_C
	ic IC_BOX, 25, 16, 4, 4, 2, 0x00E5484D
	ic IC_BOX, 28, 21, 4, 4, 2, 0x002E9E6B
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_moniteur:
	ic IC_BOX, 6, 9, 28, 20, 3, 0x000F2E1F
	ic IC_FILL, 9, 19, 6, 2, 0, 0x009BF0BF	; pouls : plat, pic, creux,
	ic IC_FILL, 15, 12, 2, 9, 0, 0x009BF0BF	; bosse, plat
	ic IC_FILL, 17, 12, 2, 2, 0, 0x009BF0BF
	ic IC_FILL, 19, 12, 2, 13, 0, 0x009BF0BF
	ic IC_FILL, 21, 23, 2, 2, 0, 0x009BF0BF
	ic IC_FILL, 23, 17, 2, 8, 0, 0x009BF0BF
	ic IC_FILL, 25, 17, 2, 2, 0, 0x009BF0BF
	ic IC_FILL, 27, 17, 2, 4, 0, 0x009BF0BF
	ic IC_FILL, 27, 19, 5, 2, 0, 0x009BF0BF
	ic IC_FILL, 17, 29, 6, 3, 0, 0x00FFFFFF
	ic IC_BOX, 13, 31, 14, 2, 1, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_navigateur:
	ic IC_BOX, 7, 7, 26, 26, 13, 0x00FFFFFF
	ic IC_BOX, 14, 7, 12, 26, 6, IC_C
	ic IC_BOX, 16, 9, 8, 22, 4, 0x00FFFFFF
	ic IC_FILL, 7, 19, 26, 2, 0, IC_C
	ic IC_FILL, 19, 7, 2, 26, 0, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_mixer:
	ic IC_BOX, 10, 8, 2, 24, 1, 0x00FFFFFF
	ic IC_BOX, 19, 8, 2, 24, 1, 0x00FFFFFF
	ic IC_BOX, 28, 8, 2, 24, 1, 0x00FFFFFF
	ic IC_BOX, 7, 20, 8, 5, 2, 0x00FFFFFF
	ic IC_BOX, 16, 12, 8, 5, 2, 0x00FFFFFF
	ic IC_BOX, 25, 24, 8, 5, 2, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_son:
	ic IC_BOX, 8, 16, 6, 8, 1, 0x00FFFFFF
	ic IC_FILL, 14, 13, 2, 14, 0, 0x00FFFFFF
	ic IC_FILL, 16, 11, 2, 18, 0, 0x00FFFFFF
	ic IC_FILL, 18, 9, 2, 22, 0, 0x00FFFFFF
	ic IC_BOX, 23, 15, 2, 10, 1, 0x00FFFFFF
	ic IC_BOX, 28, 11, 2, 18, 1, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_piano:
	ic IC_BOX, 7, 10, 6, 20, 1, 0x00FFFFFF
	ic IC_BOX, 14, 10, 6, 20, 1, 0x00FFFFFF
	ic IC_BOX, 21, 10, 6, 20, 1, 0x00FFFFFF
	ic IC_BOX, 28, 10, 6, 20, 1, 0x00FFFFFF
	ic IC_FILL, 11, 10, 4, 12, 0, 0x00111317
	ic IC_FILL, 18, 10, 4, 12, 0, 0x00111317
	ic IC_FILL, 25, 10, 4, 12, 0, 0x00111317
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_fmtracker:					; grille de notes, ligne jouee
	ic IC_BOX, 7, 7, 26, 26, 3, 0x001B1F24
	ic IC_FILL, 10, 11, 8, 2, 0, 0x00FFFFFF
	ic IC_FILL, 22, 11, 8, 2, 0, IC_CL
	ic IC_FILL, 10, 15, 8, 2, 0, IC_CL
	ic IC_FILL, 8, 19, 24, 4, 0, 0x0062B87A
	ic IC_FILL, 22, 23, 8, 2, 0, 0x00FFFFFF
	ic IC_FILL, 10, 27, 8, 2, 0, 0x00FFFFFF
	ic IC_FILL, 22, 27, 8, 2, 0, IC_CL
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_terminal:
	ic IC_TEXTAT, 9, 26, 16, 2, 0, 0x007CE38B, uk_s_invite
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_corbeille:					; (glyphe d'Onyx)
	ic IC_BOX, 15, 5, 10, 5, 2, 0x00FFFFFF
	ic IC_BOX, 6, 9, 28, 4, 2, 0x00FFFFFF
	ic IC_BOX, 9, 15, 22, 21, 4, 0x00FFFFFF
	ic IC_FILL, 14, 19, 2, 13, 0, IC_C
	ic IC_FILL, 19, 19, 2, 13, 0, IC_C
	ic IC_FILL, 24, 19, 2, 13, 0, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_control:					; engrenage
	ic IC_TEXT, 0, 0, 28, 0, 0, 0x00FFFFFF, uk_s_engrenage
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_theme:					; trois couleurs
	ic IC_BOX, 7, 9, 15, 15, 7, 0x00E5484D
	ic IC_BOX, 18, 9, 15, 15, 7, 0x002F74E0
	ic IC_BOX, 12, 18, 15, 15, 7, 0x00F5B33B
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_clavier:					; clavier
	ic IC_BOX, 5, 11, 30, 19, 3, 0x00FFFFFF
	ic IC_FILL, 8, 14, 4, 3, 0, IC_C
	ic IC_FILL, 14, 14, 4, 3, 0, IC_C
	ic IC_FILL, 20, 14, 4, 3, 0, IC_C
	ic IC_FILL, 26, 14, 4, 3, 0, IC_C
	ic IC_FILL, 11, 19, 4, 3, 0, IC_C
	ic IC_FILL, 17, 19, 4, 3, 0, IC_C
	ic IC_FILL, 23, 19, 4, 3, 0, IC_C
	ic IC_FILL, 12, 24, 16, 3, 0, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_ecran:					; moniteur
	ic IC_BOX, 6, 8, 28, 20, 3, 0x00FFFFFF
	ic IC_BOX, 8, 10, 24, 16, 1, IC_CL
	ic IC_FILL, 18, 28, 4, 3, 0, 0x00FFFFFF
	ic IC_BOX, 13, 31, 14, 2, 1, 0x00FFFFFF
	ic IC_END, 0, 0, 0, 0, 0, 0
ics_dockconf:					; dock et ses lanceurs
	ic IC_BOX, 6, 10, 28, 12, 3, IC_CL
	ic IC_BOX, 4, 24, 32, 10, 3, 0x00FFFFFF
	ic IC_BOX, 9, 26, 6, 6, 2, IC_C
	ic IC_BOX, 17, 26, 6, 6, 2, IC_C
	ic IC_BOX, 25, 26, 6, 6, 2, IC_C
	ic IC_END, 0, 0, 0, 0, 0, 0
uk_s_engrenage	db 0xE2,0x9A,0x99,0		; U+2699
uk_s_losange	db 0xE2,0x97,0x86,0		; U+25C6
uk_s_invite	db '>_',0
		align 4

; ---------------------------------------------------------------------------
;  Icones en images
;
;  Fichier (tools/icons/build.py) : +0 'ARIC', +4 version 16 bits, +6 n 16 bits,
;  +8 n entrees { cote 16 bits, 0 16 bits, decalage 32 bits } ; pixels :
;  mots 0xAARRGGBB, alpha premultiplie, de haut en bas. Seule la taille utile
;  est lue. Une taille absente du fichier : la plus proche au-dessus, reduite
;  une fois (bilineaire) ; elles sont assez serrees pour cela.
;
;  Cache : une liste par programme (uk_ic_list). Entree ICE_SIZE 0 : pas
;  d'image pour ce nom, quelle que soit la taille ; -1 : entree morte.
; ---------------------------------------------------------------------------

ICF_MAGIC	= 0x43495241			; 'ARIC'
ICF_MAX		= 16				; tailles lues dans un fichier
ICF_HDR		= 8 + 8 * ICF_MAX
IC_NAME_MAX	= 31
IC_SIZE_MAX	= 512
IC_RETRY	= 30000				; ms
ICE_NEXT	= 0
ICE_SIZE	= 8
ICE_TIME	= 16
ICE_NAME	= 24
ICE_PIX		= 24 + IC_NAME_MAX + 1

; x0 = destination, x1 = chaine -> x0 = son NUL dans la destination (feuille)
uk_ic_cat:
	ldrb	w2,[x1],1
	strb	w2,[x0]
	cbz	w2,.ret
	add	x0,x0,1
	b	uk_ic_cat
.ret:	ret

; x0 = nom -> x0 = fichier de son icone ouvert (< 0 : aucun)
uk_ic_open:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_ic_path
	adr	x1,uk_ic_s_apps
	bl	uk_ic_cat
	mov	x1,x19
	bl	uk_ic_cat
	adr	x1,uk_ic_s_bundle
	bl	uk_ic_cat
	adr	x0,uk_ic_path
	mov	x1,O_READ
	sys	SYS_OPEN
	tbz	x0,63,.out
	adr	x0,uk_ic_path
	adr	x1,uk_ic_s_res
	bl	uk_ic_cat
	mov	x1,x19
	bl	uk_ic_cat
	adr	x1,uk_ic_s_ext
	bl	uk_ic_cat
	adr	x0,uk_ic_path
	mov	x1,O_READ
	sys	SYS_OPEN
.out:	ldr	x19,[sp],16
	epilogue

; w0, w1 = pixels, x2 = 0..256 (part de w1) -> w0 (feuille ; touche w3 a w6)
uk_ic_lerp:
	movl	x6,0x00FF00FF
	mov	w3,256
	sub	w3,w3,w2
	and	w4,w0,w6
	mul	w4,w4,w3
	and	w5,w1,w6
	mul	w5,w5,w2
	add	w4,w4,w5
	lsr	w4,w4,8
	and	w4,w4,w6
	lsr	w0,w0,8
	and	w0,w0,w6
	mul	w0,w0,w3
	lsr	w1,w1,8
	and	w1,w1,w6
	mul	w1,w1,w2
	add	w0,w0,w1
	bic	w0,w0,w6
	orr	w0,w0,w4
	ret

; x0 = position p sur le cote n, x1 = cote z de la source, x2 = n ->
; x0 = pixel de la source, x1 = pixel suivant, x2 = part du suivant (0..255) :
; centre du pixel p (feuille)
uk_ic_pos:
	lsl	x0,x0,1
	add	x0,x0,1
	mul	x0,x0,x1
	lsl	x0,x0,7
	udiv	x0,x0,x2
	subs	x0,x0,128
	csel	x0,xzr,x0,lo
	and	x2,x0,255
	lsr	x0,x0,8
	sub	x3,x1,1
	cmp	x0,x3
	csel	x0,x3,x0,hi
	add	x1,x0,1
	cmp	x1,x3
	csel	x1,x3,x1,hi
	ret

; x0 = source, x1 = son cote, x2 = destination, x3 = son cote : mise a l'echelle
; bilineaire (pixels premultiplies)
uk_ic_scale:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,0				; ligne
.y:	cmp	x23,x22
	b.hs	.out
	mov	x0,x23
	mov	x1,x20
	mov	x2,x22
	bl	uk_ic_pos
	mov	x24,x2				; part de la ligne suivante
	mul	x0,x0,x20
	add	x25,x19,x0,lsl 2		; ligne, ligne suivante
	mul	x1,x1,x20
	add	x26,x19,x1,lsl 2
	mov	x27,0				; colonne
.x:	cmp	x27,x22
	b.hs	.ny
	mov	x0,x27
	mov	x1,x20
	mov	x2,x22
	bl	uk_ic_pos
	mov	x7,x0
	mov	x8,x1
	mov	x28,x2
	ldr	w0,[x25,x7,lsl 2]
	ldr	w1,[x25,x8,lsl 2]
	bl	uk_ic_lerp
	mov	w9,w0
	ldr	w0,[x26,x7,lsl 2]
	ldr	w1,[x26,x8,lsl 2]
	mov	x2,x28
	bl	uk_ic_lerp
	mov	w1,w0
	mov	w0,w9
	mov	x2,x24
	bl	uk_ic_lerp
	str	w0,[x21],4
	add	x27,x27,1
	b	.x
.ny:	add	x23,x23,1
	b	.y
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom, x1 = taille -> x0 = entree du cache avec ses pixels (0 : pas d'image)
uk_ic_get:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	str	x27,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	sub	x0,x20,1
	cmp	x0,IC_SIZE_MAX
	b.hs	.none
	mov	x0,x19
	bl	uk_strlen
	sub	x0,x0,1
	cmp	x0,IC_NAME_MAX
	b.hs	.none
	mov	x22,0				; entree « pas d'image » de ce nom
	adr	x21,uk_ic_list
.scan:	ldr	x21,[x21,ICE_NEXT]
	cbz	x21,.load
	ldr	x0,[x21,ICE_SIZE]
	cbz	x0,.neg
	cmp	x0,x20
	b.ne	.scan
	mov	x0,x19
	add	x1,x21,ICE_NAME
	bl	uk_str_eq
	cbz	x0,.scan
	mov	x0,x21
	b	.out
.neg:	mov	x0,x19
	add	x1,x21,ICE_NAME
	bl	uk_str_eq
	cbz	x0,.scan
	sys	SYS_TIME_MS
	ldr	x1,[x21,ICE_TIME]
	sub	x1,x0,x1
	mov	x2,IC_RETRY
	cmp	x1,x2
	b.lo	.none
	str	x0,[x21,ICE_TIME]		; (on regarde a nouveau)
	mov	x22,x21
.load:	bl	uk_kits
	mov	x0,x19
	bl	uk_ic_open
	tbnz	x0,63,.absent
	mov	x23,x0
	adr	x1,uk_ic_hdr
	mov	x2,ICF_HDR
	sys	SYS_READ
	cmp	x0,8
	b.lt	.close
	mov	x3,x0				; octets lus
	adr	x9,uk_ic_hdr
	ldr	w0,[x9]
	movl	x1,ICF_MAGIC
	cmp	w0,w1
	b.ne	.close
	ldrh	w0,[x9,6]			; n
	cbz	w0,.close
	cmp	w0,ICF_MAX
	b.hi	.close
	add	x1,x9,8
	add	x0,x1,x0,lsl 3			; fin des entrees
	sub	x2,x0,x9
	cmp	x2,x3
	b.hi	.close
	mov	x24,0				; x24 = cote, x25 = decalage
	mov	x25,0
.pick:	cmp	x1,x0				; la plus petite >= taille, sinon la plus
	b.hs	.picked				; grande
	ldrh	w2,[x1]
	ldr	w3,[x1,4]
	add	x1,x1,8
	cbz	x24,.take
	cmp	x24,x20
	b.hs	.above
	cmp	x2,x24				; (la retenue est trop petite)
	b.ls	.pick
	b	.take
.above:	cmp	x2,x20
	b.lo	.pick
	cmp	x2,x24
	b.hs	.pick
.take:	mov	x24,x2
	mov	x25,x3
	b	.pick
.picked:
	cbz	x24,.close
	mul	x0,x20,x20			; l'entree et ses pixels
	lsl	x0,x0,2
	add	x0,x0,ICE_PIX
	bl	uk_alloc
	cbz	x0,.close
	mov	x27,x0
	add	x26,x27,ICE_PIX
	cmp	x24,x20				; a la bonne taille : lue en place
	b.eq	.read
	mul	x0,x24,x24
	lsl	x0,x0,2
	bl	uk_alloc
	cbz	x0,.drop
	mov	x26,x0
.read:	mov	x0,x23
	mov	x1,x25
	mov	x2,0
	sys	SYS_SEEK
	tbnz	x0,63,.bad
	mul	x25,x24,x24
	lsl	x25,x25,2
	mov	x0,x23
	mov	x1,x26
	mov	x2,x25
	sys	SYS_READ
	cmp	x0,x25
	b.ne	.bad
	mov	x0,x23
	sys	SYS_CLOSE
	cmp	x24,x20
	b.eq	.link
	mov	x0,x26
	mov	x1,x24
	add	x2,x27,ICE_PIX
	mov	x3,x20
	bl	uk_ic_scale
	mov	x0,x26
	bl	uk_free
.link:	str	x20,[x27,ICE_SIZE]
	add	x0,x27,ICE_NAME
	mov	x1,x19
	bl	uk_ic_cat
	adr	x0,uk_ic_list
	ldr	x1,[x0]
	str	x1,[x27,ICE_NEXT]
	str	x27,[x0]
	cbz	x22,.ok
	mov	x0,-1				; elle a fini par arriver
	str	x0,[x22,ICE_SIZE]
.ok:	mov	x0,x27
	b	.out
.bad:	add	x0,x27,ICE_PIX
	cmp	x0,x26
	b.eq	.drop
	mov	x0,x26
	bl	uk_free
.drop:	mov	x0,x27
	bl	uk_free
.close:	mov	x0,x23
	sys	SYS_CLOSE
.absent:
	cbnz	x22,.none			; (deja notee)
	mov	x0,ICE_PIX
	bl	uk_alloc
	cbz	x0,.none
	mov	x21,x0
	add	x0,x21,ICE_NAME
	mov	x1,x19
	bl	uk_ic_cat
	sys	SYS_TIME_MS
	str	x0,[x21,ICE_TIME]
	adr	x0,uk_ic_list
	ldr	x1,[x0]
	str	x1,[x21,ICE_NEXT]
	str	x21,[x0]
.none:	mov	x0,0
.out:	ldr	x27,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pixels premultiplies, x1 = x, x2 = y, x3 = cote, x4 = 0, ou bit 32 et
; couleur : l'image ne donne que l'alpha (glyphe). Compose dans la
; surface, decoupee a ui_clip. Fond opaque : s + d * (255 - a) / 255. Fenetre
; WF_ALPHA dont le fond est transparent (octet haut : 0 opaque) : les deux
; couvertures s'ajoutent, le resultat reste en alpha droit pour Elegant.
uk_ic_blit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	adr	x0,uk_vars			; bornes : surface, decoupe
	ldr	x0,[x0]
	ldp	x24,x25,[x0,UV_W]		; x24, x25 = bord droit, bas
	mov	x26,0				; x26, x27 = bord gauche, haut
	mov	x27,0
	adr	x1,uk_clip_on
	ldr	x1,[x1]
	cbz	x1,.b
	adr	x1,uk_clip
	ldp	x26,x27,[x1]
	ldp	x2,x3,[x1,16]
	cmp	x2,x24
	csel	x24,x2,x24,lt
	cmp	x3,x25
	csel	x25,x3,x25,lt
.b:	cmp	x27,x21
	csel	x27,x21,x27,lt
	add	x1,x21,x22
	cmp	x25,x1
	csel	x25,x1,x25,gt
	cmp	x26,x20
	csel	x26,x20,x26,lt
	add	x1,x20,x22
	cmp	x24,x1
	csel	x24,x1,x24,gt
	ldr	x16,[x0,UV_W]
	ldr	x17,[x0,UV_SURF]
	adr	x0,uk_flags
	ldr	x0,[x0]
	mov	w15,0				; w15 : masque de la transparence du fond
	tst	x0,WF_ALPHA
	b.eq	.c
	mov	w15,255
.c:	movl	x14,0x00FF00FF
	movl	x13,0x00800080
.y:	cmp	x27,x25
	b.ge	.out
	sub	x0,x27,x21
	mul	x0,x0,x22
	add	x28,x19,x0,lsl 2		; x28 = ligne source
	mul	x1,x27,x16
	add	x2,x17,x1,lsl 2			; x2 = ligne destination
	mov	x3,x26
.x:	cmp	x3,x24
	b.ge	.ny
	sub	x4,x3,x20
	ldr	w5,[x28,x4,lsl 2]
	lsr	w6,w5,24			; alpha
	cbz	w6,.nx
	cbz	x23,.t
	and	w7,w23,w14			; glyphe : la couleur, premultipliee
	mul	w7,w7,w6
	add	w7,w7,w13
	lsr	w8,w7,8
	and	w8,w8,w14
	add	w7,w7,w8
	lsr	w7,w7,8
	and	w7,w7,w14
	ubfx	w8,w23,8,8
	mul	w8,w8,w6
	add	w8,w8,128
	add	w8,w8,w8,lsr 8
	lsr	w8,w8,8
	orr	w5,w7,w8,lsl 8
	orr	w5,w5,w6,lsl 24
.t:	cmp	w6,255
	b.ne	.blend
	and	w5,w5,0x00FFFFFF
	str	w5,[x2,x3,lsl 2]
	b	.nx
.blend:	ldr	w7,[x2,x3,lsl 2]
	lsr	w8,w7,24
	and	w8,w8,w15
	cbnz	w8,.clear
	mov	w9,255				; fond opaque, canaux deux par deux
	sub	w9,w9,w6
	and	w10,w7,w14
	mul	w10,w10,w9
	add	w10,w10,w13
	lsr	w11,w10,8
	and	w11,w11,w14
	add	w10,w10,w11
	lsr	w10,w10,8
	and	w10,w10,w14
	ubfx	w11,w7,8,8
	mul	w11,w11,w9
	add	w11,w11,128
	add	w11,w11,w11,lsr 8
	lsr	w11,w11,8
	and	w5,w5,0x00FFFFFF
	add	w5,w5,w10
	add	w5,w5,w11,lsl 8
	str	w5,[x2,x3,lsl 2]
	b	.nx
.clear:	mov	w9,255				; fond transparent
	sub	w9,w9,w8
	mov	w10,255
	sub	w8,w10,w6
	mul	w11,w9,w8
	add	w11,w11,127
	udiv	w11,w11,w10			; w11 : ce qui reste du fond
	add	w12,w6,w11			; w12 : couverture du resultat
	sub	w4,w10,w12
	lsl	w4,w4,24
	mov	w8,0
.ch:	lsrv	w9,w5,w8
	and	w9,w9,0xFF
	mul	w9,w9,w10
	lsrv	w0,w7,w8
	and	w0,w0,0xFF
	madd	w9,w0,w11,w9
	udiv	w9,w9,w12
	cmp	w9,255
	csel	w9,w10,w9,hi
	lslv	w9,w9,w8
	orr	w4,w4,w9
	add	w8,w8,8
	cmp	w8,24
	b.lo	.ch
	str	w4,[x2,x3,lsl 2]
.nx:	add	x3,x3,1
	b	.x
.ny:	add	x27,x27,1
	b	.y
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom du glyphe (« glyph-power »...), x1 = x, x2 = y, x3 = cote, w4 = couleur :
; un bouton d'interface, tools/icons le rend en masque, le jeton du theme le
; colorie ici. Fichier absent : un point de cette couleur, le bouton se voit
ui_glyph:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	mov	w22,w4
	and	x22,x22,0x00FFFFFF
	mov	x1,x21
	bl	uk_ic_get
	cbz	x0,.dot
	add	x0,x0,ICE_PIX
	mov	x1,x19
	mov	x2,x20
	mov	x3,x21
	mov	x4,1
	orr	x4,x22,x4,lsl 32
	bl	uk_ic_blit
	b	.out
.dot:	lsr	x4,x21,2
	add	x0,x19,x4
	add	x1,x20,x4
	lsr	x2,x21,1
	mov	x3,x2
	mov	w5,w22
	mov	w6,w22
	bl	ui_rbox
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

uk_ic_s_apps	db 'SD0:/apps/',0
uk_ic_s_bundle	db '.app/icon.rgba',0
uk_ic_s_res	db 'SD0:/res/icons/',0
uk_ic_s_ext	db '.rgba',0
		align 4

; x0 = valeur sur la grille de 40 -> x0 = pixels (taille de l'icone : uk_ic_t)
uk_ic_s:
	adr	x1,uk_ic_t
	ldr	x1,[x1]
	mul	x0,x0,x1
	add	x0,x0,20
	mov	x1,40
	udiv	x0,x0,x1
	ret

; x0 = nom de l'application, x1 = x, x2 = y, x3 = taille (px) : dessine son
; icone dans la surface courante
ui_icone:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,32
	mov	x19,x0
	adr	x9,uk_ic_t
	stp	x3,x1,[x9]			; taille, x
	str	x2,[x9,16]			; y
	mov	x21,x3
	mov	x0,x19				; son image, si elle en a une
	mov	x1,x21
	bl	uk_ic_get
	cbz	x0,.code
	add	x0,x0,ICE_PIX
	adr	x9,uk_ic_t
	ldp	x3,x1,[x9]
	ldr	x2,[x9,16]
	mov	x4,0
	bl	uk_ic_blit
	b	.out
.code:	adr	x20,uk_icones			; repli : l'icone de ce nom, par le code
.find:	ldrb	w0,[x20]
	cbz	w0,.unknown
	mov	x0,x19
	mov	x1,x20
	bl	uk_str_eq
	cbnz	x0,.found
	add	x20,x20,UK_ICONE_SIZE
	b	.find
.unknown:					; initiale, couleur tiree du nom
	mov	x0,0
	mov	x1,x19
.h:	ldrb	w2,[x1],1
	cbz	w2,.hd
	add	x0,x0,x2
	b	.h
.hd:	and	x0,x0,7
	adr	x1,uk_ic_couleurs
	ldr	w22,[x1,x0,lsl 2]
	mov	x23,0				; pas de symbole
	b	.tile
.found:	ldr	w22,[x20,16]			; couleur
	ldr	w0,[x20,20]
	add	x23,x20,x0			; symbole
.tile:	mov	w0,w22				; tuile : degrade, reflet en haut
	mov	x1,116
	bl	ui_tone
	str	x0,[sp]
	mov	w0,w22
	mov	x1,140
	bl	ui_tone
	mov	w5,w0
	ldr	x6,[sp]
	adr	x9,uk_ic_t
	ldp	x3,x0,[x9]
	ldr	x1,[x9,16]
	mov	x2,x3
	lsr	x4,x3,2
	bl	ui_rbox
	mov	w0,w22
	mov	x1,140
	bl	ui_tone
	mov	w1,0x00FFFFFF
	mov	x2,90
	bl	ui_mix
	mov	w4,w0
	adr	x9,uk_ic_t
	ldp	x2,x0,[x9]
	ldr	x1,[x9,16]
	lsr	x3,x2,2				; (rayon)
	add	x0,x0,x3
	add	x1,x1,1
	sub	x2,x2,x3,lsl 1
	mov	x3,1
	bl	ui_fill
	cbz	x23,.initial
.op:	ldrb	w24,[x23]			; --- symbole
	cbz	w24,.out
	ldr	w25,[x23,8]			; couleur
	lsr	w0,w25,24			; couleurs speciales : octet haut 1, 2
	cmp	w0,1
	b.ne	.cl
	mov	w25,w22
.cl:	cmp	w0,2
	b.ne	.go
	mov	w0,w22
	mov	w1,0x00FFFFFF
	mov	x2,150
	bl	ui_mix
	mov	w25,w0
.go:	cmp	w24,IC_TEXT
	b.hs	.text
	ldrb	w0,[x23,1]			; boite ou rectangle, mis a l'echelle :
	bl	uk_ic_s				; x [sp], y x26, largeur [sp,8],
	adr	x9,uk_ic_t			; hauteur [sp,16]
	ldr	x1,[x9,8]
	add	x0,x0,x1
	str	x0,[sp]
	ldrb	w0,[x23,2]
	bl	uk_ic_s
	adr	x9,uk_ic_t
	ldr	x1,[x9,16]
	add	x26,x0,x1
	ldrb	w0,[x23,3]
	bl	uk_ic_s
	cmp	x0,1
	csinc	x0,x0,xzr,hs
	str	x0,[sp,8]
	ldrb	w0,[x23,4]
	bl	uk_ic_s
	cmp	x0,1
	csinc	x0,x0,xzr,hs
	str	x0,[sp,16]
	cmp	w24,IC_BOX
	b.ne	.fill
	ldrb	w0,[x23,5]
	bl	uk_ic_s
	mov	x4,x0
	ldr	x0,[sp]
	mov	x1,x26
	ldp	x2,x3,[sp,8]
	mov	w5,w25
	mov	w6,w25
	bl	ui_rbox
	b	.next
.fill:	ldr	x0,[sp]
	mov	x1,x26
	ldp	x2,x3,[sp,8]
	mov	w4,w25
	bl	ui_fill
	b	.next
.text:	ldrsw	x0,[x23,12]			; texte
	add	x26,x23,x0
	ldrb	w0,[x23,3]			; taille
	bl	uk_ic_s
	str	x0,[sp]
	ldrb	w0,[x23,4]			; police
	bl	uk_ic_font
	str	x0,[sp,8]
	cmp	w24,IC_TEXTAT
	b.eq	.at
	mov	x0,x26				; centre dans la tuile
	ldr	x1,[sp]
	ldr	x2,[sp,8]
	bl	ui_text_width
	adr	x9,uk_ic_t
	ldp	x2,x1,[x9]			; taille, x
	sub	x3,x2,x0
	add	x1,x1,x3,asr 1
	ldr	x4,[sp]				; ligne de base : milieu + 0,36 taille
	mov	x3,36
	mul	x3,x3,x4
	mov	x5,100
	udiv	x3,x3,x5
	ldr	x5,[x9,16]
	add	x5,x5,x2,lsr 1
	add	x2,x5,x3
	b	.draw
.at:	ldrb	w0,[x23,1]
	bl	uk_ic_s
	adr	x9,uk_ic_t
	ldr	x1,[x9,8]
	add	x1,x0,x1
	str	x1,[sp,-16]!
	ldrb	w0,[x23,2]
	bl	uk_ic_s
	adr	x9,uk_ic_t
	ldr	x2,[x9,16]
	add	x2,x0,x2
	ldr	x1,[sp],16
.draw:	mov	x0,x26
	mov	w3,w25
	ldr	x4,[sp]
	ldr	x5,[sp,8]
	mov	x6,0
	bl	ui_text
.next:	add	x23,x23,16
	b	.op
.initial:					; --- application inconnue : initiale
	ldrb	w0,[x19]
	sub	w1,w0,'a'
	cmp	w1,25
	b.hi	.cap
	sub	w0,w0,32
.cap:	adr	x1,uk_ic_lettre
	strb	w0,[x1]
	strb	wzr,[x1,1]
	adr	x26,uk_ic_lettre
	mov	x0,20
	bl	uk_ic_s
	str	x0,[sp]
	mov	x0,1
	bl	uk_ic_font
	str	x0,[sp,8]
	mov	w25,0x00FFFFFF
	mov	x0,x26
	ldr	x1,[sp]
	ldr	x2,[sp,8]
	bl	ui_text_width
	adr	x9,uk_ic_t
	ldp	x2,x1,[x9]
	sub	x3,x2,x0
	add	x1,x1,x3,asr 1
	ldr	x4,[sp]
	mov	x3,36
	mul	x3,x3,x4
	mov	x5,100
	udiv	x3,x3,x5
	ldr	x5,[x9,16]
	add	x5,x5,x2,lsr 1
	add	x2,x5,x3
	mov	x0,x26
	mov	w3,w25
	ldr	x5,[sp,8]
	mov	x6,0
	bl	ui_text
.out:	add	sp,sp,32
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = 0 Sans, 1 Bold, 2 Mono -> x0 = police du programme
uk_ic_font:
	adr	x1,uk_vars
	ldr	x1,[x1]
	mov	x2,UI_FONT_SIZE
	madd	x0,x0,x2,x1
	add	x0,x0,UV_FONT
	ret
