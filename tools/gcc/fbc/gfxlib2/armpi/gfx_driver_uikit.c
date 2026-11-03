/* le pilote de gfxlib2 sous ARMPI (docs/GCC.md), de type fbdev : le tampon graphique de FreeBASIC est
   copie dans une fenetre d'UIKit par un thread, comme le pilote X11 ; clavier et souris en reviennent.
   Les modes a palette (SCREEN 13...) sont convertis a la copie (fb_hGetBlitter) : la fenetre est
   toujours en 0x00RRGGBB, soit « BGR » pour gfxlib2, et un changement de palette la redessine. */

#include "../fb_gfx.h"
#include "../../rtlib/unix/fb_private_console.h"
#include <pthread.h>
#include "gfx_uikit.h"

#define WAIT_MS		8
#define MS_PER_FRAME	16

static pthread_t thread;
static pthread_mutex_t mutex;
static pthread_cond_t cond;
static int is_running, is_started, open_failed;
static int win_w, win_h;
static char *win_title;
static BLITTER *blitter;
static unsigned char *back;		/* fenetre plus petite que le mode : image complete, copiee coupee */
static int mouse_x, mouse_y, mouse_wheel, mouse_buttons, mouse_on;
static int key_mods;

/* code HID -> code de touche FreeBASIC (de 0x04 a 0x63) */
static const unsigned char hid_to_sc[0x64] =
{
	0, 0, 0, 0,
	SC_A, SC_B, SC_C, SC_D, SC_E, SC_F, SC_G, SC_H, SC_I, SC_J, SC_K, SC_L, SC_M,
	SC_N, SC_O, SC_P, SC_Q, SC_R, SC_S, SC_T, SC_U, SC_V, SC_W, SC_X, SC_Y, SC_Z,
	SC_1, SC_2, SC_3, SC_4, SC_5, SC_6, SC_7, SC_8, SC_9, SC_0,
	SC_ENTER, SC_ESCAPE, SC_BACKSPACE, SC_TAB, SC_SPACE, SC_MINUS, SC_EQUALS,
	SC_LEFTBRACKET, SC_RIGHTBRACKET, SC_BACKSLASH, SC_BACKSLASH, SC_SEMICOLON, SC_QUOTE,
	SC_TILDE, SC_COMMA, SC_PERIOD, SC_SLASH, SC_CAPSLOCK,
	SC_F1, SC_F2, SC_F3, SC_F4, SC_F5, SC_F6, SC_F7, SC_F8, SC_F9, SC_F10, SC_F11, SC_F12,
	0, SC_SCROLLLOCK, 0, SC_INSERT, SC_HOME, SC_PAGEUP, SC_DELETE, SC_END, SC_PAGEDOWN,
	SC_RIGHT, SC_LEFT, SC_DOWN, SC_UP,
	SC_NUMLOCK, SC_SLASH, SC_MULTIPLY, SC_MINUS, SC_PLUS, SC_ENTER,
	SC_END, SC_DOWN, SC_PAGEDOWN, SC_LEFT, SC_CLEAR, SC_RIGHT, SC_HOME, SC_UP, SC_PAGEUP,
	SC_INSERT, SC_DELETE
};

/* les modificateurs (EV_MODS, l'octet du rapport HID) : Ctrl, Maj, Alt, Gui, a gauche puis a droite */
static const unsigned char mod_to_sc[8] =
{
	SC_CONTROL, SC_LSHIFT, SC_ALT, SC_LWIN, SC_CONTROL, SC_RSHIFT, SC_ALT, SC_RWIN
};

static void update_mods( int mods )
{
	int i;

	for( i = 0; i < 8; i++ ) {
		if( ((mods ^ key_mods) >> i) & 1 ) {
			EVENT e;
			__fb_gfx->key[mod_to_sc[i]] = (mods >> i) & 1;
			e.type = ((mods >> i) & 1) ? EVENT_KEY_PRESS : EVENT_KEY_RELEASE;
			e.scancode = mod_to_sc[i];
			e.ascii = 0;
			fb_hPostEvent( &e );
		}
	}
	key_mods = mods;
}

static int translate_key( int scancode, int ch )
{
	switch( scancode ) {
	case SC_ENTER:     return 13;
	case SC_ESCAPE:    return 27;
	case SC_BACKSPACE: return 8;
	case SC_TAB:       return 9;
	case SC_DELETE:    return KEY_DEL;
	}
	if( ch == 0x7F )
		return KEY_DEL;
	if( ch > 0 && ch < 256 )
		return ch;
	return fb_hScancodeToExtendedKey( scancode );
}

