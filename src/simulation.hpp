#pragma once
// Simulacao autoritativa, C++11 sem dependencias Win32 (permite testes nativos).
#include <cmath>
#include <algorithm>
#include <stdint.h>
#include "protocol.h"
#include "bus_script.hpp"

namespace sim {
static const float PI = 3.14159265358979323846f;
static const float BUS_HALF_WIDTH=1.18f, BUS_HALF_LENGTH=3.78f;
static const float WHEEL_RADIUS=0.48f, SPRING_REST=0.88f;
static const float BUS_MASS=12000.0f, GRAVITY=9.81f;
static const float SUSPENSION_SAG=0.20f; // Compression at rest on level ground.
static const float STEER_WHEELBASE=5.0f, STEER_TRACK=2.08f;
static const float MAX_LATERAL_ACCEL = 3.25f;
static const float MAP_ROAD_SPACING = 60.0f;
static const int MAP_GRID_RADIUS = 34;
static const float MAP_HALF_EXTENT = MAP_GRID_RADIUS * MAP_ROAD_SPACING;
static const int MAP_RENDER_RADIUS = 5;

struct Box {
	float x;
	float z;
	float hx;
	float hz;
};

inline bool insideMap(float x, float z, float margin = 0.0f) {
	const float limit = MAP_HALF_EXTENT - margin;
	return std::fabs(x) <= limit && std::fabs(z) <= limit;
}

inline int roadIndex(float coordinate) {
	return (int)std::floor(coordinate / MAP_ROAD_SPACING);
}

inline float clamp(float x,float mn,float mx) {return std::max(mn,std::min(mx,x));}
inline float terrain(float x,float z) {
    // Asfalto plano; lombada suave para exercitar os raycasts.
    if(std::fabs(x)<7.0f && z>30.0f && z<39.0f) {
        const float t=(z-30.0f)/9.0f;
        return 0.16f*std::sin(t*PI);
    }
    return 0.0f;
}
inline float wheelOffsetX(int wheel) {return (wheel&1)?STEER_TRACK*0.5f:-STEER_TRACK*0.5f;}
inline float wheelOffsetZ(int wheel) {return wheel<2?2.70f:(wheel<4?-1.20f:-2.65f);}

inline Box building(int i,int j) {
	Box b = {
		MAP_ROAD_SPACING * (float)i + MAP_ROAD_SPACING * 0.5f,
		MAP_ROAD_SPACING * (float)j + MAP_ROAD_SPACING * 0.5f,
		10.0f,
		11.0f
	};
	return b;
}
inline bool overlaps(float ax,float az,float ahx,float ahz,float yaw,
                     float bx,float bz,float bhx,float bhz,float byaw) {
    // SAT em quatro eixos: duas OBB 2D no plano XZ.
    const float ac=std::cos(yaw),as=std::sin(yaw);
    const float bc=std::cos(byaw),bs=std::sin(byaw);
    const float axes[4][2]={{ac,-as},{as,ac},{bc,-bs},{bs,bc}};
    const float dx=bx-ax,dz=bz-az;
    const float au[2][2]={{ac,-as},{as,ac}};
    const float bu[2][2]={{bc,-bs},{bs,bc}};
    for(int i=0;i<4;i++) {
        const float nx=axes[i][0],nz=axes[i][1];
        const float ra=ahx*std::fabs(nx*au[0][0]+nz*au[0][1])
                      +ahz*std::fabs(nx*au[1][0]+nz*au[1][1]);
        const float rb=bhx*std::fabs(nx*bu[0][0]+nz*bu[0][1])
                      +bhz*std::fabs(nx*bu[1][0]+nz*bu[1][1]);
        if(std::fabs(dx*nx+dz*nz)>=ra+rb) return false;
    }
    return true;
}
inline bool collidesBuildings(float x, float z, float heading) {
	if(!insideMap(x, z, BUS_HALF_LENGTH + 2.0f)) {
		return true;
	}

	const int i0 = (int)std::floor((x - 30.0f) / MAP_ROAD_SPACING);
	const int j0 = (int)std::floor((z - 30.0f) / MAP_ROAD_SPACING);

	for(int i = i0; i <= i0 + 1; ++i) {
		for(int j = j0; j <= j0 + 1; ++j) {
			const Box block = building(i, j);
			const bool collision = overlaps(
				x, z,
				BUS_HALF_WIDTH, BUS_HALF_LENGTH, heading,
				block.x, block.z,
				block.hx, block.hz, 0.0f
			);

			if(collision) {
				return true;
			}
		}
	}

	return false;
}

struct Stop {
	float x;
	float z;
	int waiting;
};
inline Stop stop(int i) {
	// Stops near the origin are preserved; additional stops extend into
	// neighborhoods across the 4.08 km wide playable area.
	static const float positions[][2] = {
		{ 4.9f, 18.0f },
		{ 4.9f, 78.0f },
		{ 4.9f, 138.0f },
		{-4.9f, -18.0f },
		{-4.9f, -78.0f },
		{-4.9f, -138.0f },
		{ 4.9f, 378.0f },
		{ 4.9f, 798.0f },
		{ 4.9f, 1218.0f },
		{ 4.9f, 1638.0f },
		{604.9f, 1638.0f },
		{1204.9f, 1638.0f },
		{1804.9f, 1638.0f },
		{1804.9f, 1018.0f },
		{1804.9f, 418.0f },
		{1804.9f, -182.0f },
		{-4.9f, -378.0f },
		{-4.9f, -798.0f },
		{-4.9f, -1218.0f },
		{-4.9f, -1638.0f },
		{-604.9f, -1638.0f },
		{-1204.9f, -1638.0f },
		{-1804.9f, -1638.0f },
		{-1804.9f, -1018.0f },
		{-1804.9f, -418.0f },
		{-1804.9f, 182.0f },
		{-1204.9f, 618.0f },
		{-604.9f, 1018.0f }
	};

	const int index = (i % 28 + 28) % 28;
	Stop result = {
		positions[index][0],
		positions[index][1],
		8
	};
	return result;
}
static const int STOP_COUNT = 28;

// One raycast per wheel, along world -Y, against the heightfield road.
struct RayHit {
    bool hit;
    float distance, height, nx, ny, nz;
    RayHit():hit(false),distance(0),height(0),nx(0),ny(1),nz(0){}
};
inline RayHit raycastGround(float x,float originY,float z,float maxLength) {
    RayHit r;
    r.height=terrain(x,z);
    r.distance=originY-r.height;
    r.hit=(r.distance<=maxLength); // If chassis penetrates road, preserve contact.
    if(!r.hit)return r;
    const float e=0.10f;
    const float dx=(terrain(x+e,z)-terrain(x-e,z))/(2.0f*e);
    const float dz=(terrain(x,z+e)-terrain(x,z-e))/(2.0f*e);
    const float inv=1.0f/std::sqrt(1.0f+dx*dx+dz*dz);
    r.nx=-dx*inv;r.ny=inv;r.nz=-dz*inv;
    return r;
}
inline float steerLimit(float signedSpeed) {
	const float speed = std::fabs(signedSpeed);
	return 0.53f / (1.0f + 0.0035f * speed * speed);
}

// Approximate OMSI 2 keyboard handling, not a reproduction of OMSI's code.
// Allows more wheel lock for low-speed parking, with speed-sensitive steering.
inline float steerLimit(const BusState& bus) {
	if(bus.steeringMode == STEERING_OMSI_APPROX) {
		const float speed = std::fabs(bus.speed);
		return 0.70f / (1.0f + 0.0038f * speed * speed);
	}

	return steerLimit(bus.speed);
}

inline float advanceSteering(
	const BusState& bus,
	float current,
	float target,
	float dt
) {
	if(bus.steeringMode == STEERING_OMSI_APPROX) {
		const float speed = std::fabs(bus.speed);
		const bool returningToCenter = std::fabs(target) < 0.01f;
		const float baseRate = returningToCenter ? 2.30f : 1.80f;
		const float rate = baseRate / (1.0f + 0.018f * speed);
		const float maximumChange = rate * dt;
		const float difference = clamp(
			target - current,
			-maximumChange,
			maximumChange
		);

		return clamp(current + difference, -1.0f, 1.0f);
	}

	const float responsiveness = clamp(dt * 3.8f, 0.0f, 1.0f);
	return clamp(
		current + (target - current) * responsiveness,
		-1.0f,
		1.0f
	);
}

inline float wheelSteerAngle(const BusState& bus, int wheel) {
	if(wheel < 0 || wheel >= 2) {
		return 0.0f;
	}

	const float base = clamp(bus.steer, -1.0f, 1.0f) * steerLimit(bus);
	if(std::fabs(base) < 0.00001f) {
		return 0.0f;
	}

	const float radius = STEER_WHEELBASE / std::tan(std::fabs(base));
	const float side = base > 0.0f ? 1.0f : -1.0f;
	const float insideRadius = std::max(
		0.5f,
		radius - side * wheelOffsetX(wheel)
	);

	return side * std::atan(STEER_WHEELBASE / insideRadius);
}
inline float sprungWeight(int wheel) {
    // Front axle 43%, middle 24%, rearmost 33%; their weighted lever arms
    // sum to (approximately) zero about the modeled center of gravity.
    return wheel<2?0.215f:(wheel<4?0.120f:0.165f);
}
inline float springStiffness(int wheel) {
    return BUS_MASS*GRAVITY*sprungWeight(wheel)/SUSPENSION_SAG;
}
inline float springDamping(int wheel) {
    // Near 45% critical damping for each wheel's sprung mass.
    return 0.90f*std::sqrt(springStiffness(wheel)*BUS_MASS*sprungWeight(wheel));
}
inline float wheelMountHeight(const BusState& b,int i) {
    // Must match row-vector DirectX rotations: +pitch lowers front,
    // +roll lifts right side.
    return b.y-0.40f-b.pitch*wheelOffsetZ(i)+b.roll*wheelOffsetX(i);
}
inline RayHit wheelRay(const BusState& b,int i) {
    const float lx=wheelOffsetX(i),lz=wheelOffsetZ(i);
    const float c=std::cos(b.heading),s=std::sin(b.heading);
    return raycastGround(b.x+c*lx+s*lz,wheelMountHeight(b,i),
                         b.z-s*lx+c*lz,SPRING_REST+WHEEL_RADIUS);
}
inline float wheelCompression(const RayHit& ray){
    return ray.hit?clamp(SPRING_REST+WHEEL_RADIUS-ray.distance,0.0f,SPRING_REST):0.0f;
}
struct Dynamics {
    BusState b;
    float verticalSpeed,pitchSpeed,rollSpeed,yawRate,lateralSpeed;
    float throttle,steering,brake,boardingSeconds;
    float wheelTravel[6],wheelForce[6];
    bool wheelContact[6];

