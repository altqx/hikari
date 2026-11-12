#include "hikari/app/application.h"

namespace hikari::app {

void Application::takePlayback(WindowMedia &media)
{
    ensureAudioRoute();
    for (auto *other : allMedia()) {
        if (other == &media)
            continue;
        if (other->video->playing())
            other->video->pause();
        if (other->audio->playing())
            other->audio->stopPlayback();
    }
    giveDirectShow(media);
}

void Application::stopMedia(WindowMedia &media)
{
    if (media.video->playing())
        media.video->pause();
    if (media.audio->playing())
        media.audio->stopPlayback();
    if (m_directShowMedia == &media)
        useOwnPlayer(media);
}

}
