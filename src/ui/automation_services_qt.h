#pragma once

#include "hikari/application/automation_services.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <string>
#include <tuple>

namespace hikari::ui {

class QtClipboardPort : public application::ClipboardPort {
public:
    std::string text() const override;
    bool setText(const std::string &text) override;
};

class QtTextMeasurePort : public application::TextMeasurePort {
public:
    std::optional<application::TextExtents> measure(const std::vector<std::string> &style,
                                                    const std::string &text) override;

private:
    std::map<std::tuple<std::string, std::string, bool, bool, int>, int> m_points;
};

// A wx wildcard ("Label|*.a;*.b|...") from a script, as Qt name filters.
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

}
