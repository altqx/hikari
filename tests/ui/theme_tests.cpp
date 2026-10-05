// K2: the Hikari theme layer (src/ui/theme.h, docs/qt/ux/visual-language.md,
// the user's 2026-10-05 decision after MuseScore 4's appearance model,
// docs/research/musescore-appearance.md). The four themes' role tables (Light
// and Dark the spec's tokens, High contrast black the spec's high-contrast
// column), the seven accent presets per mode and their measured WCAG
// contrast, following the system's light or dark scheme (never high
// contrast), the high-contrast pickers, the content colours fixed per theme
// (Dark keeps legacy's dark theme, config.cpp:405-472), the live preview and
// the application palette, and the Theme singleton QML reads. With
// HIKARI_THEME_SHEET_DIR set the swatch sheet for the user's review is
// written there (k2-accents.png) with the measured ratios (k2-contrast.txt).

#include "settings_store.h"
#include "theme.h"

#include "hikari/application/audio_display.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QTextStream>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cmath>
#include <set>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::theme::Code;
using ui::theme::contrastRatio;

namespace {

QColor hex(QRgb rgb)
{
    return QColor::fromRgb(rgb);
}

ui::theme::Roles roles(Code code, const ui::theme::Choice &choice = {})
{
    return ui::theme::resolve(code, choice);
}

std::array<QColor, 4> surfaces(const ui::theme::Roles &r)
{
    return {r.background, r.panel, r.raised, r.field};
}

double lowest(const QColor &colour, const ui::theme::Roles &r)
{
    double out = 21;
    for (const QColor &s : surfaces(r))
        out = std::min(out, contrastRatio(colour, s));
    return out;
}

// Hue distance in degrees (0 to 180).
double hueDistance(const QColor &a, const QColor &b)
{
    const double d = std::abs(a.hsvHueF() - b.hsvHueF()) * 360;
    return std::min(d, 360 - d);
}

QString name(const QColor &c)
{
    return c.name(QColor::HexRgb).toUpper();
}

} // namespace

class ThemeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // The application's controls style, chosen before any engine loads
        // the controls (src/app/composition.cpp does the same).
        ui::theme::chooseControlsStyle();
    }

    void init()
    {
        ui::theme::useSettings(nullptr);
        ui::theme::endPreview();
        ui::theme::forceSystemScheme(Qt::ColorScheme::Unknown);
    }

    // visual-language.md, "Semantic appearance tokens": Light and Dark with
    // the default (green) accent are the spec's columns; High contrast black
    // is the spec's high-contrast column.
    void rolesAreTheSpecTokens()
    {
        const auto dark = roles(Code::Dark);
        QCOMPARE(surfaces(dark), (std::array{hex(0x171B20), hex(0x20262D), hex(0x29313A), hex(0x171D24)}));
        QCOMPARE(dark.text, hex(0xE8EDF2));
        QCOMPARE(dark.muted, hex(0xA5B1BD));
        QCOMPARE(dark.line, hex(0x414B57));
        QCOMPARE(dark.accent, hex(0x9CDBC9));
        QCOMPARE(dark.onAccent, hex(0x102C24));
        QCOMPARE(dark.select, hex(0x304C47));
        QCOMPARE(dark.focus, hex(0xF9D784));
        QCOMPARE(dark.danger, hex(0xFFADAD));
        const auto light = roles(Code::Light);
        QCOMPARE(surfaces(light), (std::array{hex(0xE5E9EC), hex(0xF9FAFB), hex(0xEDF0F3), hex(0xFFFFFF)}));
        QCOMPARE(light.text, hex(0x202832));
        QCOMPARE(light.muted, hex(0x526170));
        QCOMPARE(light.line, hex(0xAAB5BE));
        QCOMPARE(light.accent, hex(0x145C4C));
        QCOMPARE(light.onAccent, hex(0xFFFFFF));
        QCOMPARE(light.select, hex(0xD6EBE4));
        QCOMPARE(light.focus, hex(0x8D4200));
        QCOMPARE(light.danger, hex(0xA51F31));
        const auto black = roles(Code::HighContrastBlack, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
        QCOMPARE(surfaces(black), (std::array{hex(0x000000), hex(0x080808), hex(0x151515), hex(0x000000)}));
        QCOMPARE(black.text, hex(0xFFFFFF));
        QCOMPARE(black.muted, hex(0xEEEEEE));
        QCOMPARE(black.line, hex(0xFFFFFF));
        QCOMPARE(black.accent, hex(0xFFFF00));
        QCOMPARE(black.onAccent, hex(0x000000));
        QCOMPARE(black.select, hex(0x253F60));
        QCOMPARE(black.focus, hex(0x00FFFF));
        QCOMPARE(black.danger, hex(0xFFADAD));
        // The success text is legacy FontCollector's "#008000" in Light and
        // Dark (FontCollector.cpp:872, 978: the same in every legacy theme).
        QCOMPARE(light.success, hex(0x008000));
        QCOMPARE(dark.success, hex(0x008000));
        // Every role is set in every theme.
        for (const auto code : ui::theme::kCodes) {
            const auto r = roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
            for (std::size_t i = 0; i < ui::theme::kRoleNames.size(); ++i)
                QVERIFY2(ui::theme::role(r, i).isValid(), ui::theme::kRoleNames[i]);
        }
    }

    // The roles' measured contrast (WCAG 2.x) on the theme's four surfaces:
    // text, secondary text, danger and warning text at least 4.5:1; the
    // accent, focus, icons and disabled at least 3:1 (WCAG 1.4.11); text on
    // the accent and on the selected background at least 4.5:1. High
    // contrast holds every text role to 7:1 apart from disabled. The one
    // exception is legacy's success green in Light and Dark (above).
    void rolesMeetContrast()
    {
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        for (const auto code : ui::theme::kCodes) {
            const auto r = roles(code, defaults);
            const QString theme = ui::theme::codeName(code);
            const bool hc = ui::theme::isHighContrast(code);
            const double textMinimum = hc ? 7.0 : 4.5;
            for (const auto &[role, colour] : {std::pair{"text", r.text}, std::pair{"muted", r.muted},
                                               std::pair{"danger", r.danger}, std::pair{"warning", r.warning}})
                QVERIFY2(lowest(colour, r) >= textMinimum, qPrintable(theme + QLatin1Char(' ') + QLatin1String(role)));
            QVERIFY2(lowest(r.accent, r) >= (hc ? 7.0 : 4.5), qPrintable(theme));
            QVERIFY2(lowest(r.focus, r) >= 3.0, qPrintable(theme));
            QVERIFY2(lowest(r.disabled, r) >= 3.0, qPrintable(theme));
            QVERIFY2(contrastRatio(r.onAccent, r.accent) >= textMinimum, qPrintable(theme));
            QVERIFY2(contrastRatio(r.text, r.select) >= textMinimum, qPrintable(theme));
            QVERIFY2(contrastRatio(r.disabled, r.text) >= 2.0, qPrintable(theme)); // disabled looks unlike enabled
            if (hc) {
                QVERIFY2(lowest(r.success, r) >= 7.0, qPrintable(theme));
                QVERIFY2(lowest(r.line, r) >= 7.0, qPrintable(theme)); // boundaries survive without shadows
            }
        }
    }

    // Seven presets per mode, by hue, in the same order and keys in Light and
    // Dark; the default (the fourth) is the spec's green. Each meets 4.5:1
    // against every surface of its mode, carries readable text (4.5:1) and a
    // readable selected background, and keeps its focus colour apart from it
    // (3:1 on the surfaces, at least 60 degrees of hue away).
    void accentPresetsMeetContrast()
    {
        const QStringList keys{"red", "orange", "gold", "green", "blue", "purple", "pink"};
        for (const bool dark : {false, true}) {
            const auto list = ui::theme::accents(dark);
            QCOMPARE(list.size(), std::size_t(7));
            QStringList seen;
            for (const auto &a : list)
                seen << QLatin1String(a.key);
            QCOMPARE(seen, keys);
            QCOMPARE(QLatin1String(list[3].key), QLatin1String(ui::theme::kDefaultAccent));
            const Code code = dark ? Code::Dark : Code::Light;
            for (const auto &a : list) {
                ui::theme::Choice choice;
                (dark ? choice.darkAccent : choice.lightAccent) = QLatin1String(a.key);
                const auto r = roles(code, choice);
                const QString what = QLatin1String(dark ? "dark " : "light ") + QLatin1String(a.key);
                QCOMPARE(r.accent, a.accent);
                QCOMPARE(r.select, a.select);
                QVERIFY2(lowest(a.accent, r) >= 4.5, qPrintable(what));
                QVERIFY2(contrastRatio(a.onAccent, a.accent) >= 4.5, qPrintable(what));
                QVERIFY2(contrastRatio(r.text, a.select) >= 4.5, qPrintable(what));
                QVERIFY2(lowest(a.focus, r) >= 3.0, qPrintable(what));
                QVERIFY2(hueDistance(a.focus, a.accent) >= 60, qPrintable(what));
                // the selected background stays a quiet tint (the spec green's)
                QVERIFY2(contrastRatio(a.select, r.panel) < 2.0, qPrintable(what));
                // the other mode's roles and the theme's own roles stay put
                QCOMPARE(r.text, roles(code).text);
                QCOMPARE(surfaces(r), surfaces(roles(code)));
            }
        }
        // the spec's green
        QCOMPARE(ui::theme::accent(false, QStringLiteral("green")).accent, hex(0x145C4C));
        QCOMPARE(ui::theme::accent(true, QStringLiteral("green")).accent, hex(0x9CDBC9));
        // an unknown key is the default
        QCOMPARE(QLatin1String(ui::theme::accent(true, QStringLiteral("teal")).key), QLatin1String("green"));
    }

    // Light and Dark each remember their own accent; high contrast ignores
    // them (its accent is the picker's).
    void eachModeKeepsItsAccent()
    {
        ui::theme::Choice choice;
        choice.lightAccent = QStringLiteral("blue");
        choice.darkAccent = QStringLiteral("pink");
        QCOMPARE(roles(Code::Light, choice).accent, ui::theme::accent(false, QStringLiteral("blue")).accent);
        QCOMPARE(roles(Code::Dark, choice).accent, ui::theme::accent(true, QStringLiteral("pink")).accent);
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        choice.picks = defaults.picks;
        QCOMPARE(roles(Code::HighContrastBlack, choice).accent, hex(0xFFFF00));
        QCOMPARE(roles(Code::HighContrastWhite, choice).accent, hex(0x0037B3));
    }

    // Follow system theme (MuseScore's doSetIsDarkMode): Light or Dark from
    // the platform's colour scheme, keeping high contrast on its matching
    // side; never turning high contrast on or off. Off, or with no scheme
    // known, the chosen theme shows.
    void followingTheSystemSwitchesLightAndDarkOnly()
    {
        using S = Qt::ColorScheme;
        const auto shown = [](Code chosen, bool follow, S scheme) {
            ui::theme::Choice choice;
            choice.theme = chosen;
            choice.followSystem = follow;
            return ui::theme::effective(choice, scheme);
        };
        QCOMPARE(shown(Code::Dark, true, S::Light), Code::Light);
        QCOMPARE(shown(Code::Light, true, S::Dark), Code::Dark);
        QCOMPARE(shown(Code::Light, true, S::Light), Code::Light);
        QCOMPARE(shown(Code::HighContrastBlack, true, S::Light), Code::HighContrastWhite);
        QCOMPARE(shown(Code::HighContrastWhite, true, S::Dark), Code::HighContrastBlack);
        QCOMPARE(shown(Code::Dark, true, S::Unknown), Code::Dark);
        QCOMPARE(shown(Code::Dark, false, S::Light), Code::Dark);
        QCOMPARE(shown(Code::HighContrastBlack, false, S::Light), Code::HighContrastBlack);
        // Live: the platform's scheme changes, the theme follows at once.
        ui::SettingsStore store;
        ui::theme::useSettings(&store);
        int changes = 0;
        QObject context;
        ui::theme::onChanged(&context, [&] { ++changes; });
        QCOMPARE(ui::theme::current().code, Code::Dark); // the default, following, scheme unknown
        ui::theme::forceSystemScheme(S::Light);
        QCOMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(changes, 1);
        ui::theme::forceSystemScheme(S::Dark);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        QCOMPARE(changes, 2);
        // following off: the platform's scheme no longer matters
        store.setValue(QLatin1String(ui::theme::kFollowSystemSetting), false);
        ui::theme::forceSystemScheme(S::Light);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        ui::theme::useSettings(nullptr);
    }

    // The high-contrast pickers: accent, text-and-icons and border, freely
    // chosen, each high-contrast theme its own; every other role fixed; text
    // on a picked accent turns black or white to stay readable. An
    // unreadable value is the default. Light and Dark ignore them.
    void highContrastPickersSetThreeRoles()
    {
        QVariantMap values{{QStringLiteral("appearance.highContrastBlack.accent"), QStringLiteral("#000080")},
                           {QStringLiteral("appearance.highContrastBlack.text"), QStringLiteral("#FFD000")},
                           {QStringLiteral("appearance.highContrastBlack.border"), QStringLiteral("#00FF00")},
                           {QStringLiteral("appearance.highContrastWhite.accent"), QStringLiteral("not a colour")}};
        const auto choice = ui::theme::choiceFrom([&](const char *id) { return values.value(QLatin1String(id)); });
        const auto base = roles(Code::HighContrastBlack, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
        const auto black = roles(Code::HighContrastBlack, choice);
        QCOMPARE(black.accent, hex(0x000080));
        QCOMPARE(black.text, hex(0xFFD000));
        QCOMPARE(black.line, hex(0x00FF00));
        QCOMPARE(black.onAccent, hex(0xFFFFFF)); // black on navy would not read
        QCOMPARE(black.background, base.background);
        QCOMPARE(black.select, base.select);
        QCOMPARE(black.focus, base.focus);
        QCOMPARE(black.disabled, base.disabled);
        // High contrast white keeps its own (the unreadable one: its default)
        QCOMPARE(roles(Code::HighContrastWhite, choice).accent, hex(0x0037B3));
        QCOMPARE(roles(Code::HighContrastWhite, choice).text, hex(0x000000));
        // Light and Dark ignore the pickers
        QCOMPARE(roles(Code::Dark, choice), roles(Code::Dark));
        // The pickers' settings and defaults.
        QCOMPARE(QLatin1String(ui::theme::pickSetting(Code::HighContrastWhite, ui::theme::Pick::Border)),
                 QLatin1String("appearance.highContrastWhite.border"));
        QCOMPARE(ui::theme::pickDefault(Code::HighContrastBlack, ui::theme::Pick::Accent), hex(0xFFFF00));
        QCOMPARE(ui::theme::pickDefault(Code::HighContrastWhite, ui::theme::Pick::Text), hex(0x000000));
    }

    // Unknown or missing values are the defaults: the Dark theme (legacy's
    // default theme), following the system, the green accents.
    void choiceDefaults()
    {
        const auto choice = ui::theme::choiceFrom([](const char *id) {
            return QLatin1String(id) == QLatin1String("appearance.theme") ? QVariant(QStringLiteral("LightSentro"))
                                                                         : QVariant(QStringLiteral("x"));
        });
        QCOMPARE(choice.theme, Code::Dark);
        QCOMPARE(choice.lightAccent, QStringLiteral("green"));
        QCOMPARE(choice.darkAccent, QStringLiteral("green"));
        QCOMPARE(ui::theme::choiceFrom([](const char *) { return QVariant(); }).followSystem, true);
        for (const auto code : ui::theme::kCodes)
            QCOMPARE(ui::theme::codeFromName(ui::theme::codeName(code)), std::optional(code));
        QVERIFY(!ui::theme::codeFromName(QStringLiteral("highContrast")));
    }

    // Content colours are fixed per theme; Dark keeps legacy's dark theme
    // (config.cpp:405-472, LoadDefaultColors(true)): the audio display's
    // (the AudioDisplayOptions defaults, the spectrum's AUDIO_SPECTRUM_*
    // among them), the Grid's GRID_COMPARISON_* and GRID_SPELLCHECKER; Light
    // keeps legacy's light comparison and spelling colours. The
    // selection-type marks take the accent: AUDIO_SELECTION_BACKGROUND (and
    // _MODIFIED) at legacy's 0x37 alpha, AUDIO_WAVEFORM_SELECTED, and
    // GRID_SELECTION's blend at legacy's alpha 75 (config.cpp:422).
    void contentColoursAreFixedPerTheme()
    {
        const application::AudioDisplayOptions legacy;
        const auto dark = ui::theme::content(Code::Dark, roles(Code::Dark));
        auto expected = legacy;
        expected.selectionBackground = expected.selectionModified = 0x379CDBC9;
        expected.waveformSelected = 0xFF9CDBC9;
        auto actual = legacy;
        ui::theme::applyAudioColours(actual, dark.audio);
        QCOMPARE(actual.background, 0xFF36393Eu); // AUDIO_BACKGROUND
        QCOMPARE(actual.spectrumEcho, 0xFF674FD7u);
        using O = application::AudioDisplayOptions;
        for (const auto member : {&O::background, &O::lineStart, &O::lineEnd, &O::inactiveBoundary, &O::cursor,
                                  &O::secondBoundaries, &O::keyframe, &O::selectionBackground, &O::selectionModified,
                                  &O::inactiveBackground, &O::waveform, &O::waveformInactive, &O::waveformModified,
                                  &O::waveformSelected, &O::timescaleBackground, &O::timescaleText, &O::spectrumBackground,
                                  &O::spectrumEcho, &O::spectrumInner, &O::lineBoundaryMark, &O::syllableBoundaries,
                                  &O::syllableText})
            QCOMPARE(actual.*member, expected.*member);
        QCOMPARE(dark.comparison, (std::array{hex(0x2700FF), hex(0x272B32), hex(0x3A3E45), hex(0x003176), hex(0x3662A1)}));
        QCOMPARE(dark.spellcheck, hex(0x940000));
        const auto light = ui::theme::content(Code::Light, roles(Code::Light));
        QCOMPARE(light.comparison, (std::array{hex(0xFFFFFF), hex(0xFF000C), hex(0xB7AC00), hex(0x9C0000), hex(0x817900)}));
        QCOMPARE(light.spellcheck, hex(0xFF6968));
        // The accent moves the selection marks only.
        ui::theme::Choice choice;
        choice.darkAccent = QStringLiteral("blue");
        const auto blue = ui::theme::content(Code::Dark, roles(Code::Dark, choice));
        const QColor accent = ui::theme::accent(true, QStringLiteral("blue")).accent;
        QCOMPARE(blue.audio.waveformSelected, accent.rgba());
        QCOMPARE(blue.audio.selectionBackground, (accent.rgba() & 0x00FFFFFFu) | 0x37000000u);
        QCOMPARE(blue.comparisonSelection, QColor(accent.red(), accent.green(), accent.blue(), 75));
        QCOMPARE(blue.audio.background, dark.audio.background);
        QCOMPARE(blue.audio.cursor, dark.audio.cursor);
        QCOMPARE(blue.comparison, dark.comparison);
        // Each theme's display reads: the waveform and the timescale text on
        // their backgrounds (3:1), the high-contrast ones at 4.5:1.
        for (const auto code : {Code::Light, Code::HighContrastWhite, Code::HighContrastBlack}) {
            const auto c = ui::theme::content(code, roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); })));
            const double minimum = ui::theme::isHighContrast(code) ? 4.5 : 3.0;
            const auto colour = [](std::uint32_t argb) { return QColor::fromRgba(argb); };
            QVERIFY2(contrastRatio(colour(c.audio.waveform), colour(c.audio.background)) >= minimum,
                     qPrintable(ui::theme::codeName(code)));
            QVERIFY2(contrastRatio(colour(c.audio.timescaleText), colour(c.audio.timescaleBackground)) >= 4.5,
                     qPrintable(ui::theme::codeName(code)));
            QVERIFY2(contrastRatio(colour(c.audio.cursor), colour(c.audio.background)) >= 3.0,
                     qPrintable(ui::theme::codeName(code)));
        }
    }

    // With a profile the theme follows it live: the application palette the
    // controls draw with is the theme's, a change of an appearance setting
    // retints at once (others do not count), and the Options dialog's
    // preview shows staged values until it ends.
    void profilePreviewAndPalette()
    {
        ui::SettingsStore store;
        const QPalette before = QGuiApplication::palette();
        auto restore = qScopeGuard([&] {
            ui::theme::useSettings(nullptr);
            QGuiApplication::setPalette(before);
        });
        ui::theme::useSettings(&store);
        QObject context;
        int changes = 0;
        ui::theme::onChanged(&context, [&] { ++changes; });
        const auto window = [] { return QGuiApplication::palette().color(QPalette::Window); };
        QCOMPARE(window(), hex(0x20262D)); // Dark's panel
        QCOMPARE(QGuiApplication::palette().color(QPalette::Highlight), hex(0x9CDBC9));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText), hex(0x75818D));
        store.setValue(QStringLiteral("audio.spectrumOn"), true);
        QCOMPARE(changes, 0);
        store.setValue(QStringLiteral("appearance.darkAccent"), QStringLiteral("gold"));
        QCOMPARE(changes, 1);
        QCOMPARE(QGuiApplication::palette().color(QPalette::Accent), ui::theme::accent(true, QStringLiteral("gold")).accent);
        // The preview: staged values over the profile's, nothing saved.
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("light")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(window(), hex(0xF9FAFB));
        QCOMPARE(store.value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("dark"));
        ui::theme::endPreview();
        QCOMPARE(ui::theme::current().code, Code::Dark);
        QCOMPARE(window(), hex(0x20262D));
        QCOMPARE(ui::theme::current().roles.accent, ui::theme::accent(true, QStringLiteral("gold")).accent);
        // The profile goes: the defaults again.
        ui::theme::useSettings(nullptr);
        QCOMPARE(ui::theme::current().roles.accent, hex(0x9CDBC9));
    }

    // The Theme singleton QML reads: the roles, the theme's state, the
    // Appearance page's lists (the themes, a mode's accents with their
    // lowest contrast, the pickers, the roles a theme resolves to), live.
    void themeSingletonExposesTheRoles()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import Hikari.Ui
