#include "hikari/app/hotkeys_controller.h"

#include "hikari/app/automation_hotkeys_controller.h"
#include "hikari/application/settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <algorithm>
#include <utility>
#include <set>

namespace hikari::app {

namespace {

using application::HotkeyAnswer;
using application::HotkeyId;
using application::HotkeyMap;

constexpr const char *kMainSetting = application::kHotkeysSetting.data();
constexpr const char *kAudioSetting = application::kAudioHotkeysSetting.data();

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

std::vector<std::string> stdLines(const QStringList &l)
{
    std::vector<std::string> out;
    for (const auto &s : l)
        out.push_back(s.toStdString());
    return out;
}

QStringList qLines(const std::vector<std::string> &l)
{
    QStringList out;
    for (const auto &s : l)
        out << qs(s);
    return out;
}

HotkeyAnswer answerOf(const QString &a)
{
    if (a == QLatin1String("switch"))
        return HotkeyAnswer::Switch;
    if (a == QLatin1String("delete"))
        return HotkeyAnswer::Delete;
    if (a == QLatin1String("anyway"))
        return HotkeyAnswer::SetAnyway;
    return HotkeyAnswer::Cancel;
}

QVariantMap conflictMap(const std::optional<application::HotkeyConflict> &c)
{
    if (!c)
        return {};
    return {{QStringLiteral("doubled"), c->doubled},
            {QStringLiteral("message"), qs(c->message)},
            {QStringLiteral("canSwitch"), c->canSwitch},
            {QStringLiteral("canSetAnyway"), c->canSetAnyway}};
}

// US-layout base keys of shifted punctuation: wx reports the unshifted key
// with Shift ("Shift-;"), Qt the shifted character.
int unshifted(int key)
{
    static constexpr std::pair<char, char> table[] = {
        {':', ';'}, {'<', ','}, {'>', '.'}, {'?', '/'}, {'"', '\''}, {'{', '['}, {'}', ']'}, {'|', '\\'},
        {'~', '`'}, {'_', '-'}, {'+', '='}, {'!', '1'}, {'@', '2'}, {'#', '3'}, {'$', '4'}, {'%', '5'},
        {'^', '6'}, {'&', '7'}, {'*', '8'}, {'(', '9'}, {')', '0'}};
    for (const auto &[s, base] : table)
        if (key == s)
            return base;
    return key;
}

// The wx key code legacy's key events carry for a Qt key press.
int wxKeyOf(int key, Qt::KeyboardModifiers modifiers)
{
    namespace wxk = application::wxk;
    const bool keypad = modifiers & Qt::KeypadModifier;
    switch (key) {
    case Qt::Key_Shift: return wxk::Shift;
    case Qt::Key_Alt:
    case Qt::Key_AltGr: return wxk::Alt;
    case Qt::Key_Control: return wxk::Control;
    case Qt::Key_Meta: return wxk::WindowsLeft;
    case Qt::Key_Backspace: return wxk::Back;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return wxk::Tab;
    case Qt::Key_Return: return wxk::Return;
    case Qt::Key_Enter: return wxk::NumpadEnter;
    case Qt::Key_Escape: return wxk::Escape;
    case Qt::Key_Space: return wxk::Space;
    case Qt::Key_Pause: return wxk::Pause;
    case Qt::Key_Delete: return keypad ? wxk::NumpadDelete : wxk::Delete;
    case Qt::Key_End: return keypad ? wxk::NumpadHome + 7 : wxk::End;
    case Qt::Key_Home: return keypad ? wxk::NumpadHome : wxk::Home;
    case Qt::Key_Left: return keypad ? wxk::NumpadHome + 1 : wxk::Left;
    case Qt::Key_Up: return keypad ? wxk::NumpadHome + 2 : wxk::Up;
    case Qt::Key_Right: return keypad ? wxk::NumpadHome + 3 : wxk::Right;
    case Qt::Key_Down: return keypad ? wxk::NumpadHome + 4 : wxk::Down;
    case Qt::Key_PageUp: return keypad ? wxk::NumpadHome + 5 : wxk::PageUp;
    case Qt::Key_PageDown: return keypad ? wxk::NumpadHome + 6 : wxk::PageDown;
    case Qt::Key_Insert: return keypad ? wxk::NumpadHome + 9 : wxk::Insert;
    default: break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return wxk::F1 + (key - Qt::Key_F1);
    if (keypad) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9)
            return wxk::Numpad0 + (key - Qt::Key_0);
        switch (key) {
        case Qt::Key_Asterisk: return wxk::NumpadMultiply;
        case Qt::Key_Plus: return wxk::NumpadAdd;
        case Qt::Key_Minus: return wxk::NumpadSubtract;
        case Qt::Key_Period:
        case Qt::Key_Comma: return wxk::NumpadDecimal;
        case Qt::Key_Slash: return wxk::NumpadDivide;
        default: break;
        }
    }
    if (key > 0x20 && key < 0x7f)
        return (modifiers & Qt::ShiftModifier) ? unshifted(key) : key;
    return key; // other characters: their code, which no table names
}

application::HotkeyPress pressOf(int key, int modifiers)
{
    const auto mods = Qt::KeyboardModifiers(modifiers);
    application::HotkeyPress p;
    p.key = wxKeyOf(key, mods);
    p.alt = mods & Qt::AltModifier;
    p.ctrl = mods & Qt::ControlModifier;
    p.shift = mods & Qt::ShiftModifier;
    p.onlyCtrl = (mods & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier)) ==
                 Qt::ControlModifier;
    return p;
}

