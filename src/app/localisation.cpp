#include "hikari/app/localisation.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QStringDecoder>
#include <QTranslator>

#include <array>

namespace hikari::app {

namespace {

QString languagePart(const QString &tag)
{
    return tag.section(u'_', 0, 0).section(u'-', 0, 0);
}

QString accented(const QString &source)
{
    static const QString lower = QStringLiteral("åƀçđéƒĝĥîĵķļɱñöþǫŕšţûṽŵẋýž");
    static const QString upper = QStringLiteral("ÅƁÇĐÉƑĜĤÎĴĶĻṀÑÖÞǪŔŠŢÛṼŴẊÝŽ");
    QString out;
    out.reserve(source.size() * 2);
    qsizetype letters = 0;
    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar c = source[i];
        if (c == u'%' && i + 1 < source.size()) {
            qsizetype j = i + 1;
            if (source[j] == u'L')
                ++j;
            if (j < source.size() && (source[j].isDigit() || source[j] == u'n')) {
                while (j + 1 < source.size() && source[j].isDigit() && source[j + 1].isDigit())
                    ++j;
                out += source.mid(i, j - i + 1);
                i = j;
                continue;
            }
        }
        if (c == u'&' && i + 1 < source.size()) {
            const qsizetype end = source.indexOf(u';', i);
            const auto entity = [&] {
                if (end <= i + 1 || end - i > 8)
                    return false;
                for (qsizetype k = i + 1; k < end; ++k)
                    if (!source[k].isLetterOrNumber() && !(k == i + 1 && source[k] == u'#'))
                        return false;
                return true;
            };
            if (end > i && entity()) {
                out += source.mid(i, end - i + 1);
                i = end;
            } else {
                out += source.mid(i, 2);
                ++i;
            }
            continue;
        }
        if (c == u'<') {
            const qsizetype end = source.indexOf(u'>', i);
            if (end > i) {
                out += source.mid(i, end - i + 1);
                i = end;
                continue;
            }
        }
        if (c == u'(' && i + 1 < source.size() && source[i + 1] == u'*') {
            const qsizetype end = source.indexOf(u')', i);
            if (end > i) {
                out += source.mid(i, end - i + 1);
                i = end;
                continue;
            }
        }
        if (c == u'\\' && i + 1 < source.size()) {
            out += source.mid(i, 2); // \N, \n, \h: ASS escapes in help texts
            ++i;
            continue;
        }
        if (c >= u'a' && c <= u'z') {
            out += lower[c.unicode() - u'a'];
            ++letters;
        } else if (c >= u'A' && c <= u'Z') {
            out += upper[c.unicode() - u'A'];
            ++letters;
        } else {
            out += c;
        }
    }
    if (letters == 0)
        return source;
    return QStringLiteral("[") + out + QString((letters + 2) / 3, u'~') + QStringLiteral("]");
}

class PseudoTranslator final : public QTranslator {
public:
    explicit PseudoTranslator(bool rightToLeft) : m_rightToLeft(rightToLeft) {}
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *source, const char *, int) const override
    {
        if (!source || !*source)
            return {};
        if (qstrcmp(context, "QGuiApplication") == 0 && qstrcmp(source, "QT_LAYOUT_DIRECTION") == 0)
            return m_rightToLeft ? QStringLiteral("RTL") : QStringLiteral("LTR");
        const QString text = accented(QString::fromUtf8(source));
        if (!m_rightToLeft)
            return text;
        // Bidi marks around a right-to-left override make the text run and align right to left.
        return QStringLiteral("‏‮") + text + QStringLiteral("‬‏");
    }

private:
    bool m_rightToLeft;
};

} // namespace

Localisation::Localisation(QString catalogDir, QObject *parent)
    : QObject(parent), m_dir(std::move(catalogDir)), m_qtDir(QLibraryInfo::path(QLibraryInfo::TranslationsPath))
{
    QCoreApplication::instance()->installEventFilter(this);
    // Installed first, so every other catalog is searched before it.
    m_english = std::make_unique<QTranslator>();
    if (m_english->load(QStringLiteral("hikarisub_en"), m_dir))
        QCoreApplication::installTranslator(m_english.get());
    else
        m_english.reset();
}

Localisation::~Localisation()
{
    for (auto *t : {m_pseudoTranslator.get(), m_app.get(), m_qt.get(), m_english.get()})
        if (t)
            QCoreApplication::removeTranslator(t);
    if (m_rightToLeft)
        QGuiApplication::setLayoutDirection(Qt::LeftToRight);
}

QStringList Localisation::catalogLanguages() const
{
    QStringList out;
    const QDir dir(m_dir);
    for (const QString &file : dir.entryList({QStringLiteral("hikarisub_*.qm")}, QDir::Files)) {
        // MUST KILL: 10 and 13 cut "hikarisub_" and ".qm" off the file name.
        const QString tag = file.mid(10, file.size() - 13);
        if (!tag.startsWith(QLatin1String("gettext_")) && tag != u"en")
            out << tag;
    }
    out.sort();
    return out;
}

QStringList Localisation::translationsOf(const char *context, const QStringList &sources) const
{
    QStringList out;
    for (const QString &language : catalogLanguages()) {
        QTranslator catalog;
        if (!catalog.load(QStringLiteral("hikarisub_") + language, m_dir))
            continue;
        for (const QString &source : sources) {
            const QString text = catalog.translate(context, source.toUtf8().constData());
            if (!text.isEmpty() && text != source && !out.contains(text))
                out << text;
        }
    }
    return out;
}

