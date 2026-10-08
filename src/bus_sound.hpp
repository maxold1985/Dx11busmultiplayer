#pragma once
// WinMM PCM mixer for the [soundN] engine layers in Motor.txt.
// OGG decoding uses vendored public-domain stb_vorbis.
// Missing audio files never prevent the bus or the built-in motor from running.
#include "bus_script.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

class BusSoundPlayer {
private:
	static const int OUTPUT_RATE = 22050;
	static const int CHUNK = 2048;
	static const int BUFFER_COUNT = 4;
	static const int MAX_ENGINE_LAYERS = 12;

	struct Clip {
		std::vector<float> mono;
		int sampleRate;

		Clip() : sampleRate(OUTPUT_RATE) {}
	};

	struct Track {
		buscfg::SoundSpec spec;
		Clip clip;
		double position;

		Track() : position(0.0) {}
	};

	HWAVEOUT output;
	WAVEHDR headers[BUFFER_COUNT];
	short pcm[BUFFER_COUNT][CHUNK];
	std::vector<Track> tracks;
	float currentRpm;
	float currentThrottle;
	float currentSpeed;
	float maximumRpm;
	int currentGear;
	bool cockpit;
	std::string reportMessage;

	static unsigned read16(const unsigned char* data) {
		return (unsigned)data[0] | ((unsigned)data[1] << 8);
	}

	static unsigned read32(const unsigned char* data) {
		return
			(unsigned)data[0] |
			((unsigned)data[1] << 8) |
			((unsigned)data[2] << 16) |
			((unsigned)data[3] << 24);
	}

	static bool loadBinary(const std::string& path, std::vector<unsigned char>& out) {
		std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);

		if(!stream) {
			return false;
		}

		const std::streamoff size = stream.tellg();

		if(size < 44 || size > 32 * 1024 * 1024) {
			return false;
		}

