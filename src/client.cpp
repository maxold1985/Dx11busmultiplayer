#define _WIN32_WINNT 0x0601
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "protocol.h"
#include "simulation.hpp"
#include "math_compat.h"
#include "audio_motor.hpp"
#include "bus_sound.hpp"
#include "model_assimp.hpp"
#include "omsi_dx11.hpp"
#include <commdlg.h>
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <math.h>
#include <algorithm>
#include <string.h>
using namespace DirectX;

struct SceneConstants {XMFLOAT4X4 transform;XMFLOAT4 color;XMFLOAT4 flags;};
static HWND windowHandle = 0;
static HWND ipLabel = 0;
static HWND ipInput = 0;
static HWND connectButton = 0;
static HWND omsiButton = 0;
static HWND resetButton = 0;
static HWND omsiSteeringButton = 0;
static HWND scriptButton = 0;

static bool resetRequested = false;
static DWORD resetRequestedAt = 0;
static bool omsiSteeringRequested = false;
static DWORD omsiSteeringRequestedAt = 0;
static bool previousCcedillaDown = false;
static uint32_t lastShownSteeringMode = 0xFFFFFFFFu;
static ID3D11Device* device=0;
static ID3D11DeviceContext* context=0;
static IDXGISwapChain* swapChain=0;
static ID3D11RenderTargetView* target=0;
static ID3D11DepthStencilView* depthView=0;
static ID3D11Texture2D* depthTexture=0;
static ID3D11VertexShader* vertexShader=0;
static ID3D11PixelShader* pixelShader=0;
static ID3D11InputLayout* layout=0;
static ID3D11Buffer* cubeVB=0;
static ID3D11Buffer* constants=0;
static ID3D11SamplerState* sampler=0;
static ID3D11BlendState* omsiBlend=0;
static ID3D11DepthStencilState* glassDepthState=0;
static ID3D11ShaderResourceView* roadTexture=0;
static ID3D11ShaderResourceView* busTexture=0;
static ModelAsset busModel;
static omsi::Bus omsiBus;
static bool useOmsi=false;
static bool useModel=false,connected=false,running=true,cockpit=false;
static SOCKET socketUdp=INVALID_SOCKET;
static sockaddr_in serverAddress={};
static uint32_t myId=0,lastTick=0;
static const int WIDTH=1280,HEIGHT=720;
static XMMATRIX cameraMatrix;
static float orbitYaw=0.0f,orbitPitch=0.38f,orbitDistance=16.0f;
static bool orbitDragging=false;
static POINT orbitLast={0,0};
static MotorAudio motor;
static BusSoundPlayer importedBusAudio;
static bool importedAudioReady = false;
static float localThrottle = 0.0f;
struct Snapshot {
    BusState buses[MAX_BUSES];
    uint32_t count;
    DWORD received;
    Snapshot():count(0),received(0){memset(buses,0,sizeof(buses));}
};
static Snapshot earlier,latest;
static bool gotSnapshot=false;
static DWORD lastHud=0,lastNetwork=0;

