/* Archive ustar chargée par le démarrage : les premiers programmes. */
#ifndef KORIN_INITRD_H
#define KORIN_INITRD_H

#include <stdint.h>

void initrd_init(const void *data, uint64_t size);
int initrd_find(const char *name, const void **data, uint64_t *size);
void initrd_list(void);

#endif
