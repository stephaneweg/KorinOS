' Essai de pibuild (docs/GCC.md) : deux modules, un sous-dossier, un kit, un thread.
#include once "lib/add.bi"
#include once "kits/uikit.bi"

dim shared ui as ui_vars
dim shared total as integer

sub work( byval p as any ptr )
	total = add( 2, 40 )
end sub

var t = threadcreate( @work )
threadwait( t )
print "pibasic " & total
print "uikit " & uikit_open( @ui )