// The accelerator text of a key press, as the mapping window writes it.
std::string accelOfPress(const application::HotkeyPress &p)
{
    std::string out;
    if (p.alt)
        out += "Alt-";
    if (p.ctrl)
        out += "Ctrl-";
    if (p.shift)
        out += "Shift-";
    const auto &names = application::hotkeyKeyNames();
    const auto it = names.find(p.key);
    if (it != names.end())
        return out + it->second;
    if (p.key > 0x20 && p.key < 0x7f)
        return out + static_cast<char>(p.key);
    return {};
}

// The same key and modifiers as GetHKey reads them.
bool sameChord(const std::string &a, const std::string &b)
{
    if (a.empty() || b.empty())
        return false;
    const auto qa = application::qtKeysOfAccel(a);
    return !qa.empty() && qa == application::qtKeysOfAccel(b);
}

std::string symbolOf(int id)
{
    const auto s = application::hotkeySymbol(id);
    return s.empty() ? std::to_string(id) : std::string(s);
}

} // namespace

HotkeysController::HotkeysController(AutomationHotkeysController &scripts, ui::SettingsStore &settings,
                                     std::function<void(const QString &)> log, QObject *parent)
    : QObject(parent), m_scripts(scripts), m_settings(settings), m_log(std::move(log))
{
    load();
    m_installed = m_live;
    m_installedTagButtons = m_settings.integer("editor.tagButtons");
    if (m_log)
        for (const QString &notice : std::exchange(m_retiredNotices, {}))
            m_log(notice);
    logInvalid(); // the frame's SetAccels(false) at startup
    // OK in the automation hotkeys window: SetHotkeysMap, SetAccels(true), SaveHkeys().
    connect(&m_scripts, &AutomationHotkeysController::committed, this, [this] {
        m_settings.set(kMainSetting, qLines(application::hotkeyLines(m_live, false)));
        setAccels();
    });
    QCoreApplication::instance()->installEventFilter(this);
}

HotkeysController::~HotkeysController()
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(this);
}

// LoadHkeys for each file: what is stored, else LoadDefault. Any number of
// stored bindings is read as stored (C04-short-file).
void HotkeysController::load()
{
    m_live.clear();
    if (m_settings.contains(kMainSetting))
        application::readHotkeyLines(m_live, stdLines(m_settings.list(kMainSetting)));
    else
        application::loadDefaultHotkeys(m_live, false);
    if (m_settings.contains(kAudioSetting))
        application::readHotkeyLines(m_live, stdLines(m_settings.list(kAudioSetting)));
    else
        application::loadDefaultHotkeys(m_live, true);
    // V3-indexing-retired: a binding of "Open video with FFMS2" (legacy
    // GLOBAL_VIDEO_INDEXING, from a carried-over Hotkeys.txt) is dropped with
    // a notice, and the list is written without it so the notice is given once.
    const auto dropped = application::dropRetiredHotkeys(m_live);
    for (const auto &[key, hotkey] : dropped)
        m_retiredNotices << tr("The shortcut %1 of \"Open video with FFMS2\" was removed: the command is retired "
                               "and videos are always indexed.")
                                .arg(qs(hotkey.accel));
    if (!dropped.empty())
        m_settings.set(kMainSetting, qLines(application::hotkeyLines(m_live, false)));
}

