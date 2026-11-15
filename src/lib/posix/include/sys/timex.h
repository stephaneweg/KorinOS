/* sys/timex.h : l'en-tete de Linux pour adjtimex. Korin ne regle pas l'horloge par ce chemin
   (CLOCK_SET de netd) : seulement ce qu'il amene d'ordinaire, gettimeofday et struct timeval.
   Inclus par basis_universal (Godot) pour son minuteur. */
#ifndef _SYS_TIMEX_H
#define _SYS_TIMEX_H

#include <sys/time.h>

#endif