QtObject {
    property color accent: Theme.accent
    property color panel: Theme.panel
    property string code: Theme.code
    property bool dark: Theme.dark
    property bool highContrast: Theme.highContrast
    property var themes: Theme.themes()
    property var accents: Theme.accents(false)
    property var picks: Theme.picks("highContrastWhite")
    property var lightRoles: Theme.rolesOf("light", {"appearance.lightAccent": "purple"})
}
)", QUrl(QStringLiteral("qrc:/k2-test.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("accent").value<QColor>(), hex(0x9CDBC9));
        QCOMPARE(object->property("code").toString(), QStringLiteral("dark"));
        QVERIFY(object->property("dark").toBool());
        QVERIFY(!object->property("highContrast").toBool());
        const auto themes = object->property("themes").toList();
        QCOMPARE(themes.size(), 4);
        QCOMPARE(themes[2].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("High contrast white"));
        const auto accents = object->property("accents").toList();
        QCOMPARE(accents.size(), 7);
        QCOMPARE(accents[3].toMap().value(QStringLiteral("accent")).value<QColor>(), hex(0x145C4C));
        QVERIFY(accents[3].toMap().value(QStringLiteral("contrast")).toDouble() > 6.4);
        const auto picks = object->property("picks").toList();
        QCOMPARE(picks.size(), 3);
        QCOMPARE(picks[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Text and icons"));
        QCOMPARE(picks[0].toMap().value(QStringLiteral("defaultColour")).toString(), QStringLiteral("#0037B3"));
        QCOMPARE(object->property("lightRoles").toMap().value(QStringLiteral("accent")).value<QColor>(),
                 ui::theme::accent(false, QStringLiteral("purple")).accent);
        // live
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("highContrastBlack")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(object->property("accent").value<QColor>(), hex(0xFFFF00));
        QCOMPARE(object->property("panel").value<QColor>(), hex(0x080808));
        QVERIFY(object->property("highContrast").toBool());
    }

    // The controls style (src/ui/style): every control outline in the
    // theme's boundary colour (the line role, the high-contrast border pick),
    // live; Fusion's own, its window colour darkened 140%, measures about
    // 1.1:1 against the Dark and High contrast black panels. The focused
    // outline stays Fusion's accent-derived one.
    void controlsOutlineInTheBoundaryColour()
    {
        QCOMPARE(QQuickStyle::name(), QStringLiteral("HikariStyle"));
        ui::SettingsStore store;
        const QPalette before = QGuiApplication::palette();
        auto restore = qScopeGuard([&] {
            ui::theme::useSettings(nullptr);
            QGuiApplication::setPalette(before);
        });
        ui::theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls
Window {
    width: 400; height: 400
    property var outlines: [button.background.border.color, toolButton.background.border.color,
        field.background.border.color, area.background.border.color, spin.background.border.color,
        combo.background.border.color, check.indicator.border.color, radio.indicator.border.color,
        group.background.border.color, frame.background.border.color, menu.background.border.color,
        tab.background.border.color, tabBar.background.children[0].color]
    // A control the style leaves alone is Fusion's (the qmldir's fallback),
    // not Basic's: Fusion's slider handle is its SliderHandle item.
    property string sliderHandle: String(slider.handle)
    Column {
        Button { id: button; text: "B" }
        ToolButton { id: toolButton; text: "T" }
        TextField { id: field }
        TextArea { id: area }
        SpinBox { id: spin }
        ComboBox { id: combo; model: ["a"] }
        CheckBox { id: check; text: "c" }
        RadioButton { id: radio; text: "r" }
        GroupBox { id: group; title: "g" }
        Frame { id: frame }
        TabBar { id: tabBar; TabButton { id: tab; text: "t" } }
        Menu { id: menu; objectName: "menu" }
        Slider { id: slider }
    }
}
)", QUrl(QStringLiteral("qrc:/k2-controls.qml")));
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY2(window->property("sliderHandle").toString().startsWith(QStringLiteral("SliderHandle")),
                 qPrintable(window->property("sliderHandle").toString()));
        for (const Code code : ui::theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), ui::theme::codeName(code));
            const ui::theme::Roles &r = ui::theme::current().roles;
            // The controls take the new application palette an event loop
            // pass later; a closed menu when it opens (as the shell's do).
            QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("menu")), "open");
            QTRY_VERIFY_WITH_TIMEOUT(std::ranges::all_of(window->property("outlines").toList(),
                                                         [&](const QVariant &c) { return c.value<QColor>() == r.line; }),
                                     1000);
            const auto outlines = window->property("outlines").toList();
            QCOMPARE(outlines.size(), 13);
            for (qsizetype i = 0; i < outlines.size(); ++i)
                QVERIFY2(outlines[i].value<QColor>() == r.line,
                         qPrintable(QStringLiteral("%1: control %2 outline %3, line %4").arg(ui::theme::codeName(code)).arg(i)
                                        .arg(name(outlines[i].value<QColor>()), name(r.line))));
            // Fusion's outline: what the controls drew before.
            const QColor fusion = r.panel.darker(140);
            if (code == Code::Dark || code == Code::HighContrastBlack)
                QVERIFY(contrastRatio(fusion, r.panel) < 1.2);
            if (ui::theme::isHighContrast(code))
                QVERIFY(lowest(r.line, r) >= 7);
        }
        // The high-contrast border pick reaches the controls.
        store.setValue(QStringLiteral("appearance.theme"), QStringLiteral("highContrastBlack"));
        store.setValue(QString::fromLatin1(ui::theme::pickSetting(Code::HighContrastBlack, ui::theme::Pick::Border)),
                       QStringLiteral("#00FF00"));
        QTRY_COMPARE(window->property("outlines").toList().front().value<QColor>(), hex(0x00FF00));
    }

    // The swatch sheet for the user's review: per mode, the seven accents as
    // primary buttons and selection samples on the mode's four surfaces, each
    // with its lowest measured contrast, and the four themes' role tables
    // with their measured contrast as text.
    void swatchSheet()
    {
        constexpr int cell = 150, rowHeight = 112, label = 120;
        QImage sheet(label + 7 * cell, 2 * (rowHeight * 4 + 40) + 20, QImage::Format_ARGB32);
        sheet.fill(Qt::white);
        QPainter p(&sheet);
        p.setRenderHint(QPainter::Antialiasing);
        QFont font = p.font();
        font.setPixelSize(12);
        p.setFont(font);
        QString report;
        QTextStream out(&report);
        int y = 10;
        const char *surfaceNames[] = {"bg", "panel", "raised", "field"};
        for (const bool dark : {false, true}) {
            const Code code = dark ? Code::Dark : Code::Light;
            p.setPen(Qt::black);
            font.setBold(true);
            p.setFont(font);
            p.drawText(QRect(10, y, 600, 24), Qt::AlignVCenter,
                       dark ? QStringLiteral("Dark accents") : QStringLiteral("Light accents"));
            font.setBold(false);
            p.setFont(font);
            y += 30;
            out << (dark ? "Dark" : "Light") << " accents (accent / text on accent / lowest on surfaces / text on selected)\n";
            const auto list = ui::theme::accents(dark);
            for (int s = 0; s < 4; ++s) {
                const auto base = roles(code);
                const QColor surface = surfaces(base)[std::size_t(s)];
                p.fillRect(QRect(0, y, sheet.width(), rowHeight), surface);
                p.setPen(base.muted);
                p.drawText(QRect(10, y, label - 10, rowHeight), Qt::AlignVCenter,
                           QStringLiteral("%1\n%2").arg(QLatin1String(surfaceNames[s]), name(surface)));
                for (std::size_t i = 0; i < list.size(); ++i) {
                    const auto &a = list[i];
                    ui::theme::Choice choice;
                    (dark ? choice.darkAccent : choice.lightAccent) = QLatin1String(a.key);
                    const auto r = roles(code, choice);
                    const int x = label + int(i) * cell;
                    // a primary button
                    const QRect button(x + 8, y + 8, cell - 16, 30);
                    p.setPen(Qt::NoPen);
                    p.setBrush(a.accent);
                    p.drawRoundedRect(button, 1, 1);
                    p.setPen(a.onAccent);
                    p.drawText(button, Qt::AlignCenter, QString::fromLatin1(a.name));
                    // a selected row with its leading accent marker
                    const QRect row(x + 8, y + 44, cell - 16, 22);
                    p.fillRect(row, a.select);
                    p.fillRect(QRect(row.x(), row.y(), 3, row.height()), a.accent);
                    p.setPen(r.text);
                    p.drawText(row.adjusted(8, 0, 0, 0), Qt::AlignVCenter, QStringLiteral("Selected line"));
                    // the ratios
                    p.setPen(base.text);
                    p.drawText(QRect(x + 8, y + 70, cell - 16, 40), Qt::AlignLeft | Qt::AlignTop,
                               QStringLiteral("%1 %2:1\non it %3:1")
                                   .arg(name(a.accent))
                                   .arg(contrastRatio(a.accent, surface), 0, 'f', 2)
                                   .arg(contrastRatio(a.onAccent, a.accent), 0, 'f', 2));
                    if (s == 0)
                        out << "  " << a.key << " " << name(a.accent) << " / " << name(a.onAccent) << " "
                            << QString::number(contrastRatio(a.onAccent, a.accent), 'f', 2) << ":1 / "
                            << QString::number(lowest(a.accent, r), 'f', 2) << ":1 / "
                            << QString::number(contrastRatio(r.text, a.select), 'f', 2) << ":1\n";
                }
                y += rowHeight;
            }
            y += 10;
        }
        p.end();
        out << "\nRoles (lowest contrast on bg, panel, raised, field)\n";
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        for (const auto code : ui::theme::kCodes) {
            const auto r = roles(code, defaults);
            out << ui::theme::codeName(code) << "\n";
            for (std::size_t i = 0; i < ui::theme::kRoleNames.size(); ++i) {
                const QColor c = ui::theme::role(r, i);
                out << "  " << ui::theme::kRoleNames[i] << " " << name(c) << " " << QString::number(lowest(c, r), 'f', 2)
                    << ":1\n";
            }
            out << "  onAccent on accent " << QString::number(contrastRatio(r.onAccent, r.accent), 'f', 2)
                << ":1, text on select " << QString::number(contrastRatio(r.text, r.select), 'f', 2) << ":1\n";
        }
        QVERIFY(!sheet.isNull());
        const QString dir = qEnvironmentVariable("HIKARI_THEME_SHEET_DIR");
        if (dir.isEmpty())
            return;
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(sheet.save(dir + QStringLiteral("/k2-accents.png")));
        QFile text(dir + QStringLiteral("/k2-contrast.txt"));
        QVERIFY(text.open(QIODevice::WriteOnly | QIODevice::Text));
        text.write(report.toUtf8());
    }
};

QTEST_MAIN(ThemeTests)

#include "theme_tests.moc"
