#pragma once
#include <winsock2.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static const unsigned short BUS_PORT = 27015;
static const int MAX_BUSES = 16;
static const uint32_t BUS_MAGIC = 0x42555331;
#pragma pack(push,1)
struct BusState { uint32_t id; float x,z,heading,speed,steer; };
struct NetPacket {
    uint32_t magic;
    uint32_t type; // 1: input; 2: world snapshot
    uint32_t clientId;
    uint32_t count;
    float throttle;
    float steering;
    float brake;
    BusState buses[MAX_BUSES];
};
#pragma pack(pop)
inline bool initializeSockets() { WSADATA w; return WSAStartup(MAKEWORD(2,2), &w)==0; }
inline void closeSockets() { WSACleanup(); }
inline void initPacket(NetPacket& p, uint32_t t) { memset(&p,0,sizeof(p)); p.magic=BUS_MAGIC;p.type=t; }
inline bool addressEqual(const sockaddr_in& a,const sockaddr_in& b) { return a.sin_addr.s_addr==b.sin_addr.s_addr && a.sin_port==b.sin_port; }
