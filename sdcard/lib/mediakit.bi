# mediakit.bi -- mediakit pour le BASIC (#import mediakit) : genere par tools/kits/abi2bas.py depuis mediakit.abi ; ne pas editer.
kit mediakit 15
version 0 l - mk_version
open 1 l s mk_open a0
close 2 l s mk_close a0
info 3 l ss mk_info a0,a1
track 4 l sss mk_track a0,a1,a2
select 5 l sss mk_select a0,a1,a2
video_next 6 l ss mk_video_next a0,a1
video_convert 7 l sssss mk_video_convert a0,a1,a2,a3,a4
video_read 8 l ssssss mk_video_read a0,a1,a2,a3,a4,a5
audio_open 9 l ss mk_audio_open a0,a1
audio_read 10 l ssss mk_audio_read a0,a1,a2,a3
audio_packet 11 l ss mk_audio_packet a0,a1
seek 12 l ss mk_seek a0,a1
stats 13 l ss mk_stats a0,a1
error_text 14 s s mk_error_text a0
