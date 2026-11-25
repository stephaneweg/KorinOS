// L'impression de Qt sur PrinterKit (Korin OS) : voir CMakeLists.txt et docs/etude/QT.md § impression.
#pragma once

#include <qpa/qplatformprintersupport.h>
#include <qpa/qplatformprintdevice.h>
#include <private/qprintengine_pdf_p.h>
#include <QtGui/qpainterpath.h>

QT_BEGIN_NAMESPACE

class QPrintDialog;

// les imprimantes de Korin (print_printers : PDF, et les imprimantes réseau IPP)
class QKorinPrinterSupport : public QPlatformPrinterSupport
{
public:
    QPrintEngine *createNativePrintEngine(QPrinter::PrinterMode mode, const QString &deviceId) override;
    QPaintEngine *createPaintEngine(QPrintEngine *engine, QPrinter::PrinterMode mode) override;
    QPrintDevice createPrintDevice(const QString &id) override;
    QStringList availablePrintDeviceIds() const override;
    QString defaultPrintDeviceId() const override;
};

class QKorinPrintDevice : public QPlatformPrintDevice
{
public:
    explicit QKorinPrintDevice(const QString &id);
    bool isValid() const override { return m_valid; }
    bool isDefault() const override { return m_default; }
    QPrint::DeviceState state() const override { return QPrint::Idle; }
    QPageSize defaultPageSize() const override;
    QMarginsF printableMargins(const QPageSize &, QPageLayout::Orientation, int) const override { return {}; }
    int defaultResolution() const override { return 1200; }
    QPrint::DuplexMode defaultDuplexMode() const override { return QPrint::DuplexNone; }
    QPrint::ColorMode defaultColorMode() const override { return m_color ? QPrint::Color : QPrint::GrayScale; }
protected:
    void loadPageSizes() const override;
    void loadResolutions() const override;
    void loadDuplexModes() const override;
    void loadColorModes() const override;
private:
    bool m_valid = false, m_default = false, m_color = true;
    QString m_mediaDefault;
};

// Le moteur : les propriétés et la page de QPdfPrintEngine (papier, résolution, marges), mais le dessin traduit en
// print_* (tracés remplis à la règle non nulle, traits en contours, texte en contours, images) dans un travail de
// PrinterKit ; printd en fait un PDF ou l'envoie à l'imprimante.
class QKorinPrintEngine : public QPdfPrintEngine
{
public:
    QKorinPrintEngine(QPrinter::PrinterMode mode, const QString &deviceId);
    ~QKorinPrintEngine() override;

    bool begin(QPaintDevice *pdev) override;
    bool end() override;
    bool newPage() override;
    bool abort() override;
    QPrinter::PrinterState printerState() const override
    { return m_pdf ? QPdfPrintEngine::printerState() : m_state; }

    void updateState(const QPaintEngineState &st) override { if (m_pdf) QPdfPrintEngine::updateState(st); }
    void drawPoints(const QPointF *points, int pointCount) override;
    void drawLines(const QLineF *lines, int lineCount) override;
    void drawRects(const QRectF *rects, int rectCount) override;
    void drawPolygon(const QPointF *points, int pointCount, PolygonDrawMode mode) override;
    void drawPath(const QPainterPath &path) override;
    void drawTextItem(const QPointF &p, const QTextItem &textItem) override;
    void drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &sr) override;
    void drawImage(const QRectF &r, const QImage &img, const QRectF &sr, Qt::ImageConversionFlags flags) override;
    void drawTiledPixmap(const QRectF &r, const QPixmap &pm, const QPointF &p) override;

private:
    void fillDevicePath(const QPainterPath &devicePath, const QBrush &brush);
    void strokePath(const QPainterPath &path, const QPen &pen);
    bool startPage();
    long mpt(qreal device) const;		// une longueur de l'appareil -> millièmes de point

    long m_job = 0;
    bool m_pdf = false;			// l'imprimante PDF vers un fichier connu : le PDF de Qt (QPdfPrintEngine)
    bool m_skip = false;			// page non demandée (print_page -> 0) : rien à dessiner
    qreal m_scale = 0.06;			// millièmes de point par point de l'appareil (72000 / résolution)
    QPointF m_offset;			// l'origine de l'appareil sur la feuille (marges), en points de l'appareil
    long m_paperW = 0, m_paperH = 0;
    QPrinter::PrinterState m_state = QPrinter::Idle;
};

QT_END_NAMESPACE

// le dialogue d'impression de Korin pour QPrintDialog (appelé par qprintdialog_unix.cpp, retouché pour Korin) :
// 1 imprimer, 0 abandon, -1 PrinterKit absent (le dialogue de Qt)
extern "C" int qt_korin_print_dialog(QT_PREPEND_NAMESPACE(QPrintDialog) *dialog);
