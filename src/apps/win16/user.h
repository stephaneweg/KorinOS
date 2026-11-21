/* user.h -- win16 : ce que USER partage avec GDI et les contrôles (ctrl.c) */
#ifndef W16_USER_H
#define W16_USER_H
#include <stdint.h>

/* les mesures de Windows 3.1 (GetSystemMetrics) : le programme calcule ses tailles avec elles */
#define W16_CAPTION 20
#define W16_FRAME 4
#define W16_MENU 18
#define W16_SCROLL 17

/* messages */
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000a
#define WM_SETREDRAW 0x000b
#define WM_SETTEXT 0x000c
#define WM_GETTEXT 0x000d
#define WM_GETTEXTLENGTH 0x000e
#define WM_PAINT 0x000f
#define WM_CLOSE 0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SHOWWINDOW 0x0018
#define WM_CTLCOLOR 0x0019
#define WM_ACTIVATEAPP 0x001c
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_NEXTDLGCTL 0x0028
#define WM_DRAWITEM 0x002b
#define WM_MEASUREITEM 0x002c
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_GETDLGCODE 0x0087
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_INITMENU 0x0116
#define WM_INITMENUPOPUP 0x0117
#define WM_MENUSELECT 0x011f
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_USER 0x0400

#define WS_CHILD 0x40000000u
#define WS_POPUP 0x80000000u
#define WS_VISIBLE 0x10000000u
#define WS_DISABLED 0x08000000u
#define WS_CAPTION 0x00c00000u
#define WS_BORDER 0x00800000u
#define WS_DLGFRAME 0x00400000u
#define WS_VSCROLL 0x00200000u
#define WS_HSCROLL 0x00100000u
#define WS_SYSMENU 0x00080000u
#define WS_THICKFRAME 0x00040000u
#define WS_GROUP 0x00020000u
#define WS_TABSTOP 0x00010000u

/* une fenêtre (USER) */
struct wnd
{
	int used;
	uint16_t hwnd, parent, owner, id, hinst, menu, font;
	int cls;
	uint32_t proc, style, exstyle;
	int x, y, w, h;		/* le rectangle de la fenêtre, dans la zone cliente du parent (écran pour une principale) */
	int cx, cy, cw, ch;	/* la zone cliente, dans le rectangle de la fenêtre */
	char text[256];		/* le titre (CP1252, comme le programme) */
	long be;
	int visible, destroyed, disabled, dialog, dlg_end, dlg_result;
	int need_size;		/* WM_SIZE et WM_MOVE attendent le premier affichage (une fenêtre principale) */
	int invalid, erase, ix0, iy0, ix1, iy1;	/* la zone à repeindre (zone cliente) */
	uint8_t extra[64];
	int nprops;
	struct { uint16_t atom; uint16_t value; char name[32]; } props[16];
	int sb_min[2], sb_max[2], sb_pos[2];	/* barres de défilement de la fenêtre (0 horizontale, 1 verticale) */
	void *cdata;		/* l'état d'un contrôle (ctrl.c) */
	int cdata_kind;		/* sa classe du système : 1 BUTTON, 2 STATIC, 3 EDIT, 4 LISTBOX, 5 COMBOBOX, 6 SCROLLBAR */
	char clsname[32];
};

struct wnd *wnd_get (uint16_t hwnd);
int wnd_client_geometry (uint16_t hwnd, int *ox, int *oy, int *cw, int *ch, long *be);
uint32_t user_sys_color (int index);	/* COLORREF */
uint32_t user_send (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);
int user_post (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);
void user_client_origin (uint16_t hwnd, int *x, int *y);
void user_invalidate (uint16_t hwnd, int erase);
uint16_t user_set_focus (uint16_t hwnd);
uint16_t user_get_focus (void);
void user_capture (uint16_t hwnd);
void user_notify (uint16_t hwnd, uint16_t code);	/* WM_COMMAND au parent */
uint16_t user_ctl_brush (uint16_t hwnd, uint16_t hdc, int type);	/* WM_CTLCOLOR */
uint16_t user_begin (uint16_t hwnd);	/* un DC pour peindre un contrôle (valide la fenêtre) */
void user_end (uint16_t hdc);
int user_popup (uint16_t hwnd, int x, int y, const char *labels, int w);
uint16_t user_create (uint32_t exstyle, const char *cls, const char *text, uint32_t style, int x, int y, int w, int h,
	uint16_t parent, uint16_t id, uint16_t hinst, uint32_t param);
uint16_t user_dlg_font (void);
int user_key_down (int vk);
uint32_t user_theme_ref (int bt);	/* un jeton du thème, en COLORREF */

/* les contrôles (ctrl.c) */
void ctrl_register (void);
uint32_t ctrl_button_proc (uint16_t, uint16_t, uint16_t, uint32_t);
uint32_t ctrl_static_proc (uint16_t, uint16_t, uint16_t, uint32_t);
uint32_t ctrl_edit_proc (uint16_t, uint16_t, uint16_t, uint32_t);
uint32_t ctrl_listbox_proc (uint16_t, uint16_t, uint16_t, uint32_t);
uint32_t ctrl_combo_proc (uint16_t, uint16_t, uint16_t, uint32_t);
uint32_t ctrl_scroll_proc (uint16_t, uint16_t, uint16_t, uint32_t);
/* le mode natif (widgets de UIKit) */
void ctrl_native_event (long wd, int code);
void ctrl_native_redraw_under (uint16_t hwnd);
void ctrl_native_sync_tree (uint16_t hwnd);
void ctrl_native_focus (uint16_t hwnd);

/* le multimédia (mmsystem.c) : minuteries appelées pendant l'attente des messages */
void mm_poll (void);
int mm_next_ms (void);	/* ms avant la prochaine (-1 : aucune) */

#endif
