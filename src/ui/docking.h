#pragma once

// D1: Hikari's adapter around the docking engine (KDDockWidgets, QtQuick
// frontend; docs/qt/docking.md). The engine is process-wide and serves one
// QML engine; nothing outside this adapter talks to it directly.

class QQmlEngine;

namespace hikari::ui {

// Binds the docking engine to the QML engine that is about to load the shell.
// Call it before loading Main. The production shell has one engine for its
// lifetime. When an earlier engine has been destroyed (tests create one per
// case) the docking engine is re-initialized for the new one; while an
// earlier engine still lives, the call does nothing and returns false.
bool attachDocking(QQmlEngine &engine);

} // namespace hikari::ui
