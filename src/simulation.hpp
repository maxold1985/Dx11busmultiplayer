#pragma once
// Simulacao autoritativa, C++11 sem dependencias Win32 (permite testes nativos).
#include <cmath>
#include <algorithm>
#include <stdint.h>
#include "protocol.h"

namespace sim {
static const float PI = 3.14159265358979323846f;
static const float BUS_HALF_WIDTH=1.18f, BUS_HALF_LENGTH=3.78f;
static const float WHEEL_RADIUS=0.48f, SPRING_REST=0.88f;
struct Box { float x,z,hx,hz; };

inline float clamp(float x,float mn,float mx) {return std::max(mn,std::min(mx,x));}
inline float terrain(float x,float z) {
    // Asfalto plano; lombada suave para exercitar os raycasts.
    if(std::fabs(x)<7.0f && z>30.0f && z<39.0f) {
        const float t=(z-30.0f)/9.0f;
        return 0.16f*std::sin(t*PI);
    }
    return 0.0f;
}
inline float wheelOffsetX(int wheel) {return (wheel&1)?1.04f:-1.04f;}
inline float wheelOffsetZ(int wheel) {return wheel<2?2.70f:(wheel<4?-1.20f:-2.65f);}

inline Box building(int i,int j) {
    Box b={30.0f+60.0f*i,30.0f+60.0f*j,10.0f,11.0f};return b;
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
inline bool collidesBuildings(float x,float z,float heading) {
    int i0=(int)std::floor((x-30.0f)/60.0f);
    int j0=(int)std::floor((z-30.0f)/60.0f);
    for(int i=i0;i<=i0+1;i++)for(int j=j0;j<=j0+1;j++) {
        Box b=building(i,j);
        if(overlaps(x,z,BUS_HALF_WIDTH,BUS_HALF_LENGTH,heading,
                    b.x,b.z,b.hx,b.hz,0))return true;
    }
    return false;
}
struct Stop { float x,z; int waiting; };
inline Stop stop(int i) {
    const float p[6][2]={{4.9f,18.0f},{4.9f,78.0f},{4.9f,138.0f},
                         {-4.9f,-18.0f},{-4.9f,-78.0f},{-4.9f,-138.0f}};
    Stop s={p[i%6][0],p[i%6][1],8};return s;
}
static const int STOP_COUNT=6;
struct Dynamics {
    BusState b;
    float verticalSpeed,pitchSpeed,rollSpeed,throttle,steering,brake,boardingSeconds;
    float wheelTravel[6];
    int boardedAtStop;
    uint32_t lastFlags;
    Dynamics():verticalSpeed(0),pitchSpeed(0),rollSpeed(0),throttle(0),steering(0),brake(0),boardingSeconds(0),boardedAtStop(0),lastFlags(0) {
        memset(&b,0,sizeof(b));b.y=1.6f;b.gear=1;b.rpm=700;for(int i=0;i<6;i++)wheelTravel[i]=0;
    }
};
inline void input(Dynamics& d,float t,float steer,float brake,uint32_t flags) {
    d.throttle=clamp(t,-1,1);
    d.steering=clamp(steer,-1,1);
    d.brake=clamp(brake,0,1);
    const uint32_t pressed=flags & ~d.lastFlags;
    if(pressed&INPUT_TOGGLE_DOOR) {
        if(std::fabs(d.b.speed)<0.5f) d.b.door=d.b.door>0.5f?0.0f:1.0f;
    }
    if(pressed&INPUT_GEAR_UP) d.b.gear=std::min(6,d.b.gear+1);
    if(pressed&INPUT_GEAR_DOWN) d.b.gear=std::max(1,d.b.gear-1);
    d.lastFlags=flags;
}
inline float visualTravel(const BusState& b,int i) {
    const float lx=wheelOffsetX(i),lz=wheelOffsetZ(i);
    const float wx=b.x+std::cos(b.heading)*lx+std::sin(b.heading)*lz;
    const float wz=b.z-std::sin(b.heading)*lx+std::cos(b.heading)*lz;
    const float origin=b.y-0.4f+b.pitch*lz+b.roll*lx;
    return clamp(SPRING_REST-(origin-terrain(wx,wz)-WHEEL_RADIUS),0,SPRING_REST);
}
inline void suspension(Dynamics& d,float dt) {
    float force=-10000.0f*9.81f;
    float pitchTorque=0,rollTorque=0;
    for(int i=0;i<6;i++) {
        float lx=wheelOffsetX(i),lz=wheelOffsetZ(i);
        const float wx=d.b.x+std::cos(d.b.heading)*lx+std::sin(d.b.heading)*lz;
        const float wz=d.b.z-std::sin(d.b.heading)*lx+std::cos(d.b.heading)*lz;
        const float origin=d.b.y-0.4f+d.b.pitch*lz+d.b.roll*lx;
        // Raio vertical para terreno: primeiro contato com a roda.
        const float distance=origin-terrain(wx,wz)-WHEEL_RADIUS;
        const float compression=clamp(SPRING_REST-distance,0,SPRING_REST);
        float spring=0;
        if(compression>0.0f && distance<SPRING_REST) {
            const float contactVelocity=d.verticalSpeed+d.pitchSpeed*lz+d.rollSpeed*lx;
            spring=std::max(0.0f,compression*90000.0f-contactVelocity*9500.0f);
        }
        d.wheelTravel[i]=compression;
        force+=spring;
        pitchTorque-=spring*lz;
        rollTorque+=spring*lx;
    }
    d.verticalSpeed=clamp(d.verticalSpeed+force/10000.0f*dt,-15,15);
    d.b.y+=d.verticalSpeed*dt;
    d.pitchSpeed+=(pitchTorque/150000.0f-d.b.pitch*7.0f-d.pitchSpeed*4.0f)*dt;
    d.rollSpeed+=(rollTorque/65000.0f-d.b.roll*7.0f-d.rollSpeed*4.0f)*dt;
    d.b.pitch=clamp(d.b.pitch+d.pitchSpeed*dt,-0.20f,0.20f);
    d.b.roll=clamp(d.b.roll+d.rollSpeed*dt,-0.20f,0.20f);
    if(d.b.y<0.85f) {d.b.y=0.85f;d.verticalSpeed=std::max(0.0f,d.verticalSpeed);}
}
inline void step(Dynamics& d,float dt) {
    BusState& b=d.b;
    b.steer+=(d.steering-b.steer)*clamp(dt*5.0f,0,1);
    const float traction=(b.door>0.5f)?0.0f:1.0f;
    const float acceleration=(d.throttle>=0?3.4f:2.0f);
    b.speed+=d.throttle*acceleration*traction*dt;
    b.speed-=b.speed*(0.08f+d.brake*3.5f)*dt;
    b.speed=clamp(b.speed,-5.5f,22.0f);
    b.heading+=std::tan(b.steer*0.47f)*b.speed/6.2f*dt;
    b.x+=std::sin(b.heading)*b.speed*dt;
    b.z+=std::cos(b.heading)*b.speed*dt;
    if(collidesBuildings(b.x,b.z,b.heading)) {
        b.x-=std::sin(b.heading)*b.speed*dt;
        b.z-=std::cos(b.heading)*b.speed*dt;
        b.speed*=-0.1f;
    }
    b.wheelRotation+=b.speed/WHEEL_RADIUS*dt;
    const float kmh=std::fabs(b.speed)*3.6f;
    if(d.throttle>0.0f && b.gear<6 && kmh>b.gear*18.0f)++b.gear;
    if(b.gear>1 && kmh<(b.gear-1)*15.0f)--b.gear;
    b.rpm=clamp(700.0f+kmh*95.0f/std::max(1,b.gear)+std::fabs(d.throttle)*400.0f,700,3400);
    suspension(d,dt);
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
