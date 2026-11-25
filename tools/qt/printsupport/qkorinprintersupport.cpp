// L'impression de Qt sur PrinterKit (Korin OS) : voir qkorinprintersupport.h, docs/etude/QT.md § impression.
#include "qkorinprintersupport.h"

#include <QtCore/qregularexpression.h>
#include <QtGui/qpainter.h>
#include <QtGui/qpagelayout.h>
#include <QtGui/private/qtextengine_p.h>
#include <QtGui/private/qfontengine_p.h>
#include <QtPrintSupport/qprintdialog.h>
#include <QtPrintSupport/qprinter.h>
#include <private/qprintdevice_p.h>

#include <cstring>

extern "C" {
#include <armpi/kit.h>
#include <printerkit.h>		// (les structures seulement : les fonctions sont prises à l'exécution, ci-dessous)
}

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

// --- PrinterKit, ouvert à l'exécution (src/kits/printerkit/printerkit.abi : le rang de chaque fonction) ---------

namespace {
struct Kit {
    long (*setup_default)(long);
    long (*dialog)(long, long);
    long (*setup_paper)(long, long, long);
    long (*begin)(long, long);
    long (*page)(long, long, long);
    long (*path_move)(long, long, long);
    long (*path_line)(long, long, long);
    long (*path_curve)(long, long, long, long, long, long, long);
    long (*path_close)(long);
    long (*path_fill)(long, long);
    long (*image_mode)(long, long);
    long (*image)(long, long, long, long, long, long, long, long);
    long (*end)(long);
    long (*abort)(long);
    long (*printers)(long, long);
    long (*printer_media)(long, long, long, long, long, long);
};

const Kit *kit()
{
    static Kit k;
    static int state = 0;	// 0 pas encore, 1 ouvert, -1 absent
    if (state == 0) {
        void *t = armpi_kit_open("printerkit", 35);
        state = t ? 1 : -1;
        if (t) {
            auto f = [t](unsigned rank) { return armpi_kit_fn(t, rank); };
            k.setup_default = reinterpret_cast<long (*)(long)>(f(0));
            k.dialog = reinterpret_cast<long (*)(long, long)>(f(1));
            k.setup_paper = reinterpret_cast<long (*)(long, long, long)>(f(2));
            k.begin = reinterpret_cast<long (*)(long, long)>(f(3));
            k.page = reinterpret_cast<long (*)(long, long, long)>(f(4));
            k.path_move = reinterpret_cast<long (*)(long, long, long)>(f(8));
            k.path_line = reinterpret_cast<long (*)(long, long, long)>(f(9));
            k.path_curve = reinterpret_cast<long (*)(long, long, long, long, long, long, long)>(f(10));
            k.path_close = reinterpret_cast<long (*)(long)>(f(11));
            k.path_fill = reinterpret_cast<long (*)(long, long)>(f(12));
            k.image_mode = reinterpret_cast<long (*)(long, long)>(f(14));
            k.image = reinterpret_cast<long (*)(long, long, long, long, long, long, long, long)>(f(15));
            k.end = reinterpret_cast<long (*)(long)>(f(23));
            k.abort = reinterpret_cast<long (*)(long)>(f(24));
            k.printers = reinterpret_cast<long (*)(long, long)>(f(25));
            k.printer_media = reinterpret_cast<long (*)(long, long, long, long, long, long)>(f(26));
        }
    }
    return state > 0 ? &k : nullptr;
}

bool debug()
{
    static const bool d = qEnvironmentVariableIntValue("KORIN_PRINT_DEBUG") > 0;
    return d;
}

QList<PrintPrinter> printers()
{
    QList<PrintPrinter> l;
    if (const Kit *k = kit()) {
        PrintPrinter p[16];
        std::memset(p, 0, sizeof p);
        const long n = k->printers(reinterpret_cast<long>(p), 16);
        for (long i = 0; i < n && i < 16; i++)
            l.append(p[i]);
        if (debug())
            qWarning("qt.print.korin : print_printers -> %ld", n);
    } else if (debug()) {
        qWarning("qt.print.korin : PrinterKit absent");
    }
    return l;
}

// un papier IPP (PWG 5101.1 : « iso_a4_210x297mm », « na_letter_8.5x11in ») -> QPageSize
QPageSize pageSizeFromIpp(const QString &name, const QString &label)
{
    static const QRegularExpression re(u"_([0-9.]+)x([0-9.]+)(mm|in)$"_s);
    const auto m = re.match(name);
    if (!m.hasMatch())
        return QPageSize();
    const QPageSize::Unit unit = m.captured(3) == "mm"_L1 ? QPageSize::Millimeter : QPageSize::Inch;
    const QSizeF size(m.captured(1).toDouble(), m.captured(2).toDouble());
    const QPageSize::PageSizeId id = QPageSize::id(size, unit, QPageSize::FuzzyMatch);
    if (id != QPageSize::Custom)
        return QPageSize(id);
    return QPageSize(size, unit, label.isEmpty() ? name : label);
}

void copyText(char *dst, size_t cap, const QString &s)
{
    const QByteArray b = s.toUtf8();
    std::strncpy(dst, b.constData(), cap - 1);
    dst[cap - 1] = 0;
}

long toMpt(qreal pt) { return long(qRound64(pt * 1000.0)); }
}

