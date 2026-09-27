---
status: accepted
---

# Keep native docking behind Hikari's layout adapter

On 2026-09-27, [altqx approved](https://github.com/altqx/hikari/issues/46#issuecomment-5853318062) KDDockWidgets 2.4.1, QtQuick frontend only, pinned at `c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8`, behind a Hikari-owned WorkspaceLayout service. Its split/tab/floating mechanics fit the accepted workspace while avoiding a custom docking engine or a QWidget shell. Use its GPLv3 option and retain source, patches and notices; no commercial purchase is assumed.

The [integration contract](../qt/docking.md) keeps Documents, drafts, tool targets and media owners outside dock views. Hikari owns stable panel/window identities, keyboard placement and focus restoration, schema validation, atomic persistence and recovery from partial restore or changed monitors. The layout engine owns arrangement mechanics. Wayland grouping/size restoration is supported as qualified; absolute floating-window positioning is not promised.

The choice is accepted with an explicit Qt 6.11.2 native qualification matrix covering cross-window resources, keyboard/assistive technology, mixed DPI/monitors, Wayland, fullscreen and invalid layouts. No native pass follows from this decision. Engine upgrades require migration/restore evidence; a demonstrated incompatibility reopens the decision rather than silently changing the workspace contract.
