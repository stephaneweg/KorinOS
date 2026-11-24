// QKorinTheme : le thème de la plateforme korin (palette et polices d'après les jetons d'UIKit, jamais de couleur en
// dur) et les menus de Qt dans la barre de menus globale de Korin (QPlatformMenuBar, docs/etude/QT.md § menus).
#pragma once

#include <qpa/qplatformtheme.h>
#include <qpa/qplatformmenu.h>
#include <QtGui/qfont.h>
#include <QtGui/qkeysequence.h>
#include <QtGui/qpalette.h>
#include <QtCore/qlist.h>
#include <QtCore/qpointer.h>
#include <memory>

QT_BEGIN_NAMESPACE

class QKorinMenu;

class QKorinTheme : public QPlatformTheme
{
public:
    QKorinTheme();
    const QPalette *palette(Palette type = SystemPalette) const override;
    const QFont *font(Font type = SystemFont) const override;
    QVariant themeHint(ThemeHint hint) const override;
    Qt::ColorScheme colorScheme() const override;
    QPlatformMenuItem *createPlatformMenuItem() const override;
    QPlatformMenu *createPlatformMenu() const override;
    QPlatformMenuBar *createPlatformMenuBar() const override;
    void refresh();				// le thème du bureau a changé (WEV_THEME)
private:
    QPalette m_palette;
    QFont m_font, m_fixed, m_small;
};

class QKorinMenuItem : public QPlatformMenuItem
{
public:
    QKorinMenuItem();
    ~QKorinMenuItem();
    void setText(const QString &text) override { m_text = text; changed(); }
    void setIcon(const QIcon &) override { }
    void setMenu(QPlatformMenu *menu) override;
    void setVisible(bool v) override { m_visible = v; changed(); }
    void setIsSeparator(bool s) override { m_separator = s; changed(); }
    void setFont(const QFont &) override { }
    void setRole(MenuRole role) override { m_role = role; changed(); }
    void setCheckable(bool c) override { m_checkable = c; changed(); }
    void setChecked(bool c) override { m_checked = c; changed(); }
#if QT_CONFIG(shortcut)
    void setShortcut(const QKeySequence &s) override { m_shortcut = s; changed(); }
#endif
    void setEnabled(bool e) override { m_enabled = e; changed(); }
    void setIconSize(int) override { }

    void changed();
    QString m_text;
    QPointer<QKorinMenu> m_menu;
    bool m_visible = true, m_separator = false, m_checkable = false, m_checked = false, m_enabled = true;
    MenuRole m_role = NoRole;
    QKeySequence m_shortcut;
    QKorinMenu *m_parent = nullptr;
    long m_id = -1;				// son numéro dans le dernier texte publié
};

class QKorinMenu : public QPlatformMenu
{
public:
    ~QKorinMenu();
    void insertMenuItem(QPlatformMenuItem *menuItem, QPlatformMenuItem *before) override;
    void removeMenuItem(QPlatformMenuItem *menuItem) override;
    void syncMenuItem(QPlatformMenuItem *) override { changed(); }
    void syncSeparatorsCollapsible(bool) override { }
    void setText(const QString &text) override { m_text = text; changed(); }
    void setIcon(const QIcon &) override { }
    void setEnabled(bool e) override { m_enabled = e; changed(); }
    bool isEnabled() const override { return m_enabled; }
    void setVisible(bool v) override { m_visible = v; changed(); }
    QPlatformMenuItem *menuItemAt(int position) const override { return m_items.value(position); }
    QPlatformMenuItem *menuItemForTag(quintptr tag) const override;
    QPlatformMenuItem *createMenuItem() const override { return new QKorinMenuItem; }
    QPlatformMenu *createSubMenu() const override { return new QKorinMenu; }

    void changed();
    QString m_text;
    bool m_enabled = true, m_visible = true;
    QList<QKorinMenuItem *> m_items;
};

class QKorinMenuBar : public QPlatformMenuBar
{
public:
    QKorinMenuBar();
    ~QKorinMenuBar();
    void insertMenu(QPlatformMenu *menu, QPlatformMenu *before) override;
    void removeMenu(QPlatformMenu *menu) override;
    void syncMenu(QPlatformMenu *) override { changed(); }
    void handleReparent(QWindow *newParentWindow) override;
    QWindow *parentWindow() const override { return m_window; }
    QPlatformMenu *menuForTag(quintptr tag) const override;
    QPlatformMenu *createMenu() const override { return new QKorinMenu; }

    void changed();
    void publish();				// le texte des menus -> la barre (ui_menu), si la fenêtre existe
    static void activate(long win, long id);	// WEV_MENU
    static void windowCreated(QWindow *window);	// la fenêtre d'UIKit de window vient d'être faite
    static void markDirty();			// un menu ou une entrée a changé : publier bientôt

    QList<QKorinMenu *> m_menus;
    QPointer<QWindow> m_window;
    long m_published = 0;			// la fenêtre d'UIKit qui a reçu le texte
    QByteArray m_buf[2];			// deux tampons en alternance (UIKit reconnaît un texte à son adresse)
    int m_which = 0;
    QList<QPointer<QPlatformMenuItem>> m_ids;	// numéro -> entrée
};

QT_END_NAMESPACE
