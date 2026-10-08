// Android GLES 3.0 client. Original Direct3D 11 client/server remain unchanged.
// Model readers, BUS4 protocol and OMSI script parser are shared with desktop.
#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "android_net.hpp"
#include "omsi_format.hpp"
#include "3ds_format.hpp"
#include "bus_script.hpp"
#include "simulation.hpp"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"Dx11BusAndroid",__VA_ARGS__)

namespace {

struct Mat4 {
    float m[16];
    Mat4() { memset(m,0,sizeof(m));m[0]=m[5]=m[10]=m[15]=1.0f; }
};
Mat4 mul(const Mat4& a,const Mat4& b) {
    Mat4 r;
    memset(r.m,0,sizeof(r.m));
    for(int col=0;col<4;++col)
        for(int row=0;row<4;++row)
            for(int k=0;k<4;++k)
                r.m[col*4+row]+=a.m[k*4+row]*b.m[col*4+k];
    return r;
}
Mat4 scale(float x,float y,float z) {
    Mat4 r;r.m[0]=x;r.m[5]=y;r.m[10]=z;return r;
}
Mat4 translate(float x,float y,float z) {
    Mat4 r;r.m[12]=x;r.m[13]=y;r.m[14]=z;return r;
}
Mat4 rotateY(float a) {
    Mat4 r;const float c=cosf(a),s=sinf(a);
    r.m[0]=c;r.m[2]=-s;r.m[8]=s;r.m[10]=c;return r;
}
Mat4 rotateX(float a) {
    Mat4 r;const float c=cosf(a),s=sinf(a);
    r.m[5]=c;r.m[6]=s;r.m[9]=-s;r.m[10]=c;return r;
}
Mat4 rotateZ(float a) {
    Mat4 r;const float c=cosf(a),s=sinf(a);
    r.m[0]=c;r.m[1]=s;r.m[4]=-s;r.m[5]=c;return r;
}
struct V3 {float x,y,z;};
V3 sub(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V3 unit(V3 a) {
    const float d=sqrtf(a.x*a.x+a.y*a.y+a.z*a.z);
    return d>0.00001f?V3{a.x/d,a.y/d,a.z/d}:V3{0,1,0};
}
float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Mat4 lookAt(V3 eye,V3 center) {
    const V3 f=unit(sub(center,eye));
    const V3 s=unit(cross(f,{0,1,0}));
    const V3 u=cross(s,f);
    Mat4 r;
    r.m[0]=s.x;r.m[4]=s.y;r.m[8]=s.z;r.m[12]=-dot(s,eye);
    r.m[1]=u.x;r.m[5]=u.y;r.m[9]=u.z;r.m[13]=-dot(u,eye);
    r.m[2]=-f.x;r.m[6]=-f.y;r.m[10]=-f.z;r.m[14]=dot(f,eye);
    return r;
}
Mat4 perspective(float fov,float aspect,float nearZ,float farZ) {
    Mat4 r;
    memset(r.m,0,sizeof(r.m));
    const float f=1.0f/tanf(fov*0.5f);
    r.m[0]=f/aspect;r.m[5]=f;r.m[10]=(farZ+nearZ)/(nearZ-farZ);
    r.m[11]=-1.0f;r.m[14]=(2.0f*farZ*nearZ)/(nearZ-farZ);
    return r;
}
struct GpuPart {
    std::vector<uint32_t> indices;
    GLuint ibo;
    GLuint texture;
    float rgba[4];
    float center[3];
    bool glass;
    std::string textureName;
    GpuPart():ibo(0),texture(0),glass(false) {
        rgba[0]=rgba[1]=rgba[2]=rgba[3]=1;
        center[0]=center[1]=center[2]=0;
    }
};
struct GpuMesh {
    std::vector<float> vertices;
    std::vector<GpuPart> parts;
    GLuint vao,vbo;
    int wheel;
    float wheelPivot[3],wheelRadius;
    std::string sourcePath;
    std::vector<omsi::Animation> animations;
    float pivot[16];
    GpuMesh():vao(0),vbo(0),wheel(-1),wheelRadius(0) {
        wheelPivot[0]=wheelPivot[1]=wheelPivot[2]=0;
        memset(pivot,0,sizeof(pivot));
    }
};
std::vector<GpuMesh> models;
std::map<std::string,GLuint> textureCache;
GLuint program=0,gridVao=0,gridVbo=0,boxVao=0,boxVbo=0;
GLint matrixUniform=-1,colorUniform=-1,useTextureUniform=-1;
int viewportW=1,viewportH=1;
int socketFd=-1;
sockaddr_in serverAddr={};
bool connected=false,gotSnapshot=false,cockpit=false;
NetPacket latest={};
uint32_t playerId=0,lastTick=0,pendingFlags=0;
uint64_t lastSend=0,lastReceived=0,flagUntil=0;
float throttle=0,steering=0,brake=0,clutch=0;
float orbitYaw=0,orbitPitch=0.32f,orbitDistance=17.0f;
std::string modelStatus="Modelo padrao",scriptStatus="Nenhum script",networkStatus="Desconectado";
std::string modelRoot;
bool imported=false;
buscfg::ModScripts scripts;

const char* vsCode=
    "#version 300 es\n"
    "layout(location=0) in vec3 aPosition;\n"
    "layout(location=1) in vec2 aUV;\n"
    "uniform mat4 uMatrix;\n"
    "out vec2 vUV;\n"
    "void main(){gl_Position=uMatrix*vec4(aPosition,1.0);vUV=vec2(aUV.x,1.0-aUV.y);}\n";
const char* fsCode=
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 vUV;\n"
    "uniform vec4 uColor;\n"
    "uniform sampler2D uTexture;\n"
    "uniform int uUseTexture;\n"
    "out vec4 fragColor;\n"
    "void main(){vec4 col=uColor;if(uUseTexture==1)col*=texture(uTexture,vUV);"
    "if(col.a<0.025)discard;fragColor=col;}\n";

GLuint shader(GLenum kind,const char* source) {
    const GLuint sh=glCreateShader(kind);
    glShaderSource(sh,1,&source,0);
    glCompileShader(sh);
    GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        char log[1024]={};
        glGetShaderInfoLog(sh,sizeof(log),0,log);
        LOGE("GLSL: %s",log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}
bool initProgram() {
    const GLuint v=shader(GL_VERTEX_SHADER,vsCode),f=shader(GL_FRAGMENT_SHADER,fsCode);
    if(!v||!f)return false;
    program=glCreateProgram();
    glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
    glDeleteShader(v);glDeleteShader(f);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok) {
        char log[1024]={};
        glGetProgramInfoLog(program,sizeof(log),0,log);
        LOGE("OpenGL link: %s",log);
        return false;
    }
    matrixUniform=glGetUniformLocation(program,"uMatrix");
    colorUniform=glGetUniformLocation(program,"uColor");
    useTextureUniform=glGetUniformLocation(program,"uUseTexture");
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program,"uTexture"),0);
    return true;
}
void resetGpuHandles() {
    // Called after SurfaceView context creation/recreation.
    textureCache.clear();
    for(size_t i=0;i<models.size();++i) {
        models[i].vao=0;models[i].vbo=0;
        for(size_t j=0;j<models[i].parts.size();++j) {
            models[i].parts[j].ibo=0;models[i].parts[j].texture=0;
        }
    }
    gridVao=gridVbo=boxVao=boxVbo=program=0;
}
void dropModelGpu() {
    for(size_t i=0;i<models.size();++i) {
        GpuMesh& m=models[i];
        if(m.vbo)glDeleteBuffers(1,&m.vbo);
        if(m.vao)glDeleteVertexArrays(1,&m.vao);
        for(size_t j=0;j<m.parts.size();++j)
            if(m.parts[j].ibo)glDeleteBuffers(1,&m.parts[j].ibo);
    }
    for(std::map<std::string,GLuint>::iterator it=textureCache.begin();it!=textureCache.end();++it)
        if(it->second)glDeleteTextures(1,&it->second);
    textureCache.clear();models.clear();
}
bool exists(const std::string& p) {
    FILE* f=fopen(p.c_str(),"rb");
    if(!f)return false;
    fclose(f);return true;
}
std::string resolveTexture(const std::string& meshPath,const std::string& filename) {
    if(filename.empty())return "";
    std::string root=modelRoot;
    std::string meshDir=omsi::directory(meshPath);
    const std::string candidates[]={
        omsi::join(root+"Texture/",filename),
        omsi::join(root+"texture/",filename),
        omsi::join(meshDir+"Texture/",filename),
        omsi::join(meshDir,filename),
        omsi::join(root,filename)
    };
    for(unsigned i=0;i<sizeof(candidates)/sizeof(candidates[0]);++i)
        if(exists(candidates[i]))return candidates[i];
    return "";
}
// OMSI frequently uses DDS DXT1/DXT3/DXT5 textures, which BitmapFactory
// cannot open. Decode only the top mip level to standard RGBA on the CPU.
uint32_t readU32(const std::vector<uint8_t>& d,size_t p) {
    return (uint32_t)d[p]|((uint32_t)d[p+1]<<8)|
        ((uint32_t)d[p+2]<<16)|((uint32_t)d[p+3]<<24);
}
unsigned expand565(unsigned value,unsigned shift,unsigned bits) {
    return ((value>>shift)&((1u<<bits)-1u))*255u/((1u<<bits)-1u);
}
GLuint uploadRGBA(const std::vector<unsigned char>& pixels,int w,int h) {
    if(pixels.empty() || w<=0 || h<=0)return 0;
    GLuint tex=0;
    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,&pixels[0]);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}
