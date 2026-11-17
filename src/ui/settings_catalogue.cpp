#include "settings_catalogue.h"

#include "hikari/application/settings.h"

#include <QCoreApplication>

#include <algorithm>
#include <span>

namespace hikari::ui {
namespace {

constexpr char kOptions[] = "HikariSub.Options";

struct Text {
    const char *source = nullptr;
    const char *context = kOptions;

    QString translated() const { return source ? QCoreApplication::translate(context, source) : QString(); }
};

struct Page {
    const char *id;
    Text name;
    const char *icon;
};

struct Section {
    const char *id;
    Text title;
    Text keywords;
};

struct Row {
    const char *setting;
    const char *section;
    Text label;
    Text description;
};

constexpr Page kPages[] = {
    {"general", {QT_TRANSLATE_NOOP("HikariSub.Options", "General")}, "settings"},
    {"appearance", {QT_TRANSLATE_NOOP("HikariSub.Options", "Appearance")}, "appearance"},
    {"lineEditor", {QT_TRANSLATE_NOOP("HikariSub.Options", "Line editor")}, "editor"},
    {"editing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Editing")}, "cycle-values"},
    {"spelling", {QT_TRANSLATE_NOOP("HikariSub.Options", "Spelling and grammar")}, "spellchecker"},
    {"grid", {QT_TRANSLATE_NOOP("HikariSub.Options", "Grid")}, "settings-grid"},
    {"video", {QT_TRANSLATE_NOOP("HikariSub.Options", "Video")}, "settings-video"},
    {"audio", {QT_TRANSLATE_NOOP("HikariSub.Options", "Audio")}, "settings-audio"},
    {"conversion", {QT_TRANSLATE_NOOP("HikariSub.Options", "Conversion")}, "convert"},
    {"subtitleProperties", {QT_TRANSLATE_NOOP("HikariSub.Options", "Script properties")}, "script-properties"},
    {"automation", {QT_TRANSLATE_NOOP("HikariSub.Options", "Automation")}, "automation"},
    {"hotkeys", {QT_TRANSLATE_NOOP("HikariSub.Options", "Shortcuts")}, "hotkeys"},
    {"ai", {QT_TRANSLATE_NOOP("HikariSub.Options", "AI")}, "ai-assistant"},
    {"updates", {QT_TRANSLATE_NOOP("HikariSub.Options", "Updates and privacy")}, "check-updates"},
};

