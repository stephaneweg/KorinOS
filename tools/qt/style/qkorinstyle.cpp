// QKorinStyle : voir qkorinstyle.h. Chaque contrôle reprend le dessin de son widget d'UIKit (src/kits/uikit) :
// mêmes jetons par état, mêmes rayons (ui_rbox), mêmes tailles (UK_BOX = 18, métriques de uk_mt_tab).
#include "qkorinstyle.h"

#include <QtGui/qpainter.h>
#include <QtGui/qpainterpath.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformtheme.h>
#include <QtWidgets/qstylefactory.h>
#include <QtWidgets/qstyleoption.h>
#include <QtWidgets/qabstractbutton.h>
#include <QtWidgets/qabstractitemview.h>
#include <QtWidgets/qabstractspinbox.h>
#include <QtWidgets/qcombobox.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qplaintextedit.h>
#include <QtWidgets/qscrollbar.h>
#include <QtWidgets/qslider.h>
#include <QtWidgets/qtabbar.h>
#include <QtWidgets/qtextedit.h>

QT_BEGIN_NAMESPACE

// --- jetons, métriques, nuances --------------------------------------------------------------------------------

static const unsigned *s_tokens = nullptr;	// (setTokens : les essais)
static const int *s_metrics = nullptr;
// les métriques de l'échelle « moyenne » d'UIKit (uk_mt_tab : px, petite, ligne, contrôle, barre, en-tête, menu)
static const int kDefaultMetrics[QKorinStyle::MetricCount] = { 14, 13, 30, 32, 10, 24, 26, 0 };
static constexpr int kBox = 18;			// UK_BOX : case, radio, bouton du curseur

QKorinStyle::QKorinStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{
    setObjectName(QStringLiteral("korin"));
}

void QKorinStyle::setTokens(const unsigned *tokens, const int *metrics)
{
    s_tokens = tokens;
    s_metrics = metrics;
}

QColor QKorinStyle::tone(const QColor &c, int n)
{
    n = qBound(0, n, 255);
    auto ch = [n](int v) { return n < 128 ? (v * n) >> 7 : v + (255 - v) * (n - 128) / 127; };
    return QColor(ch(c.red()), ch(c.green()), ch(c.blue()));
}

QColor QKorinStyle::mix(const QColor &a, const QColor &b, int t)
{
    if (t <= 0)
        return QColor(a.rgb());
    if (t >= 256)
        return QColor(b.rgb());
    const int s = 256 - t;
    return QColor((a.red() * s + b.red() * t) >> 8, (a.green() * s + b.green() * t) >> 8,
                  (a.blue() * s + b.blue() * t) >> 8);
}

static const unsigned *platformTokens()
{
    if (s_tokens)
        return s_tokens;
    if (QPlatformTheme *t = QGuiApplicationPrivate::platformTheme()) {
        const QVariant v = t->themeHint(QPlatformTheme::ThemeHint(QKorinStyle::KorinTokensHint));
        if (v.isValid())
            return reinterpret_cast<const unsigned *>(v.value<quintptr>());
    }
    return nullptr;
}

QKorinStyle::Tokens QKorinStyle::tokens(const QPalette &pal)
{
    Tokens k;
    if (const unsigned *t = platformTokens()) {
        for (int i = 0; i < TokenCount; i++)
            k.c[i] = QColor::fromRgb(t[i] & 0xFFFFFF);
        k.dark = t[Dark] != 0;	// (TJ_DARK : 0 Givre, 1 Onyx Noir)
        return k;
    }
    // hors de Korin : ce que le thème de korin met dans la palette (qkorintheme.cpp), le reste en nuances
    k.c[Face] = pal.color(QPalette::Window);
    k.c[Surface] = pal.color(QPalette::Base);
    k.c[Sunken] = pal.color(QPalette::AlternateBase);
    k.c[Raised] = pal.color(QPalette::Button);
    k.c[RaisedLow] = pal.color(QPalette::Midlight);
    k.c[Line] = pal.color(QPalette::Mid);
    k.c[LineStrong] = pal.color(QPalette::Dark);
    k.c[Text1] = pal.color(QPalette::Text);
    k.c[Text3] = pal.color(QPalette::PlaceholderText);
    k.c[Text2] = mix(k.c[Text1], k.c[Text3], 128);
    k.c[Accent] = pal.color(QPalette::Highlight);
    k.c[AccentInk] = pal.color(QPalette::HighlightedText);
    k.c[Danger] = pal.color(QPalette::BrightText);
    k.c[Outline] = pal.color(QPalette::Shadow);
    k.dark = k.c[Face].lightness() < 128;
    k.c[AccentLight] = k.dark ? mix(k.c[Face], k.c[Accent], 64) : mix(k.c[Surface], k.c[Accent], 30);
    k.c[AccentDark] = tone(k.c[Accent], 104);
    k.c[AccentVivid] = k.c[Accent];
    k.c[Success] = k.c[Warning] = k.c[Accent];
    k.c[Bar] = k.c[Dock] = k.c[TitleInactive] = k.c[Face];
    k.c[TitleActive] = k.c[Sunken];
    k.c[Dark] = QColor(k.dark ? 1 : 0, 0, 0);
    k.c[Menu] = k.dark ? k.c[Raised] : k.c[Surface];
    k.c[Hover] = k.c[Sunken];
    return k;
}

int QKorinStyle::metric(Metric m)
{
    if (s_metrics)
        return s_metrics[m];
    static int cache[MetricCount];
    if (QPlatformTheme *t = QGuiApplicationPrivate::platformTheme()) {
        const QVariant v = t->themeHint(QPlatformTheme::ThemeHint(KorinMetricsHint));
        const QVariantList l = v.toList();
        if (l.size() >= MetricCount) {
            for (int i = 0; i < MetricCount; i++)
                cache[i] = l.at(i).toInt();
            return cache[m];
        }
    }
    return kDefaultMetrics[m];
}

// une boîte arrondie pleine (ui_rbox)
static void box(QPainter *p, const QRectF &r, qreal radius, const QColor &c)
{
    if (r.width() <= 0 || r.height() <= 0)
        return;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    const qreal rad = qMin(radius, qMin(r.width(), r.height()) / 2);
    p->drawRoundedRect(r, rad, rad);
    p->restore();
}

// un contour (1 px, couleur edge) et un intérieur (fill), comme les deux ui_rbox d'UIKit
static void framedBox(QPainter *p, const QRect &r, int radius, const QColor &edge, const QColor &fill)
{
    box(p, r, radius, edge);
    box(p, r.adjusted(1, 1, -1, -1), radius - 1, fill);
}

