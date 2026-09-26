# Keyboard navigation and screen-reader support in Qt Quick

Research for [#18](https://github.com/altqx/hikari/issues/18), completed 2026-09-27. This is source-based feasibility research, not a claim that HikariSub passed assistive-technology testing. Recommendations are inputs to decisions, not architecture already chosen.

## Answer

Qt Quick provides focus management, semantic metadata and platform bridges. It does not automatically make a custom subtitle editor accessible. Prefer Quick Controls for ordinary controls, explicitly design navigation between workspace regions, and give custom grid/waveform/video tools semantic interfaces and equivalent keyboard commands. A canvas with one accessible name is insufficient.

Use agent-built QML/HTML prototype tickets for user reaction, with Figma Starter only for occasional handoff as requested. HTML can validate layout preferences; only a QML executable with real screen readers can establish Qt/UIA/AT-SPI behavior. No screen-reader or performance measurements were performed for this report.

## Metadata and keyboard behavior

`Accessible` supplies names, descriptions, roles, check/edit/focus/selection states and action signals. Give interactive items concise translated names, expose current states, and connect accessibility actions to the same commands used by mouse and keyboard. `ignored` can suppress decorative/duplicate icon and text children. `announce(message, politeness)` exists since 6.8; `labelFor`/`labelledBy` require 6.10. Do not use those newer relations unconditionally with a 6.8 baseline. Standard Quick Controls already supply keyboard and accessibility behavior, which custom styling must preserve. [Accessible API](https://doc.qt.io/qt-6/qml-qtquick-accessible.html), [Quick accessibility](https://doc.qt.io/qt-6/accessible-qtquick.html).

`activeFocusOnTab` participates in tab traversal. `FocusScope` encapsulates a component's remembered focus target, not an application-wide navigation policy. `KeyNavigation` links directional/tab targets and can skip disabled/invisible items along the navigation chain. Events first reach the active item and then its parents until accepted; a global arrow handler must not steal text cursor movement. [Focus model](https://doc.qt.io/qt-6/qtquick-input-focus.html), [KeyNavigation](https://doc.qt.io/qt-6/qml-qtquick-keynavigation.html), [Item.activeFocusOnTab](https://doc.qt.io/qt-6/qml-qtquick-item.html#activeFocusOnTab-prop).

Recommended interaction contract to prototype: F6/Shift+F6 cycles workspace regions; Tab/Shift+Tab traverses controls or defined groups; arrows operate within a group/grid except while editing text; Esc leaves editing or dismisses a popup and restores its invoker; Enter/Space activate appropriate controls; Shift+F10 opens contextual actions. Hidden, removed or detached panels must not retain dead focus targets. This is a proposed HikariSub contract, not a Qt default or approved decision.

## Platform bridges

Qt's Windows plugin implements UI Automation. The 6.8.3 source handles `WM_GETOBJECT`, returns UIA providers and maps focus, selection, name, value, text and announcement events. Although the generic QAccessible overview mentions MSAA, that is not evidence that contemporary Qt lacks UIA. [Pinned Windows UIA source](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/windows/uiautomation/qwindowsuiaaccessibility.cpp).

Test NVDA, Narrator and JAWS separately: a correct UIA tree does not guarantee identical speech across readers. Use a UIA inspector to distinguish bad metadata from reader-specific behavior, and record OS, Qt patch and reader versions. MuseScore reports below demonstrate why this matters.

On Linux, Qt uses AT-SPI; its documentation describes D-Bus accessibility/screen-reader activation properties and `QT_LINUX_ACCESSIBILITY_ALWAYS_ON`. A GUI rendered offscreen is not an Orca test: use a desktop session with its accessibility bus and exercise supported X11/Wayland configurations. The variable requests activation, not proof of accessible behavior. [QAccessible platform notes](https://doc.qt.io/qt-6/qaccessible.html).

## Custom-painted surfaces

`QAccessibleInterface` supplies semantic hierarchy, global geometry, hit testing, names, roles and states. Add action, text, value, table/cell and selection interfaces as required; register a factory for custom QObject/item types and emit appropriate change events. A texture or scene-graph drawing does not expose its logical rows or handles. [Interface contract](https://doc.qt.io/qt-6/qaccessibleinterface.html), [factory/events](https://doc.qt.io/qt-6/qaccessible.html), [table interface](https://doc.qt.io/qt-6/qaccessibletableinterface.html), [value interface](https://doc.qt.io/qt-6/qaccessiblevalueinterface.html).

Proposed semantic models:

- Grid: row/column descriptions, selected/current row, editable text/time cells, selection actions and stable identities across sorting/filtering. Virtualization must preserve a navigable semantic hierarchy; do not make every pixel a child or omit data needed by keyboard navigation.
- Waveform: named playhead and selection endpoints with units and increment/decrement actions, seek/snap/zoom commands, and precise numeric time fields. Do not announce every amplitude sample.
- Video transforms: selected object, position/scale/rotation and named handles or equivalent numeric controls. Labeling the canvas does not make a drag operation accessible.

These are implementation proposals, not measured results. Avoid competing Qt and custom trees for the same object.

## MuseScore prior art

MuseScore's handbook documents a hierarchy: F6 between sections, Tab between control groups, arrows within groups, Esc to leave editable-control interaction, and Enter/Space to activate. Its Tab behavior deliberately skips some controls within a group. [Official accessibility handbook](https://handbook.musescore.org/navigation), [workspace explanation](https://handbook.musescore.org/navigation/the-user-interface).

The current framework revision inspected was `1d8529e782618391bf0187a2e34c85f499b0680e`. `NavigationSection` owns panels, `NavigationPanel` defines direction and controls, `NavigationControl` handles activation/triggering, and a controller dispatches section/panel/directional commands. Its accessibility module exposes an application-level interface backed by Qt, including for self-drawn content. This is current framework prior art, not the exact implementation shipped by every MuseScore 4 release. [Section](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/ui/qml/Muse/Ui/navigationsection.h), [panel](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/ui/qml/Muse/Ui/navigationpanel.h), [control](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/ui/qml/Muse/Ui/navigationcontrol.h), [controller](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/ui/internal/navigationcontroller.cpp), [module scope](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/accessibility/README.md).

The accessibility controller sends creation/destruction, focus, state, text and value events. Its inspected announcement code disables a `QAccessibleAnnouncementEvent` path and uses externally changed names plus focus revoicing. Comments report VoiceOver interruption and NVDA “blank” problems, and reader-specific name/description behavior. These are upstream observations, not independently reproduced results or an endorsement of copying the workaround. [Announcement implementation](https://github.com/musescore/muse_framework/blob/1d8529e782618391bf0187a2e34c85f499b0680e/framework/accessibility/internal/accessibilitycontroller.cpp).

Concrete reports: [#8982](https://github.com/musescore/MuseScore/issues/8982) described competing accessibility trees in 2021 and is closed; [#25404](https://github.com/musescore/MuseScore/issues/25404) reports silent instrument settings with NVDA in 4.4.3 while JAWS worked; [#15743](https://github.com/musescore/MuseScore/issues/15743) reports another reader discrepancy. Historical application reports do not establish a current Qt-wide regression.

## Known Qt gaps and version sensitivity

Upstream development history contains concrete accessibility fixes/additions relevant to the design:

| Evidence | Risk to exercise |
|---|---|
| [TextInput AT-SPI editable-text support, February 2026](https://github.com/qt/qtdeclarative/commit/007960c48aeb5f8ae9226a12a9097fa24fdbe9a2), [TextEdit counterpart](https://github.com/qt/qtdeclarative/commit/b0844a4433ad249573274ea5d55c4ad2a3962458) | Linux text editing through AT interfaces |
| [WindowContainer hierarchy, August 2026](https://github.com/qt/qtdeclarative/commit/581daf9f40140f43a790cd62b0389d5ee830d6f6), [parent consistency](https://github.com/qt/qtdeclarative/commit/c55d3d91b331cd347a95a519f1846a52987d2fda) | Embedded/native windows and detached workspace trees |
| [UIA heading levels, QTBUG-119057](https://github.com/qt/qtbase/commit/0cabfd66706c0de66c9d773d855374d00f35518b), [set position/size, QTBUG-149577](https://github.com/qt/qtbase/commit/64a9bd4c2291359a125b406bff43d3ecace07b42) | Announced hierarchy/group position |

These were observed in upstream development history. This report has **not established which stable patch releases include them**. Inspect the chosen tag before relying on a fix or deciding a backport is needed. Direct Qt tracker searches did not yield usable issue pages; the Qt commits identify bugs without inventing their current status.

## Contrast, motion and scaling

Qt 6.10 adds `QStyleHints::accessibility` and `QAccessibilityHints::contrastPreference` with change notification. Qt identifies Basic, Fusion, macOS and FluentWinUI3 among Quick styles supporting high contrast. Hardcoded colors can defeat it: use semantic palette roles and test focus, selection and error states. [Hints API](https://doc.qt.io/qt-6/qaccessibilityhints.html), [style hints](https://doc.qt.io/qt-6/qstylehints.html), [Qt high-contrast overview](https://www.qt.io/blog/high-contrast-mode-in-qt-6.10).

The inspected `QAccessibilityHints` API exposes contrast, **not a universal reduced-motion property**. Provide an application motion preference; add a small platform adapter if OS integration is required and verify its signals during implementation. Do not claim automatic reduced-motion support. Qt high DPI scales device-independent coordinates and uses Windows display scaling, but display scaling differs from text-only enlargement. Inherit system font sizes, use layouts, offer adjustable text size and test for clipping. [High DPI](https://doc.qt.io/qt-6/highdpi.html), [Qt accessibility guidance](https://doc.qt.io/qt-6/accessible.html).

## Follow-on gates

Research is complete; implementation still needs these decisions and tests:

1. Choose the Qt baseline and a small application navigation layer versus Muse adoption. Framework adoption costs/licensing are a separate decision.
2. QML prototype: keyboard-only open/edit/save, modal automation dialog, detached panels, row/cell selection, waveform timing and focus restoration. HTML is useful for user reaction but does not validate Qt integration.
3. Documented runtime matrix: Windows NVDA/Narrator and JAWS where available; Linux Orca; normal/high contrast; large fonts; keyboard-only operation. Record exact versions, actions, labels and actual speech. No passing results are claimed here.
4. Every surface spec should define names, roles, states, actions, focus order, error announcements and numeric alternatives. Custom rendering needs explicit semantic-interface implementation and test work.
