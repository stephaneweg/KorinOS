' Essai de FreeBASIC sous ARMPI (docs/GCC.md) : fichiers, dossiers, threads, mutex, conditions, exec
#include once "file.bi"
#include once "dir.bi"

dim shared as integer failures = 0

sub check( byref what as string, byval ok as boolean )
	if( ok ) then
		print "PASS " & what
	else
		print "FAIL " & what
		failures += 1
	end if
end sub

' ---- fichiers
const D = "/tmp/fbt"
mkdir( D )
var f = D & "/texte.txt"
var h = freefile( )
check( "open output", open( f, for output, as #h ) = 0 )
for i as integer = 1 to 100
	print #h, "ligne " & i
next
close #h
check( "fileexists", fileexists( f ) )
check( "filelen", filelen( f ) = 892 )

dim as string ln
dim as integer n = 0
open f for input as #h
do until eof( h )
	line input #h, ln
	n += 1
loop
close #h
check( "line input", (n = 100) and (ln = "ligne 100") )

open f for append as #h
print #h, "fin"
close #h
check( "append", filelen( f ) = 896 )

var b = D & "/bin.dat"
dim as long v(0 to 999), w(0 to 999)
for i as integer = 0 to 999
	v(i) = i * i
next
open b for binary as #h
put #h, , v()
close #h
open b for binary as #h
get #h, , w()
seek #h, 401
dim as long x
get #h, , x
close #h
check( "binary put/get", (w(999) = 998001) and (x = 10000) )

var cnt = 0, e = dir( D & "/*", fbNormal )
while len( e )
	cnt += 1
	e = dir( )
wend
check( "dir", cnt = 2 )
check( "name", name( b, D & "/bin2.dat" ) = 0 )
check( "kill", (kill( D & "/bin2.dat" ) = 0) and (kill( f ) = 0) )
check( "rmdir", rmdir( D ) = 0 )

' ---- threads
dim shared as any ptr m, c
dim shared as integer total = 0, ready = 0

sub worker( byval p as any ptr )
	for i as integer = 1 to 10000
		mutexlock( m )
		total += 1
		mutexunlock( m )
	next
end sub

sub waiter( byval p as any ptr )
	mutexlock( m )
	while ready = 0
		condwait( c, m )
	wend
	ready = 2
	mutexunlock( m )
end sub

m = mutexcreate( )
c = condcreate( )
dim as any ptr t(0 to 3)
for i as integer = 0 to 3
	t(i) = threadcreate( @worker )
next
for i as integer = 0 to 3
	threadwait( t(i) )
next
check( "threads + mutex", total = 40000 )

var tw = threadcreate( @waiter )
sleep 50, 1
mutexlock( m )
ready = 1
condsignal( c )
mutexunlock( m )
threadwait( tw )
check( "condwait", ready = 2 )
mutexdestroy( m )
conddestroy( c )

' ---- lancement, date, nombres, chaines
check( "exec", exec( "/bin/true", "" ) = 0 )
check( "shell", shell( "/bin/true" ) = 0 )
check( "date", (val( right( date( ), 4 ) ) >= 2024) )
check( "double", abs( sin( 1.0 ) ^ 2 + cos( 1.0 ) ^ 2 - 1 ) < 1e-12 )
check( "string", ucase( "armpi" & str( 64 ) ) = "ARMPI64" )
dim as wstring * 8 ws = "été"
check( "wstring", len( ws ) >= 3 )
print "exepath " & exepath( )
print "FIN " & failures & " echec(s)"
end failures
