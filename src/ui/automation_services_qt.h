#pragma once

#include "hikari/application/automation_services.h"
#include "hikari/application/subtitle_render.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace hikari::ui {

class QtClipboardPort : public application::ClipboardPort {
public:
    std::string text() const override;
    bool setText(const std::string &text) override;
};

/// Makes leased fonts measurable by QtTextMeasurePort while held. On Windows it measures with GDI, which sees only
/// installed fonts and fonts added to it: hold() adds each new lease privately to this process and removes the ones
/// it no longer lists. Elsewhere Qt measures and this holds nothing.
class GdiFontLeases {
public:
    GdiFontLeases() = default;
    GdiFontLeases(const GdiFontLeases &) = delete;
    GdiFontLeases &operator=(const GdiFontLeases &) = delete;

    void hold(const std::vector<application::FontLease> &fonts);

private:
#ifdef _WIN32
    std::vector<std::pair<std::shared_ptr<const std::vector<std::byte>>, std::shared_ptr<void>>> m_held;
#endif
};

class QtTextMeasurePort : public application::TextMeasurePort {
public:
    QtTextMeasurePort() = default;
    QtTextMeasurePort(const QtTextMeasurePort &) = delete;
    QtTextMeasurePort &operator=(const QtTextMeasurePort &) = delete;
    ~QtTextMeasurePort() override;
    std::optional<application::TextExtents> measure(const std::vector<std::string> &style,
                                                    const std::string &text) override;

private:
    std::map<std::tuple<std::string, std::string, bool, bool, int>, int> m_points;
#ifdef _WIN32
    struct GdiFont {
        void *handle = nullptr;
        long descent = 0, externalLeading = 0;
    };
    void dropFonts();
    unsigned m_gdiFontsSeen = 0;
    void *m_dc = nullptr;
    void *m_dcFont = nullptr;
    std::map<std::tuple<std::wstring, long, long, int, int, int, int>, GdiFont> m_fonts;
#endif
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
