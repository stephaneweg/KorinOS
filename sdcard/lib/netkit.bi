# netkit.bi -- netkit pour le BASIC (#import netkit) : genere par tools/kits/abi2bas.py depuis netkit.abi ; ne pas editer.
kit netkit 31
fmt_ip 0 l ss fmt_ip a0,a1
print_ip 1 l s print_ip a0
wait_dhcp 2 l s wait_dhcp a0
net_socket 3 l s net_socket a0
net_connect 4 l sss net_connect a0,a1,a2
net_bind 5 l ss net_bind a0,a1
net_listen 6 l ss net_listen a0,a1
net_accept 7 l ss net_accept a0,a1
net_send 8 l sss net_send a0,a1,a2
net_recv 9 l ssss net_recv a0,a1,a2,a3
net_close 10 l s net_close a0
net_sendto 11 l sssss net_sendto a0,a1,a2,a3,a4
net_recvfrom 12 l sssss net_recvfrom a0,a1,a2,a3,a4
net_resolve 13 l s net_resolve a0
net_info 14 l s net_info a0
net_ping 15 l ss net_ping a0,a1
net_ifaces 16 l ss net_ifaces a0,a1
net_time 17 l s net_time a0
net_ntp_sync 18 l s net_ntp_sync a0
wifi_scan 19 l ss wifi_scan a0,a1
wifi_join 20 l ss wifi_join a0,a1
wifi_status 21 l s wifi_status a0
wifi_leave 22 l - wifi_leave
wifi_forget 23 l s wifi_forget a0
wifi_known 24 l ss wifi_known a0,a1
wifi_priority 25 l ss wifi_priority a0,a1
wifi_country 26 l s wifi_country a0
wifi_join_ex 27 l ssss wifi_join_ex a0,a1,a2,a3
net_active 28 l s net_active a0
net_shutdown 29 l s net_shutdown a0
net_stats 30 l s net_stats a0
