#ifndef KORIN_SYSCALL_H
#define KORIN_SYSCALL_H

#include <stdint.h>

/* appelé par l'entrée des appels système de l'architecture */
int64_t syscall_dispatch(uint64_t nr, const uint64_t args[6]);

#endif