constexpr Section kSections[] = {
    {"general.language", {QT_TRANSLATE_NOOP("HikariSub.Options", "Language")}},
    {"general.tabs", {QT_TRANSLATE_NOOP("HikariSub.Options", "Tabs")}},
    {"general.saving", {QT_TRANSLATE_NOOP("HikariSub.Options", "Saving")}},
    {"general.notices", {QT_TRANSLATE_NOOP("HikariSub.Options", "Notices")}},
    {"general.folders", {QT_TRANSLATE_NOOP("HikariSub.Options", "Folders")}},
    {"general.trustedFolders", {QT_TRANSLATE_NOOP("HikariSub.Options", "Trusted folders")}},
    {"general.legacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Legacy")}},
    {"appearance.theme", {QT_TRANSLATE_NOOP("HikariSub.Options", "Theme")}},
    {"appearance.accentColour", {QT_TRANSLATE_NOOP("HikariSub.Options", "Accent colour")}},
    {"appearance.uiColours", {QT_TRANSLATE_NOOP("HikariSub.Options", "UI colours")}},
    {"appearance.font", {QT_TRANSLATE_NOOP("HikariSub.Options", "Font")}},
    {"appearance.window", {QT_TRANSLATE_NOOP("HikariSub.Options", "Window")}},
    {"lineEditor.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Display")}},
    {"lineEditor.editing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Editing")}},
    {"lineEditor.translationMode", {QT_TRANSLATE_NOOP("HikariSub.Options", "Translation mode")}},
    {"lineEditor.tagButtons", {QT_TRANSLATE_NOOP("HikariSub.Options", "Tag buttons")}},
    {"editing.cycleValues", {QT_TRANSLATE_NOOP("HikariSub.Options", "Cycle values")}},
    {"spelling.spelling", {QT_TRANSLATE_NOOP("HikariSub.Options", "Spelling")}},
    {"spelling.grammar", {QT_TRANSLATE_NOOP("HikariSub.Options", "Grammar")}},
    {"spelling.ruleGroups", {QT_TRANSLATE_NOOP("HikariSub.Options", "Rule groups")}},
    {"grid.activeLine", {QT_TRANSLATE_NOOP("HikariSub.Options", "Active line")}},
    {"grid.columns", {QT_TRANSLATE_NOOP("HikariSub.Options", "Columns")}},
    {"grid.counting", {QT_TRANSLATE_NOOP("HikariSub.Options", "Counting")}},
    {"grid.splitting", {QT_TRANSLATE_NOOP("HikariSub.Options", "Splitting")}},
    {"grid.newLines", {QT_TRANSLATE_NOOP("HikariSub.Options", "New lines")}},
    {"video.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Playback")}},
    {"video.subtitlesOnVideo", {QT_TRANSLATE_NOOP("HikariSub.Options", "Subtitles on video")}},
    {"video.decoding", {QT_TRANSLATE_NOOP("HikariSub.Options", "Decoding")}},
    {"video.view", {QT_TRANSLATE_NOOP("HikariSub.Options", "View")}},
    {"video.visualTools", {QT_TRANSLATE_NOOP("HikariSub.Options", "Visual tools")}},
    {"video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "Script resolution")}},
    {"audio.output", {QT_TRANSLATE_NOOP("HikariSub.Options", "Output")}},
    {"audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Playback")}},
    {"audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Display")}},
    {"audio.snapping", {QT_TRANSLATE_NOOP("HikariSub.Options", "Snapping")}},
    {"audio.karaoke", {QT_TRANSLATE_NOOP("HikariSub.Options", "Karaoke")}},
    {"audio.cache", {QT_TRANSLATE_NOOP("HikariSub.Options", "Cache")}},
    {"conversion.style", {QT_TRANSLATE_NOOP("HikariSub.Options", "Style")}},
    {"conversion.timing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Timing")}},
    {"conversion.ass", {QT_TRANSLATE_NOOP("HikariSub.Options", "ASS")}},
    {"conversion.export", {QT_TRANSLATE_NOOP("HikariSub.Options", "Export")}},
    {"subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Credits"), "HikariSub.ScriptProperties"}},
    {"automation.files", {QT_TRANSLATE_NOOP("HikariSub.Options", "Files")}},
    {"automation.debugging", {QT_TRANSLATE_NOOP("HikariSub.Options", "Debugging")}},
    {"automation.scriptDialogs", {QT_TRANSLATE_NOOP("HikariSub.Options", "Script dialogs")}},
    {"automation.keptAnswersAndPresets", {QT_TRANSLATE_NOOP("HikariSub.Options", "Kept answers and presets")}},
    {"ai.aiFeatures", {QT_TRANSLATE_NOOP("HikariSub.Options", "AI features")}},
    {"ai.providers", {QT_TRANSLATE_NOOP("HikariSub.Options", "Providers")}},
    {"ai.models", {QT_TRANSLATE_NOOP("HikariSub.Options", "Models")}},
    {"ai.webSearch", {QT_TRANSLATE_NOOP("HikariSub.Options", "Web search")}},
    {"ai.externalAgents", {QT_TRANSLATE_NOOP("HikariSub.Options", "External agents")}},
    {"ai.mcp", {QT_TRANSLATE_NOOP("HikariSub.Options", "MCP")}},
    {"ai.agent", {QT_TRANSLATE_NOOP("HikariSub.Options", "Agent")}},
    {"ai.skills", {QT_TRANSLATE_NOOP("HikariSub.Options", "Skills")}},
    {"ai.inlineActions", {QT_TRANSLATE_NOOP("HikariSub.Options", "Inline actions")}},
    {"ai.aiComponents", {QT_TRANSLATE_NOOP("HikariSub.Options", "AI components")}},
    {"ai.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Privacy")}},
    {"ai.keys", {QT_TRANSLATE_NOOP("HikariSub.Options", "Keys")}},
    {"updates.checking", {QT_TRANSLATE_NOOP("HikariSub.Options", "Checking")}},
    {"updates.installing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Installing")}},
    {"updates.plugins", {QT_TRANSLATE_NOOP("HikariSub.Options", "Plugins")}},
    {"updates.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Privacy")}},
};

