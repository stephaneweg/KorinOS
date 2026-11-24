/* SPDX-FileCopyrightText: 2026 Korin OS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * GHOST_SystemKorin : le systeme de GHOST sur UIKit (Korin OS). Modele : la plateforme korin de Godot
 * (tools/godot/platform/korin/display_server_korin.cpp) pour UIKit, GHOST_SystemSDL pour la forme,
 * GHOST_SystemWayland pour le grab sans deplacement du pointeur.
 */

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>

#include "GHOST_SystemKorin.hh"
#include "GHOST_WindowKorin.hh"

#include "GHOST_TimerManager.hh"
#include "GHOST_WindowManager.hh"

#include "GHOST_EventButton.hh"
#include "GHOST_EventCursor.hh"
#include "GHOST_EventKey.hh"
#include "GHOST_EventWheel.hh"

#ifdef WITH_VULKAN_BACKEND
#  include "GHOST_ContextVK.hh"
#endif

#include "ghost_korin_ui.h"

/** Attente la plus longue d'un kui_poll : (ui_event avec une attente negative n'est pas documente,
 * on ne compte pas sur une attente infinie) ; la boucle de #processEvents reprend ensuite. */
#define KORIN_POLL_WAIT_MAX_MS 100

/** Relecture des origines des fenetres (ui_window_pos) au plus tous les ... ms. */
#define KORIN_ORIGIN_REFRESH_MS 100

/* -------------------------------------------------------------------- */
/** \name Clavier : codes HID (page 7) -> GHOST_TKey
 * \{ */

/** La touche physique (positions du QWERTY US), comme convertSDLKey et hid_to_key de Godot. */
static GHOST_TKey convert_hid_key(const int code)
{
  if (code >= 0x04 && code <= 0x1d) {
    return GHOST_TKey(int(GHOST_kKeyA) + code - 0x04);
  }
  if (code >= 0x1e && code <= 0x26) {
    return GHOST_TKey(int(GHOST_kKey1) + code - 0x1e);
  }
  if (code >= 0x3a && code <= 0x45) {
    return GHOST_TKey(int(GHOST_kKeyF1) + code - 0x3a);
  }
  if (code >= 0x68 && code <= 0x73) {
    return GHOST_TKey(int(GHOST_kKeyF13) + code - 0x68);
  }
  switch (code) {
    case 0x27:
      return GHOST_kKey0;
    case 0x28:
      return GHOST_kKeyEnter;
    case 0x29:
      return GHOST_kKeyEsc;
    case 0x2a:
      return GHOST_kKeyBackSpace;
    case 0x2b:
      return GHOST_kKeyTab;
    case 0x2c:
      return GHOST_kKeySpace;
    case 0x2d:
      return GHOST_kKeyMinus;
    case 0x2e:
      return GHOST_kKeyEqual;
    case 0x2f:
      return GHOST_kKeyLeftBracket;
    case 0x30:
      return GHOST_kKeyRightBracket;
    case 0x31:
    case 0x32: /* (# et ~ hors US : a la place de la barre oblique inverse) */
      return GHOST_kKeyBackslash;
    case 0x33:
      return GHOST_kKeySemicolon;
    case 0x34:
      return GHOST_kKeyQuote;
    case 0x35:
      return GHOST_kKeyAccentGrave;
    case 0x36:
      return GHOST_kKeyComma;
    case 0x37:
      return GHOST_kKeyPeriod;
    case 0x38:
      return GHOST_kKeySlash;
    case 0x39:
      return GHOST_kKeyCapsLock;
    case 0x46:
      return GHOST_kKeyPrintScreen;
    case 0x47:
      return GHOST_kKeyScrollLock;
    case 0x48:
      return GHOST_kKeyPause;
    case 0x49:
      return GHOST_kKeyInsert;
    case 0x4a:
      return GHOST_kKeyHome;
    case 0x4b:
      return GHOST_kKeyUpPage;
    case 0x4c:
      return GHOST_kKeyDelete;
    case 0x4d:
      return GHOST_kKeyEnd;
    case 0x4e:
      return GHOST_kKeyDownPage;
    case 0x4f:
      return GHOST_kKeyRightArrow;
    case 0x50:
      return GHOST_kKeyLeftArrow;
    case 0x51:
      return GHOST_kKeyDownArrow;
    case 0x52:
      return GHOST_kKeyUpArrow;
    case 0x53:
      return GHOST_kKeyNumLock;
    case 0x54:
      return GHOST_kKeyNumpadSlash;
    case 0x55:
      return GHOST_kKeyNumpadAsterisk;
    case 0x56:
      return GHOST_kKeyNumpadMinus;
    case 0x57:
      return GHOST_kKeyNumpadPlus;
    case 0x58:
      return GHOST_kKeyNumpadEnter;
    case 0x59:
      return GHOST_kKeyNumpad1;
    case 0x5a:
      return GHOST_kKeyNumpad2;
    case 0x5b:
      return GHOST_kKeyNumpad3;
    case 0x5c:
      return GHOST_kKeyNumpad4;
    case 0x5d:
      return GHOST_kKeyNumpad5;
    case 0x5e:
      return GHOST_kKeyNumpad6;
    case 0x5f:
      return GHOST_kKeyNumpad7;
    case 0x60:
      return GHOST_kKeyNumpad8;
    case 0x61:
      return GHOST_kKeyNumpad9;
    case 0x62:
      return GHOST_kKeyNumpad0;
    case 0x63:
      return GHOST_kKeyNumpadPeriod;
    case 0x64: /* la touche < > de l'ISO (AZERTY, QWERTZ) */
      return GHOST_kKeyGrLess;
    case 0x65:
      return GHOST_kKeyApp;
    case 0x67: /* = du pave */
      return GHOST_kKeyEqual;
    case 0xe0:
      return GHOST_kKeyLeftControl;
    case 0xe1:
      return GHOST_kKeyLeftShift;
    case 0xe2:
      return GHOST_kKeyLeftAlt;
    case 0xe3:
      return GHOST_kKeyLeftOS;
    case 0xe4:
      return GHOST_kKeyRightControl;
    case 0xe5:
      return GHOST_kKeyRightShift;
    case 0xe6:
      return GHOST_kKeyRightAlt;
    case 0xe7:
      return GHOST_kKeyRightOS;
    default:
      return GHOST_kKeyUnknown;
  }
}

