---
status: accepted
---

# Build node effects as a native, deterministic graph engine

On 2026-10-28 altqx settled the node effect editor in the wave-8 grilling session ([#410](https://github.com/altqx/hikari/issues/410), card [NE0 #422](https://github.com/altqx/hikari/issues/422)). A node effect is a graph that turns source Lines into generated karaoke-effect Lines, edited on a canvas inside HikariSub. The prior art and the options are in the [node effect editor research note](../research/node-effect-editor.md); where it differs from this record, this record wins.

## Clean-room

The engine, the nodes and the canvas are our own implementation. [Mandholine/Pyxis-Effector](https://github.com/Mandholine/Pyxis-Effector) is credited as inspiration only. No Pyxis code is reused, and HikariSub doesn't import Pyxis graphs.

## Engine (option C)

- A native, typed, deterministic engine evaluates a graph and writes Lines directly.
- "Export as template Lines" writes the graphs the stock karaoke templater can express as ordinary `template` and `code` Lines, so they run in Aegisub too.

## Storage

- A graph lives in its Document, as a Comment Line `graph <name>` holding the graph as JSON.
- A graph library in the user folder keeps graphs for reuse across files.
- Generated Lines carry the Effect `fx <graph name>`, and KP1's folding of generated Lines in the Grid learns that marker.

## Fields, Lua and randomness

- A node's fields hold typed constants. Computed values reach a node only through wires.
- Lua runs only in an explicit Lua node, behind the trust prompt that templates holding Lua already use.
- Randomness comes only from a seeded Random node, so the same graph and the same source Lines give the same output.

## First milestone

About 20 core nodes, with all ten KE3 [karaoke presets](../karaoke-presets.md) rebuilt as graphs whose output is byte-identical to the presets' goldens.

## Its own mode inside the editor

Node effects are a mode of the main window, not a separate program, in the way Translation mode is.

- The mode shows the canvas, the video preview, a compact source list and a node inspector with values, warnings and timings.
- The canvas is a custom Qt Quick item with QtQuick.Shapes wires.
- The toolbar and the View menu switch the mode on and off. Leaving it restores the layout the tab had before.
- The mode and its canvas ([NE2 #424](https://github.com/altqx/hikari/issues/424)) are a design gate: they need owner-approved light and dark mockups before production.

## On the Hikari API

The node catalogue, the graph format and its validator are part of the Hikari API ([HA1 #411](https://github.com/altqx/hikari/issues/411)) and versioned with it: a running number that grows add-only, with removals deprecated for at least one number first. The AI's `effects.*` tools ([AI16 #444](https://github.com/altqx/hikari/issues/444)) use it.

## Around the editor

- **Order:** HA1 comes first. Node effects and plugins then run in parallel.
- **The site** gets a node-effects guide.
