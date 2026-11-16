/**************************************************************************/
/*  display_server_korin.cpp                                              */
/**************************************************************************/
/* Korin OS : DisplayServerKorin (display_server_korin.h). */

#include "display_server_korin.h"

#include "korin_ui.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/os/main_loop.h"
#include "core/os/os.h"
#include "servers/display/native_menu.h"
#include "servers/rendering/dummy/rasterizer_dummy.h"

#ifdef GLES3_ENABLED
#include "drivers/gles3/rasterizer_gles3.h"
#endif

#define KEY_F9_HID 0x42 /* comme les emulateurs et SDL : bascule du plein ecran */

static DisplayServerKorin *s_singleton = nullptr;

/* --- clavier : codes HID (page 7) -> touches de Godot (positions du QWERTY) --- */

static Key hid_to_key(int p_code) {
	if (p_code >= 0x04 && p_code <= 0x1d) {
		return Key(int(Key::A) + p_code - 0x04);
	}
	if (p_code >= 0x1e && p_code <= 0x26) {
		return Key(int(Key::KEY_1) + p_code - 0x1e);
	}
	if (p_code >= 0x3a && p_code <= 0x45) {
		return Key(int(Key::F1) + p_code - 0x3a);
	}
	if (p_code >= 0x59 && p_code <= 0x61) {
		return Key(int(Key::KP_1) + p_code - 0x59);
	}
	switch (p_code) {
		case 0x27:
			return Key::KEY_0;
		case 0x28:
			return Key::ENTER;
		case 0x29:
			return Key::ESCAPE;
		case 0x2a:
			return Key::BACKSPACE;
		case 0x2b:
			return Key::TAB;
		case 0x2c:
			return Key::SPACE;
		case 0x2d:
			return Key::MINUS;
		case 0x2e:
			return Key::EQUAL;
		case 0x2f:
			return Key::BRACKETLEFT;
		case 0x30:
			return Key::BRACKETRIGHT;
		case 0x31:
		case 0x32:
			return Key::BACKSLASH;
		case 0x33:
			return Key::SEMICOLON;
		case 0x34:
			return Key::APOSTROPHE;
		case 0x35:
			return Key::QUOTELEFT;
		case 0x36:
			return Key::COMMA;
		case 0x37:
			return Key::PERIOD;
		case 0x38:
			return Key::SLASH;
		case 0x39:
			return Key::CAPSLOCK;
		case 0x46:
			return Key::PRINT;
		case 0x47:
			return Key::SCROLLLOCK;
		case 0x48:
			return Key::PAUSE;
		case 0x49:
			return Key::INSERT;
		case 0x4a:
			return Key::HOME;
		case 0x4b:
			return Key::PAGEUP;
		case 0x4c:
			return Key::KEY_DELETE;
		case 0x4d:
			return Key::END;
		case 0x4e:
			return Key::PAGEDOWN;
		case 0x4f:
			return Key::RIGHT;
		case 0x50:
			return Key::LEFT;
		case 0x51:
			return Key::DOWN;
		case 0x52:
			return Key::UP;
		case 0x53:
			return Key::NUMLOCK;
		case 0x54:
			return Key::KP_DIVIDE;
		case 0x55:
			return Key::KP_MULTIPLY;
		case 0x56:
			return Key::KP_SUBTRACT;
		case 0x57:
			return Key::KP_ADD;
		case 0x58:
			return Key::KP_ENTER;
		case 0x62:
			return Key::KP_0;
		case 0x63:
			return Key::KP_PERIOD;
		case 0x64:
			return Key::SECTION; // la touche < > de l'ISO (AZERTY)
		case 0x65:
			return Key::MENU;
		case 0xe0:
		case 0xe4:
			return Key::CTRL;
		case 0xe1:
		case 0xe5:
			return Key::SHIFT;
		case 0xe2:
		case 0xe6:
			return Key::ALT;
		case 0xe3:
		case 0xe7:
			return Key::META;
		default:
			return Key::UNKNOWN;
	}
}

