---
name: korin-kit-netkit
description: NetKit, le réseau de Korin OS — sockets TCP/UDP par le service netd, DNS, ping, état des interfaces, heure NTP et Wi-Fi (fonctions net_* et wifi_*, IPv4 seulement). À charger pour tout programme qui ouvre une connexion, résout un nom, teste le réseau ou pilote le Wi-Fi.
---

# NetKit : le réseau

Le kit ne tient aucune pile : chaque appel est une requête au service `net` (**netd**, pile IPv4) ou au service
`wifi`. Les sockets BSD de la libc (`socket`, `connect`, `getaddrinfo`, `poll`…) sont écrits **sur NetKit** : un
programme porté d'Unix les garde. Seul NetKit donne l'état de l'interface, le ping, l'heure et le Wi-Fi.
Le TLS n'est pas dans NetKit : c'est OpenSSL, lié au programme, sur un socket BSD ordinaire.

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide de l'API (exemples, pièges) | `SD0:/apps/help.app/api/netkit_fr.md` | `src/apps/help/data/api/netkit_fr.md` |
| Table du kit (ordre, commentaires) | — | `src/kits/netkit/netkit.abi`, constantes `netkit.inc` |
| En-tête C | `/gcc/include/kits/netkit_abi.h` | généré par `tools/kits/abi2c.py` |
| Protocole kit ↔ netd, Wi-Fi | — | `docs/RESEAU.md` (§ 3, § 4), `docs/WIFI.md`, `docs/KITS.md` |

Usages réels : `src/bin/ping/ping.asm`, `src/bin/wifi/wifi.asm`, `src/bin/httpd/httpd.asm` (asm) ;
`src/apps/irc`, `src/apps/mail`, `src/apps/courier` (C++ : `net_info` pour tester le réseau, sockets BSD sinon).

## Ouvrir le kit

C / C++ :

```c
#include <netkit_abi.h>
if (!netkit_kit_open ()) return 1;     /* SD0:/lib/netkit.so absent */
```

`pibuild` lie déjà tout `/gcc/lib/lib*.a` ; à la main : `gcc -I/gcc/include/kits main.c -lnetkit`.
Les sockets BSD n'ont rien à ouvrir : la libc ouvre NetKit au premier socket.

