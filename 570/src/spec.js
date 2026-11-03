const QUESTIONS = [
  {
    id: "Q1", title: "Ways in",
    rec: "Video › Perspective tracking…, after Shift by frames to video, and in command search. It is a new command with no default key, so it needs one hotkey id through the pin queue.",
    alt: "No new command: the dialog opens only from a Track… button on the Perspective tool's bar, so no hotkey id and no pins move.",
  },
  {
    id: "Q2", title: "Which of the script's options stay",
    rec: "Keep Relative to frame; Origin (Center, No \\fax, Keep, as on the Perspective tool, default Keep as the script); Fit flat lines to the plane first (the script's “Apply perspective”, with its default: on unless the active line already turns in X or Y); Move clips with the plane (“Track clips”, on); and Scale outline and shadow (“Scale \\bord and \\shad”, on). Drop “Track position”: off, it keeps each line's own position while the plane still warps it, and the script's author doubts its use. Always save the plane with each line, with no checkbox (the script's “Add quad to extradata”, on by default).",
    alt: "Keep every option under the script's own names, including Track position and Add quad to extradata.",
  },
  {
    id: "Q3", title: "What Apply does to the lines",
    rec: "As the script: each selected line is split into one line per video frame over the data's frames, each frame's \\t, \\fad and \\move are evaluated, and that frame's perspective tags are written. The tracked lines replace the originals and are selected, and the whole apply is one undo step, “Perspective tracking”.",
    alt: "Keep each original as a comment line above its tracked lines, so it can be tracked again.",
  },
  {
    id: "Q4", title: "How the data comes in",
    rec: "A paste box, prefilled when the clipboard holds Power-Pin data as long as the selection (as the script). A status line under it reads the data as it changes: the frames, the frame rate and the tracked size, and whether it fits the selection. No file picker.",
    alt: "Add Open file… next to the box, for Power-Pin data saved as a text file.",
  },
  {
    id: "Q5", title: "Without a video",
    rec: "The command is greyed with “Open a video first: tracking data is per frame.” and the dialog never opens without one.",
    alt: "The command stays on and opens a message box that says the same.",
  },
];

const SECTIONS = [
  {
    n: 1, id: "dialog", title: "The dialog",
    opens: "Video › Perspective tracking… with line 8 selected (frames 335–339) and Power-Pin data on the clipboard. Below it, the same dialog opened with nothing usable on the clipboard.",
    rows: [
      { label: "From the clipboard", scene: "dialog", args: ["ok"] },
      { label: "Empty", scene: "dialog", args: ["idle"] },
    ],
    notes: [
      "The box takes After Effects 6.0 keyframe data with the four CC Power Pin corners (top left 0002, top right 0003, bottom right 0005, bottom left 0004), the format arch1t3cht's script reads and Akatsumekusa's Blender exporter writes.",
      "Relative to frame starts at the video's frame when it lies in the selection, else at 1, as the script does. The lines keep their look at that frame, and every other frame follows the plane's change from it.",
      "Fit flat lines to the plane first is off here, because line 8 already turns in X and Y. On, it first gives the line the plane's perspective at the reference frame, keeping its scale.",
      "The footer names the lines and frames Apply will change. Apply is the default button while the data fits; Escape cancels.",
      "Every control has a label the screen reader reads; the status line is announced when it changes.",
    ],
    recs: [
      "Approve the dialog title “Perspective tracking” and its layout: the data on the left, the options on the right.",
      "Approve the kept options and their labels (Q2).",
      "Approve the paste box prefilled from the clipboard, with no file picker (Q4).",
      "Approve the status line text: “5 frames at 23.976 fps, tracked at 1920 × 1080. Covers the selection’s 5 frames, 335–339.” and the empty hint.",
    ],
  },
  {
    n: 2, id: "states", title: "When the data doesn't fit",
    opens: "The status line, and Apply, for the script's error cases. The script stops with a log line; here Apply is greyed until the data fits, and nothing changes.",
    rows: [
      { label: "Wrong length", scene: "dialog", args: ["length"] },
      { label: "Not Power-Pin", scene: "dialog", args: ["invalid"] },
      { label: "Other size", scene: "dialog", args: ["size"] },
      { label: "Lines can't share frames", scene: "dialog", args: ["overlap"] },
    ],
    notes: [
      "The length must equal the frames the selected lines cover, as in the script.",
      "Data tracked at another size is scaled to the video, with a warning; Apply stays on. The script has a FIXME here and uses the numbers as they are.",
      "Overlapping lines are tracked together only when every one is shown at the reference frame, as in the script. Otherwise the status line says which line is missing.",
      "A line with \\move is not refused: it is split and each frame is tracked from where it shows, as the script does.",
    ],
    recs: [
      "Approve greying Apply, with the reason in the status line, for the wrong length, data that isn't Power-Pin, and lines that can't share the reference frame.",
      "Approve scaling data tracked at another size to the video, with the warning.",
    ],
  },
  {
    n: 3, id: "after", title: "After Apply",
    opens: "Line 8 of the corpus run before and after, with the script's own output: one line per frame, the plane saved with each, and one step in History.",
    rows: [
      { label: "Before", scene: "before" },
      { label: "After", scene: "after" },
    ],
    notes: [
      "The lines are split on frame boundaries, so the first starts at 0:00:13.993, the start of frame 335. \\fad becomes each frame's \\alpha.",
      "Each tracked line carries its frame's outer quad in extradata (_aegi_perspective_ambient_plane), so the Perspective tool shows that frame's plane and Derive Perspective Track reads it back.",
      "Undo removes the tracked lines and brings line 8 back as it was, extradata included.",
      "The perspective maths is VT3's port of arch1t3cht's (ISC); Help › Credits keeps its notice.",
    ],
    recs: [
      "Approve the frame-by-frame split, replacing the originals and selecting the result, as one undo step “Perspective tracking” (Q3).",
      "Approve saving the plane with each tracked line, always (Q2).",
    ],
  },
  {
    n: 4, id: "ways", title: "Ways in",
    opens: "The Video menu with a video open, and without one.",
    rows: [
      { label: "With video", scene: "menu", args: [true] },
      { label: "No video", scene: "menu", args: [false] },
    ],
    notes: [
      "Perspective tracking… joins the line commands that work from the video's frames, after Shift by frames to video.",
      "The same command is in command search as “Perspective tracking”. It has no default key and can be bound in Settings › Hotkeys.",
      "Without lines selected the command is greyed with “Select the lines to track”.",
    ],
    recs: [
      "Approve the menu item and its place, a command with no default key (Q1).",
      "Approve greying it without a video, with the reason as its hint (Q5).",
    ],
  },
];
