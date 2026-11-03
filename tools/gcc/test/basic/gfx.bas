' Essai graphique de FreeBASIC sous ARMPI (docs/GCC.md) : gfx 13 (SCREEN 13, palette), gfx 32 (ScreenRes, 32 bits).
' Dessine, puis note clavier et souris dans la fenetre et dans /tmp/gfx.log ; Echap ou la case de fermeture finit.
#include once "fbgfx.bi"

dim as integer mode32 = (command( 1 ) = "32")
var lg = freefile( )
open "/tmp/gfx.log" for output as #lg

if( mode32 ) then
	screenres 640, 480, 32
	windowtitle "gfx 32 bits"
	for y as integer = 0 to 479
		line (0, y)-(639, y), rgb( y * 255 \ 479, 64, 255 - y * 255 \ 479 )
	next
	circle (320, 240), 150, rgb( 255, 255, 0 ), , , , f
	line (40, 40)-(240, 140), rgb( 255, 255, 255 ), bf
	draw string (60, 80), "FreeBASIC 32 bits", rgb( 0, 0, 0 )
else
	screen 13
	windowtitle "gfx SCREEN 13"
	' la palette VGA par defaut, puis un degrade a nous
	for i as integer = 0 to 255
		line (i + 32, 8)-(i + 32, 28), i
	next
	for i as integer = 0 to 63
		palette 192 + i, i * 4, 0, 255 - i * 4
	next
	for i as integer = 0 to 63
		line (i * 2 + 96, 36)-(i * 2 + 97, 56), 192 + i
	next
	line (10, 70)-(150, 180), 14, b
	line (20, 80)-(140, 170), 4, bf
	circle (240, 125), 50, 10
	paint (240, 125), 2, 10
	for i as integer = 0 to 300
		pset (160 + i \ 10, 60 + (i mod 20)), 15
	next
	locate 24, 2
	print "FreeBASIC SCREEN 13";
end if

screenlock
screenunlock
print #lg, "PRET"
close #lg

dim as integer mx, my, mz, mb, omx = -1, omy = -1, omb = 0, keys = 0
dim as double t0 = timer
dim as string k
var ly = 0
do
	if( getmouse( mx, my, mz, mb ) = 0 ) then
		if( (mx <> omx) or (my <> omy) or (mb <> omb) ) then
			pset (mx, my), iif( mode32, rgb( 255, 255, 255 ), 15 )
			if( mb and 1 ) then
				circle (mx, my), 6, iif( mode32, rgb( 255, 0, 0 ), 12 )
			end if
			open "/tmp/gfx.log" for append as #lg
			print #lg, "SOURIS " & mx & " " & my & " " & mb
			close #lg
			omx = mx : omy = my : omb = mb
		end if
	end if
	k = inkey( )
	if( len( k ) ) then
		keys += 1
		open "/tmp/gfx.log" for append as #lg
		print #lg, "TOUCHE " & asc( k ) & iif( len( k ) > 1, " +" & asc( k, 2 ), "" )
		close #lg
		if( k = chr( 27 ) ) then exit do
		if( k = chr( 255 ) + "k" ) then exit do
		if( len( k ) = 1 ) then
			draw string (8 + (keys - 1) * 8, iif( mode32, 460, 150 )), k, iif( mode32, rgb( 255, 255, 255 ), 15 )
		end if
	end if
	if( multikey( fb.SC_F1 ) ) then
		open "/tmp/gfx.log" for append as #lg
		print #lg, "MULTIKEY F1"
		close #lg
		sleep 200, 1
	end if
	sleep 10, 1
loop until( timer - t0 > 60 )

open "/tmp/gfx.log" for append as #lg
print #lg, "FIN " & keys
close #lg