GLuint ddsTexture(const std::string& path) {
    std::vector<uint8_t> bytes;
    if(!omsi::loadBytes(path,bytes,128u*1024u*1024u) || bytes.size()<128 ||
       memcmp(&bytes[0],"DDS ",4)!=0 || readU32(bytes,4)!=124)return 0;
    const unsigned h=readU32(bytes,12),w=readU32(bytes,16);
    if(w==0||h==0||w>8192||h>8192)return 0;
    const uint32_t fourcc=readU32(bytes,84),bits=readU32(bytes,88);
    std::vector<unsigned char> pixels((size_t)w*h*4,255);
    const uint32_t dxt1=0x31545844u,dxt3=0x33545844u,dxt5=0x35545844u;
    if(fourcc==dxt1||fourcc==dxt3||fourcc==dxt5) {
        const size_t blocksX=(w+3)/4,blocksY=(h+3)/4;
        const size_t blockSize=fourcc==dxt1?8:16;
        if(blocksX*blocksY*blockSize>bytes.size()-128)return 0;
        for(size_t by=0;by<blocksY;++by)for(size_t bx=0;bx<blocksX;++bx) {
            const uint8_t* p=&bytes[128+(by*blocksX+bx)*blockSize];
            const uint8_t* colors=p+(fourcc==dxt1?0:8);
            const unsigned a=(unsigned)colors[0]|((unsigned)colors[1]<<8);
            const unsigned b=(unsigned)colors[2]|((unsigned)colors[3]<<8);
            unsigned palette[4][4]={{0}};
            for(int k=0;k<3;++k) {
                const unsigned shift=k==0?11:(k==1?5:0);
                const unsigned bits565=k==1?6:5;
                palette[0][k]=expand565(a,shift,bits565);
                palette[1][k]=expand565(b,shift,bits565);
            }
            palette[0][3]=palette[1][3]=255;
            if(a>b || fourcc!=dxt1) {
                for(int k=0;k<3;++k) {
                    palette[2][k]=(2*palette[0][k]+palette[1][k])/3;
                    palette[3][k]=(palette[0][k]+2*palette[1][k])/3;
                }
                palette[2][3]=palette[3][3]=255;
            } else {
                for(int k=0;k<3;++k) {
                    palette[2][k]=(palette[0][k]+palette[1][k])/2;
                    palette[3][k]=0;
                }
                palette[2][3]=255;palette[3][3]=0;
            }
            uint32_t indices=(uint32_t)colors[4]|((uint32_t)colors[5]<<8)|
                ((uint32_t)colors[6]<<16)|((uint32_t)colors[7]<<24);
            unsigned alphas[8]={255,255,0,0,0,0,0,0};
            uint64_t alphaBits=0;
            if(fourcc==dxt5) {
                alphas[0]=p[0];alphas[1]=p[1];
                if(alphas[0]>alphas[1]) {
                    for(int i=2;i<8;++i)
                        alphas[i]=((8-i)*alphas[0]+(i-1)*alphas[1])/7;
                } else {
                    for(int i=2;i<6;++i)
                        alphas[i]=((6-i)*alphas[0]+(i-1)*alphas[1])/5;
                    alphas[6]=0;alphas[7]=255;
                }
                for(int i=0;i<6;++i)alphaBits|=((uint64_t)p[2+i])<<(8*i);
            }
            for(unsigned yy=0;yy<4;++yy)for(unsigned xx=0;xx<4;++xx) {
                const size_t x=bx*4+xx,y=by*4+yy;
                if(x>=w||y>=h)continue;
                const int pixel=(int)(yy*4+xx);
                const unsigned index=(indices>>(2*pixel))&3u;
                const size_t out=(y*w+x)*4;
                for(int k=0;k<4;++k)pixels[out+k]=(unsigned char)palette[index][k];
                if(fourcc==dxt3) {
                    pixels[out+3]=(unsigned char)(
                        ((p[pixel/2]>>(4*(pixel%2)))&15u)*17u);
                } else if(fourcc==dxt5) {
                    pixels[out+3]=(unsigned char)alphas[(alphaBits>>(3*pixel))&7u];
                }
            }
        }
    } else if(fourcc==0 && bits==32) {
        if((size_t)w*h*4>bytes.size()-128)return 0;
        const uint32_t masks[4]={
            readU32(bytes,92),readU32(bytes,96),
            readU32(bytes,100),readU32(bytes,104)
        };
        for(size_t i=0;i<(size_t)w*h;++i) {
            const uint32_t pixel=readU32(bytes,128+i*4);
            for(int k=0;k<4;++k) {
                const uint32_t mask=masks[k];
                if(mask==0) {
                    pixels[i*4+k]=(unsigned char)(k==3?255:0);
                    continue;
                }
                unsigned shift=0;
                while(shift<32 && (mask&(1u<<shift))==0)++shift;
                const uint32_t range=mask>>shift;
                pixels[i*4+k]=(unsigned char)(((uint64_t)((pixel&mask)>>shift)*255u)/range);
            }
        }
    } else return 0;
    return uploadRGBA(pixels,(int)w,(int)h);
}
GLuint bitmapTexture(JNIEnv* env,const std::string& filename) {
    if(filename.empty())return 0;
    std::map<std::string,GLuint>::iterator found=textureCache.find(filename);
    if(found!=textureCache.end())return found->second;
    GLuint result=0;
    const std::string low=omsi::lower(filename);
    if(low.size()>=4 && low.substr(low.size()-4)==".dds") {
        result=ddsTexture(filename);
        textureCache[filename]=result;
        return result;
    }
    jclass factory=env->FindClass("android/graphics/BitmapFactory");
    if(!factory) {env->ExceptionClear();return 0;}
    jmethodID decode=env->GetStaticMethodID(factory,"decodeFile","(Ljava/lang/String;)Landroid/graphics/Bitmap;");
    if(decode) {
        jstring path=env->NewStringUTF(filename.c_str());
        jobject bitmap=env->CallStaticObjectMethod(factory,decode,path);
        env->DeleteLocalRef(path);
        if(env->ExceptionCheck()) {env->ExceptionClear();bitmap=0;}
        if(bitmap) {
            AndroidBitmapInfo info={};
            void* pixels=0;
            if(AndroidBitmap_getInfo(env,bitmap,&info)==ANDROID_BITMAP_RESULT_SUCCESS &&
               info.format==ANDROID_BITMAP_FORMAT_RGBA_8888 &&
               info.width>0 && info.height>0 &&
               info.width<=8192 && info.height<=8192 &&
               AndroidBitmap_lockPixels(env,bitmap,&pixels)==ANDROID_BITMAP_RESULT_SUCCESS) {
                // Copy rows: Android bitmaps can have padded row strides.
                std::vector<unsigned char> rgba((size_t)info.width*(size_t)info.height*4);
                for(uint32_t y=0;y<info.height;++y)
                    memcpy(&rgba[(size_t)y*info.width*4],
                           (const unsigned char*)pixels+(size_t)y*info.stride,
                           (size_t)info.width*4);
                AndroidBitmap_unlockPixels(env,bitmap);
                glGenTextures(1,&result);
                glBindTexture(GL_TEXTURE_2D,result);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)info.width,(GLsizei)info.height,
                             0,GL_RGBA,GL_UNSIGNED_BYTE,&rgba[0]);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
                glGenerateMipmap(GL_TEXTURE_2D);
            }
            env->DeleteLocalRef(bitmap);
        }
    }
    env->DeleteLocalRef(factory);
    textureCache[filename]=result;
    return result;
}
bool appendMesh(const omsi::Mesh& source,const std::string& origin,
                const omsi::MeshEntry* entry=0) {
    if(source.vertices.empty()||source.triangles.empty())return false;
    GpuMesh mesh;
    mesh.sourcePath=origin;
    memcpy(mesh.pivot,source.pivot,sizeof(mesh.pivot));
    if(entry)mesh.animations=entry->animations;
    mesh.vertices.reserve(source.vertices.size()*5);
    for(size_t v=0;v<source.vertices.size();++v) {
        const omsi::Vertex& p=source.vertices[v];
        mesh.vertices.push_back(p.x);mesh.vertices.push_back(p.y);
        mesh.vertices.push_back(p.z);mesh.vertices.push_back(p.u);
        mesh.vertices.push_back(p.v);
    }
    const unsigned materials=(unsigned)std::max((size_t)1,source.materials.size());
    for(unsigned m=0;m<materials;++m) {
        GpuPart part;
        const omsi::Material defaultMaterial;
        const omsi::Material& mat=source.materials.empty()?defaultMaterial:source.materials[m];
        memcpy(part.rgba,mat.rgba,sizeof(part.rgba));
        std::string lower=omsi::lower(mat.texture);
        const bool namedGlass=lower.find("glass")!=std::string::npos ||
            lower.find("vidro")!=std::string::npos ||
            lower.find("janela")!=std::string::npos ||
            lower.find("window")!=std::string::npos;
        part.glass=namedGlass || (part.rgba[3]>0.025f && part.rgba[3]<0.995f);
        if(namedGlass && part.rgba[3]>=0.995f)part.rgba[3]=0.30f;
        part.textureName=resolveTexture(origin,mat.texture);
        for(size_t t=0;t<source.triangles.size();++t) {
            const omsi::Triangle& tri=source.triangles[t];
            if(tri.material!=m && !(m==0 && tri.material>=materials))continue;
            part.indices.push_back(tri.a);
            part.indices.push_back(tri.b);
            part.indices.push_back(tri.c);
            const omsi::Vertex& a=source.vertices[tri.a];
            const omsi::Vertex& b=source.vertices[tri.b];
            const omsi::Vertex& c=source.vertices[tri.c];
            part.center[0]+=a.x+b.x+c.x;
            part.center[1]+=a.y+b.y+c.y;
            part.center[2]+=a.z+b.z+c.z;
        }
        if(part.indices.empty())continue;
        for(int axis=0;axis<3;++axis)part.center[axis]/=(float)part.indices.size();
        mesh.parts.push_back(part);
    }
    if(mesh.parts.empty())return false;
    mesh.wheel=omsi::wheelGroup3DS(source.objectName);
    if(mesh.wheel>=0) {
        float minX=source.vertices[0].x,maxX=minX,minY=source.vertices[0].y,maxY=minY;
        float minZ=source.vertices[0].z,maxZ=minZ;
        for(size_t i=1;i<source.vertices.size();++i) {
            const omsi::Vertex& v=source.vertices[i];
            minX=std::min(minX,v.x);maxX=std::max(maxX,v.x);
            minY=std::min(minY,v.y);maxY=std::max(maxY,v.y);
            minZ=std::min(minZ,v.z);maxZ=std::max(maxZ,v.z);
        }
        mesh.wheelPivot[0]=(minX+maxX)*0.5f;
        mesh.wheelPivot[1]=(minY+maxY)*0.5f;
        mesh.wheelPivot[2]=(minZ+maxZ)*0.5f;
        mesh.wheelRadius=std::max(maxY-minY,maxZ-minZ)*0.5f;
    }
    models.push_back(mesh);
    return true;
}
void appendFallback() {
    // Simple visible bus body when no OMSI mesh has been imported.
    omsi::Mesh mesh;
    const float v[][3]={
        {-1,0,-3.8f},{1,0,-3.8f},{1,2.6f,-3.8f},{-1,2.6f,-3.8f},
        {-1,0,3.8f},{1,0,3.8f},{1,2.6f,3.8f},{-1,2.6f,3.8f}
    };
    for(int i=0;i<8;++i) {
        omsi::Vertex p={};
        p.x=v[i][0];p.y=v[i][1];p.z=v[i][2];
        p.u=(i&1)?1:0;p.v=(i&2)?1:0;
        mesh.vertices.push_back(p);
    }
    const int face[][4]={
        {0,1,2,3},{4,7,6,5},{0,4,5,1},
        {3,2,6,7},{1,5,6,2},{0,3,7,4}
    };
    for(int i=0;i<6;++i)for(int j=0;j<2;++j) {
        omsi::Triangle t={};
        t.a=face[i][0];t.b=face[i][1+j];t.c=face[i][j+2];
        t.material=(uint16_t)(i==3?1:0);
        mesh.triangles.push_back(t);
    }
    omsi::Material body,windows;
    body.rgba[0]=0.86f;body.rgba[1]=0.30f;body.rgba[2]=0.15f;
    windows.rgba[0]=0.16f;windows.rgba[1]=0.65f;
    windows.rgba[2]=0.80f;windows.rgba[3]=0.40f;
    mesh.materials.push_back(body);mesh.materials.push_back(windows);
    appendMesh(mesh,"");
}
bool loadModelFile(const std::string& path) {
    std::vector<omsi::MeshEntry> entries;
    std::string error;
    if(!omsi::readModelList(path,entries,&error)) {
        modelStatus="Modelo invalido: "+error;
        return false;
    }
    dropModelGpu();
    modelRoot=omsi::directory(path);
    if(omsi::lower(omsi::filename(modelRoot.substr(0,modelRoot.size()-(!modelRoot.empty()?1:0))))=="model")
        modelRoot=omsi::directory(modelRoot.substr(0,modelRoot.size()-1));
    int ok=0,failed=0;
    for(size_t i=0;i<entries.size();++i) {
        const std::string file=entries[i].path,lower=omsi::lower(file);
        if(lower.size()>=4 && lower.substr(lower.size()-4)==".o3d") {
            omsi::Mesh m;
            if(omsi::readO3D(file,m,&error)&&appendMesh(m,file,&entries[i]))++ok;
            else ++failed;
        } else if(lower.size()>=4 && lower.substr(lower.size()-4)==".3ds") {
            std::vector<omsi::Mesh> meshes;
            if(omsi::read3DS(file,meshes,&error)) {
                for(size_t j=0;j<meshes.size();++j)
                    if(appendMesh(meshes[j],file,&entries[i]))++ok;else ++failed;
            } else ++failed;
        } else {
            ++failed; // .x, FBX and GLB require an additional importer.
        }
    }
    if(models.empty()) {
        appendFallback();
        imported=false;
        modelStatus="Falha ao importar modelo: "+error;
        return false;
    }
    imported=true;
    char result[192];
    snprintf(result,sizeof(result),"Modelo: %d malhas, %d ausentes/nao suportadas",ok,failed);
    modelStatus=result;
    return true;
}
void prepareGpu(JNIEnv* env) {
    for(size_t i=0;i<models.size();++i) {
        GpuMesh& mesh=models[i];
        if(mesh.vao)continue;
        glGenVertexArrays(1,&mesh.vao);
        glBindVertexArray(mesh.vao);
        glGenBuffers(1,&mesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER,mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,mesh.vertices.size()*sizeof(float),&mesh.vertices[0],GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
        for(size_t j=0;j<mesh.parts.size();++j) {
            GpuPart& p=mesh.parts[j];
            glGenBuffers(1,&p.ibo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,p.ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,p.indices.size()*sizeof(uint32_t),&p.indices[0],GL_STATIC_DRAW);
            p.texture=bitmapTexture(env,p.textureName);
        }
    }
    glBindVertexArray(0);
}
void drawPart(GpuMesh& mesh,GpuPart& p,const Mat4& matrix) {
    glUniformMatrix4fv(matrixUniform,1,GL_FALSE,matrix.m);
    glUniform4fv(colorUniform,1,p.rgba);
    glUniform1i(useTextureUniform,p.texture?1:0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,p.texture);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,p.ibo);
    glDrawElements(GL_TRIANGLES,(GLsizei)p.indices.size(),GL_UNSIGNED_INT,0);
}
Mat4 busMatrix(const BusState& b) {
    return mul(translate(b.x,b.y-1.6f,b.z),
        mul(rotateY(b.heading),mul(rotateX(b.pitch),rotateZ(b.roll))));
}
float animationValue(const std::string& variable,const BusState& b) {
    const std::string name=omsi::lower(variable);
    if(name.find("wheel_rotation_")==0)return b.wheelRotation;
    if(name.find("axle_steering_")==0)
        return sim::wheelSteerAngle(b,name.find("_r")!=std::string::npos?1:0);
    if(name.find("axle_suspension_")==0)return 0.0f;
    if(name.find("door_")==0)return b.door;
    if(name.find("cp_lenkrad")==0||name.find("steering")==0)return b.steer;
    return 0.0f;
}
Mat4 meshMatrix(const GpuMesh& mesh,const BusState& b,const Mat4& world) {
    Mat4 result=world;
    if(mesh.wheel>=0 && mesh.wheelRadius>=0.05f) {
        const float x=mesh.wheelPivot[0],y=mesh.wheelPivot[1],z=mesh.wheelPivot[2];
        const float steer=mesh.wheel<2?sim::wheelSteerAngle(b,mesh.wheel):0.0f;
        const float spin=b.wheelRotation*(0.48f/mesh.wheelRadius);
        const Mat4 local=mul(translate(x,y,z),
            mul(rotateY(steer),mul(rotateX(spin),translate(-x,-y,-z))));
        result=mul(result,local);
    }
    for(size_t i=0;i<mesh.animations.size();++i) {
        const omsi::Animation& animation=mesh.animations[i];
        const float value=animationValue(animation.variable,b)*animation.factor;
        if(fabsf(value)<0.000001f)continue;
        const float x=animation.fromMesh?mesh.pivot[12]:animation.origin[0];
        const float y=animation.fromMesh?mesh.pivot[13]:animation.origin[2];
        const float z=animation.fromMesh?mesh.pivot[14]:animation.origin[1];
        const float toRadians=0.0174532925199f;
        const Mat4 basis=mul(rotateX(animation.rot[0]*toRadians),
            mul(rotateZ(animation.rot[1]*toRadians),rotateY(animation.rot[2]*toRadians)));
        const Mat4 inverse=mul(rotateY(-animation.rot[2]*toRadians),
            mul(rotateZ(-animation.rot[1]*toRadians),rotateX(-animation.rot[0]*toRadians)));
        const Mat4 motion=animation.translation?
            translate(value,0,0):rotateX(value*toRadians);
        const Mat4 local=mul(translate(x,y,z),
            mul(basis,mul(motion,mul(inverse,translate(-x,-y,-z)))));
        result=mul(result,local);
    }
    return result;
}

