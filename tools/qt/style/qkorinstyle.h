// QKorinStyle : le style korin de QWidgets, les contrôles dessinés comme ceux d'UIKit (src/kits/uikit/uk_*.c :
// boutons, champs, listes déroulantes, cases, curseurs, barres de défilement, onglets, menus, infobulles…), sur la
// base de Fusion pour le reste. Couleurs : uniquement les jetons du thème du bureau (TJ_* d'UIKit), jamais en dur.
//
// D'où viennent les jetons, dans l'ordre :
//   1. QKorinStyle::setTokens (les essais hors de Korin) ;
//   2. le thème de la plateforme korin : QPlatformTheme::themeHint(KorinTokensHint) -> l'adresse du tableau des
//      jetons d'UIKit (ui_tokens), et KorinMetricsHint -> les métriques (ui_metric) ;
//   3. sinon (une autre plateforme : offscreen…), une approximation tirée de la palette de l'application.
#pragma once

#include <QtWidgets/qproxystyle.h>
#include <QtGui/qcolor.h>

QT_BEGIN_NAMESPACE

class QKorinStyle : public QProxyStyle
{
    Q_OBJECT
public:
    // les indications propres au thème de la plateforme korin (tools/qt/qpa/qkorintheme.cpp : mêmes valeurs)
    enum { KorinTokensHint = 0x4B520001, KorinMetricsHint = 0x4B520002 };

    // les jetons d'UIKit (indices KQ_TJ_* de tools/qt/qpa/korin_ui.h)
    enum Token {
        Face, Surface, Sunken, Raised, RaisedLow, Line, LineStrong, Text1, Text2, Text3, Accent, AccentLight,
        AccentDark, AccentVivid, Danger, Success, Warning, Bar, Dock, TitleActive, TitleInactive, Outline, Dark,
        Menu, AccentInk, Hover, TokenCount
    };
    // les métriques d'UIKit (UMT_* de src/kits/uikit/uikit.inc)
    enum Metric { Px, Small, Row, Ctl, Sb, Hdr, MenuRow, Touch, MetricCount };

    QKorinStyle();

    // pour les essais : un tableau de jetons (0x00RRGGBB, TokenCount mots) et de métriques (MetricCount), ou nullptr
    static void setTokens(const unsigned *tokens, const int *metrics = nullptr);

    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    void polish(QPalette &pal) override { QProxyStyle::polish(pal); }
    void polish(QApplication *app) override { QProxyStyle::polish(app); }

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                       const QWidget *w = nullptr) const override;
    void drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                     const QWidget *w = nullptr) const override;
    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p,
                            const QWidget *w = nullptr) const override;
    int pixelMetric(PixelMetric pm, const QStyleOption *opt = nullptr, const QWidget *w = nullptr) const override;
    QSize sizeFromContents(ContentsType ct, const QStyleOption *opt, const QSize &size,
                           const QWidget *w = nullptr) const override;
    int styleHint(StyleHint sh, const QStyleOption *opt = nullptr, const QWidget *w = nullptr,
                  QStyleHintReturn *ret = nullptr) const override;
    QRect subControlRect(ComplexControl cc, const QStyleOptionComplex *opt, SubControl sc,
                         const QWidget *w = nullptr) const override;
    QRect subElementRect(SubElement se, const QStyleOption *opt, const QWidget *w = nullptr) const override;
    SubControl hitTestComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, const QPoint &pt,
                                     const QWidget *w = nullptr) const override;

    // les jetons et métriques du moment (palette : pour l'approximation hors de Korin)
    struct Tokens {
        QColor c[TokenCount];
        bool dark = false;
        const QColor &operator[](Token t) const { return c[t]; }
    };
    static Tokens tokens(const QPalette &pal);
    static int metric(Metric m);
    // les nuances d'UIKit (src/kits/uikit/uk_theme.c) : ui_tone (n < 128 assombrit, > 128 éclaircit), ui_mix
    static QColor tone(const QColor &c, int n);
    static QColor mix(const QColor &a, const QColor &b, int t);
};

QT_END_NAMESPACE
