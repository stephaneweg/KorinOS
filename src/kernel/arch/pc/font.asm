; Police de la console du noyau x86-64 : la même que celle du Pi
; (src/kernel/font8x16.inc), en objet ELF64 pour le C (font_glyphs).

format ELF64

section '.rodata'

public font_glyphs
include '../../font8x16.inc'