static KeyLocation hid_location(int p_code) {
	if (p_code >= 0xe0 && p_code <= 0xe3) {
		return KeyLocation::LEFT;
	}
	if (p_code >= 0xe4 && p_code <= 0xe7) {
		return KeyLocation::RIGHT;
	}
	return KeyLocation::UNSPECIFIED;
}

static int godot_cursor(DisplayServerEnums::CursorShape p_shape) {
	switch (p_shape) {
		case DisplayServerEnums::CURSOR_IBEAM:
			return KUI_CUR_TEXT;
		case DisplayServerEnums::CURSOR_POINTING_HAND:
			return KUI_CUR_HAND;
		case DisplayServerEnums::CURSOR_CROSS:
			return KUI_CUR_CROSS;
		case DisplayServerEnums::CURSOR_WAIT:
		case DisplayServerEnums::CURSOR_BUSY:
			return KUI_CUR_WAIT;
		case DisplayServerEnums::CURSOR_DRAG:
		case DisplayServerEnums::CURSOR_CAN_DROP:
			return KUI_CUR_DRAG;
		case DisplayServerEnums::CURSOR_FORBIDDEN:
			return KUI_CUR_NO;
		case DisplayServerEnums::CURSOR_VSIZE:
		case DisplayServerEnums::CURSOR_VSPLIT:
			return KUI_CUR_VRESIZE;
		case DisplayServerEnums::CURSOR_HSIZE:
		case DisplayServerEnums::CURSOR_HSPLIT:
			return KUI_CUR_HRESIZE;
		case DisplayServerEnums::CURSOR_BDIAGSIZE:
			return KUI_CUR_NESW;
		case DisplayServerEnums::CURSOR_FDIAGSIZE:
			return KUI_CUR_NWSE;
		case DisplayServerEnums::CURSOR_MOVE:
			return KUI_CUR_MOVE;
		default:
			return KUI_CUR_ARROW;
	}
}

/* --- le pilote --- */

DisplayServerKorin *DisplayServerKorin::get_singleton() {
	return s_singleton;
}

Vector<String> DisplayServerKorin::get_rendering_drivers_func() {
	Vector<String> drivers;
#ifdef GLES3_ENABLED
	drivers.push_back("opengl3");
	drivers.push_back("opengl3_es");
	// (un projet Forward+ ou Mobile demande vulkan : accepte ici, il passe en Compatibility, comme sur Linux sans
	// Vulkan avec rendering/rendering_device/fallback_to_opengl3)
	drivers.push_back("vulkan");
#endif
	drivers.push_back("dummy");
	return drivers;
}

DisplayServer *DisplayServerKorin::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_window_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerKorin(p_rendering_driver, p_window_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, p_parent_window, r_error));
	if (r_error != OK) {
		memdelete(ds);
		return nullptr;
	}
	return ds;
}

void DisplayServerKorin::register_korin_driver() {
	register_create_function("korin", create_func, get_rendering_drivers_func);
}

void DisplayServerKorin::_dispatch_input_event(const Ref<InputEvent> &p_event) {
	Callable cb = get_singleton()->input_event_callback;
	if (cb.is_valid()) {
		cb.call(p_event);
	}
}