static void on_key( const struct uikit_event *u )
{
	EVENT e;
	int key, sc = (u->code >= 0 && u->code < 0x64) ? hid_to_sc[u->code] : 0;

	update_mods( u->mods );
	if( sc == 0 )
		return;

	e.scancode = sc;
	e.ascii = 0;
	if( u->kind ) {
		key = translate_key( sc, u->ch );
		if( key > 0 )
			fb_hPostKey( key );
		if( key > 0 && key < 256 )
			e.ascii = key;
		e.type = __fb_gfx->key[sc] ? EVENT_KEY_REPEAT : EVENT_KEY_PRESS;
		__fb_gfx->key[sc] = TRUE;
	} else {
		__fb_gfx->key[sc] = FALSE;
		e.type = EVENT_KEY_RELEASE;
	}
	fb_hPostEvent( &e );
}

static void on_mouse( const struct uikit_event *u )
{
	EVENT e;
	int x = u->x, y = u->y / __fb_gfx->scanline_size, changed;

	if( x != mouse_x || y != mouse_y ) {
		e.type = EVENT_MOUSE_MOVE;
		e.x = x;
		e.y = y;
		e.dx = x - mouse_x;
		e.dy = y - mouse_y;
		mouse_x = x;
		mouse_y = y;
		fb_hPostEvent( &e );
	}
	mouse_on = (x >= 0) && (x < win_w) && (u->y >= 0) && (u->y < win_h);

	/* (boutons d'UIKit : bit 0 gauche, 1 droit, 2 milieu ; de gfxlib2 : 0 gauche, 1 droit, 2 milieu) */
	changed = (u->code ^ mouse_buttons) & 7;
	if( changed ) {
		int b;
		for( b = 1; b <= 4; b <<= 1 ) {
			if( changed & b ) {
				e.type = (u->code & b) ? EVENT_MOUSE_BUTTON_PRESS : EVENT_MOUSE_BUTTON_RELEASE;
				e.button = b;
				fb_hPostEvent( &e );
			}
		}
		mouse_buttons = u->code & 7;
	}
}

static void on_event( const struct uikit_event *u )
{
	EVENT e;

	switch( u->type ) {
	case UIKIT_KEY:
		on_key( u );
		break;
	case UIKIT_MOUSE:
		on_mouse( u );
		break;
	case UIKIT_WHEEL:
		mouse_wheel += u->y;
		e.type = EVENT_MOUSE_WHEEL;
		e.z = mouse_wheel;
		fb_hPostEvent( &e );
		break;
	case UIKIT_CLOSE:
		fb_hPostKey( KEY_QUIT );
		e.type = EVENT_WINDOW_CLOSE;
		fb_hPostEvent( &e );
		break;
	case UIKIT_FOCUS:
		if( !u->kind ) {
			fb_hMemSet( __fb_gfx->key, FALSE, 128 );
			mouse_buttons = 0;
			key_mods = 0;
		}
		e.type = u->kind ? EVENT_WINDOW_GOT_FOCUS : EVENT_WINDOW_LOST_FOCUS;
		fb_hPostEvent( &e );
		break;
	}
}

/* les lignes modifiees du tampon -> la fenetre */
static void present( void )
{
	char *dirty = __fb_gfx->dirty;
	int first, last, y0, y1, w, h, pw, ph, y, n = win_h / __fb_gfx->scanline_size;
	uint32_t *px;

	for( first = 0; first < n && !dirty[first]; first++ )
		;
	if( first == n )
		return;
	for( last = n - 1; !dirty[last]; last-- )
		;

	px = uikit_window_pixels( &pw, &ph );
	if( px == NULL )
		return;
	if( back == NULL ) {
		blitter( (unsigned char *)px, pw * 4 );
	} else {
		blitter( back, win_w * 4 );
		w = MIN( pw, win_w );
		h = MIN( ph, win_h );
		for( y = 0; y < h; y++ )
			fb_hMemCpy( px + y * pw, back + y * win_w * 4, w * 4 );
	}
	fb_hMemSet( dirty, FALSE, n );

	y0 = first * __fb_gfx->scanline_size;
	y1 = (last + 1) * __fb_gfx->scanline_size;
	if( y1 > ph )
		y1 = ph;
	if( y1 > y0 )
		uikit_window_update( 0, y0, MIN( pw, win_w ), y1 - y0 );
}

static void *window_thread( void *arg )
{
	struct uikit_event u;
	int pw, ph;

	(void)arg;
	pthread_mutex_lock( &mutex );
	open_failed = !uikit_window_open( win_w, win_h, win_title );
	if( !open_failed ) {
		uikit_window_pixels( &pw, &ph );
		if( pw != win_w || ph != win_h ) {
			back = malloc( win_w * win_h * 4 );
			open_failed = (back == NULL);
		}
	}
	pthread_cond_signal( &cond );
	pthread_mutex_unlock( &mutex );
	if( open_failed )
		return NULL;

	while( is_running ) {
		/* (attente seulement pour le premier : ensuite tout ce qui est deja la) */
		int wait = WAIT_MS;
		while( uikit_next_event( wait, &u ) ) {
			wait = 0;
			on_event( &u );
		}

		pthread_mutex_lock( &mutex );
		present( );
		pthread_mutex_unlock( &mutex );
	}

	uikit_window_close( );
	return NULL;
}

