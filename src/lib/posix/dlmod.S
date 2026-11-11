/*
 * dlmod.S -- ce que posix.specs ajoute a chaque module de dlopen (gcc -specs=posix.specs -fPIC
 * -shared ; libposix dl.c, docs/POSIX.md « dlopen ») : ce que crtbegin et crtend donnent
 * ailleurs, et que -nostdlib retirerait.
 *  - le mot nul qui termine les tables de deroulement du module : le derouleur de libgcc les lit
 *    jusqu'a lui (__register_frame_info, dl.c). La section .eh_frame.* est rangee par le script
 *    par defaut de ld apres toutes les .eh_frame, quelle que soit la place de l'objet ;
 *  - __dso_handle, cache, propre au module : __cxa_atexit (destructeurs des objets statiques
 *    C++) range ses fonctions sous cette adresse.
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
	.section .eh_frame.posix_end, "a"
	.balign 4
	.long 0

	.section .data.rel.ro.__dso_handle, "aw"
	.balign 8
	.globl __dso_handle
	.hidden __dso_handle
	.type __dso_handle, %object
	.size __dso_handle, 8
__dso_handle:
	.quad __dso_handle

	.section .note.GNU-stack, "", %progbits