DisplayServerKorin::DisplayServerKorin(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_window_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Point2i *p_position, const Size2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	s_singleton = this;
	r_error = OK;
	native_menu = memnew(NativeMenu);

	if (!kui_open()) {
		ERR_PRINT("Korin : pas d'UIKit (le bureau Elegant ne tourne pas ?), essayer --headless.");
		r_error = ERR_UNAVAILABLE;
		return;
	}
	kui_screen(&screen_size.x, &screen_size.y, &reserved_bottom, &reserved_top);

	int usable_h = screen_size.y - reserved_top - reserved_bottom;
	int w = MIN(p_resolution.x, screen_size.x);
	int h = MIN(p_resolution.y, usable_h);
	int x = p_position ? p_position->x : (screen_size.x - w) / 2;
	int y = p_position ? p_position->y : reserved_top + (usable_h - h) / 2;
	resizable = !(p_flags & DisplayServerEnums::WINDOW_FLAG_RESIZE_DISABLED_BIT);
	int kind = (p_flags & DisplayServerEnums::WINDOW_FLAG_BORDERLESS_BIT) ? KUI_WIN_BORDERLESS : (resizable ? KUI_WIN_RESIZABLE : KUI_WIN_FRAMED);

	win = kui_window(x, y, w, h, "Godot", kind);
	if (win <= 0) {
		ERR_PRINT("Korin : fenetre refusee par Elegant.");
		r_error = ERR_UNAVAILABLE;
		return;
	}
	size = Size2i(w, h);

	bool gl_ok = false;
	String rendering_driver = p_rendering_driver;
#ifdef GLES3_ENABLED
	/* pas de Vulkan sur Korin : un projet Forward+ ou Mobile s'ouvre en Compatibility (GL ES 3.0) */
	if (rendering_driver != "opengl3" && rendering_driver != "opengl3_es" && rendering_driver != "dummy") {
		WARN_PRINT(vformat("Korin : pas de pilote %s (pas de Vulkan), rendu Compatibility (OpenGL ES 3.0).", rendering_driver));
		rendering_driver = "opengl3";
		OS::get_singleton()->set_current_rendering_method("gl_compatibility", OS::RENDERING_SOURCE_FALLBACK);
		OS::get_singleton()->set_current_rendering_driver_name(rendering_driver, OS::RENDERING_SOURCE_FALLBACK);
	}
	if (rendering_driver == "opengl3" || rendering_driver == "opengl3_es") {
		int mode = kgl_init();
		if (mode != 0) {
			gl_surface = kgl_window(win, screen_size.x, screen_size.y);
			if (gl_surface) {
				kgl_zone(gl_surface, win, size.x, size.y);
				gl_context = kgl_context();
				gl_ok = gl_context && kgl_make_current(gl_context, gl_surface);
			}
		}
		if (gl_ok) {
			if (mode == 3) {
				print_line("Korin : OpenGL ES 3.0 par le processeur (softpipe de Mesa) : lent.");
			}
			RasterizerGLES3::make_current(false);
		} else {
			OS::get_singleton()->alert("Pas d'OpenGL ES 3.0 (GlKit) : rendu impossible.", "Godot sur Korin");
			r_error = ERR_UNAVAILABLE;
			return;
		}
	}
#endif
	if (!gl_ok) {
		RasterizerDummy::make_current();
	}

	if (p_window_mode == DisplayServerEnums::WINDOW_MODE_FULLSCREEN || p_window_mode == DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN) {
		window_set_mode(p_window_mode);
	} else if (p_window_mode == DisplayServerEnums::WINDOW_MODE_MAXIMIZED) {
		window_set_mode(p_window_mode);
	}

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_event);
}

DisplayServerKorin::~DisplayServerKorin() {
	if (framed) {
		kui_window_close(framed);
	}
	if (win > 0) {
		kui_window_close(win);
	}
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}
	s_singleton = nullptr;
}

bool DisplayServerKorin::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_CLIPBOARD:
		case DisplayServerEnums::FEATURE_CURSOR_SHAPE:
		case DisplayServerEnums::FEATURE_MOUSE:
		case DisplayServerEnums::FEATURE_EXTEND_TO_TITLE: // le contenu va jusqu'en haut : voir window_get_safe_title_margins
			return true;
		default:
			return false;
	}
}

String DisplayServerKorin::get_name() const {
	return "korin";
}

/* --- fenetre --- */

void DisplayServerKorin::_set_size(int p_w, int p_h) {
	size = Size2i(p_w, p_h);
	kgl_zone(gl_surface, win, p_w, p_h);
	if (rect_changed_callback.is_valid()) {
		rect_changed_callback.call(Rect2i(window_get_position(), size));
	}
}

void DisplayServerKorin::_send_window_event(DisplayServerEnums::WindowEvent p_event) {
	if (window_event_callback.is_valid()) {
		window_event_callback.call(p_event);
	}
}

/* plein ecran : une fenetre sans cadre au-dessus de tout, de la taille de l'ecran, comme SDL et EmuKit ;
 * celle du cadre attend dessous ; la surface du GPU passe de l'une a l'autre */