static void driver_exit( void )
{
	if( is_started ) {
		is_running = FALSE;
		pthread_join( thread, NULL );
		pthread_mutex_destroy( &mutex );
		pthread_cond_destroy( &cond );
		is_started = FALSE;
	}
	free( back );
	back = NULL;
	free( win_title );
	win_title = NULL;
}

static int driver_init( char *title, int w, int h, int depth, int refresh_rate, int flags )
{
	if( flags & DRIVER_OPENGL )
		return -1;

	blitter = fb_hGetBlitter( 32, FALSE );
	if( blitter == NULL )
		return -1;

	win_w = w;
	win_h = h;
	win_title = strdup( title ? title : "FreeBASIC" );
	mouse_x = mouse_y = 0;
	mouse_on = mouse_wheel = mouse_buttons = key_mods = 0;
	__fb_gfx->refresh_rate = 1000 / MS_PER_FRAME;

	pthread_mutex_init( &mutex, NULL );
	pthread_cond_init( &cond, NULL );
	is_running = TRUE;
	open_failed = FALSE;
	pthread_mutex_lock( &mutex );
	if( pthread_create( &thread, NULL, window_thread, NULL ) ) {
		pthread_mutex_unlock( &mutex );
		pthread_mutex_destroy( &mutex );
		pthread_cond_destroy( &cond );
		is_running = FALSE;
		return -1;
	}
	is_started = TRUE;
	pthread_cond_wait( &cond, &mutex );
	pthread_mutex_unlock( &mutex );

	return open_failed ? -1 : 0;
}

static void driver_lock( void )
{
	pthread_mutex_lock( &mutex );
}

static void driver_unlock( void )
{
	pthread_mutex_unlock( &mutex );
}

static void driver_set_palette( int index, int r, int g, int b )
{
	/* la fenetre n'a pas de palette : l'image entiere est a reconvertir */
	fb_hMemSet( __fb_gfx->dirty, TRUE, __fb_gfx->h );
}

static void driver_wait_vsync( void )
{
	usleep( MS_PER_FRAME * 1000 );
}

static int driver_get_mouse( int *x, int *y, int *z, int *buttons, int *clip )
{
	if( !mouse_on )
		return -1;
	*x = mouse_x;
	*y = mouse_y;
	*z = mouse_wheel;
	*buttons = mouse_buttons;
	*clip = 0;
	return 0;
}

static void driver_set_window_title( char *title )
{
	uikit_window_title( title );
}

static int *driver_fetch_modes( int depth, int *size )
{
	static const int std[][2] = { {320, 200}, {320, 240}, {640, 400}, {640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1920, 1080} };
	int i, n = 0, sw, sh, *modes;

	if( !uikit_screen_size( &sw, &sh ) )
		return NULL;
	modes = malloc( sizeof(std) / sizeof(std[0]) * sizeof(int) );
	for( i = 0; i < (int)(sizeof(std) / sizeof(std[0])); i++ )
		if( std[i][0] <= sw && std[i][1] <= sh )
			modes[n++] = (std[i][0] << 16) | std[i][1];
	*size = n;
	return modes;
}

static const GFXDRIVER fb_gfxDriverUIKit =
{
	"UIKit",                 /* char *name; */
	driver_init,             /* int (*init)(char *title, int w, int h, int depth, int refresh_rate, int flags); */
	driver_exit,             /* void (*exit)(void); */
	driver_lock,             /* void (*lock)(void); */
	driver_unlock,           /* void (*unlock)(void); */
	driver_set_palette,      /* void (*set_palette)(int index, int r, int g, int b); */
	driver_wait_vsync,       /* void (*wait_vsync)(void); */
	driver_get_mouse,        /* int (*get_mouse)(int *x, int *y, int *z, int *buttons, int *clip); */
	NULL,                    /* void (*set_mouse)(int x, int y, int cursor, int clip); */
	driver_set_window_title, /* void (*set_window_title)(char *title); */
	NULL,                    /* int (*set_window_pos)(int x, int y); */
	driver_fetch_modes,      /* int *(*fetch_modes)(int depth, int *size); */
	NULL,                    /* void (*flip)(void); */
	NULL,                    /* void (*poll_events)(void); */
	NULL                     /* void (*update)(void); */
};

const GFXDRIVER *__fb_gfx_drivers_list[] = { &fb_gfxDriverUIKit, NULL };

void fb_hScreenInfo( ssize_t *width, ssize_t *height, ssize_t *depth, ssize_t *refresh )
{
	int w, h;

	if( uikit_screen_size( &w, &h ) ) {
		*width = w;
		*height = h;
		*depth = 32;
		*refresh = 1000 / MS_PER_FRAME;
	} else {
		*width = *height = *depth = *refresh = 0;
	}
}
