# printerkit.bi -- printerkit pour le BASIC (#import printerkit) : genere par tools/kits/abi2bas.py depuis printerkit.abi ; ne pas editer.
kit printerkit 35
setup_default 0 l s print_setup_default a0
dialog 1 l ss print_dialog a0,a1
setup_paper 2 l sss print_setup_paper a0,a1,a2
begin 3 l ss print_begin a0,a1
page 4 l sss print_page a0,a1,a2
rect 5 l ssssss print_rect a0,a1,a2,a3,a4,a5
line 6 l sssssss print_line a0,a1,a2,a3,a4,a5,a6
frame 7 l sssssss print_frame a0,a1,a2,a3,a4,a5,a6
path_move 8 l sss print_path_move a0,a1,a2
path_line 9 l sss print_path_line a0,a1,a2
path_curve 10 l sssssss print_path_curve a0,a1,a2,a3,a4,a5,a6
path_close 11 l s print_path_close a0
path_fill 12 l ss print_path_fill a0,a1
path_stroke 13 l sss print_path_stroke a0,a1,a2
image_mode 14 l ss print_image_mode a0,a1
image 15 l ssssssss print_image a0,a1,a2,a3,a4,a5,a6,a7
image_jpeg 16 l ssssssss print_image_jpeg a0,a1,a2,a3,a4,a5,a6,a7
font 17 l sss print_font a0,a1,a2
text 18 l sssssss print_text a0,a1,a2,a3,a4,a5,a6
text_width 19 l ssss print_text_width a0,a1,a2,a3
font_metrics 20 l ssss print_font_metrics a0,a1,a2,a3
font_data 21 l sss print_font_data a0,a1,a2
glyph 22 l ssssssss print_glyph a0,a1,a2,a3,a4,a5,a6,a7
end 23 l s print_end a0
abort 24 l s print_abort a0
printers 25 l ss print_printers a0,a1
printer_media 26 l ssssss print_printer_media a0,a1,a2,a3,a4,a5
printer_add 27 l ssss print_printer_add a0,a1,a2,a3
printers_find 28 l ss print_printers_find a0,a1
printer_remove 29 l s print_printer_remove a0
printer_default 30 l s print_printer_default a0
printer_status 31 l sss print_printer_status a0,a1,a2
jobs 32 l ss print_jobs a0,a1
job_cancel 33 l s print_job_cancel a0
jobs_forget 34 l - print_jobs_forget