constexpr Row kRows[] = {
    {"program.language", "general.language", {QT_TRANSLATE_NOOP("HikariSub.Options", "Interface language")}, {}},
    {"subtitles.openInNewTab", "general.tabs", {QT_TRANSLATE_NOOP("HikariSub.Options", "Always open subtitles in a new tab")}, {}},
    {"program.tabTextMaxChars", "general.tabs", {QT_TRANSLATE_NOOP("HikariSub.Options", "Tab name length")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Number of tab name characters. Range from 20 to 150")}},
    {"autosave.maxFiles", "general.saving", {QT_TRANSLATE_NOOP("HikariSub.Options", "Autosave files kept")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Number of autosaves can be set from 2 to 1000000")}},
    {"notices.toasts", "general.notices", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show notices as toasts")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Off: notices go to the Log only.")}},
    {"fonts.externalDirectory", "general.folders", {QT_TRANSLATE_NOOP("HikariSub.Options", "Fonts folder")}, {}},
    {"editor.hideTags", "lineEditor.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Hide tags")}, {}},
    {"editor.timesToFramesSwitch", "lineEditor.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show video frames instead of times")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "In the line editor and the grid.")}},
    {"editor.dontGoToNextLineOnTimesEdit", "lineEditor.editing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Stay on the line when editing times")}, {}},
    {"editor.allowNumpadHotkeys", "lineEditor.editing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Use numpad shortcuts in text fields")}, {}},
    {"editor.softLineBreak", "lineEditor.editing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Shift+Enter inserts a soft line break (%1)")}, {}},
    {"translation.showOriginal", "lineEditor.translationMode", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show the original")}, {}},
    {"translation.hideOriginalOnVideo", "lineEditor.translationMode", {QT_TRANSLATE_NOOP("HikariSub.Options", "Hide the original on the video")}, {}},
    {"translation.autoMoveTagsFromOriginal", "lineEditor.translationMode", {QT_TRANSLATE_NOOP("HikariSub.Translation", "Tags from original"), "HikariSub.Translation"}, {}},
    {"translation.sourceLanguage", "lineEditor.translationMode", {QT_TRANSLATE_NOOP("HikariSub.Project", "Source language"), "HikariSub.Project"}, {}},
    {"translation.targetLanguage", "lineEditor.translationMode", {QT_TRANSLATE_NOOP("HikariSub.Project", "Target language"), "HikariSub.Project"}, {}},
    {"translation.glossary", "lineEditor.translationMode", {QT_TRANSLATE_NOOP("HikariSub.Options", "Glossary")}, {}},
    {"editor.spellchecker", "spelling.spelling", {QT_TRANSLATE_NOOP("HikariSub.Options", "Check spelling")}, {}},
    {"editor.dictionaryLanguage", "spelling.spelling", {QT_TRANSLATE_NOOP("HikariSub.Options", "Primary language")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Words in other languages are checked with the other installed dictionaries for their script. Words in a script with no dictionary are not marked.")}},
    {"editor.suggestionsOnDoubleClick", "spelling.spelling", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show suggestions on double-click")}, {}},
    {"grammar.language", "spelling.grammar", {QT_TRANSLATE_NOOP("HikariSub.Options", "Grammar check")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Harper checks the Text field (the Translated field in translation mode) after you stop typing. It runs as a separate program only while grammar check is on.")}},
    {"grid.changeActiveOnSelection", "grid.activeLine", {QT_TRANSLATE_NOOP("HikariSub.Options", "Change the active line when adding to the selection")}, {}},
    {"grid.duplicationDontChangeSelection", "grid.activeLine", {QT_TRANSLATE_NOOP("HikariSub.Options", "Keep the selection when duplicating lines")}, {}},
    {"grid.setVisibleLineAfterFullScreen", "grid.activeLine", {QT_TRANSLATE_NOOP("HikariSub.Options", "Find the visible line after leaving full screen")}, {}},
    {"grid.autoHideColumns", "grid.columns", {QT_TRANSLATE_NOOP("HikariSub.Options", "Hide empty columns")}, {}},
    {"grid.calcSpacesAndPunctuationForWraps", "grid.counting", {QT_TRANSLATE_NOOP("HikariSub.Options", "Count spaces and punctuation for wraps")}, {}},
    {"grid.calcSpacesAndPunctuationForCps", "grid.counting", {QT_TRANSLATE_NOOP("HikariSub.Options", "Count spaces and punctuation for CPS")}, {}},
    {"grid.splitFramesBakeAnimations", "grid.splitting", {QT_TRANSLATE_NOOP("HikariSub.Options", "Bake animations when splitting into frames")}, {}},
    {"grid.insertStartOffset", "grid.newLines", {QT_TRANSLATE_NOOP("HikariSub.Options", "Start frame offset")}, {}},
    {"grid.insertEndOffset", "grid.newLines", {QT_TRANSLATE_NOOP("HikariSub.Options", "End frame offset")}, {}},
    {"grid.tagsSwapCharacter", "grid.newLines", {QT_TRANSLATE_NOOP("HikariSub.Options", "ASS tag replacement")}, {}},
    {"video.pauseOnClick", "video.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Left mouse button pauses video")}, {}},
    {"video.fullScreenOnStart", "video.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Open video from the context menu in full screen")}, {}},
    {"video.openAtActiveLine", "video.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Open video at the active line’s time")}, {}},
    {"video.moveToActiveLine", "video.playback", {}, {}},
    {"video.playAfterSelection", "video.playback", {}, {}},
    {"video.playbackPlayer", "video.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Player")}, {}},
    {"video.subtitleProvider", "video.subtitlesOnVideo", {QT_TRANSLATE_NOOP("HikariSub.Options", "Subtitle display filter")}, {}},
    {"video.previewSubtitlesAt", "video.subtitlesOnVideo", {QT_TRANSLATE_NOOP("HikariSub.Options", "Preview subtitles at")}, {}},
    {"video.disableLiveEditing", "video.subtitlesOnVideo", {QT_TRANSLATE_NOOP("HikariSub.Options", "Turn off the edits preview")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Applies when the tab opens again.")}},
    {"video.ffms2Seeking", "video.decoding", {QT_TRANSLATE_NOOP("HikariSub.Options", "Seeking method")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Applies when the video opens again.")}},
    {"video.gpuConversion", "video.decoding", {QT_TRANSLATE_NOOP("HikariSub.Options", "Convert video colours on the GPU")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Applies when the video opens again.")}},
    {"video.acceptedAudioStream", "video.decoding", {QT_TRANSLATE_NOOP("HikariSub.Options", "Preferred audio")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Separated by semicolons.")}},
    {"video.zoomModel", "video.view", {QT_TRANSLATE_NOOP("HikariSub.Options", "Zoom and pan")}, {}},
    {"video.zoomPercent", "video.view", {QT_TRANSLATE_NOOP("HikariSub.Options", "Start zoom")}, {}},
    {"video.scrollAction", "video.view", {QT_TRANSLATE_NOOP("HikariSub.Options", "Wheel on the video")}, {}},
    {"video.ctrlScrollAction", "video.view", {QT_TRANSLATE_NOOP("HikariSub.Options", "Ctrl+wheel on the video")}, {}},
    {"video.shiftScrollAction", "video.view", {QT_TRANSLATE_NOOP("HikariSub.Options", "Shift+wheel on the video")}, {}},
    {"video.handleSize", "video.visualTools", {QT_TRANSLATE_NOOP("HikariSub.Options", "Handle size")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Size of the visual tools’ handles. Range from 50 to 400")}},
    {"video.visualWarningsOff", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "Turn off the visual tools warning")}, {}},
    {"video.dontAskForBadResolution", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "Don’t warn about a resolution mismatch")}, {}},
    {"automation.scriptResolutionFromVideo", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "Set a missing script resolution from the video without asking")}, {}},
    {"video.untaggedMatrixWarning", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "Warn when the video has no colour matrix tag")}, {}},
    {"video.hdrWarning", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "Warn when the video has HDR or wide-gamut colours")}, {}},
    {"video.missingMatrix", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "No YCbCr matrix in the subtitles")}, {}},
    {"video.matrixMismatch", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "A different YCbCr matrix from the video’s")}, {}},
    {"video.missingLayoutRes", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "No layout resolution in the subtitles")}, {}},
    {"video.layoutResMismatch", "video.scriptResolution", {QT_TRANSLATE_NOOP("HikariSub.Options", "A different layout resolution from the video’s")}, {}},
    {"audio.outputDevice", "audio.output", {QT_TRANSLATE_NOOP("HikariSub.Options", "Device")}, {}},
    {"audio.outputHostApi", "audio.output", {QT_TRANSLATE_NOOP("HikariSub.Options", "API")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Applies when the output opens again.")}},
    {"audio.delay", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Audio delay")}, {}},
    {"audio.markPlayTime", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Play around the marker")}, {}},
    {"audio.leadInValue", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Lead-in")}, {}},
    {"audio.leadOutValue", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Lead-out")}, {}},
    {"audio.lockScrollOnCursor", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Follow audio during playback")}, {}},
    {"audio.dontPlayWhenLineChanges", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Don’t play after changing the line")}, {}},
    {"audio.autoFocus", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Focus the panel under the pointer")}, {}},
    {"audio.playMono", "audio.playback", {QT_TRANSLATE_NOOP("HikariSub.Options", "Play in mono")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Mixes the channels for the audio panel and editing playback. General video playback keeps the file’s channels.")}},
    {"audio.drawTimeCursor", "audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show the time beside the cursor")}, {}},
    {"audio.drawSecondaryLines", "audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show seconds markers")}, {}},
    {"audio.drawSelectionBackground", "audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show the selection background")}, {}},
    {"audio.drawVideoPosition", "audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show the video position")}, {}},
    {"audio.drawKeyframes", "audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show keyframes")}, {}},
    {"audio.lineBoundariesThickness", "audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Line boundaries thickness")}, {}},
    {"audio.inactiveLinesDisplayMode", "audio.display", {QT_TRANSLATE_NOOP("HikariSub.Options", "Inactive lines")}, {}},
    {"audio.snapToKeyframes", "audio.snapping", {QT_TRANSLATE_NOOP("HikariSub.Options", "Snap to keyframes")}, {}},
    {"audio.snapToOtherLines", "audio.snapping", {QT_TRANSLATE_NOOP("HikariSub.Options", "Snap to other lines")}, {}},
    {"audio.mergeEveryNWithSyllable", "audio.karaoke", {QT_TRANSLATE_NOOP("HikariSub.Options", "Merge every “n” with the previous syllable")}, {}},
    {"audio.karaokeMoveOnClick", "audio.karaoke", {QT_TRANSLATE_NOOP("HikariSub.Options", "Move the syllable line after a click")}, {}},
    {"audio.karaokeDoubleConsonantApart", "audio.karaoke", {QT_TRANSLATE_NOOP("HikariSub.Options", "Give a double consonant its own syllable (so|ma|t|ta)")}, {}},
    {"audio.karaokeThaiClusters", "audio.karaoke", {QT_TRANSLATE_NOOP("HikariSub.Options", "Split Thai into character clusters")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Auto split gives each Thai character cluster its own syllable, so กิน becomes กิ|น. Turn it off to keep each Thai word one syllable.")}},
    {"audio.karaokeKfThreshold", "audio.karaoke", {QT_TRANSLATE_NOOP("HikariSub.Options", "Long syllables for %1")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Switch long syllables between %1 and %2 changes the syllables at least this long.")}},
    {"audio.ramCache", "audio.cache", {QT_TRANSLATE_NOOP("HikariSub.Options", "Load audio into RAM")}, {}},
    {"audio.cacheFilesLimit", "audio.cache", {QT_TRANSLATE_NOOP("HikariSub.Options", "Cache files kept")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "0–10000. At 0, audio cache files are never removed.")}},
    {"convert.styleCatalog", "conversion.style", {QT_TRANSLATE_NOOP("HikariSub.Options", "Catalogue")}, {}},
    {"convert.style", "conversion.style", {QT_TRANSLATE_NOOP("HikariSub.Options", "Style")}, {}},
    {"convert.fpsFromVideo", "conversion.timing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Frame rate from the video")}, {}},
    {"convert.newEndTimes", "conversion.timing", {QT_TRANSLATE_NOOP("HikariSub.Options", "New end times")}, {}},
    {"convert.timePerCharacter", "conversion.timing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Time per letter")}, {}},
    {"convert.resolutionWidth", "conversion.ass", {QT_TRANSLATE_NOOP("HikariSub.Options", "Width")}, {}},
    {"convert.resolutionHeight", "conversion.ass", {QT_TRANSLATE_NOOP("HikariSub.Options", "Height")}, {}},
    {"convert.assTagsToInsertInLine", "conversion.ass", {QT_TRANSLATE_NOOP("HikariSub.Options", "Tags at the start of each line")}, {}},
    {"export.format", "conversion.export", {QT_TRANSLATE_NOOP("HikariSub.Project", "Export format"), "HikariSub.Project"}, {QT_TRANSLATE_NOOP("HikariSub.Project", "Used for files that haven’t been exported yet."), "HikariSub.Project"}},
    {"scriptProperties.askForChange", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.Project", "Ask before changing them on save"), "HikariSub.Project"}, {}},
    {"scriptProperties.title", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Title"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.titleOn", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Title"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.script", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Author"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.scriptOn", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Author"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.translation", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Translator"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.translationOn", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Translator"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.editing", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Proofreading"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.editingOn", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Proofreading"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.timing", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Timer"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.timingOn", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Timer"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.update", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Editing"), "HikariSub.ScriptProperties"}, {}},
    {"scriptProperties.updateOn", "subtitleProperties.credits", {QT_TRANSLATE_NOOP("HikariSub.ScriptProperties", "Editing"), "HikariSub.ScriptProperties"}, {}},
    {"subtitles.layoutInFile", "automation.files", {QT_TRANSLATE_NOOP("HikariSub.Options", "Save the panel layout in subtitle files")}, {}},
    {"automation.traceLevel", "automation.debugging", {QT_TRANSLATE_NOOP("HikariSub.Options", "Lua trace level")}, {}},
    {"automation.fillLastAnswers", "automation.scriptDialogs", {QT_TRANSLATE_NOOP("HikariSub.Options", "Fill in my last answers")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Script dialogs open with the answers you gave last time. A value the script works out itself, from the selection say, still wins. Off: they open with the script’s values, and ⋯ › Last used fills yours in.")}},
    {"ai.off", "ai.aiFeatures", {QT_TRANSLATE_NOOP("HikariSub.Options", "Turn off all AI features")}, {}},
    {"translation.translator", "ai.models", {QT_TRANSLATE_NOOP("HikariSub.Options", "Translator model")}, {}},
    {"translation.reviewer", "ai.models", {QT_TRANSLATE_NOOP("HikariSub.Options", "Reviewer model")}, {}},
    {"ai.webSearch", "ai.webSearch", {QT_TRANSLATE_NOOP("HikariSub.Options", "Let AI search the web")}, {}},
    {"ai.mcp.server", "ai.mcp", {QT_TRANSLATE_NOOP("HikariSub.Options", "Let agents outside HikariSub edit through MCP")}, {}},
    {"ai.mcp.http", "ai.mcp", {QT_TRANSLATE_NOOP("HikariSub.Options", "Also listen on http://127.0.0.1")}, {}},
    {"ai.mcp.skills", "ai.mcp", {QT_TRANSLATE_NOOP("HikariSub.Options", "Let them read your skills")}, {}},
    {"ai.agent.costMeter", "ai.agent", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show the cost meter")}, {}},
    {"ai.agent.autoCompact", "ai.agent", {QT_TRANSLATE_NOOP("HikariSub.Options", "Compact long conversations automatically")}, {}},
    {"ai.agent.threadCapCents", "ai.agent", {QT_TRANSLATE_NOOP("HikariSub.Options", "Cost cap per thread")}, {}},
    {"ai.agent.dailyCapCents", "ai.agent", {QT_TRANSLATE_NOOP("HikariSub.Options", "Cost cap per day")}, {}},
    {"ai.autoUpdate", "ai.aiComponents", {QT_TRANSLATE_NOOP("HikariSub.Options", "Update AI components automatically")}, {}},
    {"ai.promptCaching", "ai.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Use prompt caching")}, {}},
    {"ai.exclude", "ai.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Never send to AI")}, {}},
    {"updater.autoCheck", "updates.checking", {QT_TRANSLATE_NOOP("HikariSub.Options", "Check for updates automatically")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "At most once a day, when HikariSub starts.")}},
    {"updater.checkForStable", "updates.checking", {QT_TRANSLATE_NOOP("HikariSub.Options", "Stable versions only")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Off: prereleases (beta versions) are offered too.")}},
    {"updater.autoInstall", "updates.installing", {QT_TRANSLATE_NOOP("HikariSub.Options", "Install updates automatically")}, {}},
    {"plugins.checkUpdates", "updates.plugins", {QT_TRANSLATE_NOOP("HikariSub.Plugins", "Check for plugin updates"), "HikariSub.Plugins"}, {QT_TRANSLATE_NOOP("HikariSub.Plugins", "At start and once a day. HikariSub downloads the whole list and never sends which plugins you have."), "HikariSub.Plugins"}},
    {"plugins.autoUpdate", "updates.plugins", {QT_TRANSLATE_NOOP("HikariSub.Plugins", "Install updates automatically"), "HikariSub.Plugins"}, {QT_TRANSLATE_NOOP("HikariSub.Plugins", "Only for marketplace plugins, once they finish what they’re doing. An update that asks for more waits for you."), "HikariSub.Plugins"}},
    {"privacy.usageData", "updates.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Send anonymous usage data")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Which features are used, errors and crashes, and how fast things open. Never your files, their names or what is in them.")}},
    {"privacy.alwaysSendCrashReports", "updates.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Always send crash reports")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "After a crash or a freeze, the report is sent without asking: what crashed, your system, settings and the last Log lines. Never your subtitles, files or their names.")}},
    {"privacy.discordPresence", "updates.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Show what I am doing in Discord")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "Your Discord status shows Editing subtitles, the mode and for how long. Never the video, your subtitles or line counts.")}},
    {"privacy.discordFileName", "updates.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Include the file name")}, {}},
    {"plugins.network", "updates.privacy", {QT_TRANSLATE_NOOP("HikariSub.Options", "Let plugins connect to the internet")}, {QT_TRANSLATE_NOOP("HikariSub.Options", "HikariSub makes each request, over HTTPS and only to the hosts the plugin listed when you installed it. Off: HikariSub refuses every request a plugin makes through it. A Full trust plugin can still connect on its own.")}},
};

