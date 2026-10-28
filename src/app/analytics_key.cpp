#include "hikari/app/analytics.h"

// AN1 (#404): only this file carries the key (-DHIKARI_POSTHOG_KEY=…, the
// release workflows' secret), so a key changes one object.
QString hikari::app::Analytics::compiledKey()
{
    return QStringLiteral(HIKARI_POSTHOG_KEY);
}
