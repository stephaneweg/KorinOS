/* gdrivefs sous Linux : les fonctions de NetKit qu'il appelle, simulées par test/linux.c (pas d'oauthd). */
#ifndef NETKIT_ABI_TEST_H
#define NETKIT_ABI_TEST_H
int netkit_kit_open(void);
long oauth_token(long a0, long a1, long a2);
long oauth_error(long a0, long a1);
#endif
