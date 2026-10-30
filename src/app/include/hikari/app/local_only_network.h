#pragma once

class QQmlEngine;

namespace hikari::app {

// Makes `engine` fail every request that would leave the machine (an Image source, a rich-text
// <img>, XMLHttpRequest): only file:, qrc: and data: URLs load. Install before loading QML.
void refuseRemoteUrls(QQmlEngine &engine);

}
