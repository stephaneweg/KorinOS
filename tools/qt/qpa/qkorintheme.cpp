// QKorinTheme et les menus de la barre globale : voir qkorintheme.h et docs/etude/QT.md.
#include "qkorintheme.h"
#include "qkorinwindow.h"
#include "korin_ui.h"

#include <QtCore/qtimer.h>
#include <QtGui/qwindow.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qfontdatabase.h>

QT_BEGIN_NAMESPACE

// (tools/qt/style/qkorinstyle.h, QKorinStyle::KorinTokensHint, KorinMetricsHint)
static constexpr int KorinTokensHint = 0x4B520001;
static constexpr int KorinMetricsHint = 0x4B520002;

// --- le thème --------------------------------------------------------------------------------------------------

static QColor tok(int i)
{
    const unsigned *t = kq_tokens();
    return t ? QColor::fromRgb(t[i] & 0xFFFFFF) : QColor();
}

static bool isDark(const QColor &c)
{
    return (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000 < 128;
}

QKorinTheme::QKorinTheme()
{
    refresh();
}

void QKorinTheme::refresh()
{
    // la palette : les jetons du thème du bureau (Givre, Onyx Noir…), jamais des couleurs en dur
    QPalette p;
    const QColor face = tok(KQ_TJ_FACE), surface = tok(KQ_TJ_SURFACE), text1 = tok(KQ_TJ_TEXT1);
    const QColor text3 = tok(KQ_TJ_TEXT3), accent = tok(KQ_TJ_ACCENT);
    p.setColor(QPalette::Window, face);
    p.setColor(QPalette::WindowText, text1);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase, tok(KQ_TJ_SUNKEN));
    p.setColor(QPalette::Text, text1);
    p.setColor(QPalette::Button, tok(KQ_TJ_RAISED));
    p.setColor(QPalette::ButtonText, text1);
    p.setColor(QPalette::BrightText, tok(KQ_TJ_DANGER));
    p.setColor(QPalette::Light, tok(KQ_TJ_RAISED));
    p.setColor(QPalette::Midlight, tok(KQ_TJ_RAISED_LOW));
    p.setColor(QPalette::Mid, tok(KQ_TJ_LINE));
    p.setColor(QPalette::Dark, tok(KQ_TJ_LINE_STRONG));
    p.setColor(QPalette::Shadow, tok(KQ_TJ_OUTLINE));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, tok(KQ_TJ_ACCENT_INK));
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::LinkVisited, tok(KQ_TJ_ACCENT_DARK));
    p.setColor(QPalette::ToolTipBase, text1);	// (les infobulles de UIKit : SURFACE sur TEXTE1)
    p.setColor(QPalette::ToolTipText, surface);
    p.setColor(QPalette::PlaceholderText, text3);
    p.setColor(QPalette::Accent, accent);
    for (QPalette::ColorRole r : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
        p.setColor(QPalette::Disabled, r, text3);
    p.setColor(QPalette::Disabled, QPalette::Highlight, tok(KQ_TJ_LINE));
    m_palette = p;

    // les polices : celles de UIKit (Inter, sinon DejaVu Sans), 14 px comme les contrôles
    m_font = QFont(QStringList{ QStringLiteral("Inter"), QStringLiteral("DejaVu Sans") });
    m_font.setPixelSize(14);
    m_small = m_font;
    m_small.setPixelSize(12);
    m_fixed = QFont(QStringLiteral("DejaVu Sans Mono"));
    m_fixed.setPixelSize(14);
    m_fixed.setStyleHint(QFont::TypeWriter);
}

const QPalette *QKorinTheme::palette(Palette type) const
{
    return type == SystemPalette ? &m_palette : nullptr;
}

const QFont *QKorinTheme::font(Font type) const
{
    switch (type) {
    case SystemFont: return &m_font;
    case FixedFont: return &m_fixed;
    case SmallFont:
    case MiniFont:
    case TipLabelFont:
    case StatusBarFont: return &m_small;
    default: return nullptr;
    }
}

Qt::ColorScheme QKorinTheme::colorScheme() const
{
    return isDark(tok(KQ_TJ_FACE)) ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light;
}

