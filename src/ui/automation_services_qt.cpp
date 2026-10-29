#include "automation_services_qt.h"

#include <QClipboard>
#include <QDir>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>

namespace hikari::ui {

namespace {

std::string field(const std::vector<std::string> &style, std::size_t i)
{
    return i < style.size() ? style[i] : std::string();
}

double number(const std::vector<std::string> &style, std::size_t i)
{
    return std::atof(field(style, i).c_str());
}

bool flag(const std::vector<std::string> &style, std::size_t i)
{
    return std::atoi(field(style, i).c_str()) != 0;
}

// ASS v4+ style field positions.
enum : std::size_t { Fontname = 1, Fontsize = 2, Bold = 7, Italic = 8, Underline = 9, StrikeOut = 10, ScaleX = 11,
                     ScaleY = 12, Spacing = 13, Encoding = 22 };

QString qs(const std::string &s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

}

std::string QtClipboardPort::text() const
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text().toStdString() : std::string();
}

bool QtClipboardPort::setText(const std::string &text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(qs(text));
    return true;
}

std::optional<application::TextExtents> QtTextMeasurePort::measure(const std::vector<std::string> &style,
                                                                   const std::string &text)
{
    const float fontsize = static_cast<float>(number(style, Fontsize)) * 64.f;
    const float spacing = static_cast<float>(number(style, Spacing)) * 64.f;
    const QString string = qs(text);
    float fwidth = 0, fheight = 0, fdescent = 0, fextlead = 0;
    if (string.isEmpty())
        return application::TextExtents{};
#ifdef _WIN32
    const std::wstring wide = string.toStdWString();
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return std::nullopt;
    SetMapMode(dc, MM_TEXT);
    LOGFONTW lf{};
    lf.lfHeight = static_cast<LONG>(fontsize);
    lf.lfWeight = flag(style, Bold) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = flag(style, Italic);
    lf.lfUnderline = flag(style, Underline);
    lf.lfStrikeOut = flag(style, StrikeOut);
    lf.lfCharSet = static_cast<BYTE>(std::atoi(field(style, Encoding).c_str()));
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    const std::wstring face = qs(field(style, Fontname)).toStdWString();
    wcsncpy_s(lf.lfFaceName, face.c_str(), _TRUNCATE);
    HFONT font = CreateFontIndirectW(&lf);
    if (!font) {
        DeleteDC(dc);
        return std::nullopt;
    }
    SelectObject(dc, font);
    SIZE sz{};
    if (spacing != 0) {
        for (const wchar_t c : wide) {
            GetTextExtentPoint32W(dc, &c, 1, &sz);
            fwidth += static_cast<float>(sz.cx) + spacing;
            fheight = static_cast<float>(sz.cy);
        }
    } else {
        GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), &sz);
        fwidth = static_cast<float>(sz.cx);
        fheight = static_cast<float>(sz.cy);
    }
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    fdescent = static_cast<float>(tm.tmDescent);
    fextlead = static_cast<float>(tm.tmExternalLeading);
    DeleteDC(dc);
    DeleteObject(font);
