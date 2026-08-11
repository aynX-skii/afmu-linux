#include "AppController.h"
#include "Config.h"
#include "I18n.h"
#include "Identity.h"

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QSurfaceFormat>

#include <cstdio>

namespace {

/**
 * `afmu --fingerprint` —— 打印本机 v2 身份的 SPKI 指纹，没有就先生成。
 *
 * 这不只是给用户看的。PROTOCOL-v2-DRAFT.md §12 第 1 步要求两端做完身份层
 * 之后**立刻交叉验证指纹**：这里打印的值必须和下面这条命令的输出一致 ——
 *
 *   openssl x509 -in ~/.config/afmu/identity.pem -pubkey -noout \
 *     | openssl pkey -pubin -outform DER | sha256sum
 *
 * mTLS 手工钉扎最常见的翻车就是两端对「SPKI」的理解差一层封装（有人哈希了
 * 整张证书，有人哈希了裸 EC 点）。等握手层调通再发现，症状是「证书明明对却
 * 一直不匹配」，非常难查。这一步 10 分钟，省半天。
 *
 * 不开图形界面，所以在 QGuiApplication 之前处理。
 */
int printFingerprint()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString path = QDir(base).filePath(QStringLiteral("afmu/identity.pem"));

    afmu::Identity identity;
    if (!identity.ensure(path)) {
        std::fprintf(stderr, "身份不可用: %s\n", qPrintable(identity.lastError()));
        return 1;
    }
    std::printf("文件      %s\n", qPrintable(path));
    std::printf("SHA-256   %s\n", identity.fingerprint().toHex().constData());
    std::printf("base32    %s\n", qPrintable(identity.fingerprintBase32()));
    std::printf("展示形式  %s\n", qPrintable(identity.fingerprintDisplay()));
    std::printf(
        "\n交叉验证（上面的 SHA-256 必须和这条命令的输出一致）：\n"
        "  openssl x509 -in %s -pubkey -noout \\\n"
        "    | openssl pkey -pubin -outform DER | sha256sum\n",
        qPrintable(path));
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--fingerprint") == 0)
            return printFingerprint();
    }

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
