// QKorinIntegration : voir qkorinintegration.h et docs/etude/QT.md.
#include "qkorinintegration.h"
#include "qkorinwindow.h"
#include "qkorintheme.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdeadlinetimer.h>
#include <QtCore/qelapsedtimer.h>
#include <QtCore/qmimedata.h>
#include <QtCore/private/qeventdispatcher_unix_p.h>
#include <QtGui/qcursor.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/private/qpixmap_raster_p.h>
#include <QtGui/private/qfreetypefontdatabase_p.h>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qwindowsysteminterface.h>

#include <chrono>

QT_BEGIN_NAMESPACE

using namespace std::chrono;
using namespace Qt::StringLiterals;

// --- le curseur ------------------------------------------------------------------------------------------------

static int korinCursorShape(Qt::CursorShape s)
{
    switch (s) {
    case Qt::IBeamCursor: return KQ_CUR_TEXT;
    case Qt::PointingHandCursor: return KQ_CUR_HAND;
    case Qt::SizeHorCursor:
    case Qt::SplitHCursor: return KQ_CUR_HRESIZE;
    case Qt::SizeVerCursor:
    case Qt::SplitVCursor: return KQ_CUR_VRESIZE;
    case Qt::WaitCursor:
    case Qt::BusyCursor: return KQ_CUR_WAIT;
    case Qt::CrossCursor: return KQ_CUR_CROSS;
    case Qt::OpenHandCursor:
    case Qt::ClosedHandCursor:
    case Qt::DragMoveCursor:
    case Qt::DragCopyCursor:
    case Qt::DragLinkCursor: return KQ_CUR_DRAG;
    case Qt::SizeAllCursor: return KQ_CUR_MOVE;
    case Qt::SizeFDiagCursor: return KQ_CUR_NWSE;
    case Qt::SizeBDiagCursor: return KQ_CUR_NESW;
    case Qt::ForbiddenCursor: return KQ_CUR_NO;
    default: return KQ_CUR_ARROW;	// (BlankCursor, BitmapCursor : pas encore dans UIKit)
    }
}

void QKorinCursor::changeCursor(QCursor *windowCursor, QWindow *window)
{
    if (!window || !window->handle())
        return;
    const long win = static_cast<QKorinWindow *>(window->handle())->handle();
    if (win > 0)
        kq_cursor(win, korinCursorShape(windowCursor ? windowCursor->shape() : Qt::ArrowCursor));
}

// --- l'écran ---------------------------------------------------------------------------------------------------

QKorinScreen::QKorinScreen() : m_cursor(new QKorinCursor)
{
    refresh(0, 0);
}

void QKorinScreen::refresh(int w, int h)
{
    int sw = 0, sh = 0, bottom = 0, top = 0;
    kq_screen(&sw, &sh, &bottom, &top);
    if (w > 0 && h > 0) {
        sw = w;
        sh = h;
    }
    if (sw <= 0 || sh <= 0) {
        sw = 1280;
        sh = 800;
    }
    m_geometry = QRect(0, 0, sw, sh);
    m_available = QRect(0, top, sw, qMax(sh - top - bottom, 1));
}

QSizeF QKorinScreen::physicalSize() const
{
    // (96 points par pouce)
    return QSizeF(m_geometry.width() * 25.4 / 96.0, m_geometry.height() * 25.4 / 96.0);
}

QWindow *QKorinScreen::topLevelAt(const QPoint &p) const
{
    // (Elegant ne dit pas l'ordre des fenêtres : la fenêtre au focus d'abord, puis la première qui contient p)
    QWindow *focus = QGuiApplication::focusWindow();
    if (focus && focus->isVisible() && focus->frameGeometry().contains(p))
        return focus;
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows)
        if (w->isVisible() && w->handle() && w->frameGeometry().contains(p))
            return w;
    return nullptr;
}

// --- les polices --------------------------------------------------------------------------------------------
//
// La base de FreeType de Qt, sans fontconfig : ni alias ni repli. Une famille absente (« Times New Roman » d'une
// partition de MuseScore 2) tombait alors sur n'importe quelle police, parfois une police de musique (b -> ♮). Ici :
// les alias usuels vers les polices de /res/fonts, et des replis selon le style (puis les symboles et les émojis).

