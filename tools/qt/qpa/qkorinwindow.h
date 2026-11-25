// QKorinWindow, QKorinBackingStore : une fenêtre de premier niveau de Qt = une fenêtre d'UIKit (docs/etude/QT.md).
// Les fenêtres à cadre ont en haut une bande de titre de KQ_TOP_BAND px (les boutons du cadre d'Elegant sont sur le
// contenu) : la géométrie de Qt est le contenu sous la bande, frameMargins() la compte.
#pragma once

#include <qpa/qplatformwindow.h>
#include <qpa/qplatformbackingstore.h>
#include <QtGui/qimage.h>

#include "korin_ui.h"

QT_BEGIN_NAMESPACE

class QKorinWindow : public QPlatformWindow
{
public:
    explicit QKorinWindow(QWindow *window);
    ~QKorinWindow();

    void setGeometry(const QRect &rect) override;
    void setVisible(bool visible) override;
    void setWindowTitle(const QString &title) override;
    void setWindowState(Qt::WindowStates state) override;
    void setWindowFlags(Qt::WindowFlags flags) override;
    void raise() override;
    void lower() override { }
    void requestActivateWindow() override;
    void propagateSizeHints() override;
    QMargins frameMargins() const override { return QMargins(0, m_band, 0, 0); }
    // (non nul dès la construction : QWidget le lit une fois, à sa création, et s'en sert pour trouver son parent
    // natif ; la fenêtre d'UIKit, elle, n'existe qu'au premier show. 0 : les zones défilantes n'étaient jamais
    // recopiées à l'écran)
    WId winId() const override { return WId(quintptr(this)); }
    bool setKeyboardGrabEnabled(bool) override { return true; }
    bool setMouseGrabEnabled(bool) override { return true; }

    long handle() const { return m_win; }
    int band() const { return m_band; }
    bool alpha() const { return m_alpha; }
    QPoint origin() const { return m_origin; }	// le contenu de la fenêtre d'UIKit sur l'écran
    void refreshOrigin();			// relit la place (Elegant déplace les fenêtres)
    void handleResize(int w, int h);		// WEV_RESIZE : la taille de la fenêtre d'UIKit
    void drawBand();
    void setActive(bool active) { m_active = active; drawBand(); }

private:
    void create();
    void destroy();
    int kind() const;
public:
    bool isToolPopup() const;	// (Qt::Tool sans cadre : popup d'UIKit, focus de Qt à part)
    bool takesQtFocus() const;
private:
    void reportGeometry();

    long m_win = 0;
    int m_band = 0;
    int m_kind = KQ_WIN_RESIZABLE;
    bool m_active = false;
    bool m_alpha = false;	// (WF_ALPHA : sans cadre, format avec alpha)
    QPoint m_origin;
    QString m_title;
    Qt::WindowStates m_state = Qt::WindowNoState;
    QRect m_normalGeometry;
};

class QKorinBackingStore : public QPlatformBackingStore
{
public:
    explicit QKorinBackingStore(QWindow *window);
    QPaintDevice *paintDevice() override { return &m_image; }
    void beginPaint(const QRegion &region) override;
    void flush(QWindow *window, const QRegion &region, const QPoint &offset) override;
    void resize(const QSize &size, const QRegion &staticContents) override;
    QImage toImage() const override { return m_image; }
private:
    QImage m_image;
};

QT_END_NAMESPACE
