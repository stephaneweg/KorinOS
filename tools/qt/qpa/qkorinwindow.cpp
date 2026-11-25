// QKorinWindow, QKorinBackingStore : voir qkorinwindow.h et docs/etude/QT.md.
#include "qkorinwindow.h"
#include "qkorinintegration.h"
#include "qkorintheme.h"

#include <QtGui/qwindow.h>
#include <QtGui/qscreen.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qpainter.h>
#include <QtGui/private/qwindow_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <cstring>

QT_BEGIN_NAMESPACE

QKorinWindow::QKorinWindow(QWindow *window) : QPlatformWindow(window)
{
    m_kind = kind();
    m_band = (m_kind == KQ_WIN_POPUP || m_kind == KQ_WIN_BORDERLESS) ? 0 : KQ_TOP_BAND;
    m_title = window->title();
    // la place de départ : celle de Qt, sinon centrée dans la zone utile (QPlatformWindow::initialGeometry)
    const QRect g = QPlatformWindow::initialGeometry(window, window->geometry(), 640, 480);
    QPlatformWindow::setGeometry(g);
}

QKorinWindow::~QKorinWindow()
{
    destroy();
}

int QKorinWindow::kind() const
{
    const Qt::WindowFlags f = window()->flags();
    const Qt::WindowType t = window()->type();
    if (t == Qt::Popup || t == Qt::ToolTip || t == Qt::SplashScreen || t == Qt::Drawer)
        return KQ_WIN_POPUP;
    // une fenêtre outil sans cadre : un popup fait à la main (PopupView de MuseScore : Qt::Tool | Frameless, fermé
    // dès que sa fenêtre perd le focus). Comme un menu : sans le focus d'UIKit, au-dessus, là où Qt le met ; le
    // focus de Qt lui est donné à part (isToolPopup)
    if (t == Qt::Tool && (f & Qt::FramelessWindowHint))
        return KQ_WIN_POPUP;
    if (f & Qt::FramelessWindowHint)
        return KQ_WIN_BORDERLESS;
    if ((t == Qt::Dialog || t == Qt::Sheet) && window()->modality() != Qt::NonModal)
        return KQ_WIN_DIALOG;
    const QSize mn = window()->minimumSize(), mx = window()->maximumSize();
    if (mn == mx && mn.isValid() && !mn.isEmpty())
        return KQ_WIN_FRAMED;	// (taille fixe)
    return KQ_WIN_RESIZABLE;
}

bool QKorinWindow::isToolPopup() const
{
    return m_kind == KQ_WIN_POPUP && window()->type() == Qt::Tool;
}

bool QKorinWindow::takesQtFocus() const
{
    if (window()->flags() & Qt::WindowDoesNotAcceptFocus)
        return false;
    return m_kind != KQ_WIN_POPUP || isToolPopup();
}

void QKorinWindow::create()
{
    if (m_win > 0)
        return;
    // (le genre suit les drapeaux du moment : Qt peut faire la fenêtre de plateforme avant setFlags)
    m_kind = kind();
    m_band = (m_kind == KQ_WIN_POPUP || m_kind == KQ_WIN_BORDERLESS) ? 0 : KQ_TOP_BAND;
    // une fenêtre sans cadre qui demande de l'alpha (fond transparent de QQuickView : la marge de l'ombre des
    // popups de MuseScore) : WF_ALPHA, sinon ses pixels transparents sortent noirs
    m_alpha = m_band == 0 && window()->requestedFormat().hasAlpha();
    const QRect g = geometry();
    // x, y : le contenu d'UIKit (la bande comprise) sur l'écran
    m_win = kq_window(g.x(), g.y() - m_band, qMax(g.width(), 1), qMax(g.height(), 1) + m_band,
                      m_title.toUtf8().constData(), m_kind | (m_alpha ? KQ_WIN_ALPHA : 0));
    if (m_win <= 0) {
        m_win = 0;
        qWarning("qt.qpa.korin : fenêtre refusée par UIKit");
        return;
    }
    QKorinIntegration::instance()->addWindow(m_win, this);
    if (qEnvironmentVariableIntValue("KORIN_QPA_DEBUG"))
        qWarning("qt.qpa.korin : fenêtre %ld (genre %d) %d,%d %dx%d « %s »", m_win, m_kind, g.x(), g.y(),
                 g.width(), g.height(), qPrintable(m_title));
    if (m_kind == KQ_WIN_POPUP) {
        kq_window_place(m_win, g.x(), g.y());	// (un menu va là où Qt le veut, sans que Elegant le replace)
        kq_window_top(m_win, 1);
    }
    propagateSizeHints();
    drawBand();
    reportGeometry();
    QKorinMenuBar::windowCreated(window());
}

void QKorinWindow::destroy()
{
    if (m_win <= 0)
        return;
    QKorinIntegration::instance()->removeWindow(m_win);
    kq_window_close(m_win);
    m_win = 0;
}

