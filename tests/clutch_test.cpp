#include "../src/simulation.hpp"
#include <cassert>
#include <cmath>

int main() {
	sim::Dynamics bus;
	bus.manualGear = true;
	bus.b.transmissionMode = TRANSMISSION_MANUAL;
	sim::input(bus, 1.0f, 0.0f, 0.0f, INPUT_CLUTCH);
	assert(bus.b.clutch == 1.0f);
	const int before = bus.b.gear;
	sim::input(bus, 1.0f, 0.0f, 0.0f, INPUT_CLUTCH | INPUT_GEAR_UP);
	assert(bus.b.gear == before + 1);
	sim::input(bus, 1.0f, 0.0f, 0.0f, 0);
	assert(bus.b.clutch == 0.0f);
	const int gear = bus.b.gear;
	sim::input(bus, 1.0f, 0.0f, 0.0f, INPUT_GEAR_UP);
	assert(bus.b.gear == gear);
	sim::input(bus, 1.0f, 0.0f, 0.0f, INPUT_CLUTCH);
	sim::step(bus, 1.0f / 60.0f);
	assert(std::fabs(bus.b.speed) < 0.001f);
	return 0;
}
