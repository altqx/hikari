#include "hikari/app/style_manager_controller.h"

#include "hikari/core/ass_load.h"

#include <QColor>
#include <QCoreApplication>
#include <QCollator>
#include <QColor>
#include <QQuickImageProvider>
#include <QSet>
#include <QFile>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace hikari::app {

namespace {

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

int fieldsOf(const QStringList &keys)
{
    namespace f = application::style_field;
    static const std::unordered_map<QString, int> flags{
        {QStringLiteral("fontname"), f::FontName},         {QStringLiteral("fontsize"), f::FontSize},
        {QStringLiteral("bold"), f::Bold},                 {QStringLiteral("italic"), f::Italic},
        {QStringLiteral("underline"), f::Underline},       {QStringLiteral("strikeOut"), f::StrikeOut},
        {QStringLiteral("primary"), f::Primary},           {QStringLiteral("secondary"), f::Secondary},
        {QStringLiteral("outline"), f::Outline},           {QStringLiteral("back"), f::Shadow},
        {QStringLiteral("outlineWidth"), f::OutlineWidth}, {QStringLiteral("shadow"), f::ShadowWidth},
        {QStringLiteral("scaleX"), f::ScaleX},             {QStringLiteral("scaleY"), f::ScaleY},
        {QStringLiteral("angle"), f::Angle},               {QStringLiteral("spacing"), f::Spacing},
        {QStringLiteral("borderStyle"), f::BorderStyle},   {QStringLiteral("alignment"), f::Alignment},
        {QStringLiteral("marginLeft"), f::MarginLeft},     {QStringLiteral("marginRight"), f::MarginRight},
        {QStringLiteral("marginVertical"), f::MarginVertical}, {QStringLiteral("encoding"), f::Encoding}};
    int out = 0;
    for (const QString &key : keys)
        if (const auto it = flags.find(key); it != flags.end())
            out |= it->second;
    return out;
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

application::ClashChoices choicesOf(const QVariantList &choices)
{
    application::ClashChoices out;
    for (const auto &v : choices) {
        const auto m = v.toMap();
        const QString action = m.value(QStringLiteral("action")).toString();
        application::ClashChoice c;
        c.action = action == QLatin1String("replace")  ? application::ClashAction::Replace
                   : action == QLatin1String("rename") ? application::ClashAction::Rename
                                                       : application::ClashAction::Skip;
        c.newName = u8(m.value(QStringLiteral("newName")).toString().trimmed());
        if (c.action == application::ClashAction::Rename && c.newName.empty())
            c.action = application::ClashAction::Skip;
        out[{u8(m.value(QStringLiteral("name")).toString()), m.value(QStringLiteral("occurrence")).toInt()}] = std::move(c);
    }
    return out;
}

QVariantList clashesOf(const StyleList &into, const StyleList &styles)
{
    QVariantList out;
    std::unordered_map<std::u8string, const core::StyleValues *> first;
    for (auto it = into.rbegin(); it != into.rend(); ++it)
        first[it->name] = &*it;
    application::StyleNames all = application::namesOf(into);
    for (const auto &s : styles)
        all.insert(s.name);
    std::unordered_map<std::u8string, int> seen, next;
    for (const auto &s : styles) {
        const int occurrence = seen[s.name]++;
        const auto found = first.try_emplace(s.name, &s);
        if (found.second)
            continue;
        const int fields = application::compareStyles(*found.first->second, s);
        int &count = next.try_emplace(s.name, 2).first->second;
        std::u8string suggestion;
        for (;; ++count) {
            const auto n = std::to_string(count);
            suggestion = s.name + u8" (" + std::u8string(n.begin(), n.end()) + u8")";
            if (all.insert(suggestion).second)
                break;
        }
        out << QVariantMap{{QStringLiteral("name"), qs(s.name)},
                           {QStringLiteral("occurrence"), occurrence},
                           {QStringLiteral("fields"), fields},
                           {QStringLiteral("same"), fields == 0},
                           {QStringLiteral("suggestion"), qs(suggestion)}};
    }
    return out;
}

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

StyleList chosenInFile(const QUrl &url, const QStringList &names)
{
    const QSet<QString> wanted(names.begin(), names.end());
    StyleList chosen;
    for (auto &s : stylesInFile(url))
        if (wanted.contains(qs(s.name)))
            chosen.push_back(std::move(s));
    return chosen;
}

}

StyleManagerController::StyleManagerController(std::filesystem::path catalogDir, Hooks hooks, QObject *parent)
    : QObject(parent), m_catalogs(std::move(catalogDir)), m_hooks(std::move(hooks))
{
    if (m_hooks.lastCatalog)
        if (const QString last = m_hooks.lastCatalog(); !last.isEmpty())
            m_catalogs.choose(u8(last));
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

QVariantList StyleManagerController::panelRows() const
{
    QVariantList out;
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    const auto styles = session ? application::documentStyles(*session) : std::nullopt;
    if (!styles)
        return out;
    std::unordered_map<std::u8string, int> lines;
    for (const auto *line : session->document().lines())
        ++lines[line->style];
    const auto colour = [](const core::Colour &c) {
        return QColor(int(std::clamp<std::int64_t>(c.r, 0, 255)), int(std::clamp<std::int64_t>(c.g, 0, 255)),
                      int(std::clamp<std::int64_t>(c.b, 0, 255)), 255 - int(std::clamp<std::int64_t>(c.a, 0, 255)));
    };
    for (const auto &s : *styles) {
        const auto used = lines.find(s.name);
        out << QVariantMap{{QStringLiteral("name"), qs(s.name)},
                           {QStringLiteral("font"), qs(s.fontname)},
                           {QStringLiteral("size"), qs(s.fontsize)},
                           {QStringLiteral("bold"), s.bold},
                           {QStringLiteral("italic"), s.italic},
                           {QStringLiteral("primary"), colour(s.primary)},
                           {QStringLiteral("outline"), colour(s.outline)},
                           {QStringLiteral("lines"), used == lines.end() ? 0 : used->second}};
    }
    return out;
}

bool StyleManagerController::available() const
{
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    return session && application::documentStyles(*session).has_value();
}

void StyleManagerController::documentEdited()
{
    if (const bool now = available(); now != m_available) {
        m_available = now;
        emit changed();
    }
    const auto target = m_hooks.targetDocument ? m_hooks.targetDocument() : std::nullopt;
    const bool retargeted = target != m_notifiedTarget;
    m_notifiedTarget = target;
    emit documentChanged(retargeted);
}

QString StyleManagerController::followedCatalog() const
{
    if (auto *session = m_hooks.target ? m_hooks.target() : nullptr) {
        // Every new file names "Default", so it leaves the catalog chosen last as it is.
        const auto last = session->document().scriptInfo(u8"Last Style Storage");
        const auto names = m_catalogs.names();
        if (last && !last->empty() && *last != u8"Default" && std::ranges::find(names, *last) != names.end())
            return qs(*last);
    }
    return qs(m_catalogs.current());
}

void StyleManagerController::refreshLists()
{
    if (const QString name = followedCatalog(); name != catalog() && m_catalogs.choose(u8(name)))
        catalogChosen();
    emit changed();
}

bool StyleManagerController::chooseCatalog(const QString &name)
{
    const bool ok = loadCatalog(name);
    if (ok)
        catalogChosen();
    return ok;
}

bool StyleManagerController::loadCatalog(const QString &name)
{
    const bool ok = m_catalogs.choose(u8(name));
    emit changed();
    return ok;
}

bool StyleManagerController::createCatalog(const QString &name)
{
    const bool ok = m_catalogs.create(u8(name));
    if (ok)
        catalogChosen();
    emit changed();
    return ok;
}

bool StyleManagerController::deleteCatalog(const QString &name)
{
    const bool ok = m_catalogs.remove(u8(name));
    if (ok)
        catalogChosen();
    emit changed();
    return ok;
}

void StyleManagerController::catalogChosen()
{
    if (m_hooks.catalogChosen)
        m_hooks.catalogChosen(qs(m_catalogs.current()));
}

QVariantList StyleManagerController::styleRows(bool store) const
{
    std::optional<StyleList> doc;
    const auto *styles = list(store, doc);
    if (!styles)
        return {};
    std::unordered_map<std::u8string, int> lines;
    if (!store)
        for (const auto &line : m_hooks.target()->document().lines())
            ++lines[line->style];
    const auto hex = [](const core::Colour &c) { return QColor(c.r, c.g, c.b).name(); };
    QVariantList out;
    for (const auto &style : *styles) {
        const auto used = lines.find(style.name);
        out << QVariantMap{{QStringLiteral("name"), qs(style.name)},
                           {QStringLiteral("primary"), hex(style.primary)},
                           {QStringLiteral("outline"), hex(style.outline)},
                           {QStringLiteral("lines"), store ? -1 : used == lines.end() ? 0 : used->second}};
    }
    return out;
}

QStringList StyleManagerController::mixedFields(bool store, const QVariantList &rows) const
{
    std::optional<StyleList> doc;
    const auto *styles = list(store, doc);
    if (!styles)
        return {};
    QStringList out;
    std::optional<QVariantMap> first;
    for (const auto r : rowsOf(rows)) {
        if (r >= styles->size())
            break;
        const QVariantMap values = valuesOf((*styles)[r]);
        if (!first) {
            first = values;
            continue;
        }
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (it.key() != QLatin1String("name") && !out.contains(it.key()) && first->value(it.key()) != it.value())
                out << it.key();
    }
    return out;
}

void StyleManagerController::saveCatalog()
{
    m_catalogs.save();
}

void StyleManagerController::targetRefreshed()
{
    const auto now = m_hooks.targetDocument ? m_hooks.targetDocument() : std::nullopt;
    const bool switched = now != m_lastTarget;
    m_lastTarget = now;
    emit targetEdited(switched);
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
    pin(Edit{store, static_cast<std::size_t>(row), (*styles)[static_cast<std::size_t>(row)]});
    return valuesOf(m_edit->opened);
}

QVariantMap StyleManagerController::beginNew(bool store)
{
    std::optional<StyleList> doc;
    const auto *styles = list(store, doc);
    if (!styles)
        return {};
    pin(Edit{store, std::nullopt, application::defaultStyle(application::newStyleName(*styles))});
    return valuesOf(m_edit->opened);
}

QVariantMap StyleManagerController::beginNamed(const QString &name)
{
    std::optional<StyleList> doc;
    const auto *styles = list(false, doc);
    if (!styles || name.isEmpty())
        return {};
    const auto from = std::find_if(styles->begin(), styles->end(), [](const auto &s) { return s.name == u8"Default"; });
    auto style = from != styles->end() ? *from : application::defaultStyle();
    style.name = u8(name);
    pin(Edit{false, std::nullopt, style});
    return valuesOf(style);
}

QVariantMap StyleManagerController::beginCopy(bool store, int row)
{
    std::optional<StyleList> doc;
    const auto *styles = list(store, doc);
    if (!styles || row < 0 || static_cast<std::size_t>(row) >= styles->size())
        return {};
    auto copy = (*styles)[static_cast<std::size_t>(row)];
    copy.name = u8(QCoreApplication::translate("HikariSub.Styles", "Copy of ")) + copy.name;
    pin(Edit{store, std::nullopt, copy});
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
                                           bool renameLines, const QStringList &resolved)
{
    if (!m_edit)
        return {};
    auto *session = m_edit->store ? nullptr : editSession();
    std::optional<StyleList> doc = session ? application::documentStyles(*session) : std::nullopt;
    auto *styles = m_edit->store ? list(true, doc) : doc ? &*doc : nullptr;
    if (!styles)
        return QCoreApplication::translate("HikariSub.Styles", "The document has no styles to edit.");
    const std::u8string shown = m_catalogs.current();
    const bool away = m_edit->store && m_edit->catalog != shown;
    if (away && !m_catalogs.choose(m_edit->catalog))
        return QCoreApplication::translate("HikariSub.Styles", "The catalog “%1” no longer exists.").arg(qs(m_edit->catalog));
    application::StyleEdit edit;
    edit.row = m_edit->row;
    edit.style = styleOf(values, m_edit->opened);
    edit.oldName = m_edit->opened.name;
    edit.selected = rowsOf(selected);
    edit.fields = applyToSelected ? application::compareStyles(m_edit->opened, edit.style) | fieldsOf(resolved) : 0;
    const StyleList before = *styles;
    const auto row = application::commitStyle(*styles, edit);
    if (!row) {
        if (away)
            m_catalogs.choose(shown);
        return QCoreApplication::translate("HikariSub.Styles", "Style named \"%1\" already exists.").arg(qs(row.error()));
    }
    const bool store = m_edit->store;
    const bool renames = renameLines && m_edit->row && edit.oldName != edit.style.name;
    // MUST KILL: must be set before the lists are told, as a listener may end the edit.
    m_edit->row = *row;
    m_edit->opened = edit.style;
    if (store) {
        m_catalogs.markChanged();
        // MUST KILL: choosing the shown catalog back is what saves the edited one.
        if (away)
            m_catalogs.choose(shown);
        emit changed();
    } else {
        std::vector<std::optional<std::size_t>> origin;
        for (std::size_t i = 0; i < styles->size(); ++i)
            origin.push_back(i < before.size() ? std::optional(i) : std::nullopt);
        std::optional<std::pair<std::u8string, std::u8string>> rename;
        if (renames)
            rename = std::pair(edit.oldName, edit.style.name);
        applyDocument(*session, *styles, origin, rename);
    }
    return {};
}

void StyleManagerController::endEdit()
{
    m_edit.reset();
}

bool StyleManagerController::editOpen() const
{
    return m_edit && (m_edit->store || editSession());
}

QString StyleManagerController::editCatalog() const
{
    return m_edit && m_edit->store ? qs(m_edit->catalog) : QString();
}

QString StyleManagerController::listsKey() const
{
    const auto number = [](std::uint64_t n) {
        const auto text = std::to_string(n);
        return std::u8string(text.begin(), text.end());
    };
    const auto target = m_hooks.targetDocument ? m_hooks.targetDocument() : std::nullopt;
    std::u8string key = target ? number(target->value) : std::u8string(u8"-");
    key += u8'\n';
    key += m_catalogs.current();
    for (const bool store : {true, false}) {
        std::optional<StyleList> doc;
        const auto *styles = list(store, doc);
        key += u8'\n';
        key += number(styles ? styles->size() : 0);
        if (styles)
            for (const auto &style : *styles) {
                key += u8'\n';
                key += style.name;
            }
    }
    return qs(key);
}

void StyleManagerController::pin(Edit edit)
{
    if (!edit.store && m_hooks.targetDocument)
        edit.document = m_hooks.targetDocument();
    if (edit.store)
        edit.catalog = m_catalogs.current();
    m_edit = std::move(edit);
}

application::EditSession *StyleManagerController::editSession() const
{
    if (!m_edit || !m_edit->document || !m_hooks.session)
        return nullptr;
    return m_hooks.session(*m_edit->document);
}

QVariantList StyleManagerController::transferClashes(bool toStore, const QVariantList &rows) const
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
    return clashesOf(*into, chosen);
}

QVariantList StyleManagerController::addToStore(const QVariantList &rows, const QVariantList &choices)
{
    std::optional<StyleList> doc;
    const auto *from = list(false, doc);
    if (!from)
        return {};
    StyleList chosen;
    for (const auto r : rowsOf(rows))
        if (r < from->size())
            chosen.push_back((*from)[r]);
    const auto added = application::transferStyles(m_catalogs.styles(), chosen, choicesOf(choices));
    m_catalogs.markChanged();
    emit changed();
    return listOf(added);
}

QVariantList StyleManagerController::addToDocument(const QVariantList &rows, const QVariantList &choices)
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
    const auto added = application::transferStyles(*styles, chosen, choicesOf(choices));
    std::vector<std::optional<std::size_t>> origin;
    for (std::size_t i = 0; i < styles->size(); ++i)
        origin.push_back(i < before ? std::optional(i) : std::nullopt);
    applyDocument(*session, *styles, origin);
    return listOf(added);
}

