#pragma once

// K2: the Hikari theme layer (docs/qt/ux/visual-language.md, "Hikari theme
// and metrics layer"): one set of semantic colour roles resolved from the
// chosen appearance and exposed once to every QML surface (the Theme
// singleton, the application palette the controls draw with, the icons) and
// to the owner-drawn items (the audio display, the Grid).
//
// The appearance follows MuseScore 4's model (docs/research/musescore-appearance.md,
// the user's 2026-10-05 decision): four themes (Light, Dark, High contrast
// white, High contrast black); "Follow system theme" (on by default) takes
// Light or Dark from the platform's colour scheme and never turns high
// contrast on or off; seven accent presets per mode, Light and Dark each
// remembering its own; in high contrast the accent, text-and-icons and border
// colours are freely pickable and every other role is fixed. Content colours
// (the audio display, the Grid's comparison colours, spelling marks) are
// fixed per theme; selection-type marks use the accent. Document colours
// (ASS styles, rendered subtitles) never change with the appearance.

#include "hikari/application/audio_display.h"

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <functional>
#include <optional>
#include <span>

class QQmlEngine;
class QJSEngine;

namespace hikari::ui {

class SettingsStore;

namespace theme {

enum class Code { Light, Dark, HighContrastWhite, HighContrastBlack };
inline constexpr std::array kCodes{Code::Light, Code::Dark, Code::HighContrastWhite, Code::HighContrastBlack};

// "light", "dark", "highContrastWhite", "highContrastBlack" (the setting's values).
QString codeName(Code code);
std::optional<Code> codeFromName(const QString &name);
bool isDark(Code code);
bool isHighContrast(Code code);
// The theme on the other side (Light <-> Dark, white <-> black), keeping high contrast.
Code withDark(Code code, bool dark);

// The settings (the rewrite's own, registry ids).
inline constexpr const char *kThemeSetting = "appearance.theme";
inline constexpr const char *kFollowSystemSetting = "appearance.followSystem";
inline constexpr const char *kLightAccentSetting = "appearance.lightAccent";
inline constexpr const char *kDarkAccentSetting = "appearance.darkAccent";

// The semantic roles (visual-language.md's tokens).
struct Roles {
    QColor background; // bg: application background
    QColor panel;      // panel and popup surface
    QColor raised;     // raised control / table-header surface
    QColor field;      // input / recessed surface
    QColor text;       // primary text (and icons)
    QColor muted;      // secondary text
    QColor line;       // boundary
    QColor accent;     // action / active accent
    QColor onAccent;   // text on an accent fill
    QColor select;     // selected background
    QColor focus;      // keyboard focus: the text colour (K2, MuseScore's focus ring)
    QColor danger;     // error emphasis
    QColor warning;    // warning emphasis (legacy WINDOW_WARNING_ELEMENTS)
    QColor success;    // success text (legacy FontCollectorDialog's "#008000")
    QColor disabled;   // disabled text and icons
    bool operator==(const Roles &) const = default;
};
// The roles by name, in Roles' order (the QML property names).
inline constexpr std::array kRoleNames{"background", "panel", "raised", "field", "text", "muted", "line", "accent",
                                       "onAccent", "select", "focus", "danger", "warning", "success", "disabled"};
QColor role(const Roles &roles, std::size_t index);

// An accent preset of a mode: the accent with the roles drawn for it.
struct Accent {
    const char *key; // stored in appearance.lightAccent / darkAccent
    const char *name;
    QColor accent, onAccent, select;
};
inline constexpr const char *kDefaultAccent = "green";
// Seven per mode, by hue; the default (the spec's green) is the fourth.
std::span<const Accent> accents(bool dark);
const Accent &accent(bool dark, const QString &key); // the default for an unknown key

// The high-contrast pickers.
enum class Pick { Accent, Text, Border };
inline constexpr std::array kPicks{Pick::Accent, Pick::Text, Pick::Border};
// "appearance.highContrastWhite.accent", ...
const char *pickSetting(Code code, Pick pick);
QColor pickDefault(Code code, Pick pick);

// What the user chose.
struct Choice {
    Code theme = Code::Dark;
    bool followSystem = true;
    QString lightAccent = QString::fromLatin1(kDefaultAccent);
    QString darkAccent = QString::fromLatin1(kDefaultAccent);
    // [white, black][accent, text, border]
    std::array<std::array<QColor, 3>, 2> picks{};
    bool operator==(const Choice &) const = default;
};
// The choice from settings values by id (the store's, or the Options
// dialog's staged values); a missing or unreadable value is the default.
Choice choiceFrom(const std::function<QVariant(const char *id)> &value);

// The theme shown: following the system, Light or Dark from its colour
// scheme (high contrast stays high contrast, on the matching side); an
// unknown scheme keeps the chosen theme.
Code effective(const Choice &choice, Qt::ColorScheme system);
Roles resolve(Code code, const Choice &choice);
// The palette the Qt Quick Controls draw with.
QPalette palette(const Roles &roles);

// Owner-drawn content colours, fixed per theme.
struct Content {
    QColor spellcheck;   // GRID_SPELLCHECKER / EDITOR_SPELLCHECKER
    QColor gridAlternate; // the Grid's every other row
    QColor gridWarning;   // a fast CPS or bad wraps cell
    std::array<QColor, 5> comparison; // GRID_COMPARISON_*: outline, mismatch, match, comment mismatch, comment match
    QColor comparisonSelection;       // GRID_SELECTION over a compared row (with alpha)
    application::AudioDisplayOptions audio; // the colour fields only
};
Content content(Code code, const Roles &roles);
// Copies the theme's colours into `options` (its other fields kept).
void applyAudioColours(application::AudioDisplayOptions &options, const application::AudioDisplayOptions &colours);

// WCAG 2.x contrast ratio of two opaque colours (1 to 21).
double contrastRatio(const QColor &a, const QColor &b);
// `over` at its alpha on `under` (opaque).
QColor composite(const QColor &over, const QColor &under);

// The process's appearance: what every Theme singleton, the icons and the
// owner-drawn items follow.
struct State {
    Code code = Code::Dark;
    Choice choice;
    Roles roles;
    Content content;
};
const State &current();
// Calls `slot` whenever the appearance changes, while `context` lives.
void onChanged(QObject *context, std::function<void()> slot);
// The profile whose choice is followed (none: the defaults). With a profile
// the application palette follows the theme.
void useSettings(SettingsStore *settings);
// The Options dialog's live preview: its staged values over the profile's,
// until endPreview (OK saves them first; Cancel just ends it).
void preview(const QVariantMap &values);
void endPreview();
// The platform's colour scheme (QStyleHints::colorScheme), or one forced
// for tests and screenshots (nullopt: the platform's again).
Qt::ColorScheme systemScheme();
void forceSystemScheme(std::optional<Qt::ColorScheme> scheme);
// The Qt Quick Controls style the theme's palette drives on every platform:
// HikariStyle (src/ui/style), Fusion with every control outline in the
// theme's boundary colour (the native Windows style draws with the system's
// colours, so the four themes could not show there), unless
// QT_QUICK_CONTROLS_STYLE names another. Called before the first engine
// loads the controls.
inline constexpr const char *kControlsStyle = "HikariStyle";
void chooseControlsStyle();

} // namespace theme

// The theme for QML (the Theme singleton): the current roles, the theme's
// state and the Appearance page's lists. One per engine, all following the
// same appearance.
class Theme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString code READ code NOTIFY changed FINAL)
    Q_PROPERTY(bool dark READ dark NOTIFY changed FINAL)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY changed FINAL)
    Q_PROPERTY(bool followSystem READ followSystem NOTIFY changed FINAL)
    Q_PROPERTY(QString accentKey READ accentKey NOTIFY changed FINAL)
    Q_PROPERTY(QColor background READ background NOTIFY changed FINAL)
    Q_PROPERTY(QColor panel READ panel NOTIFY changed FINAL)
    Q_PROPERTY(QColor raised READ raised NOTIFY changed FINAL)
    Q_PROPERTY(QColor field READ field NOTIFY changed FINAL)
    Q_PROPERTY(QColor text READ text NOTIFY changed FINAL)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed FINAL)
    Q_PROPERTY(QColor line READ line NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed FINAL)
    Q_PROPERTY(QColor select READ select NOTIFY changed FINAL)
    Q_PROPERTY(QColor focus READ focus NOTIFY changed FINAL)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed FINAL)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed FINAL)
    Q_PROPERTY(QColor success READ success NOTIFY changed FINAL)
    Q_PROPERTY(QColor disabled READ disabled NOTIFY changed FINAL)
    Q_PROPERTY(QColor spellcheck READ spellcheck NOTIFY changed FINAL)
