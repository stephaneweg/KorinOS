/* des.h : DES (FIPS 46-3), chiffrement d'un bloc, pour la « VNC Authentication »
 * (RFC 6143 7.2.2) ; traduction de des.inc (des.c). */
#ifndef VNCD_DES_H
#define VNCD_DES_H

/* key : 8 octets (bits de parité ignorés) -> 16 sous-clés de 48 bits (des_cles) */
void des_keys(const u8 *key, u64 *sub);
/* sous-clés, bloc clair (8 o) -> bloc chiffré (8 o, peut être le clair) (des_bloc) */
void des_block(const u64 *sub, const u8 *in, u8 *out);

#endif
