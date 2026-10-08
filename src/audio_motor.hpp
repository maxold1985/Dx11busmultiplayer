#pragma once
// Sintese PCM de motor diesel, sem arquivos externos; WinMM.
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
class MotorAudio {
    static const int RATE=22050, CHUNK=2048, BUFFERS=4;
    HWAVEOUT output;
    WAVEHDR headers[BUFFERS];
    short samples[BUFFERS][CHUNK];
    double phase;
    float currentRpm;
    void fill(int i) {
        const float freq=20.0f+currentRpm/60.0f*2.5f;
        for(int n=0;n<CHUNK;n++) {
            phase+=6.283185307179586*freq/RATE;
            if(phase>6.283185307179586)phase-=6.283185307179586;
            float t=(float)phase;
            float sound=0.52f*sinf(t)+0.20f*sinf(t*2.0f)+0.10f*sinf(t*4.0f);
            samples[i][n]=(short)(sound*5800.0f);
        }
    }
public:
    MotorAudio():output(0),phase(0),currentRpm(700) {memset(headers,0,sizeof(headers));}
    bool start() {
        WAVEFORMATEX format={};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;
        format.nSamplesPerSec=RATE;format.wBitsPerSample=16;
        format.nBlockAlign=2;format.nAvgBytesPerSec=RATE*2;
        if(waveOutOpen(&output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) {output=0;return false;}
        for(int i=0;i<BUFFERS;i++) {
            headers[i].lpData=(LPSTR)samples[i]; headers[i].dwBufferLength=sizeof(samples[i]);
            fill(i);
            if(waveOutPrepareHeader(output,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR) {stop();return false;}
            if(waveOutWrite(output,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR) {stop();return false;}
        }
        return true;
    }
    void update(float rpm) {
        if(!output)return;
        currentRpm=rpm;
        for(int i=0;i<BUFFERS;i++)if(headers[i].dwFlags&WHDR_DONE) {
            fill(i);
            headers[i].dwFlags&=~WHDR_DONE;
            waveOutWrite(output,&headers[i],sizeof(WAVEHDR));
        }
    }
    void stop() {
        if(!output)return;
        waveOutReset(output);
        for(int i=0;i<BUFFERS;i++)if(headers[i].dwFlags&WHDR_PREPARED)
            waveOutUnprepareHeader(output,&headers[i],sizeof(WAVEHDR));
        waveOutClose(output);output=0;
    }
    ~MotorAudio(){stop();}
};
