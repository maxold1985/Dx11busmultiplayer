// Android NDK UDP client. Console transport/test client, not a graphical Activity.
#include "android_net.hpp"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if(argc<2) {
        fprintf(stderr,"Usage: bus_android_client <server_ipv4> [throttle] [steering] [brake]\n");
        return 1;
    }
    sockaddr_in server={};
    server.sin_family=AF_INET;
    server.sin_port=htons(BUS_PORT);
    if(inet_pton(AF_INET,argv[1],&server.sin_addr)!=1) {
        fprintf(stderr,"Invalid IPv4 address\n");
        return 1;
    }
    const float throttle=argc>2?(float)atof(argv[2]):0.0f;
    const float steering=argc>3?(float)atof(argv[3]):0.0f;
    const float brake=argc>4?(float)atof(argv[4]):0.0f;
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0) { perror("socket"); return 1; }
    if(!androidnet::nonblocking(fd)) { perror("fcntl"); close(fd); return 1; }
    uint64_t lastSend=0,lastPrint=0;
    uint32_t myId=0;
    printf("Android client connecting to %s:%u\n",argv[1],BUS_PORT);
    for(;;) {
        uint64_t now=androidnet::millis();
        if(now-lastSend>=50) {
            NetPacket packet;
            androidnet::initPacket(packet,PACKET_INPUT);
            packet.clientId=myId;
            packet.throttle=throttle;
            packet.steering=steering;
            packet.brake=brake;
            sendto(fd,&packet,sizeof(packet),0,(sockaddr*)&server,sizeof(server));
            lastSend=now;
        }
        NetPacket response;
        sockaddr_in from={};
        socklen_t len=sizeof(from);
        ssize_t n=0;
        while((n=recvfrom(fd,&response,sizeof(response),0,(sockaddr*)&from,&len))>0) {
            if(!androidnet::sameAddress(from,server) ||
                n<(ssize_t)offsetof(NetPacket,buses) ||
                response.magic!=BUS_MAGIC || response.type!=PACKET_WORLD ||
                response.count>MAX_BUSES ||
                n<(ssize_t)(offsetof(NetPacket,buses)+response.count*sizeof(BusState))) {
                len=sizeof(from);
                continue;
            }
            myId=response.clientId;
            if(now-lastPrint>=500) {
                for(uint32_t i=0;i<response.count;++i) {
                    const BusState& b=response.buses[i];
                    if(b.id==myId) {
                        printf("ID %u tick %u X %.2f Z %.2f speed %.2f RPM %.0f gear %d vehicles %u\n",
                            myId,response.tick,b.x,b.z,b.speed,b.rpm,b.gear,response.count);
                        fflush(stdout);
                        lastPrint=now;
                        break;
                    }
                }
            }
            len=sizeof(from);
        }
        androidnet::sleepMillis(5);
    }
}
