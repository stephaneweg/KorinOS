//
// keys.h -- les touches : les codes HID d'UIKit, et le code que grid.h, viewer.h, editor.h attendent (un caractere, ou un
// KEY_* au-dela de l'Unicode des touches sans caractere).
//
#ifndef _photos_keys_h
#define _photos_keys_h

namespace photos {

enum { HID_ENTER = 0x28, HID_ESC = 0x29, HID_BKSP = 0x2A, HID_DEL = 0x4C, HID_HOME = 0x4A, HID_PGUP = 0x4B, HID_END = 0x4D, HID_PGDN = 0x4E,
       HID_RIGHT = 0x4F, HID_LEFT = 0x50, HID_DOWN = 0x51, HID_UP = 0x52 };
enum { KEY_UP = 0x10001, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_DEL, KEY_BACKSPACE };
static inline long key_code (int hid, int ch, int)
{
	switch (hid)
	{
	case HID_UP: return KEY_UP;
	case HID_DOWN: return KEY_DOWN;
	case HID_LEFT: return KEY_LEFT;
	case HID_RIGHT: return KEY_RIGHT;
	case HID_HOME: return KEY_HOME;
	case HID_END: return KEY_END;
	case HID_PGUP: return KEY_PGUP;
	case HID_PGDN: return KEY_PGDN;
	case HID_DEL: return KEY_DEL;
	case HID_BKSP: return KEY_BACKSPACE;
	case HID_ESC: return 27;
	}
	return ch;
}

} // namespace photos

#endif