void HotkeysController::saveMain()
{
    m_settings.set(kMainSetting, qLines(application::hotkeyLines(m_live, false)));
    m_scripts.save();
}

void HotkeysController::saveAudio()
{
    m_settings.set(kAudioSetting, qLines(application::hotkeyLines(m_live, true)));
}

void HotkeysController::setAccels()
{
    m_installed = m_live;
    // TabPanel::SetAccels reads the tag button count when it installs.
    m_installedTagButtons = m_settings.integer("editor.tagButtons");
    m_scripts.install();
    logInvalid();
    emit installedChanged();
}

// Hotkeys::GetHKey (Hotkeys.cpp:372-375) for every binding SetAccels
// installs: a key that is neither a FillTable name nor one character is
// logged and installs nothing. Legacy logs it once per accelerator table the
// binding goes to (the frame's, each tab's, the audio box's); the rewrite
// once per binding at each install.
void HotkeysController::logInvalid() const
{
    if (!m_log)
        return;
    for (const auto &[key, hotkey] : m_installed) {
        if (key.type < 0 || key.type >= application::kHotkeyWindows)
            continue;
        const auto bad = application::invalidHotkeyKey(hotkey.accel);
        if (!bad.empty())
            m_log(tr("Shortcut \"%1\" is invalid").arg(qs(bad)));
    }
}

HotkeyMap HotkeysController::bindings() const
{
    HotkeyMap out = m_live;
    int id = application::kFirstScriptHotkey;
    for (const auto &[name, keys] : m_scripts.committedKeys())
        out[HotkeyId{id++, application::GlobalHotkey}] = application::Hotkey{name, application::accelOfQtKeys(keys)};
    return out;
}

// A whole map back: the static bindings here, the scripts' by name to S2.
void HotkeysController::split(const HotkeyMap &combined)
{
    m_live.clear();
    std::map<std::string, std::string> scripts;
    for (const auto &[key, hotkey] : combined) {
        if (key.id >= application::kFirstScriptHotkey) {
            // A nameless binding there came from a numeric line past the
            // static ids (LoadHkeys); legacy saves it as "=<keys>", which it
            // cannot read back, so it is gone at the next start either way.
            if (!hotkey.name.empty())
                scripts[hotkey.name] = application::qtKeysOfAccel(hotkey.accel);
        } else
            m_live[key] = hotkey;
    }
    m_scripts.replaceCommitted(scripts);
}

QVariantMap HotkeysController::keys() const
{
    static constexpr const char *windows[] = {"global", "grid", "editor", "video", "audio"};
    QVariantMap perWindow[application::kHotkeyWindows];
    for (const auto &[key, hotkey] : m_installed) {
        if (key.type < 0 || key.type >= application::kHotkeyWindows || key.id >= application::kFirstScriptHotkey)
            continue;
        const auto seq = application::qtKeysOfAccel(hotkey.accel);
        if (!seq.empty())
            perWindow[key.type].insert(qs(symbolOf(key.id)), qs(seq));
    }
    QVariantMap out;
    for (int i = 0; i < application::kHotkeyWindows; ++i)
        out.insert(QLatin1String(windows[i]), perWindow[i]);
    return out;
}