void StyleManagerController::addToAllDocuments(const QVariantList &rows, const QVariantList &choices)
{
    StyleList chosen;
    for (const auto r : rowsOf(rows))
        if (r < m_catalogs.styles().size())
            chosen.push_back(m_catalogs.styles()[r]);
    const auto shared = choicesOf(choices);
    for (auto *session : m_hooks.documents ? m_hooks.documents() : std::vector<application::EditSession *>{}) {
        auto styles = application::documentStyles(*session);
        if (!styles)
            continue;
        const std::size_t before = styles->size();
        application::transferStyles(*styles, chosen, shared);
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

QVariantList StyleManagerController::fileClashes(bool toStore, const QUrl &file, const QStringList &names) const
{
    std::optional<StyleList> doc;
    auto *into = const_cast<StyleManagerController *>(this)->list(toStore, doc);
    if (!into)
        return {};
    const StyleList chosen = chosenInFile(file, names);
    return clashesOf(*into, chosen);
}

void StyleManagerController::loadFromFile(bool toStore, const QUrl &file, const QStringList &names, const QVariantList &choices)
{
    const StyleList chosen = chosenInFile(file, names);
    if (toStore) {
        application::transferStyles(m_catalogs.styles(), chosen, choicesOf(choices));
        m_catalogs.markChanged();
        emit changed();
        return;
    }
    auto *session = m_hooks.target ? m_hooks.target() : nullptr;
    auto styles = session ? application::documentStyles(*session) : std::nullopt;
    if (!styles)
        return;
    const std::size_t before = styles->size();
    application::transferStyles(*styles, chosen, choicesOf(choices));
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

QVariantMap StyleManagerController::deleteUnusedStyles()
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
    QStringList usedNames, deletedNames;
    QString used, deleted;
    for (const auto &n : result->used) {
        usedNames << qs(n);
        used += qs(n) + u'\n';
    }
    for (const auto &n : result->deleted) {
        deletedNames << qs(n);
        deleted += qs(n) + u'\n';
    }
    if (used.isEmpty())
        used = QCoreApplication::translate("HikariSub.Styles", "None");
    if (deleted.isEmpty())
        deleted = QCoreApplication::translate("HikariSub.Styles", "None");
    return {{QStringLiteral("used"), usedNames},
            {QStringLiteral("deleted"), deletedNames},
            {QStringLiteral("report"),
             QCoreApplication::translate("HikariSub.Styles", "Styles used:\n%1\nStyles deleted:\n%2").arg(used, deleted)}};
}

void StyleManagerController::renderPreview(const QVariantMap &values, int width, int height, const QString &text,
                                           qreal scale)
{
    if (width < 1 || height < 1)
        return;
    const core::StyleValues style = styleOf(values, m_edit ? m_edit->opened : application::defaultStyle());
    m_preview = renderStylePreview(m_renderer, style, width, height, text,
                                   m_hooks.fonts ? m_hooks.fonts() : std::vector<application::FontLease>(),
                                   m_atScreenSize ? scale : 1, false);
    ++m_previewKey;
    emit previewChanged();
}

QImage renderStylePreview(backends::LibassRenderer &renderer, core::StyleValues style, int width, int height,
                          const QString &text, std::vector<application::FontLease> fonts, qreal scale, bool wrap)
{
    style.alignment = u8"5";
    const auto fields = core::legacy::styleRawFields(style);
    std::u8string line = u8"Style: ";
    for (std::size_t i = 0; i < fields.size(); ++i)
        line += (i ? u8"," : u8"") + fields[i];
    const std::u8string script =
        u8"[Script Info]\r\nPlayResX: " + u8(QString::number(width)) + u8"\r\nPlayResY: " + u8(QString::number(height)) +
        u8"\r\nScaledBorderAndShadow: Yes\r\nScriptType: v4.00+\r\nWrapStyle: " + (wrap ? u8"0" : u8"2") +
        u8"\r\n[V4+ Styles]\r\n"
        u8"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        u8"Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
        u8"MarginV, Encoding\r\n" + line + u8"\r\n \r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
        u8"MarginV, Effect, Text\r\nDialogue: 0,0:00:00.00,1:01:26.00," + style.name + u8",,0000,0000,0000,," + u8(text) + u8"\r\n";
    application::RenderSnapshot snapshot;
    snapshot.script.resize(script.size());
    std::memcpy(snapshot.script.data(), script.data(), script.size());
    snapshot.fonts = std::move(fonts);
    const qreal s = scale > 1 ? scale : 1;
    const int pixelWidth = std::max(1, qRound(width * s)), pixelHeight = std::max(1, qRound(height * s));
    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    const QRgb one = qRgb(0x43, 0x43, 0x43), two = qRgb(0x62, 0x62, 0x62);
    const auto square = [s](int pixel) { return static_cast<int>(pixel / s) / 10; };
    for (int y = 0; y < pixelHeight; ++y) {
        const bool rowPhase = square(y) % 2 == 0;
        auto *rowPixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < pixelWidth; ++x)
            rowPixels[x] = (square(x) % 2 == 0) == rowPhase ? one : two;
    }
    // Format_ARGB32's bytes are B, G, R, A on little-endian.
    static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN);
    if (renderer.prepare(std::move(snapshot)))
        if (const auto frame = application::renderOverlay(renderer, core::DocumentTime(1'000'000),
                                                          {pixelWidth, pixelHeight, width, height}))
            application::compositeOverlay(*frame, image.bits(), pixelWidth, pixelHeight, image.bytesPerLine());
    image.setDevicePixelRatio(s);
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

}

void attachStylePreview(QQmlEngine &engine, StyleManagerController &controller)
{
    engine.addImageProvider(QStringLiteral("stylepreview"), new StylePreviewProvider(controller));
}

}
