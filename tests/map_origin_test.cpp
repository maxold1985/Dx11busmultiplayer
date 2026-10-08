#include "protocol_stub.h"
#define BUS_SIM_TEST 1
#include "../src/simulation.hpp"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool nearlyEqual(float a, float b) {
	return fabsf(a - b) < 0.0001f;
}

static void testLargeMap() {
	assert(sim::MAP_HALF_EXTENT >= 2000.0f);
	assert(sim::roadIndex(0.0f) == 0);
	assert(sim::roadIndex(60.0f) == 1);
	assert(sim::roadIndex(-0.1f) == -1);

	assert(sim::insideMap(0.0f, 0.0f));
	assert(sim::insideMap(1980.0f, -1980.0f));
	assert(!sim::insideMap(2100.0f, 0.0f));

	assert(!sim::collidesBuildings(0.0f, 0.0f, 0.0f));
	assert(sim::collidesBuildings(2100.0f, 0.0f, 0.0f));

	const sim::Box distantBuilding = sim::building(20, -20);
	assert(sim::collidesBuildings(
		distantBuilding.x,
		distantBuilding.z,
		0.0f
	));

	assert(sim::STOP_COUNT == 28);
	for(int i = 0; i < sim::STOP_COUNT; ++i) {
		const sim::Stop stop = sim::stop(i);
		assert(sim::insideMap(stop.x, stop.z, 10.0f));
	}
}

static void testOriginReset() {
	sim::Dynamics bus;
	bus.b.id = 314;
	bus.b.x = 780.0f;
	bus.b.z = -650.0f;
	bus.b.y = 3.0f;
	bus.b.heading = 1.1f;
	bus.b.speed = 14.0f;
	bus.b.roll = 0.12f;
	bus.b.pitch = 0.08f;
	bus.verticalSpeed = -1.0f;
	bus.yawRate = 0.2f;
	bus.b.passengers = 20;
	bus.b.nextStop = 17;
	bus.b.door = 1.0f;

	sim::input(bus, 1.0f, 0.5f, 0.0f, INPUT_RESET_ORIGIN);

	assert(bus.b.id == 314);
	assert(nearlyEqual(bus.b.x, 0.0f));
	assert(nearlyEqual(bus.b.z, 0.0f));
	assert(nearlyEqual(bus.b.heading, 0.0f));
	assert(nearlyEqual(bus.b.speed, 0.0f));
	assert(nearlyEqual(bus.b.pitch, 0.0f));
	assert(nearlyEqual(bus.b.roll, 0.0f));
	assert(nearlyEqual(bus.verticalSpeed, 0.0f));
	assert(nearlyEqual(bus.yawRate, 0.0f));
	assert(bus.b.passengers == 0);
	assert(bus.b.nextStop == 0);
	assert(nearlyEqual(bus.b.y, 1.56f));
	assert((bus.lastFlags & INPUT_RESET_ORIGIN) != 0);

	// The key held across multiple UDP updates should only reset once.
	bus.b.x = 45.0f;
	sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_RESET_ORIGIN);
	assert(nearlyEqual(bus.b.x, 45.0f));

	// Release and press again: a second reset must work.
	sim::input(bus, 0.0f, 0.0f, 0.0f, 0);
	sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_RESET_ORIGIN);
	assert(nearlyEqual(bus.b.x, 0.0f));
}

int main() {
	testLargeMap();
	testOriginReset();

	puts("map and reset origin tests passed");
	return 0;
}