// l'anneau de focus (uk_ring) : 3 px autour, mix(FACE, ACCENT, 110), rayon 11, et le jour de FACE dedans ; r est
// le widget (l'anneau va dans la marge de 3 px que le style lui laisse)
static void focusRing(QPainter *p, const QRect &r, const QKorinStyle::Tokens &k)
{
    if (r.height() >= 80)
        return;	// (pas autour d'une grande zone)
    box(p, r.adjusted(-3, -3, 3, 3), 11, QKorinStyle::mix(k[QKorinStyle::Face], k[QKorinStyle::Accent], 110));
    box(p, r.adjusted(-1, -1, 1, 1), 9, k[QKorinStyle::Face]);
}

static QFont sizedFont(const QFont &base, int px, bool bold = false)
{
    QFont f = base;
    f.setPixelSize(px);
    if (bold)
        f.setBold(true);
    return f;
}

// un glyphe centré dans r (« ▾ », « ✓ », « − », « + », « › »)
static void glyph(QPainter *p, const QRect &r, const QString &g, const QColor &c, int px, bool bold, const QFont &base)
{
    p->save();
    p->setFont(sizedFont(base, px, bold));
    p->setPen(c);
    p->drawText(r, Qt::AlignCenter, g);
    p->restore();
}

// (UIKit montre l'anneau dès qu'un contrôle a le focus, à la souris comme au clavier : uk_ring)
static bool hasFocus(const QStyleOption *opt)
{
    return (opt->state & QStyle::State_HasFocus) && (opt->state & QStyle::State_Enabled);
}

static bool isTextEdit(const QWidget *w)
{
    return qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w);
}

// --- polish ----------------------------------------------------------------------------------------------------

void QKorinStyle::polish(QWidget *w)
{
    QProxyStyle::polish(w);
    if (qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w) || qobject_cast<QScrollBar *>(w)
        || qobject_cast<QSlider *>(w) || qobject_cast<QTabBar *>(w) || qobject_cast<QAbstractSpinBox *>(w)
        || qobject_cast<QAbstractItemView *>(w))
        w->setAttribute(Qt::WA_Hover);
    if (auto *v = qobject_cast<QAbstractItemView *>(w))
        v->viewport()->setAttribute(Qt::WA_Hover);
}

void QKorinStyle::unpolish(QWidget *w)
{
    QProxyStyle::unpolish(w);
}

// --- primitives ------------------------------------------------------------------------------------------------

void QKorinStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p, const QWidget *w) const
{
    const Tokens k = tokens(opt->palette);
    const bool enabled = opt->state & State_Enabled;
    const bool hover = (opt->state & State_MouseOver) && enabled;
    switch (pe) {
    case PE_PanelButtonCommand:
    case PE_PanelButtonBevel: {
        // uk_bt_draw : standard (contour LIGNE_FORTE, ELEVE ; survol mêlé de CREUX ; appui CREUX), principal (ACCENT ;
        // survol, appui ACCENT_FONCE), fantôme (rien ; survol CREUX ; appui ton(CREUX, 116)) ; rayon 8
        const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
        const QRect r = opt->rect.adjusted(3, 3, -3, -3);	// (la marge de l'anneau)
        const bool down = opt->state & (State_Sunken | State_On);
        if (b && (b->features & QStyleOptionButton::Flat)) {
            if (!enabled)
                break;
            if (down)
                box(p, r, 8, tone(k[Sunken], 116));
            else if (hover)
                box(p, r, 8, k[Sunken]);
        } else if (b && (b->features & QStyleOptionButton::DefaultButton)) {
            box(p, r, 8, !enabled ? k[Sunken] : (down || hover) ? k[AccentDark] : k[Accent]);
        } else {
            QColor fill = !enabled ? k[Face] : down ? k[Sunken] : hover ? mix(k[Raised], k[Sunken], 160) : k[Raised];
            framedBox(p, r, 8, enabled ? k[LineStrong] : k[Line], fill);
        }
        return;	// (l'anneau de focus : CE_PushButton, avant le bouton)
    }
    case PE_FrameFocusRect:
        return;	// (UIKit : l'anneau des contrôles, pas de pointillés)
    case PE_PanelLineEdit:
    case PE_FrameLineEdit: {
        // uk_tb_draw : contour LIGNE_FORTE (focus ACCENT), intérieur SURFACE (grisé FACE), rayon 8 ; le cadre d'un
        // QSpinBox : uk_nm_draw, rayon 6 ; dans une liste modifiable ou un délégué : rien
        if (w && (qobject_cast<const QComboBox *>(w->parentWidget()) || qobject_cast<const QAbstractSpinBox *>(w->parentWidget())))
            return;
        const auto *f = qstyleoption_cast<const QStyleOptionFrame *>(opt);
        if (f && f->lineWidth <= 0) {
            p->fillRect(opt->rect, enabled ? k[Surface] : k[Face]);
            return;
        }
        const bool focus = opt->state & State_HasFocus;
        framedBox(p, opt->rect.adjusted(3, 3, -3, -3), 8, focus ? k[Accent] : k[LineStrong],
                  enabled ? k[Surface] : k[Face]);
        return;
    }
    case PE_Frame: {
        // texte multiligne (uk_ta_draw) : un champ, rayon 6 ; une liste (uk_ls_draw) : SURFACE, LIGNE dessus et dessous
        if (isTextEdit(w)) {
            const bool focus = opt->state & State_HasFocus;
            box(p, opt->rect, 6, focus ? k[Accent] : k[LineStrong]);
            box(p, opt->rect.adjusted(1, 1, -1, -1), 5, enabled ? k[Surface] : k[Face]);
            return;
        }
        if (qobject_cast<const QAbstractItemView *>(w)) {
            p->fillRect(opt->rect, k[Surface]);
            p->fillRect(QRect(opt->rect.x(), opt->rect.y(), opt->rect.width(), 1), k[Line]);
            p->fillRect(QRect(opt->rect.x(), opt->rect.bottom(), opt->rect.width(), 1), k[Line]);
            return;
        }
        // un panneau (uk_pn_draw, style 1) : contour LIGNE, rayon 6
        if (opt->state & (State_Sunken | State_Raised)) {
            box(p, opt->rect, 6, k[Line]);
            box(p, opt->rect.adjusted(1, 1, -1, -1), 5, k[Surface]);
        }
        return;
    }
    case PE_FrameGroupBox: {
        // uk_pn_draw (UT_GROUP) : contour LIGNE, rayon 6, le corps FACE
        box(p, opt->rect, 6, k[Line]);
        box(p, opt->rect.adjusted(1, 1, -1, -1), 5, k[Face]);
        return;
    }
    case PE_IndicatorCheckBox:
    case PE_IndicatorRadioButton: {
        // uk_tg_draw : choisi ACCENT (grisé mix(FACE, ACCENT, 90)) et « ✓ » gras 13 / point de 6 en ACCENT_INK ;
        // sinon contour LIGNE_FORTE, intérieur SURFACE (grisé FACE, survol CREUX) ; case rayon 4, radio disque
        const QRect r(opt->rect.x() + (opt->rect.width() - kBox) / 2, opt->rect.y() + (opt->rect.height() - kBox) / 2,
                      kBox, kBox);
        const bool radio = pe == PE_IndicatorRadioButton;
        const int rad = radio ? kBox / 2 : 4;
        const bool on = opt->state & (State_On | State_NoChange);
        if (hasFocus(opt))
            focusRing(p, r, k);
        if (on) {
            box(p, r, rad, enabled ? k[Accent] : mix(k[Face], k[Accent], 90));
            if (radio)
                box(p, QRect(r.x() + 6, r.y() + 6, 6, 6), 3, k[AccentInk]);
            else if (opt->state & State_NoChange)
                box(p, QRect(r.x() + 4, r.y() + kBox / 2 - 1, kBox - 8, 2), 1, k[AccentInk]);
            else
                glyph(p, r, QStringLiteral("✓"), k[AccentInk], 13, true, w ? w->font() : QFont());
        } else {
            const QColor fill = !enabled ? k[Face] : hover ? k[Sunken] : k[Surface];
            framedBox(p, r, rad, k[LineStrong], fill);
        }
        return;
    }
    case PE_PanelMenu:
    case PE_FrameMenu: {
        // ui_popup : le corps MENU, rayon 8 (Elegant compose sans transparence : un contour LIGNE à la place de
        // l'ombre)
        p->fillRect(opt->rect, k[Menu]);
        p->save();
        p->setPen(k[Line]);
        p->drawRect(opt->rect.adjusted(0, 0, -1, -1));
        p->restore();
        return;
    }
    case PE_PanelTipLabel:
        // l'infobulle d'UIKit : fond TEXTE1, texte SURFACE
        p->fillRect(opt->rect, k[Text1]);
        return;
    case PE_PanelItemViewItem:
    case PE_PanelItemViewRow: {
        // uk_ls_draw : choisi ACCENT (focus) ou ACCENT_CLAIR ; survol CREUX (Givre), FACE (Noir) ; rayon 5 ;
        // lignes alternées mix(SURFACE, CREUX, 128). Une ligne de plusieurs colonnes est arrondie à ses deux bouts
        // seulement ; dans un arbre, la zone des branches (PE_PanelItemViewRow) en est le bout gauche.
        const auto *v = qstyleoption_cast<const QStyleOptionViewItem *>(opt);
        const bool selected = opt->state & State_Selected;
        if (pe == PE_PanelItemViewRow && !(selected && v && v->showDecorationSelected)) {
            if (v && (v->features & QStyleOptionViewItem::Alternate))
                p->fillRect(opt->rect, mix(k[Surface], k[Sunken], 128));
            return;
        }
        QColor c;
        if (selected)
            c = (opt->state & State_Active) && (!w || w->hasFocus()) ? k[Accent] : k[AccentLight];
        else if (hover)
            c = k.dark ? k[Face] : k[Sunken];
        else
            return;
        QRect r = opt->rect;
        bool first = true, last = true;
        if (pe == PE_PanelItemViewRow) {
            last = false;	// (la zone des branches : les cellules suivent)
        } else if (v) {
            const auto pos = v->viewItemPosition;
            first = pos == QStyleOptionViewItem::Beginning || pos == QStyleOptionViewItem::OnlyOne
                    || pos == QStyleOptionViewItem::Invalid;
            last = pos == QStyleOptionViewItem::End || pos == QStyleOptionViewItem::OnlyOne
                   || pos == QStyleOptionViewItem::Invalid;
            // (un arbre décoré : la première cellule suit la zone des branches, déjà arrondie)
            if (first && pos != QStyleOptionViewItem::Invalid && v->showDecorationSelected && r.left() > 0
                && w && w->inherits("QTreeView"))
                first = false;
        }
        if (first)
            r.setLeft(r.left() + 2);
        if (last)
            r.setRight(r.right() - 2);
        box(p, r, 5, c);
        if (!first)
            p->fillRect(QRect(r.left(), r.top(), 6, r.height()), c);
        if (!last)
            p->fillRect(QRect(r.right() - 5, r.top(), 6, r.height()), c);
        return;
    }
    case PE_IndicatorArrowDown:
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowLeft:
    case PE_IndicatorArrowRight: {
        static const char16_t *glyphs[] = { u"▴", u"▾", u"‹", u"›" };
        const int i = pe == PE_IndicatorArrowUp ? 0 : pe == PE_IndicatorArrowDown ? 1 : pe == PE_IndicatorArrowLeft ? 2 : 3;
        glyph(p, opt->rect, QString::fromUtf16(glyphs[i]), enabled ? k[Text2] : k[Text3], 13, false,
              w ? w->font() : QFont());
        return;
    }
    case PE_FrameTabWidget: {
        // le corps des onglets (uk_tabs : un panneau) : FACE, contour LIGNE rayon 6
        box(p, opt->rect, 6, k[Line]);
        box(p, opt->rect.adjusted(1, 1, -1, -1), 5, k[Face]);
        return;
    }
    case PE_FrameTabBarBase:
        return;
    case PE_IndicatorProgressChunk:
        return;
    default:
        break;
    }
    QProxyStyle::drawPrimitive(pe, opt, p, w);
}

// --- éléments --------------------------------------------------------------------------------------------------

