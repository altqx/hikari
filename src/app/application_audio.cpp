// AV1 (#238): the audio panel's output device menu. It lists what Settings >
// Audio's device choice lists (H2, decision 60), marks the stored choice and
// stores a new one the way the dialog's OK does, so the two never disagree;
// the routing then moves both audio paths. When the chosen device goes away
// and the system default plays instead, the status bar says so.
#include "hikari/app/application.h"

#include <QCoreApplication>
#include <QTimer>

namespace hikari::app {

QVariantList Application::audioOutputMenu() const
{
    const auto &settings = m_settings->settings();
    const std::string chosen = settings.text("audio.outputDevice");
    QVariantList rows;
    for (const auto &choice :
         application::outputDeviceChoices(m_audioDevices->outputs(), chosen, settings.text("audio.outputDeviceName")))
        rows << QVariantMap{{QStringLiteral("label"), outputDeviceLabel(choice)},
                            {QStringLiteral("checked"), choice.id == chosen}};
    return rows;
}

void Application::chooseAudioOutput(int row)
{
    const auto &settings = m_settings->settings();
    const auto choices = application::outputDeviceChoices(m_audioDevices->outputs(), settings.text("audio.outputDevice"),
                                                          settings.text("audio.outputDeviceName"));
    if (row < 0 || row >= static_cast<int>(choices.size()))
        return;
    const auto &choice = choices[static_cast<std::size_t>(row)];
    // The name first: the id's change moves the outputs (settingChanged).
    m_settings->set("audio.outputDeviceName", QString::fromStdString(choice.name));
    m_settings->set("audio.outputDevice", QString::fromStdString(choice.id));
}

QString Application::audioOutputName() const
{
    const auto route = m_audioRouting->route();
    return QString::fromStdString(route.name);
}

void Application::noticeAudioRoute(const application::OutputRoute &before, const application::OutputRoute &after)
{
    if (before == after)
        return;
    emit audioOutputChanged();
    // The chosen device went away (the choice is still stored): the default plays.
    if (before.systemDefault || !after.systemDefault || m_settings->settings().text("audio.outputDevice").empty())
        return;
    const QString name = QString::fromStdString(m_settings->settings().text("audio.outputDeviceName"));
    QString text;
    if (after.id.empty())
        //: The output device chosen on the audio panel or in Settings > Audio was unplugged and no other is connected; %1 is its name
        text = QCoreApplication::translate("HikariSub.Audio", "%1 is not connected. No output device is available.").arg(name);
    else
        //: The output device chosen on the audio panel or in Settings > Audio was unplugged; %1 is its name, %2 the system default that plays instead
        text = QCoreApplication::translate("HikariSub.Audio", "%1 is not connected. Audio plays through %2.")
                   .arg(name, QString::fromStdString(after.name));
    m_shell->setStatusText(text);
    // As legacy's status messages, it goes after 5 s unless something replaced it.
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this, text] {
        if (m_shell->statusText() == text)
            m_shell->setStatusText(QString());
    });
}

} // namespace hikari::app
