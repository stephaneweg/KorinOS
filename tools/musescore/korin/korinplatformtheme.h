/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Korin OS : le thème de plateforme de MuseScore (tools/musescore, docs/etude/MUSESCORE.md) : la barre de menus
 * globale de Korin (la QPA korin donne un QPlatformMenuBar) et le thème clair ou sombre du bureau (Givre, Onyx Noir).
 */
#pragma once

#include <QMetaObject>

#include "internal/iplatformtheme.h"

namespace muse::ui {
class KorinPlatformTheme : public IPlatformTheme
{
public:
    void startListening() override;
    void stopListening() override;

    bool isFollowSystemThemeAvailable() const override;
    bool isSystemThemeDark() const override;
    async::Notification platformThemeChanged() const override;

    bool isGlobalMenuAvailable() const override;

    void applyPlatformStyleOnAppForTheme(const ThemeCode& themeCode) override;
    void applyPlatformStyleOnWindowForTheme(QWindow* window, const ThemeCode& themeCode) override;

private:
    async::Notification m_platformThemeChanged;
    QMetaObject::Connection m_connection;
};
}