template<class T> static void releaseObj(T*& obj){if(obj){obj->Release();obj=0;}}
static bool readBinary(const char* name,std::vector<char>& out) {
    FILE* f=fopen(name,"rb");
    if(!f) {
        char path[MAX_PATH]={};GetModuleFileNameA(0,path,MAX_PATH);
        char* slash=strrchr(path,'\\');
        if(slash){*(slash+1)=0;strncat(path,name,MAX_PATH-strlen(path)-1);f=fopen(path,"rb");}
    }
    if(!f)return false;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<=0){fclose(f);return false;}
    out.resize((size_t)size);
    bool ok=fread(&out[0],1,(size_t)size,f)==(size_t)size;
    fclose(f);return ok;
}
static std::string applicationDirectory(){
    char path[MAX_PATH]={};GetModuleFileNameA(0,path,MAX_PATH);
    char* last=strrchr(path,'\\');if(last)last[1]=0;
    return path;
}
static ID3D11ShaderResourceView* makeChecker(unsigned char r,unsigned char g,unsigned char b) {
    const UINT w=64,h=64;std::vector<unsigned char> pixels(w*h*4);
    for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++) {
        bool stripe=(x/8+y/8)%2==0;
        unsigned char v=stripe?255:210;
        size_t k=(size_t(y)*w+x)*4;
        pixels[k]=(unsigned char)(r*v/255);pixels[k+1]=(unsigned char)(g*v/255);
        pixels[k+2]=(unsigned char)(b*v/255);pixels[k+3]=255;
    }
    return createTextureFromRGBA(device,&pixels[0],w,h);
}
static bool initializeGraphics(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sc={};sc.BufferCount=1;
    sc.BufferDesc.Width=WIDTH;sc.BufferDesc.Height=HEIGHT;
    sc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sc.OutputWindow=hwnd;
    sc.SampleDesc.Count=1;sc.Windowed=TRUE;
    sc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL features[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL granted=D3D_FEATURE_LEVEL_10_0;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(0,D3D_DRIVER_TYPE_HARDWARE,0,0,
        features,3,D3D11_SDK_VERSION,&sc,&swapChain,&device,&granted,&context);
    if(FAILED(hr))return false;
    ID3D11Texture2D* back=0;
    if(FAILED(swapChain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&back)))return false;
    hr=device->CreateRenderTargetView(back,0,&target);back->Release();
    if(FAILED(hr))return false;
    D3D11_TEXTURE2D_DESC td={};td.Width=WIDTH;td.Height=HEIGHT;td.MipLevels=1;td.ArraySize=1;
    td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    if(FAILED(device->CreateTexture2D(&td,0,&depthTexture)))return false;
    if(FAILED(device->CreateDepthStencilView(depthTexture,0,&depthView)))return false;
    context->OMSetRenderTargets(1,&target,depthView);
    D3D11_VIEWPORT viewport={};viewport.Width=(float)WIDTH;viewport.Height=(float)HEIGHT;viewport.MaxDepth=1;
    context->RSSetViewports(1,&viewport);
    D3D11_RASTERIZER_DESC raster={};raster.FillMode=D3D11_FILL_SOLID;
    raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    ID3D11RasterizerState* rs=0;
    if(SUCCEEDED(device->CreateRasterizerState(&raster,&rs))){context->RSSetState(rs);rs->Release();}
    std::vector<char> vs,ps;
    if(!readBinary("bus_vs.cso",vs)||!readBinary("bus_ps.cso",ps))return false;
    if(FAILED(device->CreateVertexShader(&vs[0],vs.size(),0,&vertexShader)))return false;
    if(FAILED(device->CreatePixelShader(&ps[0],ps.size(),0,&pixelShader)))return false;
    D3D11_INPUT_ELEMENT_DESC attributes[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}
    };
    if(FAILED(device->CreateInputLayout(attributes,2,&vs[0],vs.size(),&layout)))return false;
    const float positions[36][3]={
        {-1,-1,-1},{-1,1,-1},{1,1,-1}, {-1,-1,-1},{1,1,-1},{1,-1,-1},
        {1,-1,1},{1,1,1},{-1,1,1}, {1,-1,1},{-1,1,1},{-1,-1,1},
        {-1,-1,1},{-1,1,1},{-1,1,-1}, {-1,-1,1},{-1,1,-1},{-1,-1,-1},
        {1,-1,-1},{1,1,-1},{1,1,1}, {1,-1,-1},{1,1,1},{1,-1,1},
        {-1,1,-1},{-1,1,1},{1,1,1}, {-1,1,-1},{1,1,1},{1,1,-1},
        {-1,-1,1},{-1,-1,-1},{1,-1,-1}, {-1,-1,1},{1,-1,-1},{1,-1,1}
    };
    MeshVertex vertices[36]={};
    for(int i=0;i<36;i++) {
        vertices[i].x=positions[i][0];vertices[i].y=positions[i][1];vertices[i].z=positions[i][2];
        vertices[i].u=(positions[i][0]+1)*0.5f;
        vertices[i].v=(positions[i][2]+1)*0.5f;
        if(i<12){vertices[i].u=(positions[i][0]+1)*0.5f;vertices[i].v=(positions[i][1]+1)*0.5f;}
    }
    D3D11_BUFFER_DESC desc={};desc.ByteWidth=sizeof(vertices);
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init={};init.pSysMem=vertices;
    if(FAILED(device->CreateBuffer(&desc,&init,&cubeVB)))return false;
    desc=D3D11_BUFFER_DESC();desc.ByteWidth=sizeof(SceneConstants);
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(device->CreateBuffer(&desc,0,&constants)))return false;
    D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=D3D11_TEXTURE_ADDRESS_WRAP;sd.AddressV=D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device->CreateSamplerState(&sd,&sampler)))return false;
    D3D11_BLEND_DESC blend={};
    blend.RenderTarget[0].BlendEnable=TRUE;
    blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(device->CreateBlendState(&blend,&omsiBlend)))return false;
    D3D11_DEPTH_STENCIL_DESC glassDepth={};
    glassDepth.DepthEnable=TRUE;
    glassDepth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    glassDepth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(device->CreateDepthStencilState(&glassDepth,&glassDepthState)))return false;
    roadTexture=makeChecker(85,85,85);
    busTexture=makeChecker(230,232,235);
    // Arquivos opcionais. Em GLB/FBX, Assimp deve estar habilitado no CMake.
    std::string base=applicationDirectory();
    ID3D11ShaderResourceView* custom=loadWIC(device,utf8ToWide(base+"assets/bus.png").c_str());
    if(custom){releaseObj(busTexture);busTexture=custom;}
    useModel=loadModelAssimp(device,base+"assets/bus.glb",busModel);
    if(!useModel)useModel=loadModelAssimp(device,base+"assets/bus.fbx",busModel);
    return true;
}
static void shutdownGraphics(){
    importedBusAudio.stop();
    motor.stop();
    if(context)context->ClearState();
    busModel.clear();
    omsiBus.clear();
    releaseObj(roadTexture);releaseObj(busTexture);releaseObj(sampler);releaseObj(omsiBlend);releaseObj(glassDepthState);
    releaseObj(constants);releaseObj(cubeVB);releaseObj(layout);
    releaseObj(pixelShader);releaseObj(vertexShader);releaseObj(depthView);
    releaseObj(depthTexture);releaseObj(target);releaseObj(swapChain);
    releaseObj(context);releaseObj(device);
}
static void setWorld(const XMMATRIX& world,const XMFLOAT4& color,ID3D11ShaderResourceView* texture) {
    SceneConstants cb;
    XMStoreFloat4x4(&cb.transform,world*cameraMatrix);
    cb.color=color;cb.flags=XMFLOAT4(texture?1.0f:0.0f,0,0,0);
    context->UpdateSubresource(constants,0,0,&cb,0,0);
    context->PSSetShaderResources(0,1,&texture);
}
static void drawBox(float x,float y,float z,float sx,float sy,float sz,float yaw,
                    const XMFLOAT4& color,ID3D11ShaderResourceView* texture=0) {
    XMMATRIX world=XMMatrixScaling(sx,sy,sz)*XMMatrixRotationY(yaw)*XMMatrixTranslation(x,y,z);
    setWorld(world,color,texture);
    UINT stride=sizeof(MeshVertex),offset=0;
    context->IASetVertexBuffers(0,1,&cubeVB,&stride,&offset);
    context->Draw(36,0);
}
static void drawBusPart(const BusState& b,float x,float y,float z,
                        float sx,float sy,float sz,const XMFLOAT4& color,
                        ID3D11ShaderResourceView* texture=0,float steer=0,float spin=0) {
    XMMATRIX world=XMMatrixScaling(sx,sy,sz)*XMMatrixRotationX(spin)*
        XMMatrixRotationY(steer)*XMMatrixTranslation(x,y,z)*
        XMMatrixRotationZ(b.roll)*XMMatrixRotationX(b.pitch)*
        XMMatrixRotationY(b.heading)*XMMatrixTranslation(b.x,b.y,b.z);
    setWorld(world,color,texture);
    UINT stride=sizeof(MeshVertex),offset=0;
    context->IASetVertexBuffers(0,1,&cubeVB,&stride,&offset);
    context->Draw(36,0);
}
static void drawAssimpBus(const BusState& b,const XMFLOAT4& tint) {
    XMMATRIX world=XMMatrixScaling(1,1,1)*XMMatrixRotationZ(b.roll)*
        XMMatrixRotationX(b.pitch)*XMMatrixRotationY(b.heading)*
        XMMatrixTranslation(b.x,b.y,b.z);
    for(size_t i=0;i<busModel.meshes.size();i++) {
        const ModelSubmesh& mesh=busModel.meshes[i];
        setWorld(world,tint,mesh.diffuse);
        UINT stride=sizeof(MeshVertex),offset=0;
        context->IASetVertexBuffers(0,1,&mesh.vertices,&stride,&offset);
        context->IASetIndexBuffer(mesh.indices,DXGI_FORMAT_R32_UINT,0);
        context->DrawIndexed(mesh.count,0,0);
    }
    context->IASetIndexBuffer(0,DXGI_FORMAT_UNKNOWN,0);
}
static void drawOmsiBus(const BusState& b){
    const float factor[]={0,0,0,0};
    context->OMSetBlendState(omsiBlend,factor,0xFFFFFFFFu);
    // OMSI exporta vertices em X-direita, Y-cima, Z-frente.
    // O centro da dinamica do DX11Bus e 1.6 m acima do nivel das rodas.
    XMMATRIX placement=XMMatrixRotationZ(b.roll)*
        XMMatrixRotationX(b.pitch)*XMMatrixRotationY(b.heading)*
        XMMatrixTranslation(b.x,b.y-1.6f,b.z);
    // First render opaque body/interior into depth, then glass with
    // depth test enabled but depth writes disabled.
    for(int pass=0;pass<2;++pass) {
        if(pass==1) {
            context->OMSetDepthStencilState(glassDepthState,0);
        }
        for(size_t i=0;i<omsiBus.meshes.size();i++){
        const omsi::GpuMesh& mesh=omsiBus.meshes[i];
        // The 3DS wheel transform is local to this mesh: rotate around its
        // tire/axle pivot before applying the bus pose.
        XMMATRIX world=omsi::wheelTransform(mesh,b)*
            omsi::animationTransform(mesh,b)*placement;
        UINT stride=sizeof(MeshVertex),offset=0;
        context->IASetVertexBuffers(0,1,&mesh.vertices,&stride,&offset);
        for(size_t j=0;j<mesh.parts.size();j++){
            const omsi::DrawPart& part=mesh.parts[j];
            if(part.transparent != (pass==1))continue;
            setWorld(world,XMFLOAT4(part.rgba[0],part.rgba[1],part.rgba[2],part.rgba[3]),part.texture);
            context->IASetIndexBuffer(part.indices,DXGI_FORMAT_R32_UINT,0);
            context->DrawIndexed(part.count,0,0);
        }
    }
    }
    context->OMSetDepthStencilState(0,0);
    context->IASetIndexBuffer(0,DXGI_FORMAT_UNKNOWN,0);
    context->OMSetBlendState(0,0,0xFFFFFFFFu);
}

