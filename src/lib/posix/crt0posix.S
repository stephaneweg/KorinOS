/*
 * crt0posix.S -- le point d'entree d'un programme C d'ARMPI construit sur libposix
 * (docs/POSIX.md ; repris de crt0posix d'Onyx).
 *
 * Le noyau entre dans _start en EL0 (proc_spawn_elf) : x0 = ligne d'arguments (chaine UTF-8 en
 * haut de la pile) ou 0, sp = sommet de la pile du thread principal, TPIDR_EL0 = 0. La chaine
 * des cadres est terminee (x29 = 0, x30 = 0 : une trace s'arrete la) et __posix_start (start.c)
 * fait le reste en C : TPIDR_EL0 du thread principal (tls.c), tables de deroulement, descripteurs,
 * environnement, argc / argv, constructeurs, puis exit (main (argc, argv, envp)).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
	.section .text._start, "ax"
	.globl	_start
	.type	_start, %function
_start:
	mov	x29, #0
	mov	x30, #0
	mov	x1, sp
	and	x1, x1, #~15		/* (la pile du noyau est alignee ; on la garde ainsi) */
	mov	sp, x1
	bl	__posix_start		/* x0 = ligne d'arguments ; ne revient pas */
1:	b	1b
	.size	_start, . - _start

/*
 * _init / _fini vides : __libc_fini_array de newlib (exit) appelle _fini, et il n'y a pas de
 * crti.o. Constructeurs et destructeurs : .init_array / .fini_array. Faibles : un vrai l'emporte.
 */
	.section .text._init_fini, "ax"
	.weak	_init
	.weak	_fini
	.type	_init, %function
	.type	_fini, %function
_init:
_fini:
	ret
