/* korin_pre.h -- inclus devant chaque source de Mesa du PC (configure-iris.sh), comme -include alloca.h
 * -include limits.h pour le Pi, mais pas devant les sources en assembleur (blake3). */
#ifndef __ASSEMBLER__
#include <alloca.h>
#include <limits.h>
#endif
