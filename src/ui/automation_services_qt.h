#pragma once

// Qt adapters for automation host services (L3): the system clipboard, text
// measurement for text_extents, and a fixed QML file picker. All run on the
// GUI thread; the picker answers asynchronously.

#include "hikari/application/automation_services.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class QtClipboardPort : public application::ClipboardPort {
public:
    std::string text() const override;
    bool setText(const std::string &text) override;
};

// Legacy GetLineTextExtents: GDI on Windows (the legacy code), the font's
// metrics at the style's pixel size elsewhere (legacy used wx there). Results
// are scaled by the style's ScaleX/ScaleY; Spacing adds per character.
class QtTextMeasurePort : public application::TextMeasurePort {
public:
    std::optional<application::TextExtents> measure(const std::vector<std::string> &style,
                                                    const std::string &text) override;
};

// The legacy wx wildcard ("Label|*.a;*.b|...") as Qt name filters.
QStringList nameFiltersOf(const std::string &wildcard);

class AutomationFilePickerController : public QObject, public application::FilePickerPort {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(bool save READ isSave NOTIFY changed)
    Q_PROPERTY(bool multiple READ isMultiple NOTIFY changed)
    Q_PROPERTY(bool confirmOverwrite READ confirmOverwrite NOTIFY changed)
    Q_PROPERTY(QUrl folder READ folder NOTIFY changed)
    Q_PROPERTY(QUrl file READ file NOTIFY changed)
    Q_PROPERTY(QStringList nameFilters READ nameFilters NOTIFY changed)
public:
    explicit AutomationFilePickerController(QObject *parent = nullptr);

    void pick(const application::FilePickerRequest &request,
              std::function<void(std::optional<std::vector<std::string>>)> reply) override;
    void withdraw() override;

    bool isOpen() const { return static_cast<bool>(m_reply); }
    QString title() const { return m_title; }
    bool isSave() const { return m_request.mode == application::FilePickerRequest::Mode::Save; }
    bool isMultiple() const { return m_request.mode == application::FilePickerRequest::Mode::OpenMultiple; }
    bool confirmOverwrite() const { return m_request.promptOverwrite; }
    QUrl folder() const { return m_folder; }
    QUrl file() const { return m_file; }
    QStringList nameFilters() const { return m_filters; }

    // From QML: the chosen files, or none when cancelled.
    Q_INVOKABLE void accept(const QList<QUrl> &files);
    Q_INVOKABLE void reject();

signals:
    void changed();

private:
    application::FilePickerRequest m_request;
    std::function<void(std::optional<std::vector<std::string>>)> m_reply;
    QString m_title;
    QUrl m_folder, m_file;
    QStringList m_filters;
};

} // namespace hikari::ui
