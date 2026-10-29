#pragma once

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

QString codeName(Code code);
std::optional<Code> codeFromName(const QString &name);
bool isDark(Code code);
bool isHighContrast(Code code);
Code withDark(Code code, bool dark);

inline constexpr const char *kThemeSetting = "appearance.theme";
inline constexpr const char *kFollowSystemSetting = "appearance.followSystem";
inline constexpr const char *kLightAccentSetting = "appearance.lightAccent";
inline constexpr const char *kDarkAccentSetting = "appearance.darkAccent";

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
    bool operator==(const Roles &) const = default;
};
// MUST KILL: kRoleNames must stay in Roles' member order
inline constexpr std::array kRoleNames{"background", "panel", "raised", "field", "text", "muted", "line", "accent",
                                       "onAccent", "select", "focus", "danger", "warning", "success", "disabled",
                                       "control"};
QColor role(const Roles &roles, std::size_t index);

struct Accent {
    const char *key;
    const char *name;
    QColor accent, onAccent, select;
};
inline constexpr const char *kDefaultAccent = "green";
std::span<const Accent> accents(bool dark);
const Accent &accent(bool dark, const QString &key);

enum class Pick { Accent, Text, Border };
inline constexpr std::array kPicks{Pick::Accent, Pick::Text, Pick::Border};
const char *pickSetting(Code code, Pick pick);
QColor pickDefault(Code code, Pick pick);

struct Choice {
    Code theme = Code::Dark;
    bool followSystem = true;
    QString lightAccent = QString::fromLatin1(kDefaultAccent);
    QString darkAccent = QString::fromLatin1(kDefaultAccent);
    // MUST KILL: picks is indexed [white, black][accent, text, border] by bare ints
    std::array<std::array<QColor, 3>, 2> picks{};
    bool operator==(const Choice &) const = default;
};
Choice choiceFrom(const std::function<QVariant(const char *id)> &value);

Code effective(const Choice &choice, Qt::ColorScheme system);
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
    std::array<QColor, 11> syntax;
    // MUST KILL: audio is a full AudioDisplayOptions of which only the colour fields are set
    application::AudioDisplayOptions audio;
};
Content content(Code code, const Roles &roles);
void applyAudioColours(application::AudioDisplayOptions &options, const application::AudioDisplayOptions &colours);

double contrastRatio(const QColor &a, const QColor &b);
QColor composite(const QColor &over, const QColor &under);

struct State {
    Code code = Code::Dark;
    Choice choice;
    Roles roles;
    Content content;
};
const State &current();
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
    Q_PROPERTY(QColor spellcheck READ spellcheck NOTIFY changed FINAL)
    Q_PROPERTY(QColor grammar READ grammar NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList syntax READ syntax NOTIFY changed FINAL)
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
    QColor control() const { return roles().control; }
    QColor spellcheck() const { return theme::current().content.spellcheck; }
    QColor grammar() const { return theme::current().content.grammar; }
    QVariantList syntax() const;

    Q_INVOKABLE QVariantList themes() const;
    Q_INVOKABLE QVariantList accents(bool dark) const;
    Q_INVOKABLE QVariantList picks(const QString &code) const;
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