class QKorinFontDatabase : public QFreeTypeFontDatabase
{
public:
    QString resolveFontFamilyAlias(const QString &family) const override
    {
        static const QHash<QString, QString> alias = {
            { u"times new roman"_s, u"DejaVu Serif"_s }, { u"times"_s, u"DejaVu Serif"_s },
            { u"serif"_s, u"DejaVu Serif"_s }, { u"georgia"_s, u"DejaVu Serif"_s },
            { u"liberation serif"_s, u"DejaVu Serif"_s }, { u"freeserif"_s, u"DejaVu Serif"_s },
            { u"arial"_s, u"DejaVu Sans"_s }, { u"helvetica"_s, u"DejaVu Sans"_s },
            { u"sans-serif"_s, u"DejaVu Sans"_s }, { u"sans serif"_s, u"DejaVu Sans"_s },
            { u"sans"_s, u"DejaVu Sans"_s }, { u"verdana"_s, u"DejaVu Sans"_s },
            { u"liberation sans"_s, u"DejaVu Sans"_s }, { u"segoe ui"_s, u"Inter"_s },
            { u"courier new"_s, u"DejaVu Sans Mono"_s }, { u"courier"_s, u"DejaVu Sans Mono"_s },
            { u"monospace"_s, u"DejaVu Sans Mono"_s }, { u"liberation mono"_s, u"DejaVu Sans Mono"_s },
        };
        const QString a = alias.value(family.toLower());
        return a.isEmpty() ? QFreeTypeFontDatabase::resolveFontFamilyAlias(family) : a;
    }

    QStringList fallbacksForFamily(const QString &family, QFont::Style style, QFont::StyleHint hint,
                                   QFontDatabasePrivate::ExtendedScript script) const override
    {
        QStringList l = QFreeTypeFontDatabase::fallbacksForFamily(family, style, hint, script);
        switch (hint) {
        case QFont::Serif:
        case QFont::OldEnglish:
            l << u"DejaVu Serif"_s;
            break;
        case QFont::TypeWriter:
        case QFont::Monospace:
            l << u"DejaVu Sans Mono"_s;
            break;
        default:
            break;
        }
        l << u"DejaVu Sans"_s << u"Noto Sans Symbols"_s << u"Noto Sans Symbols2"_s << u"Noto Sans Math"_s
          << u"Noto Emoji"_s;
        l.removeDuplicates();
        return l;
    }
};

// --- la boucle d'événements ------------------------------------------------------------------------------------

static constexpr int kSliceMs = 5;		// une tranche d'attente dans ui_event
static constexpr int kWaitMaxMs = 50;		// sans minuterie : on rend la main à QEventLoop après au plus ceci

bool QKorinEventDispatcher::processEvents(QEventLoop::ProcessEventsFlags flags)
{
    const QEventLoop::ProcessEventsFlags noWait = flags & ~QEventLoop::WaitForMoreEvents;
    bool did = QEventDispatcherUNIX::processEvents(noWait);	// messages, minuteries, notificateurs (poll sans attente)
    if (!(flags & QEventLoop::ExcludeUserInputEvents))
        did |= m_integration->pumpEvents(0) > 0;
    did |= QWindowSystemInterface::sendWindowSystemEvents(flags);
    if (did || !(flags & QEventLoop::WaitForMoreEvents))
        return did;

    // rien à faire : attendre un événement d'UIKit, un réveil ou la prochaine minuterie
    auto *d = static_cast<QEventDispatcherUNIXPrivate *>(QObjectPrivate::get(this));
    int wait = kWaitMaxMs;
    if (std::optional<nanoseconds> r = d->timerList.timerWait())
        wait = int(qBound(qint64(0), qint64(duration_cast<milliseconds>(*r).count()), qint64(kWaitMaxMs)));
    if (d->interrupt.loadRelaxed())
        return false;
    emit aboutToBlock();
    QElapsedTimer t;
    t.start();
    int n = 0;
    while (!m_woken.loadAcquire()) {
        const int left = wait - int(t.elapsed());
        if (left <= 0)
            break;
        n = m_integration->pumpEvents(qMin(left, kSliceMs));
        if (n)
            break;
    }
    m_woken.storeRelease(0);
    emit awake();
    did = n > 0;
    did |= QEventDispatcherUNIX::processEvents(noWait);
    did |= QWindowSystemInterface::sendWindowSystemEvents(flags);
    return did;
}

void QKorinEventDispatcher::wakeUp()
{
    m_woken.storeRelease(1);
    QUnixEventDispatcherQPA::wakeUp();
}

// --- le presse-papiers (texte seulement, comme UIKit pour l'instant) ------------------------------------------