// Sections the Project tab shows without a catalogued projectable row.
constexpr std::string_view kProjectSections[] = {"spelling.ruleGroups", "ai.keys"};
// Sections only the Project tab shows.
constexpr std::string_view kProjectOnlySections[] = {"ai.keys"};

const Row *rowOf(const QString &setting)
{
    const QByteArray id = setting.toUtf8();
    for (const Row &row : kRows)
        if (id == row.setting)
            return &row;
    return nullptr;
}

const Section *sectionNamed(std::string_view id)
{
    for (const Section &section : kSections)
        if (id == section.id)
            return &section;
    return nullptr;
}

const Page *pageNamed(std::string_view id)
{
    for (const Page &page : kPages)
        if (id == page.id)
            return &page;
    return nullptr;
}

std::string_view pageIdOf(std::string_view section)
{
    return section.substr(0, section.find('.'));
}

bool isProjectable(std::string_view setting)
{
    const auto *definition = application::findSetting(setting);
    return definition && definition->projectable;
}

bool projectShows(std::string_view section)
{
    if (std::ranges::find(kProjectSections, section) != std::end(kProjectSections))
        return true;
    return std::ranges::any_of(kRows, [section](const Row &row) { return section == row.section && isProjectable(row.setting); });
}

bool yoursShows(std::string_view section)
{
    return std::ranges::find(kProjectOnlySections, section) == std::end(kProjectOnlySections);
}