void QKorinWindow::refreshOrigin()
{
    if (m_win <= 0)
        return;
    int x = 0, y = 0;
    if (kq_window_pos(m_win, &x, &y) < 0)
        return;
    if (QPoint(x, y) != m_origin) {
        m_origin = QPoint(x, y);
        reportGeometry();
    }
}

// la géométrie réelle (taille et place de la fenêtre d'UIKit) -> Qt
void QKorinWindow::reportGeometry()
{
    if (m_win <= 0)
        return;
    int w = 0, h = 0, x = 0, y = 0;
    kq_window_size(m_win, &w, &h);
    if (kq_window_pos(m_win, &x, &y) == 0)
        m_origin = QPoint(x, y);
    const QRect g(m_origin.x(), m_origin.y() + m_band, qMax(w, 1), qMax(h - m_band, 1));
    if (g != geometry()) {
        QPlatformWindow::setGeometry(g);
        QWindowSystemInterface::handleGeometryChange(window(), g);
    }
}

void QKorinWindow::setGeometry(const QRect &rect)
{
    QPlatformWindow::setGeometry(rect);
    if (m_win > 0) {
        int w = 0, h = 0;
        kq_window_size(m_win, &w, &h);
        if (w != rect.width() || h != rect.height() + m_band) {
            kq_window_resize(m_win, qMax(rect.width(), 1), qMax(rect.height(), 1) + m_band);
            drawBand();
        }
        if (m_origin != QPoint(rect.x(), rect.y() - m_band))
            kq_window_place(m_win, rect.x(), rect.y() - m_band);
        reportGeometry();
    } else {
        QWindowSystemInterface::handleGeometryChange(window(), rect);
    }
    if (window()->isVisible())
        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
}

void QKorinWindow::handleResize(int w, int h)
{
    // Elegant demande une taille (bord tiré, agrandir) : la fenêtre la prend (son tampon suit)
    kq_window_resize(m_win, w, h);
    drawBand();
    reportGeometry();
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
}

void QKorinWindow::setVisible(bool visible)
{
    if (visible) {
        create();
        if (m_win <= 0)
            return;
        if (takesQtFocus())
            QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
    } else {
        // un popup outil qui avait le focus de Qt (UIKit ne le lui a jamais donné) : il revient à sa fenêtre
        if (isToolPopup() && QGuiApplication::focusWindow() == window()) {
            QWindow *back = window()->transientParent();
            QWindowSystemInterface::handleFocusWindowChanged(back && back->isVisible() ? back : nullptr,
                                                             Qt::ActiveWindowFocusReason);
        }
        // (UIKit ne cache pas une fenêtre : elle est fermée, et refaite au prochain show)
        QWindowSystemInterface::handleExposeEvent(window(), QRegion());
        destroy();
    }
    QWindowSystemInterface::flushWindowSystemEvents();
}

void QKorinWindow::setWindowTitle(const QString &title)
{
    m_title = title;
    if (m_win > 0) {
        kq_window_title(m_win, title.toUtf8().constData());
        drawBand();
    }
}

void QKorinWindow::drawBand()
{
    if (m_win > 0 && m_band > 0)
        kq_title_band(m_win, m_title.toUtf8().constData(), m_active || window()->isActive());
}

void QKorinWindow::setWindowFlags(Qt::WindowFlags flags)
{
    QPlatformWindow::setWindowFlags(flags);
    // (le genre de la fenêtre d'UIKit ne change pas après sa création ; Qt refait la fenêtre si besoin)
}

void QKorinWindow::setWindowState(Qt::WindowStates state)
{
    if (m_win <= 0 || state == m_state)
        return;
    const QRect avail = screen()->availableGeometry();
    if (state & Qt::WindowMinimized) {
        kq_window_minimize(m_win);
    } else if (state & Qt::WindowFullScreen) {
        m_normalGeometry = geometry();
        const QRect s = screen()->geometry();
        kq_window_top(m_win, 1);
        kq_window_place(m_win, s.x(), s.y() - m_band);
        kq_window_resize(m_win, s.width(), s.height() + m_band);
    } else if (state & Qt::WindowMaximized) {
        if (!(m_state & (Qt::WindowMaximized | Qt::WindowFullScreen)))
            m_normalGeometry = geometry();
        kq_window_top(m_win, 0);
        kq_window_place(m_win, avail.x(), avail.y());
        kq_window_resize(m_win, avail.width(), avail.height());
    } else if (m_normalGeometry.isValid()) {
        kq_window_top(m_win, 0);
        kq_window_resize(m_win, m_normalGeometry.width(), m_normalGeometry.height() + m_band);
        kq_window_place(m_win, m_normalGeometry.x(), m_normalGeometry.y() - m_band);
    }
    m_state = state;
    drawBand();
    reportGeometry();
    QWindowSystemInterface::handleWindowStateChanged(window(), state);
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
}