static void drawBus(const BusState& b,bool mine) {
    XMFLOAT4 paint=mine?XMFLOAT4(1,0.73f,0.23f,1):XMFLOAT4(0.23f,0.78f,0.94f,1);
    XMFLOAT4 black(0.075f,0.095f,0.14f,1),rubber(0.035f,0.035f,0.04f,1);
    if(useOmsi){drawOmsiBus(b);return;}
    if(useModel)drawAssimpBus(b,XMFLOAT4(1,1,1,1));
    else {
        drawBusPart(b,0,0.50f,0,1.17f,1.27f,3.83f,paint,busTexture);
        drawBusPart(b,0,1.20f,0,1.18f,0.54f,3.65f,black);
        drawBusPart(b,0,0.95f,3.79f,1.12f,0.80f,0.055f,black);
        // Folha de porta abre lateralmente; a posicao vem do servidor.
        drawBusPart(b,1.19f+b.door*0.58f,0.12f,1.60f,0.06f,1.08f,0.76f,
                    b.door>0.5f?XMFLOAT4(0.15f,0.21f,0.24f,1):paint);
        drawBusPart(b,-0.95f,0.75f,3.84f,0.14f,0.13f,0.07f,XMFLOAT4(1,1,0.7f,1));
        drawBusPart(b,0.95f,0.75f,3.84f,0.14f,0.13f,0.07f,XMFLOAT4(1,1,0.7f,1));
    }
    // Mesmo com modelo GLB, as seis rodas fisicas sao renderizadas independentemente.
    for(int i=0;i<6;i++){
        float x=sim::wheelOffsetX(i),z=sim::wheelOffsetZ(i);
        float compression=sim::visualTravel(b,i);
        float y=-0.4f-(sim::SPRING_REST-compression);
        drawBusPart(b,x,y,z,0.20f,0.49f,0.49f,rubber,0,sim::wheelSteerAngle(b,i),b.wheelRotation);
        drawBusPart(b,x*1.17f,y,z,0.07f,0.17f,0.17f,XMFLOAT4(0.58f,0.60f,0.62f,1),0,sim::wheelSteerAngle(b,i),b.wheelRotation);
    }
}
static void drawCity(const BusState& focus) {
	const XMFLOAT4 grass(0.26f, 0.39f, 0.20f, 1.0f);
	const XMFLOAT4 asphalt(0.55f, 0.57f, 0.61f, 1.0f);
	const XMFLOAT4 marking(1.0f, 0.89f, 0.44f, 1.0f);
	const XMFLOAT4 roof(0.24f, 0.26f, 0.31f, 1.0f);

	// The world is 4080 x 4080 metres. Only nearby city objects
	// are rendered; buildings and streets stay deterministic.
	drawBox(
		0.0f, -0.30f, 0.0f,
		sim::MAP_HALF_EXTENT, 0.30f, sim::MAP_HALF_EXTENT,
		0.0f, grass
	);

	const int centerX = sim::roadIndex(focus.x);
	const int centerZ = sim::roadIndex(focus.z);
	const int radius = sim::MAP_RENDER_RADIUS;
	const int minX = std::max(-sim::MAP_GRID_RADIUS, centerX - radius);
	const int maxX = std::min(sim::MAP_GRID_RADIUS, centerX + radius);
	const int minZ = std::max(-sim::MAP_GRID_RADIUS, centerZ - radius);
	const int maxZ = std::min(sim::MAP_GRID_RADIUS, centerZ + radius);

	// Road strips cross the entire map; nearby strips only.
	for(int i = minX; i <= maxX; ++i) {
		const float x = i * sim::MAP_ROAD_SPACING;
		drawBox(
			x, 0.001f, 0.0f,
			7.0f, 0.02f, sim::MAP_HALF_EXTENT,
			0.0f, asphalt, roadTexture
		);
	}

	for(int j = minZ; j <= maxZ; ++j) {
		const float z = j * sim::MAP_ROAD_SPACING;
		drawBox(
			0.0f, 0.001f, z,
			sim::MAP_HALF_EXTENT, 0.02f, 7.0f,
			0.0f, asphalt, roadTexture
		);
	}

	// Dashes are kept close to the camera to save DX11 draw calls.
	const int markingRadius = 2;

	for(int i = minX; i <= maxX; ++i) {
		const float x = i * sim::MAP_ROAD_SPACING;

		for(int j = centerZ - markingRadius; j <= centerZ + markingRadius; ++j) {
			if(j < -sim::MAP_GRID_RADIUS || j >= sim::MAP_GRID_RADIUS) {
				continue;
			}

			for(int dash = 0; dash < 3; ++dash) {
				const float z = (
					(float)j + 0.20f + 0.30f * (float)dash
				) * sim::MAP_ROAD_SPACING;

				drawBox(
					x, 0.03f, z,
					0.085f, 0.021f, 2.2f,
					0.0f, marking
				);
			}
		}
	}

	for(int j = minZ; j <= maxZ; ++j) {
		const float z = j * sim::MAP_ROAD_SPACING;

		for(int i = centerX - markingRadius; i <= centerX + markingRadius; ++i) {
			if(i < -sim::MAP_GRID_RADIUS || i >= sim::MAP_GRID_RADIUS) {
				continue;
			}

			for(int dash = 0; dash < 3; ++dash) {
				const float x = (
					(float)i + 0.20f + 0.30f * (float)dash
				) * sim::MAP_ROAD_SPACING;

				drawBox(
					x, 0.03f, z,
					2.2f, 0.021f, 0.085f,
					0.0f, marking
				);
			}
		}
	}

	// One building per block; only blocks near the bus are drawn.
	for(int i = std::max(-sim::MAP_GRID_RADIUS, centerX - radius);
		i <= std::min(sim::MAP_GRID_RADIUS - 1, centerX + radius);
		++i) {

		for(int j = std::max(-sim::MAP_GRID_RADIUS, centerZ - radius);
			j <= std::min(sim::MAP_GRID_RADIUS - 1, centerZ + radius);
			++j) {

			const sim::Box building = sim::building(i, j);
			const int hash = (std::abs(i) * 7 + std::abs(j) * 13 + 17);
			const float height = 5.0f + (float)(hash % 5) * 1.4f;
			const float green = 0.58f + (float)(hash % 3) * 0.05f;

			drawBox(
				building.x, height * 0.5f, building.z,
				building.hx, height * 0.5f, building.hz,
				0.0f, XMFLOAT4(0.58f, green, 0.54f, 1.0f)
			);

			drawBox(
				building.x, height + 0.20f, building.z,
				building.hx + 0.3f, 0.25f, building.hz + 0.3f,
				0.0f, roof
			);
		}
	}

	// Bus-stop geometry is also streamed around the player.
	for(int i = 0; i < sim::STOP_COUNT; ++i) {
		const sim::Stop stop = sim::stop(i);

		if(std::fabs(stop.x - focus.x) > 320.0f ||
			std::fabs(stop.z - focus.z) > 320.0f) {
			continue;
		}

		drawBox(
			stop.x + 2.4f, 1.35f, stop.z,
			0.065f, 1.35f, 0.065f,
			0.0f, XMFLOAT4(0.5f, 0.5f, 0.56f, 1.0f)
		);

		drawBox(
			stop.x + 2.4f, 2.58f, stop.z,
			0.85f, 0.27f, 0.09f,
			0.0f, XMFLOAT4(0.15f, 0.38f, 0.85f, 1.0f)
		);

		for(int passenger = 0; passenger < 3; ++passenger) {
			const float px = stop.x + 2.3f + (float)(passenger % 2) * 0.75f;
			const float pz = stop.z + 2.8f + (float)passenger;

			drawBox(
				px, 0.86f, pz,
				0.18f, 0.58f, 0.18f,
				0.0f, XMFLOAT4(0.25f, 0.30f, 0.72f, 1.0f)
			);

			drawBox(
				px, 1.60f, pz,
				0.17f, 0.17f, 0.17f,
				0.0f, XMFLOAT4(0.95f, 0.69f, 0.45f, 1.0f)
			);
		}
	}

	// A small cross marks the reset origin (X=0, Z=0).
	if(std::fabs(focus.x) < 320.0f && std::fabs(focus.z) < 320.0f) {
		drawBox(
			0.0f, 0.055f, 0.0f,
			2.0f, 0.03f, 0.12f,
			0.0f, XMFLOAT4(0.90f, 0.20f, 0.16f, 1.0f)
		);

		drawBox(
			0.0f, 0.055f, 0.0f,
			0.12f, 0.03f, 2.0f,
			0.0f, XMFLOAT4(0.16f, 0.39f, 0.90f, 1.0f)
		);
	}
}

