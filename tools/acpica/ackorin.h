/* ackorin.h - plateforme Korin pour ACPICA (tools/acpica/build.sh, service acpid du PC).
 *
 * ACPICA tourne dans un service en espace utilisateur (src/svc/acpid), d'un seul fil : les verrous et
 * sémaphores de l'OSL (src/svc/acpid/osl.c) sont des compteurs, les travaux différés (AcpiOsExecute)
 * passent dans la boucle du service. Pas de bibliothèque C : ACPICA garde la sienne (utclib.c,
 * utprint.c), renommée ici pour ne pas heurter celle de korinrt (src/lib/korinrt) au moment du lien.
 * Copié dans source/include/platform/ par build.sh ; acenv.h l'inclut quand __KORIN__ est défini
 * (acenv.patch). */
#ifndef __ACKORIN_H__
#define __ACKORIN_H__

#define ACPI_MACHINE_WIDTH 64
#define COMPILER_DEPENDENT_INT64 long
#define COMPILER_DEPENDENT_UINT64 unsigned long
#define ACPI_USE_DO_WHILE_0
#define ACPI_USE_LOCAL_CACHE
#define ACPI_USE_NATIVE_DIVIDE
#define ACPI_USE_NATIVE_MATH64
#define ACPI_SINGLE_THREADED
#define ACPI_FLUSH_CPU_CACHE()

/* Verrou global de la FACS (ACPI 5.2.10.1), par l'osl : 1 si pris, 0 s'il faut attendre */
int AcpiKorinAcquireGlobalLock(void *Facs);
int AcpiKorinReleaseGlobalLock(void *Facs); /* 1 : le micrologiciel attend (GBL_RLS) */
#define ACPI_ACQUIRE_GLOBAL_LOCK(Facs, Acq) ((Acq) = (unsigned char)AcpiKorinAcquireGlobalLock(Facs))
#define ACPI_RELEASE_GLOBAL_LOCK(Facs, Pnd) ((Pnd) = (unsigned char)AcpiKorinReleaseGlobalLock(Facs))

/* la bibliothèque C d'ACPICA, sous d'autres noms (définitions et appels) */
#define strcat AcpiKstrcat
#define strchr AcpiKstrchr
#define strpbrk AcpiKstrpbrk
#define strtok AcpiKstrtok
#define strcpy AcpiKstrcpy
#define strcmp AcpiKstrcmp
#define strlen AcpiKstrlen
#define strncat AcpiKstrncat
#define strncmp AcpiKstrncmp
#define strncpy AcpiKstrncpy
#define strstr AcpiKstrstr
#define strtoul AcpiKstrtoul
#define memcmp AcpiKmemcmp
#define memcpy AcpiKmemcpy
#define memmove AcpiKmemmove
#define memset AcpiKmemset
#define tolower AcpiKtolower
#define toupper AcpiKtoupper
#define vsnprintf AcpiKvsnprintf
#define snprintf AcpiKsnprintf
#define sprintf AcpiKsprintf

#endif
