; ============================================================================
;  kits/netkit/netkit.inc - NetKit, cote programme : constantes du reseau,
;  macro netcall (les fonctions sont dans SD0:/lib/netkit.so)
; ============================================================================
; (appels 40-52 du noyau : retires, docs/RESEAU.md etape 6 ; fonctions de
; NetKit net_* a la place, macro netcall)

SOCK_TCP		= 1
SOCK_UDP		= 2
NI_IP			= 0		; champs de netinfo
NI_MASK			= 4
NI_GW			= 8
NI_DNS			= 12
NI_MAC			= 16
NI_UP			= 24
NI_DHCP			= 28		; 4 = adresse obtenue

; Appel d'une fonction de NetKit a la place d'un appel systeme : x30 garde
; (comme la macro sys), tous les registres sauf x0 gardes par NetKit
macro netcall f* {
	str	x30,[sp,-16]!
	bl	f
	ldr	x30,[sp],16
}