		out.resize((size_t)size);
		stream.seekg(0, std::ios::beg);
		return !!stream.read((char*)&out[0], size);
	}

	static bool readWav(
		const std::vector<unsigned char>& data,
		Clip& clip
	) {
		if(data.size() < 44 ||
			memcmp(&data[0], "RIFF", 4) != 0 ||
			memcmp(&data[8], "WAVE", 4) != 0) {
			return false;
		}

		unsigned channels = 0;
		unsigned rate = 0;
		unsigned bits = 0;
		unsigned format = 0;
		size_t sampleStart = 0;
		size_t sampleSize = 0;
		size_t cursor = 12;

		while(cursor + 8 <= data.size()) {
			const unsigned length = read32(&data[cursor + 4]);
			const size_t start = cursor + 8;

			if(length > data.size() - start) {
				return false;
			}

			if(memcmp(&data[cursor], "fmt ", 4) == 0 && length >= 16) {
				format = read16(&data[start]);
				channels = read16(&data[start + 2]);
				rate = read32(&data[start + 4]);
				bits = read16(&data[start + 14]);
			}

			if(memcmp(&data[cursor], "data", 4) == 0) {
				sampleStart = start;
				sampleSize = length;
			}

			const size_t next = start + (size_t)length + (length & 1u);

			if(next <= cursor || next > data.size() + 1) {
				return false;
			}

			cursor = next;
		}

		if(format != 1 ||
			(channels != 1 && channels != 2) ||
			(rate < 6000 || rate > 192000) ||
			(bits != 8 && bits != 16) ||
			sampleSize == 0) {
			return false;
		}

		const size_t frameBytes = channels * (bits / 8);
		const size_t available = sampleSize / frameBytes;
		const size_t maximum = (size_t)rate * 20;
		const size_t frames = std::min(available, maximum);

		if(frames == 0) {
			return false;
		}

		clip.sampleRate = (int)rate;
		clip.mono.resize(frames);

		for(size_t i = 0; i < frames; ++i) {
			float sample = 0.0f;

			for(size_t channel = 0; channel < channels; ++channel) {
				const size_t offset = sampleStart + i * frameBytes +
					channel * (bits / 8);

				if(bits == 16) {
					const short value = (short)read16(&data[offset]);
					sample += (float)value / 32768.0f;
				} else {
					sample += ((float)data[offset] - 128.0f) / 128.0f;
				}
			}

			clip.mono[i] = sample / (float)channels;
		}

		return true;
	}

	static bool readOgg(
		const std::vector<unsigned char>& data,
		Clip& clip
	) {
		int channels = 0;
		int sampleRate = 0;
		short* decoded = 0;

		const int frames = stb_vorbis_decode_memory(
			&data[0],
			(int)data.size(),
			&channels,
			&sampleRate,
			&decoded
		);

		if(frames <= 0 ||
			decoded == 0 ||
			channels < 1 ||
			channels > 8 ||
			sampleRate < 6000 ||
			sampleRate > 192000) {
			free(decoded);
			return false;
		}

		const size_t allowed = std::min(
			(size_t)frames,
			(size_t)sampleRate * 20
		);

		clip.sampleRate = sampleRate;
		clip.mono.resize(allowed);

		for(size_t i = 0; i < allowed; ++i) {
			float sample = 0.0f;

			for(int channel = 0; channel < channels; ++channel) {
				sample += (float)decoded[i * channels + channel] / 32768.0f;
			}

			clip.mono[i] = sample / (float)channels;
		}

		free(decoded);
		return true;
	}

	static bool loadClip(const std::string& path, Clip& clip) {
		std::vector<unsigned char> bytes;

		if(!loadBinary(path, bytes)) {
			return false;
		}

		const std::string extension = buscfg::lower(path);

		if(extension.size() >= 4 &&
			extension.substr(extension.size() - 4) == ".wav") {
			return readWav(bytes, clip);
		}

		if(extension.size() >= 4 &&
			extension.substr(extension.size() - 4) == ".ogg") {
			return readOgg(bytes, clip);
		}

		return false;
	}

	bool passesCamera(const buscfg::SoundSpec& spec) const {
		if(spec.whenToPlay == "externalcam") {
			return !cockpit;
		}

		if(spec.whenToPlay == "drivercam" ||
			spec.whenToPlay == "passengercam" ||
			spec.whenToPlay == "internalcam") {
			return cockpit;
		}

		return true;
	}

	void fill(int bufferIndex) {
		const float normalized = buscfg::bounded(
			currentRpm / std::max(1.0f, maximumRpm),
			0.0f,
			1.0f
		);

		for(int sampleIndex = 0; sampleIndex < CHUNK; ++sampleIndex) {
			float mixed = 0.0f;

			for(size_t i = 0; i < tracks.size(); ++i) {
				Track& track = tracks[i];
				const size_t frames = track.clip.mono.size();

				if(frames < 2) {
					continue;
				}

				float volume = track.spec.volume(normalized);

				if(!passesCamera(track.spec)) {
					volume = 0.0f;
				}

				if(track.spec.acceleratingOnly && currentThrottle <= 0.05f) {
					volume = 0.0f;
				}

				if(track.spec.selectedGears &&
					(currentGear < track.spec.minGear ||
					currentGear > track.spec.maxGear)) {
					volume = 0.0f;
				}

				if(track.spec.retarder && currentThrottle >= -0.01f &&
					std::fabs(currentSpeed) < 1.0f) {
					volume = 0.0f;
				}

				const float pitch = track.spec.pitch(normalized);
				const double frame = track.position;
				const size_t pos = (size_t)frame;
				const size_t next = (pos + 1) % frames;
				const float fraction = (float)(frame - pos);

				const float a = track.clip.mono[pos];
				const float b = track.clip.mono[next];
				mixed += (a + (b - a) * fraction) * volume * 0.30f;

				track.position +=
					(double)track.clip.sampleRate * pitch / OUTPUT_RATE;

				if(track.position >= frames) {
					track.position = std::fmod(track.position, (double)frames);
				}
			}

			mixed = buscfg::bounded(mixed, -1.0f, 1.0f);
			pcm[bufferIndex][sampleIndex] = (short)(mixed * 28000.0f);
		}
	}

