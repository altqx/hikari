// P9: legacy SelectInFolder (HikariSub/config.cpp:1048-1062 at 20d647c4) as
// the "Open ... containing folder" items and a Ctrl+click on a recent file
// use it. On Linux the desktop's file manager is asked over D-Bus
// (org.freedesktop.FileManager1.ShowItems) and the folder opens when none
// answers; these tests run a private session bus with a fake file manager.
// Windows uses SHOpenFolderAndSelectItems as legacy did; the Explorer window
// it opens is read back through the Shell automation objects, on an
// interactive desktop and only when asked (HIKARI_SHOW_IN_FOLDER_EXPLORER=1,
// set by the winix task show-in-folder-explorer).

#include "hikari/backends/show_in_folder.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <exdisp.h>
#include <shldisp.h>
#include <wrl/client.h>

#include <QImage>

#include <algorithm>
#else
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif

#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;

// Why Explorer cannot open a window for this process, or empty: a service
// (session 0, or a window station without a desktop) shows nothing, and
// without Explorer as the shell the call would start one.
QString noExplorerReason()
{
    DWORD session = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session) && session == 0)
        return QStringLiteral("no interactive desktop: the test runs in a service session (session 0)");
    USEROBJECTFLAGS flags{};
    if (GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof flags, nullptr)
        && !(flags.dwFlags & WSF_VISIBLE))
        return QStringLiteral("no interactive desktop: the window station is not visible (a service)");
    if (!GetShellWindow())
        return QStringLiteral("Explorer is not the desktop's shell, so no folder window can open");
    return {};
}

// The long form of an existing path (the temporary folder may be named
// with 8.3 parts; Explorer reports long names).
QString longPath(const QString &path)
{
    const std::wstring in = QDir::toNativeSeparators(path).toStdWString();
    std::wstring out(GetLongPathNameW(in.c_str(), nullptr, 0), L'\0');
    if (out.empty())
        return QDir::toNativeSeparators(path);
    out.resize(GetLongPathNameW(in.c_str(), out.data(), DWORD(out.size())));
    return QString::fromStdWString(out);
}

bool samePath(const QString &a, const QString &b)
{
    return QString::compare(longPath(a), longPath(b), Qt::CaseInsensitive) == 0;
}

QString pathOf(FolderItem *item)
{
    BSTR path = nullptr;
    if (FAILED(item->get_Path(&path)) || !path)
        return {};
    const QString text = QString::fromWCharArray(path, SysStringLen(path));
    SysFreeString(path);
    return text;
}

// An open Explorer window (or tab) as IShellWindows lists it: its folder
// and the items selected in its view (IShellFolderViewDual::SelectedItems).
struct ExplorerWindow {
    ComPtr<IWebBrowser2> browser;
    QString folder;
    QStringList selected;
};

QList<ExplorerWindow> explorerWindowsOn(const QString &folder)
{
    QList<ExplorerWindow> found;
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows))))
        return found;
    LONG count = 0;
    windows->get_Count(&count);
    for (LONG i = 0; i < count; ++i) {
        VARIANT index;
        VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;
        ComPtr<IDispatch> item;
        ComPtr<IDispatch> document;
        ComPtr<IShellFolderViewDual> view;
        ComPtr<Folder> shown;
        ComPtr<Folder2> shown2;
        ComPtr<FolderItem> self;
        ExplorerWindow window;
        if (windows->Item(index, &item) != S_OK || !item || FAILED(item.As(&window.browser))
            || FAILED(window.browser->get_Document(&document)) || !document || FAILED(document.As(&view))
            || FAILED(view->get_Folder(&shown)) || !shown || FAILED(shown.As(&shown2))
            || FAILED(shown2->get_Self(&self)) || !self)
            continue;
        window.folder = pathOf(self.Get());
        if (!samePath(window.folder, folder))
            continue;
        ComPtr<FolderItems> selection;
        LONG selectedCount = 0;
        if (SUCCEEDED(view->SelectedItems(&selection)) && selection)
            selection->get_Count(&selectedCount);
        for (LONG k = 0; k < selectedCount; ++k) {
            VARIANT at;
            VariantInit(&at);
            at.vt = VT_I4;
            at.lVal = k;
            ComPtr<FolderItem> selected;
            if (SUCCEEDED(selection->Item(at, &selected)) && selected)
                window.selected << pathOf(selected.Get());
        }
        found << window;
    }
    return found;
}

