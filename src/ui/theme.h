#pragma once

#include "hikari/application/audio_display.h"
#include "hikari/themes/theme.h"

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class QQmlEngine;
class QJSEngine;

namespace hikari::ui {

class SettingsStore;

namespace theme {

enum class Code { Light, Dark, HighContrastWhite, HighContrastBlack };
inline constexpr std::array kCodes{Code::Light, Code::Dark, Code::HighContrastWhite, Code::HighContrastBlack};

// The built-in themes' ids.
QString codeName(Code code);
std::optional<Code> codeFromName(const QString &name);
bool isDark(Code code);
bool isHighContrast(Code code);

using Type = themes::Type;

inline constexpr const char *kThemeSetting = "appearance.theme";
inline constexpr const char *kFollowSystemSetting = "appearance.followSystem";
inline constexpr const char *kAccentsSetting = "appearance.accents";
inline constexpr const char *kColoursSetting = "appearance.colours";
// In Type's order.
inline constexpr std::array kPairSettings{"appearance.lightTheme", "appearance.darkTheme",
                                          "appearance.highContrastLightTheme", "appearance.highContrastDarkTheme"};

struct Roles {
    QColor background;
    QColor panel;
    QColor raised;
    QColor field;
    QColor text;
    QColor muted;
    QColor line;
    QColor accent;
    QColor onAccent;
    QColor select;
    QColor focus;
    QColor danger;
    QColor warning;
    QColor success;
    QColor disabled;
    QColor control;
    QColor hover;
    QColor pressed;
    bool operator==(const Roles &) const = default;
};
// MUST KILL: kRoleNames must stay in Roles' member order
inline constexpr std::array kRoleNames{"background", "panel",   "raised",  "field",   "text",     "muted",
                                       "line",       "accent",  "onAccent", "select",  "focus",    "danger",
                                       "warning",    "success", "disabled", "control", "hover",    "pressed"};
QColor role(const Roles &roles, std::size_t index);

// A theme the user can choose: a built-in one, or a plugin's (#1222).
struct Entry {
    QString id;
    // Untranslated; built-in titles translate in HikariSub.Options.
    QString title;
    QString plugin;
    themes::ThemeFile file;
};
const std::vector<Entry> &entries();
const Entry *find(const QString &id);
// A plugin theme, already through themes::load; replaces one with the same id.
void install(Entry entry);
void uninstall(const QString &id);
// Why a built-in theme file could not be used, for the Log; empty when all loaded.
QStringList loadProblems();

struct Accent {
    QString key;
    QString name;
    QColor accent, onAccent, select;
};
inline constexpr const char *kDefaultAccent = "green";
std::vector<Accent> accents(const QString &themeId);
std::vector<Accent> accents(bool dark);
Accent accent(bool dark, const QString &key);

struct Choice {
    QString theme = QStringLiteral("dark");
    bool followSystem = true;
    std::array<QString, 4> pair{QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("highContrastWhite"),
                                QStringLiteral("highContrastBlack")};
    // appearance.accents: <theme id>=<choice id>.
    QStringList accents;
    // appearance.colours: <theme id>/<role>=#RRGGBB.
    QStringList colours;
    bool operator==(const Choice &) const = default;
};
Choice choiceFrom(const std::function<QVariant(const char *id)> &value);
QString accentOf(const Choice &choice, const QString &themeId);
QColor colourOf(const Choice &choice, const QString &themeId, const QString &role);
QStringList withEntry(QStringList list, const QString &key, const QString &value);

QString effective(const Choice &choice, Qt::ColorScheme system);
Type typeOf(const QString &themeId);
Roles resolve(const QString &themeId, const Choice &choice);
Roles resolve(Code code, const Choice &choice);
QPalette palette(const Roles &roles);

struct Content {
    QColor spellcheck;
    QColor grammar;
    QColor gridAlternate;
    QColor gridWarning;
    QColor gridComment;
    std::array<QColor, 5> comparison;
    QColor comparisonSelection;
    // In themes::Label's order.
    std::array<QColor, 4> gridLabels;
    std::array<QColor, 13> syntax;
    // Node effects port types, in kPortColourTypes' order.
    std::array<QColor, 8> ports;
    // MUST KILL: audio is a full AudioDisplayOptions of which only the colour fields are set
    application::AudioDisplayOptions audio;
    QColor handle, handleSelected, handleBorder;
};
// Number, Time and Alpha connect to one another, so they share Number's colour.
inline constexpr std::array kPortColourTypes{"Units", "Tags", "Number", "Text", "Colour", "Point", "Bool", "Drawing"};
Content content(const QString &themeId, const Choice &choice);
Content content(Code code, const Choice &choice);
void applyAudioColours(application::AudioDisplayOptions &options, const application::AudioDisplayOptions &colours);

double contrastRatio(const QColor &a, const QColor &b);
QColor composite(const QColor &over, const QColor &under);

struct State {
    QString id = QStringLiteral("dark");
    Type type = Type::Dark;
    Choice choice;
    Roles roles;
    Content content;
};
const State &current();
bool currentIs(Code code);
void onChanged(QObject *context, std::function<void()> slot);
void useSettings(SettingsStore *settings);
void preview(const QVariantMap &values);
void endPreview();
Qt::ColorScheme systemScheme();
void forceSystemScheme(std::optional<Qt::ColorScheme> scheme);
inline constexpr const char *kControlsStyle = "HikariStyle";
// Call before the first engine loads Qt Quick Controls.
void chooseControlsStyle();

}

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
    Q_PROPERTY(QColor control READ control NOTIFY changed FINAL)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed FINAL)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed FINAL)
    Q_PROPERTY(int radius READ radius CONSTANT FINAL)
    Q_PROPERTY(int popupRadius READ popupRadius CONSTANT FINAL)
    Q_PROPERTY(int controlHeight READ controlHeight CONSTANT FINAL)
    Q_PROPERTY(int smallControlHeight READ smallControlHeight CONSTANT FINAL)
    Q_PROPERTY(int headerHeight READ headerHeight CONSTANT FINAL)
    Q_PROPERTY(int titleBarHeight READ titleBarHeight CONSTANT FINAL)
    Q_PROPERTY(int dialogTitleBarHeight READ dialogTitleBarHeight CONSTANT FINAL)
    Q_PROPERTY(int statusBarHeight READ statusBarHeight CONSTANT FINAL)
    Q_PROPERTY(int gridRowHeight READ gridRowHeight CONSTANT FINAL)
    Q_PROPERTY(int gridHeaderHeight READ gridHeaderHeight CONSTANT FINAL)
    Q_PROPERTY(QVariantList spacing READ spacing CONSTANT FINAL)
    Q_PROPERTY(QColor spellcheck READ spellcheck NOTIFY changed FINAL)
    Q_PROPERTY(QColor grammar READ grammar NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList syntax READ syntax NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap ports READ ports NOTIFY changed FINAL)
