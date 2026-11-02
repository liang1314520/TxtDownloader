#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NovDecoder"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QTranslator translator;
    if (translator.load(QLocale(QStringLiteral("zh_CN")), QStringLiteral("qt"),
                        QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&translator);
    }

    MainWindow window;
    window.show();
    return app.exec();
}
