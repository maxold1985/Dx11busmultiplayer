#include "../src/3ds_format.hpp"
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <vector>
static void u16(std::vector<uint8_t>& b,unsigned x){b.push_back((uint8_t)x);b.push_back((uint8_t)(x>>8));}
static void u32(std::vector<uint8_t>& b,uint32_t x){for(int i=0;i<4;i++)b.push_back((uint8_t)(x>>(i*8)));}
static void f32(std::vector<uint8_t>& b,float x){uint32_t n;memcpy(&n,&x,4);u32(b,n);}
static void str(std::vector<uint8_t>& b,const char* s){while(*s)b.push_back((uint8_t)*s++);b.push_back(0);}
static std::vector<uint8_t> chunk(unsigned id,const std::vector<uint8_t>& payload){
    std::vector<uint8_t> b;u16(b,id);u32(b,(uint32_t)payload.size()+6);
    b.insert(b.end(),payload.begin(),payload.end());return b;
}
static void add(std::vector<uint8_t>& out,const std::vector<uint8_t>& sub){out.insert(out.end(),sub.begin(),sub.end());}
static std::vector<uint8_t> fixture(){
    std::vector<uint8_t> mat,name,map,file,diff,rgb;
    str(name,"MaterialA");add(mat,chunk(0xA000,name));
    str(file,"pintura.png");add(map,chunk(0xA300,file));add(mat,chunk(0xA200,map));
    rgb.push_back(90);rgb.push_back(180);rgb.push_back(240);add(diff,chunk(0x0011,rgb));add(mat,chunk(0xA020,diff));
    std::vector<uint8_t> positions;u16(positions,3);
    f32(positions,1);f32(positions,2);f32(positions,3);
    f32(positions,4);f32(positions,5);f32(positions,6);
    f32(positions,7);f32(positions,8);f32(positions,9);
    std::vector<uint8_t> uv;u16(uv,3);for(int i=0;i<3;i++){f32(uv,0.25f);f32(uv,0.75f);}
    std::vector<uint8_t> face;u16(face,1);u16(face,0);u16(face,1);u16(face,2);u16(face,0);
    std::vector<uint8_t> group;str(group,"MaterialA");u16(group,1);u16(group,0);add(face,chunk(0x4130,group));
    std::vector<uint8_t> tri;add(tri,chunk(0x4110,positions));add(tri,chunk(0x4120,face));add(tri,chunk(0x4140,uv));
    std::vector<uint8_t> obj;str(obj,"Body");add(obj,chunk(0x4100,tri));
    std::vector<uint8_t> edit;add(edit,chunk(0xAFFF,mat));add(edit,chunk(0x4000,obj));
    std::vector<uint8_t> root;add(root,chunk(0x3D3D,edit));return chunk(0x4D4D,root);
}
int main(){
    std::vector<uint8_t> input=fixture();std::vector<omsi::Mesh> meshes;
    std::string error;assert(omsi::parse3DS(input,meshes,&error));
    assert(meshes.size()==1);
    const omsi::Mesh& m=meshes[0];
    assert(m.vertices.size()==3&&m.triangles.size()==1);
    assert(m.objectName=="Body");
    assert(m.vertices[0].x==1.f&&m.vertices[0].y==3.f&&m.vertices[0].z==2.f);
    assert(fabs(m.vertices[0].v-0.25f)<0.0001f);
    assert(m.triangles[0].a==0&&m.triangles[0].b==2&&m.triangles[0].c==1);
    assert(m.materials.size()==2&&m.triangles[0].material==1);
    assert(m.materials[1].texture=="pintura.png");
    assert(fabs(m.materials[1].rgba[1]-180.f/255.f)<0.001f);
    input[0]=0;assert(!omsi::parse3DS(input,meshes,&error));
    input=fixture();input[2]=0xFF;input[3]=0xFF;input[4]=0xFF;input[5]=0x7F;
    assert(!omsi::parse3DS(input,meshes,&error));
    input=fixture();input.resize(input.size()-5);
    assert(!omsi::parse3DS(input,meshes,&error));
    // The real OMSI export uses these six distinct wheel-name families.
    const char* wheelNames[6]={"_wheel_fl_ref004__nolight_",
        "_wheel_fr_ref004__nolight_","_wheel_rl2_ref004__nolight_",
        "_wheel_rr2_ref004__nolight_","_wheel_rl_ref004__nolight_",
        "_wheel_rr_ref004__nolight_"};
    std::vector<omsi::Mesh> wheelParts;
    for(int i=0;i<6;i++){
        assert(omsi::wheelGroup3DS(wheelNames[i])==i);
        omsi::Mesh wheel;
        wheel.objectName=wheelNames[i];
        omsi::Vertex v0={},v1={};
        v0.x=(i&1)?1.0f:-1.0f;v1.x=v0.x;
        v0.y=0.16f;v1.y=1.24f;
        v0.z=(i<2?3.82f:(i<4?-2.63f:-4.08f))-0.54f;
        v1.z=v0.z+1.08f;
        wheel.vertices.push_back(v0);
        wheel.vertices.push_back(v1);
        wheelParts.push_back(wheel);
    }
    assert(omsi::wheelGroup3DS("_steering_wheel_")==-1);
    assert(omsi::wheelGroup3DS("_wheel_suspension_rl_")==-1);
    omsi::WheelPivot3DS pivots[6];
    omsi::find3DSWheelPivots(wheelParts,pivots);
    for(int i=0;i<6;i++){
        assert(pivots[i].valid);
        assert(fabsf(pivots[i].y-0.7f)<0.001f);
        assert(fabsf(pivots[i].radius-0.54f)<0.001f);
    }
    puts("native 3ds format tests passed");
    return 0;
}
