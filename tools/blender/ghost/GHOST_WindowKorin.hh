/* SPDX-FileCopyrightText: 2026 Korin OS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 * Declaration of GHOST_WindowKorin : une fenetre de Blender est une fenetre d'UIKit
 * (ghost_korin_ui.h), dessinee par Vulkan (GHOST_ContextVK, branche WITH_GHOST_KORIN :
 * surface headless + VkKorinSurfaceInfo). Pas d'OpenGL.
 */

#pragma once

#include "GHOST_Window.hh"

#ifdef WITH_VULKAN_BACKEND
#  include "GHOST_ContextVK.hh"
#endif

#include <string>

class GHOST_SystemKorin;

class GHOST_WindowKorin : public GHOST_Window {
 public:
  GHOST_WindowKorin(GHOST_SystemKorin *system,
                    const char *title,
                    int32_t left,
                    int32_t top,
                    uint32_t width,
                    uint32_t height,
                    GHOST_TWindowState state,
                    GHOST_TDrawingContextType type,
                    const GHOST_ContextParams &context_params,
                    const GHOST_GPUDevice &preferred_device,
                    const bool exclusive = false,
                    const bool is_dialog = false,
                    const GHOST_IWindow *parent_window = nullptr);

  ~GHOST_WindowKorin() override;

  /* --- Korin (pour GHOST_SystemKorin) --- */

  /** Poignee de la fenetre d'UIKit (EV_WIN des evenements, VkKorinSurfaceInfo.window). */
  long getKuiWindow() const
  {
    return kui_win_;
  }

  /** Elegant a redimensionne la fenetre (KUI_EV_RESIZE) : la taille est acceptee et retenue. */
  void handleResize(int width, int height);
  /** La bande de titre au-dessus du contenu (KUI_TOP_BAND), aux couleurs du theme. */
  void drawTitleBand();
  /** La couleur de la barre du haut de Blender pour la bande de titre (#GHOST_kDecorationColoredTitleBar). */
  GHOST_TSuccess applyWindowDecorationStyle() override;

  /** L'ecran a change de taille (KUI_EV_SCREEN) : une fenetre plein ecran le suit. */
  void handleScreenSize(int width, int height);

  /** Focus gagne ou perdu (KUI_EV_FOCUS) : Elegant rend le pointeur pris a la perte du clavier. */
  void handleFocus(bool focused);

  /** Relit l'origine du contenu sur l'ecran (#screenToClient, #clientToScreen). */
  void refreshOrigin();

  /** Pointeur pris pour un grab cache (GHOST_kGrabWrap, GHOST_kGrabHide). */
  bool isPointerLocked() const
  {
    return pointer_locked_;
  }

  void validate()
  {
    invalid_window_ = false;
  }

  /* --- GHOST_IWindow --- */

  bool getValid() const override;

  bool isDialog() const override
  {
    return is_dialog_;
  }

  void *getOSWindow() const override
  {
    return reinterpret_cast<void *>(kui_win_);
  }

  void setTitle(const char *title) override;
  std::string getTitle() const override;

  void getWindowBounds(GHOST_Rect &bounds) const override;
  void getClientBounds(GHOST_Rect &bounds) const override;

  GHOST_TSuccess setClientWidth(uint32_t width) override;
  GHOST_TSuccess setClientHeight(uint32_t height) override;
  GHOST_TSuccess setClientSize(uint32_t width, uint32_t height) override;

  void screenToClient(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const override;
  void clientToScreen(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const override;

  GHOST_TSuccess setState(GHOST_TWindowState state) override;
  GHOST_TWindowState getState() const override;

  GHOST_TSuccess setOrder(GHOST_TWindowOrder order) override;

  GHOST_TSuccess invalidate() override;

  GHOST_TSuccess hasCursorShape(GHOST_TStandardCursor shape) override;

  uint16_t getDPIHint() override
  {
    return 96; /* TODO : l'echelle du theme de Korin (ui_scale) */
  }

 protected:
  GHOST_Context *newDrawingContext(GHOST_TDrawingContextType type) override;

  GHOST_TSuccess setWindowCursorGrab(GHOST_TGrabCursorMode mode) override;
  bool getCursorGrabUseSoftwareDisplay() override;

  GHOST_TSuccess setWindowCursorShape(GHOST_TStandardCursor shape) override;
  GHOST_TSuccess setWindowCustomCursorShape(const uint8_t *bitmap,
                                            const uint8_t *mask,
                                            const int size[2],
                                            const int hot_spot[2],
                                            bool can_invert_color) override;
  GHOST_TSuccess setWindowCursorVisibility(bool visible) override;

 private:
  /** Applique #cursor_shape_ (ou la fleche) a la fenetre d'UIKit. */
  void applyCursor();
  /** Nouvelle taille du contenu : retenue, et donnee a la chaine d'echange de Vulkan. */
  void setSize(int width, int height);

  GHOST_SystemKorin *system_;
  long kui_win_ = 0;
  bool valid_setup_ = false;
  bool invalid_window_ = false;
  bool is_dialog_ = false;

  std::string title_;
  /** Le fond de la bande de titre (0x00RRGGBB, donne par Blender), -1 : le theme du bureau. */
  long titlebar_rgb_ = -1;
  int size_[2] = {0, 0};
  /** Coin du contenu sur l'ecran (ui_window_pos), relu par #refreshOrigin. */
  int origin_[2] = {0, 0};

  GHOST_TWindowState state_ = GHOST_kWindowStateNormal;
  /** Taille et place d'avant le plein ecran (pour en revenir). */
  int restore_rect_[4] = {0, 0, 0, 0};

  bool pointer_locked_ = false;
  bool focused_ = false;

  GHOST_GPUDevice preferred_device_;
#ifdef WITH_VULKAN_BACKEND
  /** Taille de la chaine d'echange : la WSI de Korin (surface headless) ne la connait pas
   * (currentExtent = 0xFFFFFFFF), GHOST_ContextVK la lit ici comme pour Wayland. */
  GHOST_ContextVK_WindowInfo vulkan_window_info_ = {{0, 0}};
#endif
};
