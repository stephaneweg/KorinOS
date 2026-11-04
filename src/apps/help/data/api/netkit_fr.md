# NetKit : le réseau

**NetKit** donne le réseau aux programmes : sockets TCP et UDP, DNS, ping, état de l'interface, heure du réseau (NTP) et Wi-Fi. Le kit lui-même ne tient aucune pile réseau : chaque appel est une requête au service `net` (**netd**, la pile IPv4) ou au service `wifi` (le pilote de la puce Wi-Fi). Le kit est en assembleur, dans `SD0:/lib/netkit.so`, partagé par tout le système.

Deux chemins mènent au réseau depuis le C :

- les **fonctions de NetKit** (`net_*`, `wifi_*`), décrites ici : elles seules donnent l'état de l'interface, le ping, l'heure, le Wi-Fi ;
- les **sockets BSD de la libc** (`socket`, `connect`, `getaddrinfo`, `poll`...) : la couche POSIX les écrit **sur NetKit**, il n'y a pas d'autre pile. Un programme porté d'Unix garde son code (voir [Les sockets BSD](#les-sockets-bsd) et la page [libc](libc_fr.md)).

> **Remarque** : IPv4 seulement. Le TLS (HTTPS) n'est pas dans NetKit : c'est OpenSSL, lié au programme, par-dessus un socket ordinaire.

## Ouvrir le kit

```c
#include <netkit_abi.h>

if (!netkit_kit_open ())
	return 1;		/* SD0:/lib/netkit.so absent */
```

Rien à ajouter au lien : `pibuild` passe tous les `/gcc/lib/lib*.a`, et `ld` ne garde que les relais appelés. Chaque fonction prend des `long` : un pointeur se convertit, `(long) buf`. Le résultat est un `long` ; une valeur négative est une erreur (numéros de Linux : `-11` pour `E_AGAIN`, `-110` délai dépassé, `-19` pas de service Wi-Fi).

Les adresses IPv4 sont des entiers de 32 bits **dans l'ordre de l'hôte** : `10.0.2.15` vaut `0x0A00020F`. Les ports aussi (pas de `htons`).

## Adresses et état du réseau

| Fonction | Arguments | Résultat |
|---|---|---|
| `fmt_ip` | ip, tampon (16 octets suffisent) | écrit « a.b.c.d » suivi d'un 0 ; rend la fin du texte |
| `print_ip` | ip | écrit l'adresse sur la console du noyau (le journal, pas la sortie standard) |
| `wait_dhcp` | tampon netinfo de 32 octets | attend une adresse (20 s au plus) : 1 obtenue, 0 sinon ; le tampon est rempli |
| `net_info` | tampon netinfo de 32 octets | 0 ; rempli de zéros sans netd |
| `net_active` | tampon de 16 octets | 0 ; nom de l'interface active (8 octets) puis son type (32 bits, à +8) |
| `net_ifaces` | tampon, places | nombre d'interfaces copiées, `NIF_ENT` (64) octets chacune |
| `net_stats` | tampon de 152 octets | 0 ; les compteurs de netd et du pilote (ceux de `tcpbench`) |

Le tampon **netinfo** (constantes `NI_*` de `netkit_abi.h`) :

