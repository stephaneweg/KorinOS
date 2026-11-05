# UIKit: the user interface

UIKit is the graphical interface kit of Korin OS: it opens windows (served by Elegant, the window server), delivers keyboard, mouse and menu bar events, draws (rectangles, rounded corners, anti-aliased shapes, TrueType text) and provides the widgets: buttons, fields, lists, trees, tabs, toolbars, sliders, hand-drawn views, canvases and surfaces. It also brings dialogs (message, files, colour, your own dialogs), menu bar menus, the clipboard, drag and drop, BMP and PNG images and text translation.

Everything is drawn in the colours of the desktop theme (Givre, Onyx Noir...): an app that draws by itself takes its colours from the theme **tokens**, never from hard-coded colours.

> **Note**: UIKit is written in assembly (`SD0:/lib/uikit.so`). From C, every function takes `long` arguments: a pointer is passed cast, `(long) p`, and a text returned by the kit is read back through `(const char *)`.

## Opening the kit

```c
#include <uikit.h>                      // uikit_abi.h (prototypes, constants) + struct ui_vars and the uikit_* helpers

static struct ui_vars ui;               // the current window: handle, pixels, size, last event, fonts

// at the start of the program:
	if (!uikit_open (&ui)) return 1;    // the kit open (and AppKit, which it uses), the fonts loaded
```

- `uikit.h` is in `/gcc/include/kits`; nothing to add when linking: `pibuild` takes every `/gcc/lib/lib*.a` library, including the UIKit stubs.
- `struct ui_vars` holds `win` (handle of the current window), `surf` (its pixels, `0x00RRGGBB`, stride = `w * 4`), `w`, `h`, `ev` (the last event) and the three fonts `font` (Inter), `font_b` (Inter SemiBold), `font_m` (DejaVu Sans Mono), passed to the text functions as `(long) ui.font`.
- `ui_ev_field (&ui, EV_CODE)` reads a 32-bit field of the event.
- Functions that return several values have a helper in `uikit.h`: `uikit_w_event`, `uikit_list_get`, `uikit_drop_get`, `uikit_list_sorted`, `uikit_text_hit`, `uikit_textbox_selection`, `uikit_textarea_caret`, `uikit_canvas_pixels`, `uikit_image_size`. For the others (`ui_tree_get`, `ui_clip_take`, `ui_clip_get_image`...), `uikit_call3` reads the second and third results: `uikit_call3 ((long (*) ()) ui_tree_get, tree, id, 0, 0, &data, 0)`.

## The window and the event loop

An app creates its window, then runs a loop: `ui_event` waits for an event, `uikit_w_event` hands it to the widget concerned, and the program reacts to what comes back.

```cpp
for (;;)
{
	long t = ui_event (200);                     // waits 200 ms at most -> type (0: nothing)
	if (t == WEV_CLOSE) break;                   // close button, or Quit in the app menu
	if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
	else if (t > 0)
	{
		long code = 0, taken = 0;
		long w = uikit_w_event (&code, &taken);  // w: the widget, code: UN_*, taken: 1 if it used the event
		if (code) on_widget (w, code);
	}
	else ui_w_idle ();                           // nothing during the wait: tooltips
	ui_view_flush ();                            // redraws the views marked by ui_view_dirty
}
```

### The Korin OS chrome

Windows have no title bar: Elegant draws the frame and three buttons (minimise, maximise, close) **at the top right of the content**, `FRAME_CAP_W` pixels wide. Nothing interactive may go there: leave that space empty (or put only a drawing there). The top 32 px of the content move the window as soon as the mouse moves 8 px while pressed; a plain click reaches the program or the widget as usual.

### Window functions

| Function | Arguments | Result |
|---|---|---|
| `ui_init` | variables (`&ui`) | 1 if the fonts are loaded (`uikit_open` calls it) |
| `ui_window` | width, height, title | handle (≤ 0: failure); window placed by Elegant |
| `ui_window_ex` | x, y (-1, -1: automatic), width, height, title, role `WF_*` | handle; `WF_RESIZABLE`: resizable |
| `ui_title` | title | changes the title (shown by the menu bar and the panel) |
| `ui_close` | | closes the current window (its widgets stay: prefer `ui_window_close`) |
| `ui_screen` | buffer of 4 `long` | screen width and height, space reserved at the bottom then at the top (menu bar) |
| `ui_event` | wait in ms | type `WEV_*` (0: nothing), the event in `ui.ev` |
| `ui_update` | x, y, width, height | shows an area drawn in `ui.surf` |
| `ui_update_all` | | shows the whole window |
| `ui_resize` | width, height | the window changes size (new buffer, `FACE` background) |
| `ui_min_size` | width, height | minimum size of a `WF_RESIZABLE` window |
| `ui_on_resize` | function `(width, height)`, 0 | called by `uikit_w_event` after a `WEV_RESIZE` |
| `ui_maximize` | | like the maximise button (maximised, or back to its previous size) |
| `ui_minimize` | | the current window minimised, like the button of its frame (`ui_raise` brings it back) |
| `ui_window_drag` | | during a press in the content: the window follows the mouse |
| `ui_frame_request` | | 1: a `WEV_FRAME` will come when Elegant has taken the image (animations) |
| `ui_hit` | x, y, width, height | 1 if the mouse (last event) is inside the rectangle |
| `ui_cursor` | shape `UC_*` | cursor over the window (`UC_ARROW`, `UC_TEXT`, `UC_HAND`, `UC_CROSS`, `UC_WAIT`, `UC_MOVE`...) |
| `ui_pointer_lock` | 1 takes, 0 releases | the window keeps the pointer: `WEV_MOUSE` of kind `WEV_M_REL`, `EV_X`, `EV_Y` = movement |

Some roles of `ui_window_ex`: `WF_RESIZABLE` (edges, maximise button), `WF_GLYPH_LIGHT` (light frame buttons, for dark content), `WF_BODY_DARK` (black frame, for black content).

### Events

| Type | When | Useful fields (`ui_ev_field`) |
|---|---|---|
| `WEV_KEY` (1) | a key | `EV_KIND` 1 press / 0 release, `EV_CODE` HID code (`KEY_ENTER`, `KEY_ESC`, `KEY_LEFT`...), `EV_CHAR` character, `EV_MODS` (`UI_MOD_CTRL`, `UI_MOD_SHIFT`, `UI_MOD_ALT`) |
| `WEV_MOUSE` (2) | the mouse | `EV_KIND` 0 move, 1 press, 2 release; `EV_X`, `EV_Y` in the content; `EV_CODE` buttons (bit 0 left, bit 1 right); `EV_MODS` |
| `WEV_CLOSE` (3) | close requested | `EV_WIN`: the window |
| `WEV_WHEEL` (4) | the wheel | `EV_Y`: steps (positive: up) |
| `WEV_FOCUS` (5) | keyboard gained or lost | `EV_KIND` 1 / 0 |
| `WEV_MENU` (7) | a menu bar choice | `EV_CODE`: id of the item |
| `WEV_RESIZE` (8) | new size requested | handled by `uikit_w_event`, then the `ui_on_resize` function |
| `WEV_DROP` (9) | a drop (drag and drop) | `EV_X`, `EV_Y`; data through `uikit_drop_get` |
| `WEV_FRAME` (10) | the image was taken | after `ui_frame_request` |
| `WEV_PAD` (11) | a gamepad | (see [EmuKit](emukit_en.md)) |
| `WEV_SCREEN` (12) | the resolution changed | `EV_X`, `EV_Y`: screen size |

