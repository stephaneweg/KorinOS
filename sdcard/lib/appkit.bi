# appkit.bi -- appkit pour le BASIC (#import appkit) : genere par tools/kits/abi2bas.py depuis appkit.abi ; ne pas editer.
kit appkit 48
abi_version 0 u - kapi_abi_version
kernel_info 1 i pi kapi_kernel_info buf,cap
puts 2 l s puts a0
fmt_dec 3 l ss fmt_dec a0,a1
print_dec 4 l s print_dec a0
str_cat 5 l ss str_cat a0,a1
malloc 6 l s malloc a0
str_len 7 l s str_len a0
str_eq 8 l ss str_eq a0,a1
mem_move 9 l sss mem_move a0,a1,a2
utf8_put 10 l ss utf8_put a0,a1
path_join 11 l sss path_join a0,a1,a2
path_parent 12 l s path_parent a0
parse_dec 13 l s parse_dec a0
str_ends 14 l ss str_ends a0,a1
fichier_sys 15 l ssssss fichier_sys a0,a1,a2,a3,a4,a5
f_ouvrir 16 l ss f_ouvrir a0,a1
f_lire 17 l sss f_lire a0,a1,a2
f_ecrire 18 l sss f_ecrire a0,a1,a2
f_position 19 l sss f_position a0,a1,a2
f_fermer 20 l s f_fermer a0
f_stat 21 l ss f_stat a0,a1
f_lire_rep 22 l ss f_lire_rep a0,a1
f_mkdir 23 l s f_mkdir a0
f_unlink 24 l s f_unlink a0
f_chdir 25 l s f_chdir a0
f_getcwd 26 l ss f_getcwd a0,a1
f_print 27 l ss f_print a0,a1
f_spawn 28 l ss f_spawn a0,a1
f_spawn_pipe 29 l sss f_spawn_pipe a0,a1,a2
f_spawn_stdio 30 l sss f_spawn_stdio a0,a1,a2
f_spawn_file 31 l sss f_spawn_file a0,a1,a2
f_pipe 32 l s f_pipe a0
f_kit_load 33 l s f_kit_load a0
mtx_lock 34 l s mtx_lock a0
mtx_trylock 35 l s mtx_trylock a0
mtx_unlock 36 l s mtx_unlock a0
cnd_wait 37 l ss cnd_wait a0,a1
cnd_timedwait 38 l sss cnd_timedwait a0,a1,a2
cnd_signal 39 l s cnd_signal a0
cnd_broadcast 40 l s cnd_broadcast a0
f_redir 41 l ss f_redir a0,a1
f_spawn_env 42 l ssss f_spawn_env a0,a1,a2,a3
f_start_blob 43 l - f_start_blob
lang_tr 44 l s lang_tr a0
lang_puts 45 l s lang_puts a0
lang_get 46 l - lang_get
lang_load 47 l s lang_load a0