QVariant QKorinTheme::themeHint(ThemeHint hint) const
{
    switch (hint) {
    case StyleNames: return QStringList{ QStringLiteral("korin"), QStringLiteral("Fusion") };
    case DialogButtonBoxLayout: return QVariant(1);	// (KdeLayout : Aide, Appliquer… OK Annuler)
    case KeyboardScheme: return QVariant(int(X11KeyboardScheme));
    case UiEffects: return QVariant(0);
    case ShowShortcutsInContextMenus: return true;
    case ContextMenuOnMouseRelease: return false;
    default: break;
    }
    // les indications propres à Korin, pour le style korin (tools/qt/style/qkorinstyle.h : mêmes valeurs) : le
    // tableau des jetons d'UIKit et ses métriques ; le style ne dépend ainsi ni d'UIKit ni de la QPA
    if (int(hint) == KorinTokensHint)
        return QVariant::fromValue(quintptr(kq_tokens()));
    if (int(hint) == KorinMetricsHint) {
        QVariantList l;
        for (int i = KQ_UMT_PX; i <= KQ_UMT_TOUCH; i++)
            l.append(kq_metric(i));
        return l;
    }
    return QPlatformTheme::themeHint(hint);
}

QPlatformMenuItem *QKorinTheme::createPlatformMenuItem() const { return new QKorinMenuItem; }
QPlatformMenu *QKorinTheme::createPlatformMenu() const { return new QKorinMenu; }
QPlatformMenuBar *QKorinTheme::createPlatformMenuBar() const { return new QKorinMenuBar; }

// --- la barre de menus globale ---------------------------------------------------------------------------------
//
// Le texte des menus d'UIKit (docs/EGUI.md, src/sys/menubar/menubar.c) : une ligne par entrée,
//   M<titre>                     un menu de la barre (M* : le menu au nom de l'application)
//   I<id>[d][c]<tab><libellé><tab><raccourci>   une entrée (d grisée, c cochée)
//   -                            un séparateur
//   S<titre> … E                 un sous-menu (la barre n'en montre qu'un niveau : au-delà, aplati)
// Un choix arrive en WEV_MENU avec l'id. 16 Ko au plus pour tout le texte (WS_MENU_MAX).

static constexpr int kMenuMax = 16384;
static QList<QKorinMenuBar *> s_menuBars;
static bool s_dirty = false;

QKorinMenuItem::QKorinMenuItem() { }

QKorinMenuItem::~QKorinMenuItem()
{
    if (m_parent)
        m_parent->m_items.removeAll(this);
    QKorinMenuBar::markDirty();
}

void QKorinMenuItem::setMenu(QPlatformMenu *menu)
{
    m_menu = static_cast<QKorinMenu *>(menu);
    changed();
}

void QKorinMenuItem::changed()
{
    QKorinMenuBar::markDirty();
}

QKorinMenu::~QKorinMenu()
{
    for (QKorinMenuItem *i : std::as_const(m_items))
        i->m_parent = nullptr;
    for (QKorinMenuBar *b : std::as_const(s_menuBars))
        b->m_menus.removeAll(this);
    QKorinMenuBar::markDirty();
}

void QKorinMenu::insertMenuItem(QPlatformMenuItem *menuItem, QPlatformMenuItem *before)
{
    auto *item = static_cast<QKorinMenuItem *>(menuItem);
    m_items.removeAll(item);
    const qsizetype at = before ? m_items.indexOf(static_cast<QKorinMenuItem *>(before)) : -1;
    if (at >= 0)
        m_items.insert(at, item);
    else
        m_items.append(item);
    item->m_parent = this;
    changed();
}

void QKorinMenu::removeMenuItem(QPlatformMenuItem *menuItem)
{
    auto *item = static_cast<QKorinMenuItem *>(menuItem);
    m_items.removeAll(item);
    if (item->m_parent == this)
        item->m_parent = nullptr;
    changed();
}

QPlatformMenuItem *QKorinMenu::menuItemForTag(quintptr tag) const
{
    for (QKorinMenuItem *i : m_items)
        if (i->tag() == tag)
            return i;
    return nullptr;
}

void QKorinMenu::changed()
{
    QKorinMenuBar::markDirty();
}

QKorinMenuBar::QKorinMenuBar()
{
    s_menuBars.append(this);
}

QKorinMenuBar::~QKorinMenuBar()
{
    s_menuBars.removeAll(this);
    if (m_published > 0)
        kq_menu(m_published, nullptr);
}

void QKorinMenuBar::insertMenu(QPlatformMenu *menu, QPlatformMenu *before)
{
    auto *m = static_cast<QKorinMenu *>(menu);
    m_menus.removeAll(m);
    const qsizetype at = before ? m_menus.indexOf(static_cast<QKorinMenu *>(before)) : -1;
    if (at >= 0)
        m_menus.insert(at, m);
    else
        m_menus.append(m);
    changed();
}