void DisplayServerKorin::_set_fullscreen(bool p_enable) {
	if (p_enable == (framed != 0)) {
		return;
	}
	if (p_enable) {
		long full = kui_window(0, 0, screen_size.x, screen_size.y, "Godot", KUI_WIN_FULLSCREEN);
		if (full <= 0) {
			return;
		}
		framed = win;
		win = full;
		_set_size(screen_size.x, screen_size.y);
	} else {
		long full = win;
		int w, h;
		win = framed;
		framed = 0;
		kui_window_close(full);
		kui_window_size(win, &w, &h);
		_set_size(w, h);
	}
	_apply_mouse_mode();
	kui_cursor(win, godot_cursor(cursor_shape));
}

void DisplayServerKorin::window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window) {
	switch (p_mode) {
		case DisplayServerEnums::WINDOW_MODE_FULLSCREEN:
		case DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN:
			_set_fullscreen(true);
			break;
		case DisplayServerEnums::WINDOW_MODE_MAXIMIZED:
			_set_fullscreen(false);
			kui_window_maximize(win);
			break;
		case DisplayServerEnums::WINDOW_MODE_MINIMIZED:
			kui_window_minimize(framed ? framed : win);
			break;
		default:
			_set_fullscreen(false);
			break;
	}
	window_mode = p_mode;
}

DisplayServerEnums::WindowMode DisplayServerKorin::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return window_mode;
}

bool DisplayServerKorin::window_is_maximize_allowed(DisplayServerEnums::WindowID p_window) const {
	return resizable;
}

void DisplayServerKorin::window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window) {
	kui_window_title(framed ? framed : win, p_title.utf8().get_data());
}

void DisplayServerKorin::window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	if (framed) {
		return;
	}
	int w = CLAMP(p_size.x, 1, screen_size.x);
	int h = CLAMP(p_size.y, 1, screen_size.y);
	kui_window_resize(win, w, h);
	_set_size(w, h);
}

Size2i DisplayServerKorin::window_get_size(DisplayServerEnums::WindowID p_window) const {
	return size;
}

Size2i DisplayServerKorin::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return size;
}

void DisplayServerKorin::window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	max_size = p_size;
}

Size2i DisplayServerKorin::window_get_max_size(DisplayServerEnums::WindowID p_window) const {
	return max_size;
}

void DisplayServerKorin::window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	min_size = p_size;
}

Size2i DisplayServerKorin::window_get_min_size(DisplayServerEnums::WindowID p_window) const {
	return min_size;
}

/* Elegant place les fenetres ; Godot ne les deplace pas */
Point2i DisplayServerKorin::window_get_position(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

Point2i DisplayServerKorin::window_get_position_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

void DisplayServerKorin::window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window) {
}

void DisplayServerKorin::window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) {
}

int DisplayServerKorin::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	return 0;
}

void DisplayServerKorin::window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window) {
}

void DisplayServerKorin::window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window) {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_RESIZE_DISABLED) {
		resizable = !p_enabled;
	}
}

bool DisplayServerKorin::window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window) const {
	if (p_flag == DisplayServerEnums::WINDOW_FLAG_RESIZE_DISABLED) {
		return !resizable;
	}
	return false;
}

void DisplayServerKorin::window_request_attention(DisplayServerEnums::WindowID p_window) {
}

void DisplayServerKorin::window_move_to_foreground(DisplayServerEnums::WindowID p_window) {
	kui_window_raise(framed ? framed : win);
}

bool DisplayServerKorin::window_is_focused(DisplayServerEnums::WindowID p_window) const {
	return focused;
}

bool DisplayServerKorin::window_can_draw(DisplayServerEnums::WindowID p_window) const {
	return window_mode != DisplayServerEnums::WINDOW_MODE_MINIMIZED;
}

bool DisplayServerKorin::can_any_window_draw() const {
	return window_can_draw();
}

DisplayServerEnums::VSyncMode DisplayServerKorin::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::VSYNC_ENABLED; // (armgl_swap attend la composition d'Elegant)
}

