// qtprint_test : QPrinter sur PrinterKit (le plugin printsupport korin).
//   --direct FICHIER.pdf : deux pages vers l'imprimante PDF de Korin, sans dialogue, puis la taille du fichier écrit
//                          (le PDF de Qt) ; --dialog : QPrintDialog (le dialogue de Korin), puis la même chose ;
//   --kit : par PrinterKit et printd même vers l'imprimante PDF (KORIN_PRINT_KIT : le chemin des imprimantes réseau,
//           texte en contours) ; --debug : les traces du plugin (KORIN_PRINT_DEBUG)
#include <QtWidgets>
#include <QtPrintSupport>

static void paint(QPrinter &printer)
{
    QPainter p(&printer);
    const QRectF r = printer.pageRect(QPrinter::DevicePixel);
    for (int page = 0; page < 2; page++) {
        if (page)
            printer.newPage();
        QFont f("DejaVu Serif");
        f.setPointSizeF(28);
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(QRectF(r.x(), r.y() + r.height() * 0.05, r.width(), r.height() * 0.1), Qt::AlignCenter,
                   QString("Korin imprime avec Qt — page %1").arg(page + 1));
        p.setPen(QPen(QColor(0x30, 0x60, 0xC0), r.width() / 300));
        p.drawLine(QPointF(r.width() * 0.1, r.height() * 0.2), QPointF(r.width() * 0.9, r.height() * 0.2));
        p.setBrush(QColor(0xE8, 0xEE, 0xF8));
        p.drawRoundedRect(QRectF(r.width() * 0.1, r.height() * 0.25, r.width() * 0.35, r.height() * 0.12), 40, 40);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(r.width() * 0.55, r.height() * 0.25, r.width() * 0.3, r.height() * 0.12));
        QImage img(64, 64, QImage::Format_RGB32);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                img.setPixel(x, y, qRgb(x * 4, y * 4, 160));
        p.drawImage(QRectF(r.width() * 0.1, r.height() * 0.42, r.width() * 0.2, r.width() * 0.2), img);
        f.setPointSizeF(11);
        p.setFont(f);
        p.setPen(Qt::darkGray);
        p.drawText(QRectF(r.width() * 0.35, r.height() * 0.42, r.width() * 0.55, r.height() * 0.3),
                   Qt::TextWordWrap, "Tracés, traits, courbes, images et texte : vers une imprimante de Korin, "
                   "ils vont à PrinterKit, puis à printd ; vers l'imprimante PDF, au PDF de Qt.");
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--debug"))
            qputenv("KORIN_PRINT_DEBUG", "1");
        else if (!strcmp(argv[i], "--kit"))
            qputenv("KORIN_PRINT_KIT", "1");
    QApplication app(argc, argv);
    const QStringList a = app.arguments();
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName("Essai de Qt");
    printf("imprimantes : %s ; par défaut %s\n", qPrintable(QPrinterInfo::availablePrinterNames().join(", ")),
           qPrintable(QPrinterInfo::defaultPrinterName()));
    QString out = "/tmp/qtprint.pdf";
    if (a.contains("--dialog")) {
        QPrintDialog d(&printer);
        d.setMinMax(1, 2);
        const int r = d.exec();
        printf("dialogue : %d, imprimante « %s », sortie « %s », format %d\n", r, qPrintable(printer.printerName()),
               qPrintable(printer.outputFileName()), int(printer.outputFormat()));
        if (r != QDialog::Accepted)
            return 1;
        if (!printer.outputFileName().isEmpty())
            out = printer.outputFileName();
    } else {
        const int i = a.indexOf("--direct");
        if (i >= 0 && i + 1 < a.size())
            out = a[i + 1];
        // (un nom en .pdf fait passer QPrinter au PDF de Qt : le nom d'abord, puis l'imprimante de Korin)
        printer.setOutputFileName(out);
        printer.setOutputFormat(QPrinter::NativeFormat);
        printer.setPrinterName("PDF");
    }
    printf("moteur : %s, format %d ; imprimantes %s\n",
           printer.paintEngine() && printer.paintEngine()->type() == QPaintEngine::Pdf ? "PDF (Qt ou Korin)" : "?",
           int(printer.outputFormat()), qPrintable(QPrinterInfo::availablePrinterNames().join(", ")));
    printf("page %s, %d ppp\n", qPrintable(printer.pageLayout().pageSize().name()), printer.resolution());
    fflush(stdout);
    paint(printer);
    // printd écrit le PDF après coup (le travail est en file) : attendre le fichier
    for (int t = 0; t < 60 && QFileInfo(out).size() == 0; t++)
        QThread::msleep(500);
    printf("%s : %lld octets\n", qPrintable(out), QFileInfo(out).size());
    return QFileInfo(out).size() > 0 ? 0 : 2;
}