struct GlassQueue {size_t mesh,part;float distance;Mat4 matrix;};
void drawBus(const Mat4& pv,const BusState& b,V3 camera) {
    const Mat4 world=busMatrix(b);
    std::vector<GlassQueue> glass;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    for(size_t i=0;i<models.size();++i) {
        GpuMesh& mesh=models[i];
        const Mat4 model=meshMatrix(mesh,b,world);
        for(size_t j=0;j<mesh.parts.size();++j) {
            GpuPart& p=mesh.parts[j];
            if(p.glass) {
                V3 center={p.center[0]+b.x,p.center[1]+b.y,p.center[2]+b.z};
                V3 delta=sub(center,camera);
                glass.push_back({i,j,dot(delta,delta),mul(pv,model)});
                continue;
            }
            drawPart(mesh,p,mul(pv,model));
        }
    }
    std::stable_sort(glass.begin(),glass.end(),
        [](const GlassQueue& a,const GlassQueue& b){return a.distance>b.distance;});
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for(size_t i=0;i<glass.size();++i)
        drawPart(models[glass[i].mesh],models[glass[i].mesh].parts[glass[i].part],glass[i].matrix);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
void initBox() {
    const float corners[8][3]={
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    };
    const int faces[6][4]={
        {0,3,2,1},{4,5,6,7},{0,1,5,4},
        {3,7,6,2},{1,2,6,5},{0,4,7,3}
    };
    const float uv[4][2]={{0,0},{0,1},{1,1},{1,0}};
    std::vector<float> triangles;
    triangles.reserve(36*5);
    for(int i=0;i<6;++i) {
        const int v[6]={0,1,2,0,2,3};
        for(int j=0;j<6;++j) {
            int k=v[j];
            const float* p=corners[faces[i][k]];
            triangles.push_back(p[0]);
            triangles.push_back(p[1]);
            triangles.push_back(p[2]);
            triangles.push_back(uv[k][0]);
            triangles.push_back(uv[k][1]);
        }
    }
    glGenVertexArrays(1,&boxVao);glBindVertexArray(boxVao);
    glGenBuffers(1,&boxVbo);glBindBuffer(GL_ARRAY_BUFFER,boxVbo);
    glBufferData(GL_ARRAY_BUFFER,triangles.size()*sizeof(float),&triangles[0],GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glBindVertexArray(0);
}
void drawBox(const Mat4& pv,float x,float y,float z,float sx,float sy,float sz,
             float red,float green,float blue) {
    if(!boxVao)return;
    const Mat4 matrix=mul(pv,mul(translate(x,y,z),scale(sx,sy,sz)));
    glUniformMatrix4fv(matrixUniform,1,GL_FALSE,matrix.m);
    glUniform4f(colorUniform,red,green,blue,1.0f);
    glUniform1i(useTextureUniform,0);
    glBindVertexArray(boxVao);
    glDrawArrays(GL_TRIANGLES,0,36);
}
void drawCity(const Mat4& pv,const BusState& focus) {
    // Same procedural map definition as the Windows client, with a mobile
    // visibility radius to avoid excessive draw calls.
    const int cx=sim::roadIndex(focus.x);
    const int cz=sim::roadIndex(focus.z);
    const int radius=4;
    const int minX=std::max(-sim::MAP_GRID_RADIUS,cx-radius);
    const int maxX=std::min(sim::MAP_GRID_RADIUS,cx+radius);
    const int minZ=std::max(-sim::MAP_GRID_RADIUS,cz-radius);
    const int maxZ=std::min(sim::MAP_GRID_RADIUS,cz+radius);
    drawBox(pv,0,-0.30f,0,sim::MAP_HALF_EXTENT,0.30f,sim::MAP_HALF_EXTENT,
            0.26f,0.39f,0.20f);
    for(int i=minX;i<=maxX;++i) {
        float x=i*sim::MAP_ROAD_SPACING;
        drawBox(pv,x,0.001f,0,7,0.02f,sim::MAP_HALF_EXTENT,
                0.55f,0.57f,0.61f);
    }
    for(int j=minZ;j<=maxZ;++j) {
        float z=j*sim::MAP_ROAD_SPACING;
        drawBox(pv,0,0.001f,z,sim::MAP_HALF_EXTENT,0.02f,7,
                0.55f,0.57f,0.61f);
    }
    for(int i=minX;i<=maxX;++i) {
        float x=i*sim::MAP_ROAD_SPACING;
        for(int j=cz-2;j<=cz+2;++j) {
            if(j < -sim::MAP_GRID_RADIUS||j>=sim::MAP_GRID_RADIUS)continue;
            for(int d=0;d<3;++d) {
                float z=((float)j+0.20f+0.30f*d)*sim::MAP_ROAD_SPACING;
                drawBox(pv,x,0.03f,z,0.085f,0.021f,2.2f,1.0f,0.89f,0.44f);
            }
        }
    }
    for(int j=minZ;j<=maxZ;++j) {
        float z=j*sim::MAP_ROAD_SPACING;
        for(int i=cx-2;i<=cx+2;++i) {
            if(i < -sim::MAP_GRID_RADIUS||i>=sim::MAP_GRID_RADIUS)continue;
            for(int d=0;d<3;++d) {
                float x=((float)i+0.20f+0.30f*d)*sim::MAP_ROAD_SPACING;
                drawBox(pv,x,0.03f,z,2.2f,0.021f,0.085f,1.0f,0.89f,0.44f);
            }
        }
    }
    for(int i=minX;i<=std::min(sim::MAP_GRID_RADIUS-1,maxX);++i) {
        for(int j=minZ;j<=std::min(sim::MAP_GRID_RADIUS-1,maxZ);++j) {
            const sim::Box b=sim::building(i,j);
            const int hash=(abs(i)*7+abs(j)*13+17);
            const float h=5.0f+(hash%5)*1.4f;
            const float green=0.58f+(hash%3)*0.05f;
            drawBox(pv,b.x,h*0.5f,b.z,b.hx,h*0.5f,b.hz,
                    0.58f,green,0.54f);
            drawBox(pv,b.x,h+0.20f,b.z,b.hx+0.3f,0.25f,b.hz+0.3f,
                    0.24f,0.26f,0.31f);
        }
    }
    for(int i=0;i<sim::STOP_COUNT;++i) {
        const sim::Stop stop=sim::stop(i);
        if(fabsf(stop.x-focus.x)>280 || fabsf(stop.z-focus.z)>280)continue;
        drawBox(pv,stop.x+2.4f,1.35f,stop.z,0.065f,1.35f,0.065f,
                0.50f,0.50f,0.56f);
        drawBox(pv,stop.x+2.4f,2.58f,stop.z,0.85f,0.27f,0.09f,
                0.15f,0.38f,0.85f);
        for(int p=0;p<3;++p) {
            float x=stop.x+2.3f+(p%2)*0.75f;
            float z=stop.z+2.8f+p;
            drawBox(pv,x,0.86f,z,0.18f,0.58f,0.18f,0.25f,0.30f,0.72f);
            drawBox(pv,x,1.60f,z,0.17f,0.17f,0.17f,0.95f,0.69f,0.45f);
        }
    }
}
void initGrid() {
    std::vector<float> lines;
    // 120m x 120m grid, road-like neutral ground guide.
    for(int i=-12;i<=12;++i) {
        const float s=i*5.0f;
        const float pts[]={s,-0.80f,-60,0,0,s,-0.80f,60,1,0,
                           -60,-0.80f,s,0,0,60,-0.80f,s,1,0};
        lines.insert(lines.end(),pts,pts+20);
    }
    glGenVertexArrays(1,&gridVao);glBindVertexArray(gridVao);
    glGenBuffers(1,&gridVbo);glBindBuffer(GL_ARRAY_BUFFER,gridVbo);
    glBufferData(GL_ARRAY_BUFFER,lines.size()*sizeof(float),&lines[0],GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glBindVertexArray(0);
}
void updateNetwork() {
    if(!connected || socketFd<0)return;
    const uint64_t now=androidnet::millis();
    if(now-lastSend>=50) {
        NetPacket input;
        androidnet::initPacket(input,PACKET_INPUT);
        input.clientId=playerId;
        input.throttle=throttle;
        input.steering=steering;
        input.brake=brake;
        input.flags=pendingFlags;
        if(clutch>0.5f)input.flags|=INPUT_CLUTCH;
        sendto(socketFd,&input,sizeof(input),0,(sockaddr*)&serverAddr,sizeof(serverAddr));
        lastSend=now;
        if(pendingFlags!=0 && now>=flagUntil)pendingFlags=0;
    }
    NetPacket packet;
    sockaddr_in from={};
    socklen_t fromSize=sizeof(from);
    ssize_t n=0;
    while((n=recvfrom(socketFd,&packet,sizeof(packet),0,(sockaddr*)&from,&fromSize))>0) {
        if(!androidnet::sameAddress(from,serverAddr) ||
            n!=(ssize_t)sizeof(packet) ||
            packet.magic!=BUS_MAGIC || packet.type!=PACKET_WORLD ||
            packet.count>MAX_BUSES ||
            (gotSnapshot && (int32_t)(packet.tick-lastTick)<=0)) {
            fromSize=sizeof(from);continue;
        }
        latest=packet;
        playerId=packet.clientId;
        gotSnapshot=true;
        lastTick=packet.tick;
        lastReceived=now;
        networkStatus="Conectado UDP";
        fromSize=sizeof(from);
    }
    if(now-lastReceived>4000 && gotSnapshot)networkStatus="Sem resposta UDP";
}
bool connectTo(const std::string& ipv4) {
    if(socketFd>=0)close(socketFd);
    socketFd=-1;connected=false;gotSnapshot=false;playerId=0;lastTick=0;
    sockaddr_in address={};
    address.sin_family=AF_INET;address.sin_port=htons(BUS_PORT);
    if(inet_pton(AF_INET,ipv4.c_str(),&address.sin_addr)!=1) {
        networkStatus="IP invalido (use IPv4)";
        return false;
    }
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0||!androidnet::nonblocking(fd)) {
        if(fd>=0)close(fd);
        networkStatus="Falha ao criar socket UDP";
        return false;
    }
    socketFd=fd;serverAddr=address;connected=true;
    lastReceived=androidnet::millis();lastSend=0;
    networkStatus="Aguardando servidor "+ipv4;
    return true;
}
std::string fromJava(JNIEnv* env,jstring value) {
    if(!value)return "";
    const char* c=env->GetStringUTFChars(value,0);
    if(!c)return "";
    const std::string result(c);
    env->ReleaseStringUTFChars(value,c);
    return result;
}
BusState focusBus() {
    BusState bus={};
    bus.y=1.6f;
    if(gotSnapshot)for(uint32_t i=0;i<latest.count;++i)
        if(latest.buses[i].id==playerId)return latest.buses[i];
    return bus;
}
} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_dx11bus_android_BusActivity_nativeInitGL(JNIEnv* env,jclass) {
    resetGpuHandles();
    if(!initProgram()) {
        modelStatus="Falha no GLSL OpenGL ES 3.0";
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE); // O3D models may contain differently wound faces.
    glClearColor(0.47f,0.70f,0.90f,1);
    if(models.empty())appendFallback();
    initGrid();
    initBox();
    prepareGpu(env);
}
extern "C" JNIEXPORT void JNICALL
Java_com_dx11bus_android_BusActivity_nativeResize(JNIEnv*,jclass,jint w,jint h) {
    viewportW=std::max((int)1,(int)w);
    viewportH=std::max((int)1,(int)h);
    glViewport(0,0,viewportW,viewportH);
}
extern "C" JNIEXPORT void JNICALL
Java_com_dx11bus_android_BusActivity_nativeDraw(JNIEnv* env,jclass) {
    updateNetwork();
    glViewport(0,0,viewportW,viewportH);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if(!program)return;
    glUseProgram(program);
    prepareGpu(env);
    const BusState focus=focusBus();
    const float yaw=focus.heading+orbitYaw;
    V3 eye={focus.x-sinf(yaw)*orbitDistance*cosf(orbitPitch),
        focus.y+1.0f+orbitDistance*sinf(orbitPitch),
        focus.z-cosf(yaw)*orbitDistance*cosf(orbitPitch)};
    V3 at={focus.x,focus.y+0.5f,focus.z};
    if(cockpit) {
        eye={focus.x+sinf(focus.heading)*2.9f,focus.y+1.4f,focus.z+cosf(focus.heading)*2.9f};
        at={eye.x+sinf(focus.heading)*20.0f,eye.y-0.15f,eye.z+cosf(focus.heading)*20.0f};
    }
    const Mat4 pv=mul(perspective(0.95f,(float)viewportW/(float)viewportH,0.10f,800.0f),lookAt(eye,at));
    drawCity(pv,focus);
    if(false && gridVao) {
        const Mat4 world=translate(floorf(focus.x/5.0f)*5.0f,0,
                                   floorf(focus.z/5.0f)*5.0f);
        const Mat4 gridMvp=mul(pv,world);
        glUniformMatrix4fv(matrixUniform,1,GL_FALSE,gridMvp.m);
        glUniform4f(colorUniform,0.20f,0.32f,0.26f,1.0f);
        glUniform1i(useTextureUniform,0);
        glBindVertexArray(gridVao);
        glDrawArrays(GL_LINES,0,100);
    }
    if(!gotSnapshot) {
        drawBus(pv,focus,eye);
    } else {
        for(uint32_t i=0;i<latest.count;++i) {
            if(cockpit && latest.buses[i].id==playerId)continue;
            drawBus(pv,latest.buses[i],eye);
        }
    }
    glBindVertexArray(0);
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dx11bus_android_BusActivity_nativeConnect(JNIEnv* env,jclass,jstring ip) {
    return connectTo(fromJava(env,ip))?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dx11bus_android_BusActivity_nativeLoadModel(JNIEnv* env,jclass,jstring file) {
    const bool result=loadModelFile(fromJava(env,file));
    if(result)prepareGpu(env);
    return result?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dx11bus_android_BusActivity_nativeLoadScript(JNIEnv* env,jclass,jstring file) {
    if(scripts.load(fromJava(env,file))) {
        scriptStatus=scripts.diagnostic;
        return JNI_TRUE;
    }
    scriptStatus="Script nao carregado: "+scripts.diagnostic;
    return JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL
Java_com_dx11bus_android_BusActivity_nativeControls(JNIEnv*,jclass,jfloat t,jfloat s,jfloat b,jfloat c) {
    throttle=std::max(-1.0f,std::min(1.0f,(float)t));
    steering=std::max(-1.0f,std::min(1.0f,(float)s));
    brake=std::max(0.0f,std::min(1.0f,(float)b));
    clutch=std::max(0.0f,std::min(1.0f,(float)c));
}
extern "C" JNIEXPORT void JNICALL
Java_com_dx11bus_android_BusActivity_nativeFlag(JNIEnv*,jclass,jint flag) {
    pendingFlags|=(uint32_t)flag;
    flagUntil=androidnet::millis()+300;
}
extern "C" JNIEXPORT void JNICALL
Java_com_dx11bus_android_BusActivity_nativeCamera(JNIEnv*,jclass,jfloat dx,jfloat dy,jfloat zoom) {
    orbitYaw+=(float)dx;
    orbitPitch=std::max(-0.05f,std::min(1.35f,orbitPitch+(float)dy));
    orbitDistance=std::max(3.0f,std::min(80.0f,orbitDistance+(float)zoom));
}
extern "C" JNIEXPORT void JNICALL
Java_com_dx11bus_android_BusActivity_nativeCockpit(JNIEnv*,jclass) {
    cockpit=!cockpit;
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_dx11bus_android_BusActivity_nativeStatus(JNIEnv* env,jclass) {
    const BusState bus=focusBus();
    char text[512];
    snprintf(text,sizeof(text),"%s | ID %u | %.0f km/h | RPM %.0f | %u veiculos\n%s\n%s",
        networkStatus.c_str(),playerId,fabsf(bus.speed)*3.6f,bus.rpm,
        gotSnapshot?latest.count:1,modelStatus.c_str(),scriptStatus.c_str());
    return env->NewStringUTF(text);
}