QMimeData *QKorinClipboard::mimeData(QClipboard::Mode mode)
{
    if (mode != QClipboard::Clipboard)
        return nullptr;
    static char buf[256 * 1024];
    const int n = kq_clip_get(buf, int(sizeof buf));
    if (!m_data)
        m_data.reset(new QMimeData);
    m_data->clear();
    if (n > 0)
        m_data->setText(QString::fromUtf8(buf, qstrnlen(buf, sizeof buf)));
    return m_data.get();
}

void QKorinClipboard::setMimeData(QMimeData *data, QClipboard::Mode mode)
{
    if (mode != QClipboard::Clipboard)
        return;
    if (data && data->hasText())
        kq_clip_set(data->text().toUtf8().constData());
    delete data;	// (QPlatformClipboard en prend la propriété)
    emitChanged(mode);
}

// --- l'intégration ---------------------------------------------------------------------------------------------

QKorinIntegration::QKorinIntegration(const QStringList &)
{
    if (!kq_open())
        qFatal("qt.qpa.korin : UIKit ne s'ouvre pas (pas de bureau ?)");
    // les polices du système (FreeType de Qt, sans fontconfig)
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", kq_font_dir());
    // Qt Quick Controls : Fusion, qui suit la palette (les jetons d'UIKit), plutôt que Basic (gris, sans thème)
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
}

QKorinIntegration::~QKorinIntegration()
{
    if (m_screen)
        QWindowSystemInterface::handleScreenRemoved(m_screen);
}

void QKorinIntegration::initialize()
{
    m_screen = new QKorinScreen;
    QWindowSystemInterface::handleScreenAdded(m_screen);
}

QKorinIntegration *QKorinIntegration::instance()
{
    return static_cast<QKorinIntegration *>(QGuiApplicationPrivate::platformIntegration());
}

bool QKorinIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps: return true;
    case MultipleWindows: return true;
    case NonFullScreenWindows: return true;
    case WindowManagement: return true;
    case RhiBasedRendering: return false;	// (Qt Quick : le rendu logiciel ; Vulkan de VkKit plus tard)
    case OpenGL: return false;
    case ApplicationState: return false;
    default: return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformWindow *QKorinIntegration::createPlatformWindow(QWindow *window) const
{
    return new QKorinWindow(window);
}

QPlatformBackingStore *QKorinIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QKorinBackingStore(window);
}

QAbstractEventDispatcher *QKorinIntegration::createEventDispatcher() const
{
    return new QKorinEventDispatcher(const_cast<QKorinIntegration *>(this));
}

QPlatformFontDatabase *QKorinIntegration::fontDatabase() const
{
    if (!m_fontDatabase)
        m_fontDatabase.reset(new QKorinFontDatabase);
    return m_fontDatabase.get();
}

QPlatformClipboard *QKorinIntegration::clipboard() const
{
    if (!m_clipboard)
        m_clipboard.reset(new QKorinClipboard);
    return m_clipboard.get();
}

QPlatformNativeInterface *QKorinIntegration::nativeInterface() const
{
    if (!m_nativeInterface)
        m_nativeInterface.reset(new QPlatformNativeInterface);
    return m_nativeInterface.get();
}

QStringList QKorinIntegration::themeNames() const
{
    return { QStringLiteral("korin") };
}

QPlatformTheme *QKorinIntegration::createPlatformTheme(const QString &) const
{
    return new QKorinTheme;
}

QVariant QKorinIntegration::styleHint(StyleHint hint) const
{
    switch (hint) {
    case ShowIsFullScreen: return false;
    case MouseDoubleClickInterval: return 400;
    case CursorFlashTime: return 1000;
    case KeyboardAutoRepeatRate: return 30;
    default: return QPlatformIntegration::styleHint(hint);
    }
}

// --- les événements d'UIKit ------------------------------------------------------------------------------------

int QKorinIntegration::pumpEvents(int wait_ms)
{
    kq_event ev;
    int n = 0;
    if (!kq_poll(&ev, wait_ms))
        return 0;
    do {
        handleEvent(ev);
        n++;
    } while (n < 256 && kq_poll(&ev, 0));
    return n;
}

Qt::KeyboardModifiers QKorinIntegration::modifiers(int mods) const
{
    Qt::KeyboardModifiers m;
    if (mods & KQ_MOD_SHIFT) m |= Qt::ShiftModifier;
    if (mods & KQ_MOD_CTRL) m |= Qt::ControlModifier;
    if (mods & KQ_MOD_LALT) m |= Qt::AltModifier;	// (AltGr, à droite : la troisième touche, pas Alt)
    if (mods & KQ_MOD_META) m |= Qt::MetaModifier;
    return m;
}

