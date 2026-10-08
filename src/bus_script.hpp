#pragma once
// Leitor INI dos arquivos de configuracao enviados pelo usuario.
// Nao executa bytecode OMSI .osc; interpreta apenas chaves conhecidas.
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace buscfg {

inline std::string trim(const std::string& value) {
	const size_t first = value.find_first_not_of(" \t\r\n");
	if(first == std::string::npos) {
		return "";
	}

	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

inline std::string lower(std::string value) {
	for(size_t i = 0; i < value.size(); ++i) {
		value[i] = (char)std::tolower((unsigned char)value[i]);
	}

	return value;
}

inline std::string normalize(std::string value) {
	std::replace(value.begin(), value.end(), '\\', '/');
	return value;
}

inline std::string directory(const std::string& path) {
	const std::string canonical = normalize(path);
	const size_t slash = canonical.find_last_of('/');
	return slash == std::string::npos ? "" : canonical.substr(0, slash + 1);
}

inline std::string basename(const std::string& path) {
	const std::string canonical = normalize(path);
	const size_t slash = canonical.find_last_of('/');
	return slash == std::string::npos ? canonical : canonical.substr(slash + 1);
}

// Config paths must stay under the selected bus folder.
inline bool safeRelative(const std::string& path) {
	const std::string canonical = normalize(path);
	if(canonical.empty() || canonical[0] == '/' || canonical.find(':') != std::string::npos) {
		return false;
	}

	std::stringstream stream(canonical);
	std::string part;

	while(std::getline(stream, part, '/')) {
		if(part == ".." || part == ".") {
			return false;
		}
	}

	return true;
}

inline bool exists(const std::string& path) {
	std::ifstream in(path.c_str(), std::ios::binary);
	return !!in;
}

inline std::string findRelative(
	const std::string& root,
	const std::string& candidate,
	const std::string& subfolder = ""
) {
	if(!safeRelative(candidate)) {
		return "";
	}

	const std::string filename = basename(candidate);
	std::vector<std::string> locations;

	if(!subfolder.empty()) {
		locations.push_back(root + subfolder + "/" + normalize(candidate));
		locations.push_back(root + subfolder + "/" + filename);
	}

	locations.push_back(root + normalize(candidate));
	locations.push_back(root + filename);
	locations.push_back(root + "Script/" + filename);
	locations.push_back(root + "script/" + filename);

	for(size_t i = 0; i < locations.size(); ++i) {
		if(exists(locations[i])) {
			return locations[i];
		}
	}

	return "";
}

struct Entry {
	std::string key;
	std::string value;
};

struct Section {
	std::string name;
	std::vector<Entry> entries;

	std::string get(const std::string& key, const std::string& fallback = "") const {
		const std::string wanted = lower(key);

		for(size_t i = 0; i < entries.size(); ++i) {
			if(entries[i].key == wanted) {
				return entries[i].value;
			}
		}

		return fallback;
	}

	float number(const std::string& key, float fallback = 0.0f) const {
		const std::string value = get(key);

		if(value.empty()) {
			return fallback;
		}

		char* end = 0;
		errno = 0;
		const double parsed = std::strtod(value.c_str(), &end);

		if(end == value.c_str() || *end != 0 || errno == ERANGE || !std::isfinite(parsed)) {
			return fallback;
		}

		return (float)parsed;
	}

	int integer(const std::string& key, int fallback = 0) const {
		const float value = number(key, (float)fallback);
		if(value < -1000000.0f || value > 1000000.0f) {
			return fallback;
		}

		return (int)value;
	}

	bool enabled(const std::string& key, bool fallback = false) const {
		return integer(key, fallback ? 1 : 0) != 0;
	}
};

struct Ini {
	std::vector<Section> sections;
	std::string source;

	const Section* find(const std::string& name) const {
		const std::string wanted = lower(name);

		for(size_t i = 0; i < sections.size(); ++i) {
			if(sections[i].name == wanted) {
				return &sections[i];
			}
		}

		return 0;
	}

	bool load(const std::string& path, std::string* error = 0) {
		sections.clear();
		source = path;

		std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
		if(!stream) {
			if(error) {
				*error = "Arquivo nao encontrado: " + path;
			}
			return false;
		}

		const std::streamoff size = stream.tellg();

		if(size < 0 || size > 2 * 1024 * 1024) {
			if(error) {
				*error = "Configuracao excede 2 MB";
			}
			return false;
		}

		stream.seekg(0, std::ios::beg);
		std::string line;
		Section* active = 0;
		size_t lineCount = 0;
		size_t entryCount = 0;

		while(std::getline(stream, line)) {
			++lineCount;

			if(lineCount > 40000) {
				break;
			}

			if(lineCount == 1 && line.size() >= 3 &&
				(unsigned char)line[0] == 0xEF &&
				(unsigned char)line[1] == 0xBB &&
				(unsigned char)line[2] == 0xBF) {
				line.erase(0, 3);
			}

			line = trim(line);

			if(line.empty() || line[0] == '#' || line[0] == ';' ||
				line.substr(0, 2) == "//") {
				continue;
			}

			if(line[0] == '[' && line[line.size() - 1] == ']') {
				if(sections.size() >= 2048) {
					break;
				}

				Section newSection;
				newSection.name = lower(trim(line.substr(1, line.size() - 2)));
				sections.push_back(newSection);
				active = &sections.back();
				continue;
			}

			const size_t equal = line.find('=');

			if(active == 0 || equal == std::string::npos || entryCount >= 60000) {
				continue;
			}

			Entry entry;
			entry.key = lower(trim(line.substr(0, equal)));
			entry.value = trim(line.substr(equal + 1));

			if(!entry.key.empty()) {
				active->entries.push_back(entry);
				++entryCount;
			}
		}

		if(sections.empty()) {
			if(error) {
				*error = "Nao ha secoes [name] validas em: " + path;
			}
			return false;
		}

		return true;
	}
};

inline float bounded(float value, float low, float high) {
	return std::max(low, std::min(high, value));
}

struct DriveProfile {
	bool valid;
	bool automatic;
	int gears;
	float ratios[6];
	float reverseRatio;
	float differential;
	float idleRpm;
	float peakRpm;
	float maxRpm;
	float idleTorque;
	float peakTorque;
	float upRpm;
	float downRpm;
	float shiftSeconds;
	float transitionSeconds;
	float nextGearMinSpeed;
	float vehicleMass;

	DriveProfile() :
		valid(false),
		automatic(false),
		gears(6),
		reverseRatio(-6.43f),
		differential(3.66f),
		idleRpm(700.0f),
		peakRpm(1450.0f),
		maxRpm(3000.0f),
		idleTorque(580.0f),
		peakTorque(1650.0f),
		upRpm(2170.0f),
		downRpm(900.0f),
		shiftSeconds(0.5f),
		transitionSeconds(0.5f),
		nextGearMinSpeed(3.0f),
		vehicleMass(17000.0f) {
		const float defaults[6] = {
			6.98f,
			4.06f,
			2.74f,
			1.89f,
			1.31f,
			1.00f
		};

		for(int i = 0; i < 6; ++i) {
			ratios[i] = defaults[i];
		}
	}

	float gearRatio(int gear) const {
		if(gear < 0) {
			return std::fabs(reverseRatio);
		}

		const int index = std::max(0, std::min(gears - 1, gear - 1));
		return ratios[index];
	}
};

inline bool readDriveProfile(
	const Ini& ini,
	bool isAutomatic,
	float mass,
	DriveProfile& result
) {
	const Section* engine = ini.find("engine");
	const Section* gearbox = ini.find(
		isAutomatic ? "automatic_gearbox" : "manual_gearbox"
	);

	if(engine == 0 || gearbox == 0) {
		return false;
	}

	result = DriveProfile();
	result.valid = true;
	result.automatic = isAutomatic;

	result.gears = std::max(
		1,
		std::min(6, gearbox->integer("num_forward_ratios", 6))
	);

	for(int i = 0; i < result.gears; ++i) {
		char name[32];
		std::sprintf(name, "fwd_ratio_%d", i + 1);

		result.ratios[i] = bounded(
			gearbox->number(name, result.ratios[i]),
			0.2f,
			20.0f
		);
	}

	result.reverseRatio = bounded(
		gearbox->number("reverse_ratio", result.reverseRatio),
		-20.0f,
		-0.1f
	);

	const Section* differential = ini.find("differential");

	if(differential != 0) {
		result.differential = bounded(
			differential->number("gearratio", result.differential),
			0.5f,
			15.0f
		);
	}

	result.idleRpm = bounded(engine->number("idle_rpm", 700.0f), 300.0f, 1700.0f);
	result.peakRpm = bounded(
		engine->number("peak_rpm", 1450.0f),
		result.idleRpm + 100.0f,
		4500.0f
	);
	result.maxRpm = bounded(
		engine->number("max_rpm", 3000.0f),
		result.peakRpm + 50.0f,
		6000.0f
	);
	result.idleTorque = bounded(
		engine->number("idle_rpm_torque", 580.0f),
		100.0f,
		5000.0f
	);
	result.peakTorque = bounded(
		engine->number("peak_rpm_torque", 1650.0f),
		100.0f,
		5000.0f
	);
	result.shiftSeconds = bounded(
		gearbox->number("gear_change_time", 0.5f),
		0.05f,
		5.0f
	);
	result.transitionSeconds = bounded(
		gearbox->number("gear_transition_time", 0.5f),
		0.05f,
		5.0f
	);
	result.upRpm = bounded(
		gearbox->number("gear_up_rpm", 2170.0f),
		result.idleRpm + 100.0f,
		result.maxRpm
	);
	result.downRpm = bounded(
		gearbox->number("gear_down_rpm", 900.0f),
		result.idleRpm,
		result.upRpm - 50.0f
	);
	result.nextGearMinSpeed = bounded(
		gearbox->number("next_gear_min_speed", 3.0f),
		0.0f,
		50.0f
	);
	result.vehicleMass = bounded(mass, 5000.0f, 40000.0f);

	return true;
}

struct SoundPoint {
	float x;
	float y;
};

struct SoundSpec {
	std::string section;
	std::string file;
	std::string whenToPlay;
	float volumeMultiplier;
	float pitchMultiplier;
	bool idle;
	bool retarder;
	bool transmission;
	bool turbo;
	bool acceleratingOnly;
	bool selectedGears;
	int minGear;
	int maxGear;
	std::vector<SoundPoint> volumeCurve;
	std::vector<SoundPoint> pitchCurve;

	SoundSpec() :
		volumeMultiplier(1.0f),
		pitchMultiplier(1.0f),
		idle(false),
		retarder(false),
		transmission(false),
		turbo(false),
		acceleratingOnly(false),
		selectedGears(false),
		minGear(1),
		maxGear(6) {}

	static float curve(
		const std::vector<SoundPoint>& points,
		float x,
		float fallback
	) {
		if(points.empty()) {
			return fallback;
		}

		if(x <= points.front().x) {
			return points.front().y;
		}

		for(size_t i = 1; i < points.size(); ++i) {
			if(x <= points[i].x) {
				const SoundPoint& a = points[i - 1];
				const SoundPoint& b = points[i];
				const float denominator = b.x - a.x;

				if(std::fabs(denominator) < 0.0001f) {
					return b.y;
				}

				const float t = (x - a.x) / denominator;
				return a.y + (b.y - a.y) * t;
			}
		}

		return points.back().y;
	}

	float volume(float normalizedRpm) const {
		return bounded(
			curve(volumeCurve, normalizedRpm, 1.0f) * volumeMultiplier,
			0.0f,
			2.0f
		);
	}

	float pitch(float normalizedRpm) const {
		return bounded(
			curve(pitchCurve, normalizedRpm, 1.0f) * pitchMultiplier,
			0.2f,
			4.0f
		);
	}
};

inline void readCurve(
	const Section& section,
	char prefix,
	std::vector<SoundPoint>& points
) {
	for(int i = 1; i <= 16; ++i) {
		std::ostringstream keyX;
		keyX << prefix << i << 'x';

		std::ostringstream keyY;
		keyY << prefix << i << 'y';

		const std::string x = section.get(keyX.str());
		const std::string y = section.get(keyY.str());

		if(x.empty() || y.empty()) {
			continue;
		}

		SoundPoint point;
		point.x = section.number(keyX.str(), 0.0f);
		point.y = section.number(keyY.str(), 0.0f);
		points.push_back(point);
	}

	std::stable_sort(
		points.begin(),
		points.end(),
		[](const SoundPoint& a, const SoundPoint& b) {
			return a.x < b.x;
		}
	);
}

inline std::vector<SoundSpec> readSounds(const Ini& ini) {
	std::vector<SoundSpec> result;

	for(size_t i = 0; i < ini.sections.size(); ++i) {
		const Section& section = ini.sections[i];

		if(section.name.size() < 6 || section.name.substr(0, 5) != "sound") {
			continue;
		}

		bool numericSuffix = true;

		for(size_t j = 5; j < section.name.size(); ++j) {
			if(!std::isdigit((unsigned char)section.name[j])) {
				numericSuffix = false;
				break;
			}
		}

		if(!numericSuffix) {
			continue;
		}

		SoundSpec sound;
		sound.section = section.name;
		sound.file = section.get("file");

		if(sound.file.empty() || !safeRelative(sound.file)) {
			continue;
		}

		sound.whenToPlay = lower(section.get("whentoplay", "allCams"));
		sound.volumeMultiplier = bounded(
			section.number("volumemultiplier", 1.0f),
			0.0f,
			2.0f
		);
		sound.pitchMultiplier = bounded(
			section.number("pitchmultiplier", 1.0f),
			0.1f,
			4.0f
		);
		sound.idle = section.enabled("is_idle");
		sound.retarder = section.enabled("is_retarder");
		sound.transmission = section.enabled("is_transmission");
		sound.turbo = section.enabled("is_turbo");
		sound.acceleratingOnly = section.enabled("playonlywhenaccelerating");
		sound.selectedGears = section.enabled("playonlyonselectedgears");
		sound.minGear = section.integer("mingeartoplay", 1);
		sound.maxGear = section.integer("maxgeartoplay", 6);
		readCurve(section, 'v', sound.volumeCurve);
		readCurve(section, 'p', sound.pitchCurve);
		result.push_back(sound);
	}

	return result;
}

struct ModScripts {
	DriveProfile manual;
	DriveProfile automatic;
	std::vector<SoundSpec> sounds;
	std::string root;
	std::string soundDirectory;
	std::string soundConfigPath;
	std::string diagnostic;
	float soundMaxRpm;
	bool hasManual;
	bool hasAutomatic;

	ModScripts() :
		soundMaxRpm(3000.0f),
		hasManual(false),
		hasAutomatic(false) {}

	bool load(const std::string& entryPath) {
		*this = ModScripts();

		Ini descriptor;
		if(!descriptor.load(entryPath, &diagnostic)) {
			return false;
		}

		root = directory(entryPath);
		// The selected INI may live beside a named bus folder (baseDir).
		// Resolve all Script and O-400 sound paths inside that folder.
		const Section* baseConfig = descriptor.find("config");
		if(baseConfig != 0) {
			const std::string baseDir = normalize(baseConfig->get("basedir"));
			if(safeRelative(baseDir) && !baseDir.empty()) {
				const std::string candidate = root + baseDir + "/";
				if(exists(candidate + "Script/Motor.txt") ||
					exists(candidate + "script/Motor.txt")) {
					root = candidate;
				}
			}
		}
		const Section* config = descriptor.find("config");
		const Section* data = descriptor.find("data");

		if(config == 0) {
			// Direct selection of Cambio_A.txt or Cambio_M.txt is supported.
			const bool isAutomatic = descriptor.find("automatic_gearbox") != 0;
			const bool isManual = descriptor.find("manual_gearbox") != 0;

			if(isAutomatic) {
				hasAutomatic = readDriveProfile(
					descriptor, true, 17000.0f, automatic
				);
			}

			if(isManual) {
				hasManual = readDriveProfile(
					descriptor, false, 17000.0f, manual
				);
			}

			sounds = readSounds(descriptor);
			const Section* soundManager = descriptor.find("sound_manager");

			if(soundManager != 0) {
				soundMaxRpm = bounded(
					soundManager->number("max_rpm", 3000.0f),
					500.0f,
					6000.0f
				);
			}

			soundConfigPath = entryPath;
			soundDirectory = root;
			return hasAutomatic || hasManual || !sounds.empty();
		}

		const float mass = data != 0 ? data->number("mass1", 17000.0f) : 17000.0f;
		const std::string autoPath = findRelative(
			root, config->get("engine1auto")
		);
		const std::string manualPath = findRelative(
			root, config->get("engine1manual")
		);
		const std::string soundPath = findRelative(
			root, config->get("sounds1")
		);

		Ini script;

		if(!autoPath.empty() && script.load(autoPath)) {
			hasAutomatic = readDriveProfile(script, true, mass, automatic);
		}

		if(!manualPath.empty() && script.load(manualPath)) {
			hasManual = readDriveProfile(script, false, mass, manual);
		}

		if(!soundPath.empty() && script.load(soundPath)) {
			sounds = readSounds(script);
			soundConfigPath = soundPath;

			const Section* manager = script.find("sound_manager");
			if(manager != 0) {
				soundMaxRpm = bounded(
					manager->number("max_rpm", 3000.0f),
					500.0f,
					6000.0f
				);
			}
		}

		soundDirectory = root;

		if(!hasAutomatic && !hasManual && sounds.empty()) {
			diagnostic = "Nenhum cambio nem som encontrado nos caminhos [config]";
			return false;
		}

		std::ostringstream report;
		report << "Configuracao: " << entryPath
			<< "\nManual: " << (hasManual ? "sim" : "nao")
			<< "\nAutomatico: " << (hasAutomatic ? "sim" : "nao")
			<< "\nSons numerados: " << sounds.size();

		diagnostic = report.str();
		return true;
	}

	std::string locateSound(const SoundSpec& sound) const {
		if(!safeRelative(sound.file)) {
			return "";
		}

		const std::string normalized = normalize(sound.file);
		const std::string base = directory(soundConfigPath);
		const std::string candidates[] = {
			// Marcopolo O-400 packs keep the OGG files in engine-specific
			// directories, not always under the conventional Sound folder.
			root + "O-400/" + normalized,
			root + "O-400/Mercedes Benz O400/" + normalized,
			root + "O-400/motor externo/" + normalized,
			root + "O-400/motor interno/" + normalized,
			root + "Sound/" + normalized,
			root + "Sounds/" + normalized,
			root + "sound/" + normalized,
			root + normalized,
			base + normalized
		};

		for(size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
			if(exists(candidates[i])) {
				return candidates[i];
			}

			// Some sound entries omit the file suffix (e.g. "5i").
			if(exists(candidates[i] + ".ogg")) {
				return candidates[i] + ".ogg";
			}

			if(exists(candidates[i] + ".wav")) {
				return candidates[i] + ".wav";
			}
		}

		return "";
	}
};

} // namespace buscfg
