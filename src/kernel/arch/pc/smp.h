#ifndef KORIN_PC_SMP_H
#define KORIN_PC_SMP_H

#include <stdint.h>

/* page libre sous 1 Mo (4 pages de suite), trouvée dans la carte de l'UEFI */
void smp_set_trampoline(uint64_t phys);
/* démarre tous les autres cœurs logiques de la MADT */
void smp_start(void);

#endif
