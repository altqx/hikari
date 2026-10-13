#pragma once

// Y4-playres-ask: the question shown when a macro reads the script resolution
// of a Document that lacks PlayResX/PlayResY (ScriptResolutionDialog.qml).
// The width and height start from the open video's size, else legacy's
// values; "Use video resolution" puts the video's size back; "Always set it
// from the video" makes later runs set both keys to the video's size without
// asking.
// Set answers with the values, Cancel without (the run does not go on).
//
// The question is asked only while a view shows it (setView); without one
// (tests, a session without a window) nobody could answer, so asking() is
// false and the automation shell does not wait for an answer.

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
        QString script;                                     // the script's file name
        application::Resolution suggested;                  // the values the fields start with
        std::optional<application::Resolution> video;       // the open video's size
    };
    struct Answer {
        application::Resolution resolution;
        bool alwaysFromVideo = false;
    };
    using Reply = std::function<void(std::optional<Answer>)>;

    explicit ScriptResolutionPromptController(QObject *parent = nullptr);

    // Whether a view shows the question (see above).
    bool asking() const { return !m_view.isNull(); }
    // Shows the question; `reply` is called once, unless it is withdrawn first.
    void ask(Question question, Reply reply);
    void withdraw();

    bool isOpen() const { return static_cast<bool>(m_reply); }
    QString script() const { return m_question.script; }
    int width() const { return m_question.suggested.width; }
    int height() const { return m_question.suggested.height; }
    bool hasVideo() const { return m_question.video.has_value(); }
    int videoWidth() const { return m_question.video ? m_question.video->width : 0; }
    int videoHeight() const { return m_question.video ? m_question.video->height : 0; }

    // From QML.
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

} // namespace hikari::ui
