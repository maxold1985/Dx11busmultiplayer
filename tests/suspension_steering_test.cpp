#include "protocol_stub.h"
#define BUS_SIM_TEST 1
#include "../src/simulation.hpp"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static bool closeTo(float a,float b,float eps){return fabsf(a-b)<=eps;}
static void frames(sim::Dynamics& d,int steps,float dt=1.0f/60.0f){
    for(int i=0;i<steps;i++)sim::step(d,dt);
}
static void parkedAndLevel(){
    sim::Dynamics d;
    const float y0=d.b.y;
    frames(d,600);
    assert(closeTo(d.b.y,y0,0.07f));
    assert(fabsf(d.b.pitch)<0.015f);
    assert(fabsf(d.b.roll)<0.015f);
    for(int i=0;i<6;i++){
        assert(d.wheelContact[i]);
        assert(d.wheelForce[i]>0.0f);
        assert(d.wheelTravel[i]>0.05f&&d.wheelTravel[i]<0.40f);
    }
    assert(fabsf(sim::wheelSteerAngle(d.b,2))<0.00001f);
    printf("idle: height %.3f pitch %.4f roll %.4f\n",d.b.y,d.b.pitch,d.b.roll);
}
static void compressionAndRays(){
    sim::Dynamics d;
    d.b.y+=0.25f;
    frames(d,600);
    assert(fabsf(d.b.y-(0.40f+sim::WHEEL_RADIUS+sim::SPRING_REST-sim::SUSPENSION_SAG))<0.09f);
    assert(fabsf(d.b.pitch)<0.03f && fabsf(d.b.roll)<0.03f);
    const sim::RayHit miss=sim::raycastGround(0,10,0,sim::SPRING_REST+sim::WHEEL_RADIUS);
    assert(!miss.hit);
    const sim::RayHit bump=sim::raycastGround(0,1.0f,34.5f,2.0f);
    assert(bump.hit&&bump.height>0&&bump.ny>0.8f);
    d.b.x=4.9f;d.b.z=34.5f;
    assert(sim::visualTravel(d.b,0)>=0.0f);
    assert(sim::visualTravel(d.b,0)<=sim::SPRING_REST);
    frames(d,180);
    assert(std::isfinite(d.b.y)&&std::isfinite(d.b.pitch)&&std::isfinite(d.b.roll));
    for(int i=0;i<6;i++)assert(d.wheelTravel[i]>=0&&d.wheelTravel[i]<=sim::SPRING_REST);
    printf("rays: bump %.3f settled %.3f\n",bump.height,d.b.y);
}
static void steeringAndBraking(){
    sim::Dynamics d;
    d.b.steer=0.75f;d.b.speed=4.0f;
    const float frontLeft=sim::wheelSteerAngle(d.b,0);
    const float frontRight=sim::wheelSteerAngle(d.b,1);
    assert(frontRight>frontLeft&&frontLeft>0.0f); // right turn: inside tire turns more
    d.b.steer=-0.75f;
    assert(sim::wheelSteerAngle(d.b,0)<sim::wheelSteerAngle(d.b,1));
    d.b.steer=0.75f;d.b.speed=20.0f;
    assert(sim::wheelSteerAngle(d.b,1)<frontRight);
    d=sim::Dynamics();
    d.b.x=0;d.b.z=-110;d.b.speed=13.0f;
    sim::input(d,0,0.9f,0,0);
    frames(d,150);
    assert(d.b.heading>0.02f);
    assert(d.yawRate>0.0f);
    assert(fabsf(d.b.speed*d.yawRate)<=sim::MAX_LATERAL_ACCEL+0.10f);
    assert(d.b.roll>0.0f); // outer side lifts under a right-hand turn
    const float previousSpeed=d.b.speed;
    sim::input(d,0,0,1,0);
    frames(d,22);
    assert(d.b.speed<previousSpeed);
    assert(d.b.pitch>0.0f); // brakes load front axle, nose down
    printf("steer: yaw %.3f roll %.3f brakePitch %.3f\n",d.yawRate,d.b.roll,d.b.pitch);
}
static void reverseYaw(){
    sim::Dynamics d;
    d.b.z=-110;d.b.speed=-3.5f;
    sim::input(d,0,0.55f,0,0);
    frames(d,60);
    assert(d.b.heading<0.0f);
}
int main(){
    parkedAndLevel();
    compressionAndRays();
    steeringAndBraking();
    reverseYaw();
    puts("six-wheel suspension and steering tests passed");
    return 0;
}
