// qtquick_test : Qt Quick (QML, rendu logiciel) sur la QPA korin. --shot FICHIER.png --after MS : capture puis fin.
#include <QtGui>
#include <QtQuick>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQmlApplicationEngine engine;
    engine.loadFromModule("KorinTest", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
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
