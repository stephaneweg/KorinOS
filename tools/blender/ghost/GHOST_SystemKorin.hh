/* SPDX-FileCopyrightText: 2026 Korin OS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 * Declaration of GHOST_SystemKorin : GHOST sur UIKit (Korin OS, docs/etude/BLENDER.md § 2.2).
 *
 * Le systeme lit les evenements d'UIKit (kui_poll, toutes les fenetres du programme) et les traduit en
 * evenements de GHOST. Korin ne sait pas deplacer le pointeur : pas de GHOST_kCapabilityCursorWarp ;
 * les « grabs » caches (GHOST_kGrabWrap, GHOST_kGrabHide) prennent le pointeur (kui_pointer_lock) et
 * recoivent des deplacements relatifs (KUI_M_REL), accumules en une position virtuelle, comme le fait
 * GHOST_SystemWayland avec zwp_relative_pointer_v1. Le curseur de GHOST_kGrabWrap est alors dessine par
 * Blender (curseur logiciel, wm_draw.cc).
 */

#pragma once

#include "../GHOST_Types.hh"
#include "GHOST_System.hh"

#include <cstdint>
#include <vector>

struct kui_event;
class GHOST_WindowKorin;

class GHOST_SystemKorin : public GHOST_System {
 public:
  GHOST_SystemKorin();
  ~GHOST_SystemKorin() override;

  /* --- GHOST_ISystem --- */

  bool processEvents(bool waitForEvent) override;

  bool setConsoleWindowState(GHOST_TConsoleWindowState /*action*/) override
  {
    return false;
  }

  GHOST_TSuccess getModifierKeys(GHOST_ModifierKeys &keys) const override;

  GHOST_TSuccess getButtons(GHOST_Buttons &buttons) const override;

  GHOST_TCapabilityFlag getCapabilities() const override;

  char *getClipboard(bool selection) const override;

  void putClipboard(const char *buffer, bool selection) const override;

  uint64_t getMilliSeconds() const override;

  uint8_t getNumDisplays() const override;

  GHOST_TSuccess getCursorPosition(int32_t &x, int32_t &y) const override;

  GHOST_TSuccess setCursorPosition(int32_t x, int32_t y) override;

  void getAllDisplayDimensions(uint32_t &width, uint32_t &height) const override;

  void getMainDisplayDimensions(uint32_t &width, uint32_t &height) const override;

  GHOST_IContext *createOffscreenContext(GHOST_GPUSettings gpu_settings) override;

  GHOST_TSuccess disposeContext(GHOST_IContext *context) override;

  /* --- Korin (pour GHOST_WindowKorin) --- */

  /** Fenetre a redessiner : un GHOST_kEventWindowUpdate au prochain #processEvents. */
  void addDirtyWindow(GHOST_WindowKorin *window);

  /** La fenetre est detruite : l'oublier (fenetres sales, pointeur, focus). */
  void removeWindow(GHOST_WindowKorin *window);

  /** Taille de l'ecran (KUI_EV_SCREEN la tient a jour). */
  void getScreenSize(int &width, int &height) const;

  /** Zone utile de l'ecran, sans les bandes du panneau et de la barre de menus. */
  void getUsableArea(int &top, int &height) const;

  /**
   * Debut d'un grab cache (pointeur pris) : la position virtuelle part de la derniere position
   * connue du pointeur dans cette fenetre (coordonnees du contenu).
   */
  void grabLockBegin(GHOST_WindowKorin *window);

  /**
   * Fin d'un grab cache : la position virtuelle redevient celle du vrai pointeur, reste la ou il a
   * ete pris (Korin ne sait pas le deplacer) ; un deplacement y est envoye pour que Blender le sache.
   */
  void grabLockEnd(GHOST_WindowKorin *window);

  /** Position du pointeur (contenu de #window) telle que GHOST la voit (virtuelle pendant un grab). */
  void getPointerClient(const GHOST_WindowKorin *window, int32_t &x, int32_t &y) const;

 private:
  GHOST_TSuccess init() override;

  GHOST_IWindow *createWindow(const char *title,
                              int32_t left,
                              int32_t top,
                              uint32_t width,
                              uint32_t height,
                              GHOST_TWindowState state,
                              GHOST_GPUSettings gpu_settings,
                              const bool exclusive = false,
                              const bool is_dialog = false,
                              const GHOST_IWindow *parent_window = nullptr) override;

  /** La fenetre de GHOST d'une poignee d'UIKit (EV_WIN), nullptr si elle n'est pas (plus) a nous. */
  GHOST_WindowKorin *findGhostWindow(long kui_win) const;

  void processEvent(const kui_event &ev);
  void processKey(GHOST_WindowKorin *window, const kui_event &ev, uint64_t event_ms);
  void processMouse(GHOST_WindowKorin *window, const kui_event &ev, uint64_t event_ms);
  /** Les modificateurs ont change (champ EV_MODS) : appuis et relaches synthetises. */
  void syncModifiers(GHOST_WindowKorin *window, int mods, uint64_t event_ms);
  void pushCursorMove(GHOST_WindowKorin *window, uint64_t event_ms);
  bool generateWindowExposeEvents();
  /** Relit l'origine (coin du contenu sur l'ecran) des fenetres : Elegant n'annonce pas les deplacements. */
  void refreshWindowOrigins(bool force);

  /** Fenetres a redessiner (#invalidate). */
  std::vector<GHOST_WindowKorin *> dirty_windows_;

  /** Debut de l'horloge de #getMilliSeconds. */
  uint64_t start_ms_;

  int screen_w_ = 0, screen_h_ = 0, reserved_bottom_ = 0, reserved_top_ = 0;

  /** Etat du clavier : octet des modificateurs du HID (KUI_MOD_*) et touches tenues (code HID). */
  int mods_ = 0;
  bool key_down_[256] = {};
  /** La touche de GHOST donnee a l'appui (la relache doit dire la meme, meme si le caractere change). */
  GHOST_TKey key_pressed_as_[256];

  /** Boutons de la souris tenus (KUI_BUTTON_*). */
  int mouse_buttons_ = 0;

  /** La fenetre sous le pointeur (dernier evenement de souris) et la position du pointeur dans son contenu. */
  GHOST_WindowKorin *pointer_window_ = nullptr;
  int32_t pointer_xy_[2] = {0, 0};

  /** Pointeur pris (grab cache) : la fenetre, et la position virtuelle (contenu) faite des deplacements relatifs. */
  GHOST_WindowKorin *lock_window_ = nullptr;
  int32_t lock_xy_[2] = {0, 0};

  /** Derniere relecture des origines des fenetres (#refreshWindowOrigins). */
  uint64_t origins_ms_ = 0;
};