// The whole desktop (every monitor) with the window raised, to a PNG; its
// size is logged, and a capture of one colour (nothing drawn) is refused.
bool saveDesktop(const QString &file, HWND window)
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    QTest::qWait(1000); // the raised window and its selection painted
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage image;
    if (bitmap) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT)) {
            GdiFlush();
            image = QImage(static_cast<const uchar *>(bits), width, height, width * 4, QImage::Format_RGB32).copy();
        }
        SelectObject(memory, previous);
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    if (image.isNull())
        return false;
    const QRgb first = image.pixel(0, 0);
    bool drawn = false;
    for (int row = 0; row < image.height() && !drawn; ++row) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(row));
        drawn = std::any_of(line, line + image.width(), [first](QRgb pixel) { return pixel != first; });
    }
    if (!drawn || !image.save(file))
        return false;
    qInfo().noquote() << "desktop saved:" << image.width() << "x" << image.height() << "pixels,"
                      << QFileInfo(file).size() << "bytes";
    return true;
}

} // namespace
#endif

// moc does not see the compiler's _WIN32, so the classes and slots it reads
// are declared on every platform; only the D-Bus test body is Linux-only.
// The fake file manager: records each ShowItems call.
class FakeFileManager : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    QList<QStringList> shown;
    QStringList startupIds;
public slots:
    void ShowItems(const QStringList &uris, const QString &startupId)
    {
        shown << uris;
        startupIds << startupId;
    }
};

class ShowInFolderTests : public QObject {
    Q_OBJECT

private slots:
    // wxFileName(filename).GetPath(), or the path itself without a folder part.
    void containingFolder()
    {
        using hikari::backends::containingFolder;
        QCOMPARE(QDir::fromNativeSeparators(containingFolder(QStringLiteral("/media/show/ep01.mkv"))),
                 QStringLiteral("/media/show"));
        QCOMPARE(QDir::fromNativeSeparators(containingFolder(QStringLiteral("/ep01.mkv"))), QStringLiteral("/"));
        QCOMPARE(containingFolder(QStringLiteral("ep01.mkv")), QStringLiteral("ep01.mkv"));
    }

    void fileManagerShowsTheFileSelected()
    {
#ifdef _WIN32
        QSKIP("the file manager is reached through D-Bus on Linux only");
#else
        const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
        if (daemon.isEmpty())
            QSKIP("dbus-daemon is not installed");
        // A private bus without service directories, so nothing is
        // activated (a real file manager never starts).
        QTemporaryDir busDir;
        const QString config = busDir.filePath(QStringLiteral("bus.conf"));
        {
            QFile f(config);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                    " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                    "<busconfig><type>session</type><listen>unix:dir=" + busDir.path().toUtf8() + "</listen>"
                    "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                    "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n");
        }
        QProcess bus;
        bus.start(daemon, {QStringLiteral("--config-file=") + config, QStringLiteral("--nofork"),
                           QStringLiteral("--print-address=1")});
        QVERIFY(bus.waitForStarted());
        QVERIFY(bus.waitForReadyRead(5000));
        const QString address = QString::fromUtf8(bus.readLine()).trimmed();
        QVERIFY(!address.isEmpty());
        auto cleanup = qScopeGuard([&] {
            QDBusConnection::disconnectFromBus(QStringLiteral("p9-server"));
            QDBusConnection::disconnectFromBus(QStringLiteral("p9-client"));
            bus.kill();
            bus.waitForFinished();
        });
        QDBusConnection server = QDBusConnection::connectToBus(address, QStringLiteral("p9-server"));
        QDBusConnection client = QDBusConnection::connectToBus(address, QStringLiteral("p9-client"));
        QVERIFY(server.isConnected() && client.isConnected());

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("ep 01 (ä).mkv"));
        QVERIFY(QFile(file).open(QIODevice::WriteOnly));

        // No file manager on the bus: the folder opens instead (legacy Linux).
        QStringList fallbacks;
        hikari::backends::selectInFolder(file, client, [&](const QString &folder) { fallbacks << folder; });
        QTRY_COMPARE(fallbacks.size(), 1);
        QCOMPARE(QDir::fromNativeSeparators(fallbacks[0]), dir.path());