Assembleur (FASMARM) : `include '../../kits/netkit/netkit.inc'`, puis dans le segment de code
`kit_use '../../kits/netkit/netkit.abi'`, `bl kits_open` au démarrage (ou `t_debut` de `tool.inc`, qui l'appelle),
`kit_data` dans le segment de données ; ensuite `bl net_socket` comme un appel ordinaire. Les fonctions de
NetKit gardent tous les registres sauf x0 (et x16, x17 des relais).

Les noms des fonctions sont les mêmes dans tous les langages ; les skills `korin-lang-*` expliquent le mécanisme
de chaque langage.

## L'essentiel

Tout argument est un `long` (pointeur : `(long) buf`), tout résultat un `long` ; négatif = erreur, numéros de
Linux (`-11` `E_AGAIN`, `-110` délai, `-19` pas de Wi-Fi). **Adresses et ports dans l'ordre de l'hôte**
(`10.0.2.15` = `0x0A00020F`, pas de `htons`). Délais en ms : 0 tout de suite, -1 sans fin.

État et adresses :
- `net_info(buf32)` → 0 ; tampon netinfo : `NI_IP` (0 : pas d'adresse), `NI_MASK`, `NI_GW`, `NI_DNS`, `NI_MAC` (6 o), `NI_UP`, `NI_DHCP` (4 : bail obtenu)
- `wait_dhcp(buf32)` → 1 si une adresse est venue (attend 20 s au plus)
- `net_active(buf16)` → nom de l'interface active (8 o) puis type à +8 (1 Ethernet, 2 Wi-Fi, 3 USB)
- `net_ifaces(buf, places)` → nombre d'entrées de 64 o (nom, type +8, lien +12, ip +16, débit +24, SSID +32)
- `fmt_ip(ip, buf16)` → écrit « a.b.c.d\0 » ; `print_ip(ip)` écrit dans le **journal** du noyau
- `net_stats(buf152)`, `net_sockets(buf, places)` : compteurs et sockets du système (`netstat`)

Sockets :
- `net_socket(SOCK_TCP | SOCK_UDP)` → sd ; `net_close(sd)` ; `net_shutdown(sd)` (FIN, on reçoit encore)
- `net_connect(sd, ip, port)` → 0 ; attend la connexion
- `net_bind(sd, port)`, `net_listen(sd, file)`, `net_accept(sd, délai)` → sd de la connexion ou `-11`
- `net_send(sd, buf, n)` → octets ; `net_recv(sd, buf, max, délai)` → octets, 0 : fermée, `-11` : délai écoulé
- `net_sendto(sd, buf, n, ip, port)` ; `net_recvfrom(sd, buf, max, délai, (long) unsigned src[2])` (ip, port)

DNS, ping, heure :
- `net_resolve("nom" ou "a.b.c.d")` → IPv4 ; négatif : inconnu ; `localhost` → 127.0.0.1
- `net_ping(ip, délai)` → aller-retour en ms, négatif sans réponse
- `net_time((long) long t[3])` ou `net_time(0)` → UTC en s (0 : inconnue) ; `t[1]` = 1 si NTP
- `net_ntp_sync(1)` attend l'heure (20 s au plus) ; `net_ntp_sync(0)` la demande

Wi-Fi (service `wifi`) :
- `wifi_status(buf64)` → 0 ; état +0 (4 connecté), RSSI +4, MAC +8, SSID +16, longueur +48
- `wifi_scan(buf, places)` → réseaux (entrées de 48 o, ~3 s) ; `wifi_join(ssid, clé ou 0)` (mémorise),
  `wifi_join_ex(ssid, clé, mémoriser, priorité)`, `wifi_leave()`, `wifi_known`, `wifi_forget`, `wifi_priority`,
  `wifi_country("FR")`

## Exemple

Une requête HTTP en clair (adapté de « Exemple : une requête HTTP par NetKit » du guide) :

```c
#include <stdio.h>
#include <netkit_abi.h>

int main (int argc, char **argv)
{
	const char *host = argc > 1 ? argv[1] : "example.com";
	if (!netkit_kit_open ()) return 1;
	unsigned char ni[32];
	net_info ((long) ni);
	if (*(unsigned *) (ni + NI_IP) == 0) { printf ("pas de reseau\n"); return 1; }
	long ip = net_resolve ((long) host);
	if (ip <= 0) { printf ("hote inconnu\n"); return 1; }
	long sd = net_socket (SOCK_TCP);
	if (sd < 0 || net_connect (sd, ip, 80) != 0) { if (sd >= 0) net_close (sd); return 1; }
	char req[256];
	int n = snprintf (req, sizeof req, "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
	net_send (sd, (long) req, n);
	static char buf[8192];
	long total = 0, r;
	while ((r = net_recv (sd, (long) buf, sizeof buf, 10000)) > 0)	/* 8 Ko au plus par appel */
		total += r;
	printf ("%ld octets%s\n", total, r < 0 ? " (delai depasse)" : "");
	net_close (sd);
	return 0;
}
```

## Pièges

- **IPv4 seulement** : `AF_INET6` rend `EAFNOSUPPORT` ; pas de `getifaddrs` (`ENOSYS`) : utiliser `net_info`.
- **Ordre des octets** : NetKit en ordre hôte, les sockets BSD en ordre réseau (`htonl`, `inet_pton`). Ne pas mêler.
- **Les appels attendent** : `net_connect`, `net_resolve`, `net_recv`/`net_accept` avec délai, `wifi_scan`,
  `wifi_join`. Dans une app graphique : un thread (`pthread_create`), ou `net_recv` avec délai 0 dans la boucle.
- **`connect` BSD attend toujours**, même sur un socket non bloquant ; l'adresse distante d'un `accept` BSD est
  inconnue (0.0.0.0:0) ; pas de `SIGPIPE` (`send` rend `EPIPE`).
- **`net_recv` rend au plus 8 Ko** : lire en boucle. Les données passent par un tampon de 128 Ko par process
  (16 zones de 8 Ko) ; plusieurs threads peuvent appeler NetKit en même temps.
- **64 sockets pour tout le système** (dans netd) : fermer ce qu'on n'utilise plus. netd ferme ceux d'un process
  qui se termine.
- **Pas d'horloge sauvegardée** sur le Pi : avant le premier NTP, l'heure est celle du dernier arrêt. Avant de
  vérifier un certificat, boucler `net_ntp_sync (1)` tant que `net_time (0)` rend 0.
- **Sans netd**, `net_info` rend des zéros et le reste des négatifs : tester `net_info` avant une action réseau.
- **Sous QEMU**, pas de Wi-Fi (`wifi_*` → `-19`) ; le réseau passe par l'USB émulé (`usb0`).
- **fork** : la libc appelle elle-même `net_fork_child` dans le fils (rang 32, `netkit.abi`) : ne pas l'appeler à la
  main.
