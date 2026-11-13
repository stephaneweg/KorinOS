/* sys_consts.h : ce qu'inclut un programme du système en C (src/sys) : le runtime korinrt,
 * svclib (ini_read, str_copy_n, M64), puis les constantes de l'asm en valeurs
 * (src/kits/uikit/uk_consts.h : armpi.inc, ws_ext.inc, bus.inc, uikit.inc...). Les noms que
 * korinrt.h ou svclib.h définissent déjà (mêmes valeurs, autre écriture) sont retirés d'abord. */
#ifndef KORIN_SYS_CONSTS_H
#define KORIN_SYS_CONSTS_H

#include "korinrt.h"
#include "svclib.h"

#undef SND_RATE
#undef E_AGAIN
#undef BUF_VA_BASE
#undef BUF_SLOT
#undef ATTR_DIR
#undef ATTR_EXEC
#undef E_XDEV
#undef E_PIPE
#undef E_NOENT
#undef E_BADF
#undef E_EXIST
#undef E_NOTDIR
#undef E_ISDIR
#undef E_INVAL
#undef E_NOSPC
#undef E_NOTEMPTY
#undef E_NOMEM
#include "uk_consts.h"

#endif
