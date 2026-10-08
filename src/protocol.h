#pragma once
#ifndef BUS_SIM_TEST
#include <winsock2.h>
#endif
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static const unsigned short BUS_PORT = 27015;
static const int MAX_BUSES = 16;
static const uint32_t BUS_MAGIC = 0x42555332; // BUS2: protocolo alterado, ambos precisam desta versao

enum { PACKET_INPUT=1, PACKET_WORLD=2 };
enum { INPUT_TOGGLE_DOOR=1, INPUT_GEAR_UP=2, INPUT_GEAR_DOWN=4 };
#pragma pack(push,1)
struct BusState {
    uint32_t id;
    float x, y, z, heading, speed, steer, pitch, roll;
    float wheelTravel[6], wheelRotation;
    float rpm, door;
    int32_t gear;
    uint32_t passengers, nextStop;
};
struct NetPacket {
    uint32_t magic, type, clientId, count, tick, flags;
    float throttle, steering, brake;
    BusState buses[MAX_BUSES];
};
#pragma pack(pop)


#ifndef BUS_SIM_TEST
inline bool initializeSockets() { WSADATA w; return WSAStartup(MAKEWORD(2,2),&w)==0; }
inline void closeSockets() { WSACleanup(); }
inline void initPacket(NetPacket& p,uint32_t type) {
    memset(&p,0,sizeof(p)); p.magic=BUS_MAGIC; p.type=type;
}
inline bool addressEqual(const sockaddr_in& a,const sockaddr_in& b) {
    return a.sin_addr.s_addr==b.sin_addr.s_addr && a.sin_port==b.sin_port;
}

#endif // BUS_SIM_TEST
