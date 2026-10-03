#include "hikari/application/automation_services.h"

#include <algorithm>
#include <memory>

namespace hikari::application {

namespace {

std::string dirOf(const std::string &path, bool windows)
{
    const auto at = windows ? path.find_last_of("\\/") : path.find_last_of('/');
    return at == std::string::npos ? std::string() : path.substr(0, at);
}

std::string join(const std::string &dir, const std::string &name, bool windows)
{
    return dir + (windows ? '\\' : '/') + name;
}

std::int64_t integer(const HostServiceRequest &r, std::size_t i)
{
    return i < r.integers.size() ? r.integers[i] : 0;
}

std::string string(const HostServiceRequest &r, std::size_t i)
{
    return i < r.strings.size() ? r.strings[i] : std::string();
}

HostServiceReply integers(std::vector<std::int64_t> values)
{
    HostServiceReply reply;
    reply.integers = std::move(values);
    return reply;
}

HostServiceReply strings(std::vector<std::string> values)
{
    HostServiceReply reply;
    reply.strings = std::move(values);
    return reply;
}

} // namespace

std::string decodeAutomationPath(std::string path, const AutomationPathContext &c)
{
    // Legacy normalises '\\' to '/' off Windows, then '/' to '\\' on Windows.
    std::replace(path.begin(), path.end(), c.windows ? '/' : '\\', c.windows ? '\\' : '/');
    if (path.size() < 5 || path[0] != '?')
        return path; // legacy indexes [1] and [4] unchecked; short paths stay as given
    const char a = path[1], b = path[4];
    const auto replace = [&](std::size_t count, const std::string &with) { path.replace(0, count, with); };
    if (a == 'a' && b == 'i')
        replace(6, dirOf(c.audioPath, c.windows));
    else if (a == 'd' && b == 'a')
        replace(5, c.automationDir);
    else if (a == 'd' && b == 't')
        replace(11, c.dictionaryDir);
    else if (a == 'l' && b == 'a')
        replace(6, c.automationDir);
    else if (a == 's' && b == 'i')
        replace(7, dirOf(c.subtitlePath, c.windows));
    else if (a == 't' && b == 'p')
        replace(5, join(c.automationDir, "temp", c.windows));
    else if (a == 'u' && b == 'r')
        replace(5, c.automationDir);
    else if (a == 'v' && b == 'e')
        replace(6, dirOf(c.videoPath, c.windows));
    return path;
}

void AutomationServiceRouter::handle(const HostServiceRequest &r, Reply reply)
{
    const auto unavailable = [&] { reply(HostServiceReply::unavailable()); };
    switch (r.service) {
    case HostService::FrameFromMs:
    case HostService::MsFromFrame: {
        if (!m_media)
            return unavailable();
        const auto v = r.service == HostService::FrameFromMs ? m_media->frameFromMs(integer(r, 0))
                                                             : m_media->msFromFrame(integer(r, 0));
        return v ? reply(integers({*v})) : unavailable();
    }
    case HostService::VideoSize: {
        const auto v = m_media ? m_media->videoSize() : std::nullopt;
        return v ? reply(integers({v->width, v->height, v->arX, v->arY})) : unavailable();
    }
    case HostService::Keyframes: {
        auto v = m_media ? m_media->keyframes() : std::nullopt;
        return v ? reply(integers(std::move(*v))) : unavailable();
    }
    case HostService::Frame:
        if (!m_media)
            return unavailable();
        return m_media->frame(integer(r, 0), integer(r, 1) != 0, [reply](std::optional<MacroMediaPort::Frame> f) {
            if (!f)
                return reply(HostServiceReply::unavailable());
            HostServiceReply out = integers({f->width, f->height});
            out.pixels = std::move(f->bgra);
            reply(std::move(out));
        });
    case HostService::AudioSelection: {
        const auto v = m_media ? m_media->audioSelection() : std::nullopt;
        return v ? reply(integers({v->first, v->second})) : unavailable();
    }
    case HostService::ProjectProperties: {
        const auto v = m_media ? m_media->project() : std::nullopt;
        if (!v)
            return unavailable();
        HostServiceReply out = integers({v->videoFrameShown});
        out.strings = {v->audioFile, v->videoFile, v->keyframesFile};
        return reply(std::move(out));
    }
    case HostService::FileName: {
        const auto v = m_media ? m_media->fileName() : std::nullopt;
        return v ? reply(strings({*v})) : unavailable();
    }
    case HostService::TextExtents: {
        const auto v = m_measure ? m_measure->measure(r.style, string(r, 0)) : std::nullopt;
        if (!v)
            return unavailable();
        HostServiceReply out;
        out.numbers = {v->width, v->height, v->descent, v->externalLeading};
        return reply(std::move(out));
    }
    case HostService::ClipboardGet:
        return m_clipboard ? reply(strings({m_clipboard->text()})) : unavailable();
    case HostService::ClipboardSet:
        return m_clipboard ? reply(integers({m_clipboard->setText(string(r, 0)) ? 1 : 0})) : unavailable();
    case HostService::OpenFiles:
    case HostService::SaveFile: {
        if (!m_picker || m_pickerOpen)
            return unavailable();
        FilePickerRequest p;
        p.mode = r.service == HostService::SaveFile ? FilePickerRequest::Mode::Save
                 : integer(r, 0) != 0               ? FilePickerRequest::Mode::OpenMultiple
                                                    : FilePickerRequest::Mode::Open;
        p.title = string(r, 0);
        p.dir = string(r, 1);
        p.file = string(r, 2);
        p.wildcard = string(r, 3);
        if (r.service == HostService::SaveFile)
            p.promptOverwrite = integer(r, 0) != 0;
        else
            p.mustExist = integer(r, 1) != 0;
        p.script = r.script;
        m_pickerOpen = true;
        return m_picker->pick(p, [this, reply](std::optional<std::vector<std::string>> paths) {
            if (!m_pickerOpen)
                return; // withdrawn: the run already ended
            m_pickerOpen = false;
            if (!paths || paths->empty())
                return reply(HostServiceReply::unavailable());
            reply(strings(std::move(*paths)));
        });
    }
    case HostService::StatusText:
        if (!m_editor)
            return unavailable();
        m_editor->setStatusText(string(r, 0));
        return reply({});
    case HostService::DecodePath:
        return reply(strings({decodeAutomationPath(string(r, 0), m_paths ? m_paths() : AutomationPathContext{})}));
    case HostService::EditorCursor:
    case HostService::EditorSelection: {
        const auto v = m_editor ? m_editor->selection() : std::nullopt;
        if (!v)
            return unavailable();
        return reply(r.service == HostService::EditorCursor ? integers({v->first}) : integers({v->first, v->second}));
    }
    case HostService::SetEditorCursor:
    case HostService::SetEditorSelection:
        if (!m_editor)
            return unavailable();
        m_editor->setSelection(integer(r, 0), r.service == HostService::SetEditorCursor ? integer(r, 0) : integer(r, 1));
        return reply({});
    case HostService::EditorModified:
        return m_editor ? reply(integers({m_editor->modified() ? 1 : 0})) : unavailable();
    }
    unavailable();
}

void AutomationServiceRouter::withdraw()
{
    if (!m_pickerOpen)
        return;
    m_pickerOpen = false;
    if (m_picker)
        m_picker->withdraw();
}

} // namespace hikari::application
