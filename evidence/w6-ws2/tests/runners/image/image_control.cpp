// Controls for image comparison: a Qt Quick scene and a libass drawing are
// rendered and compared with committed references; a noise case exercises the
// tolerance. The failing twin renders a different scene against the same
// reference. Shapes only: no text, so host fonts cannot change the pixels.

#include "image_compare.h"

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

extern "C" {
#include <ass/ass.h>
}

using namespace hikari::testing;

namespace {

const char *kScene = R"(
import QtQuick
Rectangle {
    width: 96; height: 64; color: "#202830"
    Rectangle { x: 8; y: 8; width: 40; height: 24; color: "#e0a030" }
    Rectangle { x: 52; y: 20; width: 32; height: 32; radius: 6; color: "#3080e0"; border.color: "white"; border.width: 2 }
    Rectangle { x: 16; y: 40; width: 24; height: 16; rotation: 30; color: "#70c070"; antialiasing: true }
}
)";

QImage renderScene(const char *qml)
{
    QQuickWindow window;
    window.resize(96, 64);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(qml, QUrl());
    auto *item = qobject_cast<QQuickItem *>(component.create());
    if (!item)
        return {};
    item->setParentItem(window.contentItem());
    window.show();
    if (!QTest::qWaitForWindowExposed(&window))
        return {};
    QImage image = window.grabWindow();
    delete item;
    return image;
}

// A filled ASS vector drawing (\p1). libass still needs one usable font to
// accept the event, so the control registers a fixed font from the locked Qt
// install (OFL Titillium Web) in memory; no system font provider is used.
QImage renderAssDrawing()
{
    QFile fontFile(QStringLiteral(HIKARI_TEST_FONT));
    if (!fontFile.open(QIODevice::ReadOnly))
        return {};
    QByteArray font = fontFile.readAll();
    ASS_Library *lib = ass_library_init();
    ass_add_font(lib, "Titillium Web", font.data(), static_cast<int>(font.size()));
    ASS_Renderer *renderer = ass_renderer_init(lib);
    ass_set_frame_size(renderer, 96, 64);
    ass_set_storage_size(renderer, 96, 64);
    ass_set_fonts(renderer, nullptr, "Titillium Web", ASS_FONTPROVIDER_NONE, nullptr, 0);
    const char script[] = "[Script Info]\nScriptType: v4.00+\nPlayResX: 96\nPlayResY: 64\n\n"
                          "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                          "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, "
                          "Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                          "Style: D,Titillium Web,20,&H0030A0E0,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,"
                          "0,0,0,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
                          "Effect, Text\nDialogue: 0,0:00:00.00,0:00:05.00,D,,0,0,0,,{\\pos(10,10)\\p1}m 0 0 l 50 0 "
                          "40 30 0 40{\\p0}\n";
    ASS_Track *track = ass_read_memory(lib, const_cast<char *>(script), sizeof script - 1, nullptr);
    QImage frame(96, 64, QImage::Format_ARGB32);
    frame.fill(qRgba(0, 0, 0, 255));
    int changed = 0;
    for (ASS_Image *img = ass_render_frame(renderer, track, 1000, &changed); img; img = img->next) {
        const int r = (img->color >> 24) & 0xff, g = (img->color >> 16) & 0xff, b = (img->color >> 8) & 0xff;
        const int opacity = 255 - (img->color & 0xff);
        for (int y = 0; y < img->h; ++y)
            for (int x = 0; x < img->w; ++x) {
                const int a = img->bitmap[y * img->stride + x] * opacity / 255;
                const QRgb dst = frame.pixel(img->dst_x + x, img->dst_y + y);
                frame.setPixel(img->dst_x + x, img->dst_y + y,
                               qRgba((r * a + qRed(dst) * (255 - a)) / 255, (g * a + qGreen(dst) * (255 - a)) / 255,
                                     (b * a + qBlue(dst) * (255 - a)) / 255, 255));
            }
    }
    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(lib);
    return frame;
}

bool check(const QImage &image, const QString &name, ImageTolerance tolerance = {})
{
    QString message;
    const bool ok = matchesReference(image, name, tolerance, QStringLiteral(HIKARI_REFERENCE_DIR),
                                     QStringLiteral(HIKARI_TEST_ARTIFACT_DIR), &message);
    if (!ok)
        qWarning("%s", qPrintable(message));
    return ok;
}

} // namespace

class ImageControl : public QObject {
    Q_OBJECT
private slots:
    void qtQuickSceneMatchesReference()
    {
#if HIKARI_IMAGE_FAILING
        // Same reference, different scene: must fail.
        const QImage image = renderScene("import QtQuick\nRectangle { width: 96; height: 64; color: \"white\" }");
#else
        const QImage image = renderScene(kScene);
#endif
        QVERIFY(!image.isNull());
        QVERIFY(check(image, QStringLiteral("runner-qtquick-shapes")));
    }

    void libassDrawingMatchesReference()
    {
        const QImage image = renderAssDrawing();
        QVERIFY(!image.isNull());
        // Guard against a silently empty render: the drawing must paint pixels.
        QVERIFY(image.pixel(30, 20) != qRgba(0, 0, 0, 255));
        QVERIFY(check(image, QStringLiteral("runner-libass-drawing")));
    }

    void toleranceAdmitsNoiseOnly()
    {
        QImage base(32, 32, QImage::Format_ARGB32);
        base.fill(qRgba(100, 100, 100, 255));
        QImage noisy = base;
        noisy.setPixel(3, 3, qRgba(102, 100, 100, 255));
        QVERIFY(compareImages(noisy, base, {2, 0}).withinTolerance);
        QVERIFY(!compareImages(noisy, base, {0, 0}).withinTolerance);
        QVERIFY(compareImages(noisy, base, {0, 1.0 / 1024}).withinTolerance); // one pixel in 1024 allowed
        QImage resized(16, 32, QImage::Format_ARGB32);
        QVERIFY(!compareImages(resized, base, {255, 1}).sizeMatches);
    }
};

QTEST_MAIN(ImageControl)
#include "image_control.moc"
