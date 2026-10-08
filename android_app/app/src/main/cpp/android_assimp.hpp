#pragma once
// Optional Assimp reader for Android. No Windows dependency.
// Include only when BUS_ANDROID_HAS_ASSIMP is set by CMake.
#ifdef BUS_ANDROID_HAS_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <cstdio>
#include <vector>
#include <string>
#include "omsi_format.hpp"

namespace androidimport {
inline bool load(const std::string& filename,
                 std::vector<omsi::Mesh>& result,
                 std::string& error) {
    result.clear();
    Assimp::Importer importer;
    const aiScene* scene=importer.ReadFile(filename,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_PreTransformVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_ValidateDataStructure);
    if(!scene || !scene->HasMeshes()) {
        error=importer.GetErrorString();
        return false;
    }
    if(scene->mNumMeshes>4096) {
        error="Assimp: model has too many meshes";
        return false;
    }
    for(unsigned i=0;i<scene->mNumMeshes;++i) {
        const aiMesh* src=scene->mMeshes[i];
        if(!src || !src->HasPositions() || src->mNumVertices>1500000 ||
           src->mNumFaces>2000000)continue;
        omsi::Mesh out;
        out.objectName=src->mName.C_Str();
        out.vertices.reserve(src->mNumVertices);
        for(unsigned v=0;v<src->mNumVertices;++v) {
            omsi::Vertex item={};
            item.x=src->mVertices[v].x;
            item.y=src->mVertices[v].y;
            item.z=src->mVertices[v].z;
            if(src->HasNormals()) {
                item.nx=src->mNormals[v].x;
                item.ny=src->mNormals[v].y;
                item.nz=src->mNormals[v].z;
            }
            if(src->HasTextureCoords(0)) {
                item.u=src->mTextureCoords[0][v].x;
                item.v=1.0f-src->mTextureCoords[0][v].y;
            }
            out.vertices.push_back(item);
        }
        for(unsigned k=0;k<src->mNumFaces;++k) {
            const aiFace& face=src->mFaces[k];
            if(face.mNumIndices!=3)continue;
            omsi::Triangle tri={};
            tri.a=face.mIndices[0];
            tri.b=face.mIndices[1];
            tri.c=face.mIndices[2];
            tri.material=0;
            out.triangles.push_back(tri);
        }
        if(out.triangles.empty())continue;
        omsi::Material material;
        if(src->mMaterialIndex<scene->mNumMaterials) {
            const aiMaterial* mat=scene->mMaterials[src->mMaterialIndex];
            aiColor4D color;
            if(mat->Get(AI_MATKEY_COLOR_DIFFUSE,color)==AI_SUCCESS) {
                material.rgba[0]=color.r;
                material.rgba[1]=color.g;
                material.rgba[2]=color.b;
                material.rgba[3]=color.a;
            }
            float opacity=1.0f;
            if(mat->Get(AI_MATKEY_OPACITY,opacity)==AI_SUCCESS)
                material.rgba[3]*=opacity;
            aiString path;
            if(mat->GetTexture(aiTextureType_DIFFUSE,0,&path)==AI_SUCCESS ||
               mat->GetTexture(aiTextureType_BASE_COLOR,0,&path)==AI_SUCCESS) {
                material.texture=omsi::normalized(path.C_Str());
                if(!material.texture.empty() && material.texture[0]=='*') {
                    unsigned imageIndex=0;
                    if(sscanf(material.texture.c_str(),"*%u",&imageIndex)==1 &&
                       imageIndex<scene->mNumTextures) {
                        const aiTexture* image=scene->mTextures[imageIndex];
                        if(image->mHeight==0 && image->mWidth>0 &&
                           image->mWidth<16u*1024u*1024u) {
                            const bool jpeg=omsi::lower(image->achFormatHint)=="jpg";
                            char suffix[64];
                            snprintf(suffix,sizeof(suffix),"_%u_%u.%s",
                                     i,imageIndex,jpeg?"jpg":"png");
                            const std::string output=filename+suffix;
                            FILE* file=fopen(output.c_str(),"wb");
                            if(file) {
                                const size_t written=fwrite(image->pcData,1,
                                    (size_t)image->mWidth,file);
                                fclose(file);
                                if(written==image->mWidth)
                                    material.texture=output;
                            }
                        }
                    }
                }
            }
        }
        out.materials.push_back(material);
        result.push_back(out);
    }
    if(result.empty())error="Assimp: no drawable meshes";
    return !result.empty();
}
} // namespace androidimport
#endif