/* Le chrome de Korin n'a pas de barre de titre : reduire, agrandir et fermer sont dessines en haut a droite du
 * contenu (FRAME_CAP_W x FRAME_CAP_H, src/inc/armpi.inc). L'editeur de Godot laisse cette place libre a droite de
 * sa barre de menus (expand_to_title) ; un jeu, lui, dessine dessous comme les autres apps. */
Vector3i DisplayServerKorin::window_get_safe_title_margins(DisplayServerEnums::WindowID p_window) const {
	const int frame_cap_w = 3 * 28 + 18, frame_cap_h = 26;
	if (framed) {
		return Vector3i(); // plein ecran : pas de cadre
	}
	return Vector3i(0, frame_cap_w + 8, frame_cap_h);
}

Vector<DisplayServerEnums::WindowID> DisplayServerKorin::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> ret;
	ret.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return ret;
}

DisplayServerEnums::WindowID DisplayServerKorin::get_window_at_screen_position(const Point2i &p_position) const {
	return DisplayServerEnums::MAIN_WINDOW_ID;
}

void DisplayServerKorin::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	window_attached_instance_id = p_instance;
}

ObjectID DisplayServerKorin::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	return window_attached_instance_id;
}

void DisplayServerKorin::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	rect_changed_callback = p_callable;
}

void DisplayServerKorin::window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_event_callback = p_callable;
}

void DisplayServerKorin::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_event_callback = p_callable;
}

void DisplayServerKorin::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_text_callback = p_callable;
}

void DisplayServerKorin::window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	drop_files_callback = p_callable;
}

/* --- ecran --- */

int DisplayServerKorin::get_screen_count() const {
	return 1;
}

int DisplayServerKorin::get_primary_screen() const {
	return 0;
}

Point2i DisplayServerKorin::screen_get_position(int p_screen) const {
	return Point2i();
}

Size2i DisplayServerKorin::screen_get_size(int p_screen) const {
	return screen_size;
}

Rect2i DisplayServerKorin::screen_get_usable_rect(int p_screen) const {
	return Rect2i(0, reserved_top, screen_size.x, screen_size.y - reserved_top - reserved_bottom);
}

int DisplayServerKorin::screen_get_dpi(int p_screen) const {
	return 96;
}

float DisplayServerKorin::screen_get_refresh_rate(int p_screen) const {
	return 60.0f;
}

/* --- souris et curseur --- */

void DisplayServerKorin::cursor_set_shape(DisplayServerEnums::CursorShape p_shape) {
	cursor_shape = p_shape;
	if (win > 0) {
		kui_cursor(win, godot_cursor(p_shape));
	}
}

DisplayServerEnums::CursorShape DisplayServerKorin::cursor_get_shape() const {
	return cursor_shape;
}

/* captive (jeux) : le pointeur est pris par la fenetre (ui_pointer_lock), les deplacements arrivent relatifs */
void DisplayServerKorin::_apply_mouse_mode() {
	DisplayServerEnums::MouseMode m = mouse_mode_override_enabled ? mouse_mode_override : mouse_mode;
	kui_pointer_lock(win, m == DisplayServerEnums::MOUSE_MODE_CAPTURED ? 1 : 0);
}

void DisplayServerKorin::mouse_set_mode(DisplayServerEnums::MouseMode p_mode) {
	mouse_mode = p_mode;
	_apply_mouse_mode();
}

DisplayServerEnums::MouseMode DisplayServerKorin::mouse_get_mode() const {
	return mouse_mode;
}

void DisplayServerKorin::mouse_set_mode_override(DisplayServerEnums::MouseMode p_mode) {
	mouse_mode_override = p_mode;
	_apply_mouse_mode();
}

DisplayServerEnums::MouseMode DisplayServerKorin::mouse_get_mode_override() const {
	return mouse_mode_override;
}

void DisplayServerKorin::mouse_set_mode_override_enabled(bool p_override_enabled) {
	mouse_mode_override_enabled = p_override_enabled;
	_apply_mouse_mode();
}

