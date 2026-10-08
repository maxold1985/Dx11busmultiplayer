#include "protocol_stub.h"
#define BUS_SIM_TEST 1
#include "../src/simulation.hpp"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool closeEnough(float a, float b, float tolerance = 0.0001f) {
	return fabsf(a - b) < tolerance;
}

static void testToggleOnlyOncePerPress() {
	sim::Dynamics bus;
	assert(bus.b.steeringMode == STEERING_CLASSIC);

	sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_TOGGLE_OMSI_STEERING);
	assert(bus.b.steeringMode == STEERING_OMSI_APPROX);

	for(int i = 0; i < 12; ++i) {
		sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_TOGGLE_OMSI_STEERING);
	}

	assert(bus.b.steeringMode == STEERING_OMSI_APPROX);

	sim::input(bus, 0.0f, 0.0f, 0.0f, 0);
	sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_TOGGLE_OMSI_STEERING);
	assert(bus.b.steeringMode == STEERING_CLASSIC);
}

static void testMoreWheelLockInOmsiMode() {
	sim::Dynamics classic;
	sim::Dynamics omsi;

	classic.b.speed = 3.0f;
	classic.b.steer = 1.0f;
	omsi.b.speed = 3.0f;
	omsi.b.steer = 1.0f;
	omsi.b.steeringMode = STEERING_OMSI_APPROX;

	assert(sim::steerLimit(omsi.b) > sim::steerLimit(classic.b));
	assert(sim::wheelSteerAngle(omsi.b, 1) > sim::wheelSteerAngle(classic.b, 1));
	assert(sim::wheelSteerAngle(omsi.b, 1) > sim::wheelSteerAngle(omsi.b, 0));

	const float lowSpeedAngle = sim::steerLimit(omsi.b);
	omsi.b.speed = 20.0f;
	assert(sim::steerLimit(omsi.b) < lowSpeedAngle);
}

static void testKeyboardResponse() {
	sim::Dynamics bus;
	bus.b.steeringMode = STEERING_OMSI_APPROX;

	const float dt = 1.0f / 60.0f;
	const float first = sim::advanceSteering(bus.b, 0.0f, 1.0f, dt);

	assert(first > 0.0f);
	assert(first < 0.05f);
	assert(closeEnough(
		sim::advanceSteering(bus.b, 0.0f, -1.0f, dt),
		-first
	));

	float position = 0.0f;

	for(int i = 0; i < 70; ++i) {
		position = sim::advanceSteering(bus.b, position, 1.0f, dt);
	}

	assert(position > 0.99f);

	for(int i = 0; i < 60; ++i) {
		position = sim::advanceSteering(bus.b, position, 0.0f, dt);
	}

	assert(fabsf(position) < 0.0001f);
}

static void testServerStateAndReset() {
	sim::Dynamics a;
	sim::Dynamics b;

	a.b.id = 100;
	b.b.id = 101;

	sim::input(a, 0.0f, 0.0f, 0.0f, INPUT_TOGGLE_OMSI_STEERING);

	assert(a.b.steeringMode == STEERING_OMSI_APPROX);
	assert(b.b.steeringMode == STEERING_CLASSIC);

	a.b.x = 90.0f;
	a.b.z = -190.0f;
	a.b.speed = 8.0f;

	sim::input(a, 0.0f, 0.0f, 0.0f, 0);
	sim::input(a, 0.0f, 0.0f, 0.0f, INPUT_RESET_ORIGIN);

	assert(a.b.id == 100);
	assert(a.b.steeringMode == STEERING_OMSI_APPROX);
	assert(closeEnough(a.b.x, 0.0f));
	assert(closeEnough(a.b.z, 0.0f));
	assert(closeEnough(a.b.speed, 0.0f));
}

static void testLowSpeedTurn() {
	sim::Dynamics classic;
	sim::Dynamics omsi;

	classic.b.z = -130.0f;
	omsi.b.z = -130.0f;
	classic.b.speed = 3.0f;
	omsi.b.speed = 3.0f;

	sim::input(classic, 0.0f, 1.0f, 0.0f, 0);
	sim::input(omsi, 0.0f, 1.0f, 0.0f, INPUT_TOGGLE_OMSI_STEERING);

	for(int i = 0; i < 120; ++i) {
		sim::step(classic, 1.0f / 60.0f);
		sim::step(omsi, 1.0f / 60.0f);
	}

	assert(omsi.b.steeringMode == STEERING_OMSI_APPROX);
	assert(omsi.b.heading > classic.b.heading + 0.03f);
	assert(std::isfinite(omsi.b.roll));
	assert(std::isfinite(omsi.b.pitch));

	printf(
		"Two-second turn: classic %.3f rad, OMSI approx %.3f rad\n",
		classic.b.heading,
		omsi.b.heading
	);
}

int main() {
	static_assert(sizeof(NetPacket) <= 1400, "UDP packets must not fragment");

	testToggleOnlyOncePerPress();
	testMoreWheelLockInOmsiMode();
	testKeyboardResponse();
	testServerStateAndReset();
	testLowSpeedTurn();

	puts("omsi steering mode tests passed");
	return 0;
}
