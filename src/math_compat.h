#pragma once
#include <math.h>
#include <string.h>
namespace DirectX {
struct XMFLOAT4 { float x,y,z,w; XMFLOAT4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){} };
struct XMFLOAT4X4 {float m[4][4];};
struct XMVECTOR {float x,y,z,w;};
struct XMMATRIX {float m[4][4];};
static const float XM_PIDIV4=0.7853981633974483f;
inline XMVECTOR XMVectorSet(float x,float y,float z,float w){XMVECTOR a={x,y,z,w};return a;}
inline XMMATRIX identity(){XMMATRIX a={};for(int i=0;i<4;++i)a.m[i][i]=1;return a;}
inline XMMATRIX operator*(const XMMATRIX& a,const XMMATRIX& b){XMMATRIX r={};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)r.m[i][j]+=a.m[i][k]*b.m[k][j];return r;}
inline XMMATRIX XMMatrixScaling(float x,float y,float z){XMMATRIX a=identity();a.m[0][0]=x;a.m[1][1]=y;a.m[2][2]=z;return a;}
inline XMMATRIX XMMatrixRotationX(float a){XMMATRIX r=identity();float c=cosf(a),s=sinf(a);r.m[1][1]=c;r.m[1][2]=s;r.m[2][1]=-s;r.m[2][2]=c;return r;}
inline XMMATRIX XMMatrixRotationZ(float a){XMMATRIX r=identity();float c=cosf(a),s=sinf(a);r.m[0][0]=c;r.m[0][1]=s;r.m[1][0]=-s;r.m[1][1]=c;return r;}
inline XMMATRIX XMMatrixRotationY(float a){XMMATRIX r=identity();float c=cosf(a),s=sinf(a);r.m[0][0]=c;r.m[0][2]=-s;r.m[2][0]=s;r.m[2][2]=c;return r;}
inline XMMATRIX XMMatrixTranslation(float x,float y,float z){XMMATRIX r=identity();r.m[3][0]=x;r.m[3][1]=y;r.m[3][2]=z;return r;}
inline XMVECTOR XMVectorZero(){return XMVectorSet(0,0,0,0);}
inline XMVECTOR XMVectorSubtract(XMVECTOR a,XMVECTOR b){return XMVectorSet(a.x-b.x,a.y-b.y,a.z-b.z,a.w-b.w);}
inline float XMVectorGetX(XMVECTOR v){return v.x;}
inline XMVECTOR XMVector3LengthSq(XMVECTOR v){float length=v.x*v.x+v.y*v.y+v.z*v.z;return XMVectorSet(length,length,length,length);}
inline XMVECTOR XMVector3TransformCoord(XMVECTOR v,const XMMATRIX& m){
    const float x=v.x*m.m[0][0]+v.y*m.m[1][0]+v.z*m.m[2][0]+m.m[3][0];
    const float y=v.x*m.m[0][1]+v.y*m.m[1][1]+v.z*m.m[2][1]+m.m[3][1];
    const float z=v.x*m.m[0][2]+v.y*m.m[1][2]+v.z*m.m[2][2]+m.m[3][2];
    const float w=v.x*m.m[0][3]+v.y*m.m[1][3]+v.z*m.m[2][3]+m.m[3][3];
    if(fabsf(w)>1.0e-8f){return XMVectorSet(x/w,y/w,z/w,1.0f);}
    return XMVectorSet(x,y,z,1.0f);
}
inline XMVECTOR sub(XMVECTOR a,XMVECTOR b){return XMVectorSet(a.x-b.x,a.y-b.y,a.z-b.z,0);}
inline XMVECTOR cross(XMVECTOR a,XMVECTOR b){return XMVectorSet(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x,0);}
inline float dot(XMVECTOR a,XMVECTOR b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline XMVECTOR norm(XMVECTOR a){float l=sqrtf(dot(a,a));return l>1.e-8f?XMVectorSet(a.x/l,a.y/l,a.z/l,0):XMVectorSet(0,0,0,0);}
inline XMMATRIX XMMatrixLookAtLH(XMVECTOR eye,XMVECTOR at,XMVECTOR up){XMVECTOR z=norm(sub(at,eye)),x=norm(cross(up,z)),y=cross(z,x);XMMATRIX r=identity();r.m[0][0]=x.x;r.m[0][1]=y.x;r.m[0][2]=z.x;r.m[1][0]=x.y;r.m[1][1]=y.y;r.m[1][2]=z.y;r.m[2][0]=x.z;r.m[2][1]=y.z;r.m[2][2]=z.z;r.m[3][0]=-dot(x,eye);r.m[3][1]=-dot(y,eye);r.m[3][2]=-dot(z,eye);return r;}
inline XMMATRIX XMMatrixPerspectiveFovLH(float fov,float aspect,float zn,float zf){float s=1.0f/tanf(fov*0.5f);XMMATRIX r={};r.m[0][0]=s/aspect;r.m[1][1]=s;r.m[2][2]=zf/(zf-zn);r.m[2][3]=1;r.m[3][2]=-zn*zf/(zf-zn);return r;}
inline void XMStoreFloat4x4(XMFLOAT4X4* dst,const XMMATRIX& m){memcpy(dst->m,m.m,sizeof(m.m));}
}