void QKorinStyle::drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p, const QWidget *w) const
{
    const Tokens k = tokens(opt->palette);
    const bool enabled = opt->state & State_Enabled;
    switch (ce) {
    case CE_PushButton: {
        const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
        if (!b)
            break;
        if (hasFocus(opt) && !(b->features & QStyleOptionButton::Flat))
            focusRing(p, opt->rect.adjusted(3, 3, -3, -3), k);
        proxy()->drawControl(CE_PushButtonBevel, opt, p, w);
        QStyleOptionButton sub = *b;
        sub.rect = proxy()->subElementRect(SE_PushButtonContents, opt, w);
        // le texte : TEXTE1 ; principal ACCENT_INK ; grisé TEXTE3
        QColor ink = !enabled ? k[Text3] : (b->features & QStyleOptionButton::DefaultButton) ? k[AccentInk] : k[Text1];
        sub.palette.setColor(QPalette::ButtonText, ink);
        sub.palette.setColor(QPalette::Disabled, QPalette::ButtonText, ink);
        proxy()->drawControl(CE_PushButtonLabel, &sub, p, w);
        return;
    }
    case CE_PushButtonBevel:
        proxy()->drawPrimitive(PE_PanelButtonCommand, opt, p, w);
        return;
    case CE_CheckBox:
    case CE_RadioButton: {
        // le texte à UK_BOX + 8 (tg_text) : TEXTE1, grisé TEXTE3
        if (const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
            QStyleOptionButton sub = *b;
            sub.palette.setColor(QPalette::WindowText, enabled ? k[Text1] : k[Text3]);
            sub.palette.setColor(QPalette::ButtonText, enabled ? k[Text1] : k[Text3]);
            QProxyStyle::drawControl(ce, &sub, p, w);
            return;
        }
        break;
    }
    case CE_ProgressBar:
    case CE_ProgressBarGroove:
    case CE_ProgressBarContents: {
        // uk_pg_draw : rail CREUX contour LIGNE, demi-disques ; la part faite ACCENT (au moins un disque)
        const auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt);
        if (!pb)
            break;
        if (ce == CE_ProgressBarContents)
            return;	// (CE_ProgressBar dessine tout)
        const bool horiz = !(pb->state & QStyle::State_Horizontal) ? false : true;
        QRect r = opt->rect;
        const int th = qMin(horiz ? r.height() : r.width(), 10);
        if (horiz)
            r = QRect(r.x(), r.y() + (r.height() - th) / 2, r.width(), th);
        else
            r = QRect(r.x() + (r.width() - th) / 2, r.y(), th, r.height());
        framedBox(p, r, th / 2, k[Line], k[Sunken]);
        if (ce == CE_ProgressBarGroove)
            return;
        const qint64 span = qint64(pb->maximum) - pb->minimum;
        if (span <= 0) {
            // indéterminée : un tiers du rail, au milieu (UIKit n'en a pas)
            const QRect part = horiz ? QRect(r.x() + r.width() / 3, r.y(), r.width() / 3, th)
                                     : QRect(r.x(), r.y() + r.height() / 3, th, r.height() / 3);
            box(p, part, th / 2, k[Accent]);
            return;
        }
        const int len = horiz ? r.width() : r.height();
        int done = int((qint64(pb->progress) - pb->minimum) * len / span);
        if (done <= 0)
            return;
        done = qMax(done, th);
        const QRect part = horiz ? QRect(pb->invertedAppearance ? r.right() - done + 1 : r.x(), r.y(), done, th)
                                 : QRect(r.x(), r.bottom() - done + 1, th, done);
        box(p, part, th / 2, enabled ? k[Accent] : mix(k[Face], k[Accent], 90));
        return;
    }
    case CE_ProgressBarLabel:
        return;	// (UIKit : pas de texte sur la barre)
    case CE_MenuItem: {
        // ui_popup : ligne de UMT_MENU ; survol ACCENT rayon 5 (5 px des bords), texte ACCENT_INK ; grisée TEXTE3 ;
        // « ✓ » dans la marge ; texte à 14 ; raccourci à droite (TEXTE3) ; séparateur LIGNE, 10 px des bords
        const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
        if (!mi)
            break;
        p->fillRect(opt->rect, k[Menu]);
        if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
            p->fillRect(QRect(opt->rect.x() + 10, opt->rect.center().y(), opt->rect.width() - 20, 1), k[Line]);
            return;
        }
        const bool selected = (opt->state & State_Selected) && enabled;
        QColor ink = !enabled ? k[Text3] : k[Text1];
        if (selected) {
            box(p, opt->rect.adjusted(5, 0, -5, 0), 5, k[Accent]);
            ink = k[AccentInk];
        }
        const QFont font = sizedFont(mi->font, metric(Px));
        p->save();
        p->setFont(font);
        if (mi->checkType != QStyleOptionMenuItem::NotCheckable && mi->checked) {
            p->setFont(sizedFont(mi->font, 12));
            p->setPen(ink);
            p->drawText(QRect(opt->rect.x() + 1, opt->rect.y(), 14, opt->rect.height()), Qt::AlignCenter,
                        mi->checkType == QStyleOptionMenuItem::Exclusive ? QStringLiteral("•") : QStringLiteral("✓"));
            p->setFont(font);
        }
        int x = opt->rect.x() + 14;
        if (!mi->icon.isNull()) {
            const int is = 16;
            mi->icon.paint(p, QRect(x, opt->rect.y() + (opt->rect.height() - is) / 2, is, is),
                           Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
            x += is + 8;
        } else if (mi->maxIconWidth > 0) {
            x += 24;
        }
        QString text = mi->text, shortcut;
        const qsizetype tab = text.indexOf(u'\t');
        if (tab >= 0) {
            shortcut = text.mid(tab + 1);
            text.truncate(tab);
        }
        const int right = opt->rect.right() - 14 - (mi->menuItemType == QStyleOptionMenuItem::SubMenu ? 12 : 0);
        p->setPen(ink);
        p->drawText(QRect(x, opt->rect.y(), right - x, opt->rect.height()),
                    Qt::AlignVCenter | Qt::AlignLeft | Qt::TextShowMnemonic | Qt::TextHideMnemonic, text);
        if (!shortcut.isEmpty()) {
            p->setPen(selected ? k[AccentInk] : k[Text3]);
            p->drawText(QRect(x, opt->rect.y(), right - x, opt->rect.height()), Qt::AlignVCenter | Qt::AlignRight,
                        shortcut);
        }
        if (mi->menuItemType == QStyleOptionMenuItem::SubMenu)
            glyph(p, QRect(opt->rect.right() - 22, opt->rect.y(), 16, opt->rect.height()), QStringLiteral("›"),
                  selected ? k[AccentInk] : k[Text2], 14, false, mi->font);
        p->restore();
        return;
    }
    case CE_MenuEmptyArea:
        p->fillRect(opt->rect, k[Menu]);
        return;
    case CE_HeaderSection: {
        // ls_header : fond de la liste (SURFACE), trait LIGNE dessous, séparateur LIGNE à droite (6 px du haut et du bas)
        const QRect r = opt->rect;
        p->fillRect(r, k[Surface]);
        p->fillRect(QRect(r.x(), r.bottom(), r.width(), 1), k[Line]);
        p->fillRect(QRect(r.right(), r.y() + 6, 1, qMax(r.height() - 12, 1)), k[Line]);
        return;
    }
    case CE_HeaderLabel: {
        // le titre de colonne : TEXTE3, à 10 px
        if (const auto *h = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
            QStyleOptionHeader sub = *h;
            sub.rect = h->rect.adjusted(10, 0, -10, 0);
            sub.palette.setColor(QPalette::ButtonText, k[Text3]);
            sub.palette.setColor(QPalette::WindowText, k[Text3]);
            QProxyStyle::drawControl(ce, &sub, p, w);
            return;
        }
        break;
    }
    case CE_HeaderEmptyArea:
        p->fillRect(opt->rect, k[Surface]);
        p->fillRect(QRect(opt->rect.x(), opt->rect.bottom(), opt->rect.width(), 1), k[Line]);
        return;
    case CE_ItemViewItem: {
        // le texte d'un élément choisi : ACCENT_INK sur ACCENT, TEXTE1 sur ACCENT_CLAIR
        if (const auto *v = qstyleoption_cast<const QStyleOptionViewItem *>(opt)) {
            QStyleOptionViewItem sub = *v;
            const bool strong = (opt->state & State_Active) && (!w || w->hasFocus());
            sub.palette.setColor(QPalette::HighlightedText, strong ? k[AccentInk] : k[Text1]);
            sub.palette.setColor(QPalette::Text, enabled ? k[Text1] : k[Text3]);
            QProxyStyle::drawControl(ce, &sub, p, w);
            return;
        }
        break;
    }
    case CE_TabBarTabShape: {
        // uk_tabs (seg_paint) : un contrôle segmenté — rail CREUX (Noir : SURFACE) rayon 8 sous tous les onglets,
        // l'actif SURFACE (Noir : ELEVE) rayon 6 sur une ombre LIGNE d'un pixel ;
        // mode document (uk_ts_draw) : fond CREUX, trait LIGNE en bas, l'actif SURFACE arrondi en haut sur le trait
        const auto *t = qstyleoption_cast<const QStyleOptionTab *>(opt);
        if (!t)
            break;
        const bool selected = opt->state & State_Selected;
        const QRect r = opt->rect;
        if (t->documentMode) {
            p->fillRect(r, k[Sunken]);
            p->fillRect(QRect(r.x(), r.bottom(), r.width(), 1), k[Line]);
            if (selected) {
                box(p, QRect(r.x(), r.y() + 4, r.width(), r.height() - 3), 6, k[Line]);
                box(p, QRect(r.x() + 1, r.y() + 5, r.width() - 2, r.height() - 4), 5, k[Surface]);
            } else if (t->position != QStyleOptionTab::End && t->position != QStyleOptionTab::OnlyOneTab) {
                p->fillRect(QRect(r.right(), r.y() + 10, 1, qMax(r.height() - 20, 1)), k[Line]);
            }
            return;
        }
        const QColor rail = k.dark ? k[Surface] : k[Sunken];
        const bool first = t->position == QStyleOptionTab::Beginning || t->position == QStyleOptionTab::OnlyOneTab;
        const bool last = t->position == QStyleOptionTab::End || t->position == QStyleOptionTab::OnlyOneTab;
        box(p, r, 8, rail);
        if (!first)
            p->fillRect(QRect(r.left(), r.top(), 9, r.height()), rail);
        if (!last)
            p->fillRect(QRect(r.right() - 8, r.top(), 9, r.height()), rail);
        if (selected) {
            const QRect s = r.adjusted(first ? 2 : 0, 2, last ? -2 : 0, -1);
            box(p, s, 6, k[Line]);
            box(p, s.adjusted(0, 0, 0, -1), 6, k.dark ? k[Raised] : k[Surface]);
        }
        return;
    }
    case CE_TabBarTabLabel: {
        // le texte : gras (segmenté), TEXTE1 actif ou survolé, sinon TEXTE2
        if (const auto *t = qstyleoption_cast<const QStyleOptionTab *>(opt)) {
            QStyleOptionTab sub = *t;
            const bool strong = (opt->state & (State_Selected | State_MouseOver));
            const QColor ink = !enabled ? k[Text3] : strong ? k[Text1] : k[Text2];
            sub.palette.setColor(QPalette::WindowText, ink);
            sub.palette.setColor(QPalette::ButtonText, ink);
            p->save();
            if (!t->documentMode) {
                QFont f = p->font();
                f.setBold(true);
                p->setFont(f);
            }
            QProxyStyle::drawControl(ce, &sub, p, w);
            p->restore();
            return;
        }
        break;
    }
    case CE_ScrollBarAddLine:
    case CE_ScrollBarSubLine:
        return;	// (pas de flèches : la barre d'UIKit n'a que son pouce)
    default:
        break;
    }
    QProxyStyle::drawControl(ce, opt, p, w);
}

