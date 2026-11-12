/*
 * dlunwind.S -- le derouleur de l'hote, exporte vers ses modules (docs/POSIX.md « dlopen »,
 * docs/RUST.md). Dans libgcc.a, _Unwind_* et __register_frame* sont GLOBAL HIDDEN : un hote
 * -rdynamic ne peut pas les exporter. Chaque __posix_unw_X est un saut vers X, exporte (dlhost.ld) ;
 * un module les atteint par dlunwmod.S (libgcc_mod.a). Ainsi un processus n'a qu'un derouleur, qui
 * connait les tables de l'hote et celles que dlopen inscrit pour chaque module : une exception (une
 * panique Rust, une exception C++ relancee par _Unwind_Resume) traverse hote et modules.
 * Un saut, pas un appel : aucun cadre de plus sur la pile, X voit l'appelant d'origine.
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
	.globl __posix_unw_\nom
	.type __posix_unw_\nom, %function
	.balign 4
__posix_unw_\nom:
	b \nom
	.size __posix_unw_\nom, . - __posix_unw_\nom
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