QVariantList HotkeysController::globalShortcuts() const
{
    using namespace application;
    QVariantList out;
    std::set<QString> taken;
    for (const auto &[key, hotkey] : m_installed) {
        if (key.type != GlobalHotkey || key.id >= kFirstScriptHotkey || hotkey.accel.empty())
            continue;
        const auto seq = qtKeysOfAccel(hotkey.accel);
        if (seq.empty())
            continue;
        // GetHKey sends an id under AUDIO_COMMIT as id + 10; the frame
        // handles it only when it bound that id (an installed Global binding).
        int id = key.id;
        if (id < kAudioCommit) {
            id += 10;
            const auto main = m_installed.find(HotkeyId{id, GlobalHotkey});
            if (main == m_installed.end() || main->second.accel.empty())
                continue;
        }
        const QString keys = QKeySequence::fromString(qs(seq), QKeySequence::PortableText).toString(QKeySequence::PortableText);
        if (keys.isEmpty() || !taken.insert(keys).second)
            continue;
        out << QVariantMap{{QStringLiteral("symbol"), qs(symbolOf(id))}, {QStringLiteral("keys"), keys}};
    }
    return out;
}

QStringList HotkeysController::globalSequences() const
{
    QStringList out;
    for (const auto &v : globalShortcuts())
        out << v.toMap().value(QStringLiteral("keys")).toString();
    return out;
}

bool HotkeysController::repeatedKey(const QString &action, int interval)
{
    // config::CheckLastKeyEvent: an ignored key does not move lastCheckedTime.
    const qint64 now = m_keyClock ? m_keyClock() : QDateTime::currentMSecsSinceEpoch();
    if (now < m_lastCheckedTime && m_lastCheckedTime != 0)
        m_lastCheckedTime = now;
    else if (action == m_lastCheckedId && now < m_lastCheckedTime + interval)
        return true;
    m_lastCheckedTime = now;
    m_lastCheckedId = action;
    return false;
}

QString HotkeysController::accelOf(const QString &symbol, int window) const
{
    const int id = application::hotkeyIdOf(symbol.toStdString());
    const auto it = m_installed.find(HotkeyId{id, window});
    return it == m_installed.end() ? QString() : qs(it->second.accel);
}

QString HotkeysController::actionFor(int window, int key, int modifiers, bool textField) const
{
    using namespace application;
    const std::string pressed = accelOfPress(pressOf(key, modifiers));
    if (pressed.empty())
        return {};
    // The text field's own accelerators act first in the Line editor.
    if (window == EditorHotkey && textField &&
        textFieldOwnsKey(pressed, m_settings.boolean("editor.allowNumpadHotkeys")))
        return {};
    // TabPanel::SetAccels: which window's table each binding goes to.
    const auto routed = [&](const HotkeyId &k) -> int {
        const int id = k.id;
        if (k.type != AudioHotkey) {
            if (id >= 2000 && id < 3000 && k.type != VideoHotkey)
                return k.type == GridHotkey ? GridHotkey : EditorHotkey;
            if (id >= 3000 && id < 4000 && k.type != EditorHotkey)
                return k.type == GridHotkey ? GridHotkey : VideoHotkey;
            if (id >= 4000 && id < 5000 && k.type != GridHotkey)
                return k.type == VideoHotkey ? VideoHotkey : -1; // the Editor branch is unreachable
        }
        return k.type;
    };
    for (const auto &[k, hotkey] : m_installed) {
        if (hotkey.accel.empty() || k.type == GlobalHotkey)
            continue; // Global: the main window's
        int table = routed(k);
        // Video play/seek bindings also go to the Grid.
        bool alsoGrid = k.type == VideoHotkey && k.id >= kVideoPlayPause && k.id <= kVideo5SecondsBackward;
        // The Audio window's bindings: the audio box's own table
        // (AudioBox::SetAccels), and the Grid's too except AUDIO_COMMIT to
        // AUDIO_NEXT (the _ALT ids stay), which SubsGrid::OnAccelerator
        // hands to the box; so do the Grid's own bindings of audio ids.
        if (k.type == AudioHotkey) {
            table = AudioHotkey;
            alsoGrid = !(k.id >= kAudioCommit && k.id <= kAudioNext);
        } else if (k.type == GridHotkey && k.id >= 1000 && k.id < 2000 && k.id >= kAudioCommit && k.id <= kAudioNext) {
            continue;
        }
        if (table != window && !(alsoGrid && window == GridHotkey))
            continue;
        // No hotkeys for hidden tag buttons.
        if (table == EditorHotkey && k.type == EditorHotkey && k.id >= kEditorTagButton1 + m_installedTagButtons &&
            k.id <= kEditorTagButton20)
            continue;
        // Hotkeys::GetHKey: an id under AUDIO_COMMIT (the _ALT ones) sends
        // its id + 10, so AUDIO_PLAY_LINE_ALT runs AUDIO_PLAY_LINE.
        if (sameChord(hotkey.accel, pressed))
            return qs(symbolOf(k.id < kAudioCommit ? k.id + 10 : k.id));
    }
    return {};
}

