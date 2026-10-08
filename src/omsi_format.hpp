#pragma once
// Leitor portatil de malhas OMSI (.o3d) e listas de malhas .cfg/.bus.
// Formato documentado pelo projeto openOMSI (docs/FORMATS.md).
// Usa apenas C++11: tambem pode ser compilado pelo MinGW i686 sem Assimp.
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <cmath>
#include <stdio.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace omsi {
struct Vertex {float x,y,z,nx,ny,nz,u,v;};
struct Triangle {uint32_t a,b,c;uint16_t material;};
struct Material {
    float rgba[4];
    std::string texture;
    Material() {rgba[0]=rgba[1]=rgba[2]=rgba[3]=1.0f;}
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    float pivot[16];
    Mesh() {memset(pivot,0,sizeof(pivot));for(int i=0;i<4;i++)pivot[i*4+i]=1.0f;}
};
struct Animation {
    std::string variable;
    float factor,origin[3],rot[3];
    bool translation,fromMesh;
    Animation():factor(0),translation(false),fromMesh(false){
        for(int i=0;i<3;i++){origin[i]=0;rot[i]=0;}
    }
};
struct MeshEntry {std::string path;std::vector<Animation> animations;};
inline std::string trim(std::string s) {
    const char* ws=" \t\r\n";
    size_t a=s.find_first_not_of(ws);
    if(a==std::string::npos)return "";
    return s.substr(a,s.find_last_not_of(ws)-a+1);
}
inline std::string lower(std::string s) {
    for(size_t i=0;i<s.size();i++)s[i]=(char)std::tolower((unsigned char)s[i]);
    return s;
}
inline std::string normalized(std::string s) {
    std::replace(s.begin(),s.end(),'\\','/');
    return s;
}
inline std::string directory(const std::string& path) {
    const std::string s=normalized(path);const size_t p=s.find_last_of('/');
    return p==std::string::npos?"":s.substr(0,p+1);
}
inline std::string filename(const std::string& path) {
    const std::string s=normalized(path);const size_t p=s.find_last_of('/');
    return p==std::string::npos?s:s.substr(p+1);
}
inline bool absolute(const std::string& s) {
    return s.size()>1 && s[1]==':' || (!s.empty() && (s[0]=='/'||s[0]=='\\'));
}
inline std::string join(const std::string& dir,const std::string& path) {
    if(absolute(path))return normalized(path);
    return normalized(dir+path);
}
inline bool loadBytes(const std::string& path,std::vector<uint8_t>& bytes,size_t limit=128*1024*1024) {
    std::ifstream in(path.c_str(),std::ios::in|std::ios::binary|std::ios::ate);
    if(!in)return false;
    const std::streamoff size=in.tellg();
    if(size<=0 || (uint64_t)size>limit)return false;
    bytes.resize((size_t)size);
    in.seekg(0,std::ios::beg);
    return !!in.read((char*)&bytes[0],(std::streamsize)bytes.size());
}
struct Reader {
    const std::vector<uint8_t>& data;
    size_t at;
    explicit Reader(const std::vector<uint8_t>& d):data(d),at(0){}
    bool has(size_t n)const{return n<=data.size()-at;}
    bool u8(uint8_t& value){if(!has(1))return false;value=data[at++];return true;}
    bool u16(uint16_t& value){if(!has(2))return false;value=(uint16_t)(data[at]|(data[at+1]<<8));at+=2;return true;}
    bool u32(uint32_t& value){
        if(!has(4))return false;
        value=(uint32_t)data[at] | (uint32_t)data[at+1]<<8 |
              (uint32_t)data[at+2]<<16 | (uint32_t)data[at+3]<<24;
        at+=4;return true;
    }
    bool f32(float& value){uint32_t bits;if(!u32(bits))return false;memcpy(&value,&bits,4);return true;}
    bool skip(size_t bytes){if(!has(bytes))return false;at+=bytes;return true;}
    bool name(std::string& out){
        uint8_t n;if(!u8(n)||!has(n))return false;
        out.assign((const char*)&data[at],(size_t)n);at+=n;return true;
    }
};
inline void unscramble(Mesh& mesh,uint32_t key,uint8_t ver,uint8_t flags) {
    if(ver<4||key==0xFFFFFFFFu)return;
    const uint32_t modulus=0xFDE8u;
    uint32_t state=(key+ver-4+((flags&2u)?0x17Du:0u))%modulus;
    const uint32_t n=(uint32_t)mesh.vertices.size()%modulus;
    uint32_t prev=0;
    for(size_t i=0;i<mesh.vertices.size();++i) {
        Vertex& v=mesh.vertices[i];
        if(key==0u)state=(flags&2u)?0x130u:0u;
        state=(state*n+n*prev)%8000u;
        const float fx=v.x-truncf(v.x),fy=v.y-truncf(v.y),fz=v.z-truncf(v.z);
        prev=(uint32_t)(fabsf(fx*fy*fz)*600.0f)%256u;
        if(state<1000)std::swap(v.x,v.y);
        else if(state<3000)std::swap(v.x,v.z);
        else if(state>7000)std::swap(v.y,v.z);
        if(state%4==0)v.nx=-v.nx;
        if(state%6==0)v.ny=-v.ny;
        if(state%7==0)v.nz=-v.nz;
        if(state<600)std::swap(v.ny,v.nz);
        else if(state>4500)std::swap(v.nx,v.ny);
        else if(state>6500)std::swap(v.nx,v.nz);
        if(state%5==0){float a=(float)(state%100);v.u-=a*a/10000.0f;}
        if(state%3==0){float a=(float)(state%50);v.v-=a*a/2500.0f;}
    }
}
inline bool parseO3D(const std::vector<uint8_t>& data,Mesh& output,std::string* error=0) {
    output=Mesh();
    Reader r(data);
    uint8_t a,b,version,flags=0;
    uint32_t key=0xFFFFFFFFu;
    if(!r.u8(a)||!r.u8(b)||a!=0x84||b!=0x19||!r.u8(version))
        {if(error)*error="O3D magic invalida";return false;}
    if(version!=1&&version!=3&&version!=4&&version!=5&&version!=7)
        {if(error)*error="Versao O3D nao suportada";return false;}
    if(version>=3 && !r.u8(flags))return false;
    if(version>=4 && !r.u32(key))return false;
    bool gotVertices=false,gotTriangles=false;
    while(r.has(1)) {
        uint8_t tag;if(!r.u8(tag))break;
        if(tag==0x17) {
            uint32_t count=0;uint16_t shortCount=0;
            if(version>=3){if(!r.u32(count))return false;}
            else {if(!r.u16(shortCount))return false;count=shortCount;}
            if(count>1500000 || !r.has((size_t)count*32))return false;
            output.vertices.resize(count);
            for(uint32_t i=0;i<count;i++) {
                float* fields=&output.vertices[i].x;
                for(int j=0;j<8;j++)if(!r.f32(fields[j]))return false;
            }
            unscramble(output,key,version,flags);
            gotVertices=true;
        } else if(tag==0x49) {
            uint32_t count=0;uint16_t shortCount=0;
            if(version>=3){if(!r.u32(count))return false;}
            else {if(!r.u16(shortCount))return false;count=shortCount;}
            bool wide=(flags&1)!=0;
            if(count>2500000 || !r.has((size_t)count*(wide?14:8)))return false;
            output.triangles.resize(count);
            for(uint32_t i=0;i<count;i++) {
                Triangle& t=output.triangles[i];
                if(wide){if(!r.u32(t.a)||!r.u32(t.b)||!r.u32(t.c))return false;}
                else {uint16_t x,y,z;if(!r.u16(x)||!r.u16(y)||!r.u16(z))return false;
                      t.a=x;t.b=y;t.c=z;}
                if(!r.u16(t.material))return false;
            }
            gotTriangles=true;
        } else if(tag==0x26) {
            uint16_t count;if(!r.u16(count)||count>4096)return false;
            output.materials.resize(count);
            for(unsigned i=0;i<count;i++) {
                float scalar=0;
                for(int j=0;j<11;j++) {
                    if(!r.f32(scalar))return false;
                    if(j<4)output.materials[i].rgba[j]=scalar;
                }
                if(!r.name(output.materials[i].texture))return false;
            }
        } else if(tag==0x79) {
            for(int i=0;i<16;i++)if(!r.f32(output.pivot[i]))return false;
        } else if(tag==0x54) {
            uint16_t count;if(!r.u16(count)||count>1024)return false;
            for(unsigned i=0;i<count;i++) {
                std::string unused;uint16_t weights;
                if(!r.name(unused)||!r.u16(weights))return false;
                if(!r.skip((size_t)weights*((flags&1)?8:6)))return false;
            }
        } else {
            // OMSI permits unknown bytes between sections (e.g. protected mods).
            continue;
        }
    }
    if(!gotVertices||!gotTriangles||output.vertices.empty()||output.triangles.empty()) {
        if(error)*error="O3D sem vertices/triangulos";return false;
    }
    if(output.materials.empty())output.materials.push_back(Material());
    for(size_t i=0;i<output.triangles.size();i++) {
        const Triangle& t=output.triangles[i];
        if(t.a>=output.vertices.size()||t.b>=output.vertices.size()||
           t.c>=output.vertices.size()) {
            if(error)*error="Indice de vertice invalido";return false;
        }
    }
    return true;
}
inline bool readO3D(const std::string& path,Mesh& output,std::string* error=0) {
    std::vector<uint8_t> bytes;
    if(!loadBytes(path,bytes)){if(error)*error="Nao foi possivel abrir "+path;return false;}
    return parseO3D(bytes,output,error);
}
inline bool readLines(const std::string& path,std::vector<std::string>& lines) {
    std::ifstream in(path.c_str(),std::ios::binary);
    if(!in)return false;
    std::string line;
    while(std::getline(in,line)) {
        if(lines.size()>100000)return false;
        if(!line.empty()&&line[line.size()-1]=='\r')line.resize(line.size()-1);
        if(lines.empty()&&line.size()>=3&&
           (unsigned char)line[0]==0xEF&&(unsigned char)line[1]==0xBB&&(unsigned char)line[2]==0xBF)line.erase(0,3);
        lines.push_back(trim(line));
    }
    return true;
}
inline std::string nextParam(const std::vector<std::string>& lines,size_t& i) {
    for(size_t j=i+1;j<lines.size();j++) {
        if(lines[j].empty()||lines[j][0]==';'||lines[j].substr(0,2)=="//")continue;
        if(lines[j][0]=='[')return "";
        i=j;return lines[j];
    }
    return "";
}
inline float parseFloat(const std::string& s,float fallback=0) {
    if(s.empty())return fallback;
    char* end=0;const float f=strtof(s.c_str(),&end);
    if(end==s.c_str()||!std::isfinite(f))return fallback;
    return f;
}
inline bool readModelList(const std::string& input,std::vector<MeshEntry>& entries,std::string* error=0) {
    entries.clear();
    const std::string suffix=lower(input.substr(input.find_last_of('.')==std::string::npos?input.size():input.find_last_of('.')));
    if(suffix==".o3d"||suffix==".x") {
        MeshEntry e;e.path=input;entries.push_back(e);return true;
    }
    std::string cfg=input;
    if(suffix==".bus"||suffix==".ovh") {
        std::vector<std::string> lines;
        if(!readLines(input,lines)){if(error)*error="Nao foi possivel abrir .bus";return false;}
        std::string model;
        for(size_t i=0;i<lines.size();i++)if(lower(lines[i])=="[model]"){
            model=nextParam(lines,i);break;
        }
        if(model.empty()){if(error)*error=".bus sem [model]";return false;}
        cfg=join(directory(input),model);
    }
    std::vector<std::string> lines;
    if(!readLines(cfg,lines)){if(error)*error="Nao foi possivel abrir model.cfg: "+cfg;return false;}
    MeshEntry* active=0;Animation* animation=0;
    int lodLevel=0;
    for(size_t i=0;i<lines.size();i++) {
        const std::string key=lower(trim(lines[i]));
        if(key=="[lod]") {
            (void)nextParam(lines,i);
            ++lodLevel;active=0;animation=0;
        } else if(key=="[mesh]") {
            const std::string file=nextParam(lines,i);
            if(lodLevel>1){active=0;animation=0;continue;}
            if(file.empty()||entries.size()>=1500){active=0;animation=0;continue;}
            MeshEntry entry;entry.path=join(directory(cfg),file);
            entries.push_back(entry);
            active=&entries.back();animation=0;
        } else if(key=="[newanim]"&&active) {
            if(active->animations.size()<32){active->animations.push_back(Animation());animation=&active->animations.back();}
        } else if(animation&&(key=="origin_trans"||key=="[origin_trans]")) {
            for(int k=0;k<3;k++)animation->origin[k]=parseFloat(nextParam(lines,i));
        } else if(animation&&(key=="origin_rot_x"||key=="[origin_rot_x]")) {
            animation->rot[0]=parseFloat(nextParam(lines,i));
        } else if(animation&&(key=="origin_rot_y"||key=="[origin_rot_y]")) {
            animation->rot[1]=parseFloat(nextParam(lines,i));
        } else if(animation&&(key=="origin_rot_z"||key=="[origin_rot_z]")) {
            animation->rot[2]=parseFloat(nextParam(lines,i));
        } else if(animation&&(key=="origin_from_mesh"||key=="[origin_from_mesh]")) {
            animation->fromMesh=true;
        } else if(animation&&(key=="anim_rot"||key=="[anim_rot]"||key=="anim_trans"||key=="[anim_trans]")) {
            animation->translation=(key.find("trans")!=std::string::npos);
            animation->variable=nextParam(lines,i);
            animation->factor=parseFloat(nextParam(lines,i));
        }
    }
    if(entries.empty()&&error)*error="Nenhum [mesh] no model.cfg";
    return !entries.empty();
}
} // namespace omsi