| Décalage | Constante | Contenu |
|---|---|---|
| 0 | `NI_IP` | adresse (32 bits ; 0 : pas d'adresse) |
| 4 | `NI_MASK` | masque |
| 8 | `NI_GW` | passerelle |
| 12 | `NI_DNS` | serveur DNS |
| 16 | `NI_MAC` | adresse MAC (6 octets) |
| 24 | `NI_UP` | 1 : lien monté |
| 28 | `NI_DHCP` | 4 : bail DHCP obtenu |

Une entrée de `net_ifaces` : nom (8 octets, `eth0`, `wlan0`, `usb0`), type à +8 (1 Ethernet, 2 Wi-Fi, 3 USB), lien à +12, adresse à +16 (seulement pour l'interface active), priorité à +20, débit en Mb/s à +24, active à +28, SSID à +32 (32 octets). Une seule interface a une adresse à la fois : celle qui a le lien et la plus haute priorité (Ethernet, puis Wi-Fi, puis USB).

« Le réseau est-il là ? » se teste comme le font Mail, IRC et Courier :

```c
unsigned char ni[32];
net_info ((long) ni);
int up = *(unsigned *) (ni + NI_IP) != 0;
```

## Sockets

| Fonction | Arguments | Résultat |
|---|---|---|
| `net_socket` | `SOCK_TCP` (1) ou `SOCK_UDP` (2) | numéro de socket (`sd`) |
| `net_connect` | sd, ip, port | 0 ; attend la connexion |
| `net_bind` | sd, port | 0 |
| `net_listen` | sd, file d'attente | 0 |
| `net_accept` | sd, délai en ms | sd de la connexion ; `E_AGAIN` (-11) au bout du délai |
| `net_send` | sd, tampon, octets | octets envoyés |
| `net_recv` | sd, tampon, max, délai en ms | octets reçus ; 0 : fermée par l'autre bout ; `E_AGAIN` : délai écoulé |
| `net_sendto` | sd, tampon, octets, ip, port | octets (UDP) |
| `net_recvfrom` | sd, tampon, max, délai, `unsigned src[2]` | comme `net_recv` ; `src[0]` ip, `src[1]` port de l'envoyeur |
| `net_shutdown` | sd | 0 ; FIN après ce qui reste à envoyer, on reçoit toujours |
| `net_close` | sd | ferme le socket |

Le délai est en millisecondes : 0 rend tout de suite, -1 attend sans fin. `net_recv` rend au plus 8 Ko par appel : lire en boucle. Les sockets d'un programme sont fermés par netd à sa fin. Plusieurs threads peuvent appeler NetKit en même temps.

## DNS, ping, heure

| Fonction | Arguments | Résultat |
|---|---|---|
| `net_resolve` | nom (ou « a.b.c.d ») | adresse IPv4 ; négatif : inconnu ou pas de réseau ; `localhost` : 127.0.0.1 |
| `net_ping` | ip, délai en ms | aller-retour en ms ; négatif : pas de réponse |
| `net_time` | `long t[3]` ou 0 | heure UTC (secondes depuis 1970 ; 0 : inconnue) ; `t[0]` l'heure, `t[1]` 1 si elle vient du NTP, `t[2]` instant de la synchronisation (ms depuis le démarrage) |
| `net_ntp_sync` | 1 : attendre (20 s au plus), 0 : demander | 0 quand l'heure est reprise |

Le Pi n'a pas d'horloge sauvegardée : avant le premier NTP, il se croit à l'heure de son dernier arrêt. Un programme qui vérifie des certificats attend donc `net_time` comme le fait l'assistant de premier démarrage :

```c
for (int i = 0; i < 3 && !net_time (0); i++)
	net_ntp_sync (1);
```

## Wi-Fi

Ces fonctions parlent au service `wifi`. Sans puce (sous QEMU, par exemple), elles rendent `-19`.

| Fonction | Arguments | Résultat |
|---|---|---|
| `wifi_scan` | tampon, places | nombre de réseaux vus (entrées de 48 octets) ; dure environ 3 s |
| `wifi_status` | tampon de 64 octets | 0 ; état (+0, 32 bits), RSSI en dBm (+4), MAC (+8), SSID (+16), longueur du SSID (+48) |
| `wifi_join` | SSID, clé (0 : réseau ouvert) | 0, ou `-2` inconnu, `-13` clé refusée, `-110` délai ; le réseau est mémorisé |
| `wifi_join_ex` | SSID, clé, mémoriser (0/1), priorité | comme `wifi_join` |
| `wifi_leave` | aucun | 0 ; plus de reconnexion automatique jusqu'au prochain `wifi_join` |
| `wifi_known` | tampon, places | nombre de réseaux connus, entrées de 48 octets : SSID (+0), sécurité (+36), priorité (+40) ; jamais les clés |
| `wifi_forget` | SSID | retire le réseau de `SD0:/etc/wifi.txt` |
| `wifi_priority` | SSID, priorité | le plus grand passe d'abord |
| `wifi_country` | code du pays (« FR ») | réappliqué tout de suite |

Une entrée de `wifi_scan` : SSID (+0, 32 octets, sans 0 final), longueur du SSID (+32), canal (+33), sécurité (+34 : 0 ouvert, 1 WEP, 2 WPA, 3 WPA2), RSSI en dBm (+36, 16 bits signés), BSSID (+38, 6 octets).

États de `wifi_status` : 0 absent, 1 démarrage, 2 prêt, 3 connexion en cours, 4 connecté.

`wifi_scan` et `wifi_join` prennent plusieurs secondes : dans une app graphique, appelez-les depuis un thread (`pthread_create`), jamais depuis la boucle d'événements.

## Exemple : l'état du réseau

Un programme pour le Terminal : interfaces, adresse, passerelle, heure du réseau, Wi-Fi.

```c
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <netkit_abi.h>

static const char *ip_text (unsigned ip, char *t)
{
	fmt_ip (ip, (long) t);
	return t;
}

int main (void)
{
	if (!netkit_kit_open ())
	{
		printf ("NetKit absent\n");
		return 1;
	}
	unsigned char ni[32];
	char a[20], b[20];
	net_info ((long) ni);
	unsigned ip = *(unsigned *) (ni + NI_IP);
	if (!ip)
		printf ("Pas d'adresse : le reseau n'est pas la.\n");
	else
	{
		printf ("adresse    %s / %s\n", ip_text (ip, a), ip_text (*(unsigned *) (ni + NI_MASK), b));
		printf ("passerelle %s\n", ip_text (*(unsigned *) (ni + NI_GW), a));
		printf ("DNS        %s\n", ip_text (*(unsigned *) (ni + NI_DNS), a));
	}
	unsigned char act[16];
	net_active ((long) act);
	printf ("interface active : %.8s\n", act[0] ? (char *) act : "(aucune)");

	unsigned char ifs[8][64];
	long n = net_ifaces ((long) ifs, 8);
	for (long i = 0; i < n; i++)
	{
		unsigned type = *(unsigned *) (ifs[i] + 8), link = *(unsigned *) (ifs[i] + 12);
		printf ("  %-6.8s %s, lien %s\n", (char *) ifs[i],
			type == 1 ? "Ethernet" : type == 2 ? "Wi-Fi" : "USB", link ? "monte" : "coupe");
	}

	long t[3];
	if (net_time ((long) t))
	{
		time_t s = (time_t) t[0];
		printf ("heure UTC  %s", asctime (gmtime (&s)));
		printf ("           %s\n", t[1] ? "(par NTP)" : "(pas encore synchronisee)");
	}

	unsigned char ws[64];
	if (wifi_status ((long) ws) == 0)
	{
		int st = *(int *) ws, rssi = *(int *) (ws + 4), len = ws[48];
		if (st == 4)
			printf ("Wi-Fi      %.*s, %d dBm\n", len < 32 ? len : 32, (char *) ws + 16, rssi);
		else
			printf ("Wi-Fi      non connecte (etat %d)\n", st);
	}
	else
		printf ("Wi-Fi      pas de puce\n");
	return 0;
}
```

## Exemple : ping

Résout un nom, puis envoie quatre pings. `ping example.com` dans le Terminal.

```c
#include <stdio.h>
#include <unistd.h>
#include <netkit_abi.h>

int main (int argc, char **argv)
{
	const char *host = argc > 1 ? argv[1] : "example.com";
	if (!netkit_kit_open ())
		return 1;
	long ip = net_resolve ((long) host);
	if (ip <= 0)
	{
		printf ("%s : hote inconnu (%ld)\n", host, ip);
		return 1;
	}
	char t[20];
	fmt_ip (ip, (long) t);
	printf ("%s -> %s\n", host, t);
	int ok = 0;
	for (int i = 0; i < 4; i++)
	{
		long ms = net_ping (ip, 2000);
		if (ms >= 0)
		{
			printf ("reponse en %ld ms\n", ms);
			ok++;
		}
		else
			printf ("pas de reponse (%ld)\n", ms);
		usleep (300 * 1000);
	}
	printf ("%d sur 4 recus\n", ok);
	return ok ? 0 : 1;
}
```

## Exemple : une requête HTTP par NetKit

Une connexion TCP, une requête, la réponse lue jusqu'à la fermeture. HTTP en clair seulement : pour HTTPS, OpenSSL par-dessus un socket BSD.

```c
#include <stdio.h>
#include <string.h>
#include <netkit_abi.h>

int main (int argc, char **argv)
{
	const char *host = argc > 1 ? argv[1] : "example.com";
	if (!netkit_kit_open ())
		return 1;
	long ip = net_resolve ((long) host);
	if (ip <= 0)
	{
		printf ("hote inconnu\n");
		return 1;
	}
	long sd = net_socket (SOCK_TCP);
	if (sd < 0 || net_connect (sd, ip, 80) != 0)
	{
		printf ("connexion refusee\n");
		if (sd >= 0)
			net_close (sd);
		return 1;
	}
	char req[256];
	int n = snprintf (req, sizeof req, "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
	net_send (sd, (long) req, n);

	static char buf[8192];
	long total = 0, r;
	while ((r = net_recv (sd, (long) buf, sizeof buf - 1, 10000)) > 0)
	{
		if (total == 0)
		{
			buf[r] = 0;
			char *eol = strstr (buf, "\r\n");
			if (eol)
				*eol = 0;
			printf ("%s\n", buf);		/* la ligne d'etat : HTTP/1.1 200 OK */
		}
		total += r;
	}
	printf ("%ld octets recus%s\n", total, r < 0 ? " (delai depasse)" : "");
	net_close (sd);
	return 0;
}
```

## Les sockets BSD

La libc de Korin OS (couche POSIX, `libposix`) écrit les sockets BSD sur NetKit : `socket (AF_INET, SOCK_STREAM | SOCK_DGRAM, 0)`, `connect`, `bind`, `listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `shutdown`, `close`, `poll`, `select`, `getaddrinfo` (par `net_resolve`), `inet_pton`, `htons`. Le programme n'a pas à ouvrir NetKit : la libc le fait au premier socket.

Ce qui diffère d'un Unix : IPv4 seulement (`AF_INET6` rend `EAFNOSUPPORT`) ; `connect` attend toujours la connexion, même sur un socket non bloquant ; l'adresse de l'autre bout d'un `accept` n'est pas connue (0.0.0.0:0) ; pas de `SIGPIPE` (`send` rend `EPIPE`) ; `getifaddrs` rend `ENOSYS` : pour l'état de l'interface, `net_info`.

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

int main (void)
{
	struct addrinfo hints, *res;
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	int e = getaddrinfo ("example.com", "80", &hints, &res);
	if (e != 0)
	{
		printf ("getaddrinfo : %s\n", gai_strerror (e));
		return 1;
	}
	int s = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s < 0 || connect (s, res->ai_addr, res->ai_addrlen) < 0)
	{
		perror ("connect");
		freeaddrinfo (res);
		return 1;
	}
	freeaddrinfo (res);
	const char *req = "HEAD / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
	send (s, req, strlen (req), 0);
	char buf[1024];
	ssize_t n;
	while ((n = recv (s, buf, sizeof buf, 0)) > 0)
		fwrite (buf, 1, (size_t) n, stdout);
	close (s);
	return 0;
}
```

## Pièges

- **Ordre des octets** : NetKit prend et rend les adresses dans l'ordre de l'hôte ; les sockets BSD, dans l'ordre du réseau (`htonl`, `inet_pton`). Ne pas mélanger.
- **`print_ip` écrit dans le journal**, pas dans le Terminal : pour afficher, `fmt_ip` puis `printf`.
- **Les appels attendent** : `net_connect`, `net_resolve`, `net_recv` avec un délai, `wifi_scan`, `wifi_join`. Dans une app graphique, un thread à part, ou un délai court (`net_recv` avec 0 dans la boucle d'événements).
- **Pas de réseau, pas d'erreur fatale** : sans netd, `net_info` rend des zéros et les autres fonctions une valeur négative. Testez `net_info` avant de proposer une action réseau.
- **Le Wi-Fi sous QEMU** n'existe pas : `wifi_*` rendent `-19`. Le réseau, lui, passe par l'USB émulé (`usb0`).

## Voir aussi

- [libc et couche POSIX](libc_fr.md) : sockets BSD, threads, fichiers.
- [Écrire un kit](kit_fr.md), [IPC](ipc_fr.md) : comment un kit parle à un service.
- [UIKit](uikit_fr.md) : pour une app graphique qui montre l'état du réseau.