// --- les imprimantes -------------------------------------------------------------------------------------------

QPrintEngine *QKorinPrinterSupport::createNativePrintEngine(QPrinter::PrinterMode mode, const QString &deviceId)
{
    return new QKorinPrintEngine(mode, deviceId.isEmpty() ? defaultPrintDeviceId() : deviceId);
}

QPaintEngine *QKorinPrinterSupport::createPaintEngine(QPrintEngine *engine, QPrinter::PrinterMode)
{
    return static_cast<QKorinPrintEngine *>(engine);
}

QPrintDevice QKorinPrinterSupport::createPrintDevice(const QString &id)
{
    return QPlatformPrinterSupport::createPrintDevice(new QKorinPrintDevice(id));
}

QStringList QKorinPrinterSupport::availablePrintDeviceIds() const
{
    QStringList l;
    for (const PrintPrinter &p : printers())
        l << QString::fromUtf8(p.name);
    return l;
}

QString QKorinPrinterSupport::defaultPrintDeviceId() const
{
    const auto l = printers();
    for (const PrintPrinter &p : l)
        if (p.is_default)
            return QString::fromUtf8(p.name);
    return l.isEmpty() ? QString() : QString::fromUtf8(l.first().name);
}

QKorinPrintDevice::QKorinPrintDevice(const QString &id) : QPlatformPrintDevice(id)
{
    for (const PrintPrinter &p : printers()) {
        if (QString::fromUtf8(p.name) != id)
            continue;
        m_valid = true;
        m_default = p.is_default;
        m_color = p.color;
        m_name = id == "PDF"_L1 ? u"PDF"_s : id;
        m_makeAndModel = QString::fromUtf8(p.model);
        m_location = QString::fromUtf8(p.uri);
        m_isRemote = std::strcmp(p.kind, "pdf") != 0;
        m_mediaDefault = QString::fromUtf8(p.media_default);
        m_supportsMultipleCopies = true;
        m_supportsCollateCopies = false;
        m_supportsCustomPageSizes = !m_isRemote;
        m_minimumPhysicalPageSize = QSize(72, 72);
        m_maximumPhysicalPageSize = QSize(72 * 60, 72 * 60);
        break;
    }
}

QPageSize QKorinPrintDevice::defaultPageSize() const
{
    const QPageSize s = pageSizeFromIpp(m_mediaDefault, QString());
    return s.isValid() ? s : QPageSize(QPageSize::A4);
}

void QKorinPrintDevice::loadPageSizes() const
{
    m_pageSizes.clear();
    if (const Kit *k = kit()) {
        const QByteArray id = m_id.toUtf8();
        for (long i = 0; i < 64; i++) {
            char name[64] = {}, label[64] = {};
            if (!k->printer_media(reinterpret_cast<long>(id.constData()), i, reinterpret_cast<long>(name),
                                  sizeof name, reinterpret_cast<long>(label), sizeof label))
                break;
            const QPageSize s = pageSizeFromIpp(QString::fromUtf8(name), QString::fromUtf8(label));
            if (s.isValid() && !m_pageSizes.contains(s))
                m_pageSizes.append(s);
        }
    }
    if (m_pageSizes.isEmpty())
        m_pageSizes = { QPageSize(QPageSize::A4), QPageSize(QPageSize::Letter), QPageSize(QPageSize::A3),
                        QPageSize(QPageSize::A5), QPageSize(QPageSize::Legal) };
    m_havePageSizes = true;
}

