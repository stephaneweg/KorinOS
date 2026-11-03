# gpukit.bi -- gpukit pour le BASIC (#import gpukit) : genere par tools/kits/abi2bas.py depuis gpukit.abi ; ne pas editer.
kit gpukit 12
info 0 l s gk_info a0
alloc 1 l s gk_alloc a0
free 2 l s gk_free a0
fill 3 l sssssss gk_fill a0,a1,a2,a3,a4,a5,a6
copy 4 l ssssss gk_copy a0,a1,a2,a3,a4,a5
demo_triangle 5 l ssss gk_demo_triangle a0,a1,a2,a3
texture 6 l sssss gk_texture a0,a1,a2,a3,a4
texture_rect 7 l sssssss gk_texture_rect a0,a1,a2,a3,a4,a5,a6
render 8 l sssss gk_render a0,a1,a2,a3,a4
debug 9 l sss gk_debug a0,a1,a2
draw 10 l ssssss gk_draw a0,a1,a2,a3,a4,a5
texture_info 11 l ss gk_texture_info a0,a1
