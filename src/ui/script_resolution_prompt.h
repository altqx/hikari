#pragma once

#include "hikari/application/resample.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace hikari::ui {

class ScriptResolutionPromptController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(QString script READ script NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY changed)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY changed)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY changed)
public:
    struct Question {
        QString script;
        application::Resolution suggested;
        std::optional<application::Resolution> video;
    };
    struct Answer {
        application::Resolution resolution;
        bool alwaysFromVideo = false;
    };
    using Reply = std::function<void(std::optional<Answer>)>;

    explicit ScriptResolutionPromptController(QObject *parent = nullptr);

    // MUST KILL: asking() means a view is attached, not that a question is open (that is isOpen)
    bool asking() const { return !m_view.isNull(); }
    void ask(Question question, Reply reply);
    void withdraw();

    bool isOpen() const { return static_cast<bool>(m_reply); }
    QString script() const { return m_question.script; }
    int width() const { return m_question.suggested.width; }
    int height() const { return m_question.suggested.height; }
    bool hasVideo() const { return m_question.video.has_value(); }
    int videoWidth() const { return m_question.video ? m_question.video->width : 0; }
    int videoHeight() const { return m_question.video ? m_question.video->height : 0; }

    Q_INVOKABLE void setView(QObject *view);
    Q_INVOKABLE void accept(int width, int height, bool alwaysFromVideo);
    Q_INVOKABLE void reject();

signals:
    void changed();

private:
    QPointer<QObject> m_view;
    Question m_question;
    Reply m_reply;
};

}
