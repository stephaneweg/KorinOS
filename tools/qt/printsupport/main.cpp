// L'impression de Qt sur PrinterKit (Korin OS) : le plugin (tools/qt/printsupport).
#include <qpa/qplatformprintplugin.h>
#include "qkorinprintersupport.h"

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

class QKorinPrinterSupportPlugin : public QPlatformPrinterSupportPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformPrinterSupportFactoryInterface_iid FILE "korin.json")
public:
    QPlatformPrinterSupport *create(const QString &key) override
    {
        if (!key.compare("korinprintsupport"_L1, Qt::CaseInsensitive))
            return new QKorinPrinterSupport;
        return nullptr;
    }
};

QT_END_NAMESPACE

#include "main.moc"