// ---- The Options page ----

void HotkeysController::beginOptions()
{
    // O2-stale-copy (approved): each dialog edits its own copy of the
    // bindings, filled at its first edit; a cancelled dialog's copy goes
    // with it (legacy's static hotkeysCopy outlived the dialog).
    m_copy.clear();
    m_list = std::make_unique<application::HotkeyList>(m_copy);
    m_list->build(bindings());
    emit rowsChanged();
}

QVariantList HotkeysController::rows() const
{
    QVariantList out;
    if (!m_list)
        return out;
    const bool filtered = m_list->filtered();
    int position = 0;
    for (const int i : m_list->shown()) {
        const auto &r = m_list->row(i);
        out << QVariantMap{{QStringLiteral("row"), i},
                           {QStringLiteral("text"), qs(r.text)},
                           {QStringLiteral("accel"), qs(r.accel)},
                           {QStringLiteral("textModified"), r.textModified},
                           {QStringLiteral("keyModified"), r.keyModified},
                           {QStringLiteral("block"), filtered ? m_list->hiddenBlock(position) : 0},
                           {QStringLiteral("inBlock"), filtered && r.visible == 2}};
        ++position;
    }
    return out;
}

bool HotkeysController::listFiltered() const
{
    return m_list && m_list->filtered();
}

int HotkeysController::topBlock() const
{
    return m_list && m_list->filtered() ? m_list->hiddenBlock(-1) : 0;
}

void HotkeysController::toggleBlock(int position)
{
    if (!m_list)
        return;
    m_list->toggleBlock(position);
    emit rowsChanged();
}

int HotkeysController::selected() const
{
    if (!m_list)
        return -1;
    const int row = m_list->selection();
    if (row < 0)
        return -1;
    const auto shown = m_list->shown();
    const auto it = std::ranges::find(shown, row);
    return it == shown.end() ? -1 : int(it - shown.begin());
}

int HotkeysController::filterMode() const
{
    return m_list ? m_list->filterMode() : 0;
}

void HotkeysController::filter(int mode)
{
    if (!m_list)
        return;
    m_list->filter(mode);
    emit rowsChanged();
}

void HotkeysController::select(int position)
{
    if (!m_list)
        return;
    m_list->selectShown(position);
    emit rowsChanged();
}

QVariantMap HotkeysController::mapTarget() const
{
    if (!m_list || m_list->selection() < 0)
        return {};
    const int row = m_list->selection();
    return {{QStringLiteral("name"), qs(m_list->mapName(row))},
            {QStringLiteral("windows"), m_list->mapOffersWindows(row)},
            {QStringLiteral("type"), m_list->row(row).key.type}};
}

QVariantMap HotkeysController::optionsConflict(const QString &accel, int type)
{
    if (!m_list || m_list->selection() < 0)
        return {};
    return conflictMap(m_list->mapConflict(m_list->selection(), accel.toStdString(), type, bindings()));
}

void HotkeysController::optionsMap(const QString &accel, int type, const QString &answer)
{
    if (!m_list || m_list->selection() < 0)
        return;
    m_list->map(m_list->selection(), accel.toStdString(), type, answerOf(answer), bindings());
    emit rowsChanged();
}

void HotkeysController::optionsReset()
{
    if (!m_list || m_list->selection() < 0)
        return;
    m_list->reset(m_list->selection(), bindings());
    emit rowsChanged();
}

void HotkeysController::optionsDelete()
{
    if (!m_list || m_list->selection() < 0)
        return;
    m_list->remove(m_list->selection(), bindings());
    emit rowsChanged();
}

void HotkeysController::optionsUndo()
{
    if (!m_list)
        return;
    m_list->undo();
    emit rowsChanged();
}