static const BusState* findBus(const Snapshot& s,uint32_t id){
    for(uint32_t i=0;i<s.count;i++)if(s.buses[i].id==id)return &s.buses[i];
    return 0;
}
static BusState interpolated(const BusState& a,const BusState& b,float t) {
	// Reset to origin is a teleport, not a physical movement.
	// Skip interpolation to avoid sweeping the model across the city.
	const float dx = b.x - a.x;
	const float dz = b.z - a.z;

	if(dx * dx + dz * dz > 2500.0f) {
		return b;
	}

    BusState r=b;
    r.x=a.x+(b.x-a.x)*t;r.y=a.y+(b.y-a.y)*t;r.z=a.z+(b.z-a.z)*t;
    float difference=atan2f(sinf(b.heading-a.heading),cosf(b.heading-a.heading));
    r.heading=a.heading+difference*t;
    r.speed=a.speed+(b.speed-a.speed)*t;
    r.steer=a.steer+(b.steer-a.steer)*t;
    r.roll=a.roll+(b.roll-a.roll)*t;
    r.pitch=a.pitch+(b.pitch-a.pitch)*t;
    r.door=a.door+(b.door-a.door)*t;
    r.wheelRotation=a.wheelRotation+(b.wheelRotation-a.wheelRotation)*t;
    return r;
}
static BusState currentBus(uint32_t id,float alpha) {
    const BusState* newer=findBus(latest,id);
    if(!newer){BusState empty={};return empty;}
    const BusState* older=findBus(earlier,id);
    if(!older)return *newer;
    return interpolated(*older,*newer,alpha);
}
static void drawFrame(){
    if(!device)return;
    float color[]={0.48f,0.69f,0.89f,1.0f};
    context->ClearRenderTargetView(target,color);
    context->ClearDepthStencilView(depthView,D3D11_CLEAR_DEPTH,1,0);
    context->IASetInputLayout(layout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader,0,0);
    context->PSSetShader(pixelShader,0,0);
    context->VSSetConstantBuffers(0,1,&constants);
    context->PSSetConstantBuffers(0,1,&constants);
    context->PSSetSamplers(0,1,&sampler);
    float alpha=1.0f;
    if(gotSnapshot && earlier.count>0)alpha=sim::clamp((GetTickCount()-latest.received)/50.0f,0,1);
    BusState focus=currentBus(myId,alpha);
    float sine=sinf(focus.heading),cosine=cosf(focus.heading);
    XMVECTOR eye,at;
    if(cockpit) {
        eye=XMVectorSet(focus.x+sine*2.90f,focus.y+1.38f,focus.z+cosine*2.90f,1);
        at=XMVectorSet(focus.x+sine*35.0f,focus.y+1.20f,focus.z+cosine*35.0f,1);
    } else {
        // Orbit camera: right mouse drag rotates around the bus; wheel zooms.
        const float angle=focus.heading+orbitYaw;
        const float horizontal=orbitDistance*cosf(orbitPitch);
        eye=XMVectorSet(focus.x-sinf(angle)*horizontal,
                        focus.y+0.45f+orbitDistance*sinf(orbitPitch),
                        focus.z-cosf(angle)*horizontal,1);
        at=XMVectorSet(focus.x,focus.y+0.45f,focus.z,1);
    }
    cameraMatrix=XMMatrixLookAtLH(eye,at,XMVectorSet(0,1,0,0))*
        XMMatrixPerspectiveFovLH(XM_PIDIV4,float(WIDTH)/HEIGHT,0.1f,800.0f);
    drawCity(focus);
    for(uint32_t i=0;i<latest.count;i++) {
        if(cockpit && latest.buses[i].id==myId)continue;
        drawBus(currentBus(latest.buses[i].id,alpha),latest.buses[i].id==myId);
    }
    if(importedAudioReady && importedBusAudio.playing()) {
        importedBusAudio.update(
            focus.rpm,
            focus.gear,
            localThrottle,
            focus.speed,
            cockpit
        );
    } else {
        motor.update(focus.rpm);
    }
    if(connected && GetTickCount()-lastHud>250) {
        char title[512];
        if(latest.count>0 && (DWORD)(GetTickCount()-latest.received)>3000)
            sprintf(title,"DX11 Bus | Sem resposta do servidor ha mais de 3 segundos");
        else if(latest.count > 0) {
			const char* steeringLabel = focus.steeringMode == STEERING_OMSI_APPROX ?
				"OMSI aprox." : "Classica";

			const char* gearboxLabel =
				focus.transmissionMode == TRANSMISSION_MANUAL ?
				"Manual" : "Automatica";

			sprintf(
				title,
				"DX11 Bus | ID %u | %.0f km/h | Marcha %d | %.0f RPM | "
				"%u passageiros | Parada %u | %s | Veiculos %u | "
				"Cambio: %s (Q/Z/G) | Direcao: %s | R origem | X %.0f Z %.0f",
				myId,
				fabsf(focus.speed) * 3.6f,
				(int)focus.gear,
				focus.rpm,
				(unsigned)focus.passengers,
				(unsigned)focus.nextStop + 1,
				cockpit ? "Cabine" : "Externa",
				(unsigned)latest.count,
				gearboxLabel,
				steeringLabel,
				focus.x,
				focus.z
			);
		}
        else sprintf(title,"DX11 Bus | Esperando servidor UDP 27015...");
        SetWindowTextA(windowHandle,title);lastHud=GetTickCount();
    }
    swapChain->Present(1,0);
}
static void requestOriginReset() {
	if(!connected) {
		return;
	}

	resetRequestedAt = GetTickCount();
	resetRequested = true;

	// Recenter the orbit camera as the bus returns to the world origin.
	orbitYaw = 0.0f;
	orbitPitch = 0.38f;
	orbitDistance = 16.0f;
}

