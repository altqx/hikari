# HikariSub

HikariSub edits subtitle documents and coordinates their text, timing, styles and media context. This glossary records the terms already settled for the rewrite; it will grow as the remaining domain decisions are resolved.

## Language

**Workspace**:
The application's shared arrangement of subtitle-editing panels and tools, independent of which document is the editing target.
_Avoid_: Document layout when referring to this shared arrangement.

**Editing target**:
The subtitle document to which ordinary editing commands apply. A tool with an explicit pinned target may name a different unprotected document. It is distinct from the control or panel that currently has keyboard focus.
_Avoid_: Focused panel as a synonym for the document receiving edits.

**Protected reference**:
A document shown for optional comparison whose content cannot be changed by editing-target commands.
_Avoid_: Second editing target.

**Tool target**:
The document explicitly named by a tool for inspection or an operation. It follows the editing target by default and can be pinned independently; a protected reference permits inspection but no content changes.
_Avoid_: Keyboard focus as a synonym for the tool target.
