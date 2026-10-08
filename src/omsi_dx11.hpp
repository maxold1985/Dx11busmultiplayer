#pragma once
// Adaptador OMSI -> Direct3D 11, sem Assimp. So modelos locais fornecidos pelo usuario.
#include "omsi_format.hpp"
#include "3ds_format.hpp"
#include "simulation.hpp"
#include "model_assimp.hpp"
#ifdef BUS_HAS_ASSIMP
#include <assimp/material.h>
#endif
#include "math_compat.h"
#include <map>
#include <fstream>
#include <vector>
#include <string>
#include <stdio.h>

namespace omsi {
inline uint32_t ddsU32(const std::vector<uint8_t>& d,size_t at){
    return (uint32_t)d[at]|(uint32_t)d[at+1]<<8|(uint32_t)d[at+2]<<16|(uint32_t)d[at+3]<<24;
}
inline std::wstring fileWide(const std::string& ansi){
    int length=MultiByteToWideChar(CP_ACP,0,ansi.c_str(),-1,0,0);
    if(length<=0)return std::wstring();
    std::vector<wchar_t> result((size_t)length);
    MultiByteToWideChar(CP_ACP,0,ansi.c_str(),-1,&result[0],length);
    return std::wstring(&result[0]);
}
// DDS BC1/BC2/BC3: upload do nivel 0 sem descompressao ou d3dcompiler_47.dll.
inline ID3D11ShaderResourceView* loadDDS(ID3D11Device* device,const std::string& path) {
    std::vector<uint8_t> d;
    if(!loadBytes(path,d,128*1024*1024)||d.size()<128)return 0;
    if(memcmp(&d[0],"DDS ",4)!=0||ddsU32(d,4)!=124)return 0;
    const uint32_t height=ddsU32(d,12),width=ddsU32(d,16),fourcc=ddsU32(d,88);
    if(width==0||height==0||width>8192||height>8192)return 0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    size_t at=128;unsigned blockBytes=0;
    if(fourcc==0x31545844u){format=DXGI_FORMAT_BC1_UNORM;blockBytes=8;}
    else if(fourcc==0x33545844u){format=DXGI_FORMAT_BC2_UNORM;blockBytes=16;}
    else if(fourcc==0x35545844u){format=DXGI_FORMAT_BC3_UNORM;blockBytes=16;}
    else if(fourcc==0x30315844u && d.size()>=148) {
        uint32_t dxgi=ddsU32(d,128);at=148;
        if(dxgi==71||dxgi==72){format=DXGI_FORMAT_BC1_UNORM;blockBytes=8;}
        else if(dxgi==74||dxgi==75){format=DXGI_FORMAT_BC2_UNORM;blockBytes=16;}
        else if(dxgi==77||dxgi==78){format=DXGI_FORMAT_BC3_UNORM;blockBytes=16;}
    }
    if(format==DXGI_FORMAT_UNKNOWN)return 0;
    const size_t rows=(height+3)/4,columns=(width+3)/4;
    const size_t pitch=columns*blockBytes,bytes=rows*pitch;
    if(bytes>d.size()-at)return 0;
    D3D11_TEXTURE2D_DESC td={};
    td.Width=width;td.Height=height;td.MipLevels=1;td.ArraySize=1;
    td.Format=format;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;
    td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init={};
    init.pSysMem=&d[at];init.SysMemPitch=(UINT)pitch;
    ID3D11Texture2D* texture=0;
    if(FAILED(device->CreateTexture2D(&td,&init,&texture)))return 0;
    ID3D11ShaderResourceView* view=0;
    device->CreateShaderResourceView(texture,0,&view);
    texture->Release();return view;
}
inline ID3D11ShaderResourceView* loadTGA(ID3D11Device* device,const std::string& path){
    std::vector<uint8_t> d;
    if(!loadBytes(path,d,128*1024*1024)||d.size()<18)return 0;
    const unsigned w=d[12]|(d[13]<<8),h=d[14]|(d[15]<<8);
    const unsigned depth=d[16],type=d[2],id=d[0];
    if(d[1]!=0||(type!=2&&type!=10)||(depth!=24&&depth!=32)||
       w==0||h==0||w>8192||h>8192)return 0;
    const size_t bytesPerPixel=depth/8;
    size_t at=18u+id;if(at>d.size())return 0;
    std::vector<uint8_t> rgba(size_t(w)*h*4);
    unsigned index=0;
    while(index<(unsigned)(w*h)){
        unsigned run=1;bool rle=false;
        if(type==10) {
            if(at>=d.size())return 0;
            const uint8_t packet=d[at++];
            run=(packet&0x7F)+1;rle=(packet&0x80)!=0;
        }
        if(index+run>(unsigned)(w*h))return 0;
        uint8_t pixel[4]={0,0,0,255};
        for(unsigned j=0;j<run;j++){
            if(!rle||j==0){
                if(d.size()-at<bytesPerPixel)return 0;
                pixel[2]=d[at];pixel[1]=d[at+1];pixel[0]=d[at+2];
                pixel[3]=bytesPerPixel==4?d[at+3]:255;at+=bytesPerPixel;
            }
            unsigned row=index/w,col=index%w;
            if(!(d[17]&0x20))row=h-1-row;
            if(d[17]&0x10)col=w-1-col;
            size_t k=(size_t(row)*w+col)*4;
            memcpy(&rgba[k],pixel,4);
            ++index;
        }
    }
    return createTextureFromRGBA(device,&rgba[0],w,h);
}
inline ID3D11ShaderResourceView* loadTexture(ID3D11Device* device,const std::string& path){
    const std::string n=lower(path);
    if(n.size()>=4&&n.substr(n.size()-4)==".dds")return loadDDS(device,path);
    if(n.size()>=4&&n.substr(n.size()-4)==".tga")return loadTGA(device,path);
    std::wstring wide=fileWide(path);
    if(wide.empty())return 0;
    return loadWIC(device,wide.c_str());
}
struct DrawPart {
    ID3D11Buffer* indices;
    ID3D11ShaderResourceView* texture; // owned by Bus::textureCache
    UINT count;
    float rgba[4];
    DrawPart():indices(0),texture(0),count(0){for(int i=0;i<4;i++)rgba[i]=1;}
};
struct GpuMesh {
    ID3D11Buffer* vertices;
    std::vector<DrawPart> parts;
    std::vector<Animation> animations;
    float pivot[16];
    int nativeWheel; // -1: no wheel animation; 0..5: FL FR RL2 RR2 RL RR
    float wheelPivot[3],wheelRadius;
    GpuMesh():vertices(0),nativeWheel(-1),wheelRadius(0.48f){
        memset(pivot,0,sizeof(pivot));
        for(int i=0;i<4;i++)pivot[i*4+i]=1;
        for(int i=0;i<3;i++)wheelPivot[i]=0.0f;
    }
};
struct Bus {
    std::vector<GpuMesh> meshes;
    std::map<std::string,ID3D11ShaderResourceView*> textureCache;
    std::string report,source;
    unsigned imported,missing;
    Bus():imported(0),missing(0){}
    void clear() {
        for(size_t i=0;i<meshes.size();i++){
            if(meshes[i].vertices)meshes[i].vertices->Release();
            for(size_t j=0;j<meshes[i].parts.size();j++)
                if(meshes[i].parts[j].indices)meshes[i].parts[j].indices->Release();
        }
        meshes.clear();
        for(std::map<std::string,ID3D11ShaderResourceView*>::iterator i=textureCache.begin();
            i!=textureCache.end();++i)if(i->second)i->second->Release();
        textureCache.clear();report.clear();source.clear();imported=missing=0;
    }
    ~Bus(){clear();}
};
inline std::string rootFor(const std::string& source) {
    const std::string extension=lower(source.substr(source.find_last_of('.')==std::string::npos?source.size():source.find_last_of('.')));
    if(extension==".bus"||extension==".ovh")return directory(source);
    const std::string dir=directory(source);
    // Se for model.cfg dentro de /model/, devolva pasta pai.
    std::string d=dir;
    if(!d.empty() && d[d.size()-1]=='/')d.resize(d.size()-1);
    if(lower(filename(d))=="model")return directory(d);
    return dir;
}
inline ID3D11ShaderResourceView* findTexture(ID3D11Device* device,Bus& bus,
                                              const std::string& meshPath,const std::string& name) {
    if(name.empty())return 0;
    const std::string root=rootFor(bus.source),meshDir=directory(meshPath);
    const std::string paths[]={
        join(root+"Texture/",name),join(root+"texture/",name),
        join(root,name),join(meshDir+"Texture/",name),
        join(meshDir,name),normalized(name)
    };
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
        const std::string key=lower(paths[i]);
        std::map<std::string,ID3D11ShaderResourceView*>::iterator cached=bus.textureCache.find(key);
        if(cached!=bus.textureCache.end())return cached->second;
        std::ifstream probe(paths[i].c_str(),std::ios::binary);
        if(!probe.good())continue;
        probe.close();
        ID3D11ShaderResourceView* view=loadTexture(device,paths[i]);
        bus.textureCache[key]=view;
        if(view)return view;
    }
    return 0;
}
inline bool upload(ID3D11Device* device,Bus& bus,const Mesh& mesh,
                   const MeshEntry& entry,const std::string& filename) {
    if(mesh.vertices.empty()||mesh.triangles.empty())return false;
    std::vector<MeshVertex> vertices(mesh.vertices.size());
    for(size_t i=0;i<vertices.size();i++){
        vertices[i].x=mesh.vertices[i].x;
        vertices[i].y=mesh.vertices[i].y;
        vertices[i].z=mesh.vertices[i].z;
        vertices[i].u=mesh.vertices[i].u;
        vertices[i].v=mesh.vertices[i].v;
    }
    if(vertices.size()>UINT32_MAX/sizeof(MeshVertex))return false;
    D3D11_BUFFER_DESC bd={};
    bd.ByteWidth=(UINT)(vertices.size()*sizeof(MeshVertex));
    bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    bd.Usage=D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA init={};init.pSysMem=&vertices[0];
    GpuMesh gpu;
    if(FAILED(device->CreateBuffer(&bd,&init,&gpu.vertices)))return false;
    gpu.animations=entry.animations;
    memcpy(gpu.pivot,mesh.pivot,sizeof(gpu.pivot));
    for(size_t material=0;material<mesh.materials.size();material++){
        std::vector<uint32_t> indices;
        for(size_t i=0;i<mesh.triangles.size();i++){
            const Triangle& t=mesh.triangles[i];
            if(t.material!=material && !(material==0 && t.material>=mesh.materials.size()))continue;
            indices.push_back(t.a);indices.push_back(t.b);indices.push_back(t.c);
        }
        if(indices.empty()||indices.size()>UINT32_MAX/sizeof(uint32_t))continue;
        DrawPart part;
        bd.ByteWidth=(UINT)(indices.size()*sizeof(uint32_t));
        bd.BindFlags=D3D11_BIND_INDEX_BUFFER;
        init.pSysMem=&indices[0];
        if(FAILED(device->CreateBuffer(&bd,&init,&part.indices)))continue;
        part.count=(UINT)indices.size();
        memcpy(part.rgba,mesh.materials[material].rgba,sizeof(part.rgba));
        part.rgba[3]=1.0f; // alpha do material O3D tambem pode ser mascara de reflexao
        part.texture=findTexture(device,bus,filename,mesh.materials[material].texture);
        gpu.parts.push_back(part);
    }
    if(gpu.parts.empty()){gpu.vertices->Release();return false;}
    bus.meshes.push_back(gpu);return true;
}
#ifdef BUS_HAS_ASSIMP
inline bool loadAssimpMesh(ID3D11Device* device,Bus& bus,const MeshEntry& entry) {
    // Importa .x e .3ds diretamente com Assimp i686 habilitado.
    Assimp::Importer importer;
    const aiScene* scene=importer.ReadFile(entry.path,
        aiProcess_Triangulate|aiProcess_PreTransformVertices|
        aiProcess_JoinIdenticalVertices|aiProcess_FlipUVs);
    if(!scene||!scene->HasMeshes()){
        if(bus.report.size()<12000)bus.report+="Assimp failed: "+entry.path+" : "+importer.GetErrorString()+"\n";
        return false;
    }
    if(bus.report.size()<12000){char info[128];sprintf(info,"Assimp scene: %u meshes, %u materials\\n",scene->mNumMeshes,scene->mNumMaterials);bus.report+=info;}
    bool any=false;
    for(unsigned m=0;m<scene->mNumMeshes;m++) {
        const aiMesh* src=scene->mMeshes[m];
        if(!src||!src->HasPositions()||!src->HasFaces()||
           src->mNumVertices>1500000)continue;
        Mesh mesh;mesh.vertices.resize(src->mNumVertices);
        mesh.materials.resize(1);
        if(src->mMaterialIndex<scene->mNumMaterials){
            aiMaterial* material=scene->mMaterials[src->mMaterialIndex];
            aiString filename;
            if(material->GetTexture(aiTextureType_DIFFUSE,0,&filename)==AI_SUCCESS)
                mesh.materials[0].texture=filename.C_Str();
            aiColor4D diffuse;
            if(aiGetMaterialColor(material,AI_MATKEY_COLOR_DIFFUSE,&diffuse)==AI_SUCCESS) {
                mesh.materials[0].rgba[0]=diffuse.r;
                mesh.materials[0].rgba[1]=diffuse.g;
                mesh.materials[0].rgba[2]=diffuse.b;
                mesh.materials[0].rgba[3]=diffuse.a;
            }
        }
        for(unsigned v=0;v<src->mNumVertices;v++) {
            Vertex& out=mesh.vertices[v];
            out.x=src->mVertices[v].x;out.y=src->mVertices[v].y;out.z=src->mVertices[v].z;
            out.nx=src->HasNormals()?src->mNormals[v].x:0;
            out.ny=src->HasNormals()?src->mNormals[v].y:1;
            out.nz=src->HasNormals()?src->mNormals[v].z:0;
            out.u=src->HasTextureCoords(0)?src->mTextureCoords[0][v].x:0;
            out.v=src->HasTextureCoords(0)?src->mTextureCoords[0][v].y:0;
        }
        for(unsigned t=0;t<src->mNumFaces;t++) {
            const aiFace& face=src->mFaces[t];
            if(face.mNumIndices!=3)continue;
            Triangle tri={face.mIndices[0],face.mIndices[1],face.mIndices[2],0};
            mesh.triangles.push_back(tri);
        }
        if(upload(device,bus,mesh,entry,entry.path))any=true;
        else if(bus.report.size()<12000){char info[128];sprintf(info,"GPU upload failed: mesh %u, vertices %u, triangles %u\\n",m,(unsigned)mesh.vertices.size(),(unsigned)mesh.triangles.size());bus.report+=info;}
    }
    return any;
}
#endif