static void requestOmsiSteeringToggle() {
	if(!connected || omsiSteeringRequested) {
		return;
	}

	omsiSteeringRequestedAt = GetTickCount();
	omsiSteeringRequested = true;
}

// Resolve Ç through the active Windows keyboard layout (ABNT2 included).
// Polling also works when a child button has focus, unlike WM_CHAR alone.
static void pollOmsiSteeringKey() {
	const SHORT mappedKey = VkKeyScanExW(
		L'\u00e7',
		GetKeyboardLayout(0)
	);

	bool keyDown = false;

	if(mappedKey != -1 && GetForegroundWindow() == windowHandle) {
		const int virtualKey = mappedKey & 0xFF;
		keyDown = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
	}

	if(keyDown && !previousCcedillaDown) {
		requestOmsiSteeringToggle();
	}

	previousCcedillaDown = keyDown;
}

static void updateOmsiSteeringButton() {
	if(!connected || !omsiSteeringButton || !gotSnapshot) {
		return;
	}

	const BusState* player = findBus(latest, myId);

	if(!player || player->steeringMode == lastShownSteeringMode) {
		return;
	}

	lastShownSteeringMode = player->steeringMode;

	if(player->steeringMode == STEERING_OMSI_APPROX) {
		SetWindowTextW(
			omsiSteeringButton,
			L"Direcao OMSI: ON (\u00c7)"
		);
	} else {
		SetWindowTextW(
			omsiSteeringButton,
			L"Direcao OMSI: OFF (\u00c7)"
		);
	}
}

