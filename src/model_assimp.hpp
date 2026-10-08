#pragma once
// Compilado apenas com BUS_WITH_ASSIMP=ON e Assimp i686 compativel.
#include <d3d11.h>
#include <string>
#include <vector>
#include "texture_wic.hpp"
#ifdef BUS_HAS_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#endif
struct MeshVertex { float x,y,z,u,v; };
struct ModelSubmesh {
    ID3D11Buffer* vertices;
    ID3D11Buffer* indices;
    ID3D11ShaderResourceView* diffuse;
    UINT count;
    ModelSubmesh():vertices(0),indices(0),diffuse(0),count(0){}
};
struct ModelAsset {
    std::vector<ModelSubmesh> meshes;
    void clear() {
        for(size_t i=0;i<meshes.size();i++) {
            if(meshes[i].vertices)meshes[i].vertices->Release();
            if(meshes[i].indices)meshes[i].indices->Release();
            if(meshes[i].diffuse)meshes[i].diffuse->Release();
        }
        meshes.clear();
    }
    ~ModelAsset(){clear();}
};
#ifdef BUS_HAS_ASSIMP
inline bool loadModelAssimp(ID3D11Device* device,const std::string& path,ModelAsset& asset) {
    asset.clear();
    Assimp::Importer importer;
    const aiScene* scene=importer.ReadFile(path,aiProcess_Triangulate|
          aiProcess_PreTransformVertices|aiProcess_JoinIdenticalVertices|aiProcess_FlipUVs);
    if(!scene||!scene->HasMeshes())return false;
    size_t slash=path.find_last_of("/\\");
    std::string dir=slash==std::string::npos?"":path.substr(0,slash+1);
    for(unsigned m=0;m<scene->mNumMeshes;m++) {
        const aiMesh* src=scene->mMeshes[m];
        if(!src->HasPositions()||!src->HasFaces())continue;
        std::vector<MeshVertex> vertices(src->mNumVertices);
        for(unsigned i=0;i<src->mNumVertices;i++) {
            const aiVector3D& pos=src->mVertices[i];
            vertices[i].x=pos.x;vertices[i].y=pos.y;vertices[i].z=pos.z;
            vertices[i].u=src->HasTextureCoords(0)?src->mTextureCoords[0][i].x:0;
            vertices[i].v=src->HasTextureCoords(0)?src->mTextureCoords[0][i].y:0;
        }
        std::vector<uint32_t> indices;
        for(unsigned i=0;i<src->mNumFaces;i++) {
            const aiFace& face=src->mFaces[i];
            if(face.mNumIndices==3)for(unsigned k=0;k<3;k++)indices.push_back(face.mIndices[k]);
        }
        if(indices.empty())continue;
        ModelSubmesh mesh;
        D3D11_BUFFER_DESC bd={};D3D11_SUBRESOURCE_DATA init={};
        bd.ByteWidth=(UINT)(vertices.size()*sizeof(MeshVertex));
        bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        init.pSysMem=&vertices[0];
        if(FAILED(device->CreateBuffer(&bd,&init,&mesh.vertices)))continue;
        bd.ByteWidth=(UINT)(indices.size()*sizeof(uint32_t));bd.BindFlags=D3D11_BIND_INDEX_BUFFER;
        init.pSysMem=&indices[0];
        if(FAILED(device->CreateBuffer(&bd,&init,&mesh.indices))) {mesh.vertices->Release();continue;}
        mesh.count=(UINT)indices.size();
        if(src->mMaterialIndex<scene->mNumMaterials) {
            aiString textureName;
            if(scene->mMaterials[src->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE,0,&textureName)==AI_SUCCESS) {
                const aiTexture* embedded=scene->GetEmbeddedTexture(textureName.C_Str());
                if(embedded) {
                    if(embedded->mHeight==0) {
                        mesh.diffuse=loadWIC(device,0,(const unsigned char*)embedded->pcData,embedded->mWidth);
                    } else {
                        std::vector<unsigned char> rgba(size_t(embedded->mWidth)*embedded->mHeight*4);
                        for(unsigned i=0;i<embedded->mWidth*embedded->mHeight;i++) {
                            rgba[i*4]=embedded->pcData[i].r;rgba[i*4+1]=embedded->pcData[i].g;
                            rgba[i*4+2]=embedded->pcData[i].b;rgba[i*4+3]=embedded->pcData[i].a;
                        }
                        mesh.diffuse=createTextureFromRGBA(device,&rgba[0],embedded->mWidth,embedded->mHeight);
                    }
                } else {
                    std::wstring full=utf8ToWide(dir+textureName.C_Str());
                    mesh.diffuse=loadWIC(device,full.c_str());
                }
            }
        }
        asset.meshes.push_back(mesh);
    }
    return !asset.meshes.empty();
}
#else
inline bool loadModelAssimp(ID3D11Device*,const std::string&,ModelAsset&) {return false;}
#endif