// --- contrôles complexes ---------------------------------------------------------------------------------------

void QKorinStyle::drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p,
                                     const QWidget *w) const
{
    const Tokens k = tokens(opt->palette);
    const bool enabled = opt->state & State_Enabled;
    const bool hover = (opt->state & State_MouseOver) && enabled;
    switch (cc) {
    case CC_ScrollBar: {
        // uk_sb_draw : survol, le rail en CREUX rayon 4 ; le pouce ton(LIGNE_FORTE, 128 ; survol 100 ; appui 84),
        // 1 px des bords, demi-disques
        const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt);
        if (!sb)
            break;
        p->fillRect(opt->rect, w && w->parentWidget() ? k[Surface] : k[Face]);
        if (hover)
            box(p, opt->rect, 4, k[Sunken]);
        const QRect s = proxy()->subControlRect(cc, opt, SC_ScrollBarSlider, w);
        if (s.isEmpty() || sb->minimum == sb->maximum)
            return;
        int level = 128;
        if (hover)
            level = 100;
        if ((opt->state & State_Sunken) && (opt->activeSubControls & SC_ScrollBarSlider))
            level = 84;
        const bool horiz = sb->orientation == Qt::Horizontal;
        const QRect t = horiz ? s.adjusted(0, 1, 0, -1) : s.adjusted(1, 0, -1, 0);
        box(p, t, qMin(t.width(), t.height()) / 2.0, tone(k[LineStrong], level));
        return;
    }
    case CC_Slider: {
        // uk_sl_draw : rail LIGNE 4 px rayon 2 depuis le milieu du bouton, la part faite ACCENT (grisé
        // mix(FACE, ACCENT, 90)) ; bouton : disque de 18, contour LIGNE_FORTE (appui ACCENT), intérieur clair
        const auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt);
        if (!sl)
            break;
        const QRect groove = proxy()->subControlRect(cc, opt, SC_SliderGroove, w);
        const QRect handle = proxy()->subControlRect(cc, opt, SC_SliderHandle, w);
        const QColor acc = enabled ? k[Accent] : mix(k[Face], k[Accent], 90);
        if (sl->orientation == Qt::Horizontal) {
            const int cy = groove.center().y();
            const QRect rail(groove.x() + kBox / 2, cy - 1, groove.width() - kBox, 4);
            box(p, rail, 2, k[Line]);
            box(p, QRect(rail.x(), rail.y(), handle.center().x() - rail.x(), 4), 2, acc);
        } else {
            const int cx = groove.center().x();
            const QRect rail(cx - 1, groove.y() + kBox / 2, 4, groove.height() - kBox);
            box(p, rail, 2, k[Line]);
            box(p, QRect(rail.x(), handle.center().y(), 4, rail.bottom() - handle.center().y()), 2, acc);
        }
        const bool pressed = (opt->state & State_Sunken) && (opt->activeSubControls & SC_SliderHandle);
        if (hasFocus(opt))
            focusRing(p, handle, k);
        // (UIKit : l'intérieur du bouton est blanc ; ici l'encre de l'accent, le même blanc tiré des jetons)
        framedBox(p, handle, kBox / 2, pressed ? k[Accent] : k[LineStrong], k[AccentInk]);
        if (sl->subControls & SC_SliderTickmarks)
            QProxyStyle::drawComplexControl(cc, opt, p, w);	// (rare : les graduations de Fusion par-dessus)
        return;
    }
    case CC_ComboBox: {
        // uk_dd_draw : contour LIGNE_FORTE, intérieur ELEVE (survol CREUX, grisé FACE), rayon 8, « ▾ » TEXTE2 à
        // 22 px du bord ; modifiable (UT_COMBO) : le champ, et un bouton carré à droite (contour LIGNE_FORTE, survol
        // ACCENT ; ELEVE)
        const auto *cb = qstyleoption_cast<const QStyleOptionComboBox *>(opt);
        if (!cb)
            break;
        const QRect r = opt->rect.adjusted(3, 3, -3, -3);
        if (hasFocus(opt) && !cb->editable)
            focusRing(p, r, k);
        const QFont font = w ? w->font() : QFont();
        if (!cb->editable) {
            const QColor fill = !enabled ? k[Face] : hover ? k[Sunken] : k[Raised];
            framedBox(p, r, 8, k[LineStrong], fill);
            glyph(p, QRect(r.right() - 22, r.y(), 16, r.height()), QStringLiteral("▾"), k[Text2], 13, false, font);
        } else {
            const bool focus = opt->state & State_HasFocus;
            framedBox(p, r, 8, focus ? k[Accent] : k[LineStrong], enabled ? k[Surface] : k[Face]);
            const QRect b(r.right() - r.height() + 1, r.y(), r.height(), r.height());
            const bool overArrow = hover && (opt->activeSubControls & SC_ComboBoxArrow);
            framedBox(p, b, 8, overArrow ? k[Accent] : k[LineStrong], k[Raised]);
            glyph(p, b, QStringLiteral("▾"), k[Text2], 13, false, font);
        }
        return;
    }
    case CC_SpinBox: {
        // uk_nm_draw : cadre d'un champ rayon 6 (focus ACCENT), SURFACE ; deux boutons à droite « − » « + »
        // séparés par des traits LIGNE, glyphes gras 15 TEXTE2, appui CREUX
        const auto *sp = qstyleoption_cast<const QStyleOptionSpinBox *>(opt);
        if (!sp)
            break;
        const QRect r = opt->rect.adjusted(3, 3, -3, -3);	// (la marge des autres contrôles)
        const bool focus = opt->state & State_HasFocus;
        if (sp->frame)
            framedBox(p, r, 6, focus ? k[Accent] : k[LineStrong], enabled ? k[Surface] : k[Face]);
        else
            p->fillRect(r, enabled ? k[Surface] : k[Face]);
        if (sp->buttonSymbols == QAbstractSpinBox::NoButtons)
            return;
        const QRect down = proxy()->subControlRect(cc, opt, SC_SpinBoxDown, w);
        const QRect up = proxy()->subControlRect(cc, opt, SC_SpinBoxUp, w);
        p->fillRect(QRect(down.x(), r.y() + 1, 1, r.height() - 2), k[Line]);
        p->fillRect(QRect(up.x(), r.y() + 1, 1, r.height() - 2), k[Line]);
        const bool sunken = opt->state & State_Sunken;
        if (sunken && (opt->activeSubControls & SC_SpinBoxDown))
            p->fillRect(down.adjusted(1, 1, -1, -1), k[Sunken]);
        if (sunken && (opt->activeSubControls & SC_SpinBoxUp))
            box(p, up.adjusted(1, 1, -1, -1), 5, k[Sunken]);
        const QFont font = w ? w->font() : QFont();
        // (UIKit : toujours « − » et « + », quels que soient les symboles demandés)
        glyph(p, down, QStringLiteral("−"), (sp->stepEnabled & QAbstractSpinBox::StepDownEnabled) && enabled ? k[Text2] : k[Text3], 15, true, font);
        glyph(p, up, QStringLiteral("+"), (sp->stepEnabled & QAbstractSpinBox::StepUpEnabled) && enabled ? k[Text2] : k[Text3], 15, true, font);
        return;
    }
    case CC_GroupBox: {
        // uk_pn_draw (UT_GROUP) : le cadre 8 px sous le haut, le titre gras petit TEXTE2 sur le trait, à 14 px
        const auto *g = qstyleoption_cast<const QStyleOptionGroupBox *>(opt);
        if (!g)
            break;
        const QRect frame = proxy()->subControlRect(cc, opt, SC_GroupBoxFrame, w);
        if (!(g->features & QStyleOptionFrame::Flat)) {
            QStyleOptionFrame f;
            f.QStyleOption::operator=(*g);
            f.rect = frame;
            proxy()->drawPrimitive(PE_FrameGroupBox, &f, p, w);
        }
        const QRect label = proxy()->subControlRect(cc, opt, SC_GroupBoxLabel, w);
        if (!g->text.isEmpty()) {
            p->fillRect(label.adjusted(-4, 0, 4, 0), k[Face]);
            p->save();
            p->setFont(sizedFont(w ? w->font() : QFont(), metric(Small), true));
            p->setPen(enabled ? k[Text2] : k[Text3]);
            p->drawText(label, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic, g->text);
            p->restore();
        }
        if (g->subControls & SC_GroupBoxCheckBox) {
            QStyleOptionButton b;
            b.QStyleOption::operator=(*g);
            b.rect = proxy()->subControlRect(cc, opt, SC_GroupBoxCheckBox, w);
            proxy()->drawPrimitive(PE_IndicatorCheckBox, &b, p, w);
        }
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawComplexControl(cc, opt, p, w);
}