static void sendControls() {
	if(!connected) {
		return;
	}

	NetPacket packet;
	initPacket(packet, PACKET_INPUT);
	packet.clientId = myId;

	if(GetForegroundWindow() == windowHandle) {
		packet.throttle =
			((GetAsyncKeyState('W') & 0x8000) ? 1.0f : 0.0f) -
			((GetAsyncKeyState('S') & 0x8000) ? 1.0f : 0.0f);

		packet.steering =
			((GetAsyncKeyState('D') & 0x8000) ? 1.0f : 0.0f) -
			((GetAsyncKeyState('A') & 0x8000) ? 1.0f : 0.0f);

		packet.brake =
			(GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1.0f : 0.0f;

		if(GetAsyncKeyState('E') & 0x8000) {
			packet.flags |= INPUT_TOGGLE_DOOR;
		}

		if(GetAsyncKeyState('Q') & 0x8000) {
			packet.flags |= INPUT_GEAR_UP;
		}

		if(GetAsyncKeyState('Z') & 0x8000) {
			packet.flags |= INPUT_GEAR_DOWN;
		}

		if(GetAsyncKeyState(VK_TAB) & 0x8000) {
			packet.flags |= INPUT_CLUTCH;
		}

		if(GetAsyncKeyState('G') & 0x8000) {
			packet.flags |= INPUT_AUTO_GEAR;
		}
	}

	localThrottle = packet.throttle;

	// Send toggles for 300 ms to tolerate a dropped UDP packet.
	// Server-side rising-edge detection applies each request only once.
	if(omsiSteeringRequested) {
		const DWORD elapsed = GetTickCount() - omsiSteeringRequestedAt;

		if(elapsed < 300) {
			packet.flags |= INPUT_TOGGLE_OMSI_STEERING;
		} else {
			omsiSteeringRequested = false;
		}
	}

	// Send the reset request for 300 ms so a single dropped UDP
	// packet does not lose it. The server applies it once per press.
	if(resetRequested) {
		const DWORD elapsed = GetTickCount() - resetRequestedAt;

		if(elapsed < 300) {
			packet.flags |= INPUT_RESET_ORIGIN;
		} else {
			resetRequested = false;
		}
	}

	sendto(
		socketUdp,
		(const char*)&packet,
		sizeof(packet),
		0,
		(sockaddr*)&serverAddress,
		sizeof(serverAddress)
	);
}

static void receiveUpdates() {
    if(!connected)return;
    NetPacket packet; sockaddr_in sender={};int senderSize=sizeof(sender);int n;
    while((n=recvfrom(socketUdp,(char*)&packet,sizeof(packet),0,(sockaddr*)&sender,&senderSize))>0) {
        if(sender.sin_addr.s_addr==serverAddress.sin_addr.s_addr &&
           sender.sin_port==serverAddress.sin_port && n==(int)sizeof(packet) &&
           packet.magic==BUS_MAGIC && packet.type==PACKET_WORLD &&
           packet.count<=MAX_BUSES && (!gotSnapshot || (int32_t)(packet.tick-lastTick)>0)) {
            earlier=latest;
            latest.count=packet.count;
            memcpy(latest.buses,packet.buses,sizeof(BusState)*latest.count);
            latest.received=GetTickCount();
            myId=packet.clientId;lastTick=packet.tick;gotSnapshot=true;
        }
        senderSize=sizeof(sender);
    }
}
static bool connectTo(const char* ipv4) {
    if(connected)return true;
    if(!initializeSockets())return false;
    socketUdp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(socketUdp==INVALID_SOCKET){closeSockets();return false;}
    serverAddress.sin_family=AF_INET;serverAddress.sin_port=htons(BUS_PORT);
    serverAddress.sin_addr.s_addr=inet_addr(ipv4);
    if(serverAddress.sin_addr.s_addr==INADDR_NONE) {
        closesocket(socketUdp);socketUdp=INVALID_SOCKET;closeSockets();return false;
    }
    u_long nonBlocking=1;ioctlsocket(socketUdp,FIONBIO,&nonBlocking);
    connected=true;myId=0;gotSnapshot=false;lastTick=0;earlier=Snapshot();latest=Snapshot();
    ShowWindow(ipLabel,SW_HIDE);ShowWindow(ipInput,SW_HIDE);ShowWindow(connectButton,SW_HIDE);
    if(importedAudioReady && importedBusAudio.start()) {
        motor.stop();
    } else {
        motor.start();
    }
	EnableWindow(resetButton, TRUE);
	EnableWindow(omsiSteeringButton, TRUE);
	lastShownSteeringMode = 0xFFFFFFFFu;
	omsiSteeringRequested = false;
	previousCcedillaDown = false;
    return true;
}
static bool loadBusScriptFiles(
    const std::string& path,
    bool showDialog,
    HWND dialogOwner
) {
    buscfg::ModScripts scripts;

    if(!scripts.load(path)) {
        if(showDialog) {
            MessageBoxA(
                dialogOwner,
                scripts.diagnostic.c_str(),
                "Falha ao ler scripts",
                MB_OK | MB_ICONWARNING
            );
        }

        return false;
    }

    importedAudioReady = importedBusAudio.load(scripts);

    if(connected) {
        if(importedAudioReady && importedBusAudio.start()) {
            motor.stop();
        } else {
            motor.start();
        }
    }

    std::string information = scripts.diagnostic;
    information += "\n";
    information += importedBusAudio.report();

    if(scripts.hasManual || scripts.hasAutomatic) {
        information +=
            "\n\nCAMBIO: carregue esta mesma configuracao no servidor "
            "com DX11BUS_MOD_CONFIG e reinicie-o. "
            "A simulacao de transmissoes e autoritativa.";
    }

    FILE* report = fopen(
        (applicationDirectory() + "bus_scripts.log").c_str(),
        "wb"
    );

    if(report != 0) {
        fprintf(report, "%s\n", information.c_str());
        fclose(report);
    }

    if(showDialog) {
        MessageBoxA(
            dialogOwner,
            information.c_str(),
            "Scripts e sons do onibus",
            MB_OK
        );
    }

    return true;
}

static void browseBusScripts(HWND hwnd) {
    char filename[2048] = {};
    OPENFILENAMEA chooser = {};

    chooser.lStructSize = sizeof(chooser);
    chooser.hwndOwner = hwnd;
    chooser.lpstrFile = filename;
    chooser.nMaxFile = sizeof(filename);
    chooser.lpstrFilter =
        "Configuracao do onibus (*.ini;*.txt)\0*.ini;*.txt\0"
        "Todos os arquivos (*.*)\0*.*\0";
    chooser.nFilterIndex = 1;
    chooser.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR;

    if(!GetOpenFileNameA(&chooser)) {
        return;
    }

    loadBusScriptFiles(filename, true, hwnd);
}

static void browseOmsiModel(HWND hwnd){
    char filename[MAX_PATH]={};
    OPENFILENAMEA chooser={};chooser.lStructSize=sizeof(chooser);
    chooser.hwndOwner=hwnd;
    chooser.lpstrFile=filename;chooser.nMaxFile=MAX_PATH;
    chooser.lpstrFilter="OMSI bus and model (*.bus;*.ovh;*.cfg;*.o3d;*.3ds;*.x)\0*.bus;*.ovh;*.cfg;*.o3d;*.3ds;*.x\0All files (*.*)\0*.*\0";
    chooser.nFilterIndex=1;
    chooser.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameA(&chooser))return;
    omsi::Bus candidate;
    const bool success=omsi::load(device,filename,candidate);
    FILE* log=fopen((applicationDirectory()+"omsi_import.log").c_str(),"wb");
    if(log){
        fprintf(log,"File: %s\nResult: %s\n%s\n",filename,success?"partial/success":"failed",candidate.report.c_str());
        fclose(log);
    }
    if(success){
        // Move manual: liberamos o asset anterior antes de trocar.
        omsiBus.clear();
        omsiBus.meshes.swap(candidate.meshes);
        omsiBus.textureCache.swap(candidate.textureCache);
        omsiBus.report=candidate.report;
        omsiBus.source=candidate.source;
        omsiBus.imported=candidate.imported;
        omsiBus.missing=candidate.missing;
        useOmsi=true;
        MessageBoxA(hwnd,omsiBus.report.c_str(),"Modelo OMSI importado (veja omsi_import.log)",MB_OK);
    } else {
        MessageBoxA(hwnd,candidate.report.c_str(),"Falha ao importar OMSI",MB_OK|MB_ICONWARNING);
    }
}

