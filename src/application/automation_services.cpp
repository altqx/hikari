#include "hikari/application/automation_services.h"

#include "hikari/application/audio_spectrum.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
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

namespace {

std::string folded(std::string text, bool windows)
{
    if (windows)
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    return text;
}

std::string joinAll(std::string dir, std::initializer_list<const char *> names, bool windows)
{
    for (const char *name : names) {
        if (!dir.empty() && dir.back() != (windows ? '\\' : '/'))
            dir += windows ? '\\' : '/';
        dir += name;
    }
    return dir;
}

void addUnique(std::vector<std::string> &folders, const std::string &folder, bool windows)
{
    for (const auto &have : folders)
        if (folded(have, windows) == folded(folder, windows))
            return;
    folders.push_back(folder);
}

} // namespace

AutomationFolders automationFolders(const std::string &shipped, const std::string &user, bool windows)
{
    AutomationFolders f;
    f.autoload.push_back(joinAll(shipped, {"automation", "Autoload"}, windows));
    const bool installed = !user.empty() && folded(user, windows) != folded(shipped, windows);
    if (installed) {
        addUnique(f.autoload, joinAll(user, {"automation", "Autoload"}, windows), windows);
        addUnique(f.autoload, joinAll(user, {"automation", "autoload"}, windows), windows);
        addUnique(f.include, joinAll(user, {"automation", "Include"}, windows), windows);
        addUnique(f.include, joinAll(user, {"automation", "include"}, windows), windows);
        for (const char *sub : {"log", "temp", "autosave"})
            f.made.push_back(joinAll(user, {sub}, windows));
        f.made.push_back(joinAll(user, {"automation", "autoload"}, windows));
        f.made.push_back(joinAll(user, {"automation", "include"}, windows));
    }
    addUnique(f.include, joinAll(shipped, {"automation", "Include"}, windows), windows);
    return f;
}

std::vector<std::string> chooseAutoloadScripts(
    const std::vector<std::pair<std::string, std::vector<std::string>>> &folders, bool windows)
{
    std::vector<std::pair<std::string, std::string>> chosen; // folded name, path
    for (const auto &[folder, names] : folders) {
        for (const auto &name : names) {
            const auto dot = name.find_last_of('.');
            const std::string ext = dot == std::string::npos ? std::string() : folded(name.substr(dot + 1), true);
            if (ext != "lua" && ext != "moon")
                continue;
            const std::string key = folded(name, windows);
            const std::string path = joinAll(folder, {name.c_str()}, windows);
            const auto same = std::find_if(chosen.begin(), chosen.end(), [&](const auto &e) { return e.first == key; });
            if (same != chosen.end())
                same->second = path;
            else
                chosen.emplace_back(key, path);
        }
    }
    std::vector<std::string> paths;
    for (const auto &entry : chosen)
        paths.push_back(entry.second);
    return paths;
}

std::string decodeAutomationPath(std::string path, const AutomationPathContext &c)
{
    // Legacy normalises '\\' to '/' off Windows, then '/' to '\\' on Windows.
    std::replace(path.begin(), path.end(), c.windows ? '/' : '\\', c.windows ? '\\' : '/');
    if (path.size() < 5 || path[0] != '?')
        return path; // legacy indexes [1] and [4] unchecked; short paths stay as given
    const char a = path[1], b = path[4];
    const auto replace = [&](std::size_t count, const std::string &with) { path.replace(0, count, with); };
    const std::string &user = c.userAutomationDir.empty() ? c.automationDir : c.userAutomationDir;
    if (a == 'a' && b == 'i')
        replace(6, dirOf(c.audioPath, c.windows));
    else if (a == 'd' && b == 'a')
        replace(5, c.automationDir);
    else if (a == 'd' && b == 't')
        replace(11, c.dictionaryDir);
    else if (a == 'l' && b == 'a')
        replace(6, user);
    else if (a == 's' && b == 'i')
        replace(7, dirOf(c.subtitlePath, c.windows));
    else if (a == 't' && b == 'p')
        replace(5, join(user, "temp", c.windows));
    else if (a == 'u' && b == 'r')
        replace(5, user);
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
    case HostService::FrequencyPeaks: {
        const auto audio = m_media ? m_media->peakAudio() : std::nullopt;
        if (!audio)
            return unavailable();
        if (!*audio)
            return reply(integers({1}));
        // Legacy reads the spectrum only for times it accepts (the helper
        // raises or answers {} for the others).
        HostServiceReply out = integers({0});
        const std::int64_t start = integer(r, 0), end = integer(r, 1);
        if (start >= 0 && end >= 0 && start < end) {
            const auto peaks = legacyFrequencyPeaks(**audio, start, end, static_cast<int>(integer(r, 2)),
                                                    static_cast<int>(integer(r, 3)), static_cast<int>(integer(r, 4)));
            out.integers.insert(out.integers.end(), peaks.times.begin(), peaks.times.end());
            out.numbers.assign(peaks.intensities.begin(), peaks.intensities.end());
        }
        return reply(std::move(out));
    }
    case HostService::Gettext:
        return m_translate ? reply(strings({m_translate(string(r, 0))})) : unavailable();
    case HostService::ScriptResolution: {
        if (!m_resolution || m_resolutionOpen)
            return unavailable();
        ScriptResolutionRequest q;
        q.script = r.script;
        q.written = Resolution{static_cast<int>(integer(r, 0)), static_cast<int>(integer(r, 1))};
        m_resolutionOpen = true;
        return m_resolution->resolve(q, [this, reply](std::optional<Resolution> chosen) {
            if (!m_resolutionOpen)
                return; // withdrawn: the run already ended
            m_resolutionOpen = false;
            if (!chosen || chosen->width < 1 || chosen->height < 1)
                return reply(HostServiceReply::unavailable());
            reply(integers({chosen->width, chosen->height}));
        });
    }
    }
    unavailable();
}

void AutomationServiceRouter::withdraw()
{
    if (m_resolutionOpen) {
        m_resolutionOpen = false;
        if (m_resolution)
            m_resolution->withdraw();
    }
    if (!m_pickerOpen)
        return;
    m_pickerOpen = false;
    if (m_picker)
        m_picker->withdraw();
}

} // namespace hikari::application