QStyle::SubControl QKorinStyle::hitTestComplexControl(ComplexControl cc, const QStyleOptionComplex *opt,
                                                      const QPoint &pt, const QWidget *w) const
{
    if (cc == CC_ScrollBar) {
        for (SubControl sc : { SC_ScrollBarSlider, SC_ScrollBarSubPage, SC_ScrollBarAddPage })
            if (proxy()->subControlRect(cc, opt, sc, w).contains(pt))
                return sc;
        return SC_None;
    }
    return QProxyStyle::hitTestComplexControl(cc, opt, pt, w);
}

QRect QKorinStyle::subControlRect(ComplexControl cc, const QStyleOptionComplex *opt, SubControl sc,
                                  const QWidget *w) const
{
    switch (cc) {
    case CC_ScrollBar: {
        // sans flèches : le rail est toute la barre ; le pouce au moins 24 px
        const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt);
        if (!sb)
            break;
        const QRect r = opt->rect;
        const bool horiz = sb->orientation == Qt::Horizontal;
        const int len = horiz ? r.width() : r.height();
        const qint64 range = qint64(sb->maximum) - sb->minimum;
        int thumb = range > 0 ? int(qint64(len) * sb->pageStep / (range + sb->pageStep)) : len;
        thumb = qBound(qMin(24, len), thumb, len);
        const int pos = range > 0 ? sliderPositionFromValue(sb->minimum, sb->maximum, sb->sliderPosition,
                                                            len - thumb, sb->upsideDown) : 0;
        auto along = [&](int a, int l) {
            return horiz ? QRect(r.x() + a, r.y(), l, r.height()) : QRect(r.x(), r.y() + a, r.width(), l);
        };
        switch (sc) {
        case SC_ScrollBarGroove: return r;
        case SC_ScrollBarSlider: return along(pos, thumb);
        case SC_ScrollBarSubPage: return along(0, pos);
        case SC_ScrollBarAddPage: return along(pos + thumb, len - pos - thumb);
        case SC_ScrollBarAddLine:
        case SC_ScrollBarSubLine:
        case SC_ScrollBarFirst:
        case SC_ScrollBarLast: return QRect();
        default: break;
        }
        break;
    }
    case CC_Slider: {
        // le bouton de UK_BOX, la course = la longueur - UK_BOX (sl_pos)
        const auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt);
        if (!sl)
            break;
        const QRect r = opt->rect;
        const bool horiz = sl->orientation == Qt::Horizontal;
        if (sc == SC_SliderGroove)
            return horiz ? QRect(r.x(), r.center().y() - kBox / 2, r.width(), kBox)
                         : QRect(r.center().x() - kBox / 2, r.y(), kBox, r.height());
        if (sc == SC_SliderHandle) {
            const int course = (horiz ? r.width() : r.height()) - kBox;
            const int pos = sliderPositionFromValue(sl->minimum, sl->maximum, sl->sliderPosition, qMax(course, 0),
                                                    horiz ? sl->upsideDown : !sl->upsideDown);
            return horiz ? QRect(r.x() + pos, r.center().y() - kBox / 2 + 1, kBox, kBox)
                         : QRect(r.center().x() - kBox / 2 + 1, r.y() + pos, kBox, kBox);
        }
        break;
    }
    case CC_ComboBox: {
        const auto *cb = qstyleoption_cast<const QStyleOptionComboBox *>(opt);
        if (!cb)
            break;
        const QRect r = opt->rect.adjusted(3, 3, -3, -3);
        switch (sc) {
        case SC_ComboBoxFrame: return r;
        case SC_ComboBoxArrow:
            return QRect(r.right() - r.height() + 1, r.y(), r.height(), r.height());
        case SC_ComboBoxEditField:
            // (le texte à 10 px ; « ▾ » ou le bouton carré à droite)
            return cb->editable ? QRect(r.x() + 9, r.y() + 2, r.width() - r.height() - 12, r.height() - 4)
                                : QRect(r.x() + 10, r.y() + 1, r.width() - 40, r.height() - 2);
        case SC_ComboBoxListBoxPopup: return opt->rect;
        default: break;
        }
        break;
    }
    case CC_SpinBox: {
        const auto *sp = qstyleoption_cast<const QStyleOptionSpinBox *>(opt);
        if (!sp)
            break;
        const QRect r = opt->rect.adjusted(3, 3, -3, -3);
        const int h = r.height();
        const bool buttons = sp->buttonSymbols != QAbstractSpinBox::NoButtons;
        switch (sc) {
        case SC_SpinBoxFrame: return r;
        case SC_SpinBoxDown: return buttons ? QRect(r.right() - 2 * h + 1, r.y(), h, h) : QRect();
        case SC_SpinBoxUp: return buttons ? QRect(r.right() - h + 1, r.y(), h, h) : QRect();
        case SC_SpinBoxEditField:
            return QRect(r.x() + 9, r.y() + 1, r.width() - (buttons ? 2 * h : 0) - 11, h - 2);
        default: break;
        }
        break;
    }
    case CC_GroupBox: {
        // le cadre 8 px sous le haut, le titre à 14 px (16 px de haut)
        const auto *g = qstyleoption_cast<const QStyleOptionGroupBox *>(opt);
        if (!g)
            break;
        const QRect r = opt->rect;
        const QFontMetrics fm(sizedFont(w ? w->font() : QFont(), metric(Small), true));
        const int tw = fm.horizontalAdvance(g->text);
        const bool check = g->subControls & SC_GroupBoxCheckBox;
        const int cx = r.x() + 14;
        switch (sc) {
        case SC_GroupBoxFrame: return r.adjusted(0, 8, 0, 0);
        case SC_GroupBoxContents: return r.adjusted(10, 24, -10, -10);
        case SC_GroupBoxCheckBox: return check ? QRect(cx, r.y(), 16, 16) : QRect();
        case SC_GroupBoxLabel: return QRect(cx + (check ? 22 : 0), r.y(), tw, 16);
        default: break;
        }
        break;
    }
    default:
        break;
    }
    return QProxyStyle::subControlRect(cc, opt, sc, w);
}

