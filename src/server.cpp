#define _WIN32_WINNT 0x0601
#include "simulation.hpp"
#include <windows.h>
#include <stdio.h>
struct Player {
    bool active;
    sockaddr_in address;
    sim::Dynamics physics;
    DWORD lastSeen;
    Player():active(false),lastSeen(0) {memset(&address,0,sizeof(address));}
};
int main() {
    if(!initializeSockets())return 1;
    SOCKET sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(sock==INVALID_SOCKET){closeSockets();return 1;}
    sockaddr_in address={};
    address.sin_family=AF_INET;address.sin_addr.s_addr=INADDR_ANY;address.sin_port=htons(BUS_PORT);
    if(bind(sock,(sockaddr*)&address,sizeof(address))==SOCKET_ERROR) {
        printf("Nao foi possivel abrir UDP %u, erro=%d\n",BUS_PORT,WSAGetLastError());
        closesocket(sock);closeSockets();return 1;
    }
    u_long nonBlocking=1;ioctlsocket(sock,FIONBIO,&nonBlocking);
    Player players[MAX_PLAYERS];
    struct Traffic {sim::Dynamics physics;int waypoint;};
    Traffic traffic[AI_BUSES];
    const float route[4][2]={{3.0f,3.0f},{3.0f,57.0f},{57.0f,57.0f},{57.0f,3.0f}};
    for(int i=0;i<AI_BUSES;i++) {
        traffic[i].physics.b.id=0x80000000u+(uint32_t)i;
        traffic[i].physics.b.x=route[i][0];
        traffic[i].physics.b.z=route[i][1];
        traffic[i].waypoint=(i+1)%4;
        traffic[i].physics.b.heading=atan2f(
            route[traffic[i].waypoint][0]-route[i][0],
            route[traffic[i].waypoint][1]-route[i][1]);
    }
    uint32_t nextId=1,tick=0;
    LARGE_INTEGER frequency,previous,now;
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&previous);
    double accumulator=0,sendAccumulator=0;
    printf("DX11 Bus servidor autoritativo: UDP %u, %d jogadores\n",BUS_PORT,MAX_PLAYERS);
    for(;;) {
        NetPacket packet;
        sockaddr_in from={};int fromSize=sizeof(from);
        int n;
        while((n=recvfrom(sock,(char*)&packet,sizeof(packet),0,(sockaddr*)&from,&fromSize))>0) {
            if(n<(int)offsetof(NetPacket,buses) || packet.magic!=BUS_MAGIC || packet.type!=PACKET_INPUT) {
                fromSize=sizeof(from);continue;
            }
            int index=-1;
            for(int i=0;i<MAX_PLAYERS;i++) if(players[i].active && addressEqual(players[i].address,from)) {index=i;break;}
            if(index<0) {
                for(int i=0;i<MAX_PLAYERS;i++)if(!players[i].active){index=i;break;}
                if(index<0){fromSize=sizeof(from);continue;}
                players[index]=Player();
                players[index].active=true;
                players[index].address=from;
                players[index].physics.b.id=nextId++;
                players[index].physics.b.x=(index%4)*3.0f-4.5f;
                players[index].physics.b.z=-20.0f+(index/4)*12.0f;
                printf("Conectou: ID %u\n",players[index].physics.b.id);
            }
            Player& player=players[index];
            player.lastSeen=GetTickCount();
            sim::input(player.physics,packet.throttle,packet.steering,packet.brake,packet.flags);
            fromSize=sizeof(from);
        }
        QueryPerformanceCounter(&now);
        double elapsed=double(now.QuadPart-previous.QuadPart)/double(frequency.QuadPart);
        previous=now;
        if(elapsed>0.2)elapsed=0.2;
        accumulator+=elapsed;sendAccumulator+=elapsed;
        while(accumulator>=1.0/60.0) {
            for(int i=0;i<MAX_PLAYERS;i++) {
                if(!players[i].active)continue;
                Player& p=players[i];
                if((DWORD)(GetTickCount()-p.lastSeen)>5000) {
                    printf("Desconectou ID %u\n",p.physics.b.id);
                    p.active=false;continue;
                }
                sim::step(p.physics,1.0f/60.0f);
            }
            for(int i=0;i<AI_BUSES;i++) {
                Traffic& ai=traffic[i];
                float dx=route[ai.waypoint][0]-ai.physics.b.x;
                float dz=route[ai.waypoint][1]-ai.physics.b.z;
                float distance=sqrtf(dx*dx+dz*dz);
                if(distance<6.0f) {
                    ai.waypoint=(ai.waypoint+1)%4;
                    dx=route[ai.waypoint][0]-ai.physics.b.x;
                    dz=route[ai.waypoint][1]-ai.physics.b.z;
                    distance=sqrtf(dx*dx+dz*dz);
                }
                float desired=atan2f(dx,dz);
                float difference=atan2f(sinf(desired-ai.physics.b.heading),
                                        cosf(desired-ai.physics.b.heading));
                float steering=sim::clamp(difference*1.7f,-1.0f,1.0f);
                float desiredSpeed=distance<17.0f?3.0f:7.0f;
                float accel=ai.physics.b.speed<desiredSpeed?0.55f:0.0f;
                float braking=ai.physics.b.speed>desiredSpeed?0.7f:0.0f;
                sim::input(ai.physics,accel,steering,braking,0);
                sim::step(ai.physics,1.0f/60.0f);
            }
            for(int i=0;i<MAX_PLAYERS;i++)if(players[i].active) {
                for(int j=i+1;j<MAX_PLAYERS;j++)if(players[j].active)
                    sim::separate(players[i].physics,players[j].physics);
            }
            for(int i=0;i<AI_BUSES;i++)for(int j=0;j<MAX_PLAYERS;j++)
                if(players[j].active)sim::separate(traffic[i].physics,players[j].physics);
            ++tick;
            accumulator-=1.0/60.0;
        }
        if(sendAccumulator>=1.0/20.0) {
            sendAccumulator=0;
            NetPacket response;
            initPacket(response,PACKET_WORLD);
            response.tick=tick;
            for(int i=0;i<MAX_PLAYERS;i++)if(players[i].active)
                response.buses[response.count++]=players[i].physics.b;
            for(int i=0;i<AI_BUSES;i++)
                response.buses[response.count++]=traffic[i].physics.b;
            for(int i=0;i<MAX_PLAYERS;i++)if(players[i].active) {
                response.clientId=players[i].physics.b.id;
                sendto(sock,(const char*)&response,sizeof(response),0,
                       (sockaddr*)&players[i].address,sizeof(players[i].address));
            }
        }
        Sleep(1);
    }
}
