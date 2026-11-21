/* wsock.c -- essai de WINSOCK.DLL dans win16 (Open Watcom : tools/win16/build-tests.sh).
 *
 *   wsock.exe [port [adresse]]    (127.0.0.1, port 7777 par défaut : un serveur d'écho, tools/win16/tests/echo.py ;
 *                                  sous QEMU, l'hôte est 10.0.2.2)
 *
 * 1. bloquant : WSAStartup, gethostbyname ("localhost"), socket, connect, send, recv, closesocket ;
 * 2. asynchrone : WSAAsyncSelect (FD_CONNECT, FD_READ, FD_CLOSE), connect non bloquant, send au FD_CONNECT,
 *    recv au FD_READ, fin au FD_CLOSE ;
 * puis une boîte de message avec le bilan (« OK » ou ce qui a manqué). */
#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WM_SOCK (WM_USER + 1)

static char report[512];
static unsigned short port = 7777;
static char host[32] = "127.0.0.1";
static SOCKET as = INVALID_SOCKET;
static char abuf[128];
static int alen, ok_connect, ok_read, ok_close;

static void add (const char *s)
{
	if (strlen (report) + strlen (s) + 2 < sizeof report)
	{
		strcat (report, s);
		strcat (report, "\n");
	}
}

static int blocking_test (void)
{
	char line[160], buf[128];
	struct hostent FAR *h = gethostbyname ("localhost");
	struct sockaddr_in a;
	SOCKET s;
	int n;
	if (!h)
	{
		sprintf (line, "gethostbyname : erreur %d", WSAGetLastError ());
		add (line);
		return 0;
	}
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons (port);
	a.sin_addr.s_addr = inet_addr (host);
	/* (modèle small : les chaînes lointaines de Winsock copiées dans un tampon proche avant sprintf) */
	_fstrcpy (buf, inet_ntoa (*(struct in_addr FAR *) h->h_addr_list[0]));
	sprintf (line, "localhost = %s", buf);
	add (line);
	s = socket (AF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET || connect (s, (struct sockaddr FAR *) &a, sizeof a) == SOCKET_ERROR)
	{
		sprintf (line, "connect : erreur %d", WSAGetLastError ());
		add (line);
		return 0;
	}
	send (s, "bloquant", 8, 0);
	n = recv (s, buf, sizeof buf - 1, 0);
	closesocket (s);
	if (n <= 0)
	{
		sprintf (line, "recv : %d, erreur %d", n, WSAGetLastError ());
		add (line);
		return 0;
	}
	buf[n] = 0;
	sprintf (line, "bloquant : %s", buf);
	add (line);
	return strcmp (buf, "ECHO:bloquant") == 0;
}

static void finish (HWND w)
{
	char line[160];
	sprintf (line, "asynchrone : connect %d, read %d, close %d : %s", ok_connect, ok_read, ok_close, abuf);
	add (line);
	if (as != INVALID_SOCKET)
		closesocket (as);
	as = INVALID_SOCKET;
	DestroyWindow (w);
}

LRESULT CALLBACK WndProc (HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	if (m == WM_SOCK)
	{
		int ev = WSAGETSELECTEVENT (lp), err = WSAGETSELECTERROR (lp);
		if (err)
		{
			char line[80];
			sprintf (line, "WSAAsyncSelect : evenement %d, erreur %d", ev, err);
			add (line);
			finish (w);
			return 0;
		}
		if (ev == FD_CONNECT)
		{
			ok_connect = 1;
			send (as, "asynchrone", 10, 0);
		}
		else if (ev == FD_READ)
		{
			int n = recv (as, abuf + alen, sizeof abuf - 1 - alen, 0);
			if (n > 0)
			{
				alen += n;
				abuf[alen] = 0;
				ok_read = 1;
			}
		}
		else if (ev == FD_CLOSE)
		{
			ok_close = 1;
			finish (w);
		}
		return 0;
	}
	if (m == WM_TIMER)
	{
		add ("asynchrone : delai depasse");
		finish (w);
		return 0;
	}
	if (m == WM_DESTROY)
	{
		PostQuitMessage (0);
		return 0;
	}
	return DefWindowProc (w, m, wp, lp);
}

int PASCAL WinMain (HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
	WSADATA d;
	WNDCLASS wc;
	HWND w;
	MSG msg;
	struct sockaddr_in a;
	int good;
	if (cmd && *cmd)
	{
		char t[48];
		_fstrncpy (t, cmd, sizeof t - 1);
		t[sizeof t - 1] = 0;
		port = (unsigned short) atoi (t);
		if (strchr (t, ' '))
		{
			_fstrncpy (host, _fstrchr (cmd, ' ') + 1, sizeof host - 1);
			host[sizeof host - 1] = 0;
		}
	}
	if (WSAStartup (0x0101, &d))
	{
		MessageBox (0, "WSAStartup a echoue", "wsock", MB_OK);
		return 1;
	}
	{
		char t[260];
		_fstrcpy (t, d.szDescription);
		add (t);
	}
	good = blocking_test ();
	memset (&wc, 0, sizeof wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.lpszClassName = "wsock";
	RegisterClass (&wc);
	w = CreateWindow ("wsock", "wsock", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, inst, 0);
	as = socket (AF_INET, SOCK_STREAM, 0);
	WSAAsyncSelect (as, w, WM_SOCK, FD_CONNECT | FD_READ | FD_CLOSE);
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons (port);
	a.sin_addr.s_addr = inet_addr (host);
	if (connect (as, (struct sockaddr FAR *) &a, sizeof a) == SOCKET_ERROR && WSAGetLastError () != WSAEWOULDBLOCK)
		add ("connect asynchrone : erreur");
	SetTimer (w, 1, 5000, 0);
	while (GetMessage (&msg, 0, 0, 0))
	{
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}
	good = good && ok_connect && ok_read && ok_close && strcmp (abuf, "ECHO:asynchrone") == 0;
	add (good ? "OK" : "ECHEC");
	MessageBox (0, report, "wsock", MB_OK);
	WSACleanup ();
	return good ? 0 : 1;
}