bool hasRows(std::string_view section)
{
    return std::ranges::any_of(kRows, [section](const Row &row) { return section == row.section; });
}

struct Query {
    QStringList words;
    bool modified = false;
    QString plugin;

    bool matches(const QString &haystack) const
    {
        return std::ranges::all_of(words, [&haystack](const QString &word) { return haystack.contains(word, Qt::CaseInsensitive); });
    }
};

Query parse(const QString &text)
{
    Query query;
    for (const QString &word : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word == QLatin1String("@modified"))
            query.modified = true;
        else if (word.startsWith(QLatin1String("@plugin:")))
            query.plugin = word.mid(8);
        else
            query.words << word;
    }
    return query;
}

void count(QVariantMap &map, const QString &key)
{
    map.insert(key, map.value(key).toInt() + 1);
}

}

QString SettingsCatalogue::label(const QString &setting)
{
    const Row *row = rowOf(setting);
    return row ? row->label.translated() : QString();
}

QString SettingsCatalogue::description(const QString &setting)
{
    const Row *row = rowOf(setting);
    return row ? row->description.translated() : QString();
}

QString SettingsCatalogue::sectionOf(const QString &setting)
{
    if (setting.startsWith(QLatin1String("grammar.group.")))
        return QStringLiteral("spelling.ruleGroups");
    const Row *row = rowOf(setting);
    return row ? QLatin1String(row->section) : QString();
}

