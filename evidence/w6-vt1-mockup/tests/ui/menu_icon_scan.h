#pragma once

// IC1 (#226): reads the QML sources for the shell's menus, so that a test can
// require an icon on every menu and context-menu item. A small reader, not a
// QML parser: it finds the object blocks ("Type {"), matches their braces
// with string literals and comments blanked out, and reads a block's own
// properties (its text without the nested blocks and brace expressions).

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>

namespace menu_icon_scan {

struct Block {
    QString type;
    qsizetype typeAt = 0; // where the type name starts
    qsizetype open = 0;   // its "{"
    qsizetype close = 0;  // its matching "}"
    int parent = -1;
};

struct QmlFile {
    QString name;
    QString text;  // the source
    QString code;  // the source with its comments as spaces
    QString blank; // the source with string contents and comments as spaces
    QList<Block> blocks;

    int line(qsizetype at) const { return int(text.left(at).count(QLatin1Char('\n'))) + 1; }

    // The block's own text: its body without its nested blocks or any other
    // brace expression (handlers, object literals), from the code.
    QString own(int index) const
    {
        const Block &b = blocks[index];
        QString out;
        int depth = 0;
        for (qsizetype i = b.open + 1; i < b.close; ++i) {
            const QChar c = blank[i];
            if (c == QLatin1Char('{'))
                ++depth;
            else if (c == QLatin1Char('}'))
                --depth;
            else if (depth == 0)
                out += code[i];
        }
        return out;
    }