void QKorinPrintDevice::loadResolutions() const
{
    m_resolutions = { 300, 600, 1200 };	// (le dessin part en vecteurs : la résolution ne fixe que les mesures)
    m_haveResolutions = true;
}

void QKorinPrintDevice::loadDuplexModes() const
{
    m_duplexModes = { QPrint::DuplexNone };
    m_haveDuplexModes = true;
}

void QKorinPrintDevice::loadColorModes() const
{
    m_colorModes = m_color ? QList<QPrint::ColorMode>{ QPrint::GrayScale, QPrint::Color }
                           : QList<QPrint::ColorMode>{ QPrint::GrayScale };
    m_haveColorModes = true;
}

// --- le moteur -------------------------------------------------------------------------------------------------

QKorinPrintEngine::QKorinPrintEngine(QPrinter::PrinterMode mode, const QString &deviceId)
    : QPdfPrintEngine(mode)
{
    QPdfPrintEngine::setProperty(PPK_PrinterName, deviceId);
    QPdfPrintEngine::setProperty(PPK_Resolution, 1200);
}

QKorinPrintEngine::~QKorinPrintEngine()
{
    if (m_job)
        abort();
}

long QKorinPrintEngine::mpt(qreal device) const
{
    return long(qRound64(device * m_scale));
}

bool QKorinPrintEngine::begin(QPaintDevice *pdev)
{
    if (debug())
        qWarning("qt.print.korin : begin, imprimante « %s »", qPrintable(property(PPK_PrinterName).toString()));
    // l'imprimante PDF de Korin avec un fichier de sortie : le PDF de Qt (texte gardé, polices incluses), comme après
    // le dialogue (qt_korin_print_dialog). KORIN_PRINT_KIT=1 : PrinterKit et printd quand même (le PDF de printd,
    // texte en contours : l'essai du chemin des imprimantes réseau, tools/qt/test/qtprint_test.cpp --kit)
    m_pdf = property(PPK_PrinterName).toString() == "PDF"_L1
            && !property(PPK_OutputFileName).toString().isEmpty() && qEnvironmentVariableIsEmpty("KORIN_PRINT_KIT");
    if (m_pdf)
        return QPdfPrintEngine::begin(pdev);
    const Kit *k = kit();
    if (!k) {
        qWarning("qt.print.korin : PrinterKit absent");
        m_state = QPrinter::Error;
        return false;
    }
    // la feuille et l'origine de l'appareil (QPdfPrintEngine : propriétés en points de l'appareil)
    const int res = property(PPK_Resolution).toInt() > 0 ? property(PPK_Resolution).toInt() : 1200;
    m_scale = 72000.0 / res;
    const QRect paper = property(PPK_PaperRect).toRect(), page = property(PPK_PageRect).toRect();
    m_offset = page.topLeft() - paper.topLeft();
    m_paperW = mpt(paper.width());
    m_paperH = mpt(paper.height());

    PrintSetup s;
    std::memset(&s, 0, sizeof s);
    s.size = sizeof s;
    k->setup_default(reinterpret_cast<long>(&s));
    const QString name = property(PPK_PrinterName).toString();
    if (!name.isEmpty())
        copyText(s.printer, sizeof s.printer, name);
    s.paper_w = int(m_paperW);
    s.paper_h = int(m_paperH);
    s.orientation = m_paperW > m_paperH ? PRINT_LANDSCAPE : PRINT_PORTRAIT;
    s.margin_l = s.margin_t = s.margin_r = s.margin_b = 0;
    s.copies = qMax(1, property(PPK_CopyCount).toInt());
    s.mono = property(PPK_ColorMode).toInt() == QPrinter::GrayScale;
    s.from = s.to = 0;		// (l'app ne dessine que les pages demandées)
    const QString out = property(PPK_OutputFileName).toString();
    if (!out.isEmpty())
        copyText(s.output, sizeof s.output, out);
    const QByteArray title = property(PPK_DocumentName).toString().toUtf8();
    m_job = k->begin(reinterpret_cast<long>(&s), reinterpret_cast<long>(title.isEmpty() ? "Qt" : title.constData()));
    if (!m_job) {
        qWarning("qt.print.korin : print_begin refusé (file d'impression ?)");
        m_state = QPrinter::Error;
        return false;
    }
    m_state = QPrinter::Active;
    return startPage();
}