/** Le bit de l'octet des modificateurs du HID pour une touche 0xe0..0xe7 (0 : pas un modificateur). */
static int hid_modifier_bit(const int code)
{
  return (code >= 0xe0 && code <= 0xe7) ? (1 << (code - 0xe0)) : 0;
}

/**
 * La touche de GHOST d'un appui. HIDManager donne le caractere de la disposition (AZERTY…) dans
 * EV_CHAR : les lettres le suivent (Ctrl+Z est sur la touche marquee Z), comme X11 et Wayland qui
 * prennent le keysym de la disposition. Les chiffres et le reste suivent la position physique (la
 * rangee des chiffres de l'AZERTY donne & e " ' sans Maj : Blender y attend 1 2 3 4, comme sous X11).
 */
static GHOST_TKey convert_key(const int code, const int ch, const int mods)
{
  const GHOST_TKey physical = convert_hid_key(code);
  if (physical < GHOST_kKeyA || physical > GHOST_kKeyZ) {
    /* Ni lettre, ni signe (0x2d..0x38), ni la touche < > de l'ISO : la position physique. Les
     * signes peuvent porter une lettre ailleurs qu'en QWERTY (le M de l'AZERTY est en 0x33). */
    if (!(code >= 0x2d && code <= 0x38) && code != 0x64) {
      return physical;
    }
  }
  if (ch >= 'a' && ch <= 'z') {
    return GHOST_TKey(int(GHOST_kKeyA) + ch - 'a');
  }
  if (ch >= 'A' && ch <= 'Z') {
    return GHOST_TKey(int(GHOST_kKeyA) + ch - 'A');
  }
  /* Ctrl tenu : le caractere peut etre un code de controle (Ctrl+A = 1 … Ctrl+Z = 26) ; a verifier
   * sur Korin (HIDManager), le cas est couvert dans les deux sens. */
  if ((mods & KUI_MOD_CTRL) && ch >= 1 && ch <= 26) {
    return GHOST_TKey(int(GHOST_kKeyA) + ch - 1);
  }
  return physical;
}

