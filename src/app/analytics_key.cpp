#include "hikari/app/analytics.h"

QString hikari::app::Analytics::compiledKey()
{
    return QStringLiteral(HIKARI_POSTHOG_KEY);
}
