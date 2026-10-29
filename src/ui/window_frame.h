#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

namespace hikari::ui {

namespace windowframe {

inline constexpr const char *kTitleBarSetting = "appearance.titleBar";

struct Environment {
    QString platform;
    QString currentDesktop;
    QString sessionDesktop;
    QStringList markers;
    QStringList waylandGlobals;
};

enum class Desktop {
    Windows,
    Kde,
    Gnome,
    Tiling,
    Other,
    Unsupported,
};

Desktop classify(const Environment &environment);
Environment currentEnvironment();
Desktop currentDesktop();
bool systemTitleBarByDefault(Desktop desktop);
bool usesSystemTitleBar(const QString &setting, Desktop desktop);

void overrideDesktop(std::optional<Desktop> desktop);

class NativeFrame;

}

class WindowFrame : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool systemTitleBarByDefault READ systemTitleBarByDefault NOTIFY desktopChanged FINAL)
    Q_PROPERTY(bool customAvailable READ customAvailable NOTIFY desktopChanged FINAL)
    Q_PROPERTY(bool nativeResize READ nativeResize CONSTANT FINAL)
    Q_PROPERTY(int resizeBand READ resizeBand CONSTANT FINAL)
public:
    explicit WindowFrame(QObject *parent = nullptr);

    bool systemTitleBarByDefault() const;
    bool customAvailable() const;
    static bool nativeResize();
    static int resizeBand();
    Q_INVOKABLE bool usesSystemTitleBar(const QString &setting) const;
    Q_INVOKABLE static bool remaking(QWindow *window);

    static void notifyDesktopChanged();

signals:
    void desktopChanged();
};

class MainWindowFrame : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    Q_PROPERTY(bool custom READ custom WRITE setCustom NOTIFY customChanged FINAL)
    Q_PROPERTY(QQuickItem *titleBar READ titleBar WRITE setTitleBar NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QQuickItem *moveArea READ moveArea WRITE setMoveArea NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QQuickItem *maximizeButton READ maximizeButton WRITE setMaximizeButton NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QQuickItem *windowMenuButton READ windowMenuButton WRITE setWindowMenuButton NOTIFY itemsChanged FINAL)
    Q_PROPERTY(bool maximizeHovered READ maximizeHovered NOTIFY maximizeStateChanged FINAL)
    Q_PROPERTY(bool maximizePressed READ maximizePressed NOTIFY maximizeStateChanged FINAL)
    Q_PROPERTY(bool altHeld READ altHeld NOTIFY altHeldChanged FINAL)
    Q_PROPERTY(bool resizable READ resizable WRITE setResizable NOTIFY resizableChanged FINAL)
    Q_PROPERTY(bool closable READ closable WRITE setClosable NOTIFY closableChanged FINAL)
    Q_PROPERTY(bool dialog READ dialog CONSTANT FINAL)
public:
    enum Hit {
        Client,
        Caption,
        MaximizeButton,
        WindowMenuButton,
    };
    Q_ENUM(Hit)

    explicit MainWindowFrame(QObject *parent = nullptr);
    ~MainWindowFrame() override;

    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    bool custom() const { return m_custom; }
    void setCustom(bool custom);
    QQuickItem *titleBar() const { return m_titleBar; }
    void setTitleBar(QQuickItem *item);
    QQuickItem *moveArea() const { return m_moveArea; }
    void setMoveArea(QQuickItem *item);
    QQuickItem *maximizeButton() const { return m_maximizeButton; }
    void setMaximizeButton(QQuickItem *item);
    QQuickItem *windowMenuButton() const { return m_windowMenuButton; }
    void setWindowMenuButton(QQuickItem *item);
    bool maximizeHovered() const { return m_maximizeHovered; }
    bool maximizePressed() const { return m_maximizePressed; }
    bool altHeld() const { return m_altHeld; }
    bool resizable() const { return m_resizable; }
    void setResizable(bool resizable);
    bool closable() const { return m_closable; }
    void setClosable(bool closable);
    bool dialog() const { return m_dialog; }

    Q_INVOKABLE Hit hitAt(QPointF position) const;
    Q_INVOKABLE bool showWindowMenu(QPointF position);
    Q_INVOKABLE void toggleMaximized();

    void setMaximizeState(bool hovered, bool pressed);

signals:
    void windowChanged();
    void customChanged();
    void itemsChanged();
    void maximizeStateChanged();
    void windowMenuRequested(QPointF position);
    void altHeldChanged();
    void resizableChanged();
    void closableChanged();
    void altTapped();

protected:
    struct DialogKind {};
    MainWindowFrame(DialogKind, QObject *parent);
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void apply();
    void setAltHeld(bool held);

    QPointer<QQuickWindow> m_window;
    bool m_custom = false;
    bool m_applied = false;
    QPointer<QQuickItem> m_titleBar;
    QPointer<QQuickItem> m_moveArea;
    QPointer<QQuickItem> m_maximizeButton;
    QPointer<QQuickItem> m_windowMenuButton;
    bool m_maximizeHovered = false;
    bool m_maximizePressed = false;
    bool m_altHeld = false;
    bool m_altAlone = false;
    bool m_resizable = true;
    bool m_closable = true;
    const bool m_dialog = false;
    std::unique_ptr<windowframe::NativeFrame> m_native;
};

class DialogFrame : public MainWindowFrame {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit DialogFrame(QObject *parent = nullptr);
};

}
