/* Archive ustar chargée par le démarrage : les premiers programmes. */
#ifndef KORIN_INITRD_H
#define KORIN_INITRD_H

#include <stdint.h>

void initrd_init(const void *data, uint64_t size);
/* une seconde archive (\korin\local.tar du PC) : un nom qu'elle contient passe avant celui de
 * l'initrd */
void initrd_init_local(const void *data, uint64_t size);
/* un module texte fourni par le noyau (« bootpart ») ; text doit rester valable ; passe avant les
 * archives ; 4 au plus */
void initrd_set_text(const char *name, const char *text);
int initrd_find(const char *name, const void **data, uint64_t *size);
void initrd_list(void);

#endif
