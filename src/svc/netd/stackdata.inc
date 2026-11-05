; ============================================================================
;  Donnees de la pile (segment des variables de netd)
; ============================================================================
net_name	db 'net',0
net_msg_dhcp	db 'NET : DHCP ok, adresse ',0
net_msg_gw	db '  passerelle ',0
net_msg_dns	db '  DNS ',0
		align 8
netdev_mac	dd 0				; MAC de la carte active
net_next_timer	dd 0
net_st		dd 0, 0, 0, 0, 0, 0, 0, 0	; trames, expirations, hors ordre, sans place, rapides, sondes, octets recus, emis
net_busy_t	dd 0				; derniere trame traitee, ms
net_rxframe	dd 0
net_txbuf	dd 0
sock_table	dd 0
ping_reply_ms	dd 0
dhcp_next_ms	dd 0
dhcp_tries	dd 0			; DISCOVER envoyes depuis le lien (stack.inc)
net_ip		dw 0
net_mask	dw 0
net_gw		dw 0
net_dns		dw 0
net_ip_id	dw 0
net_next_port	dw 0
dhcp_state	dw 0
dhcp_xid	dw 0
dhcp_offer_ip	dw 0
dhcp_server	dw 0
ping_id		dw 0
ping_seq	dw 0
arp_next	dw 0
net_lock	dw 0
netdev_up	dw 0				; 1 : une carte est prete
ping_mutex	dw 0
		align 16
arp_table	rb ARP_ENTRIES * 16
net_arpbuf	rb 64
net_mcmac	rb 8

