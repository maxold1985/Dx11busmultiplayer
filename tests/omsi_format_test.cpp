#include "../src/omsi_format.hpp"
#include <assert.h>
#include <stdio.h>
#include <fstream>
#include <vector>
#include <string>
#include <math.h>
static void u8(std::vector<uint8_t>& out,uint8_t value){out.push_back(value);}
static void u16(std::vector<uint8_t>& out,uint16_t value) {
    out.push_back((uint8_t)value);out.push_back((uint8_t)(value>>8));
}
static void u32(std::vector<uint8_t>& out,uint32_t value){
    for(int i=0;i<4;i++)out.push_back((uint8_t)(value>>(8*i)));
}
static void f32(std::vector<uint8_t>& out,float value){
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));u32(out,bits);
}
static std::vector<uint8_t> makeMesh(int ver,bool wide){
    std::vector<uint8_t> out;
    u8(out,0x84);u8(out,0x19);u8(out,(uint8_t)ver);
    if(ver>=3)u8(out,wide?1:0);
    if(ver>=4)u32(out,0xFFFFFFFFu); // no scrambling
    u8(out,0x17);
    if(ver>=3)u32(out,3);else u16(out,3);
    for(int i=0;i<3;i++){
        f32(out,(float)i+1);f32(out,2.0f);f32(out,3.0f);
        f32(out,0);f32(out,1);f32(out,0);
        f32(out,(float)i*0.5f);f32(out,0.75f);
    }
    u8(out,0x49);
    if(ver>=3)u32(out,1);else u16(out,1);
    if(wide){u32(out,0);u32(out,1);u32(out,2);}
    else {u16(out,0);u16(out,1);u16(out,2);}
    u16(out,0);
    u8(out,0x26);u16(out,1);
    for(int i=0;i<11;i++)f32(out,i==3?0.7f:1.0f);
    std::string name="body.dds";u8(out,(uint8_t)name.size());
    out.insert(out.end(),name.begin(),name.end());
    u8(out,0x79);
    for(int i=0;i<16;i++)f32(out,i%5==0?1.0f:0.0f);
    return out;
}
int main() {
    for(int ver=1;ver<=7;ver++) {
        if(ver!=1&&ver!=3&&ver!=4&&ver!=5&&ver!=7)continue;
        std::vector<uint8_t> bytes=makeMesh(ver,ver==7);
        omsi::Mesh mesh;
        std::string message;
        assert(omsi::parseO3D(bytes,mesh,&message));
        assert(mesh.vertices.size()==3);
        assert(mesh.triangles.size()==1);
        assert(mesh.triangles[0].a==0&&mesh.triangles[0].c==2);
        assert(mesh.materials[0].texture=="body.dds");
        assert(fabsf(mesh.vertices[1].x-2)<0.001f);
        bytes.resize(bytes.size()-4);
        assert(!omsi::parseO3D(bytes,mesh,&message));
    }
    std::vector<uint8_t> corrupt=makeMesh(3,false);
    corrupt[0]=0;
    omsi::Mesh mesh;assert(!omsi::parseO3D(corrupt,mesh));
    // model.cfg com varias seções, mesh e animacoes OMSI.
    const char* model="omsi_parser_fixture.cfg";
    const char* vehicle="omsi_parser_fixture.bus";
    {
        std::ofstream f(model);
        f<<"[mesh]\nBody\\carroceria.o3d\n[matl]\nbody.dds\n0\n";
        f<<"[newanim]\norigin_trans\n1\n3\n2\nanim_rot\nWheel_Rotation_0_L\n57.2957795\n";
        f<<"[mesh]\nRodas\\roda.o3d\n[newanim]\norigin_from_mesh\n";
        f<<"anim_trans\nAxle_Suspension_0_L\n1\n";
        f<<"[LOD]\n0.1\n[mesh]\nDetalhe\\detalhe.o3d\n[LOD]\n1\n[mesh]\nBaixo\\baixo.o3d\n";
    }
    {
        std::ofstream f(vehicle);
        f<<"[friendlyname]\nBus test\n[model]\nomsi_parser_fixture.cfg\n";
    }
    std::vector<omsi::MeshEntry> entries;
    std::string error;
    assert(omsi::readModelList(vehicle,entries,&error));
    assert(entries.size()==3);
    assert(entries[0].path=="Body/carroceria.o3d");
    assert(entries[1].path=="Rodas/roda.o3d");
    assert(entries[0].animations.size()==1);
    assert(entries[0].animations[0].variable=="Wheel_Rotation_0_L");
    assert(fabsf(entries[0].animations[0].origin[1]-3)<0.001f);
    assert(entries[1].animations[0].translation);
    assert(entries[1].animations[0].fromMesh);
    remove(model);remove(vehicle);
    puts("omsi_format tests passed");
    return 0;
}
