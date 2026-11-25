// QKorinIntegration : la plateforme korin de Qt sur UIKit (Korin OS). Fenêtres d'UIKit, rendu raster recopié dans
// leurs pixels, événements d'UIKit (ui_event) traduits pour QWindowSystemInterface, thème d'après les jetons
// d'UIKit, menus dans la barre de menus globale (docs/etude/QT.md).
#pragma once

#include <qpa/qplatformintegration.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformcursor.h>
#include <qpa/qplatformclipboard.h>
#include <QtGui/private/qunixeventdispatcher_qpa_p.h>
#include <QtCore/qatomic.h>
#include <QtCore/qhash.h>
#include <QtCore/qpointer.h>
#include <memory>

#include "korin_ui.h"

QT_BEGIN_NAMESPACE

class QKorinWindow;
class QKorinIntegration;

class QKorinCursor : public QPlatformCursor
{
public:
    void changeCursor(QCursor *windowCursor, QWindow *window) override;
    QPoint pos() const override { return m_pos; }
    QPoint m_pos;
};

class QKorinScreen : public QPlatformScreen
{
public:
    QKorinScreen();
    QRect geometry() const override { return m_geometry; }
    QRect availableGeometry() const override { return m_available; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
    QSizeF physicalSize() const override;
    QDpi logicalDpi() const override { return QDpi(96, 96); }
    QString name() const override { return QStringLiteral("Korin"); }
    QPlatformCursor *cursor() const override { return m_cursor.get(); }
    QWindow *topLevelAt(const QPoint &p) const override;

    void refresh(int w, int h);	// (largeur, hauteur nouvelles de l'écran : 0, 0 = les relire)
    QRect m_geometry, m_available;
    std::unique_ptr<QKorinCursor> m_cursor;
};

// La boucle d'événements : celle d'Unix (messages, minuteries, notificateurs), plus ui_event. UIKit n'a pas de
// descripteur pour poll : l'attente se fait dans ui_event, par tranches courtes (un réveil d'un autre fil n'attend
// pas plus d'une tranche), jusqu'à la prochaine minuterie.
class QKorinEventDispatcher : public QUnixEventDispatcherQPA
{
public:
    explicit QKorinEventDispatcher(QKorinIntegration *integration) : m_integration(integration) { }
    bool processEvents(QEventLoop::ProcessEventsFlags flags) override;
    void wakeUp() override;
private:
    QKorinIntegration *m_integration;
    QAtomicInt m_woken;
};

class QKorinClipboard : public QPlatformClipboard
{
public:
    QMimeData *mimeData(QClipboard::Mode mode = QClipboard::Clipboard) override;
    void setMimeData(QMimeData *data, QClipboard::Mode mode = QClipboard::Clipboard) override;
    bool supportsMode(QClipboard::Mode mode) const override { return mode == QClipboard::Clipboard; }
    bool ownsMode(QClipboard::Mode) const override { return false; }
private:
    std::unique_ptr<QMimeData> m_data;
};

class QKorinIntegration : public QPlatformIntegration
{
public:
    explicit QKorinIntegration(const QStringList &parameters);
    ~QKorinIntegration();

    void initialize() override;
    bool hasCapability(QPlatformIntegration::Capability cap) const override;
    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;
    QAbstractEventDispatcher *createEventDispatcher() const override;
    QPlatformFontDatabase *fontDatabase() const override;
    QPlatformClipboard *clipboard() const override;
    QPlatformNativeInterface *nativeInterface() const override;
    QStringList themeNames() const override;
    QPlatformTheme *createPlatformTheme(const QString &name) const override;
    QVariant styleHint(StyleHint hint) const override;

    static QKorinIntegration *instance();
    QKorinScreen *screen() const { return m_screen; }

    // les fenêtres d'UIKit -> celles de la QPA (pour les événements)
    void addWindow(long win, QKorinWindow *w) { m_windows.insert(win, w); }
    void removeWindow(long win) { m_windows.remove(win); }
    QKorinWindow *windowFor(long win) const { return m_windows.value(win, nullptr); }

    // les événements d'UIKit : le premier attendu au plus wait_ms, puis ce qui est déjà là -> nombre traité
    int pumpEvents(int wait_ms);

private:
    void handleEvent(const kq_event &ev);
    void handleKey(QKorinWindow *w, const kq_event &ev);
    void handleMouse(QKorinWindow *w, const kq_event &ev);
    Qt::KeyboardModifiers modifiers(int mods) const;

    QKorinScreen *m_screen = nullptr;
    mutable std::unique_ptr<QPlatformFontDatabase> m_fontDatabase;
    mutable std::unique_ptr<QKorinClipboard> m_clipboard;
    mutable std::unique_ptr<QPlatformNativeInterface> m_nativeInterface;
    QHash<long, QKorinWindow *> m_windows;
    int m_mods = 0;			// modificateurs tenus (octet du HID)
    int m_buttons = 0;			// boutons de la souris tenus (bits KQ_BUTTON_*)
    int m_ignored = 0;			// appuis dans une bande de titre (pour Elegant)
    QPointer<QWindow> m_pointerWindow;	// la fenêtre sous le pointeur (entrée, sortie)
    bool m_keyDown[256] = {};
    int m_keyAs[256] = {};		// la touche de Qt donnée à l'appui (la relâche reprend la même)
    QString m_keyText[256];
};

QT_END_NAMESPACE