void QKorinIntegration::handleEvent(const kq_event &ev)
{
    QKorinWindow *w = windowFor(ev.win);
    static const bool debug = qEnvironmentVariableIntValue("KORIN_QPA_DEBUG") > 0;
    if (debug && ev.type != KQ_EV_MOUSE)
        qWarning("qt.qpa.korin : événement %d genre %d x %d y %d code %d car %d mods %x fenêtre %ld%s", ev.type,
                 ev.kind, ev.x, ev.y, ev.code, ev.ch, ev.mods, ev.win, w ? "" : " (inconnue)");
    switch (ev.type) {
    case KQ_EV_KEY:
        if (!w) {
            QWindow *f = QGuiApplication::focusWindow();
            w = f && f->handle() ? static_cast<QKorinWindow *>(f->handle()) : nullptr;
        }
        if (w)
            handleKey(w, ev);
        break;
    case KQ_EV_MOUSE:
        if (w)
            handleMouse(w, ev);
        break;
    case KQ_EV_WHEEL:
        if (w && ev.y) {
            const QPointF local(m_screen->m_cursor->m_pos - w->geometry().topLeft());
            QWindowSystemInterface::handleWheelEvent(w->window(), local, QPointF(m_screen->m_cursor->m_pos),
                                                     QPoint(), QPoint(0, ev.y * 120), modifiers(m_mods));
        }
        break;
    case KQ_EV_CLOSE:
        if (w)
            QWindowSystemInterface::handleCloseEvent(w->window());
        break;
    case KQ_EV_FOCUS:
        if (!w)
            break;
        w->setActive(ev.kind != 0);
        {
            // un popup outil ouvert par cette fenêtre tient le focus de Qt sans celui d'UIKit (QKorinWindow::isToolPopup)
            QWindow *f = QGuiApplication::focusWindow();
            QKorinWindow *fw = f && f->handle() ? static_cast<QKorinWindow *>(f->handle()) : nullptr;
            const bool popupOfW = fw && fw != w && fw->isToolPopup() && f->transientParent() == w->window();
            if (ev.kind) {
                w->refreshOrigin();
                if (!popupOfW)
                    QWindowSystemInterface::handleFocusWindowChanged(w->window(), Qt::ActiveWindowFocusReason);
            } else {
                // le clavier part : relâcher les modificateurs tenus
                m_mods = 0;
                std::fill(std::begin(m_keyDown), std::end(m_keyDown), false);
                if (f == w->window() || popupOfW)
                    QWindowSystemInterface::handleFocusWindowChanged(nullptr, Qt::ActiveWindowFocusReason);
            }
        }
        break;
    case KQ_EV_MENU:
        QKorinMenuBar::activate(ev.win, ev.code);
        break;
    case KQ_EV_RESIZE:
        if (w && ev.x > 0 && ev.y > 0)
            w->handleResize(ev.x, ev.y);
        break;
    case KQ_EV_SCREEN:
        if (ev.x > 0 && ev.y > 0) {
            m_screen->refresh(ev.x, ev.y);
            QWindowSystemInterface::handleScreenGeometryChange(m_screen->screen(), m_screen->m_geometry,
                                                               m_screen->m_available);
        }
        break;
    case KQ_EV_THEME:
        if (auto *t = static_cast<QKorinTheme *>(QGuiApplicationPrivate::platformTheme()))
            t->refresh();
        for (QKorinWindow *kw : std::as_const(m_windows))
            kw->drawBand();
        QWindowSystemInterface::handleThemeChange();
        break;
    default:
        break;	// (WEV_DROP, WEV_FRAME, WEV_PAD… : pas encore)
    }
}

