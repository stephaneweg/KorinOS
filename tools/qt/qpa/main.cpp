// La QPA korin de Qt : le plugin (docs/etude/QT.md).
#include <qpa/qplatformintegrationplugin.h>
#include "qkorinintegration.h"

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

class QKorinIntegrationPlugin : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "korin.json")
public:
    QPlatformIntegration *create(const QString &system, const QStringList &paramList) override
    {
        if (!system.compare("korin"_L1, Qt::CaseInsensitive))
            return new QKorinIntegration(paramList);
        return nullptr;
    }
};

QT_END_NAMESPACE

#include "main.moc"
