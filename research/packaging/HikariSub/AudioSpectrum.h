//  Copyright (c) 2016-2026, Marcin Drob

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

//this code piervously was taken from Aegisub 2 it's rewritten by me almost all.

#pragma once

//#include <wx/wxprec.h>
//#include <stdint.h>
#include "GFFT/GFFT.h"
#include "Provider.h"


class SpectrumCache;
class AudioSpectrumMultiThreading;

typedef std::vector<float> CacheLine;


class AudioSpectrum {
	friend class SpectrumThread;
private:

	// Colour pallettes
	unsigned char palette[256 * 3];

	Provider *provider;
	//unsigned int fft_overlaps; // number of overlaps used in FFT
	float power_scale; // amplification of displayed power
	int minband; // smallest frequency band displayed
	int maxband; // largest frequency band displayed
	bool nonlinear = true;
	wxCriticalSection CritSec;
	void SetupSpectrum(int overlaps = 1);
	std::vector<SpectrumCache*> sub_caches;
	AudioSpectrumMultiThreading *AudioThreads;
public:
	AudioSpectrum(Provider *_provider);
	~AudioSpectrum();
	
	void RenderRange(long long range_start, long long range_end, unsigned char *img, int imgwidth, int imgpitch, int imgheight, int percent);
	void CreateRange(std::vector<int> &output, std::vector<int> &intensities, long long timeStart, long long timeEnd, wxPoint frequency, int peek);
	void SetScaling(float _power_scale);
	void ChangeColours();
	void SetNonLinear(bool _nonlinear){ nonlinear = _nonlinear; }
};

class AudioSpectrumMultiThreading
{
public:
	AudioSpectrumMultiThreading(Provider *provider, std::vector<SpectrumCache*> *_sub_caches);
	~AudioSpectrumMultiThreading();
	//void SetCache(unsigned int _overlaps){overlaps = _overlaps; }
	void CreateCache(unsigned long _start, unsigned long _end);
	unsigned long start=0, end=0, lastCachePosition = 0;
	void FindCache(unsigned long _start, unsigned long _end);
	int numThreads;
private:
	static unsigned int __stdcall AudioProc(void* cls);
	void AudioPorocessing(int numOfTread);
	void SetAudio(unsigned long start, int len, FFT *fft);
	std::vector<SpectrumCache*> *sub_caches;
	unsigned long len;
	//unsigned int overlaps;
	FFT *ffttable = nullptr;
	HANDLE *threads=nullptr;
	HANDLE *eventCacheCopleted = nullptr;
	HANDLE *eventMakeCache = nullptr;
	HANDLE eventKillSelf;
	static AudioSpectrumMultiThreading *sthread;
};