QRect QKorinStyle::subElementRect(SubElement se, const QStyleOption *opt, const QWidget *w) const
{
    switch (se) {
    case SE_PushButtonContents:
        return opt->rect.adjusted(3 + 8, 3, -3 - 8, -3);
    case SE_PushButtonFocusRect:
        return opt->rect.adjusted(3, 3, -3, -3);
    case SE_LineEditContents: {
        // le texte à 8 px du cadre (uk_tb_draw) ; le champ d'une liste modifiable ou d'un nombre : leur style le place
        const auto *f = qstyleoption_cast<const QStyleOptionFrame *>(opt);
        if ((w && (qobject_cast<const QComboBox *>(w->parentWidget())
                   || qobject_cast<const QAbstractSpinBox *>(w->parentWidget())))
            || (f && f->lineWidth <= 0))
            return opt->rect.adjusted(1, 0, -1, 0);
        return opt->rect.adjusted(3 + 9, 3 + 1, -3 - 9, -3 - 1);
    }
    case SE_CheckBoxIndicator:
    case SE_RadioButtonIndicator:
        return QRect(opt->rect.x() + 3, opt->rect.y() + (opt->rect.height() - kBox) / 2, kBox, kBox);
    case SE_CheckBoxContents:
    case SE_RadioButtonContents: {
        const int x = opt->rect.x() + 3 + kBox + 8;
        return QRect(x, opt->rect.y(), opt->rect.right() - x + 1, opt->rect.height());
    }
    case SE_CheckBoxFocusRect:
    case SE_RadioButtonFocusRect:
        return QRect();
    case SE_ProgressBarGroove:
    case SE_ProgressBarContents:
        return opt->rect;
    case SE_ProgressBarLabel:
        return QRect();
    default:
        break;
    }
    return QProxyStyle::subElementRect(se, opt, w);
}

