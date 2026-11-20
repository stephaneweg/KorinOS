/* hello.c -- premier programme d'essai de win16 (docs/etude/WIN16.md) : une fenêtre Win16, du texte, un rectangle.
 * Construit par Open Watcom v2 (tools/win16/build-tests.sh). */
#include <windows.h>

static char szClass[] = "KorinHello";

LRESULT CALLBACK WndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;
	RECT rc;
	switch (msg) {
	case WM_PAINT:
		hdc = BeginPaint (hwnd, &ps);
		GetClientRect (hwnd, &rc);
		Rectangle (hdc, 10, 10, rc.right - 10, rc.bottom - 10);
		TextOut (hdc, 30, 30, "Hello from Windows 3.x!", 23);
		TextOut (hdc, 30, 50, "Running on Korin OS.", 20);
		EndPaint (hwnd, &ps);
		return 0;
	case WM_DESTROY:
		PostQuitMessage (0);
		return 0;
	}
	return DefWindowProc (hwnd, msg, wParam, lParam);
}

int PASCAL WinMain (HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
	WNDCLASS wc;
	HWND hwnd;
	MSG msg;
	(void) lpCmd;
	if (!hPrev) {
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hInst;
		wc.hIcon = LoadIcon (NULL, IDI_APPLICATION);
		wc.hCursor = LoadCursor (NULL, IDC_ARROW);
		wc.hbrBackground = GetStockObject (WHITE_BRUSH);
		wc.lpszMenuName = NULL;
		wc.lpszClassName = szClass;
		if (!RegisterClass (&wc))
			return 0;
	}
	hwnd = CreateWindow (szClass, "Hello Win16", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 360, 160,
		NULL, NULL, hInst, NULL);
	ShowWindow (hwnd, nShow);
	UpdateWindow (hwnd);
	while (GetMessage (&msg, NULL, 0, 0)) {
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}
	return msg.wParam;
}
