pragma Singleton
import QtQuick

QtObject {
    property QtObject hotkeys: null

    function windowOf(command: string): int {
        if (command.startsWith("GRID_"))
            return 1
        if (command.startsWith("EDITBOX_"))
            return 2
        if (command.startsWith("VIDEO_"))
            return 3
        if (command.startsWith("AUDIO_"))
            return 4
        return 0
    }
    function text(command: string, window: int): string {
        return hotkeys && command.length > 0 && hotkeys.keys ? hotkeys.accelOf(command, window) : ""
    }
}
