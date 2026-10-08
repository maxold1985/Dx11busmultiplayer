#include "protocol_stub.h"
#define BUS_SIM_TEST 1

#include "../src/bus_script.hpp"
#include "../src/simulation.hpp"

#include <assert.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

static void writeFile(const std::string& path, const char* contents) {
	std::ofstream file(path.c_str(), std::ios::binary);
	assert(file);
	file << contents;
	assert(file.good());
}

static bool approximatelyEqual(float a, float b, float epsilon = 0.01f) {
	return std::fabs(a - b) < epsilon;
}

int main() {
	const std::string root = "bus_script_test_";
	const std::string descriptor = root + "bus.ini";
	const std::string manualPath = root + "Cambio_M.txt";
	const std::string autoPath = root + "Cambio_A.txt";
	const std::string soundPath = root + "Motor.txt";

	writeFile(
		descriptor,
		"[config]\n"
		"engine1auto=bus_script_test_Cambio_A.txt\n"
		"engine1manual=bus_script_test_Cambio_M.txt\n"
		"sounds1=bus_script_test_Motor.txt\n"
		"[data]\n"
		"mass1=17000\n"
	);

	writeFile(
		autoPath,
		"[engine]\n"
		"idle_rpm=550\n"
		"idle_rpm_torque=1650\n"
		"peak_rpm=1100\n"
		"peak_rpm_torque=1650\n"
		"max_rpm=2280\n"
		"[automatic_gearbox]\n"
		"num_forward_ratios=6\n"
		"fwd_ratio_1=6.98\n"
		"fwd_ratio_2=4.06\n"
		"fwd_ratio_3=2.74\n"
		"fwd_ratio_4=1.89\n"
		"fwd_ratio_5=1.31\n"
		"fwd_ratio_6=1.0\n"
		"gear_up_rpm=2170\n"
		"gear_down_rpm=900\n"
		"gear_change_time=2.0\n"
		"next_gear_min_speed=3\n"
		"[differential]\n"
		"gearRatio=3.666\n"
	);

	writeFile(
		manualPath,
		"[engine]\n"
		"idle_rpm=640\n"
		"peak_rpm=1450\n"
		"max_rpm=3000\n"
		"idle_rpm_torque=580\n"
		"peak_rpm_torque=1650\n"
		"[manual_gearbox]\n"
		"num_forward_ratios=6\n"
		"fwd_ratio_1=6.98\n"
		"fwd_ratio_2=4.06\n"
		"fwd_ratio_3=2.74\n"
		"fwd_ratio_4=1.89\n"
		"fwd_ratio_5=1.31\n"
		"fwd_ratio_6=1.0\n"
		"reverse_ratio=-6.43\n"
		"gear_change_time=0.5\n"
		"[differential]\n"
		"gearRatio=3.66\n"
	);

	writeFile(
		soundPath,
		"[sound_manager]\n"
		"max_rpm=3100\n"
		"[sound1]\n"
		"file=O-400/Mercedes Benz O400/idle.ogg\n"
		"volumeMultiplier=0.5\n"
		"pitchMultiplier=0.8\n"
		"whenToPlay=internalCam\n"
		"is_idle=1\n"
		"v1x=0\n"
		"v1y=0\n"
		"v2x=0.5\n"
		"v2y=1\n"
		"v3x=1\n"
		"v3y=0\n"
		"p1x=0\n"
		"p1y=0.4\n"
		"p2x=1\n"
		"p2y=1.2\n"
		"[sound2]\n"
		"file=../escape.ogg\n"
		"[horn]\n"
		"file=horn.ogg\n"
	);

	buscfg::ModScripts scripts;
	assert(scripts.load(descriptor));
	assert(scripts.hasAutomatic);
	assert(scripts.hasManual);
	assert(scripts.automatic.gears == 6);
	assert(scripts.manual.gears == 6);
	assert(approximatelyEqual(scripts.automatic.ratios[0], 6.98f));
	assert(approximatelyEqual(scripts.automatic.ratios[5], 1.0f));
	assert(approximatelyEqual(scripts.automatic.differential, 3.666f));
	assert(approximatelyEqual(scripts.automatic.idleRpm, 550.0f));
	assert(approximatelyEqual(scripts.automatic.upRpm, 2170.0f));
	assert(approximatelyEqual(scripts.automatic.downRpm, 900.0f));
	assert(approximatelyEqual(scripts.automatic.shiftSeconds, 2.0f));
	assert(approximatelyEqual(scripts.manual.idleRpm, 640.0f));
	assert(approximatelyEqual(scripts.manual.shiftSeconds, 0.5f));
	assert(approximatelyEqual(scripts.automatic.vehicleMass, 17000.0f));
	assert(!buscfg::safeRelative("../out.ogg"));
	assert(!buscfg::safeRelative("C:/folder/file.ogg"));
	assert(buscfg::safeRelative("O-400/Motor/idle.ogg"));

	assert(scripts.sounds.size() == 1);
	assert(scripts.sounds[0].section == "sound1");
	assert(scripts.sounds[0].idle);
	assert(approximatelyEqual(scripts.sounds[0].volume(0.5f), 0.5f));
	assert(approximatelyEqual(scripts.sounds[0].pitch(0.5f), 0.64f));
	assert(scripts.locateSound(scripts.sounds[0]).empty());

	sim::Dynamics bus;
	bus.b.z = -120.0f;
	sim::setDriveProfiles(bus, scripts.automatic, scripts.manual);
	assert(!bus.manualGear);
	assert(approximatelyEqual(bus.b.rpm, 550.0f));

	sim::input(bus, 1.0f, 0.0f, 0.0f, 0);
	for(int frame = 0; frame < 720; ++frame) {
		sim::step(bus, 1.0f / 60.0f);
	}

	assert(bus.b.speed > 3.0f);
	assert(bus.b.gear >= 2);
	assert(bus.b.rpm >= 550.0f);
	assert(bus.b.rpm <= 2280.0f);

	const int beforeManual = bus.b.gear;
	sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_GEAR_UP);
	assert(bus.manualGear);
	assert(bus.b.gear == std::min(6, beforeManual + 1));
	assert(bus.shiftTimer > 0.0f || beforeManual == 6);

	sim::input(bus, 0.0f, 0.0f, 0.0f, 0);
	sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_AUTO_GEAR);
	assert(!bus.manualGear);

	sim::input(bus, 0.0f, 0.0f, 0.0f, 0);
	sim::input(bus, 0.0f, 0.0f, 0.0f, INPUT_RESET_ORIGIN);
	assert(bus.driveAutomatic.valid);
	assert(bus.driveManual.valid);
	assert(approximatelyEqual(bus.b.rpm, 550.0f));
	assert(bus.b.gear == 1);

	std::remove(descriptor.c_str());
	std::remove(manualPath.c_str());
	std::remove(autoPath.c_str());
	std::remove(soundPath.c_str());

	puts("bus scripts, audio curves and six-speed transmission tests passed");
	return 0;
}
