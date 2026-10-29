#include "script_resolution_prompt.h"

namespace hikari::ui {

ScriptResolutionPromptController::ScriptResolutionPromptController(QObject *parent) : QObject(parent) {}

void ScriptResolutionPromptController::setView(QObject *view)
{
    m_view = view;
}

void ScriptResolutionPromptController::ask(Question question, Reply reply)
{
    if (m_reply)
        m_reply(std::nullopt);
    m_question = std::move(question);
    m_reply = std::move(reply);
    emit changed();
}

void ScriptResolutionPromptController::withdraw()
{
    if (!m_reply)
        return;
    m_reply = nullptr;
    emit changed();
}

void ScriptResolutionPromptController::accept(int width, int height, bool alwaysFromVideo)
{
    if (!m_reply || width < 1 || height < 1)
        return;
    auto reply = std::move(m_reply);
    m_reply = nullptr;
    emit changed();
    reply(Answer{application::Resolution{width, height}, alwaysFromVideo && m_question.video.has_value()});
}

void ScriptResolutionPromptController::reject()
{
    if (!m_reply)
        return;
    auto reply = std::move(m_reply);
    m_reply = nullptr;
    emit changed();
    reply(std::nullopt);
}

}
