#pragma once

#include "hikari/api/json.h"

#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace hikari::app {

class PluginPopups;

// The quick pick or input box a plugin shows, for QuickPick.qml and InputBox.qml.
class PluginPopup : public QObject {
    Q_OBJECT
    // "pick" or "box".
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString pluginName READ pluginName CONSTANT)
    // ask's popups answer once; open's stay until the plugin closes them.
    Q_PROPERTY(bool live READ live CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString placeholder READ placeholder NOTIFY changed)
    // "2 / 3", or empty.
    Q_PROPERTY(QString step READ step NOTIFY changed)
    Q_PROPERTY(bool back READ back NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // The text the plugin set; valueSet changes each time it sets one.
    Q_PROPERTY(QString value READ value NOTIFY valueSet)
    // QuickPickModel items: {id, label, description, detail, icon, picked, alwaysShow, disabled, buttons, kind}.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(bool many READ many NOTIFY changed)
    Q_PROPERTY(QStringList matchOn READ matchOn NOTIFY changed)
    Q_PROPERTY(bool keepOrder READ keepOrder NOTIFY changed)
    Q_PROPERTY(bool filtering READ filtering NOTIFY changed)
    // The item to highlight; activeSet changes each time the plugin names one.
    Q_PROPERTY(QString active READ active NOTIFY activeSet)
    // Title row icon buttons: [{id, icon, tooltip}].
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(QString prompt READ prompt NOTIFY changed)
    Q_PROPERTY(bool password READ password NOTIFY changed)
    // UTF-16 offsets in value; -1 selects all of it.
    Q_PROPERTY(int selectionFrom READ selectionFrom NOTIFY valueSet)
    Q_PROPERTY(int selectionTo READ selectionTo NOTIFY valueSet)
    // "error", "warning", "info" or empty: the validation's or the plugin's line under the box.
    Q_PROPERTY(QString severity READ severity NOTIFY messageChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    PluginPopup(PluginPopups &owner, QString kind, QString pluginName, bool live, QObject *parent);

    QString kind() const { return m_kind; }
    QString pluginName() const { return m_pluginName; }
    bool live() const { return m_live; }
    QString title() const { return m_title; }
    QString placeholder() const { return m_placeholder; }
    QString step() const { return m_step; }
    bool back() const { return m_back; }
    bool busy() const { return m_busy; }
    QString value() const { return m_value; }
    QVariantList items() const { return m_items; }
    bool many() const { return m_many; }
    QStringList matchOn() const { return m_matchOn; }
    bool keepOrder() const { return m_keepOrder; }
    bool filtering() const { return m_filtering; }
    QString active() const { return m_active; }
    QVariantList buttons() const { return m_buttons; }
    QString prompt() const { return m_prompt; }
    bool password() const { return m_password; }
    int selectionFrom() const { return m_selectionFrom; }
    int selectionTo() const { return m_selectionTo; }
    QString severity() const;
    QString message() const;

    Q_INVOKABLE void edit(const QString &value);
    Q_INVOKABLE void activate(const QString &item);
    // A pick answers the given ids; a box its current text.
    Q_INVOKABLE void accept(const QStringList &items = {});
    Q_INVOKABLE void press(const QString &button, const QString &item = {});
    Q_INVOKABLE void goBack();
    // Esc, a press outside, or the window losing focus.
    Q_INVOKABLE void dismiss();

signals:
    void changed();
    void valueSet();
    void activeSet();
    void messageChanged();

private:
    friend class PluginPopups;
    struct Validation {
        std::optional<QRegularExpression> pattern;
        QString message;
        int minLength = 0;
        int maxLength = -1;
    };
    void apply(const api::Json &spec);
    std::optional<QString> failedCheck() const;

    PluginPopups &m_owner;
    QString m_kind, m_pluginName;
    bool m_live = false;
    QString m_title, m_placeholder, m_step;
    bool m_back = false;
    bool m_busy = false;
    QString m_value;
    QVariantList m_items;
    bool m_many = false;
    QStringList m_matchOn;
    bool m_keepOrder = false;
    bool m_filtering = true;
    QString m_active;
    QVariantList m_buttons;
    QString m_prompt;
    bool m_password = false;
    int m_selectionFrom = -1;
    int m_selectionTo = -1;
    std::optional<Validation> m_validation;
    QString m_noticeSeverity, m_notice;
    bool m_acceptWaits = false;
};

// Plugins' quick picks and input boxes: quick_pick.* and input_box.*. One shows at a time.
class PluginPopups : public QObject {
    Q_OBJECT
    Q_PROPERTY(hikari::app::PluginPopup *shown READ shown NOTIFY shownChanged)

public:
    using Reply = std::function<void(std::string)>;
    // As PluginUi::Deliver.
    using Deliver = std::function<bool(const std::string &plugin, const std::string &event, const std::string &json,
                                       const std::string &coalesceKey)>;
    // Whether the plugin is answering something the user started.
    using UserAsked = std::function<bool(const std::string &plugin)>;

    explicit PluginPopups(Deliver deliver, QObject *parent = nullptr);
    ~PluginPopups() override;

    static bool handles(const std::string &operation);
    void serve(const std::string &plugin, const QString &pluginName, const std::string &operation, const api::Json &params,
               Reply reply);
    void setUserAsked(UserAsked asked) { m_userAsked = std::move(asked); }
    // The plugin stopped or went off: its popup closes, answering as cancelled.
    void closeOf(const std::string &plugin);
    PluginPopup *shown() const { return m_shown ? m_shown->state : nullptr; }
    // HikariSub's own quick pick opened over it.
    Q_INVOKABLE void dismissShown();

signals:
    void shownChanged();

private:
    friend class PluginPopup;
    struct Popup {
        std::string plugin;
        std::string id;
        PluginPopup *state = nullptr;
        std::optional<Reply> asked;
    };
    void open(const std::string &plugin, const QString &pluginName, bool pick, bool live, const api::Json &params,
              const Reply &reply);
    void update(const std::string &plugin, bool pick, const api::Json &params, const Reply &reply);
    void close(const std::string &plugin, bool pick, const api::Json &params, const Reply &reply);
    Popup *find(const std::string &plugin, bool pick, const api::Json &params);
    void event(api::Json data, bool coalesce = false);
    void answer(api::Json data);
    void end(bool deliverHide);

    Deliver m_deliver;
    UserAsked m_userAsked;
    std::unique_ptr<Popup> m_shown;
    int m_next = 0;
};

}