bool QKorinPrintEngine::startPage()
{
    m_skip = !kit()->page(m_job, m_paperW, m_paperH);
    return true;
}

bool QKorinPrintEngine::newPage()
{
    if (m_pdf)
        return QPdfPrintEngine::newPage();
    return m_job && startPage();
}

bool QKorinPrintEngine::end()
{
    if (m_pdf) {
        m_pdf = false;
        return QPdfPrintEngine::end();
    }
    if (!m_job)
        return false;
    const long id = kit()->end(m_job);
    m_job = 0;
    m_state = id > 0 ? QPrinter::Idle : QPrinter::Error;
    if (id <= 0)
        qWarning("qt.print.korin : travail refusé (%ld)", id);
    return id > 0;
}

bool QKorinPrintEngine::abort()
{
    if (m_pdf) {
        m_pdf = false;
        return QPdfPrintEngine::abort();
    }
    if (m_job)
        kit()->abort(m_job);
    m_job = 0;
    m_state = QPrinter::Aborted;
    return true;
}

// une couleur de PrinterKit (0xRRGGBB) : la transparence mêlée au blanc du papier
static long kitColor(QColor c, qreal opacity)
{
    const qreal a = c.alphaF() * opacity;
    auto mix = [a](int v) { return int(qRound(v * a + 255 * (1 - a))); };
    return (long(mix(c.red())) << 16) | (long(mix(c.green())) << 8) | long(mix(c.blue()));
}

static QColor brushColor(const QBrush &b)
{
    if (const QGradient *g = b.gradient()) {	// (pas de dégradé dans PrinterKit : la couleur du milieu)
        const auto stops = g->stops();
        if (!stops.isEmpty())
            return stops.at(stops.size() / 2).second;
    }
    return b.color();
}

void QKorinPrintEngine::fillDevicePath(const QPainterPath &devicePath, const QBrush &brush)
{
    if (!m_job || m_skip || brush.style() == Qt::NoBrush || devicePath.isEmpty())
        return;
    const QColor c = brushColor(brush);
    const qreal opacity = painter() ? painter()->opacity() : 1.0;
    if (c.alpha() == 0 || opacity <= 0)
        return;
    // printd remplit à la règle non nulle : un chemin pair-impair est simplifié d'abord
    QPainterPath p = devicePath;
    if (p.fillRule() == Qt::OddEvenFill && p.elementCount() > 4)
        p = p.simplified();
    const Kit *k = kit();
    const auto X = [this](qreal x) { return mpt(x + m_offset.x()); };
    const auto Y = [this](qreal y) { return mpt(y + m_offset.y()); };
    bool open = false;
    for (int i = 0; i < p.elementCount(); i++) {
        const QPainterPath::Element e = p.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            if (open)
                k->path_close(m_job);
            k->path_move(m_job, X(e.x), Y(e.y));
            open = true;
            break;
        case QPainterPath::LineToElement:
            k->path_line(m_job, X(e.x), Y(e.y));
            break;
        case QPainterPath::CurveToElement: {
            const QPainterPath::Element c2 = p.elementAt(i + 1), e3 = p.elementAt(i + 2);
            k->path_curve(m_job, X(e.x), Y(e.y), X(c2.x), Y(c2.y), X(e3.x), Y(e3.y));
            i += 2;
            break;
        }
        default:
            break;
        }
    }
    if (open)
        k->path_close(m_job);
    k->path_fill(m_job, kitColor(c, opacity));
}

void QKorinPrintEngine::strokePath(const QPainterPath &path, const QPen &pen)
{
    if (pen.style() == Qt::NoPen || !painter())
        return;
    const QTransform t = painter()->transform();
    QPainterPathStroker st(pen);
    QPainterPath outline;
    if (pen.isCosmetic() || pen.widthF() == 0) {
        // (largeur dans l'appareil ; 0 : le trait le plus fin, ici un quart de point)
        st.setWidth(pen.widthF() > 0 ? pen.widthF() : 0.25 / m_scale * 1000.0);
        outline = st.createStroke(t.map(path));
    } else {
        outline = t.map(st.createStroke(path));
    }
    outline.setFillRule(Qt::WindingFill);
    fillDevicePath(outline, pen.brush());
}

