# Star on GitHub suggestion

Once someone has used HikariSub for a while, one quiet toast suggests starring it on GitHub ([ST1 #645](https://github.com/altqx/hikari/issues/645)). It never asks for anything else: no rating, sign-in or email.

## What it shows

The NT1 toast, as an info notice: **Enjoying HikariSub?** "A star on GitHub helps others find it." with **Star on GitHub**, which opens https://github.com/altqx/hikari, and **Not now**. It is not written to the Log.

## When

All of these must hold:
- **Settings > Updates and privacy > Suggest starring HikariSub on GitHub** is on. It is on by default; off stops it for good.
- HikariSub has been opened on at least **5 different days**, and has at least **3 hours of foreground editing**: time while its window is active and a Document is open, counted a minute at a time.
- At least **10 minutes** since this run started.
- Not a **first run** (Getting started still due), and not the **first run after an update** (What's new).
- Nothing is **playing** (video or audio), the video is not **full screen**, no **dialog or popup** is open, and no key was pressed in the last **10 seconds**.
- Not **snoozed**, and shown **fewer than twice**, and Star on GitHub never clicked.

Each showing snoozes it for **30 days**, however it goes: Not now, the close button or the toast fading. After the second showing, or once Star on GitHub is clicked, it never shows again.

## What is kept and sent

The counters are in `settings.toml`: `starPrompt.days`, `starPrompt.lastDay` (an ISO date), `starPrompt.editingSeconds`, `starPrompt.shown`, `starPrompt.snoozedUntil` (seconds since the epoch) and `starPrompt.starred`. The switch is `privacy.starPrompt`.

Nothing is sent for it, except with usage data on: `star_prompt_shown` and `star_prompt_clicked`, with no properties of their own ([analytics.md](analytics.md)).

## For developers

- `src/app/star_prompt.cpp` holds the counters and every rule; Main.qml passes the playback, full-screen and dialog state to `offer()` when it emits `due()`, then shows the toast.
- `hikari_ui_star_prompt_tests` checks each rule with a fake clock; `hikari_ui_star_prompt_ui_tests` shows the toast in the window, clicks Star on GitHub through a fake opener and checks it never comes back.
