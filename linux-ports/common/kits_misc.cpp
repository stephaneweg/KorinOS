//
// kits_misc.cpp -- les petits bouts de NetKit et d'ArchiveKit dont Forge et TerraCube se servent, pour Linux :
//
//   NetKit       netkit_kit_open, net_info (l'adresse IPv4 de la machine : la premiere interface montee hors boucle
//                locale, getifaddrs ; l'adresse dite a l'agent IA dans la commande « claude mcp add »). Les sockets
//                des apps sont celles de POSIX : rien a refaire.
//   ArchiveKit   archivekit_kit_open, ar_crc32, ar_deflate_new (enveloppe AR_RAW, AR_ZLIB, AR_GZIP), ar_data,
//                ar_bytes, ar_free : sur zlib (les PNG des captures)
//
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <netkit_abi.h>
#include <archivekit_abi.h>

extern "C" {

int netkit_kit_open (void) { return 1; }

long net_info (long buf)
{
	unsigned char *b = (unsigned char *) buf;
	if (!b) return 0;
	memset (b, 0, 32);
	struct ifaddrs *ia = 0;
	if (getifaddrs (&ia) != 0) return 0;
	for (struct ifaddrs *i = ia; i; i = i->ifa_next)
	{
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
		uint32_t ip = ntohl (((struct sockaddr_in *) i->ifa_addr)->sin_addr.s_addr);
		uint32_t mask = i->ifa_netmask ? ntohl (((struct sockaddr_in *) i->ifa_netmask)->sin_addr.s_addr) : 0;
		memcpy (b + NI_IP, &ip, 4);	// (comme Korin : le premier octet de l'adresse dans les bits hauts)
		memcpy (b + NI_MASK, &mask, 4);
		uint32_t up = 1;
		memcpy (b + NI_UP, &up, 4);
		break;
	}
	freeifaddrs (ia);
	return 0;
}

int archivekit_kit_open (void) { return 1; }

long ar_crc32 (long crc, long data, long n) { return (long) crc32 ((uLong) crc, (const Bytef *) data, (uInt) n); }

struct ArBuf { unsigned char *p; long n; };

long ar_deflate_new (long src, long n, long wrap, long level)
{
	if (level < 0 || level > 9) level = 6;
	z_stream z;
	memset (&z, 0, sizeof z);
	int bits = wrap == AR_RAW ? -15 : wrap == AR_GZIP ? 31 : 15;
	if (deflateInit2 (&z, (int) level, Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY) != Z_OK) return 0;
	uLong cap = deflateBound (&z, (uLong) n) + 32;
	ArBuf *b = (ArBuf *) malloc (sizeof (ArBuf));
	if (!b) { deflateEnd (&z); return 0; }
	b->p = (unsigned char *) malloc (cap);
	if (!b->p) { free (b); deflateEnd (&z); return 0; }
	z.next_in = (Bytef *) src; z.avail_in = (uInt) n;
	z.next_out = b->p; z.avail_out = (uInt) cap;
	int r = deflate (&z, Z_FINISH);
	b->n = (long) z.total_out;
	deflateEnd (&z);
	if (r != Z_STREAM_END) { free (b->p); free (b); return 0; }
	return (long) b;
}

long ar_data (long h) { return h ? (long) ((ArBuf *) h)->p : 0; }
long ar_bytes (long h) { return h ? ((ArBuf *) h)->n : 0; }
long ar_free (long h) { if (h) { free (((ArBuf *) h)->p); free ((ArBuf *) h); } return 0; }

}