// HID (page 7) -> Qt::Key, d'après la position (QWERTY US) : ce qui ne donne pas de caractère
static int hidToQtKey(int code)
{
    if (code >= 0x04 && code <= 0x1d) return Qt::Key_A + (code - 0x04);
    if (code >= 0x1e && code <= 0x26) return Qt::Key_1 + (code - 0x1e);
    if (code >= 0x3a && code <= 0x45) return Qt::Key_F1 + (code - 0x3a);
    if (code >= 0x68 && code <= 0x73) return Qt::Key_F13 + (code - 0x68);
    if (code >= 0x59 && code <= 0x61) return Qt::Key_1 + (code - 0x59);	// pavé 1-9
    switch (code) {
    case 0x27: return Qt::Key_0;
    case 0x28: return Qt::Key_Return;
    case 0x29: return Qt::Key_Escape;
    case 0x2a: return Qt::Key_Backspace;
    case 0x2b: return Qt::Key_Tab;
    case 0x2c: return Qt::Key_Space;
    case 0x2d: return Qt::Key_Minus;
    case 0x2e: return Qt::Key_Equal;
    case 0x2f: return Qt::Key_BracketLeft;
    case 0x30: return Qt::Key_BracketRight;
    case 0x31: case 0x32: return Qt::Key_Backslash;
    case 0x33: return Qt::Key_Semicolon;
    case 0x34: return Qt::Key_Apostrophe;
    case 0x35: return Qt::Key_QuoteLeft;
    case 0x36: return Qt::Key_Comma;
    case 0x37: return Qt::Key_Period;
    case 0x38: return Qt::Key_Slash;
    case 0x39: return Qt::Key_CapsLock;
    case 0x46: return Qt::Key_Print;
    case 0x47: return Qt::Key_ScrollLock;
    case 0x48: return Qt::Key_Pause;
    case 0x49: return Qt::Key_Insert;
    case 0x4a: return Qt::Key_Home;
    case 0x4b: return Qt::Key_PageUp;
    case 0x4c: return Qt::Key_Delete;
    case 0x4d: return Qt::Key_End;
    case 0x4e: return Qt::Key_PageDown;
    case 0x4f: return Qt::Key_Right;
    case 0x50: return Qt::Key_Left;
    case 0x51: return Qt::Key_Down;
    case 0x52: return Qt::Key_Up;
    case 0x53: return Qt::Key_NumLock;
    case 0x54: return Qt::Key_Slash;
    case 0x55: return Qt::Key_Asterisk;
    case 0x56: return Qt::Key_Minus;
    case 0x57: return Qt::Key_Plus;
    case 0x58: return Qt::Key_Enter;
    case 0x62: return Qt::Key_0;
    case 0x63: return Qt::Key_Period;
    case 0x64: return Qt::Key_Less;		// la touche < > de l'ISO
    case 0x65: return Qt::Key_Menu;
    case 0x67: return Qt::Key_Equal;
    case 0xe0: case 0xe4: return Qt::Key_Control;
    case 0xe1: case 0xe5: return Qt::Key_Shift;
    case 0xe2: return Qt::Key_Alt;
    case 0xe6: return Qt::Key_AltGr;
    case 0xe3: case 0xe7: return Qt::Key_Meta;
    default: return Qt::Key_unknown;
    }
}

static int hidModifierBit(int code)
{
    switch (code) {
    case 0xe0: return KQ_MOD_LCTRL;
    case 0xe1: return KQ_MOD_LSHIFT;
    case 0xe2: return KQ_MOD_LALT;
    case 0xe3: return KQ_MOD_LMETA;
    case 0xe4: return KQ_MOD_RCTRL;
    case 0xe5: return KQ_MOD_RSHIFT;
    case 0xe6: return KQ_MOD_RALT;
    case 0xe7: return KQ_MOD_RMETA;
    default: return 0;
    }
}

static bool isKeypad(int code)
{
    return code >= 0x54 && code <= 0x63;
}