	buscfg::DriveProfile driveAutomatic;
	buscfg::DriveProfile driveManual;
	float shiftTimer;
    int boardedAtStop;
    uint32_t lastFlags;
    bool manualGear;
    Dynamics():verticalSpeed(0),pitchSpeed(0),rollSpeed(0),yawRate(0),lateralSpeed(0),
        throttle(0),steering(0),brake(0),boardingSeconds(0),boardedAtStop(0),
        lastFlags(0),manualGear(false),shiftTimer(0.0f) {
        memset(&b,0,sizeof(b));
        b.y=0.40f+WHEEL_RADIUS+SPRING_REST-SUSPENSION_SAG;
        b.gear=1;b.rpm=700;
        for(int i=0;i<6;i++){
            wheelTravel[i]=SUSPENSION_SAG;
            wheelForce[i]=BUS_MASS*GRAVITY*sprungWeight(i);
            wheelContact[i]=true;
        }
    }
};
inline void setDriveProfiles(
	Dynamics& d,
	const buscfg::DriveProfile& automatic,
	const buscfg::DriveProfile& manual
) {
	d.driveAutomatic = automatic;
	d.driveManual = manual;
	d.manualGear = !automatic.valid && manual.valid;
	d.b.gear = 1;
	d.shiftTimer = 0.0f;

	if(automatic.valid) {
		d.b.rpm = automatic.idleRpm;
	} else if(manual.valid) {
		d.b.rpm = manual.idleRpm;
	}
}

inline const buscfg::DriveProfile* activeDrive(const Dynamics& d) {
	if(d.manualGear && d.driveManual.valid) {
		return &d.driveManual;
	}

	if(!d.manualGear && d.driveAutomatic.valid) {
		return &d.driveAutomatic;
	}

	if(d.driveManual.valid) {
		return &d.driveManual;
	}

	if(d.driveAutomatic.valid) {
		return &d.driveAutomatic;
	}

	return 0;
}

inline float engineTorque(
	const buscfg::DriveProfile& config,
	float rpm
) {
	const float low = config.idleRpm;
	const float peak = config.peakRpm;

	if(rpm <= peak) {
		const float t = clamp(
			(rpm - low) / std::max(1.0f, peak - low),
			0.0f,
			1.0f
		);

		return config.idleTorque +
			(config.peakTorque - config.idleTorque) * t;
	}

	const float t = clamp(
		(rpm - peak) / std::max(1.0f, config.maxRpm - peak),
		0.0f,
		1.0f
	);

	return config.peakTorque * (1.0f - 0.40f * t);
}

inline float drivenRpm(
	const buscfg::DriveProfile& profile,
	int gear,
	float speed
) {
	const float circumference = 2.0f * PI * 0.55f;
	const float revolutionsPerMinute =
		std::fabs(speed) / circumference * 60.0f;

	return revolutionsPerMinute * profile.gearRatio(gear) *
		profile.differential;
}

// Reset executed by the authoritative server; preserve the network identity.
inline void resetOrigin(Dynamics& dynamics) {
	const uint32_t savedId = dynamics.b.id;
	const uint32_t savedSteeringMode = dynamics.b.steeringMode;
	const buscfg::DriveProfile savedAuto = dynamics.driveAutomatic;
	const buscfg::DriveProfile savedManual = dynamics.driveManual;
	const bool savedManualMode = dynamics.manualGear;

	dynamics = Dynamics();
	setDriveProfiles(dynamics, savedAuto, savedManual);
	dynamics.manualGear = savedManualMode;
	dynamics.b.id = savedId;
	dynamics.b.steeringMode = savedSteeringMode;
	dynamics.b.x = 0.0f;
	dynamics.b.z = 0.0f;
	dynamics.b.heading = 0.0f;
}

inline void input(
	Dynamics& d,
	float throttle,
	float steering,
	float brake,
	uint32_t flags
) {
	d.throttle = clamp(throttle, -1.0f, 1.0f);
	d.steering = clamp(steering, -1.0f, 1.0f);
	d.brake = clamp(brake, 0.0f, 1.0f);

	const uint32_t pressed = flags & ~d.lastFlags;

	if((pressed & INPUT_RESET_ORIGIN) != 0) {
		resetOrigin(d);
		d.lastFlags = flags;
		return;
	}

	if((pressed & INPUT_TOGGLE_OMSI_STEERING) != 0) {
		if(d.b.steeringMode == STEERING_OMSI_APPROX) {
			d.b.steeringMode = STEERING_CLASSIC;
		} else {
			d.b.steeringMode = STEERING_OMSI_APPROX;
		}
	}

	if((pressed & INPUT_TOGGLE_DOOR) != 0) {
		if(std::fabs(d.b.speed) < 0.5f) {
			d.b.door = d.b.door > 0.5f ? 0.0f : 1.0f;
		}
	}

	if((pressed & INPUT_AUTO_GEAR) != 0) {
		if(d.driveAutomatic.valid || !d.driveManual.valid) {
			d.manualGear = false;
		}
	}

	if((pressed & INPUT_GEAR_UP) != 0) {
		d.manualGear = true;
		const buscfg::DriveProfile* profile = activeDrive(d);
		const int maximum = profile != 0 ? profile->gears : 6;
		const int next = std::min(maximum, d.b.gear + 1);

		if(next != d.b.gear) {
			d.b.gear = next;
			d.shiftTimer = profile != 0 ? profile->shiftSeconds : 0.0f;
		}
	}

	if((pressed & INPUT_GEAR_DOWN) != 0) {
		d.manualGear = true;
		const buscfg::DriveProfile* profile = activeDrive(d);
		const int next = std::max(1, d.b.gear - 1);

		if(next != d.b.gear) {
			d.b.gear = next;
			d.shiftTimer = profile != 0 ? profile->shiftSeconds : 0.0f;
		}
	}

	d.lastFlags = flags;
}

inline float visualTravel(const BusState& b,int i) {
    if(i<0||i>=6)return 0.0f;
    return wheelCompression(wheelRay(b,i));
}
inline void suspension(Dynamics& d,float dt,float forwardAcceleration=0) {
    float normal[6],compression[6],spring[6]={0,0,0,0,0,0};
    float force=-BUS_MASS*GRAVITY,pitchTorque=0,rollTorque=0;
    // Gather all ray hits before computing anti-roll coupling.
    for(int i=0;i<6;i++){
        const RayHit hit=wheelRay(d.b,i);
        d.wheelContact[i]=hit.hit;
        compression[i]=wheelCompression(hit);
        d.wheelTravel[i]=compression[i];
        normal[i]=hit.ny;
        if(!hit.hit || compression[i]<=0.0f)continue;
        const float pointVelocity=d.verticalSpeed-
            d.pitchSpeed*wheelOffsetZ(i)+d.rollSpeed*wheelOffsetX(i);
        float f=springStiffness(i)*compression[i]-
            springDamping(i)*pointVelocity;
        // Progressive bump stop prevents suspension bottoming at full travel.
        if(compression[i]>SPRING_REST-0.12f){
            const float bottom=compression[i]-(SPRING_REST-0.12f);
            f+=350000.0f*bottom*bottom;
        }
        spring[i]=clamp(f,0.0f,180000.0f);
    }
    for(int axle=0;axle<3;axle++){
        const int left=axle*2,right=left+1;
        if(!d.wheelContact[left] || !d.wheelContact[right])continue;
        const float antiRoll=27000.0f*(compression[left]-compression[right]);
        spring[left]=std::max(0.0f,spring[left]+antiRoll);
        spring[right]=std::max(0.0f,spring[right]-antiRoll);
    }
    for(int i=0;i<6;i++){
        d.wheelForce[i]=spring[i];
        force+=spring[i]*normal[i];
        pitchTorque-=spring[i]*wheelOffsetZ(i);
        rollTorque+=spring[i]*wheelOffsetX(i);
    }
    // Body inertia under acceleration and cornering, not arbitrary tilt.
    pitchTorque-=BUS_MASS*forwardAcceleration*0.76f;
    rollTorque+=BUS_MASS*d.b.speed*d.yawRate*0.96f;
    pitchTorque-=d.b.pitch*42000.0f+d.pitchSpeed*31000.0f;
    rollTorque-=d.b.roll*42000.0f+d.rollSpeed*26000.0f;
    d.verticalSpeed=clamp(d.verticalSpeed+(force/BUS_MASS)*dt,-7.0f,7.0f);
    d.b.y+=d.verticalSpeed*dt;
    d.pitchSpeed=clamp(d.pitchSpeed+pitchTorque/95000.0f*dt,-0.70f,0.70f);
    d.rollSpeed=clamp(d.rollSpeed+rollTorque/42000.0f*dt,-0.70f,0.70f);
    d.b.pitch=clamp(d.b.pitch+d.pitchSpeed*dt,-0.16f,0.16f);
    d.b.roll=clamp(d.b.roll+d.rollSpeed*dt,-0.16f,0.16f);
    // Safety floor only; raycast spring/damper normally supports the bus.
    const float minHeight=terrain(d.b.x,d.b.z)+0.84f;
    if(d.b.y<minHeight){
        d.b.y=minHeight;
        d.verticalSpeed=std::max(d.verticalSpeed,0.0f);
    }
}
inline void step(Dynamics& d,float dt) {
    if(dt<=0.0f)return;
    // Prevent explosive integration if the host experiences a frame stall.
    const float frameDt=clamp(dt,0.0f,1.0f/30.0f);
    BusState& b=d.b;
	b.steer = advanceSteering(b, b.steer, d.steering, frameDt);
    const float oldSpeed=b.speed;
    // Previous tick's raycast normal loads determine available tire grip.
    // Airborne wheels cannot generate drive, braking or cornering force.
    float contactLoad=0.0f;
    for(int i=0;i<6;i++)if(d.wheelContact[i])contactLoad+=d.wheelForce[i];
    const float grip=clamp(contactLoad/(BUS_MASS*GRAVITY),0.0f,1.0f);
    const float traction=(b.door>0.5f)?0.0f:grip;
	const buscfg::DriveProfile* drive = activeDrive(d);
	const float acceleration = d.throttle >= 0.0f ? 2.6f : 1.8f;
	float motor = d.throttle * acceleration * traction;

	if(drive != 0) {
		const float maxRatio = drive->gearRatio(
			d.throttle < 0.0f ? -1 : b.gear
		);

		const float rpmFromWheels = drivenRpm(
			*drive,
			b.gear,
			b.speed
		);
		const float freeRevTarget =
			drive->idleRpm + std::fabs(d.throttle) * 380.0f;
		const float requestedRpm = clamp(
			std::max(freeRevTarget, rpmFromWheels),
			drive->idleRpm,
			drive->maxRpm
		);

		b.rpm += (requestedRpm - b.rpm) *
			clamp(frameDt * 5.0f, 0.0f, 1.0f);
		b.rpm = clamp(b.rpm, drive->idleRpm, drive->maxRpm);

		const float torque = engineTorque(*drive, b.rpm);
		const float wheelForce =
			torque * maxRatio * drive->differential * 0.82f / 0.55f;

		const float driveAcceleration = clamp(
			wheelForce / drive->vehicleMass,
			0.0f,
			3.3f
		);

		motor = d.throttle * driveAcceleration * traction;

		if(d.shiftTimer > 0.0f) {
			d.shiftTimer = std::max(0.0f, d.shiftTimer - frameDt);
			motor *= 0.15f;
		}
	}
    const float speedAbs=std::fabs(b.speed);
    const float drag=0.0065f*b.speed*speedAbs;
    const float braking=(d.brake*6.5f*grip+0.11f)*(b.speed>0?1.0f:(b.speed<0?-1.0f:0.0f));
    b.speed+= (motor-drag-braking)*frameDt;
    if((oldSpeed>0&&b.speed<0&&d.throttle>=0) ||
       (oldSpeed<0&&b.speed>0&&d.throttle<=0))b.speed=0.0f;
    if(std::fabs(b.speed)<0.03f && std::fabs(d.throttle)<0.01f)b.speed=0.0f;
    b.speed=clamp(b.speed,-5.5f,22.0f);
    const float accelReal=(b.speed-oldSpeed)/frameDt;

    // Relaxed single-track yaw response: slower bus steering and realistic
    // high-speed understeer, with grip-limited lateral acceleration.
	const bool omsiMode = b.steeringMode == STEERING_OMSI_APPROX;
	const float centerAngle = b.steer * steerLimit(b);
	const float speedSquared = b.speed * b.speed;
	const float understeerRate = omsiMode ? 0.0014f : 0.0020f;
	const float understeer = 1.0f + understeerRate * speedSquared;
	float desiredYaw = b.speed * std::tan(centerAngle) /
		(STEER_WHEELBASE * understeer);

	const float yawGrip = MAX_LATERAL_ACCEL * grip /
		std::max(std::fabs(b.speed), 1.0f);

	desiredYaw = clamp(desiredYaw, -yawGrip, yawGrip);

	const float yawResponse = omsiMode ? 4.8f : 3.5f;
	d.yawRate += (desiredYaw - d.yawRate) *
		clamp(frameDt * yawResponse, 0.0f, 1.0f);
	d.yawRate = clamp(d.yawRate, -yawGrip, yawGrip);
    d.lateralSpeed+=(-4.5f*d.lateralSpeed-b.speed*d.yawRate)*frameDt;
    d.lateralSpeed=clamp(d.lateralSpeed,-2.0f,2.0f);
    b.heading+=d.yawRate*frameDt;
    if(b.heading>PI)b.heading-=2.0f*PI;
    if(b.heading<-PI)b.heading+=2.0f*PI;
    const float sine=std::sin(b.heading),cosine=std::cos(b.heading);
    const float oldX=b.x,oldZ=b.z;
    b.x+=(sine*b.speed+cosine*d.lateralSpeed)*frameDt;
    b.z+=(cosine*b.speed-sine*d.lateralSpeed)*frameDt;
    if(collidesBuildings(b.x,b.z,b.heading)){
        b.x=oldX;b.z=oldZ;b.speed*=-0.08f;
        d.yawRate=0.0f;d.lateralSpeed=0.0f;
    }
    b.wheelRotation+=b.speed/WHEEL_RADIUS*frameDt;
    if(std::fabs(b.wheelRotation)>10000.0f)
        b.wheelRotation=std::fmod(b.wheelRotation,2.0f*PI);
	const float kmh = std::fabs(b.speed) * 3.6f;

	if(drive != 0 && !d.manualGear && drive->automatic) {
		if(d.shiftTimer <= 0.0f) {
			if(
				d.throttle > 0.05f &&
				b.gear < drive->gears &&
				b.rpm >= drive->upRpm &&
				std::fabs(b.speed) >= drive->nextGearMinSpeed
			) {
				++b.gear;
				d.shiftTimer = drive->shiftSeconds;
			} else if(
				b.gear > 1 &&
				b.rpm <= drive->downRpm
			) {
				--b.gear;
				d.shiftTimer = drive->shiftSeconds;
			}
		}
	} else if(drive == 0) {
		// Built-in fallback when no external gearbox profile is installed.
		if(!d.manualGear) {
			if(d.throttle > 0.0f && b.gear < 6 && kmh > b.gear * 18.0f) {
				++b.gear;
			}

			if(b.gear > 1 && kmh < (b.gear - 1) * 15.0f) {
				--b.gear;
			}
		}

		b.rpm = clamp(
			700.0f + kmh * 95.0f / std::max(1, b.gear) +
			std::fabs(d.throttle) * 400.0f,
			700.0f,
			3400.0f
		);
	}
    suspension(d,frameDt,accelReal);
    Stop s=stop((int)b.nextStop);
    const float dx=b.x-s.x,dz=b.z-s.z;
    const float distance2=dx*dx+dz*dz;
    if(distance2<64.0f && std::fabs(b.speed)<0.3f && b.door>0.5f) {
        d.boardingSeconds+=dt;
        if(d.boardingSeconds>=1.0f && d.boardedAtStop<6) {
            d.boardingSeconds=0;
            if(b.passengers<40)++b.passengers;
            ++d.boardedAtStop;
        }
    } else d.boardingSeconds=0;
    // Apos pelo menos um embarque, sair do ponto avanca a rota.
    if(distance2>196.0f && d.boardedAtStop>0) {
        d.boardedAtStop=0;
        b.nextStop=(b.nextStop+1)%STOP_COUNT;
        if(b.passengers>8)b.passengers-=4; // desembarque simplificado
    }
}
inline void separate(Dynamics& a,Dynamics& b) {
    if(!overlaps(a.b.x,a.b.z,BUS_HALF_WIDTH,BUS_HALF_LENGTH,a.b.heading,
                 b.b.x,b.b.z,BUS_HALF_WIDTH,BUS_HALF_LENGTH,b.b.heading))return;
    const float dx=a.b.x-b.b.x,dz=a.b.z-b.b.z;
    const float length=std::sqrt(dx*dx+dz*dz);
    const float nx=length>0.001f?dx/length:1, nz=length>0.001f?dz/length:0;
    a.b.x+=nx*0.12f;a.b.z+=nz*0.12f;
    b.b.x-=nx*0.12f;b.b.z-=nz*0.12f;
    a.b.speed*=0.45f;b.b.speed*=0.45f;
}
} // namespace sim
