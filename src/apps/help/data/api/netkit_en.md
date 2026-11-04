# NetKit: networking

**NetKit** gives programs the network: TCP and UDP sockets, DNS, ping, interface state, network time (NTP) and Wi-Fi. The kit holds no network stack itself: each call is a request to the `net` service (**netd**, the IPv4 stack) or to the `wifi` service (the Wi-Fi chip driver). The kit is written in assembly, lives in `SD0:/lib/netkit.so` and is shared by the whole system.

Two roads lead to the network from C:

- the **NetKit functions** (`net_*`, `wifi_*`) described here: only they give the interface state, ping, time and Wi-Fi;
- the **BSD sockets of the libc** (`socket`, `connect`, `getaddrinfo`, `poll`...): the POSIX layer implements them **on top of NetKit**; there is no other stack. A program ported from Unix keeps its code (see [BSD sockets](#bsd-sockets) and the [libc](libc_en.md) page).

> **Note**: IPv4 only. TLS (HTTPS) is not in NetKit: it is OpenSSL, linked into the program, on top of an ordinary socket.

## Opening the kit

```c
#include <netkit_abi.h>

if (!netkit_kit_open ())
	return 1;		/* SD0:/lib/netkit.so missing */
```

Nothing to add to the link: `pibuild` passes every `/gcc/lib/lib*.a`, and `ld` keeps only the stubs that are called. Every function takes `long` arguments: convert a pointer, `(long) buf`. The result is a `long`; a negative value is an error (Linux numbers: `-11` for `E_AGAIN`, `-110` timed out, `-19` no Wi-Fi service).

IPv4 addresses are 32-bit integers **in host byte order**: `10.0.2.15` is `0x0A00020F`. Ports too (no `htons`).

## Addresses and network state

| Function | Arguments | Result |
|---|---|---|
| `fmt_ip` | ip, buffer (16 bytes are enough) | writes "a.b.c.d" followed by a 0; returns the end of the text |
| `print_ip` | ip | writes the address on the kernel console (the log, not standard output) |
| `wait_dhcp` | 32-byte netinfo buffer | waits for an address (20 s at most): 1 obtained, 0 otherwise; the buffer is filled |
| `net_info` | 32-byte netinfo buffer | 0; filled with zeros without netd |
| `net_active` | 16-byte buffer | 0; name of the active interface (8 bytes) then its type (32 bits, at +8) |
| `net_ifaces` | buffer, slots | number of interfaces copied, `NIF_ENT` (64) bytes each |
| `net_stats` | 152-byte buffer | 0; the counters of netd and of the driver (those of `tcpbench`) |

The **netinfo** buffer (`NI_*` constants of `netkit_abi.h`):

| Offset | Constant | Content |
|---|---|---|
| 0 | `NI_IP` | address (32 bits; 0: no address) |
| 4 | `NI_MASK` | mask |
| 8 | `NI_GW` | gateway |
| 12 | `NI_DNS` | DNS server |
| 16 | `NI_MAC` | MAC address (6 bytes) |
| 24 | `NI_UP` | 1: link up |
| 28 | `NI_DHCP` | 4: DHCP lease obtained |

A `net_ifaces` entry: name (8 bytes, `eth0`, `wlan0`, `usb0`), type at +8 (1 Ethernet, 2 Wi-Fi, 3 USB), link at +12, address at +16 (only for the active interface), priority at +20, speed in Mb/s at +24, active at +28, SSID at +32 (32 bytes). Only one interface has an address at a time: the one with a link and the highest priority (Ethernet, then Wi-Fi, then USB).

"Is the network there?" is tested the way Mail, IRC and Courier do it:

```c
unsigned char ni[32];
net_info ((long) ni);
int up = *(unsigned *) (ni + NI_IP) != 0;
```

## Sockets

| Function | Arguments | Result |
|---|---|---|
| `net_socket` | `SOCK_TCP` (1) or `SOCK_UDP` (2) | socket number (`sd`) |
| `net_connect` | sd, ip, port | 0; waits for the connection |
| `net_bind` | sd, port | 0 |
| `net_listen` | sd, backlog | 0 |
| `net_accept` | sd, timeout in ms | sd of the connection; `E_AGAIN` (-11) when the timeout expires |
| `net_send` | sd, buffer, bytes | bytes sent |
| `net_recv` | sd, buffer, max, timeout in ms | bytes received; 0: closed by the other end; `E_AGAIN`: timed out |
| `net_sendto` | sd, buffer, bytes, ip, port | bytes (UDP) |
| `net_recvfrom` | sd, buffer, max, timeout, `unsigned src[2]` | like `net_recv`; `src[0]` ip, `src[1]` port of the sender |
| `net_shutdown` | sd | 0; FIN after what is left to send, receiving goes on |
| `net_close` | sd | closes the socket |

Timeouts are in milliseconds: 0 returns at once, -1 waits forever. `net_recv` returns at most 8 KB per call: read in a loop. netd closes a program's sockets when it ends. Several threads may call NetKit at the same time.

## DNS, ping, time

| Function | Arguments | Result |
|---|---|---|
| `net_resolve` | name (or "a.b.c.d") | IPv4 address; negative: unknown or no network; `localhost`: 127.0.0.1 |
| `net_ping` | ip, timeout in ms | round trip in ms; negative: no answer |
| `net_time` | `long t[3]` or 0 | UTC time (seconds since 1970; 0: unknown); `t[0]` the time, `t[1]` 1 if it comes from NTP, `t[2]` when it was synchronised (ms since boot) |
| `net_ntp_sync` | 1: wait (20 s at most), 0: request | 0 once the time is set |

The Pi has no battery-backed clock: before the first NTP it believes it is the time it was last shut down. A program that checks certificates therefore waits for `net_time`, as the first-boot setup does:

```c
for (int i = 0; i < 3 && !net_time (0); i++)
	net_ntp_sync (1);
```

## Wi-Fi

These functions talk to the `wifi` service. Without a chip (under QEMU, for example), they return `-19`.

| Function | Arguments | Result |
|---|---|---|
| `wifi_scan` | buffer, slots | number of networks seen (48-byte entries); takes about 3 s |
| `wifi_status` | 64-byte buffer | 0; state (+0, 32 bits), RSSI in dBm (+4), MAC (+8), SSID (+16), SSID length (+48) |
| `wifi_join` | SSID, key (0: open network) | 0, or `-2` unknown, `-13` key refused, `-110` timed out; the network is remembered |
| `wifi_join_ex` | SSID, key, remember (0/1), priority | like `wifi_join` |
| `wifi_leave` | none | 0; no automatic reconnection until the next `wifi_join` |
| `wifi_known` | buffer, slots | number of known networks, 48-byte entries: SSID (+0), security (+36), priority (+40); never the keys |
| `wifi_forget` | SSID | removes the network from `SD0:/etc/wifi.txt` |
| `wifi_priority` | SSID, priority | the highest goes first |
| `wifi_country` | country code ("FR") | applied at once |

A `wifi_scan` entry: SSID (+0, 32 bytes, no final 0), SSID length (+32), channel (+33), security (+34: 0 open, 1 WEP, 2 WPA, 3 WPA2), RSSI in dBm (+36, signed 16 bits), BSSID (+38, 6 bytes).

`wifi_status` states: 0 absent, 1 starting, 2 ready, 3 joining, 4 connected.

`wifi_scan` and `wifi_join` take several seconds: in a graphical app, call them from a thread (`pthread_create`), never from the event loop.

## Example: network state

A program for the Terminal: interfaces, address, gateway, network time, Wi-Fi.

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
		printf ("NetKit missing\n");
		return 1;
	}
	unsigned char ni[32];
	char a[20], b[20];
	net_info ((long) ni);
	unsigned ip = *(unsigned *) (ni + NI_IP);
	if (!ip)
		printf ("No address: the network is not there.\n");
	else
	{
		printf ("address    %s / %s\n", ip_text (ip, a), ip_text (*(unsigned *) (ni + NI_MASK), b));
		printf ("gateway    %s\n", ip_text (*(unsigned *) (ni + NI_GW), a));
		printf ("DNS        %s\n", ip_text (*(unsigned *) (ni + NI_DNS), a));
	}
	unsigned char act[16];
	net_active ((long) act);
	printf ("active interface: %.8s\n", act[0] ? (char *) act : "(none)");

	unsigned char ifs[8][64];
	long n = net_ifaces ((long) ifs, 8);
	for (long i = 0; i < n; i++)
	{
		unsigned type = *(unsigned *) (ifs[i] + 8), link = *(unsigned *) (ifs[i] + 12);
		printf ("  %-6.8s %s, link %s\n", (char *) ifs[i],
			type == 1 ? "Ethernet" : type == 2 ? "Wi-Fi" : "USB", link ? "up" : "down");
	}

	long t[3];
	if (net_time ((long) t))
	{
		time_t s = (time_t) t[0];
		printf ("UTC time   %s", asctime (gmtime (&s)));
		printf ("           %s\n", t[1] ? "(from NTP)" : "(not synchronised yet)");
	}

	unsigned char ws[64];
	if (wifi_status ((long) ws) == 0)
	{
		int st = *(int *) ws, rssi = *(int *) (ws + 4), len = ws[48];
		if (st == 4)
			printf ("Wi-Fi      %.*s, %d dBm\n", len < 32 ? len : 32, (char *) ws + 16, rssi);
		else
			printf ("Wi-Fi      not connected (state %d)\n", st);
	}
	else
		printf ("Wi-Fi      no chip\n");
	return 0;
}
```

## Example: ping

Resolves a name, then sends four pings. `ping example.com` in the Terminal.

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
		printf ("%s: unknown host (%ld)\n", host, ip);
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
			printf ("reply in %ld ms\n", ms);
			ok++;
		}
		else
			printf ("no reply (%ld)\n", ms);
		usleep (300 * 1000);
	}
	printf ("%d of 4 received\n", ok);
	return ok ? 0 : 1;
}
```

