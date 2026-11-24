// qtwidgets_test : une fenêtre QWidgets sur la QPA korin (fenêtre, bande de titre, champs, liste déroulante,
// barre de menus globale). --shot FICHIER.png : capture de la fenêtre après --after ms, puis fin (aussi avec
// QT_QPA_PLATFORM=offscreen, pour essayer QtGui sans bureau).
#include <QtWidgets>
#include <QtGui/qbackingstore.h>
#include <qpa/qplatformbackingstore.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)	// (le shell de Korin n'a pas « VAR=valeur commande »)
        if (!strcmp(argv[i], "--qpadebug"))
        {
            qputenv("KORIN_QPA_DEBUG", "2");
            qputenv("QT_LOGGING_RULES", "qt.widgets.painting=true");
        }
    QApplication app(argc, argv);
    QString shot;
    int after = 1500;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); i++) {
        if (args[i] == "--shot") shot = args[i + 1];
        if (args[i] == "--after") after = args[i + 1].toInt();
    }

    QMainWindow win;
    win.setWindowTitle("Essai de Qt");
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    auto say = [log](const QString &s) { log->appendPlainText(s); printf("%s\n", qPrintable(s)); fflush(stdout); };

    // les menus : dans la barre de menus globale de Korin (QMenuBar natif)
    QMenu *file = win.menuBar()->addMenu("&Fichier");
    file->addAction("&Ouvrir…", QKeySequence::Open, [&] { say("menu : Ouvrir"); });
    QMenu *recent = file->addMenu("Récents");
    recent->addAction("partition.mscz", [&] { say("menu : Récents › partition.mscz"); });
    recent->addAction("etude.mscz", [&] { say("menu : Récents › etude.mscz"); });
    file->addSeparator();
    file->addAction("&Quitter", QKeySequence::Quit, &app, &QApplication::quit);
    QMenu *edit = win.menuBar()->addMenu("&Édition");
    QAction *grid = edit->addAction("Grille", QKeySequence("Ctrl+G"), [&](bool on) { say(QString("menu : Grille %1").arg(on)); });
    grid->setCheckable(true);
    QAction *off = edit->addAction("Grisée");
    off->setEnabled(false);

    auto *central = new QWidget;
    auto *form = new QFormLayout(central);
    auto *line = new QLineEdit("Bonjour, Korin");
    auto *combo = new QComboBox;
    combo->addItems({ "Givre", "Onyx Noir", "Ardoise" });
    auto *slider = new QSlider(Qt::Horizontal);
    auto *check = new QCheckBox("Case à cocher");
    auto *button = new QPushButton("Bouton");
    form->addRow("Texte", line);
    form->addRow("Liste", combo);
    form->addRow("Curseur", slider);
    form->addRow("", check);
    form->addRow("", button);
    form->addRow(log);
    QObject::connect(button, &QPushButton::clicked, [&] { say("bouton : " + line->text()); });
    QObject::connect(combo, &QComboBox::currentTextChanged, [&](const QString &t) { say("liste : " + t); });
    win.setCentralWidget(central);
    win.statusBar()->showMessage(QString("Qt %1, plateforme %2").arg(qVersion(), QGuiApplication::platformName()));
    win.resize(560, 460);
    win.show();
    say(QString("plateforme %1, écran %2x%3, menus natifs %4")
            .arg(QGuiApplication::platformName())
            .arg(win.screen()->geometry().width()).arg(win.screen()->geometry().height())
            .arg(win.menuBar()->isNativeMenuBar()));

    // --selftest : l'app ouvre elle-même la liste déroulante et dit ce qu'elle voit (essais sous QEMU sans clics)
    if (args.contains("--selftest")) {
        QTimer::singleShot(3000, [&] {
            say(QString("journal : %1 lignes, viewport %2,%3 %4x%5, style %6, barre %7, contrôle %8")
                    .arg(log->blockCount()).arg(log->viewport()->x()).arg(log->viewport()->y())
                    .arg(log->viewport()->width()).arg(log->viewport()->height())
                    .arg(app.style()->name()).arg(app.style()->pixelMetric(QStyle::PM_ScrollBarExtent))
                    .arg(line->height()));
            // des pixels autres que le fond : dans grab() (rendu direct) et dans l'image du backing store
            auto inked = [](const QImage &im, const QRect &r) {
                int n = 0;
                const QRgb bg = im.pixel(r.topLeft());
                for (int y = r.top(); y <= r.bottom(); y++)
                    for (int x = r.left(); x <= r.right(); x++)
                        if (im.valid(x, y) && im.pixel(x, y) != bg)
                            n++;
                return n;
            };
            const QImage g = log->viewport()->grab().toImage();
            const QRect vr(log->viewport()->mapTo(&win, QPoint()), log->viewport()->size());
            const QImage bs = win.backingStore() ? win.backingStore()->handle()->toImage() : QImage();
            say(QString("encre : grab %1, backing store %2 (image %3x%4, rect %5,%6)")
                    .arg(inked(g, g.rect())).arg(bs.isNull() ? -1 : inked(bs, vr.translated(0, 0)))
                    .arg(bs.width()).arg(bs.height()).arg(vr.x()).arg(vr.y()));
            combo->showPopup();
        });
        QTimer::singleShot(6000, [&] {
            QWidget *popup = QApplication::activePopupWidget();
            say(QString("popup : %1 %2").arg(popup ? "ouvert" : "fermé")
                    .arg(popup ? QString("%1,%2 %3x%4").arg(popup->x()).arg(popup->y()).arg(popup->width()).arg(popup->height()) : QString()));
            say(QString("fenêtre : %1,%2 %3x%4 active %5").arg(win.x()).arg(win.y()).arg(win.width()).arg(win.height()).arg(win.isActiveWindow()));
        });
    }
    if (!shot.isEmpty())
        QTimer::singleShot(after, [&] {
            const bool ok = win.grab().save(shot);
            say(QString("capture %1 : %2").arg(shot).arg(ok));
            app.quit();
        });
    return app.exec();
}
