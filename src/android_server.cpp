// Android authoritative UDP server, compatible with Windows BUS4 clients.
#include "android_net.hpp"
#include "simulation.hpp"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

struct AndroidPlayer {
    bool active;
    sockaddr_in address;
    sim::Dynamics physics;
    uint64_t lastSeen;
    AndroidPlayer() : active(false), lastSeen(0) { memset(&address, 0, sizeof(address)); }
};

int main() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) { perror("socket"); return 1; }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(BUS_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(fd, (sockaddr*)&address, sizeof(address)) < 0 || !androidnet::nonblocking(fd)) {
        perror("bind/nonblocking");
        close(fd);
        return 1;
    }

    AndroidPlayer players[MAX_PLAYERS];
    sim::Dynamics traffic[AI_BUSES];
    buscfg::ModScripts scripts;
    const char* modPath=getenv("DX11BUS_MOD_CONFIG");
    const bool customTransmission=modPath && *modPath &&
        scripts.load(modPath) &&
        (scripts.hasAutomatic || scripts.hasManual);
    if(customTransmission)
        printf("Android server OMSI gearbox: %s\n",modPath);
    else if(modPath && *modPath)
        printf("Android server could not load gearbox: %s\n",modPath);
    const float route[4][2] = {{3,3},{3,57},{57,57},{57,3}};
    int waypoint[AI_BUSES] = {};
    for(int i=0;i<AI_BUSES;++i) {
        if(customTransmission)sim::setDriveProfiles(traffic[i],scripts.automatic,scripts.manual);
        traffic[i].b.id=0x80000000u+(uint32_t)i;
        traffic[i].b.x=route[i][0];
        traffic[i].b.z=route[i][1];
        waypoint[i]=(i+1)%4;
        traffic[i].b.heading=atan2f(route[waypoint[i]][0]-route[i][0],route[waypoint[i]][1]-route[i][1]);
    }

    uint32_t nextId=1, tick=0;
    uint64_t previous=androidnet::millis();
    double accumulator=0, sendAccumulator=0;
    printf("Android BUS4 UDP server port %u\n",BUS_PORT);
    fflush(stdout);

    for(;;) {
        NetPacket packet;
        sockaddr_in from={};
        socklen_t fromSize=sizeof(from);
        ssize_t n=0;
        while((n=recvfrom(fd,&packet,sizeof(packet),0,(sockaddr*)&from,&fromSize))>0) {
            if(n<(ssize_t)offsetof(NetPacket,buses) || packet.magic!=BUS_MAGIC || packet.type!=PACKET_INPUT) {
                fromSize=sizeof(from);
                continue;
            }
            int index=-1;
            for(int i=0;i<MAX_PLAYERS;++i) {
                if(players[i].active && androidnet::sameAddress(players[i].address,from)) { index=i; break; }
            }
            if(index<0) {
                for(int i=0;i<MAX_PLAYERS;++i) {
                    if(!players[i].active) { index=i; break; }
                }
                if(index<0) { fromSize=sizeof(from); continue; }
                players[index]=AndroidPlayer();
                if(customTransmission)
                    sim::setDriveProfiles(players[index].physics,scripts.automatic,scripts.manual);
                players[index].active=true;
                players[index].address=from;
                players[index].physics.b.id=nextId++;
                players[index].physics.b.x=(index%4)*3.0f-4.5f;
                players[index].physics.b.z=-20.0f+(index/4)*12.0f;
                printf("Connected ID %u\n",players[index].physics.b.id);
            }
            players[index].lastSeen=androidnet::millis();
            sim::input(players[index].physics,packet.throttle,packet.steering,packet.brake,packet.flags);
            fromSize=sizeof(from);
        }

        const uint64_t now=androidnet::millis();
        const double elapsed=(now-previous)>200?0.2:(double)(now-previous)/1000.0;
        previous=now;
        accumulator+=elapsed;
        sendAccumulator+=elapsed;
        while(accumulator>=1.0/60.0) {
            for(int i=0;i<MAX_PLAYERS;++i) {
                if(!players[i].active)continue;
                if(now-players[i].lastSeen>5000) {
                    printf("Disconnected ID %u\n",players[i].physics.b.id);
                    players[i].active=false;
                    continue;
                }
                sim::step(players[i].physics,1.0f/60.0f);
            }
            for(int i=0;i<AI_BUSES;++i) {
                sim::Dynamics& ai=traffic[i];
                float dx=route[waypoint[i]][0]-ai.b.x;
                float dz=route[waypoint[i]][1]-ai.b.z;
                float distance=sqrtf(dx*dx+dz*dz);
                if(distance<6.0f) {
                    waypoint[i]=(waypoint[i]+1)%4;
                    dx=route[waypoint[i]][0]-ai.b.x;
                    dz=route[waypoint[i]][1]-ai.b.z;
                    distance=sqrtf(dx*dx+dz*dz);
                }
                const float desired=atan2f(dx,dz);
                const float difference=atan2f(sinf(desired-ai.b.heading),cosf(desired-ai.b.heading));
                const float steering=sim::clamp(difference*1.7f,-1.0f,1.0f);
                const float speed=distance<17.0f?3.0f:7.0f;
                sim::input(ai,ai.b.speed<speed?0.55f:0.0f,steering,ai.b.speed>speed?0.7f:0.0f,0);
                sim::step(ai,1.0f/60.0f);
            }
            for(int i=0;i<MAX_PLAYERS;++i)if(players[i].active) {
                for(int j=i+1;j<MAX_PLAYERS;++j)if(players[j].active)
                    sim::separate(players[i].physics,players[j].physics);
            }
            for(int i=0;i<AI_BUSES;++i)for(int j=0;j<MAX_PLAYERS;++j)if(players[j].active)
                sim::separate(traffic[i],players[j].physics);
            ++tick;
            accumulator-=1.0/60.0;
        }
        if(sendAccumulator>=1.0/20.0) {
            sendAccumulator=0;
            NetPacket response;
            androidnet::initPacket(response,PACKET_WORLD);
            response.tick=tick;
            for(int i=0;i<MAX_PLAYERS;++i)if(players[i].active)
                response.buses[response.count++]=players[i].physics.b;
            for(int i=0;i<AI_BUSES;++i)
                response.buses[response.count++]=traffic[i].b;
            for(int i=0;i<MAX_PLAYERS;++i)if(players[i].active) {
                response.clientId=players[i].physics.b.id;
                sendto(fd,&response,sizeof(response),0,(sockaddr*)&players[i].address,sizeof(players[i].address));
            }
        }
        androidnet::sleepMillis(1);
    }
}
