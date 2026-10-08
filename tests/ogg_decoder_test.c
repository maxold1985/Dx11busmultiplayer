// Portable decoder smoke test; requires no real copyrighted audio assets.
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int main(void) {
	const unsigned char invalidOgg[] = {
		0x4e, 0x6f, 0x74, 0x20,
		0x4f, 0x67, 0x67, 0x21
	};
	int channels = 0;
	int sampleRate = 0;
	short* samples = NULL;

	const int frames = stb_vorbis_decode_memory(
		invalidOgg,
		(int)sizeof(invalidOgg),
		&channels,
		&sampleRate,
		&samples
	);

	assert(frames <= 0);
	free(samples);
	puts("stb_vorbis decoder linked and invalid input rejected");
	return 0;
}