QString Localisation::resolve(const QString &tag, const QStringList &available, bool *found)
{
    if (found)
        *found = true;
    if (tag.isEmpty() || tag == u"en" || tag == u"0" || tag == u"1")
        return {};
    if (available.contains(tag))
        return tag;
    const QString language = languagePart(tag);
    for (const QString &candidate : available)
        if (languagePart(candidate) == language)
            return candidate;
    if (found)
        *found = language == u"en";
    return {};
}

QString Localisation::firstStartLanguage(const QStringList &uiLanguages)
{
    return !uiLanguages.isEmpty() && languagePart(uiLanguages.first()) == u"pl" ? QStringLiteral("pl") : QString();
}

Localisation::Pseudo Localisation::pseudoNamed(const QString &name)
{
    if (name == u"accents")
        return Pseudo::Accents;
    if (name == u"rtl")
        return Pseudo::RightToLeft;
    return Pseudo::None;
}

bool Localisation::setLanguage(const QString &tag)
{
    bool found = true;
    const QString language = resolve(tag, catalogLanguages(), &found);
    if (language != m_language) {
        m_language = language;
        install();
    }
    return found;
}

void Localisation::setPseudo(Pseudo pseudo)
{
    if (pseudo == m_pseudo)
        return;
    m_pseudo = pseudo;
    install();
}

void Localisation::install()
{
    std::unique_ptr<QTranslator> qt, app, gettext, pseudo;
    if (!m_language.isEmpty()) {
        app = std::make_unique<QTranslator>();
        if (!app->load(QStringLiteral("hikarisub_") + m_language, m_dir))
            app.reset();
        gettext = std::make_unique<QTranslator>();
        if (!gettext->load(QStringLiteral("hikarisub_gettext_") + m_language, m_dir))
            gettext.reset();
        qt = std::make_unique<QTranslator>();
        if (!qt->load(QLocale(m_language), QStringLiteral("qt"), QStringLiteral("_"), m_qtDir))
            qt.reset();
    }
    if (m_pseudo != Pseudo::None)
        pseudo = std::make_unique<PseudoTranslator>(m_pseudo == Pseudo::RightToLeft);
    for (auto *old : {m_pseudoTranslator.get(), m_app.get(), m_qt.get()})
        if (old)
            QCoreApplication::removeTranslator(old);
    // QCoreApplication searches the last installed first.
    for (auto *t : {qt.get(), app.get(), pseudo.get()})
        if (t)
            QCoreApplication::installTranslator(t);
    m_qt = std::move(qt);
    m_app = std::move(app);
    m_gettext = std::move(gettext);
    m_pseudoTranslator = std::move(pseudo);
    // QGuiApplication takes the layout direction from this translation.
    m_rightToLeft = QCoreApplication::translate("QGuiApplication", "QT_LAYOUT_DIRECTION") == QLatin1String("RTL");
    QGuiApplication::setLayoutDirection(m_rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    refreshWindows();
    emit languageChanged();
}

void Localisation::refreshWindows()
{
    QSet<QQmlEngine *> engines;
    for (QWindow *w : QGuiApplication::allWindows()) {
        if (auto *quick = qobject_cast<QQuickWindow *>(w)) {
            mirror(quick);
            if (QQmlEngine *engine = qmlEngine(quick))
                engines.insert(engine);
        }
    }
    for (QQmlEngine *engine : std::as_const(engines))
        engine->retranslate();
    // Qt sets standard button texts once, in the language of the moment, so they are set anew.
    QSet<QObject *> seen;
    for (QWindow *w : QGuiApplication::allWindows()) {
        for (QObject *o : w->findChildren<QObject *>()) {
            if (seen.contains(o) || !(o->inherits("QQuickDialog") || o->inherits("QQuickDialogButtonBox")))
                continue;
            seen.insert(o);
            const QVariant buttons = o->property("standardButtons");
            const QVariant none(buttons.metaType());
            if (buttons.isValid() && buttons != none) {
                o->setProperty("standardButtons", none);
                o->setProperty("standardButtons", buttons);
            }
        }
    }
}

// QQmlProperty resolves an attached property only through a QML context.
void Localisation::mirror(QQuickWindow *window) const
{
    QQmlContext *context = qmlContext(window);
    if (!context && window->contentItem()) {
        for (QQuickItem *child : window->contentItem()->childItems())
            if ((context = qmlContext(child)))
                break;
    }
    if (!context || !window->contentItem())
        return;
    QQmlProperty enabled(window->contentItem(), QStringLiteral("LayoutMirroring.enabled"), context);
    if (!enabled.isValid() || enabled.read().toBool() == m_rightToLeft)
        return;
    enabled.write(m_rightToLeft);
    QQmlProperty(window->contentItem(), QStringLiteral("LayoutMirroring.childrenInherit"), context).write(true);
}

bool Localisation::eventFilter(QObject *watched, QEvent *event)
{
    if (m_rightToLeft && event->type() == QEvent::Show)
        if (auto *window = qobject_cast<QQuickWindow *>(watched))
            mirror(window);
    return QObject::eventFilter(watched, event);
}

std::string Localisation::gettext(const std::string &source) const
{
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    [[maybe_unused]] const QString decoded = utf8.decode(QByteArrayView(source.data(), qsizetype(source.size())));
    if (utf8.hasError())
        return {};
    if (source.empty() || !m_gettext || source.find('\0') != std::string::npos)
        return source;
    const QString text = m_gettext->translate(kGettextContext, source.c_str());
    return text.isEmpty() ? source : text.toStdString();
}

} // namespace hikari::app
