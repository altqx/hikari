#include "hikari/app/style_manager_controller.h"

#include "hikari/core/ass_load.h"

#include <QCollator>
#include <QQuickImageProvider>
#include <QFile>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace hikari::app {

namespace {

using application::Replace;
using application::StyleList;

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(b.constData()), static_cast<std::size_t>(b.size()));
}

QVariantMap colourMap(const core::Colour &c)
{
    return {{QStringLiteral("r"), int(c.r)}, {QStringLiteral("g"), int(c.g)}, {QStringLiteral("b"), int(c.b)},
            {QStringLiteral("a"), int(c.a)}};
}

core::Colour colourOf(const QVariant &v)
{
    const auto m = v.toMap();
    return {m.value(QStringLiteral("r")).toInt(), m.value(QStringLiteral("g")).toInt(), m.value(QStringLiteral("b")).toInt(),
            m.value(QStringLiteral("a")).toInt()};
}

QVariantMap valuesOf(const core::StyleValues &s)
{
    return {{QStringLiteral("name"), qs(s.name)},
            {QStringLiteral("fontname"), qs(s.fontname)},
            {QStringLiteral("fontsize"), qs(s.fontsize)},
            {QStringLiteral("primary"), colourMap(s.primary)},
            {QStringLiteral("secondary"), colourMap(s.secondary)},
            {QStringLiteral("outline"), colourMap(s.outline)},
            {QStringLiteral("back"), colourMap(s.back)},
            {QStringLiteral("bold"), s.bold},
            {QStringLiteral("italic"), s.italic},
            {QStringLiteral("underline"), s.underline},
            {QStringLiteral("strikeOut"), s.strikeOut},
            {QStringLiteral("scaleX"), qs(s.scaleX)},
            {QStringLiteral("scaleY"), qs(s.scaleY)},
            {QStringLiteral("spacing"), qs(s.spacing)},
            {QStringLiteral("angle"), qs(s.angle)},
            {QStringLiteral("borderStyle"), s.borderStyle},
            {QStringLiteral("outlineWidth"), qs(s.outlineWidth)},
            {QStringLiteral("shadow"), qs(s.shadow)},
            {QStringLiteral("alignment"), qs(s.alignment)},
            {QStringLiteral("marginLeft"), qs(s.marginLeft)},
            {QStringLiteral("marginRight"), qs(s.marginRight)},
            {QStringLiteral("marginVertical"), qs(s.marginVertical)},
            {QStringLiteral("encoding"), qs(s.encoding)}};
}

core::StyleValues styleOf(const QVariantMap &m, core::StyleValues s)
{
    const auto text = [&](const char *key, std::u8string &field) {
        if (m.contains(QLatin1String(key)))
            field = u8(m.value(QLatin1String(key)).toString());
    };
    const auto flag = [&](const char *key, bool &field) {
        if (m.contains(QLatin1String(key)))
            field = m.value(QLatin1String(key)).toBool();
    };
    const auto colour = [&](const char *key, core::Colour &field) {
        if (m.contains(QLatin1String(key)))
            field = colourOf(m.value(QLatin1String(key)));
    };
    text("name", s.name);
    text("fontname", s.fontname);
    text("fontsize", s.fontsize);
    colour("primary", s.primary);
    colour("secondary", s.secondary);
    colour("outline", s.outline);
    colour("back", s.back);
    flag("bold", s.bold);
    flag("italic", s.italic);
    flag("underline", s.underline);
    flag("strikeOut", s.strikeOut);
    text("scaleX", s.scaleX);
    text("scaleY", s.scaleY);
    text("spacing", s.spacing);
    text("angle", s.angle);
    flag("borderStyle", s.borderStyle);
    text("outlineWidth", s.outlineWidth);
    text("shadow", s.shadow);
    text("alignment", s.alignment);
    text("marginLeft", s.marginLeft);
    text("marginRight", s.marginRight);
    text("marginVertical", s.marginVertical);
    text("encoding", s.encoding);
    s.complete = true;
    return s;
}

