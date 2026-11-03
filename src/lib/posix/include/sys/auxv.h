/* sys/auxv.h : getauxval (misc.c). ARMPI n'a pas de vecteur auxiliaire : le peu qu'on sait du
   processeur vient de libposix. */
#ifndef _SYS_AUXV_H
#define _SYS_AUXV_H

#define AT_HWCAP	16
#define AT_HWCAP2	26

#define HWCAP_FP	(1UL << 0)
#define HWCAP_ASIMD	(1UL << 1)

#ifdef __cplusplus
extern "C" {
#endif
unsigned long getauxval (unsigned long type);
#ifdef __cplusplus
}
#endif

#endif