void QKorinMenuBar::removeMenu(QPlatformMenu *menu)
{
    m_menus.removeAll(static_cast<QKorinMenu *>(menu));
    changed();
}

QPlatformMenu *QKorinMenuBar::menuForTag(quintptr tag) const
{
    for (QKorinMenu *m : m_menus)
        if (m->tag() == tag)
            return m;
    return nullptr;
}

void QKorinMenuBar::handleReparent(QWindow *newParentWindow)
{
    if (m_published > 0 && (!newParentWindow || newParentWindow != m_window))
        kq_menu(m_published, nullptr);
    m_published = 0;
    m_window = newParentWindow;
    changed();
}

void QKorinMenuBar::changed()
{
    markDirty();
}

void QKorinMenuBar::markDirty()
{
    // (un QMenuBar change des dizaines d'entrées d'affilée : un seul envoi, au retour dans la boucle)
    if (s_dirty || !QCoreApplication::instance())
        return;
    s_dirty = true;
    QTimer::singleShot(0, [] {
        s_dirty = false;
        for (QKorinMenuBar *b : std::as_const(s_menuBars))
            b->publish();
    });
}

void QKorinMenuBar::windowCreated(QWindow *window)
{
    for (QKorinMenuBar *b : std::as_const(s_menuBars))
        if (b->m_window == window) {
            b->m_published = 0;
            b->changed();
        }
}

// « &Fichier » -> « Fichier » (&& -> &) ; ni tabulation ni retour à la ligne
static QString plainLabel(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); i++) {
        const QChar c = s.at(i);
        if (c == u'&') {
            if (i + 1 < s.size() && s.at(i + 1) == u'&') {
                out += u'&';
                i++;
            }
            continue;
        }
        if (c == u'\t' || c == u'\n' || c == u'\r')
            out += u' ';
        else
            out += c;
    }
    // (le texte après une tabulation de Qt est un raccourci écrit à la main : déjà remplacé par un espace)
    return out.trimmed();
}

// un raccourci comme la barre de Korin les écrit (« Ctrl+Maj+S », docs/KITS.md, ui_menu_key)
static QString korinShortcut(const QKeySequence &seq)
{
    if (seq.isEmpty())
        return QString();
    const QKeyCombination kc = seq[0];
    const Qt::KeyboardModifiers m = kc.keyboardModifiers();
    QString s;
    if (m & (Qt::ControlModifier | Qt::MetaModifier))
        s += QStringLiteral("Ctrl+");
    if (m & Qt::ShiftModifier)
        s += QStringLiteral("Maj+");
    if (m & Qt::AltModifier)
        s += QStringLiteral("Alt+");
    const Qt::Key k = kc.key();
    switch (k) {
    case Qt::Key_Space: s += QStringLiteral("Espace"); break;
    case Qt::Key_Return: case Qt::Key_Enter: s += QStringLiteral("Entrée"); break;
    case Qt::Key_Escape: s += QStringLiteral("Échap"); break;
    case Qt::Key_Backspace: s += QStringLiteral("Retour arrière"); break;
    case Qt::Key_Delete: s += QStringLiteral("Suppr"); break;
    case Qt::Key_Tab: s += QStringLiteral("Tab"); break;
    case Qt::Key_Left: s += QStringLiteral("←"); break;
    case Qt::Key_Right: s += QStringLiteral("→"); break;
    case Qt::Key_Up: s += QStringLiteral("↑"); break;
    case Qt::Key_Down: s += QStringLiteral("↓"); break;
    case Qt::Key_PageUp: s += QStringLiteral("Pg préc"); break;
    case Qt::Key_PageDown: s += QStringLiteral("Pg suiv"); break;
    case Qt::Key_Home: s += QStringLiteral("Début"); break;
    case Qt::Key_End: s += QStringLiteral("Fin"); break;
    default:
        if (k >= Qt::Key_F1 && k <= Qt::Key_F35)
            s += QStringLiteral("F%1").arg(int(k - Qt::Key_F1) + 1);
        else
            s += QKeySequence(k).toString(QKeySequence::PortableText);
        break;
    }
    return s;
}

namespace {
struct MenuText {
    QByteArray text;
    QList<QPointer<QPlatformMenuItem>> *ids;

