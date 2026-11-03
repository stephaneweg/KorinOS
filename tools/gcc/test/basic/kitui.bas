' UIKit depuis FreeBASIC (docs/GCC.md) : une fenetre, du texte, un bouton dessines par le kit, sans
' gfxlib2. Un clic, une touche ou la fermeture finit.
#include once "kits/uikit.bi"
#include once "kits/netkit.bi"

dim shared ui as ui_vars
if( uikit_open( @ui ) = 0 ) then
	print "UIKit absent"
	end 1
end if
print "uikit " & UIKIT_KIT_FUNCTIONS & " fonctions, theme " & ui_theme( )

ui_window( 420, 200, kit_str( "UIKit depuis BASIC" ) )
ui_fill( 0, 0, ui.w, ui.h, &hF0F2F5 )
ui_text( kit_str( "Bonjour depuis FreeBASIC" ), 24, 60, &h202830, 20, kit_ptr( @ui.font_b(0) ), 400 )
ui_button( 24, 100, 160, 40, kit_str( "Fermer" ), 1 )
ui_update_all( )

dim as string ip = space( 32 )
if( netkit_kit_open( ) ) then
	fmt_ip( &h0A000202, kit_str( ip ) )
	print "netkit : " & trim( *cast( zstring ptr, strptr( ip ) ) )
end if

dim as integer n = 0
do
	var t = ui_event( 100 )
	if( t = WEV_CLOSE ) then exit do
	if( (t = WEV_MOUSE) andalso (ui_ev_field( ui, EV_KIND ) = 1) ) then exit do
	if( t = WEV_KEY ) then exit do
	n += 1
loop until( n > 100 )
print "KITS BASIC OK"