void QKorinIntegration::handleKey(QKorinWindow *w, const kq_event &ev)
{
    const int code = ev.code & 0xff;
    const bool pressed = ev.kind != 0;
    QWindow *target = QGuiApplication::focusWindow();
    if (!target)
        target = w->window();

    // les modificateurs : une seule source (les touches 0xe0..0xe7 ou l'octet EV_MODS)
    if (const int bit = hidModifierBit(code)) {
        m_mods = pressed ? (m_mods | bit) : (m_mods & ~bit);
        QWindowSystemInterface::handleExtendedKeyEvent(target, pressed ? QEvent::KeyPress : QEvent::KeyRelease,
                                                       hidToQtKey(code), modifiers(m_mods), code, code, m_mods);
        m_keyDown[code] = pressed;
        return;
    }
    m_mods = ev.mods & 0xff;
    Qt::KeyboardModifiers mods = modifiers(m_mods);
    if (isKeypad(code))
        mods |= Qt::KeypadModifier;

    int key;
    QString text;
    bool repeat = false;
    if (pressed) {
        const int ch = ev.ch;
        if (ch >= 'a' && ch <= 'z')
            key = Qt::Key_A + (ch - 'a');	// (les lettres suivent la disposition : Ctrl+Z sur la touche Z de l'AZERTY)
        else if (ch >= 'A' && ch <= 'Z')
            key = Qt::Key_A + (ch - 'A');
        else if ((m_mods & KQ_MOD_CTRL) && ch >= 1 && ch <= 26)
            key = Qt::Key_A + (ch - 1);
        else if (ch > 32 && ch != 127 && !isKeypad(code))
            key = QChar::toUpper(char32_t(ch));	// (les touches de Qt pour le Latin-1 : la majuscule)
        else
            key = hidToQtKey(code);
        if (ch >= 32 && ch != 127 && !(m_mods & (KQ_MOD_CTRL | KQ_MOD_LALT | KQ_MOD_META)))
            text = QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch), 1);
        else if (key == Qt::Key_Return || key == Qt::Key_Enter)
            text = QStringLiteral("\r");
        else if (key == Qt::Key_Tab)
            text = QStringLiteral("\t");
        else if (key == Qt::Key_Backspace)
            text = QStringLiteral("\b");
        else if (key == Qt::Key_Escape)
            text = QString(QChar(27));
        repeat = m_keyDown[code];
        m_keyAs[code] = key;
        m_keyText[code] = text;
    } else {
        key = m_keyDown[code] ? m_keyAs[code] : hidToQtKey(code);
        text = m_keyDown[code] ? m_keyText[code] : QString();
    }
    m_keyDown[code] = pressed;
    QWindowSystemInterface::handleExtendedKeyEvent(target, pressed ? QEvent::KeyPress : QEvent::KeyRelease, key,
                                                   mods, code, code, m_mods, text, repeat);
}

void QKorinIntegration::handleMouse(QKorinWindow *w, const kq_event &ev)
{
    if (ev.kind == KQ_M_REL)
        return;	// (pointeur pris : pas encore)
    // le contenu de Qt commence sous la bande de titre ; une pression dans la bande est pour Elegant
    const QPoint local(ev.x, ev.y - w->band());
    if (ev.kind == KQ_M_DOWN)
        w->refreshOrigin();	// (la fenêtre a pu être déplacée par sa bande)
    const QPoint global = w->origin() + QPoint(ev.x, ev.y);
    m_screen->m_cursor->m_pos = global;
    const Qt::KeyboardModifiers mods = modifiers(ev.mods ? (ev.mods & 0xff) : m_mods);
    QWindow *qw = w->window();

    if (m_pointerWindow != qw) {
        if (m_pointerWindow)
            QWindowSystemInterface::handleLeaveEvent(m_pointerWindow);
        QWindowSystemInterface::handleEnterEvent(qw, local, global);
        m_pointerWindow = qw;
    }

    static const struct { int bit; Qt::MouseButton button; } table[] = {
        { KQ_BUTTON_LEFT, Qt::LeftButton },
        { KQ_BUTTON_RIGHT, Qt::RightButton },
        { KQ_BUTTON_MIDDLE, Qt::MiddleButton },
    };
    // m_buttons : ce que Qt sait tenu ; m_ignored : appuis laissés à Elegant (bande de titre), leurs relâches aussi
    const int b = ev.code & 7;
    const int changed = b ^ (m_buttons | m_ignored);
    auto stateOf = [&](int bits) {
        Qt::MouseButtons state;
        for (const auto &u : table)
            if (bits & u.bit)
                state |= u.button;
        return state;
    };
    if (!changed) {
        QWindowSystemInterface::handleMouseEvent(qw, local, global, stateOf(m_buttons), Qt::NoButton,
                                                 QEvent::MouseMove, mods);
        return;
    }
    for (const auto &t : table) {
        if (!(changed & t.bit))
            continue;
        const bool down = (b & t.bit) != 0;
        if (down && local.y() < 0) {
            m_ignored |= t.bit;	// (appui dans la bande de titre : Elegant déplace la fenêtre)
            continue;
        }
        if (!down && (m_ignored & t.bit)) {
            m_ignored &= ~t.bit;
            continue;
        }
        m_buttons = down ? (m_buttons | t.bit) : (m_buttons & ~t.bit);
        QWindowSystemInterface::handleMouseEvent(qw, local, global, stateOf(m_buttons), t.button,
                                                 down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease, mods);
    }
}

QT_END_NAMESPACE