Every event carries `EV_WIN`, the window receiving it (useful with [several windows](#several-windows)).

### Example: a window that draws

The smallest graphical program: a resizable window, drawn in the theme colours, redrawn whenever its size changes. It is in C; the other examples are in C++.

```c
// hello.c -- a window that draws (UIKit, no widget)
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;			// the theme tokens (TJ_*)

static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

// the whole content, at the given size; also called after a size change
static long paint (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_rbox (24, 44, w - 48, h - 68, 12, jet (TJ_SURFACE), jet (TJ_SURFACE));
	ui_text ((long) TR ("Bonjour, Korin OS"), 48, 92, jet (TJ_TEXTE1), 24, (long) ui.font_b, 0);
	ui_text_wrap ((long) TR ("Ce texte est coupé en lignes à la largeur de la carte. Agrandissez la fenêtre par son bord droit ou son bord bas."),
		48, 110, w - 96, jet (TJ_TEXTE2), (long) ui.font, 14, 0);
	// three anti-aliased discs (coordinates in 1/16 pixel)
	unsigned c[3] = { jet (TJ_ACCENT), jet (TJ_SUCCES), jet (TJ_ALERTE) };
	for (int i = 0; i < 3; i++)
		ui_vp_disc ((72 + i * 56) * 16, (h - 72) * 16, 20 * 16, c[i]);
	ui_update_all ();
	return 0;
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 480, 320, (long) TR ("Bonjour"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (360, 260);
	ui_on_resize ((long) paint, 0);
	paint (ui.w, ui.h);
	for (;;)
	{
		long t = ui_event (1000);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			uikit_w_event (&code, &taken);	// (WEV_RESIZE: new size, then paint)
		}
	}
	return 0;
}
```

> **Note**: the user-visible texts of the examples are in French, the source language of Korin OS; `TR` (`ui_tr`) translates them through `lang/en.txt` (see [Translation](#translation)).

## Drawing and text

You draw into the pixels of the current window (or of a view, a canvas: see below), then `ui_update` or `ui_update_all` shows the result. Colours are `0x00RRGGBB` integers. Text is UTF-8; the `y` of `ui_text` is the **baseline**, not the top of the text.

| Function | Arguments | Result |
|---|---|---|
| `ui_fill` | x, y, width, height, colour | filled rectangle |
| `ui_frame` | x, y, width, height, colour | one-pixel outline |
| `ui_pixel` | x, y, colour | one pixel |
| `ui_rbox` | x, y, width, height, radius, top colour, bottom colour | rounded rectangle, with a gradient |
| `ui_ombre` | width, height | shadow and outline of a body inset by 4 px, radius 8 |
| `ui_clip` | x, y, width, height (width ≤ 0: none) | clips all following drawing |
| `ui_text` | text, x, baseline, colour, size px, font, max width (0: up to the edge) | x where the text ends |
| `ui_text_fit` | like `ui_text`, max width last | text cut with "…" if it overflows |
| `ui_text_width` | text, size px, font | width in pixels |
| `ui_text_prefix` | text, bytes, size px, font | width of the first bytes (to place a caret) |
| `ui_text_hit` | text, size px, font, x | bytes before point x (helper: `uikit_text_hit`, which also returns the exact x) |
| `ui_text_wrap` | text, x, y (top), width, colour, font, size px, 1: measure only | height used; breaks at spaces, `\n`: new line |
| `ui_load_font` | path of a `.ttf`, block of `UI_FONT_SIZE` bytes | 1 if loaded |
| `ui_font_italic` | | italic font (else the regular one) |
| `ui_font_get` | `UFN_*` | font: `UFN_SANS`, `UFN_BOLD`, `UFN_MONO`, `UFN_ITALIC`, `UFN_BOLDITALIC`, `UFN_MONOBOLD`, `UFN_SERIF`, `UFN_SERIFBOLD`, `UFN_SERIFITALIC`, `UFN_SERIFBOLDITALIC` |
| `ui_vp_disc` | cx, cy, radius, colour (1/16 px) | anti-aliased disc |
| `ui_vp_line` | x0, y0, x1, y1, thickness, colour (1/16 px) | thick line with round ends |
| `ui_vp_poly` | points (`int32_t` x, y pairs in 1/16 px), count, colour | filled polygon (even-odd) |
| `ui_icone` | app name, x, y, size | draws the app's icon |
| `ui_glyph` | name `glyph-*`, x, y, side, colour | a coloured interface glyph (a dot if the file is missing) |
| `ui_button` | x, y, width, height, label, 1: accent | draws a button (drawing only: for a real button, `ui_pushbutton`) |

The rasteriser advances a tab to the next stop (every four spaces) and does not draw the carriage return.

## Theme and tokens

`ui_jetons ()` returns the address of an array of 32-bit colours, the theme **tokens** (*jetons*). `TJ_*` is a byte offset: the token is read at index `TJ_x / 4`, and its high byte is masked out (`& 0xFFFFFF`).

```c
static const unsigned *s_jet;           // s_jet = (const unsigned *) ui_jetons ();
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }
```

| Token | Use |
|---|---|
| `TJ_FACE` | window background |
| `TJ_SURFACE` | cards, panels, background of a content area |
| `TJ_CREUX` | recessed areas (tracks, side panels) |
| `TJ_ELEVE`, `TJ_ELEVE_BAS` | raised elements (top, bottom of the gradient) |
| `TJ_LIGNE`, `TJ_LIGNE_FORTE` | lines, outlines |
| `TJ_TEXTE1`, `TJ_TEXTE2`, `TJ_TEXTE3` | main, secondary, greyed text |
| `TJ_ACCENT`, `TJ_ACCENT_CLAIR`, `TJ_ACCENT_FONCE`, `TJ_ACCENT_VIF` | accent colour and its variants (selection, focus) |
| `TJ_ACCENT_INK` | readable ink on `TJ_ACCENT` |
| `TJ_DANGER`, `TJ_SUCCES`, `TJ_ALERTE` | error, success, warning |
| `TJ_MENU` | menu background |
| `TJ_BARRE`, `TJ_DOCK`, `TJ_CONTOUR` | menu bar, dock, window outline (high byte: transparency) |
| `TJ_TITRE_ACTIF`, `TJ_TITRE_INACTIF` | frame of the active window, of the others |
| `TJ_SOMBRE` | 1 if the theme is dark (not a colour) |

| Function | Arguments | Result |
|---|---|---|
| `ui_jetons` | | address of the tokens (`TJ_*`, read-only) |
| `ui_theme` | | address of the desktop theme (`TH_*`: style, frames, accent...) |
| `ui_echelle` | | 0 large, 1 medium, 2 small (depending on the screen) |
| `ui_tone` | colour, level (128: unchanged) | colour lightened (> 128) or darkened (< 128) |
| `ui_mix` | colour a, colour b, 0 to 256 | blend (0: a, 256: b) |
| `ui_ink` | background colour | readable ink on it (light or dark) |
| `ui_theme_texte` | text of a `theme.txt` (0: the file) | theme preview (Settings) |

## Widgets

A creation function returns a **handle** (0: failure); the widget belongs to the current window. It redraws itself when it changes; `ui_w_draw (0)` draws them all, for instance after painting the background. `uikit_w_event` handles hover, keyboard focus (Tab, Shift+Tab, in creation order), dragging, double-click and the wheel, and returns a code:

| Code | Meaning |
|---|---|
| `UN_CHANGE` | value, selection or text changed |
| `UN_ACTIVATE` | button click, Enter, double-click on an item |
| `UN_SORT` | list sorted by a click on a header |
| `UN_CLOSE`, `UN_NEW` | tab strip: a tab's cross, "+" |
| `UN_DROP` | a drop on this widget (read it with `uikit_drop_get`) |
| `UN_CONTEXT` | right button (list: the row is selected) |

Coordinates are those of the window; a width or height of 0 (where stated) takes the natural size. The usual height of a control is 30 px.

### Common functions

| Function | Arguments | Result |
|---|---|---|
| `ui_w_event` | 0 (the event in `ui.ev`) | widget; code and "taken": helper `uikit_w_event (&code, &taken)` |
| `ui_w_draw` | widget (0: all of the current window) | draws and shows |
| `ui_w_move` | widget, x, y, width, height | position and size |
| `ui_w_show` | widget, 1 / 0 | shown / hidden |
| `ui_w_enable` | widget, 1 / 0 | enabled / greyed |
| `ui_w_focus` | widget | gives it the keyboard |
| `ui_w_get_focus` | | widget that has the keyboard |
| `ui_w_on` | widget, function `(widget, code)`, data | function called from `ui_w_event` |
| `ui_w_user` | widget | the data of `ui_w_on` |
| `ui_w_get_value`, `ui_w_set_value` | widget (, value) | value: selected item (-1: none), position, checked box... |
| `ui_w_set_range` | widget, min, max, page, step | range of a value; field: maximum length (`0, max, 0, 0`) |
| `ui_w_set_text`, `ui_w_get_text` | widget (, text) | text of a field, a label, a button; the returned text belongs to the kit |
| `ui_w_text_len` | widget | length of the text in bytes |
| `ui_w_style` | widget, flags | replaces the creation flags (e.g. `UTF_ERROR` on a field) |
| `ui_w_set_parent` | widget, panel (0: the window) | puts the widget in a panel, a tab page, a view |
| `ui_w_cursor` | widget, `UC_*` (-1: the type's own) | cursor on hover |
| `ui_w_tip` | widget, text (0: none) | tooltip after 600 ms of hovering |
| `ui_w_idle` | | call it when `ui_event` returns 0: shows tooltips |
| `ui_tip_at` | x, y, text (0: removes) | a tooltip right away, at a point of the window |
| `ui_w_window` | widget | its window |
| `ui_w_destroy` | widget | removes and frees |

### Buttons, labels, choices

| Function | Arguments | Result |
|---|---|---|
| `ui_pushbutton` | x, y, width, height (0: auto), text, `UB_DEFAULT` / `UB_GHOST` | button: `UN_ACTIVATE` |
| `ui_label` | x, y, width, height, text, `ULB_*` | label: `ULB_BOLD`, `ULB_DIM`, `ULB_WRAP`, `ULB_RIGHT`, `ULB_CENTER`, `ULB_TITLE` |
| `ui_checkbox` | x, y, width, height, label, 1: checked | check box, value 0 / 1, `UN_CHANGE` |
| `ui_radio` | x, y, width, height, label, group | radio button; those of the same group exclude each other |
| `ui_switch` | x, y, width, height, label, 1: on | switch |
| `ui_dropdown` | x, y, width, height, items | drop-down list; value = selected item |
| `ui_combobox` | x, y, width, height, items | editable list; `ui_w_get_text`: the typed or chosen text |
| `ui_segmented` | x, y, width, height (0: auto), segments | segmented control; value = selected segment |

The items of a drop-down list, segments or tabs are written in one string, separated by vertical bars: `"Rouge|Vert|Bleu"`. A drop-down list also accepts `ui_list_add`, `ui_list_clear` and `uikit_list_get`, like a list.

### Text fields

| Function | Arguments | Result |
|---|---|---|
| `ui_textbox` | x, y, width, height, `UTF_*` | single-line field; Enter: `UN_ACTIVATE`; `UTF_PASSWORD`, `UTF_READONLY`, `UTF_MONO`, `UTF_RIGHT`, `UTF_ERROR` |
| `ui_textbox_hint` | field, hint | greyed text while the field is empty |
| `ui_textbox_select` | field, start, end (bytes) | selects part of the text |
| `ui_textbox_selection` | field | start (helper: `uikit_textbox_selection`, which also returns the end) |
| `ui_textarea` | x, y, width, height, flags | multi-line text (1 MiB by default); flags below |
| `ui_textarea_caret` | multi-line text | line (helper: `uikit_textarea_caret`, which also returns the column) |
| `ui_textarea_goto` | multi-line text, offset | caret at that offset, scrolled into view |
| `ui_textarea_insert` | multi-line text, text | inserts at the caret (replacing the selection) → `UN_CHANGE` |
| `ui_textarea_edit` | multi-line text, `UTE_*` | `UTE_UNDO`, `UTE_REDO`, `UTE_CUT`, `UTE_COPY`, `UTE_PASTE`, `UTE_ALL` → `UN_CHANGE` if the text changes |
| `ui_textarea_find` | multi-line text, word | 1 found (and selected), case-insensitive, after the selection |
| `ui_textarea_highlight` | multi-line text, line (-1: none) | a highlighted line (error, single-stepping) |
| `ui_textarea_keywords` | multi-line text, function `(word, length)` → 1 if keyword | keywords of the code editor |
| `ui_textarea_font` | multi-line text, size px (0: the interface size) | text size |
| `ui_textarea_paper` | multi-line text, paper (-1: the theme), ink | background and text colours |

Flags of `ui_textarea`: `UTF_READONLY`, `UTF_MONO`, `UTA_LINENUM` (line numbers), `UTA_VSCROLL`, `UTA_HSCROLL` (scroll bars), `UTA_CODE` (assembly colours; with `UTA_BASIC`, `UTA_CS` or `UTA_XML`: BASIC, C#, XML), `UTA_TABS` (Tab inserts a tab), `UTA_FORMTAB` (in a form, Tab moves to the next field). Both kinds of field know Ctrl+A, C, X, V; the multi-line text also has Undo and Redo (Ctrl+Z, Ctrl+Y).

### Values

| Function | Arguments | Result |
|---|---|---|
| `ui_slider` | x, y, width, height, min, max, value | slider (vertical if taller than wide) |
| `ui_progress` | x, y, width, height, max, value | progress bar |
| `ui_numeric` | x, y, width, height, min, max, value | number with + and − buttons |
| `ui_knob` | x, y, width, height, min, max, value | rotary knob |
| `ui_vumeter` | x, y, width, height, max | VU meter |
| `ui_vumeter_level` | VU meter, level | level (the peak is held) |
| `ui_lcd` | x, y, width, height, text | seven-segment display (digits, `-`, `:`, `.`) |
| `ui_scrollbar` | x, y, width, height, `USB_HORIZONTAL` or 0 | scroll bar; range through `ui_w_set_range (bar, 0, content, visible, step)` |

### Lists and trees

| Function | Arguments | Result |
|---|---|---|
| `ui_list` | x, y, width, height, `UL_*` | list with one or more columns |
| `ui_list_column` | list, title, width, `ULC_RIGHT` or 0 | adds a column |
| `ui_list_add` | list, text (columns separated by `\t`), data | item number |
| `ui_list_set` | list, number, text | replaces the text |
| `ui_list_get` | list, number | text (helper: `uikit_list_get (list, n, &data)`) |
| `ui_list_remove` | list, number | removes |
| `ui_list_clear` | list | empties |
| `ui_list_count` | list | number of items |
| `ui_list_sort` | list, column, 1: descending | sorts |
| `ui_list_sorted` | list | sorted column (helper: `uikit_list_sorted`, which also gives the direction) |
| `ui_list_virtual` | list, rows, function `(list, row)` → text | virtual list: the program keeps its rows |
| `ui_list_cell` | list, function `(list, row, column, x, y, w, h, selected)` → 1 drawn | cells drawn by the program |
| `ui_tree` | x, y, width, height | tree view; value = selected id |
| `ui_tree_add` | tree, parent (-1: root), label, data | id |
| `ui_tree_expand` | tree, id, 1 expands / 0 collapses | |
| `ui_tree_get` | tree, id | label (the data comes back in the second register) |
| `ui_tree_clear` | tree | empties |

Options of `ui_list`: `UL_HEADER` (headers), `UL_NOSORT`, `UL_USERSORT` (a header click returns `UN_SORT`, the program sorts), `UL_DRAG` (an item's text can be dragged), `UL_STRIPES`, `UL_GRID`, `UL_HSCROLL`, `UL_SIDEBAR` (side panel: an item `#title` is a section), `UL_ICONS` (icon from the data & 3: 0 file, 1 folder, 2 volume).

### Containers, tabs, toolbars

| Function | Arguments | Result |
|---|---|---|
| `ui_panel` | x, y, width, height, `UPN_SURFACE` or 0 | panel; its children are shown and hidden with it |
| `ui_groupbox` | x, y, width, height, title | titled group frame (a panel) |
| `ui_layout` | panel, `ULY_VSTACK` / `ULY_HSTACK` / `ULY_GRID`, gap, columns | places the children (does not redraw) |
| `ui_splitter` | x, y, width, height, min, max | draggable splitter; value = its position, `UN_CHANGE` |
| `ui_tabs` | x, y, width, height, tabs | tabs with pages |
| `ui_tabs_page` | tabs, number | panel of the page (put your widgets in it with `ui_w_set_parent`) |
| `ui_tabstrip` | x, y, width, height, `UTS_CLOSE` / `UTS_PLUS` | document tab strip |
| `ui_tabstrip_add` | strip, title, data | number (the tab is selected) |
| `ui_tabstrip_closing` | strip | the tab whose cross was clicked (after `UN_CLOSE`; remove it with `ui_list_remove`) |
| `ui_toolbar` | x, y, width, height (0: auto) | toolbar (`UTBS_NOLINE` through `ui_w_style`: no bottom line) |
| `ui_toolbar_add` | toolbar, glyph, label, `UTB_TOGGLE` / `UTB_SEP` | ghost button: `UN_ACTIVATE`; toggle: value 0 / 1 |

### Dates, colours, gamepad

| Function | Arguments | Result |
|---|---|---|
| `ui_calendar` | x, y, width, height (0: auto), date | month calendar; date = `year << 16` + `month << 8` + day |
| `ui_calendar_today` | calendar, date | circled day (UIKit has no clock: the program gives it) |
| `ui_calendar_show` | calendar, date | shows and selects that day |
| `ui_datepicker` | x, y, width, height, date | date field, calendar below |
| `ui_datepicker_today` | picker, date | today |
| `ui_date_popup` | widget, date, today | calendar below a widget → chosen day (-1: cancelled) |
| `ui_palette` | x, y, width, height (0: auto), colour | 8 × 5 palette |
| `ui_colorpicker` | x, y, width, height, colour | swatch; a click opens the colour dialog |
| `ui_pad_hints` | `"A\tchoose\nB\tback"`, x, y, height, 1: measure | gamepad hints; width |
| `ui_objnav` | rectangles (`int32_t` x, y, w, h), count, current, direction (0 left, 1 right, 2 up, 3 down) | the neighbouring object (-1: none) |

### Example: a form

A form on the left, a list on the right: **Ajouter** (Add, or Enter in the field) puts a row in the list, a double-click on a row puts its name back in the field. The slider updates its label through an attached function (`ui_w_on`); the other widgets go through the loop.

```cpp
// form.cpp -- a form of widgets (UIKit)
#include <stdio.h>
#include <string.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

enum { M = 16, CH = 30, LW = 96, FW = 340 };	// margin, control height, labels, form width
static long g_name, g_kind, g_size, g_sizeVal, g_bold, g_left, g_center, g_note, g_confirm, g_add, g_clear, g_list;

// the slider: called by ui_w_event (ui_w_on)
static long on_size (long w, long code)
{
	if (code != UN_CHANGE) return 0;
	char t[16];
	snprintf (t, sizeof t, "%ld px", ui_w_get_value (w));
	ui_w_set_text (g_sizeVal, (long) t);
	return 0;
}

static void add_row ()
{
	const char *name = (const char *) ui_w_get_text (g_name);
	if (!name || !*name)
	{
		ui_w_style (g_name, UTF_ERROR);			// red outline
		ui_message ((long) TR ("Formulaire"), (long) TR ("Donnez un nom."), UM_OK);
		ui_w_focus (g_name);
		return;
	}
	ui_w_style (g_name, 0);
	long data = 0;
	const char *kind = (const char *) uikit_list_get (g_kind, ui_w_get_value (g_kind), &data);
	char row[256];
	snprintf (row, sizeof row, "%s\t%s\t%ld\t%s%s", name, kind ? kind : "", ui_w_get_value (g_size),
		ui_w_get_value (g_bold) ? TR ("gras, ") : "", ui_w_get_value (g_center) ? TR ("centré") : TR ("à gauche"));
	ui_list_add (g_list, (long) row, 0);
	ui_w_set_text (g_name, (long) "");
	ui_w_focus (g_name);
}

static void clear_list ()
{
	if (ui_w_get_value (g_confirm) &&
	    ui_message ((long) TR ("Formulaire"), (long) TR ("Effacer toute la liste ?"), UM_YESNO) != 1) return;
	ui_list_clear (g_list);
}

// a list row: its name (the first column) goes back into the field
static void pick_row ()
{
	long row = ui_w_get_value (g_list), data = 0;
	if (row < 0) return;
	const char *t = (const char *) uikit_list_get (g_list, row, &data);
	char name[128];
	int n = 0;
	while (t && t[n] && t[n] != '\t' && n < (int) sizeof name - 1) { name[n] = t[n]; n++; }
	name[n] = 0;
	ui_w_set_text (g_name, (long) name);
}

// the widgets follow the window size (the list takes the space)
static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_w_move (g_note, M + LW, 252, FW - LW, h - 252 - M - CH - 12);
	ui_w_move (g_add, M, h - M - CH, 120, CH);
	ui_w_move (g_clear, M + 128, h - M - CH, 140, CH);
	ui_w_move (g_list, FW + 2 * M, 52, w - FW - 3 * M, h - 52 - M);
	ui_w_draw (0);
	ui_update_all ();
	return 0;
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 760, 480, (long) TR ("Formulaire"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (680, 420);

	// (the title is not interactive: it may go in the top strip that moves the window)
	ui_label (M, 12, FW, 28, (long) TR ("Nouvel élément"), ULB_TITLE);
	int y = 52;
	ui_label (M, y + 6, LW, 20, (long) TR ("Nom"), 0);
	g_name = ui_textbox (M + LW, y, FW - LW, CH, 0);
	ui_textbox_hint (g_name, (long) TR ("Obligatoire"));
	y += 40;
	ui_label (M, y + 6, LW, 20, (long) TR ("Genre"), 0);
	g_kind = ui_dropdown (M + LW, y, FW - LW, CH, (long) TR ("Titre|Paragraphe|Note"));
	ui_w_set_value (g_kind, 0);
	y += 40;
	ui_label (M, y + 6, LW, 20, (long) TR ("Taille"), 0);
	g_size = ui_slider (M + LW, y, FW - LW - 60, CH, 8, 48, 14);
	g_sizeVal = ui_label (M + FW - 52, y + 6, 52, 20, (long) "14 px", ULB_DIM | ULB_RIGHT);
	ui_w_on (g_size, (long) on_size, 0);
	y += 40;
	g_bold = ui_checkbox (M + LW, y, 80, CH, (long) TR ("Gras"), 0);
	g_left = ui_radio (M + LW + 90, y, 80, CH, (long) TR ("Gauche"), 1);
	g_center = ui_radio (M + LW + 170, y, 90, CH, (long) TR ("Centre"), 1);
	ui_w_set_value (g_left, 1);
	y += 40;
	g_confirm = ui_switch (M + LW, y, FW - LW, CH, (long) TR ("Confirmer l'effacement"), 1);
	y += 40;
	ui_label (M, y + 6, LW, 20, (long) TR ("Remarque"), 0);
	g_note = ui_textarea (0, 0, 10, 10, UTA_FORMTAB | UTA_VSCROLL);	// (placed by on_resize, at y = 252)
	g_add = ui_pushbutton (0, 0, 10, CH, (long) TR ("Ajouter"), UB_DEFAULT);
	g_clear = ui_pushbutton (0, 0, 10, CH, (long) TR ("Effacer la liste"), 0);
	ui_w_tip (g_clear, (long) TR ("Vide la liste de droite"));

	g_list = ui_list (0, 0, 10, 10, UL_HEADER | UL_STRIPES);
	ui_list_column (g_list, (long) TR ("Nom"), 150, 0);
	ui_list_column (g_list, (long) TR ("Genre"), 100, 0);
	ui_list_column (g_list, (long) TR ("Taille"), 60, ULC_RIGHT);
	ui_list_column (g_list, (long) TR ("Style"), 140, 0);

	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);
	ui_w_focus (g_name);

	for (;;)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code == UN_ACTIVATE && (w == g_add || w == g_name)) add_row ();
			else if (code == UN_ACTIVATE && w == g_clear) clear_list ();
			else if (code == UN_ACTIVATE && w == g_list) pick_row ();
		}
		else ui_w_idle ();
	}
	return 0;
}
```

## Hand-drawn views

A **view** (`ui_view`) is a widget whose content the program draws itself: a document, a grid, a chart, a game. UIKit gives it its place, its own pixels, hover, mouse capture during a drag, double-click, focus and its children. The program gives four functions:

| Program function | Arguments | Returns |
|---|---|---|
| paint | view, width, height | (nothing); the `ui_*` functions draw into the view, corner (0, 0) |
| mouse | view, kind (0 move, 1 press, 2 release, 3 leave), x, y, info | a `UN_*` code (0: nothing); info = clicks + buttons `<< 8` + modifiers `<< 16` |
| key | view, HID code, character, modifiers | -1 if the key is not taken (it stays with the program, e.g. for shortcuts) |
| wheel | view, steps (positive: up) | a `UN_*` code |

Mark the view with `ui_view_dirty` when its content changes; `ui_view_flush`, on every turn of the loop, redraws the marked views. A view that is not marked is copied as it is.

| Function | Arguments | Result |
|---|---|---|
| `ui_view` | x, y, width, height, `UVW_FOCUS` or 0 | view; `UVW_FOCUS`: it takes the keyboard (click, Tab) |
| `ui_view_on` | view, paint, mouse, key, wheel (0: none) | attaches the functions and marks the view |
| `ui_view_dirty` | view | to be redrawn |
| `ui_view_flush` | | redraws and shows the marked views of the current window |

The children of a view (`ui_w_set_parent`) are clipped by it: a scrolling form is made by moving its widgets (`ui_w_move`) then `ui_view_dirty`.

### Canvases and surfaces

A **canvas** is an image of your own, shown as a widget; between `ui_canvas_begin` and `ui_canvas_end`, every UIKit drawing goes into it. A **surface** is a shared buffer that Elegant composes directly into the window, without a copy: for video, emulators, GPU rendering ([GpuKit](gpukit_en.md), [MediaKit](mediakit_en.md)).

| Function | Arguments | Result |
|---|---|---|
| `ui_canvas` | x, y, width, height | canvas (white); the mouse returns `UN_CHANGE` on it |
| `ui_canvas_pixels` | canvas | pixels (helper: `uikit_canvas_pixels (c, &w, &h)`) |
| `ui_canvas_begin` | canvas | drawing goes into the canvas |
| `ui_canvas_end` | | end; show it with `ui_w_draw (canvas)` |
| `ui_surface` | x, y (area of the content), width, height, `WS_SF_XRGB8888` / `WS_SF_RGB565` | surface (0: failure) |
| `ui_surface_pixels` | surface | pixels (stride, width and height follow in x1 to x3) |
| `ui_surface_damage` | surface, x, y, width, height (width ≤ 0: everything) | reports a changed area (no `ui_update`) |
| `ui_surface_damage_list` | surface, rectangles (x, y, w, h in 16 bits), count | several areas |
| `ui_surface_move` | surface, x, y, width, height | moves or resizes → 1 |
| `ui_surface_source` | surface, shared buffer (-1: its own), stride, offset | composes from a buffer written elsewhere → 1 |
| `ui_surface_free` | surface | detached, buffer released |

### Example: a view with the mouse and the wheel

A sketch pad: you draw by dragging with the left button, the wheel changes the brush thickness (the slider follows), the right button opens a context menu, Backspace removes the last stroke.

```cpp
// sketch.cpp -- a hand-drawn view: mouse, wheel, keyboard (UIKit)
#include <stdio.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

enum { TOPH = 48, MAXP = 8192 };
struct Point { short x, y; unsigned char size, start; };	// start: first point of a stroke
static Point g_pts[MAXP];
static int g_np, g_brush = 6;
static long g_view, g_slider, g_clear;
static bool g_menu;						// context menu requested
static int g_menuX, g_menuY;					// (in the window)

static void view_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_SURFACE));
	for (int x = 32; x < w; x += 32) ui_fill (x, 0, 1, h, jet (TJ_LIGNE));
	for (int y = 32; y < h; y += 32) ui_fill (0, y, w, 1, jet (TJ_LIGNE));
	unsigned ink = jet (TJ_ACCENT);
	for (int i = 0; i < g_np; i++)
	{
		const Point &p = g_pts[i];
		if (p.start) ui_vp_disc (p.x * 16, p.y * 16, p.size * 8, ink);
		else ui_vp_line (g_pts[i - 1].x * 16, g_pts[i - 1].y * 16, p.x * 16, p.y * 16, p.size * 16, ink);
	}
	char t[64];
	snprintf (t, sizeof t, TR ("%d points, pinceau de %d px"), g_np, g_brush);
	ui_text ((long) t, 12, h - 12, jet (TJ_TEXTE2), 12, (long) ui.font, 0);
}

static void add_point (long x, long y, bool start)
{
	if (g_np >= MAXP) return;
	if (g_np == 0) start = true;
	g_pts[g_np++] = { (short) x, (short) y, (unsigned char) g_brush, (unsigned char) start };
	ui_view_dirty (g_view);
}

static void undo_stroke ()
{
	while (g_np > 0 && !g_pts[--g_np].start) {}
	ui_view_dirty (g_view);
}

static void set_brush (int b)
{
	g_brush = b < 1 ? 1 : b > 40 ? 40 : b;
	ui_w_set_value (g_slider, g_brush);
	ui_view_dirty (g_view);
}

static long view_mouse (long, long kind, long x, long y, long info)
{
	int buttons = (int) (info >> 8) & 0xFF;
	if (kind == 1 && (buttons & 2)) { g_menu = true; g_menuX = (int) x; g_menuY = (int) y + TOPH; }
	else if (kind == 1 && (buttons & 1)) add_point (x, y, true);
	else if (kind == 0 && (buttons & 1)) add_point (x, y, false);	// drag: the view keeps the mouse
	return 0;
}

static long view_wheel (long, long steps)
{
	set_brush (g_brush + (int) steps);
	return 0;
}

static long view_key (long, long code, long, long)
{
	if (code == KEY_BACKSPACE) { undo_stroke (); return 0; }
	return -1;							// not taken: left to the program
}

static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_w_move (g_view, 0, TOPH, w, h - TOPH);
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	return 0;
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 720, 500, (long) TR ("Croquis"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (420, 300);

	ui_label (16, 14, 70, 20, (long) TR ("Pinceau"), ULB_DIM);
	g_slider = ui_slider (86, 9, 180, 30, 1, 40, g_brush);
	g_clear = ui_pushbutton (280, 9, 0, 30, (long) TR ("Effacer"), 0);
	g_view = ui_view (0, 0, 10, 10, UVW_FOCUS);
	ui_view_on (g_view, (long) view_paint, (long) view_mouse, (long) view_key, (long) view_wheel);
	ui_w_cursor (g_view, UC_CROSS);
	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);
	ui_w_focus (g_view);

	for (;;)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (w == g_slider && code == UN_CHANGE) set_brush ((int) ui_w_get_value (w));
			else if (w == g_clear && code == UN_ACTIVATE) { g_np = 0; ui_view_dirty (g_view); }
		}
		if (g_menu)
		{
			g_menu = false;
			// labels one after the other, an empty string at the end; "-": a line (it counts in the numbers)
			static const char items[] = "Annuler le trait\0-\0Pinceau fin\0Pinceau épais\0Tout effacer\0";
			switch (ui_popup_local (g_menuX, g_menuY, (long) items, 200))
			{
			case 0: undo_stroke (); break;
			case 2: set_brush (2); break;
			case 3: set_brush (16); break;
			case 4: g_np = 0; ui_view_dirty (g_view); break;
			}
		}
		ui_view_flush ();
	}
	return 0;
}
```

## Dialogs

UIKit dialogs are modal: one more real window (role `WF_MODAL`), which blocks **all** the other windows of the program until the answer. They translate their own buttons.

| Function | Arguments | Result |
|---|---|---|
| `ui_message` | title, text, `UM_OK` / `UM_OKCANCEL` / `UM_YESNO` / `UM_YESNOCANCEL` | 1 OK / Yes, 0 Cancel / No; 2: No of `UM_YESNOCANCEL`; Esc: 0 |
| `ui_ask` | title, question, buffer (initial text), size | 1 OK, 0 Cancel; the answer in the buffer |
| `ui_file_open` | starting folder, buffer, size | 1: the chosen path in the buffer |
| `ui_file_save` | folder, suggested name, buffer, size | 1; asks "Replace?" if the file exists |
| `ui_folder_choose` | folder, buffer, size | 1: the chosen folder |
| `ui_file_filter` | patterns (`"*.txt;*.md"`, 0: all) | files shown by the following dialogs |
| `ui_color_dialog` | title, colour | chosen colour (-1: Cancel) |
| `ui_dialog_begin` | width, height, title | dialog; it becomes the current window: widgets created next belong to it |
| `ui_dialog_button` | dialog, slot 0 to 2, text, answer, `UB_*`, right edge | right edge for the next button |
| `ui_dialog_run` | dialog, function `(dialog, widget, code)` or 0, 0 | the answer (button, Esc, Enter) |
| `ui_dialog_close` | dialog, answer | ends `ui_dialog_run` (from the function) |
| `ui_dialog_cancel` | dialog, answer | answer for Esc and the close button |
| `ui_dialog_timer` | dialog, function `(dialog)`, period ms | called while waiting (countdown) |
| `ui_dialog_end` | dialog | its widgets destroyed, window closed, the previous window current again |
| `ui_dialog_at` | x, y | the next `ui_dialog_begin`: frameless, above everything, at that point (full screen of a slide show) |

`ui_file_filter` also accepts several named types, separated by vertical bars: `"Textes|*.txt;*.md|Tous|*"`; `ui_file_save` then adds the extension of the chosen type to a name that has none.

A dialog of your own:

```cpp
// (excerpt) ask for a name and an option
long d = ui_dialog_begin (420, 170, (long) TR ("Renommer"));
long field = ui_textbox (16, 48, 388, 30, 0);
ui_w_set_text (field, (long) old_name);
long keep = ui_checkbox (16, 88, 388, 24, (long) TR ("Garder l'extension"), 1);
long e = ui_dialog_button (d, 0, (long) TR ("Renommer"), 1, UB_DEFAULT, 420 - 12);
ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, e);
ui_dialog_cancel (d, 0);
ui_w_focus (field);
long r = ui_dialog_run (d, 0, 0);			// 1: Rename, 0: Cancel or Esc
// read the widgets BEFORE ui_dialog_end, which destroys them
snprintf (new_name, sizeof new_name, "%s", (const char *) ui_w_get_text (field));
bool keep_ext = ui_w_get_value (keep) != 0;
ui_dialog_end (d);
```

## Menus and shortcuts

Each app publishes its menus in the **menu bar** at the top of the screen with `ui_menu`, after creating its window. The menu text is made of lines:

- `M<title>` opens a menu;
- `I<id>\t<label>\t<shortcut>` an item (decimal id ≥ 0, optional shortcut); right after the id, `d` greys the item out and `c` checks it (`I4c\tMono`);
- `-` a separator.

The bar adds by itself the menu named after the app (with **Quit**, which arrives as `WEV_CLOSE`) and the **Help** menu when the app has a `help_fr.md` or a `help_en.md`. A choice arrives as `WEV_MENU`, the id in `EV_CODE`. The shortcut is only displayed: the program passes the keys its widgets did not take to `ui_menu_key`, which returns the id of the item with that shortcut ("Ctrl+S", "Ctrl+Maj+Z", "Alt+X", "F3"; *Maj* is Shift).

| Function | Arguments | Result |
|---|---|---|
| `ui_menu` | menu text (0: none) | 0 (< 0: more than 2048 bytes); the labels go through `ui_tr` |
| `ui_menu_state` | id, `UMS_DISABLED` / `UMS_CHECKED` / 0 | greys out, checks or restores an item |
| `ui_menu_key` | 0 (the event in `ui.ev`) | id of the shortcut (-1: none) |
| `ui_popup_local` | x, y (in the window), labels, width | context menu → chosen number (-1: cancelled) |
| `ui_popup` | x, y (on the screen), labels, width | pop-up menu → number (-1: cancelled) |
| `ui_popup_ex` | like `ui_popup`, flags | bit 0: `icon\ttext`; bit 4: `text\tshortcut`; bit 1: stay open on a submenu (`>text`) |
| `ui_popup_close` | | closes a menu kept open |
| `ui_menu_get`, `ui_menu_command` | | for the menu bar itself (read the menu of the active app, pass on a choice) |

The labels of a pop-up menu follow one another in one string, each ended by a NUL, with an empty string at the end; `-` is a line, a first byte `0x01` greys out the item.

> **Note**: UIKit remembers the translation of a menu text by its address. To change the menus, pass **another** buffer (two buffers in turn), or keep the same text and change the states with `ui_menu_state`.

### Example: menus and file dialog

A small text editor: menus **Fichier** (File: New, Open, Save, Save as), **Édition** (Edit: Undo, Cut, Copy, Paste, Select all) and **Présentation** (View: a checked item), shortcuts, open and save dialogs, and a question before losing changes.

```cpp
// notepad.cpp -- menus, shortcuts, file dialogs (UIKit)
#include <stdio.h>
#include <string.h>
#include <string>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

enum { M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_ALL, M_MONO };
static const char MENUS[] =
	"MFichier\n"
	"I1\tNouveau\tCtrl+N\n" "I2\tOuvrir...\tCtrl+O\n" "-\n"
	"I3\tEnregistrer\tCtrl+S\n" "I4\tEnregistrer sous...\tCtrl+Maj+S\n"
	"MÉdition\n"
	"I5\tAnnuler\tCtrl+Z\n" "I6\tRétablir\tCtrl+Y\n" "-\n"
	"I7\tCouper\tCtrl+X\n" "I8\tCopier\tCtrl+C\n" "I9\tColler\tCtrl+V\n" "-\n" "I10\tTout sélectionner\tCtrl+A\n"
	"MPrésentation\n"
	"I11\tPolice à chasse fixe\n";

static long g_text;
static std::string g_path, g_saved;		// the file, and its text as saved
static bool g_mono;

static std::string base_name (const std::string &p)
{
	size_t s = p.find_last_of ("/:");
	return s == std::string::npos ? p : p.substr (s + 1);
}

static void set_title ()
{
	std::string t = g_path.empty () ? std::string (TR ("Sans titre")) : base_name (g_path);
	ui_title ((long) (t + " - " + TR ("Bloc-notes")).c_str ());
}

static const char *text () { const char *t = (const char *) ui_w_get_text (g_text); return t ? t : ""; }

static bool save_to (const std::string &path)
{
	FILE *f = fopen (path.c_str (), "wb");
	const char *t = text ();
	size_t n = strlen (t);
	bool ok = f && fwrite (t, 1, n, f) == n;
	if (f) fclose (f);
	if (!ok) { ui_message ((long) TR ("Bloc-notes"), (long) TR ("Impossible d'enregistrer le fichier."), UM_OK); return false; }
	g_path = path; g_saved = t;
	set_title ();
	return true;
}

static bool save_as ()
{
	char p[512];
	ui_file_filter ((long) "*.txt;*.md");
	std::string name = g_path.empty () ? "notes.txt" : base_name (g_path);
	if (ui_file_save ((long) "SD0:/home", (long) name.c_str (), (long) p, sizeof p) != 1) return false;
	return save_to (p);
}

static bool save () { return g_path.empty () ? save_as () : save_to (g_path); }

// changes to lose? -> true: we may go on
static bool may_discard ()
{
	if (g_saved == text ()) return true;
	long r = ui_message ((long) TR ("Bloc-notes"), (long) TR ("Enregistrer les changements ?"), UM_YESNOCANCEL);
	if (r == 1) return save ();
	return r == 2;					// No: the changes are lost; Cancel (0): we stay
}

static void load (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f) { ui_message ((long) TR ("Bloc-notes"), (long) TR ("Impossible d'ouvrir le fichier."), UM_OK); return; }
	std::string s;
	char buf[4096];
	size_t n;
	while ((n = fread (buf, 1, sizeof buf, f)) > 0) s.append (buf, n);
	fclose (f);
	ui_w_set_text (g_text, (long) s.c_str ());
	g_path = path; g_saved = s;
	set_title ();
}

static void command (long id)
{
	switch (id)
	{
	case M_NEW:
		if (!may_discard ()) break;
		ui_w_set_text (g_text, (long) "");
		g_path.clear (); g_saved.clear ();
		set_title ();
		break;
	case M_OPEN:
	{
		if (!may_discard ()) break;
		char p[512];
		ui_file_filter ((long) "*.txt;*.md");
		if (ui_file_open ((long) "SD0:/home", (long) p, sizeof p) == 1) load (p);
		break;
	}
	case M_SAVE: save (); break;
	case M_SAVEAS: save_as (); break;
	case M_UNDO: ui_textarea_edit (g_text, UTE_UNDO); break;
	case M_REDO: ui_textarea_edit (g_text, UTE_REDO); break;
	case M_CUT: ui_textarea_edit (g_text, UTE_CUT); break;
	case M_COPY: ui_textarea_edit (g_text, UTE_COPY); break;
	case M_PASTE: ui_textarea_edit (g_text, UTE_PASTE); break;
	case M_ALL: ui_textarea_edit (g_text, UTE_ALL); break;
	case M_MONO:
		g_mono = !g_mono;
		ui_w_style (g_text, UTA_VSCROLL | (g_mono ? UTF_MONO : 0));
		ui_w_draw (g_text);
		ui_menu_state (M_MONO, g_mono ? UMS_CHECKED : 0);
		break;
	}
	ui_w_focus (g_text);
}

static long on_resize (long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_w_move (g_text, 8, 36, w - 16, h - 44);	// (the top 36 px: the window moves)
	ui_w_draw (0);
	ui_update_all ();
	return 0;
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, 640, 460, (long) TR ("Bloc-notes"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (360, 240);
	g_text = ui_textarea (0, 0, 10, 10, UTA_VSCROLL);
	ui_menu ((long) MENUS);
	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);
	if (argc > 1) load (argv[1]);			// (a file given by Files or the Terminal)
	set_title ();
	ui_w_focus (g_text);

	for (;;)
	{
		long t = ui_event (200);
		if (t == WEV_CLOSE) { if (may_discard ()) break; continue; }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			uikit_w_event (&code, &taken);
			if (press && !taken)
			{
				long id = ui_menu_key (0);	// Ctrl+S, Ctrl+O...
				if (id >= 0) command (id);
			}
		}
		else ui_w_idle ();
	}
	return 0;
}
```

The labels in the code are in French, the source language of Korin OS; `ui_menu` translates them (see [Translation](#translation)).

## Clipboard and drag and drop

The clipboard is kept by the `clipd` service, which holds the last ten copies, each with its representations (a format, its bytes). `ui_clip_set` and `ui_clip_get` are enough for text.

| Function | Arguments | Result |
|---|---|---|
| `ui_clip_set` | text, bytes (-1: up to the NUL) | copies a text |
| `ui_clip_get` | buffer, size | bytes read (NUL-terminated text) |
| `ui_clip_put` | format, data, bytes, copy to complete (0: new) | id of the copy (0: refused); several formats for one copy |
| `ui_clip_take` | formats (`"rtf\0text\0\0"`, preferred first), buffer (0: size only), size | bytes (0: nothing); the rank of the format found follows in the second register |
| `ui_clip_set_image` | pixels `0x00RRGGBB`, width, height | 1 (0: refused) |
| `ui_clip_get_image` | buffer (0: size only), size | width (0: no image); height and bytes follow in the second and third registers |
| `ui_clip_set_files` | paths separated by `\n`, 1: cut | 1 |
| `ui_clip_get_files` | buffer, size | bytes (0: none); "cut" follows in the second register |
| `ui_clip_done_cut` | | the cut files have been moved: the copy goes away |

Known formats: `text`, `image`, `files`, `files-cut`, `url`, `rtf`, and `x-<app>` for an app's own formats.

**Drag and drop**: during a press in its window, a program starts a drag with `ui_drag_start`; on release, the window underneath receives `WEV_DROP`. `uikit_w_event` inserts a text dropped on a field (`UN_CHANGE`); on another widget, it returns `UN_DROP`, and the program reads the data.

| Function | Arguments | Result |
|---|---|---|
| `ui_drag_start` | data, bytes (-1: up to the NUL; 464 at most), `UDT_TEXT` / `UDT_FILES` | starts a drag |
| `ui_drop_get` | buffer, size | bytes of the last drop (helper: `uikit_drop_get (buffer, size, &kind)`) |

```cpp
// (excerpt) files dropped on the window, from Files
if (t == WEV_DROP)
{
	long code = 0, taken = 0;
	uikit_w_event (&code, &taken);			// a text on a field: inserted (UN_CHANGE)
	if (code != UN_CHANGE)
	{
		static char b[1024];
		long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
		if (n > 0 && kind == UDT_FILES) { b[n] = 0; open_files (b); }	// one path per line
	}
}
```

## Images

UIKit reads **BMP** images (24 and 32 bits, palettes) and **PNG** (grey, colour, palette, transparency; no interlacing). An image is drawn scaled and blended by its alpha.

| Function | Arguments | Result |
|---|---|---|
| `ui_image_load` | path | image (0: failure) |
| `ui_image_decode` | data, bytes | image decoded from a buffer |
| `ui_image_size` | image | width (helper: `uikit_image_size (img, &h, &pixels)`, pixels `0xAARRGGBB`) |
| `ui_image_draw` | image, x, y, width, height (0, 0: its size) | draws into the current target (window, view, canvas) |
| `ui_image_free` | image | frees |
| `ui_imagebox` | x, y, width, height, image, `UIM_STRETCH` / `UIM_NOSCALE` / 0 | image box (0: fitted, keeping the proportions) |
| `ui_imagebox_set` | box, image | changes the image |

```cpp
// (excerpt) an image chosen by the user, in a view
static long g_img;
static void paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_SURFACE));
	long ih = 0, px = 0, iw = g_img ? uikit_image_size (g_img, &ih, &px) : 0;
	if (iw > 0 && iw <= w && ih <= h) ui_image_draw (g_img, (w - iw) / 2, (h - ih) / 2, 0, 0);
	else if (iw > 0) ui_image_draw (g_img, 0, 0, w, h);			// (stretched)
}
...
char p[512];
ui_file_filter ((long) "*.png;*.bmp");
if (ui_file_open ((long) "SD0:/home", (long) p, sizeof p) == 1)
{
	if (g_img) ui_image_free (g_img);
	g_img = ui_image_load ((long) p);
	ui_view_dirty (g_view);
}
```

## Several windows

A program may open several windows: each `ui_window_ex` returns one more, which becomes the **current window**. Drawing, created widgets, `ui_menu`, `ui_title`, `ui_min_size`, `ui_on_resize` apply to the current window; `ui_window_use` switches it. `ui_event` returns the events of all windows, `EV_WIN` says which; `uikit_w_event` goes to the right one by itself. `WEV_CLOSE` is left to the program, which closes the window with `ui_window_close`.

| Function | Arguments | Result |
|---|---|---|
| `ui_window_use` | window | the previous current window |
| `ui_window_close` | window (0: the current one) | its widgets destroyed, closed |
| `ui_window_count` | | number of open windows of the program |
| `ui_w_window` | widget | its window |
| `ui_raise` | id | brings a window to the front |
| `ui_winlist` | first, buffer (`WL_*`) | desktop windows (for the panel, the dock) |
| `ui_win_details` | first, buffer (7 entries `WE_*`) | all windows: position, size, role, state, workspace |
| `ui_screen_grab` | pixels, width, height (those of `ui_screen`) | 1: the screen as it is shown, pointer included, copied there (Screenshot) |
| `ui_desk` | workspace to show (-1: keep), count (0: keep) | current workspace + count `<< 8` |

One menu per app, as on a Mac: the bar shows that of the first window that has one.

### Example: one more window

A button opens (or brings back) an inspector window with a list; files dropped on the main window go into the list; closing the inspector does not close the app.

```cpp
// twowin.cpp -- two windows in one program (UIKit)
#include <string.h>
#include <uikit.h>

#define TR(s)	((const char *) ui_tr ((long) (s)))

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }

static long g_main, g_insp;			// the two windows (0: closed)
static long g_open, g_hint, g_list;

static void insp_open ()
{
	if (g_insp) { ui_raise (g_insp); return; }
	g_insp = ui_window_ex (-1, -1, 360, 300, (long) TR ("Inspecteur"), 0);	// it becomes the current one
	if (g_insp <= 0) { g_insp = 0; ui_window_use (g_main); return; }
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	ui_label (16, 12, 200, 24, (long) TR ("Fichiers déposés"), ULB_BOLD);
	g_list = ui_list (16, 44, ui.w - 32, ui.h - 60, 0);
	ui_w_draw (0);
	ui_update_all ();
	ui_window_use (g_main);			// drawing goes back to the main window
}

static void insp_close ()
{
	ui_window_close (g_insp);		// (its list is destroyed with it)
	g_insp = 0; g_list = 0;
	ui_window_use (g_main);
}

static void dropped (char *paths)
{
	insp_open ();
	for (char *p = strtok (paths, "\n"); p; p = strtok (0, "\n")) ui_list_add (g_list, (long) p, 0);
}

int main (void)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	g_main = ui_window_ex (-1, -1, 420, 220, (long) TR ("Deux fenêtres"), 0);
	if (g_main <= 0) return 1;
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	g_hint = ui_label (16, 48, ui.w - 32, 60, (long) TR ("Déposez des fichiers ici, depuis Fichiers."), ULB_WRAP | ULB_DIM);
	g_open = ui_pushbutton (16, 120, 0, 0, (long) TR ("Ouvrir l'inspecteur"), UB_DEFAULT);
	ui_w_draw (0);
	ui_update_all ();

	for (;;)
	{
		long t = ui_event (200);
		long win = ui_ev_field (&ui, EV_WIN);
		if (t == WEV_CLOSE)
		{
			if (g_insp && win == g_insp) { insp_close (); continue; }
			break;					// the main window: end of the program
		}
		if (t == WEV_DROP)
		{
			static char b[1024];
			long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
			if (n > 0 && kind == UDT_FILES) { b[n] = 0; dropped (b); }
		}
		else if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);	// (goes to the window of the event)
			if (w == g_open && code == UN_ACTIVATE) insp_open ();
		}
		else ui_w_idle ();
	}
	return 0;
}
```

## Translation

An app's texts are written in French in the source and go through `ui_tr`, which returns their translation in the system language (chosen in Settings, Region) or the text itself. Dictionaries are text files, one line per text:

```sh
# data/lang-en.txt -- key = the French text of the source
Formulaire = Form
Nouvel élément = New item
%d points, pinceau de %d px = %d points, %d px brush
```

| Function | Arguments | Result |
|---|---|---|
| `ui_tr` | text | its translation (the text belongs to the kit), or the text |
| `ui_tr_load` | path of a dictionary | 1 if read; its lines are added to the system's |
| `ui_lang` | | the language: `"fr"`, `"en"`... |

- The system dictionary is `SD0:/res/lang/<language>.txt`; building the card merges into it the `lang/<language>.txt` of each app.
- An app can also read its own: `ui_tr_load ("SD0:/apps/<app>.app/lang-en.txt")`, with the language from `ui_lang ()`.
- `ui_menu`, the dialogs and the UIKit widgets translate their own texts; widget labels and messages go through `ui_tr` in the program (the `TR` macro).
- A format string is translated before `snprintf`, as in the examples: the key is the format.

## Pitfalls

- **Colours**: never hard-coded colours for background and text; the tokens (`TJ_*`) follow the theme, light or dark.
- **The top right corner**: `FRAME_CAP_W` px left free at the top right, for the window buttons.
- **`ui_view_flush`** on every turn of the loop, otherwise a marked view is never redrawn.
- **`ui_w_idle`** when `ui_event` returns 0, otherwise tooltips do not appear.
- **After `ui_window_ex`** of a second window, the current window has changed: `ui_window_use` to go back.
- **A dialog of your own**: read its widgets before `ui_dialog_end`, which destroys them.
- **Texts returned** by the kit (`ui_w_get_text`, `ui_tr`, `uikit_list_get`) belong to it: copy them to keep them.
- **The `y` of `ui_text`** is the baseline; for 14 px text in a 30 px row, `y` ≈ top + 20.
- **Under QEMU**, there is no GPU: an ordinary UIKit app runs everywhere, surfaces too.

## See also

- [The guide's contents](index_en.md)
- [The skeleton of an app](skeleton_en.md): a complete project, ready to copy
- [An application](app_en.md): folder, `app.txt`, building
- [The libc and AppKit](libc_en.md)
- [GpuKit](gpukit_en.md), [MediaKit](mediakit_en.md), [EmuKit](emukit_en.md), [PrinterKit](printerkit_en.md)
