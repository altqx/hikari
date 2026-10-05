// K1: the in-house vector icon set (docs/qt/ux/icons.md). The icon manifest
// test (every role the QML names resolves to an SVG of the set; each SVG is
// valid, single-colour with at most one accent layer, on the 16-unit grid and
// without raster), the tint, the theme colours' contrast, the Icon item
// following the theme layer live (K2), the menu items, tab buttons, dialog
// titles and window icons that draw the set through the image provider, the
// right-to-left mirroring (the manifest's flags against icons.md's rule, and
// the flip painted), and the rendering fixtures: the whole set drawn by the
// Icon item at the process's scale (100% here; 150% and 200% in the
// .scale150 / .scale200 runs) in the four themes of the theme layer (K2),
// equal to the SVG files drawn at the device's pixels. With HIKARI_ICON_SHEET_DIR set the contact sheets are
// written there (k1-<theme>-<percent>.png).

#include "controls_style.h"
#include "icon_theme.h"
#include "settings_store.h"
#include "theme.h"

#include "hikari/application/settings.h"

#include <QAccessible>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPalette>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cmath>
#include <memory>
#include <set>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::theme::Code;

namespace {

QJsonObject readManifest()
{
    QFile file(QStringLiteral(HIKARI_ICON_DIR "/manifest.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Every number in an attribute value (path data, coordinates).
QList<double> numbers(const QString &text)
{
    static const QRegularExpression number(QStringLiteral(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));
    QList<double> out;
    for (auto it = number.globalMatch(text); it.hasNext();)
        out << it.next().captured().toDouble();
    return out;
}

bool onGrid(double v)
{
    return std::abs(v * 4 - std::round(v * 4)) < 1e-9;
}

// Messages Qt prints while a block runs (QSvgRenderer reports what it cannot
// draw as warnings).
struct MessageCapture {
    static inline QStringList captured;
    QtMessageHandler previous;
    MessageCapture()
    {
        captured.clear();
        previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) { captured << msg; });
    }
    ~MessageCapture() { qInstallMessageHandler(previous); }
};

QColor composite(QRgb premultiplied, const QColor &background)
{
    const double a = qAlpha(premultiplied) / 255.0;
    return QColor::fromRgbF(float(qRed(premultiplied) / 255.0 + background.redF() * (1 - a)),
                            float(qGreen(premultiplied) / 255.0 + background.greenF() * (1 - a)),
                            float(qBlue(premultiplied) / 255.0 + background.blueF() * (1 - a)));
}

// The icon as the SVG file in the source tree draws it, recoloured apart
// from the item's tint (icons::tint's text substitution): the colours are
// given as SVG's own color property, the root's in the icon colour and the
// accent group's in the accent colour, and the file's currentColor paints
// resolve to them. (QtSvg resolves an inherited stroke="currentColor" with
// the root's color, so the accent group names its stroke again.)
QImage drawnFromSource(const QString &role, int side, const QColor &colour, const QColor &accent)
{
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QFile file(QStringLiteral(HIKARI_ICON_DIR "/") + role + QStringLiteral(".svg"));
    if (!file.open(QIODevice::ReadOnly))
        return image;
    QByteArray svg = file.readAll();
    svg.replace("<svg ", "<svg color=\"" + colour.name().toLatin1() + "\" ");
    svg.replace("<g id=\"accent\">", "<g id=\"accent\" color=\"" + accent.name().toLatin1() + "\" stroke=\"currentColor\">");
    QSvgRenderer renderer(svg);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, side, side));
    return image;
}

// Shows `code` (K2): the theme layer's preview of that theme, not following
// the system; `extra` adds other appearance values (an accent, a pick).
void useTheme(Code code, const QVariantMap &extra = {})
{
    QVariantMap values{{QStringLiteral("appearance.theme"), ui::theme::codeName(code)},
                       {QStringLiteral("appearance.followSystem"), false}};
    values.insert(extra);
    ui::theme::preview(values);
}

const ui::theme::Roles &themeRoles()
{
    return ui::theme::current().roles;
}

} // namespace

class IconTests : public QObject {
    Q_OBJECT

    QJsonObject manifest;

    QStringList manifestRoles() const
    {
        QStringList out;
        for (const auto &icon : manifest.value(QLatin1String("icons")).toArray())
            out << icon.toObject().value(QLatin1String("role")).toString();
        return out;
    }

    // A window showing `qml` (a component of the Hikari.Ui module's types).
    struct Shown {
        std::unique_ptr<QQmlEngine> engine;
        std::unique_ptr<QQuickWindow> window;
        QQuickItem *root = nullptr;
    };
    static Shown show(const QByteArray &qml, QSize size)
    {
        Shown s;
        s.engine = std::make_unique<QQmlEngine>();
        QQmlComponent component(s.engine.get());
        component.setData(qml, QUrl(QStringLiteral("qrc:/k1-test.qml")));
        if (!component.isReady())
            qWarning() << component.errors();
        s.root = qobject_cast<QQuickItem *>(component.create());
        if (!s.root)
            return s;
        s.window = std::make_unique<QQuickWindow>();
        s.root->setParentItem(s.window->contentItem());
        s.root->setParent(s.window.get());
        s.window->resize(size);
        s.window->show();
        return s;
    }

private slots:
    void initTestCase()
    {
        manifest = readManifest();
        QVERIFY(!manifest.isEmpty());
        ui::theme::useSettings(nullptr);
    }