/** Un point de code en UTF-8 dans un tampon de GHOST_TEventKeyData (6 octets, termine par NUL si la place). */
static void utf8_from_codepoint(uint32_t c, char r_buf[6])
{
  memset(r_buf, 0, 6);
  if (c < 0x80) {
    r_buf[0] = char(c);
  }
  else if (c < 0x800) {
    r_buf[0] = char(0xc0 | (c >> 6));
    r_buf[1] = char(0x80 | (c & 0x3f));
  }
  else if (c < 0x10000) {
    if (c >= 0xd800 && c <= 0xdfff) {
      return; /* (moitie de paire UTF-16 : rien) */
    }
    r_buf[0] = char(0xe0 | (c >> 12));
    r_buf[1] = char(0x80 | ((c >> 6) & 0x3f));
    r_buf[2] = char(0x80 | (c & 0x3f));
  }
  else if (c < 0x110000) {
    r_buf[0] = char(0xf0 | (c >> 18));
    r_buf[1] = char(0x80 | ((c >> 12) & 0x3f));
    r_buf[2] = char(0x80 | ((c >> 6) & 0x3f));
    r_buf[3] = char(0x80 | (c & 0x3f));
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Le systeme
 * \{ */

GHOST_SystemKorin::GHOST_SystemKorin() : GHOST_System()
{
  start_ms_ = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count());
  std::fill(std::begin(key_pressed_as_), std::end(key_pressed_as_), GHOST_kKeyUnknown);

  if (!kui_open()) {
    /* (GHOST_ISystem::createSystem rapporte le message et Blender propose --background) */
    throw std::runtime_error("UIKit indisponible (le bureau Elegant ne tourne pas ?)");
  }
  kui_screen(&screen_w_, &screen_h_, &reserved_bottom_, &reserved_top_);
}

GHOST_SystemKorin::~GHOST_SystemKorin()
{
  /* (les fenetres sont fermees par GHOST_System::exit ; UIKit n'a pas de fermeture a appeler) */
}

GHOST_TSuccess GHOST_SystemKorin::init()
{
  return GHOST_System::init();
}

uint64_t GHOST_SystemKorin::getMilliSeconds() const
{
  const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
  return now - start_ms_;
}

uint8_t GHOST_SystemKorin::getNumDisplays() const
{
  return 1;
}

void GHOST_SystemKorin::getMainDisplayDimensions(uint32_t &width, uint32_t &height) const
{
  /* La zone utile : la premiere fenetre de Blender la remplit sans passer sous le panneau. */
  width = uint32_t(std::max(screen_w_, 1));
  height = uint32_t(std::max(screen_h_ - reserved_top_ - reserved_bottom_, 1));
}

void GHOST_SystemKorin::getAllDisplayDimensions(uint32_t &width, uint32_t &height) const
{
  width = uint32_t(std::max(screen_w_, 1));
  height = uint32_t(std::max(screen_h_, 1));
}

void GHOST_SystemKorin::getScreenSize(int &width, int &height) const
{
  width = screen_w_;
  height = screen_h_;
}

void GHOST_SystemKorin::getUsableArea(int &top, int &height) const
{
  top = reserved_top_;
  height = std::max(screen_h_ - reserved_top_ - reserved_bottom_, 1);
}

GHOST_IWindow *GHOST_SystemKorin::createWindow(const char *title,
                                               int32_t left,
                                               int32_t top,
                                               uint32_t width,
                                               uint32_t height,
                                               GHOST_TWindowState state,
                                               GHOST_GPUSettings gpu_settings,
                                               const bool exclusive,
                                               const bool is_dialog,
                                               const GHOST_IWindow *parent_window)
{
  const GHOST_ContextParams context_params = GHOST_CONTEXT_PARAMS_FROM_GPU_SETTINGS(gpu_settings);

  GHOST_WindowKorin *window = new GHOST_WindowKorin(this,
                                                    title,
                                                    left,
                                                    top,
                                                    width,
                                                    height,
                                                    state,
                                                    gpu_settings.context_type,
                                                    context_params,
                                                    gpu_settings.preferred_device,
                                                    exclusive,
                                                    is_dialog,
                                                    parent_window);
  if (!window->getValid()) {
    delete window;
    return nullptr;
  }
  window_manager_->addWindow(window);
  window_manager_->setActiveWindow(window);
  pushEvent(std::make_unique<GHOST_Event>(getMilliSeconds(), GHOST_kEventWindowSize, window));
  return window;
}

GHOST_IContext *GHOST_SystemKorin::createOffscreenContext(GHOST_GPUSettings gpu_settings)
{
  const GHOST_ContextParams context_params_offscreen =
      GHOST_CONTEXT_PARAMS_FROM_GPU_SETTINGS_OFFSCREEN(gpu_settings);
  (void)context_params_offscreen;

  switch (gpu_settings.context_type) {
#ifdef WITH_VULKAN_BACKEND
    case GHOST_kDrawingContextTypeVulkan: {
      /* Sans surface, comme GHOST_SystemHeadless : le meme VkInstance et VkDevice que les fenetres. */
      GHOST_Context *context = new GHOST_ContextVK(context_params_offscreen,
                                                   GHOST_kVulkanPlatformHeadless,
                                                   0,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   1,
                                                   2,
                                                   gpu_settings.preferred_device);
      if (context->initializeDrawingContext()) {
        return context;
      }
      delete context;
      return nullptr;
    }
#endif
    default:
      /* Pas d'OpenGL sur Korin PC (Mesa iris en GL ES seulement) : Vulkan seul. */
      return nullptr;
  }
}

GHOST_TSuccess GHOST_SystemKorin::disposeContext(GHOST_IContext *context)
{
  delete context;
  return GHOST_kSuccess;
}

GHOST_TCapabilityFlag GHOST_SystemKorin::getCapabilities() const
{
  return GHOST_TCapabilityFlag(
      GHOST_CAPABILITY_FLAG_ALL &
      /* NOTE: order the following flags as they they're declared in the source. */
      ~(
          /* Korin (Elegant) ne sait pas deplacer le pointeur : grab par pointeur pris et
           * deplacements relatifs, curseur logiciel de Blender pour GHOST_kGrabWrap. */
          GHOST_kCapabilityCursorWarp |
          /* Pas de selection primaire (X11) sur Korin. */
          GHOST_kCapabilityClipboardPrimary |
          /* Lecture du tampon montre : pas sure avec la WSI de Korin, Blender redessine hors ecran. */
          GHOST_kCapabilityGPUReadFrontBuffer |
          /* TODO : presse-papiers image (ui_clip_set_image, ui_clip_get_image d'UIKit). */
          GHOST_kCapabilityClipboardImage |
          /* TODO : couleur de l'ecran sous le pointeur (ui_screen_grab). */
          GHOST_kCapabilityDesktopSample |
          /* Pas de methode de saisie (IME). */
          GHOST_kCapabilityInputIME |
          /* Pas de pave tactile a gestes. */
          GHOST_kCapabilityTrackpadPhysicalDirection |
          /* Le cadre est celui du theme d'Elegant. */
          GHOST_kCapabilityWindowDecorationStyles |
          /* Pas de touche Hyper. */
          GHOST_kCapabilityKeyboardHyperKey |
          /* TODO : curseurs personnalises (bitmaps, RGBA, generes). */
          GHOST_kCapabilityCursorRGBA | GHOST_kCapabilityCursorGenerator |
          /* Un seul ecran. */
          GHOST_kCapabilityMultiMonitorPlacement |
          /* Pas de chemin de fichier attache a la fenetre. */
          GHOST_kCapabilityWindowPath));
  /* (GHOST_kCapabilityWindowDecorationServerSide reste : Elegant dessine le cadre ;
   * GHOST_kCapabilityWindowPosition reste : ui_window_pos, ui_window_place) */
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Presse-papiers
 * \{ */

char *GHOST_SystemKorin::getClipboard(bool selection) const
{
  if (selection) {
    return nullptr; /* (pas de selection primaire) */
  }
  /* kui_clip_get tronque a la taille du tampon : agrandir tant que le texte le remplit. */
  for (int cap = 64 * 1024; cap <= 64 * 1024 * 1024; cap *= 4) {
    char *buf = static_cast<char *>(malloc(size_t(cap)));
    if (buf == nullptr) {
      return nullptr;
    }
    const int n = kui_clip_get(buf, cap);
    if (n <= 0) {
      free(buf);
      return nullptr;
    }
    if (n < cap - 1) {
      return buf; /* (libere par l'appelant avec free, comme les autres systemes) */
    }
    free(buf);
  }
  return nullptr;
}

void GHOST_SystemKorin::putClipboard(const char *buffer, bool selection) const
{
  if (selection || buffer == nullptr) {
    return;
  }
  kui_clip_set(buffer);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clavier et souris : etat
 * \{ */

GHOST_TSuccess GHOST_SystemKorin::getModifierKeys(GHOST_ModifierKeys &keys) const
{
  keys.set(GHOST_kModifierKeyLeftShift, (mods_ & KUI_MOD_LSHIFT) != 0);
  keys.set(GHOST_kModifierKeyRightShift, (mods_ & KUI_MOD_RSHIFT) != 0);
  keys.set(GHOST_kModifierKeyLeftControl, (mods_ & KUI_MOD_LCTRL) != 0);
  keys.set(GHOST_kModifierKeyRightControl, (mods_ & KUI_MOD_RCTRL) != 0);
  keys.set(GHOST_kModifierKeyLeftAlt, (mods_ & KUI_MOD_LALT) != 0);
  keys.set(GHOST_kModifierKeyRightAlt, (mods_ & KUI_MOD_RALT) != 0);
  keys.set(GHOST_kModifierKeyLeftOS, (mods_ & KUI_MOD_LMETA) != 0);
  keys.set(GHOST_kModifierKeyRightOS, (mods_ & KUI_MOD_RMETA) != 0);
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_SystemKorin::getButtons(GHOST_Buttons &buttons) const
{
  buttons.set(GHOST_kButtonMaskLeft, (mouse_buttons_ & KUI_BUTTON_LEFT) != 0);
  buttons.set(GHOST_kButtonMaskMiddle, (mouse_buttons_ & KUI_BUTTON_MIDDLE) != 0);
  buttons.set(GHOST_kButtonMaskRight, (mouse_buttons_ & KUI_BUTTON_RIGHT) != 0);
  return GHOST_kSuccess;
}

void GHOST_SystemKorin::getPointerClient(const GHOST_WindowKorin *window,
                                         int32_t &x,
                                         int32_t &y) const
{
  if (lock_window_ != nullptr && lock_window_ == window) {
    x = lock_xy_[0];
    y = lock_xy_[1];
    return;
  }
  if (pointer_window_ != nullptr && pointer_window_ == window) {
    x = pointer_xy_[0];
    y = pointer_xy_[1];
    return;
  }
  /* Le pointeur est dans une autre fenetre : passer par les coordonnees de l'ecran. */
  int32_t sx = 0, sy = 0;
  if (pointer_window_ != nullptr) {
    pointer_window_->clientToScreen(pointer_xy_[0], pointer_xy_[1], sx, sy);
  }
  window->screenToClient(sx, sy, x, y);
}

GHOST_TSuccess GHOST_SystemKorin::getCursorPosition(int32_t &x, int32_t &y) const
{
  GHOST_WindowKorin *window = lock_window_ ? lock_window_ : pointer_window_;
  if (window == nullptr) {
    return GHOST_kFailure;
  }
  int32_t cx, cy;
  getPointerClient(window, cx, cy);
  window->clientToScreen(cx, cy, x, y);
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_SystemKorin::setCursorPosition(int32_t x, int32_t y)
{
  /* Korin ne deplace pas le pointeur. Pendant un grab cache, seule la position virtuelle
   * (celle du curseur logiciel) bouge, comme le fait Wayland avec son pointeur verrouille. */
  if (lock_window_ == nullptr) {
    return GHOST_kFailure;
  }
  lock_window_->screenToClient(x, y, lock_xy_[0], lock_xy_[1]);
  return GHOST_kSuccess;
}

void GHOST_SystemKorin::grabLockBegin(GHOST_WindowKorin *window)
{
  int32_t x, y;
  getPointerClient(window, x, y);
  lock_window_ = window;
  lock_xy_[0] = x;
  lock_xy_[1] = y;
}

void GHOST_SystemKorin::grabLockEnd(GHOST_WindowKorin *window)
{
  if (lock_window_ != window) {
    return;
  }
  lock_window_ = nullptr;
  /* Le vrai pointeur n'a pas bouge pendant la prise (Elegant le fige) : Blender doit le
   * retrouver la ou il est vraiment. */
  if (pointer_window_ == window) {
    pushCursorMove(window, getMilliSeconds());
  }
}

void GHOST_SystemKorin::addDirtyWindow(GHOST_WindowKorin *window)
{
  GHOST_ASSERT((window != nullptr), "addDirtyWindow() nullptr ptr trapped (window)");
  dirty_windows_.push_back(window);
}

void GHOST_SystemKorin::removeWindow(GHOST_WindowKorin *window)
{
  dirty_windows_.erase(std::remove(dirty_windows_.begin(), dirty_windows_.end(), window),
                       dirty_windows_.end());
  if (pointer_window_ == window) {
    pointer_window_ = nullptr;
  }
  if (lock_window_ == window) {
    lock_window_ = nullptr;
  }
}

GHOST_WindowKorin *GHOST_SystemKorin::findGhostWindow(const long kui_win) const
{
  if (kui_win <= 0) {
    return nullptr;
  }
  for (GHOST_IWindow *iwin : window_manager_->getWindows()) {
    GHOST_WindowKorin *window = static_cast<GHOST_WindowKorin *>(iwin);
    if (window->getKuiWindow() == kui_win) {
      return window;
    }
  }
  return nullptr;
}

void GHOST_SystemKorin::refreshWindowOrigins(const bool force)
{
  const uint64_t now = getMilliSeconds();
  if (!force && now - origins_ms_ < KORIN_ORIGIN_REFRESH_MS) {
    return;
  }
  origins_ms_ = now;
  for (GHOST_IWindow *iwin : window_manager_->getWindows()) {
    static_cast<GHOST_WindowKorin *>(iwin)->refreshOrigin();
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Evenements
 * \{ */

void GHOST_SystemKorin::pushCursorMove(GHOST_WindowKorin *window, const uint64_t event_ms)
{
  int32_t cx, cy, sx, sy;
  getPointerClient(window, cx, cy);
  window->clientToScreen(cx, cy, sx, sy);
  pushEvent(std::make_unique<GHOST_EventCursor>(
      event_ms, GHOST_kEventCursorMove, window, sx, sy, GHOST_TABLET_DATA_NONE));
}

void GHOST_SystemKorin::syncModifiers(GHOST_WindowKorin *window,
                                      const int mods,
                                      const uint64_t event_ms)
{
  static const struct {
    int bit;
    GHOST_TKey key;
  } table[] = {
      {KUI_MOD_LCTRL, GHOST_kKeyLeftControl},
      {KUI_MOD_LSHIFT, GHOST_kKeyLeftShift},
      {KUI_MOD_LALT, GHOST_kKeyLeftAlt},
      {KUI_MOD_LMETA, GHOST_kKeyLeftOS},
      {KUI_MOD_RCTRL, GHOST_kKeyRightControl},
      {KUI_MOD_RSHIFT, GHOST_kKeyRightShift},
      {KUI_MOD_RALT, GHOST_kKeyRightAlt},
      {KUI_MOD_RMETA, GHOST_kKeyRightOS},
  };
  const int changed = (mods ^ mods_) & 0xff;
  if (changed == 0) {
    return;
  }
  for (const auto &item : table) {
    if (changed & item.bit) {
      const bool down = (mods & item.bit) != 0;
      if (window != nullptr) {
        pushEvent(std::make_unique<GHOST_EventKey>(
            event_ms, down ? GHOST_kEventKeyDown : GHOST_kEventKeyUp, window, item.key, false));
      }
    }
  }
  mods_ = mods & 0xff;
}

void GHOST_SystemKorin::processKey(GHOST_WindowKorin *window,
                                   const kui_event &ev,
                                   const uint64_t event_ms)
{
  const int code = ev.code & 0xff;
  const bool pressed = ev.kind != 0;

  /* Les modificateurs : une seule source d'appuis et de relaches (#syncModifiers), qu'ils arrivent
   * comme touches (0xe0..0xe7) ou seulement dans EV_MODS. */
  const int mod_bit = hid_modifier_bit(code);
  if (mod_bit) {
    syncModifiers(window, pressed ? (mods_ | mod_bit) : (mods_ & ~mod_bit), event_ms);
    key_down_[code] = pressed;
    return;
  }
  syncModifiers(window, ev.mods, event_ms);

  GHOST_TKey key;
  if (pressed) {
    key = convert_key(code, ev.ch, ev.mods);
    key_pressed_as_[code] = key;
  }
  else {
    key = key_down_[code] ? key_pressed_as_[code] : convert_hid_key(code);
  }
  const bool is_repeat = pressed && key_down_[code];
  key_down_[code] = pressed;

  /* Le texte saisi : le caractere de la disposition (accents de l'AZERTY, AltGr compris) ; rien avec
   * Ctrl, Alt de gauche ou la touche du systeme (raccourcis). */
  char utf8_buf[sizeof(GHOST_TEventKeyData::utf8_buf)] = {'\0'};
  if (pressed && ev.ch >= 32 && ev.ch != 127 &&
      !(ev.mods & (KUI_MOD_CTRL | KUI_MOD_LALT | KUI_MOD_META)))
  {
    utf8_from_codepoint(uint32_t(ev.ch), utf8_buf);
  }

  pushEvent(std::make_unique<GHOST_EventKey>(event_ms,
                                             pressed ? GHOST_kEventKeyDown : GHOST_kEventKeyUp,
                                             window,
                                             key,
                                             is_repeat,
                                             utf8_buf));
}

void GHOST_SystemKorin::processMouse(GHOST_WindowKorin *window,
                                     const kui_event &ev,
                                     const uint64_t event_ms)
{
  if (ev.kind == KUI_M_REL) {
    /* Pointeur pris : deplacement relatif, ajoute a la position virtuelle. */
    if (lock_window_ != nullptr) {
      window = lock_window_;
      lock_xy_[0] += ev.x;
      lock_xy_[1] += ev.y;
      if (ev.x != 0 || ev.y != 0) {
        pushCursorMove(window, event_ms);
      }
    }
  }
  else if (window != nullptr) {
    /* (x, y dans la fenetre d'UIKit ; le contenu de Blender commence sous la bande de titre) */
    const int cy = ev.y - KUI_TOP_BAND;
    const bool moved = (pointer_window_ != window) || (pointer_xy_[0] != ev.x) ||
                       (pointer_xy_[1] != cy);
    pointer_window_ = window;
    pointer_xy_[0] = ev.x;
    pointer_xy_[1] = cy;
    /* (pendant une prise, la position absolue est figee : seuls les relatifs comptent) */
    if (moved && lock_window_ == nullptr) {
      pushCursorMove(window, event_ms);
    }
  }
  if (window == nullptr) {
    return;
  }

  /* Boutons : les changements du masque, apres le deplacement (comme Godot et X11). */
  static const struct {
    int bit;
    GHOST_TButton button;
  } buttons[] = {
      {KUI_BUTTON_LEFT, GHOST_kButtonMaskLeft},
      {KUI_BUTTON_RIGHT, GHOST_kButtonMaskRight},
      {KUI_BUTTON_MIDDLE, GHOST_kButtonMaskMiddle},
  };
  const int b = ev.code & 7;
  const int changed = b ^ mouse_buttons_;
  mouse_buttons_ = b;
  for (const auto &item : buttons) {
    if (changed & item.bit) {
      const bool down = (b & item.bit) != 0;
      pushEvent(std::make_unique<GHOST_EventButton>(event_ms,
                                                    down ? GHOST_kEventButtonDown :
                                                           GHOST_kEventButtonUp,
                                                    window,
                                                    item.button,
                                                    GHOST_TABLET_DATA_NONE));
    }
  }
  /* TODO : boutons 4 et 5 (precedent, suivant) si Elegant les donne un jour dans EV_CODE. */
}

void GHOST_SystemKorin::processEvent(const kui_event &ev)
{
  const uint64_t event_ms = getMilliSeconds();
  GHOST_WindowKorin *window = findGhostWindow(ev.win);

  switch (ev.type) {
    case KUI_EV_KEY: {
      if (window == nullptr) {
        window = static_cast<GHOST_WindowKorin *>(window_manager_->getActiveWindow());
      }
      if (window != nullptr) {
        processKey(window, ev, event_ms);
      }
      break;
    }
    case KUI_EV_MOUSE: {
      processMouse(window, ev, event_ms);
      break;
    }
    case KUI_EV_WHEEL: {
      /* y = crans, positif vers le haut (comme Godot : WHEEL_UP) ; pas de roulette horizontale
       * connue (TODO : si Elegant la donne dans x). */
      if (window != nullptr && ev.y != 0) {
        pushEvent(std::make_unique<GHOST_EventWheel>(
            event_ms, window, GHOST_kEventWheelAxisVertical, int32_t(ev.y)));
      }
      break;
    }
    case KUI_EV_CLOSE: {
      if (window != nullptr) {
        pushEvent(std::make_unique<GHOST_Event>(event_ms, GHOST_kEventWindowClose, window));
      }
      break;
    }
    case KUI_EV_FOCUS: {
      if (window == nullptr) {
        break;
      }
      const bool focused = ev.kind != 0;
      if (focused) {
        refreshWindowOrigins(true); /* (la fenetre a pu etre deplacee pendant qu'elle n'avait pas le focus) */
      }
      else {
        /* Le clavier part : relacher ce qui est tenu (sinon une touche resterait enfoncee). */
        syncModifiers(window, 0, event_ms);
        std::fill(std::begin(key_down_), std::end(key_down_), false);
      }
      window->handleFocus(focused);
      if (focused) {
        window_manager_->setActiveWindow(window);
      }
      else {
        window_manager_->setWindowInactive(window);
      }
      pushEvent(std::make_unique<GHOST_Event>(
          event_ms, focused ? GHOST_kEventWindowActivate : GHOST_kEventWindowDeactivate, window));
      break;
    }
    case KUI_EV_RESIZE: {
      if (window != nullptr && ev.x > 0 && ev.y > 0) {
        window->handleResize(ev.x, ev.y);
        window->refreshOrigin();
        pushEvent(std::make_unique<GHOST_Event>(event_ms, GHOST_kEventWindowSize, window));
      }
      break;
    }
    case KUI_EV_SCREEN: {
      if (ev.x > 0 && ev.y > 0) {
        screen_w_ = ev.x;
        screen_h_ = ev.y;
        for (GHOST_IWindow *iwin : window_manager_->getWindows()) {
          static_cast<GHOST_WindowKorin *>(iwin)->handleScreenSize(ev.x, ev.y);
        }
      }
      break;
    }
    case KUI_EV_DROP: {
      /* TODO : glisser-deposer de fichiers (ui_drop_get, GHOST_EventDragnDrop,
       * GHOST_kEventDraggingDropDone avec GHOST_kDragnDropTypeFilenames). */
      break;
    }
    case KUI_EV_THEME: {
      /* (Blender a ses propres themes ; seule la bande de titre suit celui du bureau) */
      for (GHOST_IWindow *iwin : window_manager_->getWindows()) {
        static_cast<GHOST_WindowKorin *>(iwin)->drawTitleBand();
      }
      break;
    }
    case KUI_EV_FRAME:
    case KUI_EV_PAD:   /* (pas de manette dans Blender) */
    default:
      break;
  }
}

bool GHOST_SystemKorin::generateWindowExposeEvents()
{
  bool any_processed = false;
  const uint64_t event_ms = getMilliSeconds();
  for (GHOST_WindowKorin *window : dirty_windows_) {
    window->validate();
    pushEvent(std::make_unique<GHOST_Event>(event_ms, GHOST_kEventWindowUpdate, window));
    any_processed = true;
  }
  dirty_windows_.clear();
  return any_processed;
}

bool GHOST_SystemKorin::processEvents(bool waitForEvent)
{
  /* Les evenements d'UIKit, traduits et poses dans la file de GHOST (#pushEvent) ; Blender les
   * distribue ensuite (dispatchEvents). Les origines des fenetres sont relues avant : tous les
   * evenements d'un passage utilisent la meme (voir #refreshWindowOrigins). */
  bool any_processed = false;

  do {
    GHOST_TimerManager *timer_mgr = getTimerManager();
    refreshWindowOrigins(false);

    int wait_ms = 0;
    if (waitForEvent && dirty_windows_.empty()) {
      const uint64_t next = timer_mgr->nextFireTime();
      if (next == uint64_t(GHOST_kFireTimeNever)) {
        wait_ms = KORIN_POLL_WAIT_MAX_MS;
      }
      else {
        const int64_t delta = int64_t(next) - int64_t(getMilliSeconds());
        wait_ms = int(std::clamp<int64_t>(delta, 0, KORIN_POLL_WAIT_MAX_MS));
      }
    }

    kui_event ev;
    if (kui_poll(&ev, wait_ms) > 0) {
      processEvent(ev);
      any_processed = true;
      while (kui_poll(&ev, 0) > 0) {
        processEvent(ev);
      }
    }

    if (timer_mgr->fireTimers(getMilliSeconds())) {
      any_processed = true;
    }

    if (generateWindowExposeEvents()) {
      any_processed = true;
    }
  } while (waitForEvent && !any_processed);

  return any_processed;
}

/** \} */
