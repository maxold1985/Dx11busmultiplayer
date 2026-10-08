#pragma once
#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <vector>
#include <string>

// Le PNG/JPEG/BMP via Windows Imaging Component (Win7+).
inline ID3D11ShaderResourceView* createTextureFromRGBA(
    ID3D11Device* device,const unsigned char* bytes,UINT w,UINT h) {
    if(!device||!bytes||!w||!h)return 0;
    D3D11_TEXTURE2D_DESC td={};
    td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;
    td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data={};data.pSysMem=bytes;data.SysMemPitch=w*4;
    ID3D11Texture2D* texture=0;
    if(FAILED(device->CreateTexture2D(&td,&data,&texture)))return 0;
    ID3D11ShaderResourceView* srv=0;
    device->CreateShaderResourceView(texture,0,&srv);
    texture->Release();return srv;
}
inline ID3D11ShaderResourceView* loadWIC(
    ID3D11Device* device,const wchar_t* path,
    const unsigned char* memory=0,UINT memoryBytes=0) {
    IWICImagingFactory* factory=0;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,0,CLSCTX_INPROC_SERVER,
                              IID_IWICImagingFactory,(void**)&factory)))return 0;
    IWICBitmapDecoder* decoder=0;
    IWICStream* stream=0;
    HRESULT hr=E_FAIL;
    if(memory && memoryBytes) {
        hr=factory->CreateStream(&stream);
        if(SUCCEEDED(hr))hr=stream->InitializeFromMemory((BYTE*)memory,memoryBytes);
        if(SUCCEEDED(hr))hr=factory->CreateDecoderFromStream(stream,0,WICDecodeMetadataCacheOnDemand,&decoder);
    } else if(path) {
        hr=factory->CreateDecoderFromFilename(path,0,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
    }
    IWICBitmapFrameDecode* frame=0;
    IWICFormatConverter* converter=0;
    UINT w=0,h=0;
    if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);
    if(SUCCEEDED(hr))hr=frame->GetSize(&w,&h);
    if(SUCCEEDED(hr) && w>0 && h>0 && w<=8192 && h<=8192)
        hr=factory->CreateFormatConverter(&converter);
    else hr=E_FAIL;
    if(SUCCEEDED(hr))hr=converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,
                                               WICBitmapDitherTypeNone,0,0,WICBitmapPaletteTypeCustom);
    ID3D11ShaderResourceView* srv=0;
    if(SUCCEEDED(hr)) {
        std::vector<unsigned char> pixels(size_t(w)*h*4);
        hr=converter->CopyPixels(0,w*4,(UINT)pixels.size(),&pixels[0]);
        if(SUCCEEDED(hr))srv=createTextureFromRGBA(device,&pixels[0],w,h);
    }
    if(converter)converter->Release();
    if(frame)frame->Release();
    if(decoder)decoder->Release();
    if(stream)stream->Release();
    factory->Release();
    return srv;
}
inline std::wstring utf8ToWide(const std::string& utf8) {
    int len=MultiByteToWideChar(CP_UTF8,0,utf8.c_str(),-1,0,0);
    if(len<=0)return std::wstring(utf8.begin(),utf8.end());
    std::vector<wchar_t> buffer(len);
    MultiByteToWideChar(CP_UTF8,0,utf8.c_str(),-1,&buffer[0],len);
    return std::wstring(&buffer[0]);
}
