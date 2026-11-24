/*
 * resolv.h -- Korin n'a pas de libresolv (la résolution des noms passe par NetKit : getaddrinfo, netdb.c).
 * Cet en-tête existe pour le code qui l'inclut sans appeler res_* (Qt : qnet_unix_p.h, docs/etude/QT.md).
 */
#ifndef _RESOLV_H
#define _RESOLV_H
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
