/* netinet/in.h -- celui de libposix, plus les noms de Linux qu'attend System.Native. Le reseau
 * d'ARMPI est en IPv4 : ces options IPv6 sont refusees a l'appel. */
#ifndef _ARMPI_COMPAT_NETINET_IN_H
#define _ARMPI_COMPAT_NETINET_IN_H

#include_next <netinet/in.h>

#define IP_HDRINCL		3
#define IP_OPTIONS		4
#define IP_PKTINFO		8
#define IPV6_UNICAST_HOPS	16
#define IPV6_MULTICAST_IF	17
#define IPV6_MULTICAST_HOPS	18
#define IPV6_MULTICAST_LOOP	19
#define IPV6_ADD_MEMBERSHIP	20
#define IPV6_DROP_MEMBERSHIP	21
#define IPV6_RECVPKTINFO	49
#define IPV6_PKTINFO		50
#define IPV6_RECVHOPLIMIT	51
#define IPV6_HOPLIMIT		52

struct in_pktinfo
{
	int ipi_ifindex;
	struct in_addr ipi_spec_dst, ipi_addr;
};

struct in6_pktinfo
{
	struct in6_addr ipi6_addr;
	unsigned ipi6_ifindex;
};

struct ipv6_mreq
{
	struct in6_addr ipv6mr_multiaddr;
	unsigned ipv6mr_interface;
};

#endif