// --- tailles ---------------------------------------------------------------------------------------------------

int QKorinStyle::pixelMetric(PixelMetric pm, const QStyleOption *opt, const QWidget *w) const
{
    switch (pm) {
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return kBox + 6;	// (la case et la marge de l'anneau)
    case PM_CheckBoxLabelSpacing:
    case PM_RadioButtonLabelSpacing:
        return 5;		// (+ 3 de la marge : 8, comme tg_text)
    case PM_ScrollBarExtent:
        return metric(Sb);
    case PM_ScrollBarSliderMin:
        return 24;
    case PM_ScrollView_ScrollBarSpacing:
        return 0;
    case PM_ScrollView_ScrollBarOverlap:
        return 0;
    case PM_SliderThickness:
    case PM_SliderControlThickness:
    case PM_SliderLength:
        return kBox + 2;
    case PM_DefaultFrameWidth:
        if (isTextEdit(w))
            return 2;
        if (qobject_cast<const QAbstractItemView *>(w))
            return 1;
        return 1;
    case PM_SpinBoxFrameWidth:
        return 4;		// (1 + la marge de 3 px)
    case PM_ComboBoxFrameWidth:
        return 4;		// (1 + la marge de l'anneau)
    case PM_ButtonMargin:
        return 16;
    case PM_ButtonDefaultIndicator:
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical:
        return 0;
    case PM_MenuHMargin:
        return 0;
    case PM_MenuVMargin:
        return 6;		// (UK_POP_PAD)
    case PM_MenuPanelWidth:
        return 1;
    case PM_MenuBarPanelWidth:
        return 0;
    case PM_ToolTipLabelFrameWidth:
        return 4;
    case PM_TabBarTabHSpace:
        return 2 * 12;	// (UK_SEG_PAD : le texte à 12 px)
    case PM_TabBarTabVSpace:
        return 10;
    case PM_TabBarBaseOverlap:
    case PM_TabBarTabOverlap:
        return 0;
    case PM_TabBarTabShiftVertical:
    case PM_TabBarTabShiftHorizontal:
        return 0;
    case PM_HeaderMargin:
        return 10;
    case PM_FocusFrameHMargin:
    case PM_FocusFrameVMargin:
        return 3;
    default:
        break;
    }
    return QProxyStyle::pixelMetric(pm, opt, w);
}

QSize QKorinStyle::sizeFromContents(ContentsType ct, const QStyleOption *opt, const QSize &size, const QWidget *w) const
{
    const int ctl = metric(Ctl);
    switch (ct) {
    case CT_PushButton: {
        // la hauteur d'un contrôle (UMT_CTL), le texte à 8 px des bords au moins, 80 px de large au moins ; et la
        // marge de 3 px de l'anneau tout autour
        const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
        int wd = size.width() + 2 * 16;
        if (b && !b->text.isEmpty())
            wd = qMax(wd, 80);
        return QSize(wd + 6, qMax(size.height() + 8, ctl) + 6);
    }
    case CT_LineEdit:
        return QSize(size.width() + 18 + 6, qMax(size.height() + 4, ctl) + 6);
    case CT_ComboBox:
        return QSize(size.width() + 10 + 40 + 6, qMax(size.height() + 4, ctl) + 6);
    case CT_SpinBox:
        return QSize(size.width() + 2 * ctl + 20 + 6, qMax(size.height() + 2, ctl) + 6);
    case CT_CheckBox:
    case CT_RadioButton:
        return QSize(size.width() + kBox + 8 + 6, qMax(size.height(), kBox + 6));
    case CT_MenuItem: {
        const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
        if (mi && mi->menuItemType == QStyleOptionMenuItem::Separator)
            return QSize(size.width(), 11);	// (UK_POP_SEP)
        int wd = size.width() + 14 + 14;
        if (mi && mi->text.contains(u'\t'))
            wd += 24;		// (l'écart libellé - raccourci de la barre de menus)
        if (mi && mi->menuItemType == QStyleOptionMenuItem::SubMenu)
            wd += 12;
        return QSize(qMax(wd, 180), metric(MenuRow));
    }
    case CT_ItemViewItem: {
        const QSize s = QProxyStyle::sizeFromContents(ct, opt, size, w);
        return QSize(s.width() + 8, qMax(s.height(), metric(Row)));
    }
    case CT_HeaderSection: {
        const QSize s = QProxyStyle::sizeFromContents(ct, opt, size, w);
        return QSize(s.width() + 8, qMax(s.height(), metric(Hdr)));
    }
    case CT_TabBarTab: {
        const QSize s = QProxyStyle::sizeFromContents(ct, opt, size, w);
        return QSize(s.width(), qMax(s.height(), ctl));
    }
    case CT_ProgressBar:
        return QSize(size.width(), 10);
    case CT_Slider:
        return QProxyStyle::sizeFromContents(ct, opt, size, w).expandedTo(QSize(kBox + 2, kBox + 2));
    default:
        break;
    }
    return QProxyStyle::sizeFromContents(ct, opt, size, w);
}

int QKorinStyle::styleHint(StyleHint sh, const QStyleOption *opt, const QWidget *w, QStyleHintReturn *ret) const
{
    switch (sh) {
    case SH_TabBar_Alignment: {
        // les onglets d'UIKit (uk_tabs) : le segmenté centré ; le mode document : à gauche
        const auto *tb = qobject_cast<const QTabBar *>(w);
        return tb && tb->documentMode() ? int(Qt::AlignLeft) : int(Qt::AlignCenter);
    }
    case SH_ScrollBar_LeftClickAbsolutePosition:
        return true;
    case SH_ScrollBar_ContextMenu:
        return false;
    case SH_ScrollView_FrameOnlyAroundContents:
        return false;
    case SH_ItemView_ShowDecorationSelected:
        return true;
    case SH_ItemView_MovementWithoutUpdatingSelection:
        return true;
    case SH_Menu_Scrollable:
        return true;
    case SH_Menu_SubMenuPopupDelay:
        return 150;
    case SH_ComboBox_Popup:
        return false;
    case SH_UnderlineShortcut:
        return false;
    case SH_Widget_Animate:
    case SH_Widget_Animation_Duration:
        return 0;
    case SH_ToolTipLabel_Opacity:
        return 255;
    case SH_DialogButtonBox_ButtonsHaveIcons:
        return false;
    case SH_EtchDisabledText:
    case SH_DitherDisabledText:
        return false;
    case SH_LineEdit_PasswordCharacter:
        return 0x2022;
    default:
        break;
    }
    return QProxyStyle::styleHint(sh, opt, w, ret);
}

QT_END_NAMESPACE
