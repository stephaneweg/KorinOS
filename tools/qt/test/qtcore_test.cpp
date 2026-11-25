// qtcore_test : QtCore sur Korin (fichiers, dossiers, fils, minuteries, expressions, JSON, dates, processus).
// Chaque essai écrit « OK nom » ou « ÉCHEC nom : raison » ; le code de sortie est le nombre d'échecs.
#include <QtCore>

static int failures = 0;
static void check(bool ok, const char *name, const QString &why = QString())
{
    if (ok) {
        printf("OK %s\n", name);
    } else {
        printf("ÉCHEC %s : %s\n", name, qPrintable(why));
        failures++;
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("Qt %s sur %s (%s)\n", qVersion(), qPrintable(QSysInfo::prettyProductName()),
           qPrintable(QSysInfo::buildAbi()));

    // chaînes, nombres, expressions
    check(QString::number(3.25, 'f', 2) == "3.25", "nombres");
    QRegularExpression re("^(\\w+)-(\\d+)$");
    auto m = re.match("korin-42");
    check(m.hasMatch() && m.captured(2) == "42", "regex");
    check(QString("Été").toUpper() == "ÉTÉ" && QString("Été").toUtf8().size() == 5, "unicode");

    // fichiers et dossiers
    const QString dir = QDir::tempPath() + "/qtcore_test";
    QDir().mkpath(dir);
    QFile f(dir + "/a.txt");
    check(f.open(QIODevice::WriteOnly) && f.write("bonjour\n") == 8, "écriture");
    f.close();
    check(f.open(QIODevice::ReadOnly) && f.readAll() == "bonjour\n", "lecture");
    f.close();
    check(QFileInfo(dir + "/a.txt").size() == 8, "taille");
    check(QFile::rename(dir + "/a.txt", dir + "/b.txt") && QFile::exists(dir + "/b.txt"), "renommer");
    check(QDir(dir).entryList(QDir::Files) == QStringList{ "b.txt" }, "liste");
    check(QDir(dir).removeRecursively(), "effacer");
    check(!QDir("/").entryList().isEmpty(), "racine", QDir("/").entryList().join(' '));
    printf("  temp=%s home=%s app=%s\n", qPrintable(QDir::tempPath()), qPrintable(QDir::homePath()),
           qPrintable(QCoreApplication::applicationFilePath()));

    // JSON, dates
    auto doc = QJsonDocument::fromJson(R"({"a":[1,2,3],"b":"x"})");
    check(doc["a"].toArray().size() == 3 && doc["b"].toString() == "x", "json");
    check(QDateTime::currentDateTimeUtc().date().year() >= 2026, "date", QDateTime::currentDateTimeUtc().toString());

    // fils et minuteries
    QAtomicInt n = 0;
    QThreadPool pool;
    for (int i = 0; i < 16; i++)
        pool.start([&n] { n.fetchAndAddRelaxed(1); });
    pool.waitForDone();
    check(n.loadRelaxed() == 16, "fils");
    QElapsedTimer t;
    t.start();
    QEventLoop loop;
    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] { if (++ticks == 5) loop.quit(); });
    timer.start(20);
    loop.exec();
    check(ticks == 5 && t.elapsed() >= 90 && t.elapsed() < 2000, "minuteries", QString::number(t.elapsed()));
    // un signal d'un autre fil, reçu dans la boucle (le réveil du répartiteur)
    QThread *th = QThread::create([&] { QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection); });
    t.restart();
    th->start();
    loop.exec();
    th->wait();
    delete th;
    check(t.elapsed() < 1000, "réveil", QString::number(t.elapsed()));

    // processus
    QProcess p;
    p.start("/bin/echo", { "salut" });
    const bool started = p.waitForFinished(5000);
    check(started && p.readAllStandardOutput().trimmed() == "salut", "processus", p.errorString());

    printf("%d échec(s)\n", failures);
    return failures;
}