bool DisplayServerKorin::mouse_is_mode_override_enabled() const {
	return mouse_mode_override_enabled;
}

Point2i DisplayServerKorin::mouse_get_position() const {
	return mouse_pos;
}

BitField<MouseButtonMask> DisplayServerKorin::mouse_get_button_state() const {
	return Input::get_singleton()->get_mouse_button_mask();
}

/* --- presse-papiers --- */

void DisplayServerKorin::clipboard_set(const String &p_text) {
	kui_clip_set(p_text.utf8().get_data());
}

String DisplayServerKorin::clipboard_get() const {
	Vector<char> buf;
	buf.resize(1 << 20);
	int n = kui_clip_get(buf.ptrw(), buf.size());
	if (n <= 0) {
		return String();
	}
	return String::utf8(buf.ptr());
}

bool DisplayServerKorin::clipboard_has() const {
	char s[2];
	return kui_clip_get(s, sizeof s) > 0;
}

/* --- evenements --- */

void DisplayServerKorin::_set_modifiers(Ref<InputEventWithModifiers> p_ev, int p_mods) {
	p_ev->set_shift_pressed(p_mods & KUI_MOD_SHIFT);
	p_ev->set_ctrl_pressed(p_mods & KUI_MOD_CTRL);
	p_ev->set_alt_pressed(p_mods & KUI_MOD_ALT);
	p_ev->set_meta_pressed(p_mods & KUI_MOD_META);
}

void DisplayServerKorin::_on_key(const kui_event &p_ev) {
	int code = p_ev.code & 255;
	bool pressed = p_ev.kind != 0;
	if (code == KEY_F9_HID && !Engine::get_singleton()->is_editor_hint()) {
		if (pressed) {
			window_set_mode(framed ? DisplayServerEnums::WINDOW_MODE_WINDOWED : DisplayServerEnums::WINDOW_MODE_FULLSCREEN);
		}
		return;
	}
	Key physical = hid_to_key(code);
	Key keycode = physical;
	char32_t ch = (char32_t)p_ev.ch;
	/* HIDManager donne le caractere de la disposition (AZERTY...) : la touche logique le suit */
	if (ch > 32 && ch < 0x10000 && ch != 127 && !(p_ev.mods & KUI_MOD_CTRL)) {
		char32_t up = ch >= 'a' && ch <= 'z' ? ch - 32 : ch;
		if ((up >= 'A' && up <= 'Z') || (up >= '0' && up <= '9') || up < 128) {
			keycode = Key(up);
		} else {
			keycode = Key(ch);
		}
	}

	Ref<InputEventKey> ev;
	ev.instantiate();
	ev->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
	ev->set_pressed(pressed);
	ev->set_echo(pressed && key_down[code]);
	ev->set_keycode(keycode);
	ev->set_physical_keycode(physical);
	ev->set_key_label(keycode);
	ev->set_location(hid_location(code));
	if (pressed && ch >= 32 && ch != 127 && !(p_ev.mods & KUI_MOD_CTRL)) {
		ev->set_unicode(ch);
	}
	_set_modifiers(ev, p_ev.mods);
	key_down[code] = pressed;
	Input::get_singleton()->parse_input_event(ev);
}

void DisplayServerKorin::_mouse_button(MouseButton p_button, bool p_pressed, int p_mods) {
	Ref<InputEventMouseButton> ev;
	ev.instantiate();
	ev->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
	ev->set_position(mouse_pos);
	ev->set_global_position(mouse_pos);
	ev->set_pressed(p_pressed);
	ev->set_button_index(p_button);
	_set_modifiers(ev, p_mods);

	if (p_pressed && p_button <= MouseButton::MIDDLE) {
		uint64_t now = OS::get_singleton()->get_ticks_msec();
		if (p_button == last_click_button && now - last_click_ms < 400 && Point2(last_click_pos).distance_to(mouse_pos) < 5) {
			ev->set_double_click(true);
			last_click_button = MouseButton::NONE;
			last_click_ms = 0;
		} else {
			last_click_button = p_button;
			last_click_ms = now;
			last_click_pos = mouse_pos;
		}
	}

	BitField<MouseButtonMask> mask = Input::get_singleton()->get_mouse_button_mask();
	MouseButtonMask flag = mouse_button_to_mask(p_button);
	if (p_pressed) {
		mask.set_flag(flag);
	} else {
		mask.clear_flag(flag);
	}
	ev->set_button_mask(mask);
	Input::get_singleton()->parse_input_event(ev);
}