#else
    // The style's size is the line's height, as GDI's positive lfHeight makes it on Windows, not the em.
    const int pixelHeight = std::max(1, static_cast<int>((fontsize / 64.f) + 0.5f));
    QImage device(1, 1, QImage::Format_ARGB32);
    device.setDotsPerMeterX(3780);
    device.setDotsPerMeterY(3780);
    QFont font(qs(field(style, Fontname)));
    font.setBold(flag(style, Bold));
    font.setItalic(flag(style, Italic));
    font.setUnderline(flag(style, Underline));
    font.setStrikeOut(flag(style, StrikeOut));
    const auto heightAt = [&](int points) {
        font.setPointSize(points);
        return QFontMetrics(font, &device).height();
    };
    const QFontInfo face(font);
    const auto key = std::tuple{face.family().toStdString(), face.styleName().toStdString(), font.bold(),
                                font.italic(), pixelHeight};
    auto found = m_points.find(key);
    if (found == m_points.end()) {
        int points = std::max(1, pixelHeight * 3 / 4);
        points = std::max(1, static_cast<int>(points * static_cast<double>(pixelHeight) / heightAt(points)));
        while (points > 1 && heightAt(points) > pixelHeight)
            --points;
        while (heightAt(points + 1) <= pixelHeight)
            ++points;
        found = m_points.emplace(key, points).first;
    }
    font.setPointSize(found->second);
    const QFontMetrics metrics(font, &device);
    if (spacing != 0) {
        for (const QChar c : string) {
            fwidth += static_cast<float>(metrics.horizontalAdvance(c)) * 64.f + spacing;
            fheight = static_cast<float>(metrics.height()) * 64.f;
        }
    } else {
        fwidth = static_cast<float>(metrics.horizontalAdvance(string)) * 64.f;
        fheight = static_cast<float>(metrics.height()) * 64.f;
    }
    fdescent = static_cast<float>(metrics.descent()) * 64.f;
    fextlead = static_cast<float>(metrics.leading()) * 64.f;
#endif
    const float scalex = static_cast<float>(number(style, ScaleX)) / 100.f;
    const float scaley = static_cast<float>(number(style, ScaleY)) / 100.f;
    return application::TextExtents{scalex * (fwidth / 64.f), scaley * (fheight / 64.f),
                                    scaley * (fdescent / 64.f), scaley * (fextlead / 64.f)};
}

QStringList nameFiltersOf(const std::string &wildcard)
{
    const QStringList parts = qs(wildcard).split(QLatin1Char('|'));
    QStringList filters;
    for (qsizetype i = 0; i + 1 < parts.size(); i += 2) {
        QString label = parts[i].trimmed();
        // A script's wx label often repeats its patterns: "Text files (*.txt)".
        if (label.endsWith(QLatin1Char(')')) && label.contains(QLatin1Char('(')))
            label = label.left(label.lastIndexOf(QLatin1Char('('))).trimmed();
        QStringList patterns = parts[i + 1].split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (QString &p : patterns)
            p = p.trimmed();
        filters << QStringLiteral("%1 (%2)").arg(label, patterns.join(QLatin1Char(' ')));
    }
    return filters;
}

AutomationFilePickerController::AutomationFilePickerController(QObject *parent) : QObject(parent) {}

void AutomationFilePickerController::pick(const application::FilePickerRequest &request,
                                std::function<void(std::optional<std::vector<std::string>>)> reply)
{
    if (m_reply)
        m_reply(std::nullopt);
    m_request = request;
    m_reply = std::move(reply);
    m_title = qs(request.title);
    m_folder = request.dir.empty() ? QUrl() : QUrl::fromLocalFile(qs(request.dir));
    m_file = request.file.empty() ? QUrl()
                                  : QUrl::fromLocalFile(QDir(qs(request.dir)).absoluteFilePath(qs(request.file)));
    m_filters = nameFiltersOf(request.wildcard);
    emit changed();
}

void AutomationFilePickerController::withdraw()
{
    if (!m_reply)
        return;
    m_reply = nullptr;
    emit changed();
}

void AutomationFilePickerController::accept(const QList<QUrl> &files)
{
    if (!m_reply)
        return;
    auto reply = std::move(m_reply);
    m_reply = nullptr;
    std::vector<std::string> paths;
    for (const QUrl &url : files)
        paths.push_back(QDir::toNativeSeparators(url.toLocalFile()).toStdString());
    emit changed();
    reply(paths.empty() ? std::nullopt : std::optional(std::move(paths)));
}

void AutomationFilePickerController::reject()
{
    if (!m_reply)
        return;
    auto reply = std::move(m_reply);
    m_reply = nullptr;
    emit changed();
    reply(std::nullopt);
}

}
