#pragma once

#include <QString>

namespace hikari::backends {

/// How writing a secret for the current user alone ended: `NotPrivate` when its folder can't be made theirs alone, as
/// on a drive without permissions; `Failed` for anything else.
enum class SecretWrite { Saved, NotPrivate, Failed };

/// True when only the current user, and on Windows the app's own AppContainers, may open `path`: on Unix no group or
/// other permission bit is set; on Windows no allow entry of its ACL names anyone else.
bool isForUserAlone(const QString &path);

/// Gives `path`, a file or folder, to the current user alone: mode 0600 or 0700 on Unix; on Windows a protected ACL
/// with one entry for the user, which a folder's new files and folders inherit.
bool keepForUserAlone(const QString &path);

}
