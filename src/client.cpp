#define _WIN32_WINNT 0x0601
#include "protocol.h"
#include <windows.h>
#include <d3d11.h>
#include "math_compat.h"
#include <stdio.h>
#include <vector>
#include <math.h>
#include <string>
#include <cstring>
using namespace DirectX;

struct SceneConstants { XMFLOAT4X4 worldViewProjection; XMFLOAT4 color; };
struct Vertex { float x,y,z; };
static HWND windowHandle=0;
static ID3D11Device* device=0;
static ID3D11DeviceContext* context=0;
static IDXGISwapChain* swapChain=0;
static ID3D11RenderTargetView* target=0;
static ID3D11DepthStencilView* depthView=0;
static ID3D11Texture2D* depthTexture=0;
static ID3D11VertexShader* vertexShader=0;
static ID3D11PixelShader* pixelShader=0;
static ID3D11InputLayout* vertexLayout=0;
static ID3D11Buffer* cubeVB=0;
static ID3D11Buffer* constantBuffer=0;
static SOCKET udp=INVALID_SOCKET;
static sockaddr_in serverAddress={};
static BusState allBuses[MAX_BUSES]={};
static uint32_t busCount=0,myId=0;
static int screenWidth=1280,screenHeight=720;
static bool running=true;

template<class T> static void releaseObj(T*& p) { if(p) {p->Release();p=0;} }
static bool readBinary(const char* path,std::vector<char>& result) {
    FILE* f=fopen(path,"rb");
    if(!f) {
        char executablePath[MAX_PATH]={};
        GetModuleFileNameA(0,executablePath,MAX_PATH);
        char* slash=strrchr(executablePath,'\\');
        if(slash) {*(slash+1)=0;strncat(executablePath,path,MAX_PATH-strlen(executablePath)-1);f=fopen(executablePath,"rb");}
    }
    if(!f)return false;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<=0) {fclose(f);return false;}
    result.resize(size_t(size)); bool ok=fread(&result[0],1,size_t(size),f)==size_t(size);
    fclose(f);return ok;
}
static bool initializeGraphics(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc={};desc.BufferCount=1;
    desc.BufferDesc.Width=screenWidth;desc.BufferDesc.Height=screenHeight;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=hwnd;
    desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL selected;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(0,D3D_DRIVER_TYPE_HARDWARE,0,0,requested,3,D3D11_SDK_VERSION,&desc,&swapChain,&device,&selected,&context);
    if(FAILED(hr))return false;
    ID3D11Texture2D* backBuffer=0;
    hr=swapChain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&backBuffer);
    if(FAILED(hr))return false;
    hr=device->CreateRenderTargetView(backBuffer,0,&target);backBuffer->Release();
    if(FAILED(hr))return false;
    D3D11_TEXTURE2D_DESC depthDesc={};depthDesc.Width=screenWidth;depthDesc.Height=screenHeight;
    depthDesc.MipLevels=1;depthDesc.ArraySize=1;depthDesc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count=1;depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    if(FAILED(device->CreateTexture2D(&depthDesc,0,&depthTexture)))return false;
    if(FAILED(device->CreateDepthStencilView(depthTexture,0,&depthView)))return false;
    context->OMSetRenderTargets(1,&target,depthView);
    D3D11_VIEWPORT vp={};vp.Width=(FLOAT)screenWidth;vp.Height=(FLOAT)screenHeight;vp.MaxDepth=1;
    context->RSSetViewports(1,&vp);
    D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
    ID3D11RasterizerState* raster=0;
    if(SUCCEEDED(device->CreateRasterizerState(&rs,&raster))) {context->RSSetState(raster);raster->Release();}
    std::vector<char> vs,ps;
    if(!readBinary("bus_vs.cso",vs)||!readBinary("bus_ps.cso",ps)) {MessageBoxA(hwnd,"Shaders .cso nao encontrados ao lado do executavel.","DX11 Bus",MB_ICONERROR);return false;}
    if(FAILED(device->CreateVertexShader(&vs[0],vs.size(),0,&vertexShader)))return false;
    if(FAILED(device->CreatePixelShader(&ps[0],ps.size(),0,&pixelShader)))return false;
    D3D11_INPUT_ELEMENT_DESC input[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(device->CreateInputLayout(input,1,&vs[0],vs.size(),&vertexLayout)))return false;
    const Vertex v[]={
      {-1,-1,-1},{-1,1,-1},{1,1,-1}, {-1,-1,-1},{1,1,-1},{1,-1,-1},
      {1,-1,1},{1,1,1},{-1,1,1}, {1,-1,1},{-1,1,1},{-1,-1,1},
      {-1,-1,1},{-1,1,1},{-1,1,-1}, {-1,-1,1},{-1,1,-1},{-1,-1,-1},
      {1,-1,-1},{1,1,-1},{1,1,1}, {1,-1,-1},{1,1,1},{1,-1,1},
      {-1,1,-1},{-1,1,1},{1,1,1}, {-1,1,-1},{1,1,1},{1,1,-1},
      {-1,-1,1},{-1,-1,-1},{1,-1,-1}, {-1,-1,1},{1,-1,-1},{1,-1,1}
    };
    D3D11_BUFFER_DESC bd={};bd.ByteWidth=sizeof(v);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init={};init.pSysMem=v;
    if(FAILED(device->CreateBuffer(&bd,&init,&cubeVB)))return false;
    bd=D3D11_BUFFER_DESC();bd.ByteWidth=sizeof(SceneConstants);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(device->CreateBuffer(&bd,0,&constantBuffer)))return false;
    return true;
}
static void shutdownGraphics() {
    if(context)context->ClearState();
    releaseObj(constantBuffer);releaseObj(cubeVB);releaseObj(vertexLayout);
    releaseObj(pixelShader);releaseObj(vertexShader);releaseObj(depthView);releaseObj(depthTexture);
    releaseObj(target);releaseObj(swapChain);releaseObj(context);releaseObj(device);
}
static XMMATRIX viewProjection;
static void drawBox(float x,float y,float z,float sx,float sy,float sz,float heading,const XMFLOAT4& color) {
    SceneConstants constants;
    XMMATRIX world=XMMatrixScaling(sx,sy,sz)*XMMatrixRotationY(heading)*XMMatrixTranslation(x,y,z);
    XMStoreFloat4x4(&constants.worldViewProjection,world*viewProjection);
    constants.color=color;
    context->UpdateSubresource(constantBuffer,0,0,&constants,0,0);
    context->Draw(36,0);
}
static void drawBus(const BusState& bus,bool ours) {
    const float x=bus.x,z=bus.z,a=bus.heading;
    const XMFLOAT4 paint=ours?XMFLOAT4(0.96f,0.65f,0.10f,1):XMFLOAT4(0.15f,0.73f,0.95f,1);
    const XMFLOAT4 dark(0.09f,0.14f,0.19f,1),wheel(0.04f,0.04f,0.05f,1);
    drawBox(x,1.65f,z,1.15f,1.5f,3.7f,a,paint);
    drawBox(x,2.3f,z,1.165f,0.53f,3.37f,a,dark);
    float frontX=x+sinf(a)*3.69f,frontZ=z+cosf(a)*3.69f;
    drawBox(frontX,2.05f,frontZ,1.08f,0.84f,0.04f,a,dark);
    float sideX=cosf(a),sideZ=-sinf(a);
    for(int side=-1;side<=1;side+=2)for(int axle=-1;axle<=1;axle+=2) {
        float longitudinal=float(axle)*2.65f;
        drawBox(x+sideX*side*1.17f+sinf(a)*longitudinal,0.56f,
                z+sideZ*side*1.17f+cosf(a)*longitudinal,0.2f,0.53f,0.52f,a,wheel);
    }
}
static void render() {
    const float clear[]={0.48f,0.69f,0.88f,1};
    context->ClearRenderTargetView(target,clear);
    context->ClearDepthStencilView(depthView,D3D11_CLEAR_DEPTH,1,0);
    UINT stride=sizeof(Vertex),offset=0;
    context->IASetInputLayout(vertexLayout);
    context->IASetVertexBuffers(0,1,&cubeVB,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader,0,0);context->PSSetShader(pixelShader,0,0);
    context->VSSetConstantBuffers(0,1,&constantBuffer);context->PSSetConstantBuffers(0,1,&constantBuffer);
    BusState focus={};bool found=false;
    for(uint32_t i=0;i<busCount;i++)if(allBuses[i].id==myId) {focus=allBuses[i];found=true;break;}
    float a=found?focus.heading:0;
    XMVECTOR eye=XMVectorSet(focus.x-sinf(a)*15.0f,9.0f,focus.z-cosf(a)*15.0f,1);
    XMVECTOR at=XMVectorSet(focus.x,1.7f,focus.z,1);
    viewProjection=XMMatrixLookAtLH(eye,at,XMVectorSet(0,1,0,0)) * XMMatrixPerspectiveFovLH(XM_PIDIV4,float(screenWidth)/screenHeight,0.1f,700.0f);
    drawBox(0,-0.21f,0,150,0.20f,150,0,XMFLOAT4(0.22f,0.39f,0.20f,1));
    drawBox(0,-0.005f,0,8,0.015f,145,0,XMFLOAT4(0.21f,0.23f,0.25f,1));
    drawBox(0,-0.003f,0,145,0.015f,7,0,XMFLOAT4(0.21f,0.23f,0.25f,1));
    for(int i=-14;i<=14;i++) {
        drawBox(0,0.02f,i*5.0f,0.08f,0.015f,1.55f,0,XMFLOAT4(1,0.9f,0.3f,1));
        drawBox(i*5.0f,0.023f,0,1.55f,0.015f,0.08f,0,XMFLOAT4(1,0.9f,0.3f,1));
    }
    for(int i=-6;i<=6;i++)for(int s=-1;s<=1;s+=2) {
        if(i==0)continue;
        float px=float(s)*19.0f;
        drawBox(px,3.0f,i*18.0f,4,3,4,0,XMFLOAT4(0.68f,0.66f,0.59f,1));
        drawBox(px,6.25f,i*18.0f,4.2f,0.25f,4.2f,0,XMFLOAT4(0.34f,0.20f,0.18f,1));
    }
    for(uint32_t i=0;i<busCount;i++)drawBus(allBuses[i],allBuses[i].id==myId);
    swapChain->Present(1,0);
}
static LRESULT CALLBACK windowProcedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_DESTROY) {running=false;PostQuitMessage(0);return 0;}
    if(message==WM_KEYDOWN && wParam==VK_ESCAPE) {DestroyWindow(hwnd);return 0;}
    return DefWindowProcA(hwnd,message,wParam,lParam);
}
static bool startNetwork(const char* ip) {
    if(!initializeSockets())return false;
    udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(udp==INVALID_SOCKET)return false;
    u_long nonBlocking=1;ioctlsocket(udp,FIONBIO,&nonBlocking);
    serverAddress.sin_family=AF_INET;serverAddress.sin_port=htons(BUS_PORT);
    serverAddress.sin_addr.s_addr=inet_addr(ip);
    return serverAddress.sin_addr.s_addr!=INADDR_NONE;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR commandLine,int show) {
    const char* ip="127.0.0.1";
    std::string address=commandLine;
    if(!address.empty()) {
        size_t b=address.find_first_not_of(" \t\"");
        size_t e=address.find_first_of(" \t\"",b==std::string::npos?0:b);
        if(b!=std::string::npos)address=address.substr(b,e==std::string::npos?e:e-b);
        if(!address.empty())ip=address.c_str();
    }
    WNDCLASSA wc={};wc.lpfnWndProc=windowProcedure;wc.hInstance=instance;
    wc.lpszClassName="DX11BusMultiplayerWindow";wc.hCursor=LoadCursor(0,IDC_ARROW);
    if(!RegisterClassA(&wc))return 1;
    RECT rect={0,0,screenWidth,screenHeight};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    windowHandle=CreateWindowA(wc.lpszClassName,"DX11 Bus Multiplayer - W/S acelerar | A/D virar | Espaco freio",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,0,0,instance,0);
    if(!windowHandle)return 1;
    ShowWindow(windowHandle,show);
    if(!initializeGraphics(windowHandle)) {MessageBoxA(windowHandle,"Erro ao inicializar Direct3D 11. Confira shaders e driver.","DX11 Bus",MB_ICONERROR);shutdownGraphics();return 1;}
    if(!startNetwork(ip)) {MessageBoxA(windowHandle,"Endereco IPv4 ou Winsock invalido.","Rede",MB_ICONERROR);shutdownGraphics();return 1;}
    LARGE_INTEGER freq,last,now;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&last);
    double sendTimer=0;
    while(running) {
        MSG msg;
        while(PeekMessageA(&msg,0,0,0,PM_REMOVE)) {if(msg.message==WM_QUIT)running=false;TranslateMessage(&msg);DispatchMessageA(&msg);}
        QueryPerformanceCounter(&now);
        double dt=double(now.QuadPart-last.QuadPart)/double(freq.QuadPart);last=now;
        if(dt>0.1)dt=0.1;
        sendTimer+=dt;
        if(sendTimer>=1.0/30.0) {
            sendTimer=0;
            NetPacket input;initPacket(input,1);
            input.clientId=myId;
            input.throttle=((GetAsyncKeyState('W')&0x8000)?1.0f:0.0f)-((GetAsyncKeyState('S')&0x8000)?1.0f:0.0f);
            input.steering=((GetAsyncKeyState('D')&0x8000)?1.0f:0.0f)-((GetAsyncKeyState('A')&0x8000)?1.0f:0.0f);
            input.brake=(GetAsyncKeyState(VK_SPACE)&0x8000)?1.0f:0.0f;
            sendto(udp,(char*)&input,sizeof(input),0,(sockaddr*)&serverAddress,sizeof(serverAddress));
        }
        NetPacket packet; sockaddr_in from={};int fromLen=sizeof(from);int size;
        while((size=recvfrom(udp,(char*)&packet,sizeof(packet),0,(sockaddr*)&from,&fromLen))>0) {
            if(from.sin_addr.s_addr==serverAddress.sin_addr.s_addr && from.sin_port==serverAddress.sin_port &&
               size==sizeof(NetPacket) && packet.magic==BUS_MAGIC && packet.type==2 && packet.count<=MAX_BUSES) {
                myId=packet.clientId;busCount=packet.count;
                memcpy(allBuses,packet.buses,sizeof(BusState)*busCount);
            }
            fromLen=sizeof(from);
        }
        render();
        Sleep(1);
    }
    if(udp!=INVALID_SOCKET)closesocket(udp);closeSockets();shutdownGraphics();return 0;
}