    // The manifest lists every SVG of the set once, with its label, the
    // surfaces using it and the legacy bitmaps it replaces; every legacy
    // bitmap is either replaced or named as not replaced, with the reason.
    void manifestAccountsForTheSetAndLegacyBitmaps()
    {
        QCOMPARE(manifest.value(QLatin1String("grid")).toInt(), 16);
        const QStringList roles = manifestRoles();
        QCOMPARE(QSet<QString>(roles.begin(), roles.end()).size(), roles.size());
        QCOMPARE(ui::icons::roles(), roles); // the resources carry the same manifest
        QStringList files = QDir(QStringLiteral(HIKARI_ICON_DIR)).entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name);
        QStringList expected;
        for (const auto &role : roles)
            expected << role + QStringLiteral(".svg");
        expected.sort();
        QCOMPARE(files, expected);
        std::set<QString> replaced;
        for (const auto &value : manifest.value(QLatin1String("icons")).toArray()) {
            const auto icon = value.toObject();
            const QString role = icon.value(QLatin1String("role")).toString();
            QCOMPARE(icon.value(QLatin1String("file")).toString(), role + QStringLiteral(".svg"));
            QVERIFY2(!icon.value(QLatin1String("label")).toString().isEmpty(), qPrintable(role));
            QVERIFY2(!icon.value(QLatin1String("surfaces")).toArray().isEmpty(), qPrintable(role));
            QVERIFY2(QFile::exists(ui::icons::resourcePath(role)), qPrintable(role));
            for (const auto &bitmap : icon.value(QLatin1String("legacy")).toArray()) {
                QVERIFY2(QFile::exists(QStringLiteral(HIKARI_LEGACY_BITMAPS "/") + bitmap.toString()), qPrintable(bitmap.toString()));
                replaced.insert(bitmap.toString());
            }
        }
        std::set<QString> accounted = replaced;
        const auto notReplaced = manifest.value(QLatin1String("notReplaced")).toObject();
        for (const auto &reason : notReplaced.keys())
            for (const auto &bitmap : notReplaced.value(reason).toArray())
                QVERIFY2(accounted.insert(bitmap.toString()).second, qPrintable(bitmap.toString()));
        std::set<QString> legacy;
        for (const auto &name : QDir(QStringLiteral(HIKARI_LEGACY_BITMAPS)).entryList(QDir::Files))
            legacy.insert(name);
        QCOMPARE(accounted, legacy);
    }

    // Every role the QML names (the iconRole bindings of Icon, IconButton,
    // IconToolButton, ShellMenuItem, ShellMenu, IconTabButton and the dialog
    // titles, and IconTheme.setWindowIcon's) resolves to an SVG of the set;
    // every role of the set is named by the QML, or the manifest marks it
    // "pending" with the later card whose surface places it (and then the
    // QML does not name it yet).
    void everyRoleTheQmlReferencesResolves()
    {
        static const QRegularExpression binding(QStringLiteral(R"re(\biconRole:\s*([^\n;}]*))re"));
        static const QRegularExpression windowIcon(QStringLiteral("\\bsetWindowIcon\\([^,]*,\\s*(\"[^\"]*\")"));
        static const QRegularExpression literal(QStringLiteral("\"([^\"]*)\""));
        QSet<QString> referenced;
        QDirIterator it(QStringLiteral(HIKARI_UI_SOURCE_DIR), {QStringLiteral("*.qml")}, QDir::Files);
        while (it.hasNext()) {
            QFile file(it.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            for (const auto *pattern : {&binding, &windowIcon})
                for (auto b = pattern->globalMatch(text); b.hasNext();) {
                    const QString expression = b.next().captured(1);
                    for (auto l = literal.globalMatch(expression); l.hasNext();) {
                        const QString role = l.next().captured(1);
                        if (role.isEmpty())
                            continue; // a component's default: no icon
                        referenced.insert(role);
                        QVERIFY2(ui::icons::exists(role), qPrintable(QFileInfo(file).fileName() + QStringLiteral(": ") + role));
                    }
                }
        }
        // The surfaces wired so far: among them the video transport, the
        // menus, the audio box, the Line editor, the document tabs, the
        // Search tool, the windows' and dialogs' icons.
        for (const char *role : {"media-play", "media-pause", "play-line", "media-stop", "frame-previous", "frame-next",
                                 "open-subtitles", "undo", "recent-subtitles", "convert-srt", "audio-previous-line", "link",
                                 "commit", "tag-bold", "colour-shadow", "tag-font", "tab-close", "tab-new",
                                 "document-modified", "search", "find-replace", "history", "styles", "settings",
                                 "select-lines", "automation", "tool-crosshair", "tool-all-tags", "font-collector"})
            QVERIFY2(referenced.contains(QLatin1String(role)), role);
        static const QRegularExpression card(QStringLiteral(R"(^[A-Z]\d+ #\d+(, [A-Z]\d+ #\d+)*$)"));
        int pending = 0;
        for (const auto &value : manifest.value(QLatin1String("icons")).toArray()) {
            const auto icon = value.toObject();
            const QString role = icon.value(QLatin1String("role")).toString();
            if (icon.contains(QLatin1String("pending"))) {
                ++pending;
                QVERIFY2(card.match(icon.value(QLatin1String("pending")).toString()).hasMatch(), qPrintable(role));
                QVERIFY2(!referenced.contains(role), qPrintable(role + QStringLiteral(" is placed: no longer pending")));
            } else {
                QVERIFY2(referenced.contains(role), qPrintable(role + QStringLiteral(" has no surface and is not pending")));
            }
        }
        QCOMPARE(pending, 35);
    }

    void svgsAreMonochromeOnTheGrid_data()
    {
        QTest::addColumn<QString>("role");
        for (const auto &role : manifestRoles())
            QTest::newRow(qPrintable(role)) << role;
    }
    void svgsAreMonochromeOnTheGrid()
    {
        QFETCH(QString, role);
        const QByteArray svg = ui::icons::source(role);
        QVERIFY(!svg.isEmpty());
        QVERIFY(!svg.contains("data:"));
        QXmlStreamReader xml(svg);
        int depth = 0, accentGroups = 0, accentDepth = -1;
        bool sawRoot = false;
        const std::set<QString> elements{QStringLiteral("svg"), QStringLiteral("path"), QStringLiteral("rect"),
                                         QStringLiteral("circle"), QStringLiteral("g")};
        const std::set<QString> geometry{QStringLiteral("d"),  QStringLiteral("x"),  QStringLiteral("y"),
                                         QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("rx"),
                                         QStringLiteral("cx"), QStringLiteral("cy"), QStringLiteral("r")};
        const std::set<QString> allowed{QStringLiteral("xmlns"), QStringLiteral("viewBox"), QStringLiteral("fill"),
                                        QStringLiteral("stroke"), QStringLiteral("stroke-width"),
                                        QStringLiteral("stroke-linecap"), QStringLiteral("stroke-linejoin"),
                                        QStringLiteral("fill-rule"), QStringLiteral("id")};
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::EndElement) {
                if (--depth == accentDepth)
                    accentDepth = -1;
                continue;
            }
            if (token != QXmlStreamReader::StartElement)
                continue;
            const QString name = xml.name().toString();
            QVERIFY2(elements.contains(name), qPrintable(name)); // no image, text, style, use, gradients
            const auto attributes = xml.attributes();
            if (!sawRoot) {
                QCOMPARE(name, QStringLiteral("svg"));
                QCOMPARE(attributes.value(QLatin1String("viewBox")).toString(), QStringLiteral("0 0 16 16"));
                QCOMPARE(attributes.value(QLatin1String("width")).toString(), QStringLiteral("16"));
                QCOMPARE(attributes.value(QLatin1String("height")).toString(), QStringLiteral("16"));
                QCOMPARE(attributes.value(QLatin1String("fill")).toString(), QStringLiteral("none"));
                QCOMPARE(attributes.value(QLatin1String("stroke")).toString(), QStringLiteral("currentColor"));
                QCOMPARE(attributes.value(QLatin1String("stroke-width")).toString(), QStringLiteral("1"));
                QCOMPARE(attributes.value(QLatin1String("stroke-linecap")).toString(), QStringLiteral("round"));
                QCOMPARE(attributes.value(QLatin1String("stroke-linejoin")).toString(), QStringLiteral("round"));
                sawRoot = true;
                ++depth;
                continue;
            }
            if (name == QLatin1String("g")) {
                // the one accent layer, not nested
                QCOMPARE(attributes.value(QLatin1String("id")).toString(), QStringLiteral("accent"));
                QCOMPARE(attributes.size(), 1);
                QCOMPARE(accentDepth, -1);
                accentDepth = depth;
                ++accentGroups;
            } else {
                QVERIFY2(!attributes.hasAttribute(QLatin1String("id")), qPrintable(name));
            }
            for (const auto &attribute : attributes) {
                const QString key = attribute.qualifiedName().toString();
                const QString value = attribute.value().toString();
                QVERIFY2(geometry.contains(key) || (allowed.contains(key) && key != QLatin1String("viewBox")),
                         qPrintable(name + QStringLiteral(" ") + key)); // no style, class, opacity, transform, href
                if (key == QLatin1String("fill") || key == QLatin1String("stroke"))
                    QVERIFY2(value == QLatin1String("currentColor") || value == QLatin1String("none"), qPrintable(value));
                if (key == QLatin1String("fill-rule"))
                    QCOMPARE(value, QStringLiteral("evenodd"));
                if (geometry.contains(key))
                    for (const double v : numbers(value))
                        QVERIFY2(onGrid(v), qPrintable(key + QStringLiteral("=") + value));
            }
            // Shapes stay inside the 16-unit box, stroke included.
            if (name == QLatin1String("rect")) {
                const double x = attributes.value(QLatin1String("x")).toDouble(), y = attributes.value(QLatin1String("y")).toDouble();
                const double w = attributes.value(QLatin1String("width")).toDouble(), h = attributes.value(QLatin1String("height")).toDouble();
                QVERIFY(x >= 0 && y >= 0 && x + w <= 16 && y + h <= 16);
            }
            if (name == QLatin1String("circle")) {
                const double cx = attributes.value(QLatin1String("cx")).toDouble(), cy = attributes.value(QLatin1String("cy")).toDouble();
                const double r = attributes.value(QLatin1String("r")).toDouble();
                QVERIFY(cx - r >= 0 && cy - r >= 0 && cx + r <= 16 && cy + r <= 16);
            }
            ++depth;
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(accentGroups <= 1);
        // The manifest says whether the icon has its accent layer.
        for (const auto &value : manifest.value(QLatin1String("icons")).toArray())
            if (value.toObject().value(QLatin1String("role")).toString() == role)
                QCOMPARE(value.toObject().value(QLatin1String("accent")).toBool(), accentGroups == 1);
        // Qt SVG draws all of it (it warns about what it cannot).
        MessageCapture capture;
        QSvgRenderer renderer(svg);
        QVERIFY(renderer.isValid());
        QCOMPARE(renderer.viewBoxF(), QRectF(0, 0, 16, 16));
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            renderer.render(&painter);
        }
        QVERIFY2(MessageCapture::captured.isEmpty(), qPrintable(MessageCapture::captured.join(QLatin1Char('\n'))));
        // Nothing crosses the box: drawn at 64 px with 16 px around it, no
        // ink outside.
        QImage room(96, 96, QImage::Format_ARGB32_Premultiplied);
        room.fill(Qt::transparent);
        {
            QPainter painter(&room);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter, QRectF(16, 16, 64, 64));
        }
        for (int y = 0; y < room.height(); ++y)
            for (int x = 0; x < room.width(); ++x)
                if (x < 16 || y < 16 || x >= 80 || y >= 80)
                    QVERIFY2(qAlpha(room.pixel(x, y)) <= 8, qPrintable(QStringLiteral("ink at %1,%2").arg(x).arg(y)));
    }

    // Tinted, an icon is drawn in its colour and its accent layer in the
    // accent colour, and in nothing else.
    void tintPaintsTwoColoursOnly_data() { svgsAreMonochromeOnTheGrid_data(); }
    void tintPaintsTwoColoursOnly()
    {
        QFETCH(QString, role);
        const QImage image = ui::icons::render(role, QSize(32, 32), QColor(255, 0, 0), QColor(0, 0, 255));
        bool red = false, blue = false;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QRgb p = image.pixel(x, y);
                QCOMPARE(qGreen(p), 0);
                QVERIFY(qRed(p) + qBlue(p) <= qAlpha(p) + 1);
                red = red || qRed(p) > 128;
                blue = blue || qBlue(p) > 128;
            }
        const bool accent = ui::icons::source(role).contains("<g id=\"accent\">");
        QCOMPARE(blue, accent);
        QVERIFY(red || (accent && role == QLatin1String("document-modified"))); // the dot is its accent alone
    }

    // tools/icons/contact_sheet.py, the set's HTML contact sheet for review,
    // shows the icons in the theme layer's colours: it reads each theme's
    // background, text, accent (Light and Dark with the default preset) and
    // disabled colours from theme.cpp, as IconTheme takes them. (It read
    // K1's icon colour settings, which K2 withdrew, and exited.) Skipped
    // without a Python 3 interpreter.
    void contactSheetToolTakesTheThemeColours()
    {
        QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty())
            python = QStandardPaths::findExecutable(QStringLiteral("python"));
        if (python.isEmpty())
            QSKIP("no Python 3 interpreter");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString page = dir.filePath(QStringLiteral("index.html"));
        QProcess tool;
        tool.start(python, {QStringLiteral(HIKARI_UI_SOURCE_DIR "/../../tools/icons/contact_sheet.py"), page});
        QVERIFY(tool.waitForFinished(60000));
        QVERIFY2(tool.exitStatus() == QProcess::NormalExit && tool.exitCode() == 0,
                 qPrintable(QString::fromUtf8(tool.readAllStandardError() + tool.readAllStandardOutput())));
        QFile file(page);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString html = QString::fromUtf8(file.readAll());
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        for (const auto code : ui::theme::kCodes) {
            const auto r = ui::theme::resolve(code, defaults);
            const QString id = ui::theme::codeName(code);
            const auto hex = [](const QColor &c) { return c.name(QColor::HexRgb).toUpper(); };
            const QString css = QStringLiteral(".%1{background:%2;color:%3}.%1 .accent{color:%4}"
                                               ".%1 .hover,.%1 .hover .accent{color:%4}"
                                               ".%1 .off,.%1 .off .accent{color:%5}")
                                    .arg(id, hex(r.background), hex(r.text), hex(r.accent), hex(r.disabled));
            QVERIFY2(html.contains(css), qPrintable(id + QStringLiteral(": expected ") + css));
        }
    }

    // The icon colours are the theme's (K2): its text, accent (the accent
    // layer, and the whole icon while hovered or pressed) and disabled
    // colours. In every theme, with every accent preset, they meet WCAG
    // 1.4.11's 3:1 against the theme's surfaces (visual-language.md's bg,
    // panel, raised and field), and a disabled icon looks unlike an enabled
    // one. There are no icon colour settings any more.
    void themeIconColoursMeetContrast()
    {
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        for (const auto code : ui::theme::kCodes) {
            QList<QVariantMap> variants{{}};
            if (!ui::theme::isHighContrast(code)) {
                variants.clear();
                const bool dark = ui::theme::isDark(code);
                for (const auto &a : ui::theme::accents(dark))
                    variants << QVariantMap{{QString::fromLatin1(dark ? ui::theme::kDarkAccentSetting
                                                                      : ui::theme::kLightAccentSetting),
                                             QString::fromLatin1(a.key)}};
            }
            for (const auto &extra : variants) {
                useTheme(code, extra);
                const auto colours = ui::IconTheme::colours();
                const auto &r = themeRoles();
                QCOMPARE(colours[0], r.text);
                QCOMPARE(colours[1], r.accent);
                QCOMPARE(colours[2], r.accent);
                QCOMPARE(colours[3], r.disabled);
                const QString what = ui::theme::codeName(code) + QLatin1Char(' ') + extra.values().value(0).toString();
                for (const QColor &colour : colours)
                    for (const QColor &surface : {r.background, r.panel, r.raised, r.field})
                        QVERIFY2(ui::theme::contrastRatio(colour, surface) >= 3.0,
                                 qPrintable(what + QLatin1Char(' ') + colour.name() + QStringLiteral(" on ") + surface.name()));
                QVERIFY2(ui::theme::contrastRatio(r.disabled, r.text) >= 2.0, qPrintable(what));
            }
        }
        for (const char *id : {"icons.light.normal", "icons.dark.accent", "icons.highContrast.disabled"})
            QVERIFY2(!application::findSetting(id), id);
    }

    // The Icon item and IconButton take the theme layer's colours live (its
    // text, accent and disabled colours), in every state: a theme change, an
    // accent preset and a high-contrast pick repaint them at once.
    void iconsFollowTheThemeLive()
    {
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        useTheme(Code::Light);
        auto shown = show(R"(
import QtQuick
import Hikari.Ui
Row {
    Icon { objectName: "plain"; iconRole: "save-as" }
    Icon { objectName: "off"; iconRole: "save-as"; enabled: false }
    Icon { objectName: "over"; iconRole: "save-as"; hovered: true }
    Icon { objectName: "mirrored"; iconRole: "undo"; LayoutMirroring.enabled: true }
    Icon { objectName: "media"; iconRole: "media-play"; LayoutMirroring.enabled: true }
    IconButton { objectName: "button"; iconRole: "media-stop"; text: "Stop" }
}
)", QSize(200, 40));
        QVERIFY(shown.root);
        QVERIFY(QTest::qWaitForWindowExposed(shown.window.get()));
        const auto item = [&](const char *name) { return shown.root->findChild<QQuickItem *>(QLatin1String(name)); };
        const auto colour = [&](const char *name, const char *property = "color") {
            return item(name)->property(property).value<QColor>().name();
        };
        QCOMPARE(colour("plain"), QStringLiteral("#202832"));
        QCOMPARE(colour("plain", "accentColor"), QStringLiteral("#145c4c"));
        QCOMPARE(colour("off"), QStringLiteral("#74808b"));
        QCOMPARE(colour("off", "accentColor"), QStringLiteral("#74808b"));
        QCOMPARE(colour("over"), QStringLiteral("#145c4c"));
        QCOMPARE(colour("over", "accentColor"), QStringLiteral("#145c4c"));
        // directional icons mirror in right-to-left layouts, media symbols do not
        QVERIFY(item("mirrored")->property("mirrored").toBool());
        QVERIFY(!item("media")->property("mirrored").toBool());
        // an icon-only button keeps its name and tooltip
        auto *button = item("button");
        QCOMPARE(button->property("display").toInt(), 0); // AbstractButton.IconOnly
        QCOMPARE(button->property("tip").toString(), QStringLiteral("Stop"));
        QCOMPARE(QAccessible::queryAccessibleInterface(button)->text(QAccessible::Name), QStringLiteral("Stop"));
        auto *buttonIcon = button->property("contentItem").value<QQuickItem *>();
        QCOMPARE(buttonIcon->property("iconRole").toString(), QStringLiteral("media-stop"));
        // Another accent preset: the accent layer and the hover colour follow.
        useTheme(Code::Light, {{QStringLiteral("appearance.lightAccent"), QStringLiteral("blue")}});
        QCOMPARE(colour("plain"), QStringLiteral("#202832"));
        QCOMPARE(colour("plain", "accentColor"), ui::theme::accent(false, QStringLiteral("blue")).accent.name());
        QCOMPARE(colour("over"), ui::theme::accent(false, QStringLiteral("blue")).accent.name());
        // Dark.
        useTheme(Code::Dark);
        QCOMPARE(colour("plain"), QStringLiteral("#e8edf2"));
        QCOMPARE(colour("plain", "accentColor"), QStringLiteral("#9cdbc9"));
        QCOMPARE(colour("off"), QStringLiteral("#75818d"));
        QCOMPARE(buttonIcon->property("color").value<QColor>().name(), QStringLiteral("#e8edf2"));
        // High contrast black, then its "Text and icons" and accent picks,
        // live and painted.
        useTheme(Code::HighContrastBlack);
        QCOMPARE(colour("plain"), QStringLiteral("#ffffff"));
        QCOMPARE(colour("plain", "accentColor"), QStringLiteral("#ffff00"));
        QCOMPARE(colour("over"), QStringLiteral("#ffff00"));
        QCOMPARE(colour("off"), QStringLiteral("#8c8c8c"));
        useTheme(Code::HighContrastBlack, {{QStringLiteral("appearance.highContrastBlack.text"), QStringLiteral("#FFD000")},
                                           {QStringLiteral("appearance.highContrastBlack.accent"), QStringLiteral("#FF00FF")}});
        QCOMPARE(colour("plain"), QStringLiteral("#ffd000"));
        QCOMPARE(colour("plain", "accentColor"), QStringLiteral("#ff00ff"));
        QTRY_VERIFY([&] {
            const QImage image = shown.window->grabWindow();
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < 16 * shown.window->effectiveDevicePixelRatio(); ++x)
                    if (const QRgb p = image.pixel(x, y); qRed(p) > 160 && qBlue(p) > 160 && qGreen(p) < 100)
                        return true; // the accent layer (diagonal: no pixel is wholly covered)
            return false;
        }());
        // An unknown role draws nothing.
        item("plain")->setProperty("iconRole", QStringLiteral("no-such-icon"));
        QVERIFY(!item("plain")->property("valid").toBool());
    }

    // The controls that draw an image source (menu items, submenus, tab
    // buttons) show the set through IconTheme's image provider: the icon's
    // URL carries its colours and mirroring, retinted live, and the image is
    // the SVG drawn at the device's pixels (crisp at 150% and 200%, not
    // scaled up). A dialog in the overlay shows its icon before its title,
    // and a window takes the icon as its own (legacy SetIcon).
    void imageSourcedControlsDrawTheSet()
    {
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        useTheme(Code::Light);
        auto shown = show(R"(
import QtQuick
import QtQuick.Controls
import Hikari.Ui
Rectangle {
    id: root
    color: "lightgrey"
    property alias menu: menu
    property alias dialog: dialog
    property alias window: window
    property alias tab: tab
    ShellMenu {
        id: menu
        objectName: "menu"
        ShellMenuItem { objectName: "undoItem"; iconRole: "undo"; text: "Undo" }
        ShellMenuItem { objectName: "offItem"; iconRole: "save"; text: "Save"; enabled: false }
        ShellMenuItem { objectName: "rtlItem"; iconRole: "undo"; text: "Undo"; LayoutMirroring.enabled: true }
        ShellMenuItem { objectName: "mediaRtlItem"; iconRole: "media-play"; text: "Play"; LayoutMirroring.enabled: true }
        ShellMenu { objectName: "recent"; iconRole: "recent-subtitles"; title: "Recent" }
    }
    TabBar {
        y: 100
        IconTabButton { id: tab; iconRole: "search"; text: "Find" }
    }
    Dialog {
        id: dialog
        objectName: "dialog"
        title: "Options"
        width: 200; height: 100
        header: IconDialogHeader { objectName: "header"; iconRole: "settings"; text: dialog.title }
    }
    Window {
        id: window
        title: "History"
        Component.onCompleted: IconTheme.setWindowIcon(window, "history")
    }
}
)", QSize(320, 240));
        QVERIFY(shown.root);
        QVERIFY(QTest::qWaitForWindowExposed(shown.window.get()));
        auto *menu = shown.root->property("menu").value<QObject *>();
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QQuickItem *, shown.root), Q_ARG(QPointF, QPointF(0, 0))));
        QTRY_VERIFY(menu->property("opened").toBool());
        // The icon's source, read through QML (icon is a value type).
        const auto url = [&](QObject *item) {
            QQmlExpression expression(qmlContext(item), item, QStringLiteral("icon.source.toString()"));
            return expression.evaluate().toString();
        };
        QObject *undo = menu->findChild<QObject *>(QStringLiteral("undoItem"));
        QVERIFY(undo);
        QCOMPARE(url(undo), QStringLiteral("image://hikari-icon/undo/202832/145c4c/0"));
        QCOMPARE(url(menu->findChild<QObject *>(QStringLiteral("offItem"))), QStringLiteral("image://hikari-icon/save/74808b/74808b/0"));
        // directional icons mirror in right-to-left layouts, media symbols do not
        QCOMPARE(url(menu->findChild<QObject *>(QStringLiteral("rtlItem"))), QStringLiteral("image://hikari-icon/undo/202832/145c4c/1"));
        QCOMPARE(url(menu->findChild<QObject *>(QStringLiteral("mediaRtlItem"))),
                 QStringLiteral("image://hikari-icon/media-play/202832/145c4c/0"));
        // a highlighted item: the whole icon in the highlighted text colour;
        // the Windows style keeps the item's text colour under its faint
        // tint, so there the icon keeps its normal colours (ShellMenuItem.qml)
        undo->setProperty("highlighted", true);
        const QString highlighted = QGuiApplication::palette().color(QPalette::HighlightedText).name().mid(1);
        if (ui::ControlsStyle::name() == QLatin1String("Windows"))
            QCOMPARE(url(undo), QStringLiteral("image://hikari-icon/undo/202832/145c4c/0"));
        else
            QCOMPARE(url(undo), QStringLiteral("image://hikari-icon/undo/%1/%1/0").arg(highlighted));
        undo->setProperty("highlighted", false);
        // the submenu's item in the parent menu shows the submenu's icon
        auto *recent = menu->findChild<QObject *>(QStringLiteral("recent"));
        QVERIFY(recent);
        QCOMPARE(url(recent), QStringLiteral("image://hikari-icon/recent-subtitles/202832/145c4c/0"));
        QQuickItem *recentItem = nullptr;
        QTRY_VERIFY(recentItem = [&]() -> QQuickItem * {
            const auto count = menu->property("count").toInt();
            for (int i = 0; i < count; ++i) {
                QQuickItem *item = nullptr;
                QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
                if (item && item->property("subMenu").value<QObject *>() == recent)
                    return item;
            }
            return nullptr;
        }());
        QCOMPARE(url(recentItem), url(recent));
        // The image is the SVG drawn at the device's pixels: compare the
        // menu item's icon on screen with the icon rendered at that size.
        auto *undoItem = qobject_cast<QQuickItem *>(undo);
        QQuickItem *image = nullptr;
        QTRY_VERIFY(image = [&]() -> QQuickItem * {
            for (auto *child : undoItem->findChildren<QQuickItem *>())
                if (QByteArray(child->metaObject()->className()).contains("IconImage") && child->property("status").toInt() == 1)
                    return child;
            return nullptr;
        }());
        QQuickWindow *window = undoItem->window();
        const qreal dpr = window->effectiveDevicePixelRatio();
        const int side = qRound(16 * dpr);
        QCOMPARE(image->property("sourceSize").toSize(), QSize(16, 16));
        QImage screen;
        QTRY_VERIFY((screen = window->grabWindow(), !screen.isNull() && screen.width() > 0));
        const QPointF at = image->mapToScene(QPointF((image->width() - 16) / 2, (image->height() - 16) / 2));
        const QColor background(screen.pixel(qRound(at.x() * dpr) - 1, qRound(at.y() * dpr) - 1));
        const QImage expected = drawnFromSource(QStringLiteral("undo"), side, QColor(0x20, 0x28, 0x32), QColor(0x14, 0x5C, 0x4C));
        int worst = 0;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const QColor want = composite(expected.pixel(x, y), background);
                const QColor got(screen.pixel(qRound(at.x() * dpr) + x, qRound(at.y() * dpr) + y));
                worst = std::max({worst, std::abs(want.red() - got.red()), std::abs(want.green() - got.green()),
                                  std::abs(want.blue() - got.blue())});
            }
        QVERIFY2(worst <= 8, qPrintable(QString::number(worst)));
        // The theme turns dark: the URLs follow at once.
        useTheme(Code::Dark);
        QCOMPARE(url(undo), QStringLiteral("image://hikari-icon/undo/e8edf2/9cdbc9/0"));
        QCOMPARE(url(recent), QStringLiteral("image://hikari-icon/recent-subtitles/e8edf2/9cdbc9/0"));
        useTheme(Code::Light);
        QMetaObject::invokeMethod(menu, "close");
        // the tab button
        auto *tab = shown.root->property("tab").value<QQuickItem *>();
        QVERIFY(tab);
        QCOMPARE(url(tab), QStringLiteral("image://hikari-icon/search/202832/145c4c/0"));
        QCOMPARE(tab->property("text").toString(), QStringLiteral("Find")); // drawn beside the icon
        // the dialog's title with its icon
        auto *dialog = shown.root->property("dialog").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *header = dialog->findChild<QQuickItem *>(QStringLiteral("header"));
        QVERIFY(header);
        QVERIFY(header->isVisible());
        auto *dialogIcon = header->findChild<QQuickItem *>(QStringLiteral("dialogIcon"));
        QVERIFY(dialogIcon && dialogIcon->property("valid").toBool());
        QCOMPARE(dialogIcon->property("iconRole").toString(), QStringLiteral("settings"));
        QVERIFY(dialogIcon->x() + dialogIcon->width() <= header->property("leftPadding").toReal());
        QMetaObject::invokeMethod(dialog, "close");
        // the window's icon, retinted with the theme
        auto *history = shown.root->property("window").value<QWindow *>();
        QVERIFY(history);
        const auto iconPixel = [&] {
            const QImage icon = history->icon().pixmap(QSize(32, 32)).toImage();
            for (int y = 0; y < icon.height(); ++y)
                for (int x = 0; x < icon.width(); ++x)
                    if (qAlpha(icon.pixel(x, y)) == 255)
                        return QColor(icon.pixel(x, y)).name();
            return QString();
        };
        QVERIFY(!history->icon().isNull());
        const QString light = iconPixel();
        QVERIFY(light == QLatin1String("#202832") || light == QLatin1String("#145c4c"));
        useTheme(Code::Dark);
        QTRY_VERIFY(iconPixel() == QLatin1String("#e8edf2") || iconPixel() == QLatin1String("#9cdbc9"));
    }

    // icons.md, "Mirroring": icons that show navigation or reading order
    // mirror in right-to-left layouts (undo, redo, list and text-line icons,
    // the session and search arrows); media transport, time, frame and data
    // symbols do not. The manifest's mirror flags are that set, and the
    // Icon item reads them.
    void mirrorFlagsFollowTheRule()
    {
        const QSet<QString> expected{
            // undo and redo
            QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("undo-to-last-save"),
            // list icons
            QStringLiteral("sort"), QStringLiteral("sort-selected"), QStringLiteral("select-lines"),
            // text-line icons
            QStringLiteral("editor"), QStringLiteral("script-properties"), QStringLiteral("spellchecker"),
            QStringLiteral("view-only-subs"),
            // the session and search arrows
            QStringLiteral("last-session"), QStringLiteral("find-replace")};
        QSet<QString> mirrored;
        for (const auto &value : manifest.value(QLatin1String("icons")).toArray()) {
            const auto icon = value.toObject();
            const QString role = icon.value(QLatin1String("role")).toString();
            const bool mirror = icon.value(QLatin1String("mirror")).toBool();
            QCOMPARE(ui::icons::mirrors(role), mirror);
            if (mirror)
                mirrored.insert(role);
            // no media transport, time or frame symbol mirrors: nothing on
            // the video transport, the full-screen controls or the audio box
            for (const auto &surface : icon.value(QLatin1String("surfaces")).toArray())
                if (const QString name = surface.toString(); name == QLatin1String("video-transport")
                                                             || name == QLatin1String("video-fullscreen")
                                                             || name == QLatin1String("audio-box"))
                    QVERIFY2(!mirror, qPrintable(role + QStringLiteral(" on ") + name));
            for (const char *symbol : {"media-", "frame", "time", "play"})
                if (role.contains(QLatin1String(symbol)))
                    QVERIFY2(!mirror, qPrintable(role));
        }
        QCOMPARE(mirrored, expected);
    }

    // A mirrored icon is painted flipped left to right; an icon that does
    // not mirror paints the same in a right-to-left layout.
    void mirroredIconsPaintFlipped()
    {
        // (the Light theme's dark icons on white)
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        useTheme(Code::Light);
        auto shown = show(R"(
import QtQuick
import Hikari.Ui
Rectangle {
    color: "white"
    Icon { objectName: "undo"; iconRole: "undo"; size: 32; x: 0 }
    Icon { objectName: "undoRtl"; iconRole: "undo"; size: 32; x: 40; LayoutMirroring.enabled: true }
    Icon { objectName: "play"; iconRole: "media-play"; size: 32; x: 80 }
    Icon { objectName: "playRtl"; iconRole: "media-play"; size: 32; x: 120; LayoutMirroring.enabled: true }
}
)", QSize(160, 32));
        QVERIFY(shown.root);
        QVERIFY(QTest::qWaitForWindowExposed(shown.window.get()));
        const auto item = [&](const char *name) { return shown.root->findChild<QQuickItem *>(QLatin1String(name)); };
        QVERIFY(item("undoRtl")->property("mirrored").toBool());
        QVERIFY(!item("playRtl")->property("mirrored").toBool());
        QImage window;
        QTRY_VERIFY((window = shown.window->grabWindow(), !window.isNull() && window.width() > 0));
        const qreal dpr = shown.window->effectiveDevicePixelRatio();
        const int side = qRound(32 * dpr);
        const auto cell = [&](int x) { return window.copy(qRound(x * dpr), 0, side, side).convertToFormat(QImage::Format_RGB32); };
        const auto difference = [](const QImage &a, const QImage &b) {
            int worst = 0;
            for (int y = 0; y < a.height(); ++y)
                for (int x = 0; x < a.width(); ++x) {
                    const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
                    worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                      std::abs(qBlue(p) - qBlue(q))});
                }
            return worst;
        };
        const QImage undo = cell(0), undoRtl = cell(40), play = cell(80), playRtl = cell(120);
        QVERIFY(difference(undo, undo.flipped(Qt::Horizontal)) > 100); // the drawing is not symmetric
        QVERIFY(difference(undo, undoRtl) > 100);
        QVERIFY2(difference(undoRtl, undo.flipped(Qt::Horizontal)) <= 8,
                 qPrintable(QString::number(difference(undoRtl, undo.flipped(Qt::Horizontal)))));
        QVERIFY(difference(play, play.flipped(Qt::Horizontal)) > 100);
        QCOMPARE(difference(playRtl, play), 0);
    }

    // The rendering check and contact sheets: the whole set drawn by the
    // Icon item on each theme's background at this process's scale,
    // every icon equal to its SVG file drawn at the device's pixels (crisp,
    // not scaled up from 16 px) in the appearance's colour, its accent layer
    // in the accent colour. The expected icon is drawn from the source
    // tree's file, recoloured through SVG's color property rather than the
    // item's tint (drawnFromSource), so a wrong drawing, a wrong tint or
    // colours in the wrong layer fail here; whether the drawings look right
    // rests on the user's review of the contact sheets written here.
    void contactSheets()
    {
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        const QStringList roles = ui::icons::roles();
        constexpr int columns = 8, cellWidth = 168, cellHeight = 44;
        const int rows = int((roles.size() + columns - 1) / columns);
        const QString outDir = qEnvironmentVariable("HIKARI_ICON_SHEET_DIR");
        for (const auto code : ui::theme::kCodes) {
            useTheme(code);
            const QColor background = themeRoles().background;
            const QColor text = themeRoles().text;
            QByteArray qml = R"(
import QtQuick
import Hikari.Ui
Rectangle {
    id: sheet
    property var roles: []
    color: "BG"
    width: COLS * CW
    height: ROWS * CH
    Grid {
        columns: COLS
        Repeater {
            model: sheet.roles
            delegate: Item {
                required property string modelData
                width: CW
                height: CH
                Icon { objectName: "icon_" + parent.modelData; iconRole: parent.modelData; x: 8; y: 6 }
                Icon { iconRole: parent.modelData; size: 32; x: 32; y: 6 }
                Text { x: 72; y: 6; width: CW - 76; text: parent.modelData; color: "FG"; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
            }
        }
    }
}
)";
            qml.replace("BG", background.name().toLatin1()).replace("FG", text.name().toLatin1());
            qml.replace("COLS", QByteArray::number(columns)).replace("ROWS", QByteArray::number(rows));
            qml.replace("CW", QByteArray::number(cellWidth)).replace("CH", QByteArray::number(cellHeight));
            auto shown = show(qml, QSize(columns * cellWidth, rows * cellHeight));
            QVERIFY(shown.root);
            shown.root->setProperty("roles", roles);
            QVERIFY(QTest::qWaitForWindowExposed(shown.window.get()));
            QImage sheet;
            QTRY_VERIFY((sheet = shown.window->grabWindow(), !sheet.isNull() && sheet.width() > 0));
            const qreal dpr = shown.window->effectiveDevicePixelRatio();
            QCOMPARE(sheet.width(), qRound(columns * cellWidth * dpr));
            const QColor colour = themeRoles().text;
            const QColor accent = themeRoles().accent;
            int worst = 0;
            QString worstRole;
            for (int i = 0; i < roles.size(); ++i) {
                // the 16-unit icon at (8, 6) of its cell, in device pixels
                const int px = qRound((i % columns * cellWidth + 8) * dpr), py = qRound((i / columns * cellHeight + 6) * dpr);
                const int side = qRound(16 * dpr);
                const QImage expected = drawnFromSource(roles[i], side, colour, accent);
                for (int y = 0; y < side; ++y)
                    for (int x = 0; x < side; ++x) {
                        const QColor want = composite(expected.pixel(x, y), background);
                        const QColor got(sheet.pixel(px + x, py + y));
                        const int d = std::max({std::abs(want.red() - got.red()), std::abs(want.green() - got.green()),
                                                std::abs(want.blue() - got.blue())});
                        if (d > worst) {
                            worst = d;
                            worstRole = roles[i];
                        }
                    }
            }
            QVERIFY2(worst <= 8, qPrintable(QStringLiteral("%1 differs by %2").arg(worstRole).arg(worst)));
            if (!outDir.isEmpty()) {
                QDir().mkpath(outDir);
                const QString name = QStringLiteral("%1/k1-%2-%3.png")
                                         .arg(outDir, ui::theme::codeName(code))
                                         .arg(qRound(dpr * 100));
                QVERIFY(sheet.save(name));
            }
        }
    }
};

QTEST_MAIN(IconTests)

#include "icon_tests.moc"
