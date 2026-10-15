//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.


#include "check.h"
#include "../HikariSub/VideoVolume.h"

TEST(fullscreen_shows_the_volume_slider_with_every_player)
{
	CHECK(FullscreenShowsVolumeSlider(true)); // DirectShow, GStreamer on Linux
	CHECK(FullscreenShowsVolumeSlider(false)); // FFMS2
}

TEST(video_volume_slider_sets_the_ffms2_audio_volume)
{
	// full volume and the quietest the slider goes, as RendererFFMS2 set them
	CHECK_EQ(AudioVolumeFromVideoVolume(0), 100);
	CHECK_EQ(AudioVolumeFromVideoVolume(-(86 * 86)), 3);
	// louder never gives a lower audio volume
	for (int value = -85; value <= 0; value++)
		CHECK(AudioVolumeFromVideoVolume(-(value * value)) >= AudioVolumeFromVideoVolume(-((value - 1) * (value - 1))));
}

TEST(video_volume_slider_follows_the_ffms2_audio_volume)
{
	CHECK_EQ(VideoSliderFromAudioVolume(100), 0);
	CHECK_EQ(VideoSliderFromAudioVolume(1), -86);
	CHECK_EQ(VideoSliderFromAudioVolume(3), -86);
	// the slider shows each volume it can set as that volume, so it and the
	// audio box's volume bar stay together
	for (int value = -86; value <= 0; value++) {
		int volume = AudioVolumeFromVideoVolume(-(value * value));
		int shown = VideoSliderFromAudioVolume(volume);
		CHECK(shown >= -86 && shown <= 0);
		CHECK_EQ(AudioVolumeFromVideoVolume(-(shown * shown)), volume);
	}
	// a volume the slider cannot set exactly shows the nearest
	for (int volume = 1; volume <= 100; volume++) {
		int shown = VideoSliderFromAudioVolume(volume);
		CHECK(std::abs(AudioVolumeFromVideoVolume(-(shown * shown)) - volume) <= 2);
	}
}
