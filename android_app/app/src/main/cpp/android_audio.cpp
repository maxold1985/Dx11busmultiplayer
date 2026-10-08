#include "android_audio.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#define STB_VORBIS_HEADER_ONLY
#include "../../../../../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

namespace {
unsigned read16(const unsigned char* b) {
    return (unsigned)b[0] | ((unsigned)b[1]<<8);
}
unsigned read32(const unsigned char* b) {
    return read16(b) | (read16(b+2)<<16);
}
bool loadFile(const std::string& path,std::vector<unsigned char>& bytes) {
    std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);
    if(!f)return false;
    const std::streamoff n=f.tellg();
    if(n<24 || n>32*1024*1024)return false;
    bytes.resize((size_t)n);
    f.seekg(0,std::ios::beg);
    return !!f.read((char*)&bytes[0],n);
}
float clamp(float value,float minimum,float maximum) {
    return value<minimum?minimum:(value>maximum?maximum:value);
}
}

AndroidBusAudio::AndroidBusAudio():
    stream_(0),rpm_(700),throttle_(0),speed_(0),maximumRpm_(3000),
    motorPhase_(0),gear_(0),rate_(48000),cockpit_(false) {}

AndroidBusAudio::~AndroidBusAudio() {
    stop();
}
bool AndroidBusAudio::loadOgg(const std::vector<unsigned char>& data,Clip& clip) {
    int channels=0,rate=0;
    short* decoded=0;
    const int frames=stb_vorbis_decode_memory(
        &data[0],(int)data.size(),&channels,&rate,&decoded);
    if(frames<=1||decoded==0||channels<1||channels>8||rate<6000||rate>192000) {
        free(decoded);return false;
    }
    const size_t count=std::min((size_t)frames,(size_t)rate*MAX_SECONDS);
    clip.mono.resize(count);
    clip.rate=rate;
    for(size_t i=0;i<count;++i) {
        float sum=0;
        for(int c=0;c<channels;++c)
            sum+=(float)decoded[i*(size_t)channels+c]/32768.0f;
        clip.mono[i]=sum/(float)channels;
    }
    free(decoded);
    return true;
}
bool AndroidBusAudio::loadWav(const std::vector<unsigned char>& bytes,Clip& clip) {
    if(bytes.size()<44||memcmp(&bytes[0],"RIFF",4)!=0||
       memcmp(&bytes[8],"WAVE",4)!=0)return false;
    unsigned format=0,channels=0,rate=0,bits=0;
    size_t dataAt=0,dataLength=0;
    for(size_t pos=12;pos+8<=bytes.size();) {
        unsigned length=read32(&bytes[pos+4]);
        size_t start=pos+8;
        if(length>bytes.size()-start)break;
        if(memcmp(&bytes[pos],"fmt ",4)==0 && length>=16) {
            format=read16(&bytes[start]);
            channels=read16(&bytes[start+2]);
            rate=read32(&bytes[start+4]);
            bits=read16(&bytes[start+14]);
        }
        if(memcmp(&bytes[pos],"data",4)==0) {
            dataAt=start;dataLength=length;
        }
        size_t next=start+(size_t)length+(length&1);
        if(next<=pos)break;
        pos=next;
    }
    if(format!=1||channels<1||channels>8||rate<6000||rate>192000||
       bits!=16||!dataAt||dataLength<channels*2)return false;
    const size_t available=dataLength/(channels*2);
    const size_t count=std::min(available,(size_t)rate*MAX_SECONDS);
    clip.mono.resize(count);
    clip.rate=(int)rate;
    for(size_t i=0;i<count;++i) {
        float sum=0;
        for(unsigned c=0;c<channels;++c) {
            size_t at=dataAt+(i*channels+c)*2;
            int16_t pcm=(int16_t)read16(&bytes[at]);
            sum+=(float)pcm/32768.0f;
        }
        clip.mono[i]=sum/(float)channels;
    }
    return count>1;
}
bool AndroidBusAudio::loadClip(const std::string& path,Clip& clip) {
    std::vector<unsigned char> data;
    if(!loadFile(path,data))return false;
    const std::string lower=buscfg::lower(path);
    if(lower.size()<4)return false;
    const std::string ext=lower.substr(lower.size()-4);
    if(ext==".ogg")return loadOgg(data,clip);
    if(ext==".wav")return loadWav(data,clip);
    return false;
}
bool AndroidBusAudio::load(const buscfg::ModScripts& config) {
    // Stop callback before touching large vectors of decoded PCM.
    const bool wasRunning=stream_!=0;
    stop();
    std::vector<Track> loops,events;
    unsigned missing=0,bad=0,skipped=0;
    for(size_t i=0;i<config.sounds.size();++i) {
        if(loops.size()>=(size_t)MAX_LOOPS)break;
        const buscfg::SoundSpec& sound=config.sounds[i];
        const int number=atoi(sound.section.c_str()+5);
        if(number<1||number>18||sound.retarder||sound.transmission) {
            ++skipped;continue;
        }
        const std::string name=buscfg::lower(sound.file);
        if(name.find("freio")!=std::string::npos||
           name.find("brake")!=std::string::npos||
           name.find("diferencial")!=std::string::npos||
           name.find("partida")!=std::string::npos||
           name.find("retarder")!=std::string::npos) {
            ++skipped;continue;
        }
        Track t;t.spec=sound;
        if(t.spec.volumeMultiplier<0.001f) {
            if(number>=1&&number<=3)t.spec.volumeMultiplier=0.45f;
            else {++skipped;continue;}
        }
        const std::string path=config.locateSound(t.spec);
        if(path.empty()) {++missing;continue;}
        if(!loadClip(path,t.clip)) {++bad;continue;}
        loops.push_back(t);
    }
    for(size_t i=0;i<config.eventSounds.size();++i) {
        if(events.size()>=(size_t)MAX_EVENTS)break;
        Track t;t.spec=config.eventSounds[i].sound;
        const std::string path=config.locateSound(t.spec);
        if(path.empty()) {++missing;continue;}
        if(!loadClip(path,t.clip)) {++bad;continue;}
        events.push_back(t);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loops_.swap(loops);
        events_.swap(events);
        maximumRpm_=config.soundMaxRpm>100?config.soundMaxRpm:3000;
        std::ostringstream report;
        report<<"Audio Android: "<<loops_.size()<<" loops, "<<events_.size()
              <<" eventos; "<<missing<<" ausentes; "<<bad
              <<" arquivos invalidos; "<<skipped<<" ignorados.";
        report_=report.str();
    }
    if(wasRunning)start();
    return !loops_.empty()||!events_.empty();
}
bool AndroidBusAudio::start() {
    if(stream_)return true;
    AAudioStreamBuilder* builder=0;
    if(AAudio_createStreamBuilder(&builder)!=AAUDIO_OK || !builder)return false;
    AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder,1);
    AAudioStreamBuilder_setDataCallback(builder,callback,this);
    AAudioStream* stream=0;
    const aaudio_result_t result=AAudioStreamBuilder_openStream(builder,&stream);
    AAudioStreamBuilder_delete(builder);
    if(result!=AAUDIO_OK||!stream)return false;
    stream_=stream;
    rate_=AAudioStream_getSampleRate(stream_);
    if(rate_<8000)rate_=48000;
    if(AAudioStream_requestStart(stream_)!=AAUDIO_OK) {
        stop();return false;
    }
    return true;
}
void AndroidBusAudio::stop() {
    if(!stream_)return;
    AAudioStream* stream=stream_;
    stream_=0;
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
}
bool AndroidBusAudio::cameraMatches(const buscfg::SoundSpec& spec) const {
    if(spec.whenToPlay=="externalcam")return !cockpit_;
    if(spec.whenToPlay=="drivercam"||spec.whenToPlay=="passengercam"||
       spec.whenToPlay=="internalcam")return cockpit_;
    return true;
}
void AndroidBusAudio::update(float rpm,float throttle,float speed,int gear,bool cockpitView) {
    std::lock_guard<std::mutex> lock(mutex_);
    rpm_=clamp(rpm,0,8000);
    throttle_=clamp(throttle,-1,1);
    speed_=speed;
    gear_=gear;
    cockpit_=cockpitView;
}
bool AndroidBusAudio::trigger(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key=buscfg::lower(name);
    for(size_t i=0;i<events_.size();++i) {
        if(events_[i].spec.section==key) {
            events_[i].cursor=0;
            events_[i].playing=true;
            return true;
        }
    }
    return false;
}
std::string AndroidBusAudio::report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return report_;
}
aaudio_data_callback_result_t AndroidBusAudio::callback(
    AAudioStream*,void* user,void* output,int32_t frames) {
    static_cast<AndroidBusAudio*>(user)->mix((int16_t*)output,frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void AndroidBusAudio::mix(int16_t* output,int32_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    const float normal=clamp(rpm_/std::max(1.0f,maximumRpm_),0,1);
    for(int32_t frame=0;frame<frames;++frame) {
        float mixed=0;
        if(loops_.empty()) {
            // Offline / missing samples: portable diesel synthesis.
            const float freq=20.0f+std::max(rpm_,650.0f)/60.0f*2.5f;
            motorPhase_+=6.283185307179586f*freq/rate_;
            if(motorPhase_>6.283185307179586f)
                motorPhase_-=6.283185307179586f;
            mixed=(0.52f*sinf(motorPhase_)+0.20f*sinf(motorPhase_*2.0f)+
                   0.10f*sinf(motorPhase_*4.0f))*0.17f;
        }
        for(size_t i=0;i<loops_.size();++i) {
            Track& track=loops_[i];
            const size_t count=track.clip.mono.size();
            if(count<2)continue;
            float volume=track.spec.volume(normal);
            if(!cameraMatches(track.spec))volume=0;
            if(track.spec.acceleratingOnly&&throttle_<0.05f)volume=0;
            if(track.spec.selectedGears &&
               (gear_<track.spec.minGear||gear_>track.spec.maxGear))volume=0;
            if(track.spec.retarder&&throttle_>=-0.01f&&fabsf(speed_)<1)volume=0;
            const size_t a=(size_t)track.cursor;
            const size_t b=(a+1)%count;
            float frac=(float)(track.cursor-a);
            mixed+=(track.clip.mono[a]+
                    (track.clip.mono[b]-track.clip.mono[a])*frac)*volume*0.3f;
            track.cursor+=track.clip.rate*track.spec.pitch(normal)/(double)rate_;
            if(track.cursor>=count)
                track.cursor=fmod(track.cursor,(double)count);
        }
        for(size_t i=0;i<events_.size();++i) {
            Track& track=events_[i];
            if(!track.playing)continue;
            const size_t count=track.clip.mono.size();
            const size_t a=(size_t)track.cursor;
            if(count<2||a>=count) {track.playing=false;continue;}
            const size_t b=std::min(a+1,count-1);
            const float frac=(float)(track.cursor-a);
            if(cameraMatches(track.spec)) {
                mixed+=(track.clip.mono[a]+(track.clip.mono[b]-
                         track.clip.mono[a])*frac)*track.spec.volumeMultiplier*0.3f;
            }
            track.cursor+=track.clip.rate*track.spec.pitchMultiplier/(double)rate_;
            if(track.cursor>=count)track.playing=false;
        }
        mixed=clamp(mixed,-1,1);
        output[frame]=(int16_t)(mixed*28000.0f);
    }
}
