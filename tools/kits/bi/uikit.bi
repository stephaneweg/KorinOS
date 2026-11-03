' uikit.bi -- UIKit d'ARMPI depuis FreeBASIC (docs/GCC.md), comme uikit.h pour le C : les fonctions et
' les constantes sont generees depuis uikit.abi (uikit_abi.bi) ; ce fichier ajoute les variables de
' l'interface et des aides.
'
'   #include once "kits/uikit.bi"
'   dim shared ui as ui_vars
'   if( uikit_open( @ui ) = 0 ) then end 1
'   ui_window( 640, 480, kit_str( "Titre" ) )
'   ui_fill( 0, 0, ui.w, ui.h, &hFFFFFF ) : ui_update_all( )
'   while( ui_event( 100 ) <> WEV_CLOSE )
'   wend
'
' Une seule fenetre par programme ; ses pixels (ui.surf, 32 bits 0x00RRGGBB, pas = ui.w * 4) se
' dessinent directement, puis ui_update( x, y, l, h ) les montre.
#pragma once
#include once "uikit_abi.bi"

' les variables de l'interface : la macro ui_data de uikit.inc, champ pour champ
type ui_vars
	win as longint				' poignee de la fenetre
	surf as ulong ptr			' ses pixels
	w as longint				' sa taille
	h as longint
	ev(0 to 31) as ubyte			' le dernier evenement (EV_*)
	font(0 to UI_FONT_SIZE - 1) as ubyte
	font_b(0 to UI_FONT_SIZE - 1) as ubyte
	font_m(0 to UI_FONT_SIZE - 1) as ubyte
end type

' une chaine ou un pointeur, passe a un kit
#define kit_str( s ) cast( longint, strptr( s ) )
#define kit_ptr( p ) cast( longint, p )

' un champ 32 bits de l'evenement (EV_TYPE, EV_KIND, EV_X, EV_Y, EV_CODE, EV_CHAR, EV_MODS, EV_WIN)
private function ui_ev_field( byref v as ui_vars, byval off_ as integer ) as long
	return *cast( long ptr, @v.ev(off_) )
end function

' les kits ouverts (UIKit et ce qu'il prend : AppKit) et ui_init fait -> 1 si les polices sont la
private function uikit_open( byval v as ui_vars ptr ) as integer
	if( uikit_kit_open( ) = 0 ) then return 0
	return ui_init( cast( longint, v ) )
end function