        FakeFileManager manager;
        QVERIFY(server.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &manager,
                                      QDBusConnection::ExportAllSlots));
        QVERIFY(server.registerService(QStringLiteral("org.freedesktop.FileManager1")));
        hikari::backends::selectInFolder(file, client, [&](const QString &folder) { fallbacks << folder; });
        QTRY_COMPARE(manager.shown.size(), 1);
        QCOMPARE(manager.shown[0], QStringList{QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded)});
        QCOMPARE(QUrl(manager.shown[0][0]).toLocalFile(), file); // the file itself, to be selected
        QCOMPARE(manager.startupIds[0], QString());
        QTest::qWait(100);
        QCOMPARE(fallbacks.size(), 1); // answered: no folder opened

        // Without a session bus at all: the folder.
        QDBusConnection none = QDBusConnection(QStringLiteral("p9-none"));
        hikari::backends::selectInFolder(file, none, [&](const QString &folder) { fallbacks << folder; });
        QCOMPARE(fallbacks.size(), 2);
#endif
    }

    // Windows: the folder opens in Explorer with exactly that file selected
    // (legacy SelectInFolder, config.cpp:1050-1058), read back from the
    // window through the Shell automation objects and then closed. With
    // HIKARI_SHOW_IN_FOLDER_SHOT set, the desktop is saved there first.
    // It opens a real window, so it runs only with
    // HIKARI_SHOW_IN_FOLDER_EXPLORER=1 (the winix task): on a CI runner a
    // desktop can look usable and Explorer still be too slow to start.
    void explorerShowsTheFileSelected()
    {
#ifndef _WIN32
        QSKIP("Explorer is Windows only");
#else
        if (qEnvironmentVariableIntValue("HIKARI_SHOW_IN_FOLDER_EXPLORER") != 1)
            QSKIP("opens a real Explorer window, so it runs with HIKARI_SHOW_IN_FOLDER_EXPLORER=1 only "
                  "(winix task show-in-folder-explorer)");
        if (const QString reason = noExplorerReason(); !reason.isEmpty())
            QSKIP(qPrintable(reason));
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QVERIFY2(SUCCEEDED(com), "COM is not available on this thread");
        auto uninitialize = qScopeGuard([] { CoUninitialize(); });

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // A neighbour sorted first, so a view that selects its first item is not taken for the file.
        QVERIFY(QFile(dir.filePath(QStringLiteral("ep 00.mkv"))).open(QIODevice::WriteOnly));
        const QString file = dir.filePath(QStringLiteral("ep 01 (ä).mkv"));
        QVERIFY(QFile(file).open(QIODevice::WriteOnly));
        QVERIFY(explorerWindowsOn(dir.path()).isEmpty());
        auto close = qScopeGuard([&] {
            for (const ExplorerWindow &window : explorerWindowsOn(dir.path()))
                window.browser->Quit();
        });

        hikari::backends::selectInFolder(file);
        // Explorer opens the window, then selects the file in its view.
        QList<ExplorerWindow> shown;
        QTRY_VERIFY_WITH_TIMEOUT(!(shown = explorerWindowsOn(dir.path())).isEmpty() && !shown[0].selected.isEmpty(),
                                 30000);
        qInfo().noquote() << "Explorer shows" << shown[0].folder << "with" << shown[0].selected.join(u'|')
                          << "selected";
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown[0].selected.size(), 1);
        QVERIFY2(samePath(shown[0].selected[0], file), qPrintable(shown[0].selected[0]));

        if (const QString shot = qEnvironmentVariable("HIKARI_SHOW_IN_FOLDER_SHOT"); !shot.isEmpty()) {
            SHANDLE_PTR window = 0;
            QVERIFY(SUCCEEDED(shown[0].browser->get_HWND(&window)) && window);
            QVERIFY2(saveDesktop(shot, reinterpret_cast<HWND>(window)), qPrintable(shot));
        }
        close.dismiss();
        QVERIFY(SUCCEEDED(shown[0].browser->Quit()));
        QTRY_VERIFY_WITH_TIMEOUT(explorerWindowsOn(dir.path()).isEmpty(), 10000);
#endif
    }
};

QTEST_GUILESS_MAIN(ShowInFolderTests)
#include "show_in_folder_tests.moc"
