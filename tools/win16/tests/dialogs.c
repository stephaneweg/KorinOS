/* dialogs.c -- programme d'essai de l'étape 3 de win16 (docs/etude/WIN16.md) : menu, accélérateurs, minuterie,
 * dialogues tirés des ressources (texte, bouton, case, boutons radio, liste, liste déroulante, champ). */
#include <windows.h>
#include <string.h>
#include "dialogs.h"

static char szClass[] = "KorinDialogs";
static HINSTANCE hInst;
static char name[64] = "nobody";
static char fruit[32] = "none";
static int loud, size = IDC_MEDIUM, ticks, counting = 1;

BOOL FAR PASCAL AboutProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	(void) lParam;
	if (msg == WM_INITDIALOG)
		return TRUE;
	if (msg == WM_COMMAND && (wParam == IDOK || wParam == IDCANCEL)) {
		EndDialog (hDlg, TRUE);
		return TRUE;
	}
	return FALSE;
}

BOOL FAR PASCAL InputProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static const char *colors[] = { "Red", "Green", "Blue", "Cyan", "Magenta", "Yellow" };
	static const char *fruits[] = { "Apple", "Banana", "Cherry" };
	int i;
	(void) lParam;
	switch (msg) {
	case WM_INITDIALOG:
		SetDlgItemText (hDlg, IDC_NAME, name);
		CheckDlgButton (hDlg, IDC_LOUD, loud);
		CheckRadioButton (hDlg, IDC_SMALL, IDC_LARGE, size);
		for (i = 0; i < 6; i++)
			SendDlgItemMessage (hDlg, IDC_COLORS, LB_ADDSTRING, 0, (LPARAM) (LPSTR) colors[i]);
		SendDlgItemMessage (hDlg, IDC_COLORS, LB_SETCURSEL, 2, 0);
		for (i = 0; i < 3; i++)
			SendDlgItemMessage (hDlg, IDC_FRUIT, CB_ADDSTRING, 0, (LPARAM) (LPSTR) fruits[i]);
		SendDlgItemMessage (hDlg, IDC_FRUIT, CB_SETCURSEL, 1, 0);
		SetFocus (GetDlgItem (hDlg, IDC_NAME));
		return FALSE;
	case WM_COMMAND:
		if (wParam == IDOK) {
			GetDlgItemText (hDlg, IDC_NAME, name, sizeof name);
			loud = IsDlgButtonChecked (hDlg, IDC_LOUD);
			for (i = IDC_SMALL; i <= IDC_LARGE; i++)
				if (IsDlgButtonChecked (hDlg, i))
					size = i;
			i = (int) SendDlgItemMessage (hDlg, IDC_FRUIT, CB_GETCURSEL, 0, 0);
			if (i >= 0)
				SendDlgItemMessage (hDlg, IDC_FRUIT, CB_GETLBTEXT, i, (LPARAM) (LPSTR) fruit);
			EndDialog (hDlg, TRUE);
			return TRUE;
		}
		if (wParam == IDCANCEL) {
			EndDialog (hDlg, FALSE);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

LRESULT CALLBACK WndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;
	char fmt[80], buf[160];
	FARPROC proc;
	HMENU menu;
	switch (msg) {
	case WM_CREATE:
		SetTimer (hwnd, 1, 500, NULL);
		return 0;
	case WM_TIMER:
		if (counting) {
			ticks++;
			InvalidateRect (hwnd, NULL, TRUE);
		}
		return 0;
	case WM_COMMAND:
		switch (wParam) {
		case IDM_INPUT:
			proc = MakeProcInstance ((FARPROC) InputProc, hInst);
			DialogBox (hInst, MAKEINTRESOURCE (IDD_INPUT), hwnd, (DLGPROC) proc);
			FreeProcInstance (proc);
			InvalidateRect (hwnd, NULL, TRUE);
			return 0;
		case IDM_ABOUT:
			proc = MakeProcInstance ((FARPROC) AboutProc, hInst);
			DialogBox (hInst, MAKEINTRESOURCE (IDD_ABOUT), hwnd, (DLGPROC) proc);
			FreeProcInstance (proc);
			return 0;
		case IDM_COUNT:
			counting = !counting;
			menu = GetMenu (hwnd);
			CheckMenuItem (menu, IDM_COUNT, counting ? MF_CHECKED : MF_UNCHECKED);
			return 0;
		case IDM_RESET:
			ticks = 0;
			InvalidateRect (hwnd, NULL, TRUE);
			return 0;
		case IDM_EXIT:
			DestroyWindow (hwnd);
			return 0;
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint (hwnd, &ps);
		LoadString (hInst, IDS_HELLO, fmt, sizeof fmt);
		wsprintf (buf, fmt, (LPSTR) name, size - IDC_SMALL + 1, (LPSTR) fruit, (LPSTR) (loud ? "!!!" : ""));
		TextOut (hdc, 10, 10, buf, lstrlen (buf));
		wsprintf (buf, "Timer ticks: %d", ticks);
		TextOut (hdc, 10, 30, buf, lstrlen (buf));
		EndPaint (hwnd, &ps);
		return 0;
	case WM_DESTROY:
		KillTimer (hwnd, 1);
		PostQuitMessage (0);
		return 0;
	}
	return DefWindowProc (hwnd, msg, wParam, lParam);
}

int PASCAL WinMain (HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
	WNDCLASS wc;
	HWND hwnd;
	MSG msg;
	HACCEL accel;
	(void) lpCmd;
	hInst = hInstance;
	if (!hPrev) {
		memset (&wc, 0, sizeof wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.hInstance = hInstance;
		wc.hIcon = LoadIcon (NULL, IDI_APPLICATION);
		wc.hCursor = LoadCursor (NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
		wc.lpszMenuName = "MainMenu";
		wc.lpszClassName = szClass;
		if (!RegisterClass (&wc))
			return 0;
	}
	hwnd = CreateWindow (szClass, "Win16 dialogs", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 400, 160,
		NULL, NULL, hInstance, NULL);
	ShowWindow (hwnd, nShow);
	UpdateWindow (hwnd);
	accel = LoadAccelerators (hInstance, "MainAccel");
	while (GetMessage (&msg, NULL, 0, 0)) {
		if (!TranslateAccelerator (hwnd, accel, &msg)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
	return msg.wParam;
}
