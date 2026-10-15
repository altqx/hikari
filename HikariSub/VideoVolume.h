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

#pragma once

#include <cstdlib>

// Whether the fullscreen video panel shows its volume slider. It did only for
// DirectShow (and GStreamer, its stand-in on Linux); a video FFMS2 plays had
// only the mouse wheel over the panel. It is shown whatever the player.
inline bool FullscreenShowsVolumeSlider(bool isDirectShow)
{
	(void)isDirectShow;
	return true;
}

// The video volume sliders (VolSlider) go from -86 to 0, and the volume they
// set is -(value * value) hundredths of a dB (VideoBox::OnVolume). A video
// FFMS2 plays has the audio box's audio, whose volume bar goes from 1 to 100:
// RendererFFMS2::SetVolume turns the hundredths of a dB into that volume.
inline int AudioVolumeFromVideoVolume(int hundredthsOfDb)
{
	double volume = (7600 + hundredthsOfDb) / 7600.0;
	return (int)(volume * 99) + 1;
}

// The video volume slider's value for the audio box's volume, so the
// fullscreen slider of a video FFMS2 plays shows the volume it sets: the
// value whose volume is the nearest, the louder of two as near.
inline int VideoSliderFromAudioVolume(int audioVolume)
{
	int best = 0;
	int bestDistance = -1;
	for (int value = 0; value >= -86; value--) {
		int distance = std::abs(AudioVolumeFromVideoVolume(-(value * value)) - audioVolume);
		if (bestDistance < 0 || distance < bestDistance) {
			best = value;
			bestDistance = distance;
		}
	}
	return best;
}
