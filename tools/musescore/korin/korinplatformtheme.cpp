/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Korin OS : le thème de plateforme de MuseScore (korinplatformtheme.h).
 */
#include "korinplatformtheme.h"

#include <QGuiApplication>
#include <QStyleHints>

using namespace muse::ui;
using namespace muse::async;

void KorinPlatformTheme::startListening()
{
    // (la QPA korin dit le changement de thème du bureau : WEV_THEME -> handleThemeChange -> colorSchemeChanged)
    m_connection = QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                                    [this](Qt::ColorScheme) { m_platformThemeChanged.notify(); });
}

void KorinPlatformTheme::stopListening()
{
    QObject::disconnect(m_connection);
}

bool KorinPlatformTheme::isFollowSystemThemeAvailable() const
{
    return true;
}

bool KorinPlatformTheme::isSystemThemeDark() const
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

Notification KorinPlatformTheme::platformThemeChanged() const
{
    return m_platformThemeChanged;
}

bool KorinPlatformTheme::isGlobalMenuAvailable() const
{
    return true;	// (la barre de menus de Korin, par le QPlatformMenuBar de la QPA)
}

void KorinPlatformTheme::applyPlatformStyleOnAppForTheme(const ThemeCode&)
{
}

void KorinPlatformTheme::applyPlatformStyleOnWindowForTheme(QWindow*, const ThemeCode&)
{
}
