/*
 * dlunwmod.S -- le derouleur vu d'un module (-shared), range dans libgcc_mod.a a la place de
 * unwind-dw2.o et unwind-dw2-fde.o (tools/posix/build.sh, docs/POSIX.md « dlopen »,
 * docs/RUST.md) : chaque _Unwind_X / __register_frame_X du module, cache, saute vers
 * __posix_unw_X de l'hote (dlunwind.S). Le module n'a donc pas de derouleur a lui : sans cela, la
 * copie privee de libgcc ne connaitrait aucune table (dlopen les inscrit chez l'hote) et une panique
 * Rust levee dans le module finirait en abort.
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
	.text
	.macro relais nom
	.globl \nom
	.hidden \nom
	.type \nom, %function
	.balign 4
\nom:
	b __posix_unw_\nom
	.size \nom, . - \nom
	.endm

	relais _Unwind_Backtrace
	relais _Unwind_DeleteException
	relais _Unwind_FindEnclosingFunction
	relais _Unwind_ForcedUnwind
	relais _Unwind_GetCFA
	relais _Unwind_GetDataRelBase
	relais _Unwind_GetGR
	relais _Unwind_GetIP
	relais _Unwind_GetIPInfo
	relais _Unwind_GetLanguageSpecificData
	relais _Unwind_GetRegionStart
	relais _Unwind_GetTextRelBase
	relais _Unwind_RaiseException
	relais _Unwind_Resume
	relais _Unwind_Resume_or_Rethrow
	relais _Unwind_SetGR
	relais _Unwind_SetIP
	relais _Unwind_Find_FDE
	relais __register_frame
	relais __register_frame_info
	relais __register_frame_info_bases
	relais __register_frame_info_table
	relais __register_frame_info_table_bases
	relais __register_frame_table
	relais __deregister_frame
	relais __deregister_frame_info
	relais __deregister_frame_info_bases

	.section .note.GNU-stack, "", %progbits
