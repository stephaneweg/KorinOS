/* msgbox.c -- le plus petit programme d'essai de win16 : une boîte de message. */
#include <windows.h>

int PASCAL WinMain (HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
	(void) hInst; (void) hPrev; (void) lpCmd; (void) nShow;
	MessageBox (NULL, "Hello from a Win16 program!", "win16", MB_OK);
	return 0;
}
