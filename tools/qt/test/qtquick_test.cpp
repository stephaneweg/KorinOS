// qtquick_test : Qt Quick (QML, rendu logiciel) sur la QPA korin. --shot FICHIER.png --after MS : capture puis fin.
#include <QtGui>
#include <QtQuick>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError> &w) {
        for (const QQmlError &e : w)
            printf("avertissement QML : %s\n", qPrintable(e.toString()));
        fflush(stdout);
    });
    engine.loadFromModule("KorinTest", "Main");
    if (engine.rootObjects().isEmpty()) {
        // (diagnostic : les chemins d'import et les ressources, puis le chargement par l'adresse)
        printf("chemins d'import : %s\n", qPrintable(engine.importPathList().join(' ')));
        QDirIterator it(":/", QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (f.endsWith(".qml") || f.endsWith("qmldir"))
                printf("ressource : %s\n", qPrintable(f));
        }
        fflush(stdout);
        engine.load(QUrl("qrc:/qt/qml/KorinTest/Main.qml"));
        if (engine.rootObjects().isEmpty())
            engine.load(QUrl("qrc:/KorinTest/Main.qml"));
        if (engine.rootObjects().isEmpty())
            return 1;
    }
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QString shot;
    int after = 2000;
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); i++) {
        if (args[i] == "--shot") shot = args[i + 1];
        if (args[i] == "--after") after = args[i + 1].toInt();
    }
    if (win && !shot.isEmpty())
        QTimer::singleShot(after, [&] {
            const bool ok = win->grabWindow().save(shot);
            printf("capture %s : %d\n", qPrintable(shot), ok);
            fflush(stdout);
            app.quit();
        });
    return app.exec();
}
