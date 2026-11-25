// Le style korin de QWidgets : le plugin (docs/etude/QT.md § style).
#include <QtWidgets/qstyleplugin.h>
#include "qkorinstyle.h"

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

class QKorinStylePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "korin.json")
public:
    QStyle *create(const QString &key) override
    {
        if (!key.compare("korin"_L1, Qt::CaseInsensitive))
            return new QKorinStyle;
        return nullptr;
    }
};

QT_END_NAMESPACE

#include "main.moc"