    // What stands before the block's type on its line ("delegate: ").
    QString lead(int index) const
    {
        const qsizetype at = blocks[index].typeAt;
        const qsizetype lineStart = blank.lastIndexOf(QLatin1Char('\n'), at - 1) + 1;
        return blank.mid(lineStart, at - lineStart).trimmed();
    }
};

// The text with its comments as spaces, and with its string contents too
// unless keepStrings.
inline QString blankOut(const QString &text, bool keepStrings = false)
{
    QString out = text;
    const qsizetype n = text.size();
    qsizetype i = 0;
    while (i < n) {
        const QChar c = text[i];
        if (c == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('/')) {
            while (i < n && text[i] != QLatin1Char('\n'))
                out[i++] = QLatin1Char(' ');
        } else if (c == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('*')) {
            while (i < n && !(text[i] == QLatin1Char('*') && i + 1 < n && text[i + 1] == QLatin1Char('/'))) {
                if (text[i] != QLatin1Char('\n'))
                    out[i] = QLatin1Char(' ');
                ++i;
            }
            for (int k = 0; k < 2 && i < n; ++k)
                out[i++] = QLatin1Char(' ');
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            ++i;
            while (i < n && text[i] != c) {
                if (text[i] == QLatin1Char('\\') && i + 1 < n) {
                    if (!keepStrings)
                        out[i] = QLatin1Char(' ');
                    ++i;
                }
                if (!keepStrings && text[i] != QLatin1Char('\n'))
                    out[i] = QLatin1Char(' ');
                ++i;
            }
            ++i;
        } else {
            ++i;
        }
    }
    return out;
}

inline QmlFile read(const QString &path)
{
    QmlFile file;
    file.name = QFileInfo(path).fileName();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return file;
    file.text = QString::fromUtf8(f.readAll());
    file.code = blankOut(file.text, true);
    file.blank = blankOut(file.text);
    // An object declaration: a type name (capitalised, perhaps qualified)
    // and its brace, not a property's name.
    static const QRegularExpression object(QStringLiteral(R"re((?:^|[^\w.])((?:\w+\.)*[A-Z]\w*)\s*\{)re"));
    for (auto m = object.globalMatch(file.blank); m.hasNext();) {
        const auto match = m.next();
        Block b;
        b.type = match.captured(1).section(QLatin1Char('.'), -1);
        b.typeAt = match.capturedStart(1);
        b.open = match.capturedEnd(0) - 1;
        int depth = 0;
        b.close = file.blank.size();
        for (qsizetype i = b.open; i < file.blank.size(); ++i) {
            if (file.blank[i] == QLatin1Char('{'))
                ++depth;
            else if (file.blank[i] == QLatin1Char('}') && --depth == 0) {
                b.close = i;
                break;
            }
        }
        for (int p = int(file.blocks.size()) - 1; p >= 0; --p) {
            if (file.blocks[p].open < b.open && file.blocks[p].close > b.open) {
                b.parent = p;
                break;
            }
        }
        file.blocks.append(b);
    }
    return file;
}

inline bool hasProperty(const QString &own, const QString &name)
{
    return QRegularExpression(QStringLiteral(R"re((?:^|[\s;{])%1\s*:)re").arg(name)).match(own).hasMatch();
}

// A check item: checkable, unless it says false.
inline bool isCheckable(const QString &own)
{
    static const QRegularExpression checkable(QStringLiteral(R"re((?:^|[\s;{])checkable\s*:(?!\s*false\b))re"));
    return checkable.match(own).hasMatch();
}

inline QString shown(const QString &own)
{
    static const QRegularExpression text(QStringLiteral(R"re((?:^|[\s;{])(?:text|title|objectName)\s*:\s*([^;\n]*))re"));
    const auto match = text.match(own);
    if (match.hasMatch())
        return match.captured(1).trimmed();
    return own.simplified().isEmpty() ? QStringLiteral("(no text)") : own.simplified().left(80);
}

struct Exemption {
    QString file;
    QString needle; // a piece of the item's own text that identifies it
    QString reason;
};

// The menu items without an icon: an item of a menu (ShellMenuItem) or a
// submenu (a ShellMenu, or a type built on it, inside a menu) without an
// iconRole, which is no check item, no menu's item template (its
// `delegate`) and no exemption. Each exemption that matched nothing is
// reported too, as stale.
inline QStringList itemsWithoutIcons(const QString &dir, const QList<Exemption> &exemptions)
{
    QList<QmlFile> files;
    QDirIterator it(dir, {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        // K2: the controls style defines Menu itself (ShellMenu builds on it).
        if (path.startsWith(dir + QStringLiteral("/style/")))
            continue;
        const QString name = QFileInfo(path).fileName();
        if (name == QLatin1String("ShellMenu.qml") || name == QLatin1String("ShellMenuItem.qml"))
            continue;
        files.append(read(path));
    }
    // The menu types: ShellMenu and the files whose root is one; such a root
    // with an iconRole gives every use its icon.
    QSet<QString> menuTypes{QStringLiteral("ShellMenu")};
    QHash<QString, bool> rootIcon;
    for (bool grew = true; grew;) {
        grew = false;
        for (const QmlFile &file : files) {
            const QString type = file.name.chopped(4);
            if (!file.blocks.isEmpty() && menuTypes.contains(file.blocks.first().type) && !menuTypes.contains(type)) {
                menuTypes.insert(type);
                rootIcon.insert(type, hasProperty(file.own(0), QStringLiteral("iconRole")));
                grew = true;
            }
        }
    }
    QStringList problems;
    QList<bool> used(exemptions.size(), false);
    for (const QmlFile &file : files) {
        for (int i = 0; i < file.blocks.size(); ++i) {
            const Block &b = file.blocks[i];
            const bool item = b.type == QLatin1String("ShellMenuItem");
            const bool menu = menuTypes.contains(b.type);
            if (!item && !menu)
                continue;
            int p = b.parent;
            while (p >= 0 && file.blocks[p].type == QLatin1String("Instantiator"))
                p = file.blocks[p].parent;
            const bool inMenu = p >= 0 && menuTypes.contains(file.blocks[p].type);
            if (menu && !inMenu)
                continue; // a menu of its own (menu bar, context menu, file root)
            if (item && b.parent >= 0 && menuTypes.contains(file.blocks[b.parent].type)
                && file.lead(i).startsWith(QLatin1String("delegate")))
                continue; // the menu's item template
            const QString own = file.own(i);
            if (hasProperty(own, QStringLiteral("iconRole")) || (menu && rootIcon.value(b.type)))
                continue;
            if (item && isCheckable(own))
                continue;
            bool exempt = false;
            for (int e = 0; e < exemptions.size(); ++e) {
                if (exemptions[e].file == file.name && own.contains(exemptions[e].needle)) {
                    used[e] = true;
                    exempt = true;
                }
            }
            if (!exempt)
                problems << QStringLiteral("%1:%2: %3 %4 has no iconRole")
                                .arg(file.name).arg(file.line(b.typeAt)).arg(b.type, shown(own));
        }
    }
    for (int e = 0; e < exemptions.size(); ++e) {
        if (!used[e])
            problems << QStringLiteral("%1: the exemption \"%2\" matches no menu item").arg(exemptions[e].file, exemptions[e].needle);
    }
    return problems;
}

} // namespace menu_icon_scan
