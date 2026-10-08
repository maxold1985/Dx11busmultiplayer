#pragma once
// Android AAudio engine. Uses same OMSI sound scripts and stb_vorbis as Windows.
#include "bus_script.hpp"
#include <aaudio/AAudio.h>
#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>

class AndroidBusAudio {
public:
    AndroidBusAudio();
    ~AndroidBusAudio();
    bool start();
    void stop();
    bool load(const buscfg::ModScripts& config);
    bool trigger(const std::string& eventName);
    void update(float rpm,float throttle,float speed,int gear,bool cockpitView);
    std::string report() const;
private:
    struct Clip {
        std::vector<float> mono;
        int rate;
        Clip():rate(22050){}
    };
    struct Track {
        buscfg::SoundSpec spec;
        Clip clip;
        double cursor;
        bool playing;
        Track():cursor(0),playing(false){}
    };
    static const int MAX_LOOPS=12;
    static const int MAX_EVENTS=48;
    static const int MAX_SECONDS=15;

    AAudioStream* stream_;
    mutable std::mutex mutex_;
    std::vector<Track> loops_;
    std::vector<Track> events_;
    std::string report_;
    float rpm_,throttle_,speed_,maximumRpm_,motorPhase_;
    int gear_,rate_;
    bool cockpit_;
    static aaudio_data_callback_result_t callback(
        AAudioStream* stream,void* user,void* output,int32_t frames);
    void mix(int16_t* output,int32_t frames);
    bool cameraMatches(const buscfg::SoundSpec& spec) const;
    static bool loadClip(const std::string& path,Clip& clip);
    static bool loadOgg(const std::vector<unsigned char>& data,Clip& clip);
    static bool loadWav(const std::vector<unsigned char>& data,Clip& clip);
};