QString SettingsCatalogue::pageOf(const QString &setting)
{
    const QString section = sectionOf(setting);
    return section.left(section.indexOf(QLatin1Char('.')));
}

bool SettingsCatalogue::projectable(const QString &setting)
{
    return isProjectable(setting.toStdString());
}

QVariantList SettingsCatalogue::pages()
{
    QVariantList out;
    for (const Page &page : kPages) {
        QVariantList sections;
        for (const Section &section : kSections)
            if (pageIdOf(section.id) == page.id)
                sections << QVariantMap{{QStringLiteral("id"), QLatin1String(section.id)},
                                        {QStringLiteral("title"), section.title.translated()},
                                        {QStringLiteral("project"), projectShows(section.id)},
                                        {QStringLiteral("yours"), yoursShows(section.id)}};
        out << QVariantMap{{QStringLiteral("id"), QLatin1String(page.id)},
                           {QStringLiteral("name"), page.name.translated()},
                           {QStringLiteral("icon"), QLatin1String(page.icon)},
                           {QStringLiteral("sections"), sections}};
    }
    return out;
}

QString SettingsCatalogue::sectionTitle(const QString &section)
{
    const Section *found = sectionNamed(section.toStdString());
    return found ? found->title.translated() : QString();
}

QVariantMap SettingsCatalogue::search(const QString &text, const QVariantList &plugins, const QStringList &changed, bool project)
{
    const Query query = parse(text);
    QVariantMap settings, sections, pages;
    int total = 0;
    const auto found = [&](const QString &setting, const QString &section, const QString &page) {
        if (!setting.isEmpty())
            settings.insert(setting, true);
        count(sections, section);
        count(pages, page);
        ++total;
    };
    if (query.plugin.isEmpty()) {
        for (const Row &row : kRows) {
            const QString setting = QLatin1String(row.setting);
            if ((project && !isProjectable(row.setting)) || (!project && !yoursShows(row.section)) ||
                (query.modified && !changed.contains(setting)))
                continue;
            const Page *page = pageNamed(pageIdOf(row.section));
            const QString haystack = QStringList{row.label.translated(), row.description.translated(), setting,
                                                 sectionNamed(row.section)->title.translated(),
                                                 sectionNamed(row.section)->keywords.translated(), page->name.translated()}
                                         .join(QLatin1Char('\n'));
            if (query.matches(haystack))
                found(setting, QLatin1String(row.section), QLatin1String(page->id));
        }
        for (const Section &section : kSections) {
            if (query.modified || hasRows(section.id) || (project ? !projectShows(section.id) : !yoursShows(section.id)))
                continue;
            const Page *page = pageNamed(pageIdOf(section.id));
            const QString haystack = QStringList{section.title.translated(), section.keywords.translated(), page->name.translated()}
                                         .join(QLatin1Char('\n'));
            if (query.matches(haystack))
                found({}, QLatin1String(section.id), QLatin1String(page->id));
        }
    }
    for (const QVariant &entry : plugins) {
        const QVariantMap row = entry.toMap();
        const QString setting = row.value(QStringLiteral("setting")).toString();
        const QString plugin = row.value(QStringLiteral("plugin")).toString();
        if ((!query.plugin.isEmpty() && plugin != query.plugin) || (project && !row.value(QStringLiteral("projectable")).toBool()) ||
            (query.modified && !changed.contains(setting)))
            continue;
        const QString haystack = QStringList{row.value(QStringLiteral("label")).toString(), row.value(QStringLiteral("description")).toString(),
                                             row.value(QStringLiteral("keywords")).toStringList().join(QLatin1Char(' ')), setting,
                                             row.value(QStringLiteral("pluginName")).toString(),
                                             QCoreApplication::translate(kOptions, "Plugins")}
                                     .join(QLatin1Char('\n'));
        if (query.matches(haystack))
            found(setting, QStringLiteral("plugins.") + plugin, QStringLiteral("plugins"));
    }
    return {{QStringLiteral("settings"), settings}, {QStringLiteral("sections"), sections}, {QStringLiteral("pages"), pages},
            {QStringLiteral("count"), total}, {QStringLiteral("modified"), query.modified}, {QStringLiteral("plugin"), query.plugin}};
}

QStringList SettingsCatalogue::settings()
{
    QStringList out;
    for (const Row &row : kRows)
        out << QLatin1String(row.setting);
    return out;
}

}
