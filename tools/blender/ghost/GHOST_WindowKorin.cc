/* SPDX-FileCopyrightText: 2026 Korin OS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * GHOST_WindowKorin : une fenetre d'UIKit (cadre du theme d'Elegant), contenu dessine par Vulkan.
 */

#include "GHOST_WindowKorin.hh"
#include "GHOST_SystemKorin.hh"

#include "GHOST_Event.hh"

#include "ghost_korin_ui.h"

#include <algorithm>
#include <cstdint>
#include <memory>

/* -------------------------------------------------------------------- */
/** \name Curseurs : GHOST_TStandardCursor -> formes d'UIKit (UC_*)
 * \{ */

/** La forme d'UIKit, -1 si UIKit n'en a pas d'approchante (Blender passe alors a ses bitmaps,
 * #setWindowCustomCursorShape, qui retombe pour l'instant sur la fleche). */
static int kui_cursor_from_ghost(const GHOST_TStandardCursor shape)
{
  switch (shape) {
    case GHOST_kStandardCursorDefault:
      return KUI_CUR_ARROW;
    case GHOST_kStandardCursorWait:
      return KUI_CUR_WAIT;
    case GHOST_kStandardCursorText:
      return KUI_CUR_TEXT;
    case GHOST_kStandardCursorCrosshair:
    case GHOST_kStandardCursorCrosshairA:
    case GHOST_kStandardCursorCrosshairB:
    case GHOST_kStandardCursorCrosshairC:
      return KUI_CUR_CROSS;
    case GHOST_kStandardCursorDestroy:
    case GHOST_kStandardCursorStop:
      return KUI_CUR_NO;
    case GHOST_kStandardCursorHandPoint:
    case GHOST_kStandardCursorHandOpen:
      return KUI_CUR_HAND;
    case GHOST_kStandardCursorHandClosed:
      return KUI_CUR_DRAG;
    case GHOST_kStandardCursorMove:
    case GHOST_kStandardCursorNSEWScroll:
      return KUI_CUR_MOVE;
    /* (VerticalSplit : la separation est verticale, on tire a gauche ou a droite, comme X11) */
    case GHOST_kStandardCursorVerticalSplit:
    case GHOST_kStandardCursorEWScroll:
    case GHOST_kStandardCursorLeftRight:
    case GHOST_kStandardCursorLeftSide:
    case GHOST_kStandardCursorRightSide:
      return KUI_CUR_HRESIZE;
    case GHOST_kStandardCursorHorizontalSplit:
    case GHOST_kStandardCursorNSScroll:
    case GHOST_kStandardCursorUpDown:
    case GHOST_kStandardCursorTopSide:
    case GHOST_kStandardCursorBottomSide:
      return KUI_CUR_VRESIZE;
    case GHOST_kStandardCursorTopLeftCorner:
    case GHOST_kStandardCursorBottomRightCorner:
      return KUI_CUR_NWSE;
    case GHOST_kStandardCursorTopRightCorner:
    case GHOST_kStandardCursorBottomLeftCorner:
      return KUI_CUR_NESW;
    default:
      /* RightArrow, LeftArrow, Info, Help, Pencil, Up/DownArrow, Eraser, Knife, Eyedropper,
       * ZoomIn/Out, Copy, Left/Right/BothHandles, Blade, Slip, Custom : pas de forme d'UIKit. */
      return -1;
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name La fenetre
 * \{ */

GHOST_WindowKorin::GHOST_WindowKorin(GHOST_SystemKorin *system,
                                     const char *title,
                                     int32_t left,
                                     int32_t top,
                                     uint32_t width,
                                     uint32_t height,
                                     GHOST_TWindowState state,
                                     GHOST_TDrawingContextType type,
                                     const GHOST_ContextParams &context_params,
                                     const GHOST_GPUDevice &preferred_device,
                                     const bool exclusive,
                                     const bool is_dialog,
                                     const GHOST_IWindow * /*parent_window*/)
    : GHOST_Window(width, height, state, context_params, exclusive),
      system_(system),
      is_dialog_(is_dialog),
      title_(title ? title : ""),
      preferred_device_(preferred_device)
{
  /* Taille et place : dans la zone utile de l'ecran (Elegant place lui-meme une fenetre hors champ). */
  int screen_w, screen_h, usable_top, usable_h;
  system_->getScreenSize(screen_w, screen_h);
  system_->getUsableArea(usable_top, usable_h);
  /* (la hauteur demandee est celle du contenu : la fenetre a en plus la bande de titre) */
  const int w = std::clamp(int(width), 1, std::max(screen_w, 1));
  const int h = std::clamp(int(height) + KUI_TOP_BAND, KUI_TOP_BAND + 1, std::max(usable_h, 1));
  int x = int(left), y = int(top);
  if (x < 0 || x + w > screen_w) {
    x = std::max((screen_w - w) / 2, 0);
  }
  if (y < usable_top || y + h > usable_top + usable_h) {
    y = usable_top + std::max((usable_h - h) / 2, 0);
  }

  kui_win_ = kui_window(x, y, w, h, title_.c_str(), KUI_WIN_RESIZABLE);
  if (kui_win_ <= 0) {
    kui_win_ = 0;
    return;
  }
  int rw = 0, rh = 0;
  kui_window_size(kui_win_, &rw, &rh);
  setSize(rw > 0 ? rw : w, rh > 0 ? rh : h);
  refreshOrigin();
  drawTitleBand();

  /* Le contexte de dessin (Vulkan) : apres la fenetre, sa surface en depend. */
  if (setDrawingContextType(type) == GHOST_kSuccess) {
    valid_setup_ = true;
  }

  if (state != GHOST_kWindowStateNormal) {
    setState(state);
  }
  if (exclusive) {
    kui_window_raise(kui_win_);
  }
  applyCursor();
}

GHOST_WindowKorin::~GHOST_WindowKorin()
{
  system_->removeWindow(this);
  if (kui_win_ > 0 && pointer_locked_) {
    kui_pointer_lock(kui_win_, 0);
    pointer_locked_ = false;
  }
  releaseNativeHandles();
  /* La chaine d'echange et la surface de Vulkan d'abord, la fenetre d'UIKit ensuite
   * (GHOST_Window::~GHOST_Window ne detruirait le contexte qu'apres la fermeture). */
  setDrawingContextType(GHOST_kDrawingContextTypeNone);
  if (kui_win_ > 0) {
    kui_window_close(kui_win_);
    kui_win_ = 0;
  }
}

bool GHOST_WindowKorin::getValid() const
{
  return GHOST_Window::getValid() && valid_setup_ && kui_win_ > 0;
}

GHOST_Context *GHOST_WindowKorin::newDrawingContext(GHOST_TDrawingContextType type)
{
  switch (type) {
#ifdef WITH_VULKAN_BACKEND
    case GHOST_kDrawingContextTypeVulkan: {
      /* Branche WITH_GHOST_KORIN de GHOST_ContextVK (GHOST_ContextVK_korin.patch.md) : la poignee de
       * la fenetre passe par le parametre « window » de X11 (void * sans X11), la taille de la
       * chaine d'echange par la structure de taille de Wayland. */
      GHOST_ContextVK *context = new GHOST_ContextVK(
          want_context_params_,
          GHOST_kVulkanPlatformKorin,
          reinterpret_cast<Window>(static_cast<intptr_t>(kui_win_)),
          nullptr,
          nullptr,
          nullptr,
          &vulkan_window_info_,
          1,
          2,
          preferred_device_,
          &hdr_info_);
      if (context->initializeDrawingContext()) {
        return context;
      }
      delete context;
      return nullptr;
    }
#endif
    default:
      /* Pas d'OpenGL sur Korin PC : Vulkan seul (GHOST_ContextNone sinon, par GHOST_Window). */
      return nullptr;
  }
}

void GHOST_WindowKorin::setSize(int width, int height)
{
  /* width, height : la fenetre d'UIKit ; size_ : le contenu de Blender, sous la bande de titre */
  size_[0] = std::max(width, 1);
  size_[1] = std::max(height - KUI_TOP_BAND, 1);
#ifdef WITH_VULKAN_BACKEND
  /* GHOST_ContextVK refait la chaine d'echange au prochain swapBufferAcquire si la taille change. */
  vulkan_window_info_.size[0] = size_[0];
  vulkan_window_info_.size[1] = size_[1];
#endif
}

void GHOST_WindowKorin::handleResize(int width, int height)
{
  /* Elegant demande une taille (bord tire, bouton agrandir) : la fenetre la prend (son tampon suit). */
  kui_window_resize(kui_win_, width, height);
  setSize(width, height);
  drawTitleBand();
}

void GHOST_WindowKorin::drawTitleBand()
{
  kui_title_band(kui_win_, title_.c_str(), titlebar_rgb_);
}

GHOST_TSuccess GHOST_WindowKorin::applyWindowDecorationStyle()
{
  /* Blender donne la couleur de sa barre du haut (son theme) : la bande de titre la prend, et se
   * fond dans l'interface (WM_window_decoration_style_apply). */
  if (window_decoration_style_flags_ & GHOST_kDecorationColoredTitleBar) {
    const float *c = window_decoration_style_settings_.colored_titlebar_bg_color;
    auto byte = [](float v) { return long(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    titlebar_rgb_ = (byte(c[0]) << 16) | (byte(c[1]) << 8) | byte(c[2]);
  }
  else {
    titlebar_rgb_ = -1;
  }
  drawTitleBand();
  return GHOST_kSuccess;
}

void GHOST_WindowKorin::handleScreenSize(int width, int height)
{
  if (state_ == GHOST_kWindowStateFullScreen) {
    kui_window_place(kui_win_, 0, 0);
    kui_window_resize(kui_win_, width, height);
    setSize(width, height);
    drawTitleBand();
    system_->pushEvent(std::make_unique<GHOST_Event>(
        system_->getMilliSeconds(), GHOST_kEventWindowSize, this));
  }
}

void GHOST_WindowKorin::handleFocus(bool focused)
{
  focused_ = focused;
  if (focused && pointer_locked_) {
    /* Elegant rend le pointeur pris quand la fenetre perd le clavier : le reprendre. */
    kui_pointer_lock(kui_win_, 1);
  }
  if (focused) {
    applyCursor();
  }
}

void GHOST_WindowKorin::refreshOrigin()
{
  int x, y;
  if (kui_win_ > 0 && kui_window_pos(kui_win_, &x, &y) == 0) {
    origin_[0] = x;
    origin_[1] = y;
  }
}

void GHOST_WindowKorin::setTitle(const char *title)
{
  title_ = title ? title : "";
  kui_window_title(kui_win_, title_.c_str());
  drawTitleBand();
}

std::string GHOST_WindowKorin::getTitle() const
{
  return title_;
}

void GHOST_WindowKorin::getWindowBounds(GHOST_Rect &bounds) const
{
  /* TODO : le cadre d'Elegant autour du contenu (FRAME_BORDER, src/inc/armpi.inc) ; le contenu
   * seul en attendant, comme GHOST_WindowSDL. */
  getClientBounds(bounds);
}

void GHOST_WindowKorin::getClientBounds(GHOST_Rect &bounds) const
{
  bounds.l_ = origin_[0];
  bounds.t_ = origin_[1] + KUI_TOP_BAND;
  bounds.r_ = origin_[0] + size_[0];
  bounds.b_ = bounds.t_ + size_[1];
}

GHOST_TSuccess GHOST_WindowKorin::setClientWidth(uint32_t width)
{
  return setClientSize(width, uint32_t(size_[1]));
}

GHOST_TSuccess GHOST_WindowKorin::setClientHeight(uint32_t height)
{
  return setClientSize(uint32_t(size_[0]), height);
}

GHOST_TSuccess GHOST_WindowKorin::setClientSize(uint32_t width, uint32_t height)
{
  if (state_ == GHOST_kWindowStateFullScreen) {
    return GHOST_kFailure;
  }
  int screen_w, screen_h;
  system_->getScreenSize(screen_w, screen_h);
  const int w = std::clamp(int(width), 1, std::max(screen_w, 1));
  const int h = std::clamp(int(height) + KUI_TOP_BAND, KUI_TOP_BAND + 1, std::max(screen_h, 1));
  kui_window_resize(kui_win_, w, h);
  setSize(w, h);
  drawTitleBand();
  /* (ui_resize ne repond pas par WEV_RESIZE : l'evenement de GHOST est pose ici) */
  system_->pushEvent(
      std::make_unique<GHOST_Event>(system_->getMilliSeconds(), GHOST_kEventWindowSize, this));
  return GHOST_kSuccess;
}

void GHOST_WindowKorin::screenToClient(int32_t inX,
                                       int32_t inY,
                                       int32_t &outX,
                                       int32_t &outY) const
{
  outX = inX - origin_[0];
  outY = inY - origin_[1] - KUI_TOP_BAND;
}

void GHOST_WindowKorin::clientToScreen(int32_t inX,
                                       int32_t inY,
                                       int32_t &outX,
                                       int32_t &outY) const
{
  outX = inX + origin_[0];
  outY = inY + origin_[1] + KUI_TOP_BAND;
}

GHOST_TSuccess GHOST_WindowKorin::setState(GHOST_TWindowState state)
{
  if (state == state_) {
    return GHOST_kSuccess;
  }
  int screen_w, screen_h;
  system_->getScreenSize(screen_w, screen_h);

  /* Sortir de l'etat present. */
  switch (state_) {
    case GHOST_kWindowStateFullScreen:
      /* TODO : un vrai plein ecran (fenetre sans cadre KUI_WIN_FULLSCREEN, comme Godot) demande
       * une autre fenetre d'UIKit, donc une autre surface de Vulkan : a faire avec le contexte. */
      kui_window_top(kui_win_, 0);
      kui_window_resize(kui_win_, restore_rect_[2], restore_rect_[3]);
      kui_window_place(kui_win_, restore_rect_[0], restore_rect_[1]);
      setSize(restore_rect_[2], restore_rect_[3]);
      drawTitleBand();
      break;
    case GHOST_kWindowStateMaximized:
      if (state == GHOST_kWindowStateNormal) {
        /* (ui_maximize est le bouton agrandir du cadre : a verifier qu'il bascule) */
        kui_window_maximize(kui_win_);
      }
      break;
    case GHOST_kWindowStateMinimized:
      kui_window_raise(kui_win_); /* (« ui_raise la rend ») */
      break;
    default:
      break;
  }

  /* Entrer dans le nouvel etat. */
  switch (state) {
    case GHOST_kWindowStateMaximized:
      kui_window_maximize(kui_win_); /* (Elegant repond par WEV_RESIZE) */
      break;
    case GHOST_kWindowStateMinimized:
      kui_window_minimize(kui_win_);
      break;
    case GHOST_kWindowStateFullScreen:
      /* Premier jet : la fenetre au-dessus des autres, son contenu sur tout l'ecran (le cadre
       * deborde). */
      restore_rect_[0] = origin_[0];
      restore_rect_[1] = origin_[1];
      restore_rect_[2] = size_[0];
      restore_rect_[3] = size_[1] + KUI_TOP_BAND; /* (la fenetre, pas le contenu) */
      kui_window_top(kui_win_, 1);
      kui_window_place(kui_win_, 0, 0);
      kui_window_resize(kui_win_, screen_w, screen_h);
      setSize(screen_w, screen_h);
      drawTitleBand();
      break;
    case GHOST_kWindowStateNormal:
    default:
      break;
  }
  const bool fullscreen_changed = (state_ == GHOST_kWindowStateFullScreen) !=
                                  (state == GHOST_kWindowStateFullScreen);
  state_ = state;
  refreshOrigin();
  /* (agrandir : Elegant repond par WEV_RESIZE ; le plein ecran est fait ici, sans reponse) */
  if (fullscreen_changed) {
    system_->pushEvent(
        std::make_unique<GHOST_Event>(system_->getMilliSeconds(), GHOST_kEventWindowSize, this));
  }
  return GHOST_kSuccess;
}

GHOST_TWindowState GHOST_WindowKorin::getState() const
{
  /* TODO : Elegant ne dit pas quand l'utilisateur agrandit ou reduit par le cadre ; l'etat est
   * celui que Blender a demande. */
  return state_;
}

GHOST_TSuccess GHOST_WindowKorin::setOrder(GHOST_TWindowOrder order)
{
  if (order == GHOST_kWindowOrderTop) {
    kui_window_raise(kui_win_);
  }
  /* (GHOST_kWindowOrderBottom : pas de fonction d'UIKit, rien) */
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowKorin::invalidate()
{
  if (!invalid_window_) {
    system_->addDirtyWindow(this);
    invalid_window_ = true;
  }
  return GHOST_kSuccess;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Curseur
 * \{ */

void GHOST_WindowKorin::applyCursor()
{
  if (kui_win_ <= 0) {
    return;
  }
  const int shape = kui_cursor_from_ghost(cursor_shape_);
  kui_cursor(kui_win_, shape >= 0 ? shape : KUI_CUR_ARROW);
}

GHOST_TSuccess GHOST_WindowKorin::hasCursorShape(GHOST_TStandardCursor shape)
{
  return (kui_cursor_from_ghost(shape) >= 0) ? GHOST_kSuccess : GHOST_kFailure;
}

GHOST_TSuccess GHOST_WindowKorin::setWindowCursorShape(GHOST_TStandardCursor shape)
{
  const int kui_shape = kui_cursor_from_ghost(shape);
  if (kui_win_ > 0) {
    kui_cursor(kui_win_, kui_shape >= 0 ? kui_shape : KUI_CUR_ARROW);
  }
  /* (une forme sans equivalent prend la fleche, mais reste « posee » pour GHOST) */
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowKorin::setWindowCustomCursorShape(const uint8_t * /*bitmap*/,
                                                             const uint8_t * /*mask*/,
                                                             const int /*size*/[2],
                                                             const int /*hot_spot*/[2],
                                                             bool /*can_invert_color*/)
{
  /* TODO : curseurs personnalises (bitmap 1 bit + masque) : UIKit n'a que les formes UC_* ;
   * a ajouter en fin d'uikit.abi (ui_cursor_image), puis ici. La fleche en attendant. */
  if (kui_win_ > 0) {
    kui_cursor(kui_win_, KUI_CUR_ARROW);
  }
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowKorin::setWindowCursorVisibility(bool /*visible*/)
{
  /* TODO : UIKit ne sait cacher le curseur que pointeur pris (ui_pointer_lock) ; Blender cache le
   * curseur surtout par les grabs (GHOST_kGrabHide), qui eux le prennent. */
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowKorin::setWindowCursorGrab(GHOST_TGrabCursorMode mode)
{
  /* (appele par GHOST_Window::setCursorGrab avant qu'il ne change cursor_grab_ : c'est encore le
   * mode d'avant) */
  const bool lock_next = (mode == GHOST_kGrabWrap) || (mode == GHOST_kGrabHide);
  if (lock_next && !pointer_locked_) {
    /* Korin ne sait pas ramener le pointeur au bord oppose (pas de « warp ») : la fenetre prend le
     * pointeur, Elegant le cache et le fige, les deplacements arrivent relatifs (KUI_M_REL) et
     * GHOST_SystemKorin les accumule. GHOST_kGrabWrap : Blender dessine un curseur logiciel. */
    system_->grabLockBegin(this);
    kui_pointer_lock(kui_win_, 1);
    pointer_locked_ = true;
  }
  else if (!lock_next && pointer_locked_) {
    kui_pointer_lock(kui_win_, 0);
    pointer_locked_ = false;
    system_->grabLockEnd(this);
  }
  /* GHOST_kGrabNormal (garder le pointeur dans la fenetre) : pas de confinement dans Elegant, rien. */
  return GHOST_kSuccess;
}

bool GHOST_WindowKorin::getCursorGrabUseSoftwareDisplay()
{
  return GHOST_GRAB_NEEDS_SOFTWARE_CURSOR_FOR_WARP(cursor_grab_);
}

/** \} */