void QKorinWindow::raise()
{
    if (m_win > 0)
        kq_window_raise(m_win);
}

void QKorinWindow::requestActivateWindow()
{
    if (m_win <= 0)
        return;
    kq_window_raise(m_win);
    if (takesQtFocus())
        QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
}

void QKorinWindow::propagateSizeHints()
{
    if (m_win <= 0 || m_kind != KQ_WIN_RESIZABLE)
        return;
    const QSize mn = window()->minimumSize();
    if (mn.isValid() && !mn.isEmpty())
        kq_window_min_size(m_win, mn.width(), mn.height() + m_band);
}

// --- le backing store ------------------------------------------------------------------------------------------

QKorinBackingStore::QKorinBackingStore(QWindow *window) : QPlatformBackingStore(window) { }

void QKorinBackingStore::resize(const QSize &size, const QRegion &)
{
    if (m_image.size() != size)
        m_image = QImage(size, window()->format().hasAlpha() ? QImage::Format_ARGB32_Premultiplied
                                                              : QImage::Format_RGB32);
}

void QKorinBackingStore::beginPaint(const QRegion &region)
{
    if (m_image.hasAlphaChannel()) {
        // (une fenêtre transparente : effacer avant de dessiner ; Elegant compose sans transparence pour l'instant)
        QPainter p(&m_image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (const QRect &r : region)
            p.fillRect(r, Qt::transparent);
    }
}

void QKorinBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    static const bool debug0 = qEnvironmentVariableIntValue("KORIN_QPA_DEBUG") > 1;
    if (debug0)
        qWarning("qt.qpa.korin : flush(%s %p, handle %p, %d rect.) store de %p", window->metaObject()->className(),
                 (void *)window, (void *)window->handle(), int(region.rectCount()), (void *)this->window());
    QPlatformWindow *pw = window->handle();
    if (!pw)
        return;
    // (une fenêtre enfant native n'a pas de fenêtre d'UIKit : on va à son premier niveau, offset compris)
    QPoint shift;
    while (pw->parent()) {
        shift += pw->geometry().topLeft();
        pw = pw->parent();
    }
    auto *kw = static_cast<QKorinWindow *>(pw);
    const long win = kw->handle();
    if (win <= 0)
        return;
    int dw = 0, dh = 0;
    unsigned *dst = kq_window_pixels(win, &dw, &dh);
    if (!dst)
        return;
    const int band = kw->band();
    const QRect clip(0, 0, dw, dh - band);
    static const bool debug = qEnvironmentVariableIntValue("KORIN_QPA_DEBUG") > 0;
    if (debug)
        qWarning("qt.qpa.korin : flush fenêtre %ld (%dx%d) image %dx%d région %d rect. %d,%d %dx%d décalage %d,%d", win,
                 dw, dh, m_image.width(), m_image.height(), int(region.rectCount()), region.boundingRect().x(),
                 region.boundingRect().y(), region.boundingRect().width(), region.boundingRect().height(),
                 offset.x(), offset.y());
    // Le rectangle englobant de la région : une seule recopie et un seul ui_update (UIKit réunit les zones de
    // toute façon, et Qt en donne souvent plusieurs voisines).
    QRect src = region.boundingRect().translated(offset) & m_image.rect();
    QRect tgt = QRect(src.topLeft() - offset + shift, src.size()) & clip;
    if (tgt.isEmpty())
        return;
    src = QRect(tgt.topLeft() - shift + offset, tgt.size());
    const int sbpl = m_image.bytesPerLine();
    const uchar *s = m_image.constBits() + src.y() * sbpl + src.x() * 4;
    unsigned *d = dst + (tgt.y() + band) * dw + tgt.x();
    const bool alpha = kw->alpha() && m_image.hasAlphaChannel();
    for (int y = 0; y < tgt.height(); y++) {
        if (!alpha) {
            std::memcpy(d, s, size_t(tgt.width()) * 4);
        } else {
            // ARGB prémultiplié de Qt (alpha 255 : opaque) -> pixel de WF_ALPHA (octet haut : transparence,
            // 0 opaque ; couleur non prémultipliée)
            const unsigned *sp = reinterpret_cast<const unsigned *>(s);
            for (int x = 0; x < tgt.width(); x++) {
                const unsigned p = sp[x], a = p >> 24;
                if (a == 255) {
                    d[x] = p & 0xffffffu;
                } else if (a == 0) {
                    d[x] = 0xff000000u;
                } else {
                    const unsigned r = (((p >> 16) & 255) * 255 + a / 2) / a, g = (((p >> 8) & 255) * 255 + a / 2) / a,
                                   b = ((p & 255) * 255 + a / 2) / a;
                    d[x] = ((255 - a) << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
                }
            }
        }
        s += sbpl;
        d += dw;
    }
    kq_window_update(win, tgt.x(), tgt.y() + band, tgt.width(), tgt.height());
}

QT_END_NAMESPACE