void QKorinPrintEngine::drawPath(const QPainterPath &path)
{
    if (m_pdf)
        return QPdfPrintEngine::drawPath(path);
    if (!painter())
        return;
    fillDevicePath(painter()->transform().map(path), painter()->brush());
    strokePath(path, painter()->pen());
}

void QKorinPrintEngine::drawPolygon(const QPointF *points, int pointCount, PolygonDrawMode mode)
{
    if (m_pdf)
        return QPdfPrintEngine::drawPolygon(points, pointCount, mode);
    if (pointCount < 2)
        return;
    QPainterPath path;
    path.moveTo(points[0]);
    for (int i = 1; i < pointCount; i++)
        path.lineTo(points[i]);
    if (mode == PolylineMode) {
        strokePath(path, painter()->pen());
        return;
    }
    path.closeSubpath();
    path.setFillRule(mode == OddEvenMode ? Qt::OddEvenFill : Qt::WindingFill);
    drawPath(path);
}

void QKorinPrintEngine::drawRects(const QRectF *rects, int rectCount)
{
    if (m_pdf)
        return QPdfPrintEngine::drawRects(rects, rectCount);
    for (int i = 0; i < rectCount; i++) {
        QPainterPath path;
        path.addRect(rects[i]);
        drawPath(path);
    }
}

void QKorinPrintEngine::drawLines(const QLineF *lines, int lineCount)
{
    if (m_pdf)
        return QPdfPrintEngine::drawLines(lines, lineCount);
    QPainterPath path;
    for (int i = 0; i < lineCount; i++) {
        path.moveTo(lines[i].p1());
        path.lineTo(lines[i].p2());
    }
    strokePath(path, painter()->pen());
}

void QKorinPrintEngine::drawPoints(const QPointF *points, int pointCount)
{
    if (m_pdf)
        return QPdfPrintEngine::drawPoints(points, pointCount);
    QPainterPath path;
    for (int i = 0; i < pointCount; i++) {
        path.moveTo(points[i]);
        path.lineTo(points[i] + QPointF(0.001, 0));
    }
    strokePath(path, painter()->pen());
}

void QKorinPrintEngine::drawTextItem(const QPointF &p, const QTextItem &textItem)
{
    if (m_pdf)
        return QPdfPrintEngine::drawTextItem(p, textItem);
    // le texte en contours (les glyphes de la police de Qt) : rien ne dépend des polices de printd
    const QTextItemInt &ti = static_cast<const QTextItemInt &>(textItem);
    if (!ti.fontEngine || !painter())
        return;
    QVarLengthArray<QFixedPoint> positions;
    QVarLengthArray<glyph_t> glyphs;
    ti.fontEngine->getGlyphPositions(ti.glyphs, QTransform::fromTranslate(p.x(), p.y()), ti.flags, glyphs, positions);
    if (glyphs.isEmpty())
        return;
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    ti.fontEngine->addGlyphsToPath(glyphs.data(), positions.data(), int(glyphs.size()), &path, ti.flags);
    fillDevicePath(painter()->transform().map(path), painter()->pen().brush());
}

void QKorinPrintEngine::drawImage(const QRectF &r, const QImage &img, const QRectF &sr, Qt::ImageConversionFlags flags)
{
    if (m_pdf)
        return QPdfPrintEngine::drawImage(r, img, sr, flags);
    if (!m_job || m_skip || img.isNull() || !painter())
        return;
    const QImage part = (sr.toRect() == img.rect() ? img : img.copy(sr.toRect()));
    const bool alpha = part.hasAlphaChannel();
    const QImage px = part.convertToFormat(alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    const QRectF d = painter()->transform().mapRect(r);	// (pas de rotation des images : leur cadre)
    const Kit *k = kit();
    k->image_mode(m_job, alpha ? PRINT_IMG_ALPHA : 0);
    k->image(m_job, reinterpret_cast<long>(px.constBits()), px.width(), px.height(), mpt(d.x() + m_offset.x()),
             mpt(d.y() + m_offset.y()), mpt(d.width()), mpt(d.height()));
}

void QKorinPrintEngine::drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &sr)
{
    if (m_pdf)
        return QPdfPrintEngine::drawPixmap(r, pm, sr);
    drawImage(r, pm.toImage(), sr, Qt::AutoColor);
}