void DisplayServerKorin::_on_mouse(const kui_event &p_ev) {
	/* boutons : bit 0 gauche, 1 droit, 2 milieu (comme le pilote de SDL) */
	static const MouseButton buttons[3] = { MouseButton::LEFT, MouseButton::RIGHT, MouseButton::MIDDLE };
	int b = p_ev.code & 7;
	Vector2 relative;

	if (p_ev.kind == KUI_M_REL) {
		relative = Vector2(p_ev.x, p_ev.y);
	} else {
		Point2i pos(p_ev.x, p_ev.y);
		relative = Vector2(pos - mouse_pos);
		mouse_pos = pos;
	}
	if (relative != Vector2()) {
		Ref<InputEventMouseMotion> ev;
		ev.instantiate();
		ev->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
		_set_modifiers(ev, p_ev.mods);
		ev->set_button_mask(Input::get_singleton()->get_mouse_button_mask());
		ev->set_position(mouse_pos);
		ev->set_global_position(mouse_pos);
		ev->set_relative(relative);
		ev->set_relative_screen_position(relative);
		ev->set_velocity(Input::get_singleton()->get_last_mouse_velocity());
		ev->set_screen_velocity(ev->get_velocity());
		Input::get_singleton()->parse_input_event(ev);
	}
	for (int i = 0; i < 3; i++) {
		if ((b ^ mouse_buttons) & (1 << i)) {
			_mouse_button(buttons[i], (b >> i) & 1, p_ev.mods);
		}
	}
	mouse_buttons = b;
}

void DisplayServerKorin::_on_wheel(const kui_event &p_ev) {
	int notches = p_ev.y;
	MouseButton button = notches > 0 ? MouseButton::WHEEL_UP : MouseButton::WHEEL_DOWN;
	for (int i = 0; i < (notches < 0 ? -notches : notches); i++) {
		_mouse_button(button, true, p_ev.mods);
		_mouse_button(button, false, p_ev.mods);
	}
}

void DisplayServerKorin::process_events() {
	kui_event ev;
	while (kui_poll(&ev, 0) > 0) {
		switch (ev.type) {
			case KUI_EV_KEY:
				_on_key(ev);
				break;
			case KUI_EV_MOUSE:
				_on_mouse(ev);
				break;
			case KUI_EV_WHEEL:
				_on_wheel(ev);
				break;
			case KUI_EV_CLOSE:
				_send_window_event(DisplayServerEnums::WINDOW_EVENT_CLOSE_REQUEST);
				break;
			case KUI_EV_FOCUS:
				/* (en plein ecran, la fenetre a cadre, cachee dessous, perd le focus : ce n'est pas Godot) */
				if (ev.win == win) {
					focused = ev.kind != 0;
					_send_window_event(focused ? DisplayServerEnums::WINDOW_EVENT_FOCUS_IN : DisplayServerEnums::WINDOW_EVENT_FOCUS_OUT);
					if (focused) {
						_apply_mouse_mode();
					}
				}
				break;
			case KUI_EV_RESIZE:
				if (ev.win == win) {
					int w = ev.x, h = ev.y;
					kui_window_resize(win, w, h);
					_set_size(w, h);
				}
				break;
			case KUI_EV_SCREEN:
				screen_size = Size2i(ev.x, ev.y);
				if (framed && ev.win == win) {
					kui_window_resize(win, ev.x, ev.y);
					_set_size(ev.x, ev.y);
				}
				break;
			default:
				break;
		}
	}
	Input::get_singleton()->flush_buffered_events();
}

void DisplayServerKorin::swap_buffers() {
	if (gl_surface) {
		kgl_swap(gl_surface);
	}
}