inline bool loadNative3DS(ID3D11Device* device,Bus& bus,const MeshEntry& entry){
    std::vector<Mesh> meshes;
    std::string error;
    if(!read3DS(entry.path,meshes,&error)){
        if(bus.report.size()<12000)bus.report+="Native 3DS error: "+entry.path+": "+error+"\n";
        return false;
    }
    WheelPivot3DS wheelRigs[6];
    find3DSWheelPivots(meshes,wheelRigs);
    unsigned drawable=0,animatedParts=0;
    size_t vertices=0,triangles=0;
    for(size_t i=0;i<meshes.size();i++){
        vertices+=meshes[i].vertices.size();
        triangles+=meshes[i].triangles.size();
        const size_t before=bus.meshes.size();
        if(!upload(device,bus,meshes[i],entry,entry.path))continue;
        ++drawable;
        const int wheel=wheelGroup3DS(meshes[i].objectName);
        if(wheel<0 || !wheelRigs[wheel].valid)continue;
        for(size_t j=before;j<bus.meshes.size();j++){
            GpuMesh& gpu=bus.meshes[j];
            gpu.nativeWheel=wheel;
            gpu.wheelPivot[0]=wheelRigs[wheel].x;
            gpu.wheelPivot[1]=wheelRigs[wheel].y;
            gpu.wheelPivot[2]=wheelRigs[wheel].z;
            gpu.wheelRadius=wheelRigs[wheel].radius;
            ++animatedParts;
        }
    }
    char stats[256];
    sprintf(stats,"Native 3DS: %u/%u meshes on GPU, %u vertices, %u triangles\n",
            drawable,(unsigned)meshes.size(),(unsigned)vertices,(unsigned)triangles);
    if(bus.report.size()<12000)bus.report+=stats;
    unsigned detected=0;
    for(int i=0;i<6;i++)if(wheelRigs[i].valid)++detected;
    sprintf(stats,"Native 3DS wheels: %u/6 groups, %u animated parts (steer front, spin all)\n",
            detected,animatedParts);
    if(bus.report.size()<12000)bus.report+=stats;
    if(drawable==0 && bus.report.size()<12000)
        bus.report+="Native 3DS parsed but no meshes uploaded; inspect Direct3D resources.\n";
    return drawable>0;
}
inline bool load(ID3D11Device* device,const std::string& path,Bus& bus) {
    bus.clear();bus.source=normalized(path);
    std::vector<MeshEntry> entries;std::string err;
    if(!readModelList(bus.source,entries,&err)){bus.report=err;return false;}
    for(size_t i=0;i<entries.size();i++){
        std::string name=lower(entries[i].path);
        if(name.size()>=4 && name.substr(name.size()-4)==".3ds"){
            if(loadNative3DS(device,bus,entries[i]))++bus.imported;
            else ++bus.missing;
            continue;
        }
        if(name.size()>=2 && name.substr(name.size()-2)==".x") {
#ifdef BUS_HAS_ASSIMP
            if(loadAssimpMesh(device,bus,entries[i]))++bus.imported;
            else {++bus.missing;if(bus.report.size()<12000)bus.report+="Failed Assimp mesh "+entries[i].path+"\n";}
#else
            ++bus.missing;
            if(bus.report.size()<1200)bus.report+=".x requires BUS_WITH_ASSIMP=ON: "+entries[i].path+"\n";
#endif
            continue;
        }
        if(name.size()<4||name.substr(name.size()-4)!=".o3d"){
            ++bus.missing;
            if(bus.report.size()<1200)bus.report+="Unknown mesh type: "+entries[i].path+"\n";
            continue;
        }
        Mesh mesh;std::string reason;
        if(!readO3D(entries[i].path,mesh,&reason)||
           !upload(device,bus,mesh,entries[i],entries[i].path)) {
            ++bus.missing;
            if(bus.report.size()<1200)bus.report+="Skipped "+entries[i].path+": "+reason+"\n";
            continue;
        }
        ++bus.imported;
    }
    char summary[160];
    sprintf(summary,"OMSI: %u meshes loaded; %u missing / unsupported.\n",bus.imported,bus.missing);
    bus.report=std::string(summary)+bus.report;
    return !bus.meshes.empty();
}
inline DirectX::XMMATRIX wheelTransform(const GpuMesh& mesh,const BusState& state){
    using namespace DirectX;
    if(mesh.nativeWheel<0 || mesh.nativeWheel>5 || mesh.wheelRadius<=0.05f)
        return identity();
    const float x=mesh.wheelPivot[0],y=mesh.wheelPivot[1],z=mesh.wheelPivot[2];
    const float spin=state.wheelRotation*(sim::WHEEL_RADIUS/mesh.wheelRadius);
    const float steer=mesh.nativeWheel<2?state.steer*0.47f:0.0f;
    // Row-vector convention: spin around axle X, steer around vertical Y.
    // Pivots are shared among every part of the same 3DS wheel group.
    return XMMatrixTranslation(-x,-y,-z)*
           XMMatrixRotationX(spin)*XMMatrixRotationY(steer)*
           XMMatrixTranslation(x,y,z);
}
inline float variableValue(const std::string& variable,const BusState& state) {
    const std::string v=lower(variable);
    if(v.find("wheel_rotation_")==0)return state.wheelRotation;
    if(v.find("axle_steering_")==0)return (v.find("axle_steering_0_")==0)?state.steer*0.47f:0;
    if(v.find("axle_suspension_")==0){
        unsigned axle=0;char side='L';
        if(sscanf(v.c_str(),"axle_suspension_%u_%c",&axle,&side)==2 && axle<3){
            const int wheel=(int)axle*2+(side=='r'?1:0);
            return sim::visualTravel(state,wheel)-0.50f;
        }
        return 0;
    }
    if(v.find("door_")==0)return state.door;
    if(v.find("cp_lenkrad")==0||v.find("steering")==0)return state.steer;
    return 0;
}
inline DirectX::XMMATRIX animationTransform(const GpuMesh& mesh,const BusState& state){
    using namespace DirectX;
    XMMATRIX result=identity();
    const float toRad=3.14159265358979323846f/180.0f;
    for(size_t i=0;i<mesh.animations.size();i++){
        const Animation& anim=mesh.animations[i];
        float value=variableValue(anim.variable,state)*anim.factor;
        if(value==0)continue;
        const float x=anim.fromMesh?mesh.pivot[12]:anim.origin[0];
        const float y=anim.fromMesh?mesh.pivot[13]:anim.origin[2];
        const float z=anim.fromMesh?mesh.pivot[14]:anim.origin[1];
        // model.cfg tem X direita, Y frente, Z cima; O3D/D3D tem X direita, Y cima, Z frente.
        XMMATRIX basis=XMMatrixRotationX(anim.rot[0]*toRad)*
                        XMMatrixRotationZ(anim.rot[1]*toRad)*
                        XMMatrixRotationY(anim.rot[2]*toRad);
        XMMATRIX inverse=XMMatrixRotationY(-anim.rot[2]*toRad)*
                          XMMatrixRotationZ(-anim.rot[1]*toRad)*
                          XMMatrixRotationX(-anim.rot[0]*toRad);
        XMMATRIX motion=anim.translation?XMMatrixTranslation(value,0,0):XMMatrixRotationX(value*toRad);
        result=result*(XMMatrixTranslation(-x,-y,-z)*inverse*motion*basis*XMMatrixTranslation(x,y,z));
    }
    return result;
}
} // namespace omsi
