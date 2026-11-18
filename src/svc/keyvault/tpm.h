/* Le TPM 2.0 du PC pour le coffre (tpm.c) : la clé de la machine scellée dans le TPM. */
#ifndef KEYVAULT_TPM_H
#define KEYVAULT_TPM_H

#include <stdint.h>

/* la clé de la machine (32 octets) : 1 rendue par le TPM (scellée dans <dir>/machine.tpm, faite au premier
 * usage) ; 0 pas de TPM (le coffre prend le fichier machine.key) ; < 0 le TPM refuse de la rendre */
int tpm_machine_key(const char *dir, uint8_t out[32]);

#endif
