// Frozen-Qt proof: translation (Linguist), compiled shader (ShaderTools),
// multimedia backend and a QML module load; --render checks the shaded pixel.
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLibraryInfo>
#include <QMediaFormat>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QTranslator>
#include <cstdio>

static int fail(const char *what)
{
    std::fprintf(stderr, "FAIL %s\n", what);
    return 1;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const bool render = app.arguments().contains(QStringLiteral("--render"));
    std::printf("qt_runtime=%s\nqt_prefix=%s\nplatform=%s\n", qVersion(),
                qPrintable(QLibraryInfo::path(QLibraryInfo::PrefixPath)),
                qPrintable(QGuiApplication::platformName()));
    if (QLatin1StringView(qVersion()) != QLatin1StringView("6.11.2"))
        return fail("runtime version");

    QTranslator translator;
    if (!translator.load(QStringLiteral(":/i18n/qtproof_de.qm")))
        return fail("qm load");
    app.installTranslator(&translator);
    if (QCoreApplication::translate("Proof", "Frozen Qt proof") != QStringLiteral("Eingefrorener Qt-Nachweis"))
        return fail("translation");

    QFile qsb(QStringLiteral(":/tint.frag.qsb"));
    // Presence of the qsb resource; --render proves the shader actually runs.
    if (!qsb.open(QIODevice::ReadOnly) || qsb.size() < 64)
        return fail("compiled shader");

    const auto decoders = QMediaFormat().supportedFileFormats(QMediaFormat::Decode);
    std::printf("media_decode_formats=%lld\n", static_cast<long long>(decoders.size()));
    if (decoders.isEmpty())
        return fail("multimedia backend");

    QQmlApplicationEngine engine;
    engine.loadFromModule("HikariProof", "Main");
    if (engine.rootObjects().isEmpty())
        return fail("qml load");
    if (!render) {
        std::puts("PASS selftest");
        return 0;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window)
        return fail("window");
    QTimer::singleShot(1500, &app, [&] {
        if (window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
            std::puts("FAIL render: software scene graph cannot run ShaderEffect");
            app.exit(1);
            return;
        }
        const QImage image = window->grabWindow();
        const QRgb px = image.isNull() ? 0 : image.pixel(image.width() / 2, image.height() / 2);
        std::printf("graphics_api=%d center=%08x\n", int(window->rendererInterface()->graphicsApi()), px);
        const bool magenta = qRed(px) > 200 && qGreen(px) < 60 && qBlue(px) > 200;
        std::puts(magenta ? "PASS render" : "FAIL render");
        app.exit(magenta ? 0 : 1);
    });
    return app.exec();
}
