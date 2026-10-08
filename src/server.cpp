#define _WIN32_WINNT 0x0601

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "simulation.hpp"

#include <windows.h>
#include <stdio.h>

struct Player {
	bool active;
	sockaddr_in address;
	sim::Dynamics physics;
	DWORD lastSeen;

	Player() : active(false), lastSeen(0) {
		memset(&address, 0, sizeof(address));
	}
};

struct Traffic {
	sim::Dynamics physics;
	int waypoint;
};

int main() {
	if(!initializeSockets()) {
		return 1;
	}

	SOCKET socketUdp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if(socketUdp == INVALID_SOCKET) {
		closeSockets();
		return 1;
	}

	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(BUS_PORT);

	if(bind(socketUdp, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
		printf(
			"Nao foi possivel abrir UDP %u, erro=%d\n",
			BUS_PORT,
			WSAGetLastError()
		);
		closesocket(socketUdp);
		closeSockets();
		return 1;
	}

	u_long nonBlocking = 1;
	ioctlsocket(socketUdp, FIONBIO, &nonBlocking);

	Player players[MAX_PLAYERS];
	Traffic traffic[AI_BUSES];

	const float route[4][2] = {
		{ 3.0f, 3.0f },
		{ 3.0f, 57.0f },
		{ 57.0f, 57.0f },
		{ 57.0f, 3.0f }
	};

	for(int i = 0; i < AI_BUSES; ++i) {
		Traffic& ai = traffic[i];
		ai.physics.b.id = 0x80000000u + (uint32_t)i;
		ai.physics.b.x = route[i][0];
		ai.physics.b.z = route[i][1];
		ai.waypoint = (i + 1) % 4;
		ai.physics.b.heading = atan2f(
			route[ai.waypoint][0] - route[i][0],
			route[ai.waypoint][1] - route[i][1]
		);
	}

	uint32_t nextId = 1;
	uint32_t tick = 0;

	LARGE_INTEGER frequency;
	LARGE_INTEGER previous;
	LARGE_INTEGER now;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&previous);

	double accumulator = 0.0;
	double sendAccumulator = 0.0;

	printf(
		"DX11 Bus servidor: UDP %u, %d jogadores, mapa %.0f x %.0f metros\n",
		BUS_PORT,
		MAX_PLAYERS,
		sim::MAP_HALF_EXTENT * 2.0f,
		sim::MAP_HALF_EXTENT * 2.0f
	);
	printf("Pressione R no cliente para resetar seu onibus na origem (0, 0).\n");

	for(;;) {
		NetPacket packet;
		sockaddr_in from = {};
		int fromSize = sizeof(from);
		int n = 0;

		while((
			n = recvfrom(
				socketUdp,
				(char*)&packet,
				sizeof(packet),
				0,
				(sockaddr*)&from,
				&fromSize
			)
		) > 0) {
			if(n < (int)offsetof(NetPacket, buses) ||
				packet.magic != BUS_MAGIC ||
				packet.type != PACKET_INPUT) {
				fromSize = sizeof(from);
				continue;
			}

			int index = -1;

			for(int i = 0; i < MAX_PLAYERS; ++i) {
				if(players[i].active && addressEqual(players[i].address, from)) {
					index = i;
					break;
				}
			}

			if(index < 0) {
				for(int i = 0; i < MAX_PLAYERS; ++i) {
					if(!players[i].active) {
						index = i;
						break;
					}
				}

				if(index < 0) {
					fromSize = sizeof(from);
					continue;
				}

				players[index] = Player();
				players[index].active = true;
				players[index].address = from;
				players[index].physics.b.id = nextId++;
				players[index].physics.b.x = (index % 4) * 3.0f - 4.5f;
				players[index].physics.b.z = -20.0f + (index / 4) * 12.0f;

				printf("Conectou: ID %u\n", players[index].physics.b.id);
			}

			Player& player = players[index];
			player.lastSeen = GetTickCount();

			const bool originReset =
				(packet.flags & INPUT_RESET_ORIGIN) != 0 &&
				(player.physics.lastFlags & INPUT_RESET_ORIGIN) == 0;

			sim::input(
				player.physics,
				packet.throttle,
				packet.steering,
				packet.brake,
				packet.flags
			);

			if(originReset) {
				printf(
				"Reset origem: ID %u -> X=0 Z=0, velocidade=0\n",
				player.physics.b.id
				);
			}

			fromSize = sizeof(from);
		}

		QueryPerformanceCounter(&now);

		const double elapsedRaw =
			(double)(now.QuadPart - previous.QuadPart) /
			(double)frequency.QuadPart;
		previous = now;

		const double elapsed = elapsedRaw > 0.2 ? 0.2 : elapsedRaw;
		accumulator += elapsed;
		sendAccumulator += elapsed;

		while(accumulator >= 1.0 / 60.0) {
			for(int i = 0; i < MAX_PLAYERS; ++i) {
				Player& player = players[i];

				if(!player.active) {
					continue;
				}

				if((DWORD)(GetTickCount() - player.lastSeen) > 5000) {
					printf("Desconectou ID %u\n", player.physics.b.id);
					player.active = false;
					continue;
				}

				sim::step(player.physics, 1.0f / 60.0f);
			}

			for(int i = 0; i < AI_BUSES; ++i) {
				Traffic& ai = traffic[i];

				float dx = route[ai.waypoint][0] - ai.physics.b.x;
				float dz = route[ai.waypoint][1] - ai.physics.b.z;
				float distance = sqrtf(dx * dx + dz * dz);

				if(distance < 6.0f) {
					ai.waypoint = (ai.waypoint + 1) % 4;
					dx = route[ai.waypoint][0] - ai.physics.b.x;
					dz = route[ai.waypoint][1] - ai.physics.b.z;
					distance = sqrtf(dx * dx + dz * dz);
				}

				const float desired = atan2f(dx, dz);
				const float difference = atan2f(
					sinf(desired - ai.physics.b.heading),
					cosf(desired - ai.physics.b.heading)
				);
				const float steering = sim::clamp(
					difference * 1.7f,
					-1.0f,
					1.0f
				);

				const float desiredSpeed = distance < 17.0f ? 3.0f : 7.0f;
				const float accel = ai.physics.b.speed < desiredSpeed ? 0.55f : 0.0f;
				const float braking = ai.physics.b.speed > desiredSpeed ? 0.7f : 0.0f;

				sim::input(ai.physics, accel, steering, braking, 0);
				sim::step(ai.physics, 1.0f / 60.0f);
			}

			for(int i = 0; i < MAX_PLAYERS; ++i) {
				if(!players[i].active) {
					continue;
				}

				for(int j = i + 1; j < MAX_PLAYERS; ++j) {
					if(players[j].active) {
						sim::separate(players[i].physics, players[j].physics);
					}
				}
			}

			for(int i = 0; i < AI_BUSES; ++i) {
				for(int j = 0; j < MAX_PLAYERS; ++j) {
					if(players[j].active) {
						sim::separate(traffic[i].physics, players[j].physics);
					}
				}
			}

			++tick;
			accumulator -= 1.0 / 60.0;
		}

		if(sendAccumulator >= 1.0 / 20.0) {
			sendAccumulator = 0.0;

			NetPacket response;
			initPacket(response, PACKET_WORLD);
			response.tick = tick;

			for(int i = 0; i < MAX_PLAYERS; ++i) {
				if(players[i].active) {
					response.buses[response.count++] = players[i].physics.b;
				}
			}

			for(int i = 0; i < AI_BUSES; ++i) {
				response.buses[response.count++] = traffic[i].physics.b;
			}

			for(int i = 0; i < MAX_PLAYERS; ++i) {
				if(!players[i].active) {
					continue;
				}

				response.clientId = players[i].physics.b.id;

				sendto(
					socketUdp,
					(const char*)&response,
					sizeof(response),
					0,
					(sockaddr*)&players[i].address,
					sizeof(players[i].address)
				);
			}
		}

		Sleep(1);
	}
}
