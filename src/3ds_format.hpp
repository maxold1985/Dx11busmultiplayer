#pragma once
// 3D Studio .3ds geometry and material reader, C++11, no Assimp/Windows headers.
// Input: 3DS X-right, Y-length, Z-up. Output: game X-right, Y-up, Z-length.
#include "omsi_format.hpp"
#include <map>
#include <utility>
#include <limits>

namespace omsi {
namespace studio3ds {
struct Chunk { uint16_t id; size_t data,end; };
inline bool next(const std::vector<uint8_t>& b,size_t& at,size_t limit,Chunk& c) {
    if(at>limit || limit>b.size() || limit-at<6)return false;
    const uint32_t size=(uint32_t)b[at+2]|((uint32_t)b[at+3]<<8)|
        ((uint32_t)b[at+4]<<16)|((uint32_t)b[at+5]<<24);
    if(size<6 || size>limit-at)return false;
    c.id=(uint16_t)(b[at]|b[at+1]<<8);
    c.data=at+6;c.end=at+size;at=c.end;return true;
}
inline bool stringAt(const std::vector<uint8_t>& b,size_t& at,size_t end,std::string& result){
    const size_t start=at;
    while(at<end && b[at]!=0)++at;
    if(at==end || at-start>1024)return false;
    result.assign((const char*)&b[start],at-start);++at;return true;
}
inline uint16_t u16(const std::vector<uint8_t>& b,size_t at){
    return (uint16_t)(b[at]|(uint16_t(b[at+1])<<8));
}
inline float f32(const std::vector<uint8_t>& b,size_t at){
    const uint32_t bits=(uint32_t)b[at]|(uint32_t)b[at+1]<<8|
        (uint32_t)b[at+2]<<16|(uint32_t)b[at+3]<<24;
    float value;memcpy(&value,&bits,sizeof(value));return value;
}
inline bool color(const std::vector<uint8_t>& b,const Chunk& section,float out[3]){
    size_t at=section.data;
    while(at<section.end){
        Chunk c;if(!next(b,at,section.end,c))return false;
        if((c.id==0x0011 || c.id==0x0012) && c.end-c.data>=3){
            for(int i=0;i<3;i++)out[i]=b[c.data+i]/255.0f;
            return true;
        }
        if((c.id==0x0010 || c.id==0x0013) && c.end-c.data>=12){
            for(int i=0;i<3;i++)out[i]=f32(b,c.data+4*i);
            return true;
        }
    }
    return true;
}
inline bool textureMap(const std::vector<uint8_t>& b,const Chunk& section,std::string& texture){
    size_t at=section.data;
    while(at<section.end){
        Chunk c;if(!next(b,at,section.end,c))return false;
        if(c.id==0xA300){size_t p=c.data;if(!stringAt(b,p,c.end,texture))return false;}
    }
    return true;
}
inline bool material(const std::vector<uint8_t>& b,const Chunk& section,
                     std::map<std::string,Material>& result){
    size_t at=section.data;std::string name;Material m;
    while(at<section.end){
        Chunk c;if(!next(b,at,section.end,c))return false;
        if(c.id==0xA000){size_t p=c.data;if(!stringAt(b,p,c.end,name))return false;}
        else if(c.id==0xA020){if(!color(b,c,m.rgba))return false;}
        else if(c.id==0xA200){if(!textureMap(b,c,m.texture))return false;}
    }
    if(!name.empty())result[lower(name)]=m;
    return true;
}
struct Group {std::string name;std::vector<uint16_t> faces;};
inline bool faceList(const std::vector<uint8_t>& b,const Chunk& section,
                     Mesh& mesh,std::vector<Group>& groups){
    if(section.end-section.data<2)return false;
    const unsigned count=u16(b,section.data);
    if(count>(section.end-section.data-2)/8)return false;
    size_t at=section.data+2;
    mesh.triangles.resize(count);
    for(unsigned i=0;i<count;i++){
        const uint16_t a=u16(b,at),c=u16(b,at+2),d=u16(b,at+4);
        // Swapping the Y/Z axes reverses handedness, so reverse triangle winding.
        Triangle t={a,d,c,0};mesh.triangles[i]=t;at+=8;
    }
    while(at<section.end){
        Chunk c;if(!next(b,at,section.end,c))return false;
        if(c.id!=0x4130)continue;
        size_t p=c.data;Group g;
        if(!stringAt(b,p,c.end,g.name)||c.end-p<2)return false;
        const unsigned n=u16(b,p);p+=2;
        if(n>(c.end-p)/2)return false;
        g.faces.reserve(n);
        for(unsigned j=0;j<n;j++){
            const uint16_t face=u16(b,p);p+=2;
            if(face>=count)return false;
            g.faces.push_back(face);
        }
        groups.push_back(std::move(g));
    }
    return true;
}
inline bool triangleMesh(const std::vector<uint8_t>& b,const Chunk& section,
                         const std::map<std::string,Material>& palette,Mesh& mesh){
    std::vector<Group> groups;
    size_t at=section.data;
    while(at<section.end){
        Chunk c;if(!next(b,at,section.end,c))return false;
        if(c.id==0x4110){
            if(c.end-c.data<2)return false;
            const unsigned n=u16(b,c.data);
            if(n>(c.end-c.data-2)/12)return false;
            mesh.vertices.resize(n);
            for(unsigned i=0;i<n;i++){
                const size_t p=c.data+2+i*12;
                const float x=f32(b,p),y=f32(b,p+4),z=f32(b,p+8);
                if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
                Vertex& v=mesh.vertices[i];
                v.x=x;v.y=z;v.z=y;v.nx=0;v.ny=1;v.nz=0;v.u=0;v.v=0;
            }
        } else if(c.id==0x4140){
            if(c.end-c.data<2)return false;
            const unsigned n=u16(b,c.data);
            if(n>(c.end-c.data-2)/8)return false;
            // 3DS stores bottom-left texture coordinates; WIC uses top-left.
            for(unsigned i=0;i<n && i<mesh.vertices.size();i++){
                const size_t p=c.data+2+i*8;
                mesh.vertices[i].u=f32(b,p);
                mesh.vertices[i].v=1.0f-f32(b,p+4);
            }
        } else if(c.id==0x4120){
            if(!faceList(b,c,mesh,groups))return false;
        }
        // 0x4160 local transform: OMSI .3ds exports often already bake the
        // object coordinates, so keep authored vertex positions to avoid
        // applying translations/scales twice.
    }
    if(mesh.vertices.empty()||mesh.triangles.empty())return true;
    for(size_t i=0;i<mesh.triangles.size();i++){
        const Triangle& t=mesh.triangles[i];
        if(t.a>=mesh.vertices.size()||t.b>=mesh.vertices.size()||t.c>=mesh.vertices.size())return false;
    }
    mesh.materials.push_back(Material());
    std::map<std::string,uint16_t> materialIds;
    for(size_t i=0;i<groups.size();i++){
        const std::string key=lower(groups[i].name);
        std::map<std::string,uint16_t>::iterator id=materialIds.find(key);
        uint16_t m=0;
        if(id==materialIds.end()){
            if(mesh.materials.size()>=65535)return false;
            std::map<std::string,Material>::const_iterator selected=palette.find(key);
            Material selectedMaterial;
            if(selected!=palette.end())selectedMaterial=selected->second;
            m=(uint16_t)mesh.materials.size();
            mesh.materials.push_back(selectedMaterial);
            materialIds[key]=m;
        }else m=id->second;
        for(size_t j=0;j<groups[i].faces.size();j++)
            mesh.triangles[groups[i].faces[j]].material=m;
    }
    return true;
}
inline bool object(const std::vector<uint8_t>& b,const Chunk& section,
                   const std::map<std::string,Material>& palette,
                   std::vector<Mesh>& output){
    size_t at=section.data;std::string name;
    if(!stringAt(b,at,section.end,name))return false;
    while(at<section.end){
        Chunk c;if(!next(b,at,section.end,c))return false;
        if(c.id!=0x4100)continue;
        Mesh mesh;
        if(!triangleMesh(b,c,palette,mesh))return false;
        if(!mesh.vertices.empty()&&!mesh.triangles.empty())output.push_back(std::move(mesh));
    }
    return true;
}
} // namespace studio3ds
inline bool parse3DS(const std::vector<uint8_t>& data,std::vector<Mesh>& output,
                     std::string* error=0){
    output.clear();
    if(data.size()<12 || data.size()>256u*1024u*1024u){if(error)*error="Invalid 3DS file size";return false;}
    size_t rootAt=0;studio3ds::Chunk root;
    if(!studio3ds::next(data,rootAt,data.size(),root)||root.id!=0x4D4D){
        if(error)*error="Invalid 3DS main chunk (expected 0x4D4D)";
        return false;
    }
    std::map<std::string,Material> materials;
    size_t pos=root.data;
    while(pos<root.end){
        studio3ds::Chunk edit;
        if(!studio3ds::next(data,pos,root.end,edit)){if(error)*error="Corrupt 3DS root chunk";return false;}
        if(edit.id!=0x3D3D)continue;
        size_t p=edit.data;
        while(p<edit.end){
            studio3ds::Chunk c;
            if(!studio3ds::next(data,p,edit.end,c)){if(error)*error="Corrupt 3DS editor chunk";return false;}
            if(c.id==0xAFFF && !studio3ds::material(data,c,materials)){
                if(error)*error="Corrupt 3DS material chunk";
                return false;
            }
        }
        p=edit.data;
        while(p<edit.end){
            studio3ds::Chunk c;
            if(!studio3ds::next(data,p,edit.end,c)){if(error)*error="Corrupt 3DS editor chunk";return false;}
            if(c.id==0x4000){
                if(output.size()>=4096){if(error)*error="Too many 3DS objects";return false;}
                if(!studio3ds::object(data,c,materials,output)){
                    if(error)*error="Invalid 3DS object geometry or face indices";
                    return false;
                }
            }
        }
    }
    if(output.empty()){if(error)*error="3DS contains no triangle meshes";return false;}
    return true;
}
inline bool read3DS(const std::string& path,std::vector<Mesh>& output,std::string* error=0){
    std::vector<uint8_t> data;
    if(!loadBytes(path,data,256u*1024u*1024u)){
        if(error)*error="Unable to read .3ds file (missing or larger than 256 MiB)";
        return false;
    }
    return parse3DS(data,output,error);
}
} // namespace omsi
