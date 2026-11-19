' pyramid.bas -- une pyramide en marches, de laine de toutes les couleurs, à côté du joueur.
'   basic /apps/terracube.app/basic/pyramid.bas
'
#import NetKit

DECLARE FUNCTION TcConnect ()
DECLARE SUB TcClose ()
DECLARE FUNCTION Tc$ (v)
DECLARE SUB TcSend (c$)
DECLARE FUNCTION TcAsk$ (c$)
DECLARE SUB TcSetBlock (x, y, z, b$)
DECLARE SUB TcFill (x1, y1, z1, x2, y2, z2, b$)
DECLARE FUNCTION TcGetBlock (x, y, z)
DECLARE FUNCTION TcHeight (x, z)
DECLARE SUB TcPost (t$)
DECLARE SUB TcPlayerTile ()
DECLARE SUB TcTeleport (x, y, z)

DIM SHARED tcSock, tcBuf#, tcPend$, tcX, tcY, tcZ

IF TcConnect() = 0 THEN PRINT "TerraCube ne repond pas (le jeu tourne-t-il, Pilotage > Scripts coche ?)": END

DIM c$(5)
c$(0) = "wool_red": c$(1) = "wool_orange": c$(2) = "wool_yellow"
c$(3) = "wool_green": c$(4) = "wool_blue": c$(5) = "wool_purple"
TcPlayerTile
size = 12
cx = tcX + size + 3: cz = tcZ
base = TcHeight(cx, cz) + 1
FOR level = 0 TO size
	r = size - level
	TcFill cx - r, base + level, cz - r, cx + r, base + level, cz + r, c$(level MOD 6)
NEXT level
TcSetBlock cx, base + size + 1, cz, "lamp"
TcPost "Pyramide de couleurs en " + Tc$(cx) + " " + Tc$(base) + " " + Tc$(cz)
r$ = TcAsk$("ping()")
TcClose
END

' ---- TerraCube : les SUB et FUNCTION de connexion (à recopier dans vos programmes) ------------------------
' Le protocole : une commande par ligne sur le port 4730 (docs/TERRACUBE.md). Les blocs se donnent par nom
' ("stone", "gold_block"...) ou par numéro. Les commandes « get... » attendent leur réponse.

FUNCTION TcConnect ()
	tcBuf# = ALLOC(65536)
	tcSock = NetKit.net_socket(1)
	IF tcSock < 0 THEN TcConnect = 0: EXIT FUNCTION
	IF NetKit.net_connect(tcSock, 2130706433, 4730) <> 0 THEN TcConnect = 0: EXIT FUNCTION   ' 127.0.0.1
	tcPend$ = ""
	TcConnect = 1
END FUNCTION

SUB TcClose ()
	NetKit.net_close tcSock
	DEALLOC tcBuf#
END SUB

FUNCTION Tc$ (v)
	Tc$ = LTRIM$(STR$(INT(v)))
END FUNCTION

SUB TcSend (c$)
	l$ = c$ + CHR$(10)
	n = NetKit.net_send(tcSock, l$, LEN(l$))
END SUB

FUNCTION TcAsk$ (c$)
	TcSend c$
	tries = 0
	DO WHILE INSTR(tcPend$, CHR$(10)) = 0
		n = NetKit.net_recv(tcSock, tcBuf#, 65535, 5000)
		IF n = 0 THEN TcAsk$ = "error: connection closed": EXIT FUNCTION
		IF n < 0 THEN
			' (rien depuis 5 s : le jeu est occupé, on attend encore, 5 minutes au plus)
			tries = tries + 1
			IF tries > 60 THEN TcAsk$ = "error: no answer": EXIT FUNCTION
		ELSE
			tcPend$ = tcPend$ + CSTR$(tcBuf#, n)
		END IF
	LOOP
	p = INSTR(tcPend$, CHR$(10))
	TcAsk$ = LEFT$(tcPend$, p - 1)
	tcPend$ = MID$(tcPend$, p + 1)
END FUNCTION

SUB TcSetBlock (x, y, z, b$)
	TcSend "world.setBlock(" + Tc$(x) + "," + Tc$(y) + "," + Tc$(z) + "," + b$ + ")"
END SUB

SUB TcFill (x1, y1, z1, x2, y2, z2, b$)
	TcSend "world.setBlocks(" + Tc$(x1) + "," + Tc$(y1) + "," + Tc$(z1) + "," + Tc$(x2) + "," + Tc$(y2) + "," + Tc$(z2) + "," + b$ + ")"
END SUB

FUNCTION TcGetBlock (x, y, z)
	TcGetBlock = VAL(TcAsk$("world.getBlock(" + Tc$(x) + "," + Tc$(y) + "," + Tc$(z) + ")"))
END FUNCTION

FUNCTION TcHeight (x, z)
	TcHeight = VAL(TcAsk$("world.getHeight(" + Tc$(x) + "," + Tc$(z) + ")"))
END FUNCTION

SUB TcPost (t$)
	TcSend "chat.post(" + t$ + ")"
END SUB

' la case des pieds du joueur -> tcX, tcY, tcZ
SUB TcPlayerTile ()
	r$ = TcAsk$("player.getTile()")
	a = INSTR(r$, ","): b = INSTR(a + 1, r$, ",")
	IF a = 0 OR b = 0 THEN PRINT "TerraCube : "; r$: END
	tcX = VAL(LEFT$(r$, a - 1)): tcY = VAL(MID$(r$, a + 1, b - a - 1)): tcZ = VAL(MID$(r$, b + 1))
END SUB

SUB TcTeleport (x, y, z)
	TcSend "player.setTile(" + Tc$(x) + "," + Tc$(y) + "," + Tc$(z) + ")"
END SUB