void HotkeysController::optionsRedo()
{
    if (!m_list)
        return;
    m_list->redo();
    emit rowsChanged();
}

void HotkeysController::commitOptions()
{
    if (!m_list)
        return;
    const auto committed = m_list->commit();
    if (!committed)
        return;
    // SetHotkeysMap(hotkeysCopy), SaveHkeys(), SaveHkeys(true), SetAccels().
    split(*committed);
    saveMain();
    saveAudio();
    setAccels();
    emit rowsChanged();
}

void HotkeysController::resetDefaults()
{
    // Hkeys.ResetDefaults(): the defaults in memory (the scripts' bindings
    // go too); nothing is saved or installed. ClearList, AddHotkeysOnList.
    split(application::defaultHotkeys());
    if (m_list) {
        m_list->clear();
        m_list->build(bindings());
    }
    emit rowsChanged();
}

QVariantMap HotkeysController::capture(int key, int modifiers, int window) const
{
    const auto c = application::captureHotkey(pressOf(key, modifiers), window);
    const char *result = c.result == application::HotkeyCapture::Result::Accepted  ? "accepted"
                         : c.result == application::HotkeyCapture::Result::Refused ? "refused"
                                                                                    : "ignored";
    return {{QStringLiteral("result"), QLatin1String(result)},
            {QStringLiteral("accel"), qs(c.accel)},
            {QStringLiteral("message"), qs(c.message)}};
}

// ---- The gesture (Hotkeys::OnMapHkey) ----

bool HotkeysController::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
        // Only a click on a button or menu item counts (a Shift+click in the
        // Grid extends the selection); the window sees the event first.
        const auto mods = static_cast<QMouseEvent *>(event)->modifiers() &
                          (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        const bool button = watched->inherits("QQuickAbstractButton");
        m_lastClickShift = button && mods == Qt::ShiftModifier;
        m_lastClickShiftHeld = button && (mods & Qt::ShiftModifier);
        m_lastClickCtrlHeld = button && (mods & Qt::ControlModifier);
        break;
    }
    case QEvent::KeyPress:
    case QEvent::ShortcutOverride:
        m_lastClickShift = false;
        m_lastClickShiftHeld = false;
        m_lastClickCtrlHeld = false;
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

bool HotkeysController::shiftClicked(bool exact) const
{
    return exact ? m_lastClickShift : m_lastClickShiftHeld;
}

QVariantMap HotkeysController::gestureTarget(const QString &symbolOrScript, bool second) const
{
    const std::string s = symbolOrScript.toStdString();
    if (s.starts_with("Script ")) {
        // OnMapHkey(-1, name): the script's binding, else the next script id.
        int id = application::kFirstScriptHotkey;
        for (const auto &[key, hotkey] : bindings())
            if (key.id >= application::kFirstScriptHotkey) {
                if (hotkey.name == s)
                    return {{QStringLiteral("id"), key.id}, {QStringLiteral("name"), symbolOrScript}};
                id = std::max(id, key.id + 1);
            }
        return {{QStringLiteral("id"), id}, {QStringLiteral("name"), symbolOrScript}};
    }
    const int id = application::hotkeyIdOf(s) - (second ? 10 : 0);
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), qs(application::hotkeyName(id))}};
}

QVariantMap HotkeysController::gestureConflict(int id, const QString &accel, int type) const
{
    return conflictMap(application::mapHotkeyNowConflict(bindings(), id, accel.toStdString(), type));
}

void HotkeysController::gestureMap(int id, const QString &name, const QString &accel, int type, const QString &answer)
{
    auto combined = bindings();
    const auto r = application::mapHotkeyNow(combined, id, name.toStdString(), accel.toStdString(), type,
                                             answerOf(answer));
    if (!r.changed)
        return;
    // SetAccels(true), SaveHkeys(), and SaveHkeys(true) when Audio was involved.
    split(combined);
    setAccels();
    saveMain();
    if (r.saveAudio)
        saveAudio();
    emit rowsChanged();
}

bool HotkeysController::ignoreFilteringFromOption() const
{
    return !m_settings.boolean("grid.ignoreFiltering");
}

} // namespace hikari::app