public:
	BusSoundPlayer() :
		output(0),
		currentRpm(700.0f),
		currentThrottle(0.0f),
		currentSpeed(0.0f),
		maximumRpm(3000.0f),
		currentGear(1),
		cockpit(false) {
		memset(headers, 0, sizeof(headers));
	}

	bool load(const buscfg::ModScripts& scripts) {
		stop();
		tracks.clear();
		reportMessage.clear();

		maximumRpm = scripts.soundMaxRpm;

		int missing = 0;
		int failed = 0;
		std::string details;

		for(size_t i = 0; i < scripts.sounds.size(); ++i) {
			if(tracks.size() >= MAX_ENGINE_LAYERS) {
				break;
			}

			const buscfg::SoundSpec& sound = scripts.sounds[i];

			// Only the first engine/gearbox layers are looping samples.
			// Later [soundN] entries often represent doors, buttons, horns,
			// and other events and must not loop continuously.
			const int number = atoi(sound.section.c_str() + 5);
			if(number < 1 || number > 18) {
				continue;
			}

			// This Marcopolo mod ships its first three interior motor layers
			// muted (volumeMultiplier=0). Enable a conservative preview mix
			// without changing the original Motor.txt on disk.
			buscfg::SoundSpec activeSound = sound;
			if(activeSound.volumeMultiplier < 0.001f) {
				const int sectionNumber = atoi(sound.section.c_str() + 5);
				if(sectionNumber >= 1 && sectionNumber <= 3) {
					activeSound.volumeMultiplier = 0.45f;
				} else {
					continue;
				}
			}

			const std::string file = scripts.locateSound(activeSound);

			if(file.empty()) {
				++missing;
				details += "MISSING [" + sound.section + "] " + sound.file + "\\n";
				continue;
			}

			Track track;
			track.spec = activeSound;

			if(!loadClip(file, track.clip)) {
				++failed;
				details += "DECODE FAILED [" + sound.section + "] " + file + "\n";
				continue;
			}

			char sampleInfo[96];
			sprintf(sampleInfo, " (%u frames, %d Hz)\n",
				(unsigned)track.clip.mono.size(), track.clip.sampleRate);
			details += "LOADED [" + sound.section + "] " + file + sampleInfo;
			tracks.push_back(track);
		}

		char message[320];
		sprintf(
			message,
			"Engine layers decoded: %u / %u; missing files: %d; "
			"unsupported/corrupt: %d. WAV PCM and OGG Vorbis supported.",
			(unsigned)tracks.size(),
			(unsigned)scripts.sounds.size(),
			missing,
			failed
		);

		reportMessage = message;
		reportMessage += "\n" + details;
		return !tracks.empty();
	}

	bool start() {
		if(output != 0) {
			return true;
		}

		if(tracks.empty()) {
			return false;
		}

		WAVEFORMATEX format = {};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 1;
		format.nSamplesPerSec = OUTPUT_RATE;
		format.wBitsPerSample = 16;
		format.nBlockAlign = sizeof(short);
		format.nAvgBytesPerSec = OUTPUT_RATE * sizeof(short);

		if(waveOutOpen(
			&output,
			WAVE_MAPPER,
			&format,
			0,
			0,
			CALLBACK_NULL
		) != MMSYSERR_NOERROR) {
			output = 0;
			return false;
		}

		for(int i = 0; i < BUFFER_COUNT; ++i) {
			memset(&headers[i], 0, sizeof(WAVEHDR));
			headers[i].lpData = (LPSTR)pcm[i];
			headers[i].dwBufferLength = sizeof(pcm[i]);
			fill(i);

			if(waveOutPrepareHeader(
				output,
				&headers[i],
				sizeof(WAVEHDR)
			) != MMSYSERR_NOERROR) {
				stop();
				return false;
			}

			if(waveOutWrite(
				output,
				&headers[i],
				sizeof(WAVEHDR)
			) != MMSYSERR_NOERROR) {
				stop();
				return false;
			}
		}

		return true;
	}

	void update(
		float rpm,
		int gear,
		float throttle,
		float speed,
		bool cockpitView
	) {
		currentRpm = std::max(0.0f, rpm);
		currentGear = gear;
		currentThrottle = throttle;
		currentSpeed = speed;
		cockpit = cockpitView;

		if(output == 0) {
			return;
		}

		for(int i = 0; i < BUFFER_COUNT; ++i) {
			if((headers[i].dwFlags & WHDR_DONE) != 0) {
				fill(i);
				headers[i].dwFlags &= ~WHDR_DONE;

				waveOutWrite(
					output,
					&headers[i],
					sizeof(WAVEHDR)
				);
			}
		}
	}

	void stop() {
		if(output == 0) {
			return;
		}

		waveOutReset(output);

		for(int i = 0; i < BUFFER_COUNT; ++i) {
			if((headers[i].dwFlags & WHDR_PREPARED) != 0) {
				waveOutUnprepareHeader(
					output,
					&headers[i],
					sizeof(WAVEHDR)
				);
			}
		}

		waveOutClose(output);
		output = 0;
	}

	bool hasSounds() const {
		return !tracks.empty();
	}

	bool playing() const {
		return output != 0;
	}

	const std::string& report() const {
		return reportMessage;
	}

	~BusSoundPlayer() {
		stop();
	}
};