std::vector<std::size_t> rowsOf(const QVariantList &rows)
{
    std::vector<std::size_t> out;
    for (const auto &r : rows)
        if (r.toInt() >= 0)
            out.push_back(static_cast<std::size_t>(r.toInt()));
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

QVariantList listOf(const std::vector<std::size_t> &rows)
{
    QVariantList out;
    for (const auto r : rows)
        out << int(r);
    return out;
}

// The answers QML collected, in the order the transfer asks.
application::AskReplace answering(const QStringList &answers)
{
    auto next = std::make_shared<int>(0);
    return [answers, next](const std::u8string &) {
        const QString a = *next < answers.size() ? answers[(*next)++] : QStringLiteral("no");
        return a == QLatin1String("yes")        ? Replace::Yes
               : a == QLatin1String("yesToAll") ? Replace::YesToAll
               : a == QLatin1String("cancel")   ? Replace::Cancel
                                                : Replace::No;
    };
}

// The names a transfer would ask about: those already present (or added
// earlier in the same transfer).
QStringList conflictsOf(StyleList into, const StyleList &styles)
{
    QStringList out;
    for (const auto &s : styles) {
        if (std::any_of(into.begin(), into.end(), [&](const auto &e) { return e.name == s.name; }))
            out << qs(s.name);
        else
            into.push_back(s);
    }
    return out;
}

// LoadStylesS: the "Style: " lines between the first "\nStyle: " and [Events].
StyleList stylesInFile(const QUrl &url)
{
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = f.readAll();
    const std::string text(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    const auto start = text.find("\nStyle: "), end = text.find("[Events]");
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return {};
    return application::readCatalog(text.substr(start, end - start));
}

} // namespace

StyleManagerController::StyleManagerController(std::filesystem::path catalogDir, Hooks hooks, QObject *parent)
    : QObject(parent), m_catalogs(std::move(catalogDir)), m_hooks(std::move(hooks))
{
}

QStringList StyleManagerController::catalogs() const
{
    QStringList out;
    for (const auto &n : m_catalogs.names())
        out << qs(n);
    return out;
}

QString StyleManagerController::catalog() const
{
    return qs(m_catalogs.current());
}

QStringList StyleManagerController::storeStyles() const
{
    QStringList out;
    for (const auto &s : m_catalogs.styles())
        out << qs(s.name);
    return out;
}

QStringList StyleManagerController::documentStyles() const
{
    QStringList out;
    if (auto *session = m_hooks.target ? m_hooks.target() : nullptr)
        if (const auto styles = application::documentStyles(*session))
            for (const auto &s : *styles)
                out << qs(s.name);
    return out;
}

bool StyleManagerController::available() const
{
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    return session && application::documentStyles(*session).has_value();
}

void StyleManagerController::refreshLists()
{
    if (auto *session = m_hooks.target ? m_hooks.target() : nullptr) {
        const auto last = session->document().scriptInfo(u8"Last Style Storage");
        if (last && !last->empty() && *last != m_catalogs.current())
            m_catalogs.choose(*last);
    }
    emit changed();
}

bool StyleManagerController::chooseCatalog(const QString &name)
{
    const bool ok = m_catalogs.choose(u8(name));
    emit changed();
    return ok;
}

bool StyleManagerController::createCatalog(const QString &name)
{
    const bool ok = m_catalogs.create(u8(name));
    emit changed();
    return ok;
}

bool StyleManagerController::deleteCatalog(const QString &name)
{
    const bool ok = m_catalogs.remove(u8(name));
    emit changed();
    return ok;
}

void StyleManagerController::saveCatalog()
{
    m_catalogs.save();
}

StyleList *StyleManagerController::list(bool store, std::optional<StyleList> &document) const
{
    if (store)
        return const_cast<StyleList *>(&m_catalogs.styles());
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    document = session ? application::documentStyles(*session) : std::nullopt;
    return document ? &*document : nullptr;
}

bool StyleManagerController::applyDocument(application::EditSession &session, const StyleList &styles,
                                           const std::vector<std::optional<std::size_t>> &origin,
                                           std::optional<std::pair<std::u8string, std::u8string>> rename)
{
    const bool ok = application::setDocumentStyles(session, styles, origin, std::move(rename)).has_value();
    if (m_hooks.refresh)
        m_hooks.refresh();
    emit changed();
    return ok;
}

QVariantMap StyleManagerController::beginEdit(bool store, int row)
{
    std::optional<StyleList> doc;
    const auto *styles = list(store, doc);
    if (!styles || row < 0 || static_cast<std::size_t>(row) >= styles->size())
        return {};
    m_edit = Edit{store, static_cast<std::size_t>(row), (*styles)[static_cast<std::size_t>(row)]};
    return valuesOf(m_edit->opened);
}

QVariantMap StyleManagerController::beginNew(bool store)
{
    std::optional<StyleList> doc;
    const auto *styles = list(store, doc);
    if (!styles)
        return {};
    m_edit = Edit{store, std::nullopt, application::defaultStyle(application::newStyleName(*styles))};
    return valuesOf(m_edit->opened);
}

QVariantMap StyleManagerController::beginCopy(bool store, int row)
{
    std::optional<StyleList> doc;
    const auto *styles = list(store, doc);
    if (!styles || row < 0 || static_cast<std::size_t>(row) >= styles->size())
        return {};
    auto copy = (*styles)[static_cast<std::size_t>(row)];
    copy.name = u8(tr("Copy of ")) + copy.name;
    m_edit = Edit{store, std::nullopt, copy};
    return valuesOf(copy);
}

QVariantMap StyleManagerController::commitQuestions(const QVariantMap &values) const
{
    if (!m_edit)
        return {};
    const auto style = styleOf(values, m_edit->opened);
    return {{QStringLiteral("fields"), application::compareStyles(m_edit->opened, style)},
            {QStringLiteral("rename"), !m_edit->store && m_edit->row && style.name != m_edit->opened.name}};
}

QString StyleManagerController::commitEdit(const QVariantMap &values, const QVariantList &selected, bool applyToSelected,
                                           bool renameLines)
{
    if (!m_edit)
        return {};
    std::optional<StyleList> doc;
    auto *styles = list(m_edit->store, doc);
    if (!styles)
        return tr("The Document has no styles to edit.");
    application::StyleEdit edit;
    edit.row = m_edit->row;
    edit.style = styleOf(values, m_edit->opened);
    edit.oldName = m_edit->opened.name;
    edit.selected = rowsOf(selected);
    edit.fields = applyToSelected ? application::compareStyles(m_edit->opened, edit.style) : 0;
    const StyleList before = *styles;
    const auto row = application::commitStyle(*styles, edit);
    if (!row)
        return tr("Style named \"%1\" already exists.").arg(qs(row.error()));
    if (m_edit->store) {
        m_catalogs.markChanged();
        emit changed();
    } else {
        std::vector<std::optional<std::size_t>> origin;
        for (std::size_t i = 0; i < styles->size(); ++i)
            origin.push_back(i < before.size() ? std::optional(i) : std::nullopt);
        std::optional<std::pair<std::u8string, std::u8string>> rename;
        if (renameLines && m_edit->row && edit.oldName != edit.style.name)
            rename = std::pair(edit.oldName, edit.style.name);
        applyDocument(*m_hooks.target(), *styles, origin, rename);
    }
    // The editor stays on the committed Style (Apply keeps it open).
    m_edit->row = *row;
    m_edit->opened = edit.style;
    return {};
}

void StyleManagerController::endEdit()
{
    m_edit.reset();
}

QStringList StyleManagerController::transferConflicts(bool toStore, const QVariantList &rows) const
{
    std::optional<StyleList> doc;
    auto *from = const_cast<StyleManagerController *>(this)->list(!toStore, doc);
    std::optional<StyleList> doc2;
    auto *into = const_cast<StyleManagerController *>(this)->list(toStore, doc2);
    if (!from || !into)
        return {};
    StyleList chosen;
    for (const auto r : rowsOf(rows))
        if (r < from->size())
            chosen.push_back((*from)[r]);
    return conflictsOf(*into, chosen);
}

QVariantList StyleManagerController::addToStore(const QVariantList &rows, const QStringList &answers)
{
    std::optional<StyleList> doc;
    const auto *from = list(false, doc);
    if (!from)
        return {};
    StyleList chosen;
    for (const auto r : rowsOf(rows))
        if (r < from->size())
            chosen.push_back((*from)[r]);
    const auto added = application::transferStyles(m_catalogs.styles(), chosen, answering(answers));
    m_catalogs.markChanged();
    emit changed();
    return listOf(added);
}

QVariantList StyleManagerController::addToDocument(const QVariantList &rows, const QStringList &answers)
{
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    auto styles = session ? application::documentStyles(*session) : std::nullopt;
    if (!styles)
        return {};
    StyleList chosen;
    for (const auto r : rowsOf(rows))
        if (r < m_catalogs.styles().size())
            chosen.push_back(m_catalogs.styles()[r]);
    const std::size_t before = styles->size();
    const auto added = application::transferStyles(*styles, chosen, answering(answers));
    std::vector<std::optional<std::size_t>> origin;
    for (std::size_t i = 0; i < styles->size(); ++i)
        origin.push_back(i < before ? std::optional(i) : std::nullopt);
    applyDocument(*session, *styles, origin);
    return listOf(added);
}

void StyleManagerController::addToAllDocuments(const QVariantList &rows, const QStringList &answers)
{
    StyleList chosen;
    for (const auto r : rowsOf(rows))
        if (r < m_catalogs.styles().size())
            chosen.push_back(m_catalogs.styles()[r]);
    // One run of answers over every ASS Document (legacy keeps its prompt across tabs).
    const auto ask = answering(answers);
    std::optional<Replace> sticky;
    const application::AskReplace shared = [&](const std::u8string &name) {
        if (sticky == Replace::YesToAll || sticky == Replace::Cancel)
            return *sticky;
        sticky = ask(name);
        return *sticky;
    };
    for (auto *session : m_hooks.documents ? m_hooks.documents() : std::vector<application::EditSession *>{}) {
        auto styles = application::documentStyles(*session);
        if (!styles)
            continue;
        const std::size_t before = styles->size();
        application::transferStyles(*styles, chosen, shared, false);
        std::vector<std::optional<std::size_t>> origin;
        for (std::size_t i = 0; i < styles->size(); ++i)
            origin.push_back(i < before ? std::optional(i) : std::nullopt);
        application::setDocumentStyles(*session, *styles, origin);
    }
    if (m_hooks.refresh)
        m_hooks.refresh();
    emit changed();
}

QStringList StyleManagerController::fileStyles(const QUrl &file)
{
    QStringList out;
    for (const auto &s : stylesInFile(file))
        out << qs(s.name);
    return out;
}

QStringList StyleManagerController::fileConflicts(bool toStore, const QUrl &file, const QStringList &names) const
{
    std::optional<StyleList> doc;
    auto *into = const_cast<StyleManagerController *>(this)->list(toStore, doc);
    if (!into)
        return {};
    StyleList chosen;
    for (const auto &s : stylesInFile(file))
        if (names.contains(qs(s.name)))
            chosen.push_back(s);
    return conflictsOf(*into, chosen);
}

void StyleManagerController::loadFromFile(bool toStore, const QUrl &file, const QStringList &names, const QStringList &answers)
{
    StyleList chosen;
    for (const auto &s : stylesInFile(file))
        if (names.contains(qs(s.name)))
            chosen.push_back(s);
    if (toStore) {
        application::transferStyles(m_catalogs.styles(), chosen, answering(answers), false);
        m_catalogs.markChanged();
        emit changed();
        return;
    }
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    auto styles = session ? application::documentStyles(*session) : std::nullopt;
    if (!styles)
        return;
    const std::size_t before = styles->size();
    application::transferStyles(*styles, chosen, answering(answers), false);
    std::vector<std::optional<std::size_t>> origin;
    for (std::size_t i = 0; i < styles->size(); ++i)
        origin.push_back(i < before ? std::optional(i) : std::nullopt);
    applyDocument(*session, *styles, origin);
}

QVariantList StyleManagerController::removeStyles(bool store, const QVariantList &rows)
{
    const auto chosen = rowsOf(rows);
    if (store) {
        auto &styles = m_catalogs.styles();
        for (auto it = chosen.rbegin(); it != chosen.rend(); ++it)
            if (*it < styles.size())
                styles.erase(styles.begin() + static_cast<std::ptrdiff_t>(*it));
        m_catalogs.markChanged();
        emit changed();
        return {0};
    }
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    auto styles = session ? application::documentStyles(*session) : std::nullopt;
    if (!styles)
        return {};
    StyleList kept;
    std::vector<std::optional<std::size_t>> origin;
    for (std::size_t i = 0; i < styles->size(); ++i)
        if (!std::binary_search(chosen.begin(), chosen.end(), i)) {
            kept.push_back((*styles)[i]);
            origin.push_back(i);
        }
    applyDocument(*session, kept, origin);
    return {0};
}

void StyleManagerController::sortStyles(bool store)
{
    const QCollator collator;
    const application::NameCompare compare = [&](std::u8string_view a, std::u8string_view b) {
        return collator.compare(qs(std::u8string(a)), qs(std::u8string(b)));
    };
    if (store) {
        application::sortStyles(m_catalogs.styles(), compare);
        m_catalogs.markChanged();
        emit changed();
        return;
    }
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    auto styles = session ? application::documentStyles(*session) : std::nullopt;
    if (!styles)
        return;
    // Track where each Style came from so unchanged ones keep their bytes.
    std::vector<std::pair<core::StyleValues, std::size_t>> tagged;
    for (std::size_t i = 0; i < styles->size(); ++i)
        tagged.emplace_back((*styles)[i], i);
    std::stable_sort(tagged.begin(), tagged.end(),
                     [&](const auto &a, const auto &b) { return compare(a.first.name, b.first.name) < 0; });
    StyleList sorted;
    std::vector<std::optional<std::size_t>> origin;
    for (const auto &[s, i] : tagged) {
        sorted.push_back(s);
        origin.push_back(i);
    }
    applyDocument(*session, sorted, origin);
}

QVariantList StyleManagerController::moveStyles(bool store, const QVariantList &rows, int kind)
{
    const auto move = static_cast<application::StyleMove>(std::clamp(kind, 0, 3));
    if (store) {
        const auto moved = application::moveStyles(m_catalogs.styles(), rowsOf(rows), move);
        m_catalogs.markChanged();
        emit changed();
        return listOf(moved);
    }
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    auto styles = session ? application::documentStyles(*session) : std::nullopt;
    if (!styles)
        return {};
    // Move the original rows the same way to know where each Style came from.
    StyleList indices;
    for (std::size_t i = 0; i < styles->size(); ++i) {
        core::StyleValues tag;
        tag.name = std::u8string(reinterpret_cast<const char8_t *>(std::to_string(i).c_str()));
        indices.push_back(tag);
    }
    const auto moved = application::moveStyles(*styles, rowsOf(rows), move);
    application::moveStyles(indices, rowsOf(rows), move);
    std::vector<std::optional<std::size_t>> origin;
    for (const auto &t : indices)
        origin.push_back(static_cast<std::size_t>(std::stoul(std::string(t.name.begin(), t.name.end()))));
    applyDocument(*session, *styles, origin);
    return listOf(moved);
}

QString StyleManagerController::cleanStyles()
{
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    if (!session)
        return {};
    const auto result = application::cleanDocumentStyles(*session);
    if (m_hooks.refresh)
        m_hooks.refresh();
    emit changed();
    if (!result)
        return {};
    QString used, deleted;
    for (const auto &n : result->used)
        used += qs(n) + u'\n';
    for (const auto &n : result->deleted)
        deleted += qs(n) + u'\n';
    if (used.isEmpty())
        used = tr("None");
    if (deleted.isEmpty())
        deleted = tr("None");
    return tr("Styles used:\n%1\nStyles deleted:\n%2").arg(used, deleted);
}

void StyleManagerController::renderPreview(const QVariantMap &values, int width, int height, const QString &text)
{
    if (width < 1 || height < 1)
        return;
    const core::StyleValues style = styleOf(values, m_edit ? m_edit->opened : application::defaultStyle());
    m_preview = renderStylePreview(m_renderer, style, width, height, text, m_hooks.fonts ? m_hooks.fonts() : std::vector<application::FontLease>());
    ++m_previewKey;
    emit previewChanged();
}

QImage renderStylePreview(backends::LibassRenderer &renderer, core::StyleValues style, int width, int height,
                          const QString &text, std::vector<application::FontLease> fonts)
{
    // StylePreview::SubsText: the Style at alignment 5 on a script the preview's size.
    style.alignment = u8"5";
    const auto fields = core::legacy::styleRawFields(style);
    std::u8string line = u8"Style: ";
    for (std::size_t i = 0; i < fields.size(); ++i)
        line += (i ? u8"," : u8"") + fields[i];
    const std::u8string script =
        u8"[Script Info]\r\nPlayResX: " + u8(QString::number(width)) + u8"\r\nPlayResY: " + u8(QString::number(height)) +
        u8"\r\nScaledBorderAndShadow: Yes\r\nScriptType: v4.00+\r\nWrapStyle: 0\r\n[V4+ Styles]\r\n"
        u8"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        u8"Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
        u8"MarginV, Encoding\r\n" + line + u8"\r\n \r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
        u8"MarginV, Effect, Text\r\nDialogue: 0,0:00:00.00,1:01:26.00," + style.name + u8",,0000,0000,0000,," + u8(text) + u8"\r\n";
    application::RenderSnapshot snapshot;
    snapshot.script.resize(script.size());
    std::memcpy(snapshot.script.data(), script.data(), script.size());
    snapshot.fonts = std::move(fonts); // Y6: the external fonts
    // The checkered background: 10-pixel squares of the two preview colours.
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    const QRgb one = qRgb(0x43, 0x43, 0x43), two = qRgb(0x62, 0x62, 0x62);
    bool rowPhase = false;
    for (int y = 0; y < height; ++y) {
        if (y % 10 == 0)
            rowPhase = !rowPhase;
        bool phase = rowPhase;
        auto *rowPixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            if (x % 10 == 0 && x > 0)
                phase = !phase;
            rowPixels[x] = phase ? one : two;
        }
    }
    if (renderer.prepare(std::move(snapshot))) {
        if (const auto frame = renderer.render(core::DocumentTime(1'000'000), width, height); frame && !frame->empty) {
            // Premultiplied BGRA over the background.
            for (int y = 0; y < height; ++y) {
                auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
                const auto *src = frame->pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame->stride);
                for (int x = 0; x < width; ++x) {
                    const int b = src[x * 4], g = src[x * 4 + 1], r = src[x * 4 + 2], a = src[x * 4 + 3];
                    const QRgb d = dst[x];
                    dst[x] = qRgb(r + qRed(d) * (255 - a) / 255, g + qGreen(d) * (255 - a) / 255, b + qBlue(d) * (255 - a) / 255);
                }
            }
        }
    }
    return image;
}

namespace {

class StylePreviewProvider : public QQuickImageProvider {
public:
    explicit StylePreviewProvider(StyleManagerController &controller)
        : QQuickImageProvider(QQuickImageProvider::Image), m_controller(controller)
    {
    }
    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        const QImage image = m_controller.preview();
        if (size)
            *size = image.size();
        return image;
    }

private:
    StyleManagerController &m_controller;
};

} // namespace

void attachStylePreview(QQmlEngine &engine, StyleManagerController &controller)
{
    engine.addImageProvider(QStringLiteral("stylepreview"), new StylePreviewProvider(controller));
}

} // namespace hikari::app