## Example: an HTTP request through NetKit

A TCP connection, a request, the response read until the server closes. Plain HTTP only: for HTTPS, OpenSSL on top of a BSD socket.

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
		printf ("unknown host\n");
		return 1;
	}
	long sd = net_socket (SOCK_TCP);
	if (sd < 0 || net_connect (sd, ip, 80) != 0)
	{
		printf ("connection refused\n");
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
			printf ("%s\n", buf);		/* the status line: HTTP/1.1 200 OK */
		}
		total += r;
	}
	printf ("%ld bytes received%s\n", total, r < 0 ? " (timed out)" : "");
	net_close (sd);
	return 0;
}
```

## BSD sockets

The Korin OS libc (POSIX layer, `libposix`) implements BSD sockets on NetKit: `socket (AF_INET, SOCK_STREAM | SOCK_DGRAM, 0)`, `connect`, `bind`, `listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `shutdown`, `close`, `poll`, `select`, `getaddrinfo` (through `net_resolve`), `inet_pton`, `htons`. The program does not need to open NetKit: the libc does it at the first socket.

What differs from a Unix: IPv4 only (`AF_INET6` gives `EAFNOSUPPORT`); `connect` always waits for the connection, even on a non-blocking socket; the peer address of an `accept` is not known (0.0.0.0:0); no `SIGPIPE` (`send` returns `EPIPE`); `getifaddrs` returns `ENOSYS`: for the interface state, use `net_info`.

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
		printf ("getaddrinfo: %s\n", gai_strerror (e));
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

## Pitfalls

- **Byte order**: NetKit takes and returns addresses in host order; BSD sockets in network order (`htonl`, `inet_pton`). Do not mix them.
- **`print_ip` writes to the log**, not to the Terminal: to display, `fmt_ip` then `printf`.
- **Calls wait**: `net_connect`, `net_resolve`, `net_recv` with a timeout, `wifi_scan`, `wifi_join`. In a graphical app, use a separate thread, or a short timeout (`net_recv` with 0 in the event loop).
- **No network is not fatal**: without netd, `net_info` returns zeros and the other functions a negative value. Test `net_info` before offering a network action.
- **Wi-Fi under QEMU** does not exist: `wifi_*` return `-19`. The network itself goes through emulated USB (`usb0`).

## See also

- [libc and POSIX layer](libc_en.md): BSD sockets, threads, files.
- [Writing a kit](kit_en.md), [IPC](ipc_en.md): how a kit talks to a service.
- [UIKit](uikit_en.md): for a graphical app that shows the network state.
