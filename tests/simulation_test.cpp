// Compilavel sem Windows: g++ -std=c++11 -Itests tests/simulation_test.cpp
// Stub Winsock usado apenas para testar a simulacao; no jogo real, usa winsock2.h.
#include "protocol_stub.h"
#define BUS_SIM_TEST 1
#include "../src/simulation.hpp"
#include <assert.h>
#include <stdio.h>
int main() {
    assert(sim::terrain(0,34)>0);
    assert(sim::overlaps(0,0,1,2,0,0,1,1,2,0));
    assert(!sim::overlaps(0,0,1,2,0,10,10,1,2,0));
    assert(sim::collidesBuildings(30,30,0));
    assert(!sim::collidesBuildings(0,0,0));
    sim::Dynamics d;d.b.id=1;d.b.x=4.9f;d.b.z=18;
    float startZ=d.b.z;
    sim::input(d,1,0,0,0);
    for(int i=0;i<120;i++)sim::step(d,1.0f/60);
    assert(d.b.z>startZ);assert(d.b.y>0.85f);
    for(int i=0;i<6;i++)assert(d.wheelTravel[i]>=0 && d.b.wheelTravel[i]<=sim::SPRING_REST);
    d.b.speed=0;d.b.z=18;d.b.door=0;
    sim::input(d,0,0,1,INPUT_TOGGLE_DOOR);
    assert(d.b.door>0.5f);
    sim::input(d,0,0,1,INPUT_TOGGLE_DOOR);
    assert(d.b.door>0.5f); // flanco, nao oscila a cada pacote
    sim::input(d,0,0,1,0);
    sim::input(d,0,0,1,INPUT_TOGGLE_DOOR);
    assert(d.b.door<0.5f);
    puts("simulation tests passed");
    return 0;
}