    long item(QKorinMenuItem *i, const QString &prefix)
    {
        const long id = ids->size();
        ids->append(i);
        i->m_id = id;
        QByteArray line = "I" + QByteArray::number(qint64(id));
        bool enabled = i->m_enabled;
        for (QKorinMenu *m = i->m_parent; m; ) {	// (un menu grisé grise ses entrées)
            if (!m->m_enabled)
                enabled = false;
            break;
        }
        if (!enabled)
            line += 'd';
        if (i->m_checkable && i->m_checked)
            line += 'c';
        // (« Libellé<tab>Ctrl+Shift+S » : un raccourci écrit dans le texte, comme ceux de Qt.labs.platform et de
        // MuseScore, est montré à droite, à la manière de Korin)
        QString label = i->m_text, shortcut = korinShortcut(i->m_shortcut);
        const qsizetype tab = label.indexOf(u'\t');
        if (tab >= 0) {
            if (shortcut.isEmpty())
                shortcut = korinShortcut(QKeySequence(label.mid(tab + 1).trimmed(), QKeySequence::PortableText));
            label.truncate(tab);
        }
        line += '\t' + (prefix + plainLabel(label)).toUtf8() + '\t' + shortcut.toUtf8() + '\n';
        text += line;
        return id;
    }

    // les entrées d'un menu ; depth 1 : un menu de la barre, 2 : un sous-menu ; au-delà, aplati dans le sous-menu,
    // les libellés préfixés de leur chemin (« Exporter › PDF »). Un séparateur seulement entre deux entrées.
    void children(QKorinMenu *menu, int depth, const QString &prefix)
    {
        bool pendingSep = false, any = false;
        for (QKorinMenuItem *i : std::as_const(menu->m_items)) {
            if (!i->m_visible)
                continue;
            if (i->m_separator) {
                pendingSep = any;
                continue;
            }
            if (i->m_menu && !i->m_menu->m_visible)
                continue;
            if (pendingSep && depth <= 2) {
                text += "-\n";
                pendingSep = false;
            }
            if (i->m_menu) {
                const QString title = plainLabel(i->m_text.isEmpty() ? i->m_menu->m_text : i->m_text);
                if (depth == 1) {
                    text += "S" + title.toUtf8() + '\n';
                    children(i->m_menu, 2, QString());
                    text += "E\n";
                } else {
                    children(i->m_menu, depth + 1, prefix + title + QStringLiteral(" › "));
                }
            } else {
                item(i, prefix);
            }
            any = true;
        }
    }
};
}

void QKorinMenuBar::publish()
{
    QKorinWindow *kw = m_window && m_window->handle() ? static_cast<QKorinWindow *>(m_window->handle()) : nullptr;
    const long win = kw ? kw->handle() : 0;
    if (win <= 0)
        return;	// (la fenêtre d'UIKit n'existe pas encore : windowCreated republie)

    m_ids.clear();
    MenuText b{ QByteArray(), &m_ids };
    for (QKorinMenu *m : std::as_const(m_menus)) {
        if (!m->m_visible)
            continue;
        const qsizetype mark = b.text.size();
        b.text += "M" + plainLabel(m->m_text).toUtf8() + '\n';
        b.children(m, 1, QString());
        if (b.text.size() > kMenuMax) {
            b.text.truncate(mark);	// (trop long : ce menu et les suivants ne sont pas publiés)
            qWarning("qt.qpa.korin : texte des menus trop long (%d octets au plus)", kMenuMax);
            break;
        }
    }
    if (win == m_published && b.text == m_buf[m_which])
        return;
    m_which ^= 1;
    m_buf[m_which] = b.text;	// (QByteArray : terminé par NUL ; l'autre tampon reste valable)
    kq_menu(win, m_buf[m_which].isEmpty() ? nullptr : m_buf[m_which].constData());
    m_published = win;
}

void QKorinMenuBar::activate(long win, long id)
{
    for (QKorinMenuBar *b : std::as_const(s_menuBars)) {
        if (b->m_published != win && s_menuBars.size() > 1)
            continue;
        if (id < 0 || id >= b->m_ids.size())
            continue;
        QPointer<QPlatformMenuItem> item = b->m_ids.at(id);
        if (!item)
            continue;
        // (hors du traitement des événements d'UIKit : l'action peut refaire les menus)
        QMetaObject::invokeMethod(item, [item] {
            if (item) {
                emit item->hovered();
                emit item->activated();
            }
        }, Qt::QueuedConnection);
        return;
    }
}

QT_END_NAMESPACE
