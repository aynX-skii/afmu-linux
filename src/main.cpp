#include "AppController.h"
#include "Config.h"
#include "I18n.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("afmu"));
    app.setApplicationDisplayName(QStringLiteral("FileBridge"));
    app.setOrganizationName(QStringLiteral("aynux"));
    app.setDesktopFileName(QStringLiteral("afmu"));

    // Basic 样式才允许完全自定义控件外观
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // AppController 自己最先建好 I18n，这里直接取
    AppController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("Cfg"), controller.config());
    engine.rootContext()->setContextProperty(QStringLiteral("Lang"), controller.i18n());

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Afmu", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