static LRESULT CALLBACK windowProcedure(
	HWND hwnd,
	UINT message,
	WPARAM w,
	LPARAM l
) {
	if(message == WM_DESTROY) {
		running = false;
		PostQuitMessage(0);
		return 0;
	}

	if(message == WM_RBUTTONDOWN) {
		orbitDragging = true;
		orbitLast.x = GET_X_LPARAM(l);
		orbitLast.y = GET_Y_LPARAM(l);
		SetCapture(hwnd);
		return 0;
	}

	if(message == WM_MOUSEMOVE && orbitDragging) {
		const int mx = GET_X_LPARAM(l);
		const int my = GET_Y_LPARAM(l);

		orbitYaw += (mx - orbitLast.x) * 0.006f;
		orbitPitch = sim::clamp(
			orbitPitch + (my - orbitLast.y) * 0.005f,
			-0.18f,
			1.35f
		);

		orbitLast.x = mx;
		orbitLast.y = my;
		return 0;
	}

	if(message == WM_RBUTTONUP) {
		orbitDragging = false;

		if(GetCapture() == hwnd) {
			ReleaseCapture();
		}

		return 0;
	}

	if(message == WM_CAPTURECHANGED) {
		orbitDragging = false;
		return 0;
	}

	if(message == WM_MOUSEWHEEL) {
		const int delta = GET_WHEEL_DELTA_WPARAM(w);
		const float zoomFactor = delta > 0 ? 0.88f : 1.12f;

		orbitDistance = sim::clamp(
			orbitDistance * zoomFactor,
			4.0f,
			45.0f
		);
		return 0;
	}

	if(message == WM_KEYDOWN) {
		const bool firstKeyDown = ((l >> 30) & 1) == 0;

		if(w == VK_ESCAPE) {
			DestroyWindow(hwnd);
			return 0;
		}

		if(w == VK_F1 && firstKeyDown) {
			cockpit = !cockpit;
			return 0;
		}

		if(w == 'R' && firstKeyDown) {
			requestOriginReset();
			return 0;
		}
	}

	if(message == WM_COMMAND) {
		const int command = LOWORD(w);

		if(command == 103) {
			browseOmsiModel(hwnd);
			return 0;
		}

		if(command == 104) {
			requestOriginReset();
			return 0;
		}

		if(command == 105) {
			requestOmsiSteeringToggle();
			return 0;
		}

        if(command == 106) {
            browseBusScripts(hwnd);
            return 0;
        }

		if(command == 102) {
			char ipv4[80] = {};
			GetWindowTextA(ipInput, ipv4, sizeof(ipv4));

			if(!connectTo(ipv4)) {
				MessageBoxA(
					hwnd,
					"Informe um endereco IPv4 valido, ex.: 127.0.0.1",
					"Conexao",
					MB_OK | MB_ICONWARNING
				);
			}

			return 0;
		}
	}

	return DefWindowProcA(hwnd, message, w, l);
}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR commandLine,int show){
    CoInitializeEx(0,COINIT_MULTITHREADED);
    WNDCLASSA wc={};wc.lpfnWndProc=windowProcedure;wc.hInstance=instance;
    wc.hCursor=LoadCursor(0,IDC_ARROW);wc.lpszClassName="DX11BusNetworkGame";
    if(!RegisterClassA(&wc))return 1;
    const DWORD style=WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT rect={0,0,WIDTH,HEIGHT};AdjustWindowRect(&rect,style,FALSE);
    windowHandle=CreateWindowA(wc.lpszClassName,"DX11 Bus Multiplayer | Digite o IP para conectar",style,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,0,0,instance,0);
    if(!windowHandle)return 1;
    ipLabel=CreateWindowA("STATIC","IP do servidor:",WS_CHILD|WS_VISIBLE,22,18,120,25,windowHandle,0,instance,0);
    ipInput=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","127.0.0.1",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
         140,14,180,27,windowHandle,(HMENU)101,instance,0);
    connectButton=CreateWindowA("BUTTON","Conectar",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
         335,14,110,27,windowHandle,(HMENU)102,instance,0);
    omsiButton=CreateWindowA("BUTTON","Carregar OMSI (.bus/.3ds)",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
         460,14,195,27,windowHandle,(HMENU)103,instance,0);
	resetButton = CreateWindowA(
		"BUTTON", "Reset origem (R)",
		WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
		670, 14, 150, 27,
		windowHandle, (HMENU)104, instance, 0
	);

	omsiSteeringButton = CreateWindowW(
		L"BUTTON",
		L"Direcao OMSI: OFF (\u00c7)",
		WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
		835, 14, 230, 27,
		windowHandle, (HMENU)105, instance, 0
	);

    scriptButton = CreateWindowA(
        "BUTTON",
        "Ler Scripts/Sons",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        1077, 14, 175, 27,
        windowHandle, (HMENU)106, instance, 0
    );
    ShowWindow(windowHandle,show);
    if(!initializeGraphics(windowHandle)){
        MessageBoxA(windowHandle,"Nao foi possivel inicializar DX11 ou carregar os shaders .cso.","DX11 Bus",MB_ICONERROR);
        shutdownGraphics();return 1;
    }
    // Pode abrir modelo diretamente com a variavel de ambiente do usuario.
    char pathEnv[2048]={};
    DWORD envLength=GetEnvironmentVariableA("DX11BUS_OMSI_BUS",pathEnv,sizeof(pathEnv));
    if(envLength>0 && envLength<sizeof(pathEnv)){
        useOmsi=omsi::load(device,pathEnv,omsiBus);
        FILE* log=fopen((applicationDirectory()+"omsi_import.log").c_str(),"wb");
        if(log){fprintf(log,"%s\n",omsiBus.report.c_str());fclose(log);}
    }
    // Both programs read DX11BUS_MOD_CONFIG. The client loads the sound
    // files; the server separately loads authoritative gearbox physics.
    char modConfigPath[2048] = {};
    const DWORD modConfigLength = GetEnvironmentVariableA(
        "DX11BUS_MOD_CONFIG",
        modConfigPath,
        sizeof(modConfigPath)
    );

    if(modConfigLength > 0 && modConfigLength < sizeof(modConfigPath)) {
        loadBusScriptFiles(modConfigPath, false, windowHandle);
    }

    std::string cmd=commandLine;
    if(!cmd.empty()){
        size_t start=cmd.find_first_not_of(" \t\"");
        if(start!=std::string::npos){
            size_t end=cmd.find_first_of(" \t\"",start);
            cmd=cmd.substr(start,end==std::string::npos?std::string::npos:end-start);
            if(!cmd.empty()){SetWindowTextA(ipInput,cmd.c_str());connectTo(cmd.c_str());}
        }
    }
    LARGE_INTEGER frequency,last,now;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&last);
    double accumulator=0;
    while(running){
        MSG msg;
        while(PeekMessageA(&msg,0,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT){running=false;break;}
            TranslateMessage(&msg);DispatchMessageA(&msg);
        }
        if(!running)break;
        QueryPerformanceCounter(&now);
        double delta=double(now.QuadPart-last.QuadPart)/double(frequency.QuadPart);last=now;
        accumulator+=std::min(0.20,delta);
		pollOmsiSteeringKey();

		while(accumulator >= 1.0 / 30.0) {
			sendControls();
			accumulator -= 1.0 / 30.0;
		}

		receiveUpdates();
		updateOmsiSteeringButton();

		if(!IsIconic(windowHandle)) {
			drawFrame();
		}
        Sleep(1);
    }
    if(socketUdp!=INVALID_SOCKET)closesocket(socketUdp);
    if(connected)closeSockets();
    shutdownGraphics();CoUninitialize();
    return 0;
}