public:
    explicit Theme(QObject *parent = nullptr);
    static Theme *create(QQmlEngine *engine, QJSEngine *js);

    QString code() const { return theme::current().id; }
    bool dark() const { return themes::isDark(theme::current().type); }
    bool highContrast() const { return themes::isHighContrast(theme::current().type); }
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
    QColor control() const { return roles().control; }
    QColor hover() const { return roles().hover; }
    QColor pressed() const { return roles().pressed; }
    QColor spellcheck() const { return theme::current().content.spellcheck; }
    QColor grammar() const { return theme::current().content.grammar; }
    int radius() const { return 4; }
    int popupRadius() const { return 6; }
    int controlHeight() const { return 24; }
    int smallControlHeight() const { return 22; }
    int headerHeight() const { return 30; }
    int titleBarHeight() const { return 36; }
    int dialogTitleBarHeight() const { return 32; }
    int statusBarHeight() const { return 24; }
    int gridRowHeight() const { return 22; }
    int gridHeaderHeight() const { return 24; }
    QVariantList spacing() const { return {4, 8, 12, 16, 24}; }
    QVariantList syntax() const;
    // The colour of each port type by name, Time and Alpha with Number's.
    QVariantMap ports() const;

    Q_INVOKABLE QVariantList themes() const;
    Q_INVOKABLE QVariantList accents(bool dark) const;
    Q_INVOKABLE QVariantList accentsOf(const QString &code) const;
    // The roles the theme lets the user change (its userColours).
    Q_INVOKABLE QVariantList picks(const QString &code) const;
    Q_INVOKABLE QString accentChoice(const QStringList &list, const QString &code) const;
    Q_INVOKABLE QStringList withAccentChoice(const QStringList &list, const QString &code, const QString &choice) const;
    Q_INVOKABLE QString userColour(const QStringList &list, const QString &code, const QString &role) const;
    Q_INVOKABLE QStringList withUserColour(const QStringList &list, const QString &code, const QString &role,
                                           const QString &colour) const;
    Q_INVOKABLE QStringList withoutUserColours(const QStringList &list, const QString &code) const;
    // Typography roles: a reference size of the spec (13 is the body) in pixels at the program font.
    Q_INVOKABLE qreal fontSize(int reference) const;
    Q_INVOKABLE QVariantMap rolesOf(const QString &code, const QVariantMap &values) const;
    Q_INVOKABLE void preview(const QVariantMap &values) { theme::preview(values); }
    Q_INVOKABLE void endPreview() { theme::endPreview(); }
    Q_INVOKABLE bool systemSchemeKnown() const;

signals:
    void changed();

private:
    static const theme::Roles &roles() { return theme::current().roles; }
};

}
