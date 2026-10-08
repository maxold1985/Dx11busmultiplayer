#pragma once

#ifndef BUS_SIM_TEST
#include <winsock2.h>
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static const unsigned short BUS_PORT = 27015;
static const int MAX_PLAYERS = 16;
static const int AI_BUSES = 4;
static const int MAX_BUSES = MAX_PLAYERS + AI_BUSES;

// BUS4 replicates steering and transmission mode to every client.
// Rebuild the server and every client together before connecting.
static const uint32_t BUS_MAGIC = 0x42555334;

enum PacketType {
	PACKET_INPUT = 1,
	PACKET_WORLD = 2
};

enum InputFlags {
	INPUT_TOGGLE_DOOR = 1,
	INPUT_GEAR_UP = 2,
	INPUT_GEAR_DOWN = 4,
	INPUT_AUTO_GEAR = 8,
	INPUT_RESET_ORIGIN = 16,
	INPUT_TOGGLE_OMSI_STEERING = 32
};

enum SteeringMode {
	STEERING_CLASSIC = 0,
	STEERING_OMSI_APPROX = 1
};

enum TransmissionMode {
	TRANSMISSION_AUTOMATIC = 0,
	TRANSMISSION_MANUAL = 1
};

#pragma pack(push, 1)

struct BusState {
	uint32_t id;

	float x;
	float y;
	float z;
	float heading;
	float speed;
	float steer;
	float pitch;
	float roll;

	float wheelRotation;
	float rpm;
	float door;

	int32_t gear;

	uint32_t passengers;
	uint32_t nextStop;
	uint32_t steeringMode;
	uint32_t transmissionMode;
};

struct NetPacket {
	uint32_t magic;
	uint32_t type;
	uint32_t clientId;
	uint32_t count;
	uint32_t tick;
	uint32_t flags;

	float throttle;
	float steering;
	float brake;

	BusState buses[MAX_BUSES];
};

#pragma pack(pop)

static_assert(
	sizeof(NetPacket) <= 1400,
	"UDP snapshot excede limite para evitar fragmentacao"
);

#ifndef BUS_SIM_TEST

inline bool initializeSockets() {
	WSADATA w;
	return WSAStartup(MAKEWORD(2, 2), &w) == 0;
}

inline void closeSockets() {
	WSACleanup();
}

inline void initPacket(NetPacket& packet, uint32_t type) {
	memset(&packet, 0, sizeof(packet));
	packet.magic = BUS_MAGIC;
	packet.type = type;
}

inline bool addressEqual(const sockaddr_in& a, const sockaddr_in& b) {
	return
		a.sin_addr.s_addr == b.sin_addr.s_addr &&
		a.sin_port == b.sin_port;
}

#endif // BUS_SIM_TEST
