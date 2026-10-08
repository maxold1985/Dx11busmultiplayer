#define _WIN32_WINNT 0x0601
#include "protocol.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>

struct Player { bool active; sockaddr_in address; BusState bus; float throttle,steer,brake; DWORD lastSeen; };
int main() {
    if(!initializeSockets()) return 1;
    SOCKET sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(sock==INVALID_SOCKET) { closeSockets(); return 1; }
    sockaddr_in local={}; local.sin_family=AF_INET; local.sin_port=htons(BUS_PORT); local.sin_addr.s_addr=INADDR_ANY;
    if(bind(sock,(sockaddr*)&local,sizeof(local))==SOCKET_ERROR) { printf("Porta %u ocupada\n",BUS_PORT);closesocket(sock);closeSockets();return 1; }
    u_long nonBlocking=1; ioctlsocket(sock,FIONBIO,&nonBlocking);
    Player players[MAX_BUSES]={};
    uint32_t nextId=1;
    LARGE_INTEGER freq,last,now; QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&last);
    double accumulator=0,sendTimer=0;
    printf("Servidor UDP ativo na porta %u; maximo de %d onibus. Ctrl+C encerra.\n",BUS_PORT,MAX_BUSES);
    for(;;) {
        sockaddr_in from={}; int fromLen=sizeof(from); NetPacket p={};
        int bytes;
        while((bytes=recvfrom(sock,(char*)&p,sizeof(p),0,(sockaddr*)&from,&fromLen))>0) {
            if(bytes<static_cast<int>(offsetof(NetPacket,buses)) || p.magic!=BUS_MAGIC || p.type!=1) { fromLen=sizeof(from);continue; }
            int index=-1;
            for(int i=0;i<MAX_BUSES;i++) if(players[i].active && addressEqual(players[i].address,from)) {index=i;break;}
            if(index<0) {
                for(int i=0;i<MAX_BUSES;i++) if(!players[i].active) {index=i;break;}
                if(index<0) {fromLen=sizeof(from);continue;}
                players[index]=Player(); players[index].active=true;players[index].address=from;
                players[index].bus.id=nextId++;players[index].bus.x=float(index%4)*7.0f-10.0f;
                players[index].bus.z=float(index/4)*10.0f;
                printf("Entrou onibus ID %u\n",players[index].bus.id);
            }
            Player& pl=players[index]; pl.lastSeen=GetTickCount();
            pl.throttle=std::max(-1.0f,std::min(1.0f,p.throttle));
            pl.steer=std::max(-1.0f,std::min(1.0f,p.steering));
            pl.brake=std::max(0.0f,std::min(1.0f,p.brake));
            fromLen=sizeof(from);
        }
        QueryPerformanceCounter(&now);
        double elapsed=double(now.QuadPart-last.QuadPart)/double(freq.QuadPart);last=now;
        if(elapsed>0.2)elapsed=0.2;
        accumulator+=elapsed;sendTimer+=elapsed;
        while(accumulator>=1.0/60.0) {
            const float dt=1.0f/60.0f;
            for(int i=0;i<MAX_BUSES;i++) {
                Player& pl=players[i]; if(!pl.active)continue;
                if((DWORD)(GetTickCount()-pl.lastSeen)>5000) {printf("Saiu ID %u\n",pl.bus.id);pl.active=false;continue;}
                BusState& b=pl.bus;
                b.steer+=(pl.steer-b.steer)*std::min(1.0f,dt*5.0f);
                b.speed+=pl.throttle*8.0f*dt;
                b.speed-=b.speed*(0.32f+pl.brake*5.0f)*dt;
                b.speed=std::max(-7.0f,std::min(23.0f,b.speed));
                // Cinematica do tipo bicicleta: distancia entre eixos aproximada 6 m.
                b.heading+=tanf(b.steer*0.47f)*b.speed/6.0f*dt;
                b.x+=sinf(b.heading)*b.speed*dt;
                b.z+=cosf(b.heading)*b.speed*dt;
            }
            accumulator-=dt;
        }
        if(sendTimer>=1.0/20.0) {
            sendTimer=0;
            NetPacket snapshot;initPacket(snapshot,2);
            for(int i=0;i<MAX_BUSES;i++) if(players[i].active) snapshot.buses[snapshot.count++]=players[i].bus;
            for(int i=0;i<MAX_BUSES;i++) if(players[i].active) {
                snapshot.clientId=players[i].bus.id;
                sendto(sock,(const char*)&snapshot,sizeof(snapshot),0,(sockaddr*)&players[i].address,sizeof(players[i].address));
            }
        }
        Sleep(1);
    }
}