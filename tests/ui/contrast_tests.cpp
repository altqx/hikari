#include "hikari/app/application.h"
#include "docking.h"
#include "theme.h"

#include <QDir>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <set>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
namespace theme = hikari::ui::theme;

namespace {

constexpr double kMinimumContrast = 4.5;

bool shown(const QQuickItem *item)
{
    for (const QQuickItem *at = item; at; at = at->parentItem())
        if (!at->isVisible() || at->opacity() <= 0.01)
            return false;
    return true;
}

double opacityOf(const QQuickItem *item)
{
    double opacity = 1;
    for (const QQuickItem *at = item; at; at = at->parentItem())
        opacity *= at->opacity();
    return opacity;
}

QColor fillOf(const QQuickItem *item)
{
    if (!item->inherits("QQuickRectangle") || !shown(item))
        return {};
    QColor colour = item->property("color").value<QColor>();
    colour.setAlphaF(float(colour.alphaF() * opacityOf(item)));
    return colour;
}

bool paintsBelow(const QQuickItem *sibling, const QQuickItem *item)
{
    if (sibling->z() != item->z())
        return sibling->z() < item->z();
    const QList<QQuickItem *> order = item->parentItem()->childItems();
    return order.indexOf(const_cast<QQuickItem *>(sibling)) < order.indexOf(const_cast<QQuickItem *>(item));
}

bool covers(const QQuickItem *fill, const QQuickItem *text)
{
    const QPointF centre = text->mapToItem(fill, QPointF(text->width() / 2, text->height() / 2));
    return QRectF(0, 0, fill->width(), fill->height()).contains(centre);
}

QList<QColor> layersUnder(const QQuickItem *text)
{
    QList<QColor> layers;
    const auto add = [&](const QColor &colour) {
        if (colour.isValid() && colour.alpha() > 0)
            layers << colour;
        return colour.isValid() && colour.alpha() == 255;
    };
    for (const QQuickItem *at = text; at->parentItem(); at = at->parentItem()) {
        const QQuickItem *parent = at->parentItem();
        QList<QQuickItem *> below;
        for (QQuickItem *sibling : parent->childItems())
            if (sibling != at && paintsBelow(sibling, at))
                below << sibling;
        std::stable_sort(below.begin(), below.end(), [&](QQuickItem *a, QQuickItem *b) { return paintsBelow(b, a); });
        for (QQuickItem *sibling : std::as_const(below))
            if (covers(sibling, text) && add(fillOf(sibling)))
                return layers;
        if (add(fillOf(parent)))
            return layers;
    }
    QColor window = text->window() ? text->window()->color() : QColor(Qt::white);
    layers << (window.alpha() < 255 ? theme::composite(window, theme::current().roles.panel) : window);
    return layers;
}

QColor backdropOf(const QQuickItem *text)
{
    QList<QColor> layers = layersUnder(text);
    QColor result = layers.takeLast();
    while (!layers.isEmpty())
        result = theme::composite(layers.takeLast(), result);
    return result;
}

QString pathOf(const QQuickItem *item)
{
    QStringList names;
    for (const QQuickItem *at = item; at; at = at->parentItem())
        if (!at->objectName().isEmpty())
            names.prepend(at->objectName());
    return names.join(QLatin1Char('/'));
}

void collectTexts(QQuickItem *at, QList<QQuickItem *> &out)
{
    if (!at->isVisible() || at->opacity() <= 0.01)
        return;
    if ((at->inherits("QQuickText") || at->inherits("QQuickTextEdit") || at->inherits("QQuickTextInput"))
        && !at->property("text").toString().trimmed().isEmpty() && at->width() > 0 && at->height() > 0 && at->isEnabled())
        out << at;
    for (QQuickItem *child : at->childItems())
        collectTexts(child, out);
}

}

class ContrastTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QString shots;

    QObject *root() const { return engine->rootObjects().first(); }

    QStringList unreadableUnder(QQuickItem *top, const QString &surface)
    {
        QList<QQuickItem *> texts;
        collectTexts(top, texts);
        QStringList problems;
        for (QQuickItem *text : std::as_const(texts)) {
            const QColor back = backdropOf(text);
            QColor fore = text->property("color").value<QColor>();
            fore.setAlphaF(float(fore.alphaF() * opacityOf(text)));
            const double ratio = theme::contrastRatio(theme::composite(fore, back), back);
            if (ratio < kMinimumContrast)
                problems << QStringLiteral("%1: \"%2\" %3 on %4 is %5:1 (%6, %7)")
                                .arg(surface, text->property("text").toString().left(40), fore.name(QColor::HexArgb),
                                     back.name(), QString::number(ratio, 'f', 2), pathOf(text),
                                     QString::fromLatin1(text->metaObject()->className()));
        }
        return problems;
    }

    void shoot(QQuickWindow *of, const QString &name)
    {
        if (shots.isEmpty() || !of)
            return;
        QString file = name;
        file.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
        of->grabWindow().save(shots + QLatin1Char('/') + QString::fromLatin1(QTest::currentDataTag()) + QLatin1Char('-')
                              + file + QStringLiteral(".png"));
    }

    QList<QPointer<QObject>> popups() const
    {
        QList<QPointer<QObject>> found;
        std::set<QObject *> seen;
        const auto take = [&](QObject *from) {
            for (QObject *o : from->findChildren<QObject *>())
                if (o->inherits("QQuickPopup") && seen.insert(o).second)
                    found << o;
        };
        take(root());
        for (QWindow *w : QGuiApplication::topLevelWindows())
            take(w);
        return found;
    }

    void activateDialogLoaders()
    {
        for (QObject *loader : root()->findChildren<QObject *>()) {
            if (!loader->inherits("QQuickLoader") || loader->property("active").toBool())
                continue;
            if (!loader->property("sourceComponent").value<QObject *>() && loader->property("source").toUrl().isEmpty())
                continue;
            loader->setProperty("active", true);
            QObject *made = loader->property("item").value<QObject *>();
            if (!made || !(made->inherits("QQuickWindow") || made->inherits("QQuickPopup")))
                loader->setProperty("active", false);
        }
    }

    QQuickWindow *ownerOf(QObject *popup) const
    {
        if (auto *parent = popup->property("parent").value<QQuickItem *>(); parent && parent->window())
            return parent->window();
        for (QObject *at = popup->parent(); at; at = at->parent())
            if (auto *quick = qobject_cast<QQuickWindow *>(at))
                return quick;
        return window;
    }

    QStringList sweepPopups(const QList<QPointer<QObject>> &all, QQuickWindow *owner)
    {
        QStringList problems;
        for (const QPointer<QObject> &popup : all) {
            if (!popup || ownerOf(popup) != owner)
                continue;
            const QString name = popup->objectName().isEmpty() ? QString::fromLatin1(popup->metaObject()->className())
                                                                : popup->objectName();
            QMetaObject::invokeMethod(popup, "open");
            if (!QTest::qWaitFor([&] { return !popup || popup->property("opened").toBool(); }, 1500) || !popup)
                continue;
            auto *content = popup->property("contentItem").value<QQuickItem *>();
            QPointer<QQuickItem> popupItem = content ? content->parentItem() : nullptr;
            QTest::qWait(50);
            if (popupItem) {
                problems << unreadableUnder(popupItem, name);
                shoot(popupItem->window(), QStringLiteral("popup-") + name);
            }
            if (!popup)
                continue;
            QMetaObject::invokeMethod(popup, "close");
            if (!QTest::qWaitFor([&] { return !popup || !popup->property("visible").toBool(); }, 1500))
                problems << name + QStringLiteral(": stays open after close()");
        }
        return problems;
    }

    QStringList sweep()
    {
        const QList<QPointer<QObject>> all = popups();
        QStringList problems = sweepPopups(all, window);
        QList<QPointer<QQuickWindow>> windows;
        for (QWindow *top : QGuiApplication::topLevelWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(top))
                windows << quick;
        for (const QPointer<QQuickWindow> &quick : std::as_const(windows)) {
            if (!quick || quick == window || quick->inherits("QQuickPopupWindow") || quick->isVisible())
                continue;
            const QString name = quick->objectName().isEmpty() ? QString::fromLatin1(quick->metaObject()->className())
                                                               : quick->objectName();
            quick->show();
            if (!QTest::qWaitFor([&] { return !quick || quick->isExposed(); }, 1500))
                continue;
            QTest::qWait(50);
            if (!quick)
                continue;
            problems << unreadableUnder(quick->contentItem(), name);
            shoot(quick, QStringLiteral("window-") + name);
            problems << sweepPopups(all, quick);
            if (quick)
                quick->hide();
        }
        return problems;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        theme::forceSystemScheme(Qt::ColorScheme::Unknown);
        shots = qEnvironmentVariable("HIKARI_CONTRAST_SHOTS");
        if (!shots.isEmpty())
            QVERIFY(QDir().mkpath(shots));
    }
    void init()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = new app::Application(options);
        engine = new QQmlApplicationEngine;
        ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(root());
        QVERIFY(window);
        window->resize(1280, 800);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void cleanup()
    {
        theme::forceSystemScheme(Qt::ColorScheme::Unknown);
        delete engine;
        delete application;
        engine = nullptr;
        application = nullptr;
    }

    void menuAndDialogTextIsReadable_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<bool>("fromSystem");
        QTest::newRow("darkFromSystem") << int(theme::Code::Dark) << true;
        for (const auto code : theme::kCodes)
            if (code == theme::Code::Dark || !qEnvironmentVariableIsEmpty("HIKARI_CONTRAST_SHOTS"))
                QTest::newRow(qPrintable(theme::codeName(code))) << int(code) << false;
    }
    void menuAndDialogTextIsReadable()
    {
        QFETCH(int, code);
        QFETCH(bool, fromSystem);
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), fromSystem);
        auto *controls = root()->property("palette").value<QObject *>();
        if (fromSystem) {
            settings.setValue(QStringLiteral("appearance.theme"), theme::codeName(theme::Code::Light));
            theme::forceSystemScheme(Qt::ColorScheme::Light);
            QTRY_COMPARE(controls->property("window").value<QColor>(), theme::current().roles.panel);
            activateDialogLoaders();
            theme::forceSystemScheme(Qt::ColorScheme::Dark);
        } else {
            settings.setValue(QStringLiteral("appearance.theme"), theme::codeName(theme::Code(code)));
        }
        QCOMPARE(theme::current().code, theme::Code(code));
        QTRY_COMPARE(controls->property("window").value<QColor>(), theme::current().roles.panel);
        activateDialogLoaders();
        const QStringList problems = sweep();
        if (!problems.isEmpty())
            QFAIL(qPrintable(QStringLiteral("%1 text items under %2:1\n").arg(problems.size()).arg(kMinimumContrast)
                             + problems.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(ContrastTests)
#include "contrast_tests.moc"