void QKorinPrintEngine::drawTiledPixmap(const QRectF &r, const QPixmap &pm, const QPointF &p)
{
    if (m_pdf)
        return QPdfPrintEngine::drawTiledPixmap(r, pm, p);
    if (pm.isNull())
        return;
    const QImage img = pm.toImage();
    const qreal w = pm.width(), h = pm.height();
    for (qreal y = r.top() - p.y(); y < r.bottom(); y += h)
        for (qreal x = r.left() - p.x(); x < r.right(); x += w) {
            const QRectF tile = QRectF(x, y, w, h) & r;
            if (!tile.isEmpty())
                drawImage(tile, img, QRectF(tile.x() - x, tile.y() - y, tile.width(), tile.height()), Qt::AutoColor);
        }
}

QT_END_NAMESPACE

// --- le dialogue de Korin pour QPrintDialog --------------------------------------------------------------------

extern "C" int qt_korin_print_dialog(QPrintDialog *dialog)
{
    using namespace QT_PREPEND_NAMESPACE(Qt::StringLiterals);
    const Kit *k = kit();
    QPrinter *pr = dialog ? dialog->printer() : nullptr;
    if (!k || !pr)
        return -1;
    PrintSetup s;
    std::memset(&s, 0, sizeof s);
    s.size = sizeof s;
    k->setup_default(reinterpret_cast<long>(&s));
    // une app qui fait sa mise en page (pleine page : MuseScore, un PDF) garde son papier : pas de choix du papier
    const bool ownPaper = pr->fullPage();
    const QRectF full = pr->pageLayout().fullRect(QPageLayout::Point);
    if (ownPaper) {
        s.paper_w = int(toMpt(full.width()));
        s.paper_h = int(toMpt(full.height()));
        s.orientation = full.width() > full.height() ? PRINT_LANDSCAPE : PRINT_PORTRAIT;
    }
    if (!pr->printerName().isEmpty())
        copyText(s.printer, sizeof s.printer, pr->printerName());
    s.copies = qMax(1, pr->copyCount());
    const QByteArray title = pr->docName().toUtf8();
    PrintDialogInfo di;
    std::memset(&di, 0, sizeof di);
    di.size = sizeof di;
    di.title = title.isEmpty() ? "Document" : title.constData();
    di.pages = (dialog->maxPage() >= dialog->minPage() && dialog->maxPage() < 100000) ? dialog->maxPage() : 0;
    di.flags = ownPaper ? PRINT_DLG_OWN_PAPER : 0;
    if (!k->dialog(reinterpret_cast<long>(&s), reinterpret_cast<long>(&di)))
        return 0;

    // le choix de l'utilisateur -> QPrinter
    const QString printer = QString::fromUtf8(s.printer);
    if (printer == "PDF"_L1) {
        // l'imprimante PDF : le PDF de Qt lui-même (vectoriel, texte gardé), écrit où l'utilisateur l'a dit
        pr->setOutputFormat(QPrinter::PdfFormat);
        pr->setOutputFileName(QString::fromUtf8(s.output));
    } else {
        pr->setOutputFormat(QPrinter::NativeFormat);
        pr->setPrinterName(printer);
    }
    if (!ownPaper && s.paper_w > 0 && s.paper_h > 0) {
        const bool land = s.orientation == PRINT_LANDSCAPE;
        const qreal w = (land ? s.paper_h : s.paper_w) / 1000.0, h = (land ? s.paper_w : s.paper_h) / 1000.0;
        pr->setPageSize(QPageSize(QSizeF(w, h), QPageSize::Point, QString::fromUtf8(s.media),
                                  QPageSize::FuzzyMatch));
        pr->setPageOrientation(land ? QPageLayout::Landscape : QPageLayout::Portrait);
    }
    pr->setCopyCount(qMax(1, s.copies));
    pr->setColorMode(s.mono ? QPrinter::GrayScale : QPrinter::Color);
    if (s.from > 0) {
        pr->setPrintRange(QPrinter::PageRange);
        pr->setFromTo(s.from, s.to >= s.from ? s.to : s.from);
    } else {
        pr->setPrintRange(QPrinter::AllPages);
        if (di.pages > 0)
            pr->setFromTo(dialog->minPage(), dialog->maxPage());
    }
    return 1;
}
