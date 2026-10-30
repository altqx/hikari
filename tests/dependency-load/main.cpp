#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QDir>
#include <QIcon>
#include <QIconEngine>
#include <QIconEnginePlugin>
#include <QImage>
#include <QImageReader>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <vector>

#include <ffms.h>
extern "C" {
#include <ass/ass.h>
}
#include <portaudio.h>

// Upstream libass can carry the fork's LIBASS_VERSION; only the fork defines these macros.
#if !defined(LIBASS_HAS_TRACK_EVENTS_MODIFIED) || !defined(LIBASS_HAS_FRAME_ATLAS) \
    || !defined(LIBASS_HAS_FONT_DIAGNOSTICS)
#error "libass is not the altqx fork (ports/libass)"
#endif

static int fail(const char *what)
{
    std::fprintf(stderr, "FAIL %s\n", what);
    return 1;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    std::printf("qt=%s platform=%s\n", qVersion(), qPrintable(QGuiApplication::platformName()));
    if (QLatin1StringView(qVersion()) != QLatin1StringView(HIKARI_QT_VERSION))
        return fail("Qt runtime is not " HIKARI_QT_VERSION);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nItem { width: 4 }", QUrl());
    std::unique_ptr<QObject> item(component.create());
    if (!item || item->property("width").toInt() != 4)
        return fail(qPrintable(component.errorString()));

    if (!QImageReader::supportedImageFormats().contains("svg"))
        return fail("the Qt SVG image plugin (imageformats/qsvg) does not load");
    QByteArray svg("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\">"
                   "<rect width=\"16\" height=\"16\" fill=\"#ff0000\"/></svg>");
    QBuffer buffer(&svg);
    const QImage image = QImageReader(&buffer, "svg").read();
    if (image.isNull() || image.pixelColor(8, 8) != QColor(255, 0, 0))
        return fail("the Qt SVG image plugin does not draw an SVG");
    QTemporaryDir dir;
    QFile file(dir.filePath(QStringLiteral("probe.svg")));
    if (!dir.isValid() || !file.open(QIODevice::WriteOnly) || file.write(svg) != svg.size())
        return fail("probe.svg");
    file.close();
    // QIcon falls back to the image plugin, so load the icon engine plugin itself.
    const QDir engines(QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/iconengines"));
    bool svgEngine = false;
    for (const QString &name : engines.entryList(QDir::Files)) {
        QPluginLoader loader(engines.filePath(name));
        auto *plugin = qobject_cast<QIconEnginePlugin *>(loader.instance());
        const std::unique_ptr<QIconEngine> iconEngine(plugin ? plugin->create() : nullptr);
        if (!iconEngine || iconEngine->key() != QLatin1String("svg"))
            continue;
        iconEngine->addFile(file.fileName(), QSize(), QIcon::Normal, QIcon::Off);
        const QImage drawn = iconEngine->pixmap(QSize(48, 48), QIcon::Normal, QIcon::Off).toImage();
        svgEngine = drawn.size() == QSize(48, 48) && drawn.pixelColor(24, 24) == QColor(255, 0, 0);
        break;
    }
    if (!svgEngine)
        return fail("the Qt SVG icon engine (iconengines/qsvgicon) does not load or draw an SVG icon");
    std::puts("svg=imageformats/qsvg iconengines/qsvgicon");

    FFMS_Init(0, 0);
    const int ffms = FFMS_GetVersion();
    std::printf("ffms2=%d.%d.%d\n", (ffms >> 24) & 0xff, (ffms >> 16) & 0xff, (ffms >> 8) & 0xff);

    const int ass = ass_library_version();
    std::printf("libass=0x%08x\n", ass);
    if (ass < 0x01705020)
        return fail("libass is older than the altqx fork 0x01705020");
    ASS_Library *library = ass_library_init();
    ASS_Renderer *renderer = library ? ass_renderer_init(library) : nullptr;
    if (!renderer)
        return fail("libass renderer");
    ass_set_fonts(renderer, nullptr, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, nullptr, 0);
    ass_set_frame_size(renderer, 64, 64);
    ass_set_storage_size(renderer, 64, 64);
    char script[] = "[Script Info]\nScriptType: v4.00+\nPlayResX: 64\nPlayResY: 64\n\n"
                    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\an7\\pos(8,8)\\p1}m 0 0 l 16 0 16 16 0 16\n";
    ASS_Track *track = ass_read_memory(library, script, sizeof script - 1, nullptr);
    if (!track || track->n_events != 1)
        return fail("libass parse");
    int change = 0;
    ASS_Image *images = ass_render_frame(renderer, track, 500, &change);
    const ASS_Atlas *atlas = images ? ass_frame_pack_atlas(renderer, images, 256, 0, 1) : nullptr;
    if (!atlas || atlas->n_instances < 1 || atlas->n_pages != 1)
        return fail("libass fork: ass_frame_pack_atlas packs no frame");
    std::printf("libass-fork=atlas %dx%d instances %d\n", atlas->width, atlas->height, atlas->n_instances);
    track->events[0].Start = 2000;
    ass_track_events_modified(track, 0);
    if (!ass_render_frame(renderer, track, 2500, &change))
        return fail("libass fork: an event moved in place does not show after ass_track_events_modified");
    ass_set_font_diagnostics(library, nullptr, nullptr);
    ass_frame_atlas_reset(renderer);
    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(library);

    std::printf("portaudio=%s\n", Pa_GetVersionInfo()->versionText);
    if (Pa_Initialize() != paNoError)
        return fail("Pa_Initialize");
    std::vector<PaHostApiTypeId> apis;
    for (PaHostApiIndex i = 0; i < Pa_GetHostApiCount(); ++i)
        apis.push_back(Pa_GetHostApiInfo(i)->type);
    Pa_Terminate();
#ifdef _WIN32
    const std::vector<PaHostApiTypeId> pinned{paDirectSound, paWASAPI};
#else
    const std::vector<PaHostApiTypeId> pinned{paALSA};
#endif
    std::ranges::sort(apis);
    if (apis != pinned)
        return fail("PortAudio host APIs differ from the pinned set");

    std::puts("PASS dependency-load");
    return 0;
}
