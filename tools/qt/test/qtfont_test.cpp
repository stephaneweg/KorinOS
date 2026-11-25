// qtfont_test : le texte de Qt dans Korin, comme MuseScore le dessine (une police d'application, de petites tailles
// sous une forte mise à l'échelle du QPainter, et par QGlyphRun). --font FICHIER : une police à charger (sinon
// DejaVu Sans) ; --shot FICHIER.png --after MS.
#include <QtWidgets>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <sys/ucontext.h>

// --trace : à la faute, le RIP puis les adresses de code trouvées dans la pile (sans pointeurs de cadre dans Qt :
// une remontée approchée ; addr2line -f -C -e qtfont_test sur l'hôte)
extern "C" char _start[];
static void segv(int, siginfo_t *si, void *ctx)
{
    auto *uc = static_cast<ucontext_t *>(ctx);
    const unsigned long rip = uc->uc_mcontext.gregs[REG_RIP];
    const unsigned long *sp = reinterpret_cast<const unsigned long *>(uc->uc_mcontext.gregs[REG_RSP]);
    char line[96];
    int n = snprintf(line, sizeof line, "FAUTE rip %lx adresse %p\n", rip, si->si_addr);
    write(2, line, n);
    const unsigned long lo = reinterpret_cast<unsigned long>(_start), hi = lo + 0x4000000;
    for (int i = 0, k = 0; i < 2048 && k < 40; i++) {
        if (sp[i] > lo && sp[i] < hi) {
            n = snprintf(line, sizeof line, "PILE %lx\n", sp[i]);
            write(2, line, n);
            k++;
        }
    }
    _exit(139);
}


class View : public QWidget
{
public:
    QString family;
    int only = -1;	// (--only N : seulement le cas N : 0 x1, 1 x4, 2 x16, 3 glyphrun)
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(Qt::black);
        const QString t = "found lost wretch but now am";
        int y = 30, n = 0;
        for (qreal scale : { 1.0, 4.0, 16.0 }) {
            if (only >= 0 && only != n++) {
                y += 40;
                continue;
            }
            printf("cas x%g\n", scale);
            fflush(stdout);
            p.save();
            p.translate(10, y);
            p.scale(scale, scale);
            QFont f(family);
            f.setPointSizeF(18.0 / scale);
            p.setFont(f);
            p.drawText(QPointF(0, 0), t + QString("  (x%1)").arg(scale));
            p.restore();
            y += 40;
        }
        if (only == 4) {
            // les réglages de MuseScore (Font::toQFont) : PreferTypoLineMetrics, chaque préférence de hinting,
            // de petites tailles sous une mise à l'échelle (la partition)
            p.fillRect(rect(), Qt::white);
            int yy = 24;
            const QFont::HintingPreference hp[] = { QFont::PreferDefaultHinting, QFont::PreferNoHinting,
                                                    QFont::PreferVerticalHinting, QFont::PreferFullHinting };
            for (int h = 0; h < 4; h++) {
                for (qreal scale : { 0.5, 1.0, 2.0, 3.3 }) {
                    p.save();
                    p.translate(10 + (scale > 1.5 ? 380 : 0), yy + (scale == 1.0 || scale == 3.3 ? 22 : 0));
                    p.scale(scale, scale);
                    QFont f(family);
                    f.setPointSizeF(10.0 / scale);
                    f.setStyleStrategy(QFont::PreferTypoLineMetrics);
                    f.setHintingPreference(hp[h]);
                    p.setFont(f);
                    p.drawText(QPointF(0, 0), QString("found lost wretch but h%1 x%2").arg(h).arg(scale));
                    p.restore();
                }
                yy += 48;
            }
            return;
        }
        if (only >= 0 && only != 3)
            return;
        printf("cas glyphrun\n");
        fflush(stdout);
        // par QTextLayout et QGlyphRun (drawTextWorkaround de MuseScore)
        QFont f(family);
        f.setPointSizeF(18.0);
        QTextLayout layout(t + "  (glyphrun)", f, this);
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        for (const QGlyphRun &g : layout.glyphRuns())
            p.drawGlyphRun(QPointF(10, y), g);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    struct sigaction sa = {};
    sa.sa_sigaction = segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    View v;
    v.family = "DejaVu Sans";
    QString shot;
    int after = 3000;
    const QStringList a = app.arguments();
    for (int i = 1; i + 1 < a.size(); i++) {
        if (a[i] == "--font") {
            const int id = QFontDatabase::addApplicationFont(a[i + 1]);
            if (id >= 0)
                v.family = QFontDatabase::applicationFontFamilies(id).value(0);
            printf("police %s -> %d « %s »\n", qPrintable(a[i + 1]), id, qPrintable(v.family));
        }
        if (a[i] == "--shot") shot = a[i + 1];
        if (a[i] == "--only") v.only = a[i + 1].toInt();
        if (a[i] == "--after") after = a[i + 1].toInt();
    }
    if (a.contains("--edwin")) {
        const int id = QFontDatabase::addApplicationFont(":/Edwin-Roman.otf");
        v.family = QFontDatabase::applicationFontFamilies(id).value(0);
        printf("Edwin -> %d « %s »\n", id, qPrintable(v.family));
    }
    fflush(stdout);
    v.setWindowTitle("Essai du texte");
    v.resize(760, 220);
    v.show();
    if (!shot.isEmpty())
        QTimer::singleShot(after, [&] { v.grab().save(shot); app.quit(); });
    return app.exec();
}