public:
    explicit Theme(QObject *parent = nullptr);
    static Theme *create(QQmlEngine *engine, QJSEngine *js);

    QString code() const { return theme::codeName(theme::current().code); }
    bool dark() const { return theme::isDark(theme::current().code); }
    bool highContrast() const { return theme::isHighContrast(theme::current().code); }
    bool followSystem() const { return theme::current().choice.followSystem; }
    QString accentKey() const;
    QColor background() const { return roles().background; }
    QColor panel() const { return roles().panel; }
    QColor raised() const { return roles().raised; }
    QColor field() const { return roles().field; }
    QColor text() const { return roles().text; }
    QColor muted() const { return roles().muted; }
    QColor line() const { return roles().line; }
    QColor accent() const { return roles().accent; }
    QColor onAccent() const { return roles().onAccent; }
    QColor select() const { return roles().select; }
    QColor focus() const { return roles().focus; }
    QColor danger() const { return roles().danger; }
    QColor warning() const { return roles().warning; }
    QColor success() const { return roles().success; }
    QColor disabled() const { return roles().disabled; }
    QColor spellcheck() const { return theme::current().content.spellcheck; }

    // The Appearance page: the themes ({code, name, dark, highContrast}), a
    // mode's accent presets ({key, name, accent, onAccent, contrast}: the
    // accent's lowest contrast against the mode's surfaces), the pickers'
    // settings and defaults, the roles a theme resolves to for `values`
    // (the page's theme samples), and the live preview.
    Q_INVOKABLE QVariantList themes() const;
    Q_INVOKABLE QVariantList accents(bool dark) const;
    Q_INVOKABLE QVariantList picks(const QString &code) const;
    Q_INVOKABLE QVariantMap rolesOf(const QString &code, const QVariantMap &values) const;
    Q_INVOKABLE void preview(const QVariantMap &values) { theme::preview(values); }
    Q_INVOKABLE void endPreview() { theme::endPreview(); }
    // Whether the platform reports a light or dark scheme to follow.
    Q_INVOKABLE bool systemSchemeKnown() const;

signals:
    void changed();

private:
    static const theme::Roles &roles() { return theme::current().roles; }
};

} // namespace hikari::ui
